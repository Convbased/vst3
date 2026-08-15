"""Convbased real-time voice-conversion SDK (Python).

Public surface mirrors packages/sdk-js/src/index.ts. WebRTC is provided by ``aiortc``.
"""

from __future__ import annotations

from .client import ConvbasedClient
from .endpoints import DEFAULT_GRAPHQL_URL, DEFAULT_SIGNALING_URL, DEFAULT_STUN_SERVERS
from .rtc_servers import fetch_rtc_servers
from .sdp import apply_opus_sdp_options
from .signaling_ticket import (
    SignalingTicketError,
    issue_signaling_ticket,
    signaling_ticket_url,
    signaling_websocket_url,
)
from .types import (
    ConnectionStats,
    ProcessingLatencyStats,
    RTCPreferences,
    RTCStatusCode,
    RealtimePerformanceStats,
)

__all__ = [
    "ConvbasedClient",
    "RTCPreferences",
    "RTCStatusCode",
    "ConnectionStats",
    "ProcessingLatencyStats",
    "RealtimePerformanceStats",
    "fetch_rtc_servers",
    "apply_opus_sdp_options",
    "DEFAULT_SIGNALING_URL",
    "DEFAULT_GRAPHQL_URL",
    "DEFAULT_STUN_SERVERS",
    "SignalingTicketError",
    "issue_signaling_ticket",
    "signaling_ticket_url",
    "signaling_websocket_url",
]

__version__ = "0.2.0"
