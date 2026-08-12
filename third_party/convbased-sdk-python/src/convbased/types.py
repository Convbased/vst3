"""Wire-protocol types, mirrored from docs/protocol and packages/sdk-js/src/types.ts."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import IntEnum
from typing import Any, Literal, Optional


class RTCStatusCode(IntEnum):
    """`code` carried on server ``message`` frames (see docs/protocol)."""

    ERROR = 2000
    GPU_INSUFFICIENT = 2001
    DUPLICATE_CONNECTION = 2002
    MODEL_NOT_FOUND = 2003
    UNPAID_SERVICE = 2004
    REQUEST_TOO_FAST = 2005

    CONNECTED = 3000
    REQUEST_RECEIVED = 3001
    TRACK_READY = 3002
    RESPONSE_SENT = 3003
    LOADING_MODEL = 3004
    SERVICE_READY = 3009  # converted audio is flowing

    SHUTDOWN = 4000
    SERVER_CLOSED = 5000


#: Codes that should be surfaced as a fatal error to the caller.
FATAL_CODES = frozenset(
    {
        RTCStatusCode.ERROR,
        RTCStatusCode.GPU_INSUFFICIENT,
        RTCStatusCode.DUPLICATE_CONNECTION,
        RTCStatusCode.MODEL_NOT_FOUND,
        RTCStatusCode.UNPAID_SERVICE,
        RTCStatusCode.REQUEST_TOO_FAST,
    }
)

ConnectionState = Literal[
    "idle",
    "signaling",
    "negotiating",
    "connecting",
    "connected",
    "closing",
    "closed",
    "error",
]


@dataclass(slots=True)
class RTCPreferences:
    """RVC preferences forwarded to the inference node.

    ``model_id`` and ``sample_rate`` are required in the offer; everything else is
    optional and equivalent to the Convbased-Web sliders. Unset (``None``) fields are
    omitted from the wire payload.
    """

    model_id: str
    sample_rate: int
    pitch: Optional[float] = None
    rms_mix_rate: Optional[float] = None
    f0_threshold: Optional[float] = None
    block_time: Optional[float] = None
    crossfade_time: Optional[float] = None
    extra_time: Optional[float] = None
    f0_autotune: Optional[bool] = None
    f0_autotune_strength: Optional[float] = None
    proposed_pitch: Optional[bool] = None
    proposed_pitch_threshold: Optional[float] = None
    enable_limiter: Optional[bool] = None
    limiter_threshold: Optional[float] = None
    enable_lookahead: Optional[bool] = None
    lookahead_time: Optional[float] = None
    formant: Optional[float] = None
    index_rate: Optional[float] = None
    protect: Optional[float] = None
    threshold: Optional[float] = None

    def to_wire(self) -> dict[str, Any]:
        """The JSON object sent to the server (drops unset fields)."""
        return {k: v for k, v in asdict(self).items() if v is not None}


@dataclass(slots=True)
class ConnectionStats:
    rtt_ms: float = 0.0
    jitter: float = 0.0
    packets_lost: int = 0
