# Convbased VST bridge service

This package connects the local PCM stream emitted by `ConvbasedVST3` to the
Convbased Python SDK. Install it through the repository's
`scripts/setup-bridge.ps1` script.

Start the bridge service, then paste the API key into the masked field in
the VST editor. The plug-in passes it over the loopback-only session handshake. The
model, conversion preferences, and real-time enable switch are loaded from the device
profile bound to that key in the Convbased web console. The credential is held only in
process memory and is never serialized into plug-in state or the DAW project.
