# Platform support

## Release target

The packaged plug-in supports Windows 10/11 x64 and a 64-bit VST3 host. It has
been tested with the Steinberg VST3 validator. `x64`, `x86_64`, and `AMD64` name
the same architecture; both Intel and AMD processors are supported.

Windows ARM64, macOS, and Linux are not supported. Logic Pro does not load VST3
plug-ins.

## Build boundaries

- `loopback_bridge.cpp` uses Winsock and `GetEnvironmentVariableW`.
- CMake links `ws2_32`, embeds a Win32 resource, and rejects non-Windows builds.
- Build, install, and package scripts target Visual Studio x64 and Windows VST3 directories.
- The standalone Python bridge includes native media dependencies for its target OS and CPU.
