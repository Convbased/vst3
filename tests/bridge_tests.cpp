#include "audio_ring_buffer.hpp"
#include "audio_routing.hpp"
#include "bridge_protocol.hpp"
#include "loopback_bridge.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using convbased::bridge::BridgeConfig;
using convbased::bridge::BridgeState;
using convbased::bridge::LoopbackBridge;
using convbased::bridge::SpscFloatRingBuffer;
using convbased::bridge::protocol::FrameHeader;
using convbased::bridge::protocol::MessageType;
using convbased::vst3::interpolateTransition;
using convbased::vst3::selectRoutingGains;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void testRingBufferWrapsWithoutReordering() {
    SpscFloatRingBuffer<8> ring;
    const std::array first {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    std::array<float, 4> prefix {};
    require(ring.push(first), "initial ring push failed");
    require(ring.pop(prefix), "initial ring pop failed");
    require(prefix == std::array {1.0F, 2.0F, 3.0F, 4.0F}, "ring prefix was reordered");

    const std::array second {7.0F, 8.0F, 9.0F, 10.0F, 11.0F, 12.0F};
    std::array<float, 8> result {};
    require(ring.push(second), "wrapped ring push failed");
    require(ring.pop(result), "wrapped ring pop failed");
    require(result == std::array {5.0F, 6.0F, 7.0F, 8.0F, 9.0F, 10.0F, 11.0F, 12.0F},
            "ring wrap changed sample order");
}

void testRingBufferCanDiscardStaleSamples() {
    SpscFloatRingBuffer<8> ring;
    const std::array input {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    require(ring.push(input), "ring rejected samples before stale discard");
    require(ring.discardOldest(4) == 4, "ring discarded the wrong sample count");

    std::array<float, 2> output {};
    require(ring.pop(output), "ring could not read after stale discard");
    require(output == std::array {5.0F, 6.0F}, "ring did not retain the newest samples");
}

void testProtocolRoundTrip() {
    const FrameHeader inputHeader {MessageType::audioInput, 1'024, 99};
    const auto wireHeader = convbased::bridge::protocol::encodeHeader(inputHeader);
    FrameHeader outputHeader {};
    require(convbased::bridge::protocol::decodeHeader(wireHeader, outputHeader),
            "frame header did not decode");
    require(outputHeader.type == inputHeader.type &&
                outputHeader.payloadBytes == inputHeader.payloadBytes &&
                outputHeader.sequence == inputHeader.sequence,
            "frame header changed during round trip");

    const convbased::bridge::protocol::Hello inputHello {48'000, 2, 512};
    const auto wireHello = convbased::bridge::protocol::encodeHello(inputHello);
    convbased::bridge::protocol::Hello outputHello {};
    require(convbased::bridge::protocol::decodeHello(wireHello, outputHello),
            "HELLO did not decode");
    require(outputHello.sampleRate == inputHello.sampleRate &&
                outputHello.channels == inputHello.channels &&
                outputHello.maxBlockFrames == inputHello.maxBlockFrames,
            "HELLO changed during round trip");
}

void testRoutingBlocksOriginalUnlessBypassed() {
    const auto waiting = selectRoutingGains(false, false, 1.0);
    require(waiting.original == 0.0F && waiting.converted == 0.0F,
            "waiting state leaked original audio");

    const auto connected = selectRoutingGains(false, true, 0.25);
    require(connected.original == 0.0F && connected.converted == 0.25F,
            "active conversion mixed original audio");

    const auto bypassed = selectRoutingGains(true, true, 1.0);
    require(bypassed.original == 1.0F && bypassed.converted == 0.0F,
            "Bypass did not restore original audio exclusively");
}

void testDeclickTransitionReachesTargetSmoothly() {
    require(interpolateTransition(1.0F, 0.0F, 0, 4) == 1.0F,
            "declick transition changed its starting sample");
    require(interpolateTransition(1.0F, 0.0F, 2, 4) == 0.5F,
            "declick transition did not interpolate its midpoint");
    require(interpolateTransition(1.0F, 0.0F, 4, 4) == 0.0F,
            "declick transition did not reach silence");
}

bool receiveExact(SOCKET socketHandle, std::span<std::byte> destination) {
    std::size_t received = 0;
    while (received < destination.size()) {
        const auto result = ::recv(socketHandle,
                                   reinterpret_cast<char*>(destination.data() + received),
                                   static_cast<int>(destination.size() - received), 0);
        if (result <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(result);
    }
    return true;
}

bool sendExact(SOCKET socketHandle, std::span<const std::byte> source) {
    std::size_t sent = 0;
    while (sent < source.size()) {
        const auto result = ::send(socketHandle,
                                   reinterpret_cast<const char*>(source.data() + sent),
                                   static_cast<int>(source.size() - sent), 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

void testLoopbackTransport() {
    WSADATA winsockData {};
    require(::WSAStartup(MAKEWORD(2, 2), &winsockData) == 0, "WSAStartup failed in test");

    const auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(listener != INVALID_SOCKET, "test listener socket failed");
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
            "test listener bind failed");
    require(::listen(listener, 1) == 0, "test listener listen failed");
    int addressLength = sizeof(address);
    require(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressLength) == 0,
            "test listener getsockname failed");
    const auto port = ntohs(address.sin_port);

    std::atomic<bool> releaseServer {false};
    std::exception_ptr serverFailure;
    std::jthread server([&] {
        try {
            const auto client = ::accept(listener, nullptr, nullptr);
            require(client != INVALID_SOCKET, "test server accept failed");

            std::array<std::byte, convbased::bridge::protocol::kHeaderBytes> headerBytes {};
            require(receiveExact(client, headerBytes), "test server did not receive HELLO header");
            FrameHeader header {};
            require(convbased::bridge::protocol::decodeHeader(headerBytes, header),
                    "test server received invalid HELLO header");
            require(header.type == MessageType::hello, "first bridge frame was not HELLO");
            std::array<std::byte, convbased::bridge::protocol::kHelloBytes> helloBytes {};
            require(header.payloadBytes == helloBytes.size() && receiveExact(client, helloBytes),
                    "test server did not receive HELLO payload");

            require(receiveExact(client, headerBytes), "test server did not receive AUTH header");
            require(convbased::bridge::protocol::decodeHeader(headerBytes, header),
                    "test server received invalid AUTH header");
            require(header.type == MessageType::authenticate,
                    "second bridge frame was not AUTH");
            std::vector<std::byte> apiKeyBytes(header.payloadBytes);
            require(header.payloadBytes <= convbased::bridge::protocol::kMaxApiKeyBytes &&
                        receiveExact(client, apiKeyBytes),
                    "test server did not receive AUTH payload");
            const std::string apiKey(reinterpret_cast<const char*>(apiKeyBytes.data()),
                                     apiKeyBytes.size());
            require(apiKey == "test-api-key", "bridge changed the API key payload");

            require(receiveExact(client, headerBytes), "test server did not receive audio header");
            require(convbased::bridge::protocol::decodeHeader(headerBytes, header),
                    "test server received invalid audio header");
            require(header.type == MessageType::audioInput, "bridge did not send AUDIO_INPUT");
            std::array<std::byte, 480U * 2U * sizeof(float)> payload {};
            require(header.payloadBytes == payload.size() && receiveExact(client, payload),
                    "test server did not receive expected audio payload");

            for (std::uint32_t response = 0; response < 12; ++response) {
                const auto responseHeader = convbased::bridge::protocol::encodeHeader(
                    {MessageType::audioOutput, header.payloadBytes, header.sequence + response});
                require(sendExact(client, responseHeader) && sendExact(client, payload),
                        "test server could not prime converted audio");
            }
            const auto releaseDeadline = std::chrono::steady_clock::now() + 3s;
            while (!releaseServer.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < releaseDeadline) {
                std::this_thread::sleep_for(1ms);
            }
            ::shutdown(client, SD_BOTH);
            ::closesocket(client);
        } catch (...) {
            serverFailure = std::current_exception();
        }
    });

    auto bridge = std::make_unique<LoopbackBridge>();
    require(bridge->start(BridgeConfig {48'000, 512, 2, port}), "bridge thread did not start");
    const auto credentialsDeadline = std::chrono::steady_clock::now() + 2s;
    while (bridge->state() != BridgeState::waitingForCredentials &&
           std::chrono::steady_clock::now() < credentialsDeadline) {
        std::this_thread::sleep_for(2ms);
    }
    require(bridge->state() == BridgeState::waitingForCredentials,
            "bridge did not wait for an API key");
    require(!bridge->setApiKey(""), "bridge accepted an empty API key");
    require(!bridge->setApiKey(std::string(
                convbased::bridge::protocol::kMaxApiKeyBytes + 1, 'x')),
            "bridge accepted an oversized API key");
    require(bridge->setApiKey("test-api-key"), "bridge rejected a valid API key");
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (bridge->state() != BridgeState::connected && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }
    require(bridge->state() == BridgeState::connected, "bridge did not connect to loopback server");

    std::array<float, 480U * 2U> input {};
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = static_cast<float>(index) / static_cast<float>(input.size());
    }
    require(bridge->pushInput(input), "bridge rejected input audio");

    const auto expectedOutputSamples = input.size() * 12;
    while (bridge->queuedOutputSamples() < expectedOutputSamples &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }
    require(bridge->queuedOutputSamples() == expectedOutputSamples,
            "bridge did not retain the complete converted-audio burst");

    std::array<float, 480U * 2U> output {};
    std::size_t receivedBlocks = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (bridge->tryPopOutput(output)) {
            require(output == input, "loopback bridge changed PCM samples");
            ++receivedBlocks;
            if (receivedBlocks == 12) {
                break;
            }
        } else {
            std::this_thread::sleep_for(2ms);
        }
    }
    require(receivedBlocks == 12, "bridge discarded a recoverable converted-audio burst");
    require(bridge->droppedOutputSamples() == 0,
            "bridge counted a recoverable converted-audio burst as stale");

    releaseServer.store(true, std::memory_order_release);
    bridge->stop();
    server.join();
    ::closesocket(listener);
    ::WSACleanup();
    if (serverFailure) {
        std::rethrow_exception(serverFailure);
    }
}

} // namespace

int main() {
    try {
        testRingBufferWrapsWithoutReordering();
        testRingBufferCanDiscardStaleSamples();
        testProtocolRoundTrip();
        testRoutingBlocksOriginalUnlessBypassed();
        testDeclickTransitionReachesTargetSmoothly();
        testLoopbackTransport();
        std::cout << "All bridge tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
