from __future__ import annotations

import struct
import unittest

from cardbridge.audio import JitterBuffer


def frame(value: int) -> bytes:
    return struct.pack("<320h", *([value] * 320))


class JitterBufferTests(unittest.TestCase):
    def test_waits_for_target_depth_and_plays_in_sequence(self) -> None:
        jitter = JitterBuffer(target_ms=40)
        jitter.feed(10, frame(10))
        self.assertEqual(jitter.read_samples(320), [0] * 320)
        jitter.feed(11, frame(11))
        self.assertEqual(jitter.read_samples(320), [10] * 320)
        self.assertEqual(jitter.read_samples(320), [11] * 320)

    def test_missing_frame_is_replaced_with_silence(self) -> None:
        jitter = JitterBuffer(target_ms=20)
        jitter.feed(20, frame(20))
        jitter.feed(22, frame(22))
        self.assertEqual(jitter.read_samples(320), [20] * 320)
        self.assertEqual(jitter.read_samples(320), [0] * 320)
        self.assertEqual(jitter.read_samples(320), [22] * 320)
        self.assertEqual(jitter.lost, 1)

    def test_resyncs_after_capture_pause(self) -> None:
        # Mute/reconnect freezes the sender's sequence while playback keeps
        # consuming. The resumed stream must play again instead of being
        # dropped as late forever.
        jitter = JitterBuffer(target_ms=20, max_frames=50)
        jitter.feed(100, frame(1))
        self.assertEqual(jitter.read_samples(320), [1] * 320)
        for _ in range(200):  # 4 s of playback while the sender is paused.
            jitter.read_samples(320)
        jitter.feed(101, frame(2))  # Sender resumes right after its last frame.
        self.assertEqual(jitter.resyncs, 1)
        self.assertEqual(jitter.read_samples(320), [2] * 320)

    def test_small_reorder_is_still_dropped_as_late(self) -> None:
        jitter = JitterBuffer(target_ms=20, max_frames=50)
        jitter.feed(10, frame(1))
        self.assertEqual(jitter.read_samples(320), [1] * 320)
        jitter.feed(9, frame(9))  # Genuinely late duplicate/reorder.
        self.assertEqual(jitter.late, 1)
        self.assertEqual(jitter.resyncs, 0)


if __name__ == "__main__":
    unittest.main()
