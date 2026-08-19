# Architecture

## Audio path

```text
DAW audio block
    -> VST3 process()                         (real-time thread)
    -> preallocated SPSC input ring
    -> loopback bridge worker                 (background thread)
    -> 127.0.0.1 framed float32 PCM
    -> Python bridge service + Convbased SDK
    -> WebRTC / Opus / Convbased model
    -> Python bridge service
    -> 127.0.0.1 framed float32 PCM
    -> preallocated SPSC output ring
    -> VST3 process()
    -> DAW track
```

The DAW supplies and consumes every audio block. The bridge service never opens a Windows
capture or playback device, so this path does not add a WASAPI/MME/DirectSound device
loop. The DAW still reaches the physical interface through its configured driver,
preferably ASIO with a small stable buffer.

The bridge service lets aiortc resample the host-rate source to the 48 kHz Opus RTP clock and
configures the Convbased model at 48 kHz. Converted 48 kHz audio is resampled back to the
DAW session rate before it is returned to the plug-in.

## Control plane

The user enters the API key in the plug-in's masked editor field. The controller sends it
to the processor as a non-parameter VST message, and the background bridge includes it in
the loopback session handshake. It is deliberately excluded from processor/controller
state, parameter automation, presets, and DAW project serialization.

Each bridge-service session uses that key to read `myDeviceProfile` from the SDK's default
GraphQL endpoint. The profile supplies the model, conversion preferences, and real-time
enable switch. It is polled while the bridge runs; a changed profile closes the current
WebRTC session so the plug-in's loopback worker reconnects with the new configuration. A
missing model or disabled real-time switch leaves the local bridge waiting without
opening a cloud media session.

## Real-time constraints

- `process()` performs no allocation, locking, socket I/O, logging, or process launch.
- The plug-in preallocates scratch memory during `setupProcessing()` / `setActive()`.
- Two single-producer/single-consumer rings isolate the DAW thread from transport stalls.
- TCP runs only over loopback with `TCP_NODELAY`; reconnection is handled by the worker.
- If converted audio is unavailable, the plug-in emits dry input instead of blocking.
- Input/output queues are bounded. Overflow is dropped rather than increasing latency
  without limit.

## Latency semantics

`getLatencySamples()` returns zero because the native plug-in contains no fixed
look-ahead or deterministic block delay. This does **not** mean the converted signal is
zero-latency: WebRTC packetization, network RTT, model block/crossfade/extra-time
settings, and DAW/ASIO buffers remain. Variable remote latency cannot be represented
correctly as a fixed VST delay-compensation value.

For live monitoring, use 100% wet, a wired network, 48 kHz, a small ASIO buffer, and the
smallest model settings that remain artifact-free. Dry/wet mixing can comb-filter because
the converted signal is delayed relative to dry audio.

## Local bridge protocol

All integers are little-endian. Each frame starts with a 16-byte header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `CBV3` |
| 4 | 2 | protocol version (`2`) |
| 6 | 2 | message type |
| 8 | 4 | payload byte count |
| 12 | 4 | sequence number |

The plug-in sends `HELLO`, then `AUTH`, before any `AUDIO_INPUT`. `AUTH` contains a
printable ASCII API key of at most 512 bytes. Other message types are `AUDIO_OUTPUT` and
`STATUS`; audio payloads are interleaved IEEE-754 float32 PCM. The credential handshake
is unencrypted and therefore the native transport connects only to `127.0.0.1`. Keys are
never included in logs or state files.
