from __future__ import annotations

import asyncio
import math
import struct
from dataclasses import dataclass
from enum import IntEnum

MAGIC = b"CBV3"
VERSION = 2
MAX_PAYLOAD_BYTES = 256 * 1024
MAX_API_KEY_BYTES = 512
HEADER = struct.Struct("<4sHHII")
HELLO = struct.Struct("<IHHI")
ACCOUNT_STATUS = struct.Struct("<HHdHH")
ACCOUNT_STATUS_VERSION = 1
MAX_ACCOUNT_TEXT_BYTES = 512


class ProtocolError(RuntimeError):
    """The plug-in sent a malformed or unsupported bridge frame."""


class MessageType(IntEnum):
    HELLO = 1
    AUDIO_INPUT = 2
    AUDIO_OUTPUT = 3
    STATUS = 4
    AUTH = 5
    ACCOUNT_STATUS = 6


class AccountStatusFlag(IntEnum):
    ACCOUNT_CHECKED = 1 << 0
    AUTHENTICATED = 1 << 1
    BALANCE_CHECKED = 1 << 2
    BALANCE_AVAILABLE = 1 << 3
    REALTIME_ENABLED = 1 << 4
    MODEL_CONFIGURED = 1 << 5


@dataclass(frozen=True, slots=True)
class Message:
    type: MessageType
    sequence: int
    payload: bytes


@dataclass(frozen=True, slots=True)
class Hello:
    sample_rate: int
    channels: int
    max_block_frames: int

    def validate(self) -> None:
        if not 8_000 <= self.sample_rate <= 384_000:
            raise ProtocolError(f"unsupported sample rate: {self.sample_rate}")
        if self.channels not in (1, 2):
            raise ProtocolError(f"unsupported channel count: {self.channels}")
        if self.max_block_frames <= 0:
            raise ProtocolError("max_block_frames must be positive")


@dataclass(frozen=True, slots=True)
class AccountStatus:
    account_checked: bool = False
    authenticated: bool = False
    balance_checked: bool = False
    balance_available: bool = False
    realtime_enabled: bool = False
    model_configured: bool = False
    balance: float = 0.0
    label: str = ""
    model_id: str = ""


def encode_message(message_type: MessageType, sequence: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise ProtocolError(f"payload is too large: {len(payload)} bytes")
    return HEADER.pack(MAGIC, VERSION, int(message_type), len(payload), sequence) + payload


def encode_hello(hello: Hello) -> bytes:
    hello.validate()
    return HELLO.pack(hello.sample_rate, hello.channels, 0, hello.max_block_frames)


def decode_hello(payload: bytes) -> Hello:
    if len(payload) != HELLO.size:
        raise ProtocolError(f"HELLO payload must be {HELLO.size} bytes")
    sample_rate, channels, _reserved, max_block_frames = HELLO.unpack(payload)
    hello = Hello(sample_rate, channels, max_block_frames)
    hello.validate()
    return hello


def decode_auth(payload: bytes) -> str:
    if not 1 <= len(payload) <= MAX_API_KEY_BYTES:
        raise ProtocolError("AUTH payload has an invalid length")
    try:
        api_key = payload.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ProtocolError("AUTH payload must be printable ASCII") from exc
    if any(not 0x21 <= ord(character) <= 0x7E for character in api_key):
        raise ProtocolError("AUTH payload must be printable ASCII without whitespace")
    return api_key


def encode_account_status(status: AccountStatus) -> bytes:
    label = status.label.encode("utf-8")
    model_id = status.model_id.encode("utf-8")
    if len(label) > MAX_ACCOUNT_TEXT_BYTES or len(model_id) > MAX_ACCOUNT_TEXT_BYTES:
        raise ProtocolError("account status text is too long")
    if status.balance_available and not math.isfinite(status.balance):
        raise ProtocolError("account status balance must be finite")
    flags = 0
    for enabled, flag in (
        (status.account_checked, AccountStatusFlag.ACCOUNT_CHECKED),
        (status.authenticated, AccountStatusFlag.AUTHENTICATED),
        (status.balance_checked, AccountStatusFlag.BALANCE_CHECKED),
        (status.balance_available, AccountStatusFlag.BALANCE_AVAILABLE),
        (status.realtime_enabled, AccountStatusFlag.REALTIME_ENABLED),
        (status.model_configured, AccountStatusFlag.MODEL_CONFIGURED),
    ):
        if enabled:
            flags |= int(flag)
    return (
        ACCOUNT_STATUS.pack(
            ACCOUNT_STATUS_VERSION,
            flags,
            status.balance,
            len(label),
            len(model_id),
        )
        + label
        + model_id
    )


def decode_account_status(payload: bytes) -> AccountStatus:
    if len(payload) < ACCOUNT_STATUS.size:
        raise ProtocolError("account status payload is too short")
    version, flags, balance, label_size, model_id_size = ACCOUNT_STATUS.unpack_from(
        payload
    )
    known_flags = sum(int(flag) for flag in AccountStatusFlag)
    if version != ACCOUNT_STATUS_VERSION or flags & ~known_flags:
        raise ProtocolError("account status version or flags are invalid")
    if label_size > MAX_ACCOUNT_TEXT_BYTES or model_id_size > MAX_ACCOUNT_TEXT_BYTES:
        raise ProtocolError("account status text is too long")
    if len(payload) != ACCOUNT_STATUS.size + label_size + model_id_size:
        raise ProtocolError("account status payload length is invalid")
    label_end = ACCOUNT_STATUS.size + label_size
    try:
        label = payload[ACCOUNT_STATUS.size:label_end].decode("utf-8")
        model_id = payload[label_end:].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ProtocolError("account status text must be UTF-8") from exc
    has = lambda flag: bool(flags & int(flag))
    status = AccountStatus(
        account_checked=has(AccountStatusFlag.ACCOUNT_CHECKED),
        authenticated=has(AccountStatusFlag.AUTHENTICATED),
        balance_checked=has(AccountStatusFlag.BALANCE_CHECKED),
        balance_available=has(AccountStatusFlag.BALANCE_AVAILABLE),
        realtime_enabled=has(AccountStatusFlag.REALTIME_ENABLED),
        model_configured=has(AccountStatusFlag.MODEL_CONFIGURED),
        balance=balance,
        label=label,
        model_id=model_id,
    )
    if status.balance_available and not math.isfinite(status.balance):
        raise ProtocolError("account status balance must be finite")
    return status


def decode_frame(frame: bytes) -> Message:
    if len(frame) < HEADER.size:
        raise ProtocolError("frame is shorter than its header")
    magic, version, raw_type, payload_size, sequence = HEADER.unpack_from(frame)
    if magic != MAGIC:
        raise ProtocolError("invalid frame magic")
    if version != VERSION:
        raise ProtocolError(f"unsupported protocol version: {version}")
    if payload_size > MAX_PAYLOAD_BYTES:
        raise ProtocolError(f"payload is too large: {payload_size} bytes")
    if len(frame) != HEADER.size + payload_size:
        raise ProtocolError("frame payload length does not match its header")
    try:
        message_type = MessageType(raw_type)
    except ValueError as exc:
        raise ProtocolError(f"unknown message type: {raw_type}") from exc
    return Message(message_type, sequence, frame[HEADER.size :])


async def read_message(reader: asyncio.StreamReader) -> Message:
    header = await reader.readexactly(HEADER.size)
    magic, version, raw_type, payload_size, sequence = HEADER.unpack(header)
    if magic != MAGIC:
        raise ProtocolError("invalid frame magic")
    if version != VERSION:
        raise ProtocolError(f"unsupported protocol version: {version}")
    if payload_size > MAX_PAYLOAD_BYTES:
        raise ProtocolError(f"payload is too large: {payload_size} bytes")
    try:
        message_type = MessageType(raw_type)
    except ValueError as exc:
        raise ProtocolError(f"unknown message type: {raw_type}") from exc
    return Message(message_type, sequence, await reader.readexactly(payload_size))


async def write_message(
    writer: asyncio.StreamWriter,
    lock: asyncio.Lock,
    message_type: MessageType,
    sequence: int,
    payload: bytes = b"",
) -> None:
    frame = encode_message(message_type, sequence, payload)
    async with lock:
        writer.write(frame)
        await writer.drain()
