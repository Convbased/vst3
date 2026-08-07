from __future__ import annotations

import asyncio
import contextlib
import json
import logging
import sys
import time
from array import array
from dataclasses import dataclass
from typing import Any

from .audio import (
    PcmReservoir,
    PluginAudioTrack,
    interleave_mono,
    set_audio_jitter_prefetch,
)
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
    jitter_prefetch: int = 3


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
        self.output_samples = 0
        self.output_max_gap_ms = 0.0
        self._last_output_at: float | None = None

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

        reservoir = PcmReservoir(
            hello.channels, max_samples=max(1, hello.sample_rate * 60 // 1_000)
        )
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
        metrics_task: asyncio.Task[None] | None = None
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
                _log.info("device profile is waiting: %s", detail)
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
            _log.info(
                "latency profile: block_time=%s, crossfade_time=%s, extra_time=%s, "
                "lookahead=%s/%s",
                profile.preferences.get("block_time", "server-default"),
                profile.preferences.get("crossfade_time", "server-default"),
                profile.preferences.get("extra_time", "server-default"),
                profile.preferences.get("enable_lookahead", "server-default"),
                profile.preferences.get("lookahead_time", "server-default"),
            )

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
            peer_connection = client.peer_connection
            if peer_connection is None or not set_audio_jitter_prefetch(
                peer_connection, track, self.options.jitter_prefetch
            ):
                _log.warning("could not tune the audio jitter buffer; using the SDK default")
            else:
                _log.info(
                    "audio jitter prefetch set to %s frame(s)",
                    self.options.jitter_prefetch,
                )
            output_task = asyncio.create_task(
                self._write_converted_audio(track, hello.sample_rate, hello.channels),
                name="converted-output",
            )
            await connect_task
            await self._send_status("ready", "converted audio is flowing")
            metrics_task = asyncio.create_task(
                self._log_runtime_metrics(reservoir, track, hello.sample_rate),
                name="latency-metrics",
            )
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
                metrics_task,
                profile_change_task,
            ):
                if task is not None and not task.done():
                    task.cancel()
            for task in (
                input_task,
                profile_task,
                connect_task,
                output_task,
                metrics_task,
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
            received_at = time.monotonic()
            if self._last_output_at is not None:
                self.output_max_gap_ms = max(
                    self.output_max_gap_ms,
                    (received_at - self._last_output_at) * 1_000,
                )
            self._last_output_at = received_at
            converted_frames = resampler.resample(frame)
            for converted in converted_frames:
                self.output_samples += converted.samples
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

    async def _log_runtime_metrics(
        self, reservoir: PcmReservoir, track: Any, sample_rate: int
    ) -> None:
        last_at = time.monotonic()
        last_output_samples = self.output_samples
        last_dropped_samples = reservoir.dropped_samples
        while True:
            await asyncio.sleep(2.0)
            try:
                now = time.monotonic()
                elapsed = max(now - last_at, 0.001)
                output_realtime = (
                    (self.output_samples - last_output_samples) / sample_rate / elapsed
                )
                dropped_ms = (
                    (reservoir.dropped_samples - last_dropped_samples)
                    * 1_000
                    / sample_rate
                )
                rtt_ms = 0.0
                jitter_ms = 0.0
                packets_lost = 0
                peer_connection = (
                    self.client.peer_connection if self.client is not None else None
                )
                if peer_connection is not None:
                    report = await peer_connection.getStats()
                    for entry in report.values():
                        kind = getattr(entry, "type", None)
                        media_kind = getattr(entry, "kind", None)
                        if kind == "remote-inbound-rtp" and media_kind == "audio":
                            rtt = getattr(entry, "roundTripTime", None)
                            if isinstance(rtt, (int, float)):
                                rtt_ms = rtt * 1_000
                        elif kind == "inbound-rtp" and media_kind == "audio":
                            jitter = getattr(entry, "jitter", 0)
                            if isinstance(jitter, (int, float)):
                                jitter_ms = jitter * 1_000 / 48_000
                            packets_lost = int(getattr(entry, "packetsLost", 0) or 0)
                queue = getattr(track, "_queue", None)
                decoded_frames = queue.qsize() if queue is not None else -1
                transport = self.writer.transport
                write_buffer_bytes = (
                    transport.get_write_buffer_size() if transport is not None else -1
                )
                _log.debug(
                    "latency stats: rtt_ms=%s, jitter_ms=%.1f, packets_lost=%s, "
                    "input_buffer_ms=%.1f, input_dropped_ms=%.1f, output_rate=%.3f, "
                    "output_max_gap_ms=%.1f, decoded_frames=%s, tcp_write_bytes=%s",
                    round(rtt_ms, 1),
                    jitter_ms,
                    packets_lost,
                    reservoir.available_samples * 1_000 / sample_rate,
                    dropped_ms,
                    output_realtime,
                    self.output_max_gap_ms,
                    decoded_frames,
                    write_buffer_bytes,
                )
                last_at = now
                last_output_samples = self.output_samples
                last_dropped_samples = reservoir.dropped_samples
                self.output_max_gap_ms = 0.0
            except Exception as exc:
                _log.debug("latency stats unavailable: %s", exc)

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
