"""Fetch TURN/STUN credentials from the Convbased GraphQL endpoint.

Mirrors packages/sdk-js/src/rtcServers.ts. Returns an ICE-server dict shaped for
:class:`aiortc.RTCIceServer`.
"""

from __future__ import annotations

import asyncio
import os
import re
import socket
import struct
import time
from typing import Any, Optional, cast

import aiohttp

_RTC_SERVERS_QUERY = "query { rtcServers { urls username credential } }"


def _standard_udp_turn_url(url: str) -> str | None:
    match = re.match(
        r"^turns?:(\[[^\]]+\]|[^:?]+)(?::\d+)?(?:\?.*)?$",
        url,
        flags=re.IGNORECASE,
    )
    if match is None:
        return None
    return f"turn:{match.group(1)}:3478?transport=udp"


def _probe_stun_binding(url: str, timeout: float) -> float | None:
    match = re.match(
        r"^turn:([^:?]+|\[[^\]]+\]):(\d+)(?:\?.*)?$",
        url,
        flags=re.IGNORECASE,
    )
    if match is None:
        return None
    host = match.group(1).strip("[]")
    port = int(match.group(2))
    transaction_id = os.urandom(12)
    request = struct.pack("!HHI12s", 0x0001, 0, 0x2112A442, transaction_id)
    try:
        addresses = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)
    except OSError:
        return None
    for family, socktype, protocol, _canonical_name, address in addresses:
        probe = socket.socket(family, socktype, protocol)
        try:
            probe.settimeout(timeout)
            started_at = time.perf_counter()
            probe.sendto(request, address)
            response, _peer = probe.recvfrom(2_048)
            if (
                len(response) >= 20
                and response[:2] == b"\x01\x01"
                and response[4:8] == b"\x21\x12\xa4\x42"
                and response[8:20] == transaction_id
            ):
                return (time.perf_counter() - started_at) * 1_000
        except OSError:
            pass
        finally:
            probe.close()
    return None


async def select_fastest_udp_turn_url(
    urls: list[str], *, timeout: float = 0.5
) -> str | None:
    candidates = list(
        dict.fromkeys(
            candidate
            for url in urls
            if (candidate := _standard_udp_turn_url(url)) is not None
        )
    )
    if not candidates:
        return None
    measurements = await asyncio.gather(
        *(
            asyncio.to_thread(_probe_stun_binding, candidate, timeout)
            for candidate in candidates
        )
    )
    reachable = [
        (latency, candidate)
        for candidate, latency in zip(candidates, measurements)
        if latency is not None
    ]
    return min(reachable)[1] if reachable else None


def prepare_aiortc_ice_servers(
    server: dict[str, Any],
    *,
    fallback_stun_urls: list[str],
    preferred_udp_url: str | None = None,
) -> list[dict[str, Any]]:
    """Prefer UDP TURN and add a STUN candidate for aiortc's single-server ICE stack."""
    raw_urls = server.get("urls")
    urls = [raw_urls] if isinstance(raw_urls, str) else list(raw_urls or [])
    if not urls:
        raise ValueError("ICE server is missing urls")

    def priority(url: str) -> int:
        lowered = url.lower()
        if lowered.startswith("turn:"):
            if "transport=tcp" in lowered:
                return 1
            return 0
        if lowered.startswith("turns:"):
            return 2
        return 3

    ordered_urls = sorted(urls, key=priority)
    if preferred_udp_url is not None:
        if preferred_udp_url in ordered_urls:
            ordered_urls.remove(preferred_udp_url)
        ordered_urls.insert(0, preferred_udp_url)
    prepared = [{**server, "urls": ordered_urls}]
    if any(url.lower().startswith("stun:") for url in ordered_urls):
        return prepared

    udp_turn = next(
        (
            url
            for url in ordered_urls
            if url.lower().startswith("turn:")
            and "transport=tcp" not in url.lower()
        ),
        None,
    )
    if udp_turn is not None:
        stun_url = re.sub(r"^turn:", "stun:", udp_turn, count=1, flags=re.IGNORECASE)
        stun_url = stun_url.split("?", 1)[0]
        prepared.append({"urls": [stun_url]})
    elif fallback_stun_urls:
        prepared.append({"urls": list(fallback_stun_urls)})
    return prepared


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
    return cast(dict[str, Any], servers)
