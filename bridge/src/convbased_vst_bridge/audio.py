from __future__ import annotations

import asyncio
import math
import sys
import time
from array import array
from collections import deque
from fractions import Fraction
from typing import Any


class PcmReservoir:
    """Bounded mono PCM reservoir fed with interleaved float32 plug-in blocks."""

    def __init__(self, channels: int, max_samples: int) -> None:
        if channels not in (1, 2):
            raise ValueError("channels must be one or two")
        if max_samples <= 0:
            raise ValueError("max_samples must be positive")
        self.channels = channels
        self.max_samples = max_samples
        self._chunks: deque[array[float]] = deque()
        self._offset = 0
        self._sample_count = 0
        self.dropped_samples = 0

    @property
    def available_samples(self) -> int:
        return self._sample_count

    def append_interleaved_bytes(self, payload: bytes) -> None:
        if len(payload) % (4 * self.channels) != 0:
            raise ValueError("audio payload is not aligned to float32 frames")
        values = array("f")
        values.frombytes(payload)
        if values.itemsize != 4:
            raise RuntimeError("this platform does not use 32-bit C floats")
        if sys.byteorder != "little":
            values.byteswap()

        if self.channels == 1:
            mono = values
        else:
            mono = array(
                "f",
                ((values[index] + values[index + 1]) * 0.5 for index in range(0, len(values), 2)),
            )
        if mono:
            self._chunks.append(mono)
            self._sample_count += len(mono)
            overflow = self._sample_count - self.max_samples
            if overflow > 0:
                self._discard(overflow)
                self.dropped_samples += overflow

    def read(self, sample_count: int) -> array[float]:
        if sample_count < 0:
            raise ValueError("sample_count cannot be negative")
        result = array("f")
        remaining = min(sample_count, self._sample_count)
        while remaining > 0 and self._chunks:
            chunk = self._chunks[0]
            take = min(remaining, len(chunk) - self._offset)
            result.extend(chunk[self._offset : self._offset + take])
            self._offset += take
            self._sample_count -= take
            remaining -= take
            if self._offset == len(chunk):
                self._chunks.popleft()
                self._offset = 0
        if len(result) < sample_count:
            result.extend([0.0] * (sample_count - len(result)))
        return result

    def _discard(self, sample_count: int) -> None:
        remaining = min(sample_count, self._sample_count)
        while remaining > 0 and self._chunks:
            chunk = self._chunks[0]
            take = min(remaining, len(chunk) - self._offset)
            self._offset += take
            self._sample_count -= take
            remaining -= take
            if self._offset == len(chunk):
                self._chunks.popleft()
                self._offset = 0


try:
    from aiortc import AudioStreamTrack
    from av import AudioFrame
except ModuleNotFoundError:  # Unit tests for the protocol do not require media dependencies.
    AudioStreamTrack = object  # type: ignore[assignment,misc]
    AudioFrame = None  # type: ignore[assignment]


class PluginAudioTrack(AudioStreamTrack):  # type: ignore[misc,valid-type]
    """Paced aiortc track backed by PCM supplied directly by the DAW."""

    kind = "audio"

    def __init__(self, reservoir: PcmReservoir, sample_rate: int) -> None:
        if AudioFrame is None:
            raise RuntimeError("aiortc and av must be installed to run the bridge service")
        super().__init__()
        self._reservoir = reservoir
        self._sample_rate = sample_rate
        self._samples_per_frame = max(1, round(sample_rate * 0.02))
        self._start: float | None = None
        self._timestamp = 0

    async def recv(self) -> Any:
        if self._start is None:
            self._start = time.monotonic()
            self._timestamp = 0
        else:
            self._timestamp += self._samples_per_frame
            target = self._start + self._timestamp / self._sample_rate
            delay = target - time.monotonic()
            if delay > 0:
                await asyncio.sleep(delay)

        samples = float_to_pcm16(self._reservoir.read(self._samples_per_frame))
        if sys.byteorder != "little":
            samples.byteswap()
        frame = AudioFrame(format="s16", layout="mono", samples=self._samples_per_frame)
        frame.planes[0].update(samples.tobytes())
        frame.pts = self._timestamp
        frame.sample_rate = self._sample_rate
        frame.time_base = Fraction(1, self._sample_rate)
        return frame


def set_audio_jitter_prefetch(peer_connection: Any, track: Any, frames: int) -> bool:
    """Tune aiortc's audio receiver for low-latency playout."""

    if not 1 <= frames <= 16:
        raise ValueError("jitter prefetch must be between one and sixteen frames")
    for receiver in peer_connection.getReceivers():
        if receiver.track is not track:
            continue
        jitter_buffer = getattr(receiver, "_RTCRtpReceiver__jitter_buffer", None)
        if jitter_buffer is None or not hasattr(jitter_buffer, "_prefetch"):
            return False
        jitter_buffer._prefetch = frames
        return True
    return False


def float_to_pcm16(samples: array[float]) -> array[int]:
    """Clip normalized float PCM and convert it to signed 16-bit samples."""

    output = array("h")
    for sample in samples:
        if not math.isfinite(sample):
            output.append(0)
        elif sample <= -1.0:
            output.append(-32_768)
        elif sample >= 1.0:
            output.append(32_767)
        else:
            output.append(round(sample * 32_767.0))
    if output.itemsize != 2:
        raise RuntimeError("this platform does not use 16-bit C shorts")
    return output


def interleave_mono(samples: array[float], channels: int) -> array[float]:
    if channels == 1:
        return samples
    if channels != 2:
        raise ValueError("channels must be one or two")
    output = array("f")
    for sample in samples:
        output.append(sample)
        output.append(sample)
    return output
