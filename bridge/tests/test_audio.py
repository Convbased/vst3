from __future__ import annotations

import unittest
from array import array

from convbased_vst_bridge.audio import (
    PcmReservoir,
    PluginAudioTrack,
    float_to_pcm16,
    interleave_mono,
    set_audio_jitter_prefetch,
)


class AudioTests(unittest.TestCase):
    def test_stereo_is_downmixed_and_zero_padded(self) -> None:
        reservoir = PcmReservoir(channels=2, max_samples=8)
        reservoir.append_interleaved_bytes(array("f", [1.0, -1.0, 0.5, 0.25]).tobytes())
        self.assertEqual(list(reservoir.read(3)), [0.0, 0.375, 0.0])

    def test_reservoir_discards_oldest_samples_when_bounded(self) -> None:
        reservoir = PcmReservoir(channels=1, max_samples=3)
        reservoir.append_interleaved_bytes(array("f", [1.0, 2.0, 3.0, 4.0]).tobytes())
        self.assertEqual(list(reservoir.read(3)), [2.0, 3.0, 4.0])
        self.assertEqual(reservoir.dropped_samples, 1)

    def test_mono_is_duplicated_for_stereo_output(self) -> None:
        self.assertEqual(list(interleave_mono(array("f", [0.25, -0.5]), 2)), [0.25, 0.25, -0.5, -0.5])

    def test_float_samples_are_clipped_and_converted_to_pcm16(self) -> None:
        converted = float_to_pcm16(
            array("f", [-2.0, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0, float("nan")])
        )
        self.assertEqual(
            list(converted),
            [-32_768, -32_768, -16_384, 0, 16_384, 32_767, 32_767, 0],
        )


class AudioTrackTests(unittest.IsolatedAsyncioTestCase):
    async def test_track_produces_opus_compatible_pcm16_frames(self) -> None:
        reservoir = PcmReservoir(channels=1, max_samples=960)
        reservoir.append_interleaved_bytes(array("f", [0.25] * 960).tobytes())
        frame = await PluginAudioTrack(reservoir, 48_000).recv()

        self.assertEqual(frame.format.name, "s16")
        self.assertEqual(frame.layout.name, "mono")
        self.assertEqual(frame.samples, 960)

        from aiortc.codecs.opus import OpusEncoder

        packets, timestamp = OpusEncoder().encode(frame)
        self.assertTrue(packets)
        self.assertEqual(timestamp, 0)

    async def test_audio_receiver_prefetch_is_reduced(self) -> None:
        from aiortc import RTCPeerConnection

        peer_connection = RTCPeerConnection()
        transceiver = peer_connection.addTransceiver("audio", direction="recvonly")
        try:
            self.assertTrue(
                set_audio_jitter_prefetch(peer_connection, transceiver.receiver.track, 1)
            )
            jitter_buffer = getattr(
                transceiver.receiver, "_RTCRtpReceiver__jitter_buffer"
            )
            self.assertEqual(jitter_buffer._prefetch, 1)
        finally:
            await peer_connection.close()


if __name__ == "__main__":
    unittest.main()
