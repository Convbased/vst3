"""Real-time voice-conversion client (Python), mirroring packages/sdk-js/src/client.ts.

Implements the signaling protocol in docs/protocol over WebRTC via ``aiortc``:

  1. Open the signaling WebSocket (auth in the query string).
  2. Build an ``RTCPeerConnection``, attach the caller's audio track, send
     ``{"type": "offer", "sdp", "preferences"}``.
  3. Apply the ``answer`` and ``ice_candidate`` frames; on status ``3009``
     (SERVICE_READY) the converted track is flowing — emitted via the ``track`` event.

One client = one session: call :meth:`connect` once, then :meth:`disconnect`.

Events (register with :meth:`on`):
  ``state(state, previous)`` · ``message(code, text, raw)`` · ``ready(code)`` ·
  ``track(track)`` · ``error(exc)`` · ``closed(code, reason)``.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Any, Optional

from aiortc import (
    RTCConfiguration,
    RTCIceServer,
    RTCPeerConnection,
    RTCSessionDescription,
)
from aiortc.mediastreams import MediaStreamTrack
from aiortc.sdp import candidate_from_sdp

from ._events import EventEmitter
from .endpoints import DEFAULT_GRAPHQL_URL, DEFAULT_SIGNALING_URL, DEFAULT_STUN_SERVERS
from .rtc_servers import fetch_rtc_servers
from .sdp import apply_opus_sdp_options
from .signaling import SignalingChannel
from .types import FATAL_CODES, ConnectionState, ConnectionStats, RTCPreferences, RTCStatusCode

_log = logging.getLogger("convbased")


class ConvbasedClient:
    def __init__(
        self,
        *,
        api_key: str,
        signaling_url: str = DEFAULT_SIGNALING_URL,
        graphql_url: Optional[str] = DEFAULT_GRAPHQL_URL,
        ice_servers: Optional[list[dict[str, Any]]] = None,
        ice_transport_policy: str = "all",
        bitrate_kbps: int = 64,
        stereo: bool = False,
        signaling_timeout: float = 120.0,
        connect_timeout: float = 20.0,
    ) -> None:
        api_key = api_key.strip()
        if not api_key:
            raise ValueError("ConvbasedClient requires api_key")
        self._api_key = api_key
        self._signaling_url = signaling_url
        self._graphql_url = graphql_url
        self._ice_servers = ice_servers
        self._ice_transport_policy = ice_transport_policy
        self._bitrate_kbps = bitrate_kbps
        self._stereo = stereo
        self._signaling_timeout = signaling_timeout
        self._connect_timeout = connect_timeout

        self._events = EventEmitter()
        self._state: ConnectionState = "idle"
        self._signaling: Optional[SignalingChannel] = None
        self._pc: Optional[RTCPeerConnection] = None
        self._remote_track: Optional[MediaStreamTrack] = None
        self._ready: Optional[asyncio.Future[None]] = None

    # -- public API ---------------------------------------------------------

    def on(self, event: str, handler):
        """Register an event handler; returns an unsubscribe callable."""
        return self._events.on(event, handler)

    @property
    def state(self) -> ConnectionState:
        return self._state

    @property
    def remote_track(self) -> Optional[MediaStreamTrack]:
        return self._remote_track

    @property
    def peer_connection(self) -> Optional[RTCPeerConnection]:
        return self._pc

    async def connect(
        self,
        *,
        model_id: str,
        audio: MediaStreamTrack,
        preferences: Optional[dict[str, Any]] = None,
        sample_rate: int = 48000,
    ) -> None:
        """Open signaling, negotiate WebRTC, resolve once SERVICE_READY arrives."""
        if self._state != "idle":
            raise RuntimeError(f"connect() called from invalid state {self._state!r}")
        if not model_id:
            raise ValueError("model_id is required")

        self._set_state("signaling")
        try:
            await self._open_signaling()
            ice_servers = await self._resolve_ice_servers()
            self._set_state("negotiating")
            # Arm the readiness future *before* sending the offer so a fast SERVICE_READY
            # can't race ahead of it.
            self._ready = asyncio.get_running_loop().create_future()
            await self._open_peer(audio, ice_servers)
            await self._send_offer(model_id, sample_rate, preferences or {})
            await asyncio.wait_for(self._ready, timeout=self._signaling_timeout)
            self._set_state("connected")
        except Exception as exc:  # noqa: BLE001 - normalize + surface
            error = exc if isinstance(exc, Exception) else RuntimeError(str(exc))
            self._events.emit("error", error)
            await self.disconnect()
            self._set_state("error")
            raise

    async def update_config(self, preferences: dict[str, Any]) -> None:
        """Hot-apply a partial preferences update on the inference node (no reconnect)."""
        if not self._signaling or not self._signaling.is_open:
            raise RuntimeError("cannot update_config: signaling channel is closed")
        await self._signaling.send({"type": "config", "preferences": preferences})

    async def get_stats(self) -> Optional[ConnectionStats]:
        if not self._pc:
            return None
        report = await self._pc.getStats()
        stats = ConnectionStats()
        for entry in report.values():
            kind = getattr(entry, "type", None)
            if kind == "candidate-pair" and getattr(entry, "state", None) == "succeeded":
                rtt = getattr(entry, "currentRoundTripTime", None)
                if isinstance(rtt, (int, float)):
                    stats.rtt_ms = round(rtt * 1000)
            elif kind == "inbound-rtp" and getattr(entry, "kind", None) == "audio":
                stats.jitter = getattr(entry, "jitter", stats.jitter) or stats.jitter
                stats.packets_lost = getattr(entry, "packetsLost", stats.packets_lost) or stats.packets_lost
        return stats

    async def disconnect(self) -> None:
        """Notify the server and tear the session down."""
        if self._state in ("closed", "closing"):
            return
        self._set_state("closing")
        try:
            if self._signaling and self._signaling.is_open:
                try:
                    await self._signaling.send({"type": "exit"})
                    await asyncio.sleep(0.025)  # let the frame flush
                except Exception:  # noqa: BLE001
                    pass
        finally:
            if self._pc:
                await self._pc.close()
                self._pc = None
            if self._signaling:
                await self._signaling.close()
                self._signaling = None
            self._remote_track = None
            self._reject_ready(RuntimeError("disconnected before SERVICE_READY"))
            self._set_state("closed")
            self._events.emit("closed", None, None)

    # -- internals ----------------------------------------------------------

    async def _open_signaling(self) -> None:
        channel = SignalingChannel(
            self._signaling_url,
            api_key=self._api_key,
            connect_timeout=self._connect_timeout,
        )
        await channel.connect(
            on_message=self._handle_message,
            on_close=self._handle_close,
            on_error=lambda exc: _log.warning("convbased: signaling error: %s", exc),
        )
        self._signaling = channel

    async def _resolve_ice_servers(self) -> list[dict[str, Any]]:
        if self._ice_servers:
            return self._ice_servers
        if isinstance(self._graphql_url, str):
            try:
                cfg = await fetch_rtc_servers(
                    graphql_url=self._graphql_url,
                    api_key=self._api_key,
                )
                if cfg.get("urls"):
                    return [cfg]
            except Exception as exc:  # noqa: BLE001 - fall back to STUN
                _log.warning("convbased: fetch_rtc_servers failed, using STUN: %s", exc)
        return DEFAULT_STUN_SERVERS

    async def _open_peer(self, audio: MediaStreamTrack, ice_servers: list[dict[str, Any]]) -> None:
        config = RTCConfiguration(
            iceServers=[
                RTCIceServer(
                    urls=srv["urls"],
                    username=srv.get("username"),
                    credential=srv.get("credential"),
                )
                for srv in ice_servers
            ]
        )
        pc = RTCPeerConnection(configuration=config)
        self._pc = pc

        @pc.on("track")
        def _on_track(track: MediaStreamTrack) -> None:
            self._remote_track = track
            self._events.emit("track", track)

        @pc.on("connectionstatechange")
        async def _on_conn_state() -> None:
            cs = pc.connectionState
            _log.debug("convbased: pc state %s", cs)
            if cs == "connecting" and self._state == "negotiating":
                self._set_state("connecting")
            elif cs in ("failed", "disconnected", "closed") and self._state not in ("closing", "closed"):
                self._events.emit("error", RuntimeError(f'peer connection entered "{cs}"'))
                await self.disconnect()

        pc.addTrack(audio)

    async def _send_offer(self, model_id: str, sample_rate: int, preferences: dict[str, Any]) -> None:
        assert self._pc is not None and self._signaling is not None
        # aiortc gathers ICE during setLocalDescription, so localDescription carries the
        # candidates in-SDP (non-trickle). We still apply any ice_candidate the server sends.
        await self._pc.setLocalDescription(await self._pc.createOffer())
        sdp = apply_opus_sdp_options(
            self._pc.localDescription.sdp, bitrate_kbps=self._bitrate_kbps, stereo=self._stereo
        )
        wire_prefs = {**preferences, "model_id": model_id, "sample_rate": sample_rate}
        await self._signaling.send({"type": "offer", "sdp": sdp, "preferences": wire_prefs})

    def _handle_message(self, msg: dict[str, Any]) -> None:
        mtype = msg.get("type")
        code = msg.get("code")
        text = msg.get("message")
        self._events.emit("message", code, text, msg)

        if mtype == "answer":
            asyncio.ensure_future(self._apply_answer(msg))
        elif mtype == "ice_candidate":
            asyncio.ensure_future(self._apply_remote_candidate(msg))
        elif mtype in ("message", None):
            self._handle_status(code, text)
        elif mtype in ("shutdown", "error"):
            self._events.emit("error", RuntimeError(text or f'server sent "{mtype}"'))
            asyncio.ensure_future(self.disconnect())
        elif mtype == "ping" and self._signaling:
            asyncio.ensure_future(self._signaling.send({"type": "pong"}))

    def _handle_status(self, code: Optional[int], text: Optional[str]) -> None:
        if code == RTCStatusCode.SERVICE_READY:
            self._events.emit("ready", code)
            if self._ready and not self._ready.done():
                self._ready.set_result(None)
        elif code in FATAL_CODES:
            err = RuntimeError(text or f"server error code {code}")
            self._events.emit("error", err)
            self._reject_ready(err)
        elif code == RTCStatusCode.SHUTDOWN:
            asyncio.ensure_future(self.disconnect())

    async def _apply_answer(self, msg: dict[str, Any]) -> None:
        if not self._pc:
            return
        try:
            await self._pc.setRemoteDescription(RTCSessionDescription(sdp=msg["sdp"], type="answer"))
        except Exception as exc:  # noqa: BLE001
            self._events.emit("error", RuntimeError(f"failed to apply answer: {exc}"))

    async def _apply_remote_candidate(self, msg: dict[str, Any]) -> None:
        if not self._pc:
            return
        cand = msg.get("candidate") or {}
        raw = cand.get("candidate")
        if not raw:
            return
        try:
            ice = candidate_from_sdp(raw.split(":", 1)[1] if raw.startswith("candidate:") else raw)
            ice.sdpMid = cand.get("sdpMid")
            ice.sdpMLineIndex = cand.get("sdpMLineIndex")
            await self._pc.addIceCandidate(ice)
        except Exception as exc:  # noqa: BLE001
            _log.warning("convbased: addIceCandidate failed: %s", exc)

    def _handle_close(self, code: int, reason: str) -> None:
        if self._state in ("closing", "closed"):
            return
        self._events.emit("closed", code, reason)
        # 1008 is what the server sends for auth + balance failures.
        if code == 1008:
            self._events.emit("error", RuntimeError(f"signaling rejected: {reason or 'policy violation'}"))
        elif code != 1000:
            self._events.emit("error", RuntimeError(f"signaling closed (code={code}, reason={reason or '?'})"))
        self._reject_ready(RuntimeError(f"signaling closed before SERVICE_READY (code={code})"))
        self._set_state("closed")

    def _reject_ready(self, exc: Exception) -> None:
        if self._ready and not self._ready.done():
            self._ready.set_exception(exc)

    def _set_state(self, nxt: ConnectionState) -> None:
        if nxt == self._state:
            return
        prev, self._state = self._state, nxt
        self._events.emit("state", nxt, prev)
