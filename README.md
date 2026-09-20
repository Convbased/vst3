# Convbased Bridge VST3

Windows x64 VST3 effect for Convbased real-time voice conversion. A local Python bridge service connects the plug-in to the Convbased SDK.

Prebuilt Windows x64 packages are published on the [Releases](https://github.com/Convbased/vst3/releases) page. Installation and use are covered in the [Convbased VST3 guide](https://docs.weights.chat/vst3/).

Building requires Visual Studio C++ Build Tools, CMake and Python. Supported versions and platforms are listed in [docs/platform-support.md](docs/platform-support.md).

## Build and test

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test.ps1 -Configuration Release
.\scripts\install-plugin.ps1 -Configuration Release
```

Rescan plug-ins in the DAW and insert **Convbased Bridge** on an audio track.

For FL Studio debugging, use [scripts/debug-flstudio.ps1](scripts/debug-flstudio.ps1). Installing the system VST3 copy requires administrator approval.

## Set up the bridge service

```powershell
.\scripts\setup-bridge.ps1
.\scripts\run-bridge.ps1
```

The setup script installs the Convbased Python SDK bundled in [third_party/convbased-sdk-python](third_party/convbased-sdk-python).

## Use

Start the bridge service and enter the API key in the plug-in editor. Bind a model and enable real-time conversion for that key in the Convbased web console.

The key is kept only for the current session. If you change the bridge port, configure the same port in the plug-in and bridge service.

Output is silent while converted audio is unavailable. Bypass passes the original input. Audio processing and latency are described in [docs/architecture.md](docs/architecture.md).

## Package

Use [scripts/package-windows.ps1](scripts/package-windows.ps1) to create a test archive under `out/`.

## License

Code is licensed under the [MIT License](LICENSE). VST is a trademark of Steinberg Media Technologies GmbH.
