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
    REALTIME_PERFORMANCE = 3014

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


def _wire_number(data: dict[str, Any], key: str) -> float:
    value = data.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key} must be a number")
    return float(value)


def _wire_optional_number(data: dict[str, Any], key: str) -> Optional[float]:
    value = data.get(key)
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key} must be a number or null")
    return float(value)


def _wire_int(data: dict[str, Any], key: str) -> int:
    value = data.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{key} must be an integer")
    return value


@dataclass(frozen=True, slots=True)
class ProcessingLatencyStats:
    sample_count: int
    sample_limit: int
    sample_truncated: bool
    mean_ms: Optional[float]
    p50_ms: Optional[float]
    p95_ms: Optional[float]
    p99_ms: Optional[float]
    max_ms: Optional[float]

    @classmethod
    def from_wire(cls, data: dict[str, Any]) -> "ProcessingLatencyStats":
        sample_truncated = data.get("sample_truncated")
        if not isinstance(sample_truncated, bool):
            raise ValueError("sample_truncated must be a boolean")
        return cls(
            sample_count=_wire_int(data, "sample_count"),
            sample_limit=_wire_int(data, "sample_limit"),
            sample_truncated=sample_truncated,
            mean_ms=_wire_optional_number(data, "mean"),
            p50_ms=_wire_optional_number(data, "p50"),
            p95_ms=_wire_optional_number(data, "p95"),
            p99_ms=_wire_optional_number(data, "p99"),
            max_ms=_wire_optional_number(data, "max"),
        )


@dataclass(frozen=True, slots=True)
class RealtimePerformanceStats:
    instance_id: str
    interval_ms: float
    block_ms: float
    processed_blocks: int
    processing_failures: int
    slow_processing_blocks: int
    dropped_input_samples: int
    input_buffered_samples: int
    output_buffered_samples: int
    processing: ProcessingLatencyStats

    @classmethod
    def from_wire(cls, data: dict[str, Any]) -> "RealtimePerformanceStats":
        instance_id = data.get("instance_id")
        processing = data.get("processing_ms")
        if not isinstance(instance_id, str) or not instance_id:
            raise ValueError("instance_id must be a non-empty string")
        if not isinstance(processing, dict):
            raise ValueError("processing_ms must be an object")
        return cls(
            instance_id=instance_id,
            interval_ms=_wire_number(data, "interval_ms"),
            block_ms=_wire_number(data, "block_ms"),
            processed_blocks=_wire_int(data, "processed_blocks"),
            processing_failures=_wire_int(data, "processing_failures"),
            slow_processing_blocks=_wire_int(data, "slow_processing_blocks"),
            dropped_input_samples=_wire_int(data, "dropped_input_samples"),
            input_buffered_samples=_wire_int(data, "input_buffered_samples"),
            output_buffered_samples=_wire_int(data, "output_buffered_samples"),
            processing=ProcessingLatencyStats.from_wire(processing),
        )
