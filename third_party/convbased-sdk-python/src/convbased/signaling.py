"""Signaling WebSocket channel (mirrors packages/sdk-js/src/signaling.ts)."""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Any, Callable, Optional

import websockets
from websockets.protocol import State

from .signaling_ticket import signaling_websocket_url

_log = logging.getLogger("convbased")

OnMessage = Callable[[dict[str, Any]], Any]
OnClose = Callable[[int, str], Any]
OnError = Callable[[Exception], Any]


class SignalingChannel:
    """Thin wrapper over a JSON-over-WebSocket connection to the signaling server.

    The URL carries only a short-lived, one-use ticket.
    """

    def __init__(
        self,
        signaling_url: str,
        *,
        ticket: str,
        connect_timeout: float = 20.0,
    ) -> None:
        self._url = signaling_websocket_url(signaling_url, ticket)
        self._connect_timeout = connect_timeout
        self._ws: Any = None
        self._recv_task: Optional[asyncio.Task[None]] = None

    @property
    def is_open(self) -> bool:
        if self._ws is None:
            return False
        state = getattr(self._ws, "state", None)
        if state is not None:
            return state is State.OPEN
        return getattr(self._ws, "closed", True) is False

    async def connect(self, *, on_message: OnMessage, on_close: OnClose, on_error: OnError) -> None:
        try:
            self._ws = await asyncio.wait_for(
                websockets.connect(self._url, max_size=None),
                timeout=self._connect_timeout,
            )
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            raise RuntimeError(
                f"signaling connection failed: {type(exc).__name__}"
            ) from None
        self._recv_task = asyncio.ensure_future(self._receive_loop(on_message, on_close, on_error))

    async def _receive_loop(self, on_message: OnMessage, on_close: OnClose, on_error: OnError) -> None:
        assert self._ws is not None
        try:
            async for raw in self._ws:
                try:
                    msg = json.loads(raw)
                except (ValueError, TypeError):
                    _log.warning("convbased: dropping non-JSON signaling frame")
                    continue
                if isinstance(msg, dict):
                    on_message(msg)
        except websockets.ConnectionClosed as exc:
            on_close(exc.code, exc.reason or "")
        except Exception as exc:  # noqa: BLE001 - surface to the client
            on_error(RuntimeError(f"signaling receive failed: {type(exc).__name__}"))

    async def send(self, msg: dict[str, Any]) -> None:
        if not self.is_open:
            raise RuntimeError("signaling channel is closed")
        assert self._ws is not None
        await self._ws.send(json.dumps(msg))

    async def close(self) -> None:
        if self._recv_task:
            self._recv_task.cancel()
            self._recv_task = None
        if self._ws is not None:
            try:
                await self._ws.close()
            finally:
                self._ws = None
