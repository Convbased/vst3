from __future__ import annotations

import unittest
from array import array

from convbased_vst_bridge.audio import PcmReservoir, interleave_mono


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


if __name__ == "__main__":
    unittest.main()
