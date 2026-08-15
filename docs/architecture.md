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
the loopback session handshake. The clear action securely erases the in-memory copy and
disconnects the bridge. The key is never written to disk, processor/controller state,
parameter automation, presets, or DAW project serialization.

Each bridge-service session uses that key to read `myDeviceProfile` from the SDK's default
GraphQL endpoint. The profile supplies the model, conversion preferences, and real-time
enable switch. It is polled while the bridge runs; a changed profile closes the current
WebRTC session so the plug-in's loopback worker reconnects with the new configuration. A
missing model or disabled real-time switch leaves the local bridge waiting without
opening a cloud media session.

The bridge service also attempts `me.wallet_balance` with the same API key. It sends the result,
device label, and model readiness back through an `ACCOUNT_STATUS` frame. The bridge
worker stores the latest snapshot, while the editor requests it through non-parameter VST
messages on the UI thread. If the service's API-key scope policy blocks `me`, the snapshot
marks balance as checked but unavailable; profile authentication and audio continue.

## Real-time constraints

- `process()` performs no allocation, locking, socket I/O, logging, or process launch.
- The plug-in preallocates scratch memory during `setupProcessing()` / `setActive()`.
- Two single-producer/single-consumer rings isolate the DAW thread from transport stalls.
- TCP runs only over loopback with `TCP_NODELAY`; reconnection is handled by the worker.
- If converted audio is unavailable, the plug-in emits silence. Only Bypass passes the
  original input.
- Input/output queues are bounded. Overflow is dropped rather than increasing latency
  without limit.
- Local input and bridge-service PCM queues keep at most about 60 ms of recent audio. The
  converted-output queue accepts up to 200 ms of short network bursts so TURN jitter is
  not mistaken for stale audio; sustained excess is still discarded.
- Converted output primes about 40 ms before playback and re-primes after an underrun.
  A 3 ms transition suppresses click and pop artifacts without passing original audio.
- The bridge service defaults aiortc audio jitter prefetch to three 20 ms frames. Use a lower
  `--jitter-prefetch` only when the network is stable enough to prioritize latency.

## Latency semantics

`getLatencySamples()` returns zero because the native plug-in contains no fixed
look-ahead or deterministic block delay. This does **not** mean the converted signal is
zero-latency: WebRTC packetization, network RTT, model block/crossfade/extra-time
settings, and DAW/ASIO buffers remain. Variable remote latency cannot be represented
correctly as a fixed VST delay-compensation value.

For live monitoring, use a wired network, 48 kHz, a small ASIO buffer, and the smallest
model settings that remain artifact-free. Converted Level changes only the processed
return gain; it never blends the delayed signal with the original.

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
printable ASCII API key of at most 512 bytes. Other message types are `AUDIO_OUTPUT`,
`STATUS`, and `ACCOUNT_STATUS`; audio payloads are interleaved IEEE-754 float32 PCM. The
credential handshake is unencrypted and therefore the native transport connects only to
`127.0.0.1`. Keys are never included in logs or state files.

`ACCOUNT_STATUS` starts with a 16-byte little-endian payload header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | account-status version (`1`) |
| 2 | 2 | checked/authenticated/balance/profile flags |
| 4 | 8 | IEEE-754 float64 balance |
| 12 | 2 | UTF-8 device-label byte count |
| 14 | 2 | UTF-8 model-ID byte count |

The two bounded strings follow the header. The balance value is meaningful only when the
balance-available flag is set.
