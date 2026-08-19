# Convbased Bridge VST3

Windows VST3 effect that feeds DAW audio directly into the Convbased real-time SDK and
returns converted audio to the same insert. It avoids the extra capture/playback-device
loop normally needed by a standalone voice changer.

The plug-in itself adds no fixed sample delay and reports `0` latency samples. The total
converted path is **not zero-latency**: DAW/ASIO buffers, 20 ms WebRTC audio frames,
network RTT, and model block/crossfade settings still apply.

For installation, bridge-service configuration, latency semantics, validation, and platform
boundaries, see the [Convbased VST3 guide](https://docs.weights.chat/vst3/).

## What is included

- Native C++20 VST3 effect built with Steinberg VST3 SDK 3.8.0.
- Mono and stereo host layouts; stereo input is downmixed for voice inference and the
  converted mono track is duplicated back to stereo.
- Real-time-safe DAW callback: no locks, allocations, Python, or socket calls.
- Bounded background loopback bridge with dry fallback and automatic reconnect.
- Local Python bridge service for Convbased real-time processing.
- C++ protocol/ring/transport tests and Python protocol/audio tests.

See [docs/architecture.md](docs/architecture.md) for the thread model and latency
semantics.

## Requirements

- Windows 10/11 x64.
- Visual Studio 2022 or 2026 Build Tools with **Desktop development with C++** and CMake.
- Python 3.10+ for the bridge service.
- An API key with the `realtime` scope.
- A VST3 host. For monitoring, use the host's ASIO driver when available.

## Build and test

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test.ps1 -Configuration Release
```

The first configure downloads the pinned MIT-licensed Steinberg SDK. It does not create
system-wide symbolic links.

Install the built bundle into the current user's standard VST3 directory:

```powershell
.\scripts\install-plugin.ps1 -Configuration Release
```

Then rescan plug-ins in the DAW and insert **Convbased Bridge** on an audio track.

Create a self-contained Windows x64 test ZIP (plug-in, packaged bridge service, installer,
launcher, and checksums):

```powershell
.\scripts\package-windows.ps1 -Version 0.1.0-alpha.2
```

## Configure the local bridge service

Set up the bridge service from this repository:

```powershell
.\scripts\setup-bridge.ps1
.\scripts\run-bridge.ps1
```

Bind the API key to a model and conversion preferences in the Convbased web console.
Then open the plug-in editor, paste the key into the masked **API Key** field, and select
**Apply key**. The bridge service reads that device profile for the plug-in session and polls it
while running. No model or voice parameter is configured on the plug-in machine. When
real-time conversion is disabled in the console, the local bridge stays available
without opening a billable WebRTC session.

For local SDK development, an alternate Python package directory can still be supplied:

```powershell
.\scripts\setup-bridge.ps1 -SdkPath 'D:\src\convbased-sdk\packages\sdk-python'
```

Start the bridge service before applying the key. Re-enter the key after reopening a DAW
project. The bridge service listens only
on `127.0.0.1:43127`. To use another port, set `CONVBASED_VST_BRIDGE_PORT` for the DAW
and pass the same port to `run-bridge.ps1` before loading the plug-in.

The custom editor exposes:

- **API Key**: masked, session-only credential entry with an explicit apply action.
- **Mix**: defaults to 100% converted audio. Keep it at 100% for live voice monitoring.
- **Bypass**: dry input while the transport continues running.

## Current scope

This first repository version targets Windows x64 and the existing Python SDK. A live
end-to-end session requires service credentials and therefore is not part of the offline
test suite. The Python SDK in the current workspace is marked as a protocol skeleton, so
live service validation remains a release gate before distributing binaries.

Windows x64/AMD64 covers both Intel and AMD 64-bit processors. Windows ARM64, macOS
Intel, macOS Apple Silicon, and Linux are not yet supported; their concrete build and
runtime boundaries are documented in [docs/platform-support.md](docs/platform-support.md).

## License

MIT. VST is a trademark of Steinberg Media Technologies GmbH.
