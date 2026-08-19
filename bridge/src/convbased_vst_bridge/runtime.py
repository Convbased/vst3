from __future__ import annotations

import asyncio
import contextlib
import json
import logging
import sys
from array import array
from dataclasses import dataclass
from typing import Any

from .audio import PcmReservoir, PluginAudioTrack, interleave_mono
from .profile import DeviceProfileMonitor, ProfileSnapshot
from .protocol import (
    MessageType,
    ProtocolError,
    decode_auth,
    decode_hello,
    read_message,
    write_message,
)

_log = logging.getLogger("convbased-vst-bridge")


@dataclass(frozen=True, slots=True)
class BridgeOptions:
    signaling_url: str | None = None
    graphql_url: str | None = None
    host: str = "127.0.0.1"
    port: int = 43_127


class BridgeSession:
    def __init__(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
        options: BridgeOptions,
    ) -> None:
        self.reader = reader
        self.writer = writer
        self.options = options
        self.write_lock = asyncio.Lock()
        self.sequence = 0
        self.client: Any = None

    async def run(self) -> None:
        first = await read_message(self.reader)
        if first.type != MessageType.HELLO:
            raise ProtocolError("the first frame must be HELLO")
        hello = decode_hello(first.payload)
        authentication = await read_message(self.reader)
        if authentication.type != MessageType.AUTH:
            raise ProtocolError("the second frame must be AUTH")
        api_key = decode_auth(authentication.payload)
        _log.info(
            "plug-in connected: %s Hz, %s channel(s), max block %s",
            hello.sample_rate,
            hello.channels,
            hello.max_block_frames,
        )

        try:
            from convbased import DEFAULT_GRAPHQL_URL
        except ModuleNotFoundError as exc:
            raise RuntimeError(
                "Convbased Python SDK is not installed; run scripts/setup-bridge.ps1"
            ) from exc

        self.api_key = api_key
        self.profile_monitor = DeviceProfileMonitor(
            graphql_url=self.options.graphql_url or DEFAULT_GRAPHQL_URL,
            api_key=api_key,
        )
        try:
            await self.profile_monitor.start()
        except Exception:
            await self.profile_monitor.close()
            self.api_key = ""
            raise

        reservoir = PcmReservoir(hello.channels, max_samples=hello.sample_rate * 2)
        source = PluginAudioTrack(reservoir, hello.sample_rate)
        input_task = asyncio.create_task(
            self._read_audio_input(reservoir), name="plugin-input"
        )
        profile_task = asyncio.create_task(
            self.profile_monitor.wait_for_runnable(), name="device-profile-ready"
        )
        client: Any = None
        connect_task: asyncio.Task[None] | None = None
        output_task: asyncio.Task[None] | None = None
        profile_change_task: asyncio.Task[ProfileSnapshot] | None = None
        try:
            snapshot = self.profile_monitor.snapshot
            if snapshot.profile is None:
                detail = "bind a model to this API key in the web console"
            elif not snapshot.profile.realtime_enabled:
                detail = "real-time conversion is disabled in the web console"
            elif not snapshot.profile.model_id:
                detail = "bind a model to this API key in the web console"
            else:
                detail = None
            if detail is not None:
                await self._send_status("waiting", detail)

            done, _pending = await asyncio.wait(
                {input_task, profile_task}, return_when=asyncio.FIRST_COMPLETED
            )
            if input_task in done:
                input_task.result()
            snapshot = profile_task.result()
            profile = snapshot.profile
            if profile is None or not profile.model_id:
                raise RuntimeError("device profile became unavailable")

            await self._send_status("connecting", "negotiating Convbased WebRTC")
            client = self._create_client()
            self.client = client
            loop = asyncio.get_running_loop()
            remote_track: asyncio.Future[Any] = loop.create_future()

            def on_track(track: Any) -> None:
                if not remote_track.done():
                    remote_track.set_result(track)

            client.on("track", on_track)
            client.on(
                "state",
                lambda state, previous: _log.info(
                    "sdk state: %s -> %s", previous, state
                ),
            )
            client.on("error", lambda error: _log.error("sdk error: %s", error))

            connect_task = asyncio.create_task(
                client.connect(
                    model_id=profile.model_id,
                    audio=source,
                    preferences=profile.preferences,
                    # Opus RTP is decoded at 48 kHz by the service. Keep the model
                    # configuration aligned with that stream and resample only at
                    # the local DAW boundary.
                    sample_rate=48_000,
                ),
                name="convbased-connect",
            )
            done, _pending = await asyncio.wait(
                {connect_task, remote_track}, return_when=asyncio.FIRST_COMPLETED
            )
            if connect_task in done:
                connect_task.result()
            track = await remote_track
            output_task = asyncio.create_task(
                self._write_converted_audio(track, hello.sample_rate, hello.channels),
                name="converted-output",
            )
            await connect_task
            await self._send_status("ready", "converted audio is flowing")
            profile_change_task = asyncio.create_task(
                self.profile_monitor.wait_for_change(snapshot.version),
                name="device-profile-change",
            )
            done, _pending = await asyncio.wait(
                {input_task, output_task, profile_change_task},
                return_when=asyncio.FIRST_COMPLETED,
            )
            if profile_change_task in done:
                await self._send_status(
                    "reconfiguring", "web-console configuration changed"
                )
                return
            for task in (input_task, output_task):
                if task in done:
                    task.result()
        finally:
            for task in (
                input_task,
                profile_task,
                connect_task,
                output_task,
                profile_change_task,
            ):
                if task is not None and not task.done():
                    task.cancel()
            for task in (
                input_task,
                profile_task,
                connect_task,
                output_task,
                profile_change_task,
            ):
                if task is not None:
                    with contextlib.suppress(asyncio.CancelledError, Exception):
                        await task
            if client is not None:
                with contextlib.suppress(Exception):
                    await client.disconnect()
            self.client = None
            await self.profile_monitor.close()
            self.api_key = ""

    def _create_client(self) -> Any:
        try:
            from convbased import ConvbasedClient
        except ModuleNotFoundError as exc:
            raise RuntimeError(
                "Convbased Python SDK is not installed; run scripts/setup-bridge.ps1"
            ) from exc

        kwargs: dict[str, Any] = {
            "api_key": self.api_key,
        }
        if self.options.signaling_url:
            kwargs["signaling_url"] = self.options.signaling_url
        if self.options.graphql_url:
            kwargs["graphql_url"] = self.options.graphql_url
        return ConvbasedClient(**kwargs)

    async def _read_audio_input(self, reservoir: PcmReservoir) -> None:
        while True:
            message = await read_message(self.reader)
            if message.type != MessageType.AUDIO_INPUT:
                raise ProtocolError(f"unexpected plug-in message: {message.type.name}")
            reservoir.append_interleaved_bytes(message.payload)

    async def _write_converted_audio(
        self, track: Any, sample_rate: int, channels: int
    ) -> None:
        from av import AudioResampler

        resampler = AudioResampler(format="flt", layout="mono", rate=sample_rate)
        while True:
            frame = await track.recv()
            converted_frames = resampler.resample(frame)
            for converted in converted_frames:
                byte_count = converted.samples * 4
                mono = array("f")
                mono.frombytes(bytes(converted.planes[0])[:byte_count])
                if sys.byteorder != "little":
                    mono.byteswap()
                interleaved = interleave_mono(mono, channels)
                max_values = 4_096 * channels
                for offset in range(0, len(interleaved), max_values):
                    part = interleaved[offset : offset + max_values]
                    if sys.byteorder != "little":
                        part.byteswap()
                    await self._send(MessageType.AUDIO_OUTPUT, part.tobytes())

    async def _send_status(self, state: str, message: str) -> None:
        payload = json.dumps(
            {"state": state, "message": message}, separators=(",", ":")
        ).encode()
        await self._send(MessageType.STATUS, payload)

    async def _send(self, message_type: MessageType, payload: bytes) -> None:
        sequence = self.sequence
        self.sequence = (self.sequence + 1) & 0xFFFF_FFFF
        await write_message(
            self.writer, self.write_lock, message_type, sequence, payload
        )


class BridgeServer:
    def __init__(self, options: BridgeOptions) -> None:
        self.options = options

    async def serve(self) -> None:
        server = await asyncio.start_server(
            self._handle_client, self.options.host, self.options.port
        )
        addresses = ", ".join(
            str(sock.getsockname()) for sock in server.sockets or []
        )
        _log.info("listening on %s", addresses)
        async with server:
            await server.serve_forever()

    async def _handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        peer = writer.get_extra_info("peername")
        try:
            await BridgeSession(reader, writer, self.options).run()
        except asyncio.IncompleteReadError:
            _log.info("plug-in disconnected: %s", peer)
        except Exception:  # noqa: BLE001 - isolate one failed plug-in session
            _log.exception("bridge session failed: %s", peer)
        finally:
            writer.close()
            with contextlib.suppress(Exception):
                await writer.wait_closed()
