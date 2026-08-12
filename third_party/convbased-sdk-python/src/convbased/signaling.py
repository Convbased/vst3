"""Signaling WebSocket channel (mirrors packages/sdk-js/src/signaling.ts)."""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Any, Awaitable, Callable, Optional
from urllib.parse import urlencode

import websockets

from .endpoints import resolve_signaling_url

_log = logging.getLogger("convbased")

OnMessage = Callable[[dict[str, Any]], Any]
OnClose = Callable[[int, str], Any]
OnError = Callable[[Exception], Any]


class SignalingChannel:
    """Thin wrapper over a JSON-over-WebSocket connection to the signaling server.

    Auth uses a scoped ``api_key`` in the query string.
    """

    def __init__(
        self,
        signaling_url: str,
        *,
        api_key: str,
        connect_timeout: float = 20.0,
    ) -> None:
        self._url = self._build_url(signaling_url, api_key)
        self._connect_timeout = connect_timeout
        self._ws: Optional[websockets.WebSocketClientProtocol] = None
        self._recv_task: Optional[asyncio.Task[None]] = None

    @staticmethod
    def _build_url(url: str, api_key: str) -> str:
        base = resolve_signaling_url(url)
        api_key = api_key.strip()
        if not api_key:
            raise ValueError("SignalingChannel requires api_key")
        query = urlencode({"api_key": api_key})
        sep = "&" if "?" in base else "?"
        return f"{base}{sep}{query}"

    @property
    def is_open(self) -> bool:
        return self._ws is not None and not self._ws.closed

    async def connect(self, *, on_message: OnMessage, on_close: OnClose, on_error: OnError) -> None:
        self._ws = await asyncio.wait_for(
            websockets.connect(self._url, max_size=None),
            timeout=self._connect_timeout,
        )
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
            on_error(exc)

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
