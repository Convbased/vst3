"""Fetch TURN/STUN credentials from the Convbased GraphQL endpoint.

Mirrors packages/sdk-js/src/rtcServers.ts. Returns an ICE-server dict shaped for
:class:`aiortc.RTCIceServer`.
"""

from __future__ import annotations

from typing import Any

import aiohttp

_RTC_SERVERS_QUERY = "query { rtcServers { urls username credential } }"


async def fetch_rtc_servers(
    *,
    graphql_url: str,
    api_key: str,
    timeout: float = 10.0,
) -> dict[str, Any]:
    """Return ``{"urls": [...], "username": ..., "credential": ...}``.

    Raises on transport/GraphQL errors so the caller can fall back to STUN.
    """
    api_key = api_key.strip()
    if not api_key:
        raise ValueError("fetch_rtc_servers requires api_key")
    headers: dict[str, str] = {
        "content-type": "application/json",
        "x-api-key": api_key,
    }

    payload = {"query": _RTC_SERVERS_QUERY}
    client_timeout = aiohttp.ClientTimeout(total=timeout)
    async with aiohttp.ClientSession(timeout=client_timeout) as session:
        async with session.post(graphql_url, json=payload, headers=headers) as resp:
            resp.raise_for_status()
            body = await resp.json()

    if body.get("errors"):
        raise RuntimeError(f"rtcServers GraphQL error: {body['errors']}")
    servers = (body.get("data") or {}).get("rtcServers")
    if not servers or not servers.get("urls"):
        raise RuntimeError("rtcServers response missing urls")
    return servers
