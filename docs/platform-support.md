# Platform support

## Current release target

| Operating system | CPU / host architecture | Status | Notes |
| --- | --- | --- | --- |
| Windows 10/11 | x86_64 / AMD64 | Supported and packaged | The same binary runs on 64-bit Intel and AMD processors. Tested with the Steinberg VST3 validator. |
| Windows 10/11 | ARM64 | Not supported | Requires an ARM64 VST3 build, ARM64 Python/aiortc/av runtime, and a native ARM64 DAW validation pass. An x64 plug-in can only be considered under an x64 host/emulation path; an ARM64 host cannot load it as a native plug-in. |
| macOS | Intel x86_64 | Not supported | Requires a POSIX loopback transport, macOS bundle resources, codesigning, notarization, and an Intel bridge-service build. |
| macOS | Apple Silicon arm64 | Not supported | Requires the macOS work above plus arm64-native media dependencies and host validation. A universal2 VST3 bundle is the preferred distribution. |
| Linux | x86_64 / ARM64 | Not supported | Requires a POSIX loopback transport and native VST3/bridge-service builds for the chosen distribution. |

`x64`, `x86_64`, and `AMD64` describe the same instruction-set family for this package;
the name does not restrict it to AMD processors.

## Portability boundaries

The audio ring buffer, framed loopback protocol, VST3 processor/controller, and Python
media bridge are structurally portable. The current implementation is still tied to
Windows in four places:

- `loopback_bridge.cpp` uses Winsock and `GetEnvironmentVariableW`.
- CMake links `ws2_32`, embeds a Win32 resource, and rejects non-Windows builds.
- Build/install/package scripts target Visual Studio x64 and Windows VST3 directories.
- The standalone Python bridge service contains native wheels and must be built separately on
  each operating system and CPU architecture.

The next portability step is to split the local transport into Windows and POSIX
adapters selected by CMake, then add native CI builders. macOS artifacts must be built
and signed on macOS; they should not be presented as supported based only on a cross
compile.

Logic Pro does not load VST3 plug-ins. Supporting Logic requires a separate Audio Unit
target even after the macOS transport and bridge service are available.
