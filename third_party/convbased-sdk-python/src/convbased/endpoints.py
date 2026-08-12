"""Baked-in production endpoints (override only for self-hosted deployments)."""

from __future__ import annotations

#: Signaling WebSocket. A URL ending in ``/ws`` is used as-is; a bare host gets
#: ``/signaling/ws`` appended (see :func:`resolve_signaling_url`).
DEFAULT_SIGNALING_URL = "wss://api.weights.chat/api/signaling/ws"

#: GraphQL endpoint that hands out TURN credentials (query ``rtcServers``).
DEFAULT_GRAPHQL_URL = "https://api.weights.chat/api/v1/graphql"

#: Last-resort public STUN if no TURN servers are configured/fetched.
DEFAULT_STUN_SERVERS: list[dict[str, object]] = [
    {"urls": ["stun:stun.l.google.com:19302"]},
]


def resolve_signaling_url(url: str) -> str:
    """Normalize a signaling URL the same way the JS SDK does."""
    trimmed = url.rstrip("/")
    if trimmed.endswith("/ws"):
        return trimmed
    if trimmed.endswith("/signaling"):
        return trimmed + "/ws"
    return trimmed + "/signaling/ws"
