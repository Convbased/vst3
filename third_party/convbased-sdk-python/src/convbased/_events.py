"""A tiny event emitter that accepts both sync and async handlers.

Mirrors the role of packages/sdk-js/src/events.ts. Handlers registered with
:meth:`EventEmitter.on` are invoked on :meth:`emit`; coroutine handlers are scheduled
on the running event loop.
"""

from __future__ import annotations

import asyncio
import inspect
import logging
from typing import Any, Callable

_log = logging.getLogger("convbased")


class EventEmitter:
    def __init__(self) -> None:
        self._handlers: dict[str, list[Callable[..., Any]]] = {}

    def on(self, event: str, handler: Callable[..., Any]) -> Callable[[], None]:
        """Register ``handler`` for ``event``; returns an unsubscribe callable."""
        self._handlers.setdefault(event, []).append(handler)

        def off() -> None:
            handlers = self._handlers.get(event)
            if handlers and handler in handlers:
                handlers.remove(handler)

        return off

    def emit(self, event: str, *args: Any) -> None:
        for handler in list(self._handlers.get(event, ())):
            try:
                result = handler(*args)
                if inspect.isawaitable(result):
                    asyncio.ensure_future(result)
            except Exception:  # never let one handler break the loop
                _log.exception("convbased: error in %r handler", event)
