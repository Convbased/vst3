"""Short-lived signaling ticket exchange."""

from __future__ import annotations

import re
from datetime import datetime, timedelta, timezone
from typing import Any
from urllib.parse import urlencode, urlsplit, urlunsplit

import aiohttp

_CLIENT_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_MODEL_ID = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
_TICKET = re.compile(r"^st_[A-Za-z0-9_-]{43}$")
_LOOPBACK = {"localhost", "127.0.0.1", "::1"}


class SignalingTicketError(RuntimeError):
    def __init__(self, code: str, *, retryable: bool = False) -> None:
        super().__init__(code)
        self.code = code
        self.retryable = retryable


def _websocket_parts(endpoint: str) -> tuple[str, str, str]:
    parts = urlsplit(endpoint)
    if parts.scheme not in ("ws", "wss") or not parts.netloc:
        raise SignalingTicketError("INVALID_SIGNALING_ENDPOINT")
    if parts.username or parts.password:
        raise SignalingTicketError("INVALID_SIGNALING_ENDPOINT")
    path = parts.path.rstrip("/")
    if not path.lower().endswith("/ws"):
        path = path + "/ws" if path.lower().endswith("/signaling") else path + "/signaling/ws"
    return parts.scheme, parts.netloc, path


def signaling_ticket_url(endpoint: str) -> str:
    scheme, netloc, path = _websocket_parts(endpoint)
    host = urlsplit(urlunsplit((scheme, netloc, path, "", ""))).hostname
    if scheme == "ws" and host not in _LOOPBACK:
        raise SignalingTicketError("INSECURE_SIGNALING_ENDPOINT")
    http_scheme = "https" if scheme == "wss" else "http"
    return urlunsplit((http_scheme, netloc, path[:-3] + "/ticket", "", ""))


def signaling_websocket_url(endpoint: str, ticket: str) -> str:
    if not _TICKET.fullmatch(ticket):
        raise SignalingTicketError("INVALID_SIGNALING_TICKET")
    scheme, netloc, path = _websocket_parts(endpoint)
    return urlunsplit((scheme, netloc, path, urlencode({"ticket": ticket}), ""))


def _validated_ticket(value: Any, client_id: str, model_id: str) -> str:
    if not isinstance(value, dict):
        raise SignalingTicketError("INVALID_SIGNALING_TICKET_RESPONSE")
    ticket = value.get("ticket")
    resource = value.get("resource")
    try:
        expires_at = datetime.fromisoformat(
            str(value.get("expires_at", "")).replace("Z", "+00:00")
        )
    except ValueError:
        expires_at = datetime.min.replace(tzinfo=timezone.utc)
    now = datetime.now(timezone.utc)
    if (
        not isinstance(ticket, str)
        or not _TICKET.fullmatch(ticket)
        or value.get("token_type") != "SignalingTicket"
        or value.get("client_id") != client_id
        or value.get("scopes") != ["realtime"]
        or resource != {"type": "vc_model", "id": model_id}
        or type(value.get("expires_in")) not in (int, float)
        or not 0 < value["expires_in"] <= 60
        or expires_at.tzinfo is None
        or not now < expires_at <= now + timedelta(seconds=61)
    ):
        raise SignalingTicketError("INVALID_SIGNALING_TICKET_RESPONSE")
    return ticket


async def issue_signaling_ticket(
    *,
    signaling_url: str,
    api_key: str,
    client_id: str,
    model_id: str,
    timeout: float,
) -> str:
    if not api_key.strip():
        raise ValueError("issue_signaling_ticket requires api_key")
    if not _CLIENT_ID.fullmatch(client_id) or not _MODEL_ID.fullmatch(model_id):
        raise SignalingTicketError("INVALID_ARGUMENT")
    body = {
        "client_id": client_id,
        "scopes": ["realtime"],
        "resource": {"type": "vc_model", "id": model_id},
    }
    client_timeout = aiohttp.ClientTimeout(total=timeout)
    async with aiohttp.ClientSession(timeout=client_timeout) as session, session.post(
        signaling_ticket_url(signaling_url),
        json=body,
        headers={"x-api-key": api_key.strip()},
    ) as response:
        try:
            payload = await response.json()
        except (aiohttp.ContentTypeError, ValueError):
            payload = None
        if response.status >= 400:
            code = payload.get("code") if isinstance(payload, dict) else None
            retryable = payload.get("retryable") is True if isinstance(payload, dict) else False
            raise SignalingTicketError(
                code if isinstance(code, str) else f"HTTP_{response.status}",
                retryable=retryable,
            )
    return _validated_ticket(payload, client_id, model_id)
