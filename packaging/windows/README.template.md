# Convbased Bridge VST3 {{VERSION}} test package

Platform: Windows 10/11 x64 (AMD64, Intel or AMD processor)

Commit: `{{COMMIT}}`

This is an unsigned pre-release test build. Windows may display a SmartScreen warning.

## Install and run

1. Extract the entire ZIP to a writable directory.
2. In PowerShell, run `./Install.ps1`.
3. In the Convbased web console, bind the API key to a model and enable real-time conversion.
4. Start the local bridge service:

   ```powershell
   ./Start-Bridge.ps1
   ```

5. Keep the bridge-service window open, rescan VST3 plug-ins in the DAW, and insert
   **Convbased Bridge** on the source track.
6. Open the plug-in editor, paste the key into the masked **API Key** field, and select
   **连接**. Select **清除** to erase the session copy and disconnect.

For live monitoring, start with a 48 kHz DAW session, 100% wet mix, and an ASIO buffer
of 128 or 256 samples. Converted output stays silent until fresh converted audio arrives;
the plug-in never passes original audio through as a fallback.

The plug-in and bridge service retain the API key only for the current process session. They
never write it to disk, plug-in state, presets, or the DAW project. The bridge service receives
the key over the loopback-only session handshake.

Model selection, conversion preferences, and the real-time switch come from the backend
profile bound to the key. The editor reports API Key validation, device-profile readiness,
and the current balance when the service permits API-key balance queries.

## Package contents

- `Plugin/ConvbasedVST3.vst3`: native Windows x64 VST3 bundle.
- `BridgeService/ConvbasedVSTBridge`: standalone Windows x64 bridge service and runtime libraries.
- `Install.ps1`: per-user VST3 installer.
- `Start-Bridge.ps1`: bridge-service launcher; no credential environment variable required.
- `SHA256SUMS.txt`: checksums for the plug-in module and bridge-service executable.

This package does not support Windows ARM64 or macOS. See the repository's
`docs/platform-support.md` for the exact support matrix.
