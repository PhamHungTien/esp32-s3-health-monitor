#!/usr/bin/env python3
"""Kiểm tra bộ phát hiện nhịp thích nghi bằng tín hiệu PPG tổng hợp."""

from __future__ import annotations

import math
import random


class BeatDetector:
    MIN_RR_MS = 333
    MAX_RR_MS = 1500

    def __init__(self) -> None:
        self.previous_filtered = 0.0
        self.previous_slope = 0.0
        self.envelope = 0.0
        # Trạng thái ngay sau 400 ms khởi tạo: đáy đầu tiên được dùng làm mốc RR.
        self.beat_armed = True
        self.last_beat_ms = 0
        self.bpm: float | None = None
        self.first_result_ms: int | None = None

    def process(self, filtered: float, now_ms: int) -> None:
        self.envelope += 0.025 * (abs(filtered) - self.envelope)
        slope = filtered - self.previous_filtered
        threshold = max(35.0, self.envelope * 0.55)

        if filtered > threshold * 0.45:
            self.beat_armed = True

        local_minimum = self.previous_slope < 0.0 <= slope
        outside_refractory = (
            self.last_beat_ms == 0 or now_ms - self.last_beat_ms > 280
        )
        if (
            self.beat_armed
            and local_minimum
            and self.previous_filtered < -threshold
            and outside_refractory
        ):
            self.beat_armed = False
            if self.last_beat_ms == 0:
                self.last_beat_ms = now_ms
            else:
                interval = now_ms - self.last_beat_ms
                if self.MIN_RR_MS <= interval <= self.MAX_RR_MS:
                    self.bpm = 60000.0 / interval
                    self.last_beat_ms = now_ms
                    if self.first_result_ms is None:
                        self.first_result_ms = now_ms
                elif interval > self.MAX_RR_MS:
                    self.last_beat_ms = now_ms

        self.previous_slope = slope
        self.previous_filtered = filtered


def feed_sine(detector: BeatDetector, bpm: float, amplitude: float,
              duration_ms: int, noise: float = 0.0) -> None:
    generator = random.Random(20260804)
    period_ms = 60000.0 / bpm
    for now_ms in range(400, duration_ms, 10):
        signal = amplitude * math.sin(2.0 * math.pi * now_ms / period_ms)
        signal += generator.uniform(-noise, noise)
        detector.process(signal, now_ms)


def main() -> None:
    normal = BeatDetector()
    feed_sine(normal, bpm=75.0, amplitude=80.0, duration_ms=5000, noise=8.0)
    assert normal.bpm is not None
    assert abs(normal.bpm - 75.0) < 3.0
    assert normal.first_result_ms is not None and normal.first_result_ms <= 2400

    low_perfusion = BeatDetector()
    feed_sine(low_perfusion, bpm=60.0, amplitude=48.0,
              duration_ms=6000, noise=4.0)
    assert low_perfusion.bpm is not None
    assert abs(low_perfusion.bpm - 60.0) < 3.0

    noise_only = BeatDetector()
    generator = random.Random(42)
    for now_ms in range(400, 6000, 10):
        noise_only.process(generator.uniform(-20.0, 20.0), now_ms)
    assert noise_only.bpm is None

    print("PPG_NORMAL_SIGNAL_BPM=PASS")
    print("PPG_FIRST_RESULT_WITHIN_2_4S=PASS")
    print("PPG_LOW_PERFUSION_SIGNAL_BPM=PASS")
    print("PPG_NOISE_REJECTION=PASS")


if __name__ == "__main__":
    main()
