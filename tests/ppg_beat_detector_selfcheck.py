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
        # Sau 250 ms khởi tạo, chờ tín hiệu đi qua vùng giữa trước khi lấy mốc.
        self.beat_armed = False
        self.last_beat_ms = 0
        self.pulse_polarity = 0
        self.bpm: float | None = None
        self.first_result_ms: int | None = None

    def process(self, filtered: float, now_ms: int) -> None:
        self.envelope += 0.025 * (abs(filtered) - self.envelope)
        slope = filtered - self.previous_filtered
        threshold = max(22.0, self.envelope * 0.55)

        crossed_positive = self.previous_filtered <= threshold < filtered
        crossed_negative = self.previous_filtered >= -threshold > filtered
        if self.pulse_polarity == 0 and abs(filtered) < threshold * 0.25:
            self.beat_armed = True
        elif self.pulse_polarity > 0 and filtered < -threshold * 0.45:
            self.beat_armed = True
        elif self.pulse_polarity < 0 and filtered > threshold * 0.45:
            self.beat_armed = True
        selected_edge = (
            crossed_positive or crossed_negative
            if self.pulse_polarity == 0
            else crossed_positive if self.pulse_polarity > 0 else crossed_negative
        )
        outside_refractory = (
            self.last_beat_ms == 0 or now_ms - self.last_beat_ms > 280
        )
        if (
            self.beat_armed
            and selected_edge
            and outside_refractory
        ):
            if self.pulse_polarity == 0:
                self.pulse_polarity = 1 if crossed_positive else -1
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
    for now_ms in range(250, duration_ms, 10):
        signal = amplitude * math.sin(2.0 * math.pi * now_ms / period_ms)
        signal += generator.uniform(-noise, noise)
        detector.process(signal, now_ms)


def main() -> None:
    normal = BeatDetector()
    feed_sine(normal, bpm=75.0, amplitude=80.0, duration_ms=5000, noise=8.0)
    assert normal.bpm is not None
    assert abs(normal.bpm - 75.0) < 3.0
    assert normal.first_result_ms is not None and normal.first_result_ms <= 1900

    low_perfusion = BeatDetector()
    feed_sine(low_perfusion, bpm=60.0, amplitude=48.0,
              duration_ms=6000, noise=4.0)
    assert low_perfusion.bpm is not None
    assert abs(low_perfusion.bpm - 60.0) < 3.0

    inverted = BeatDetector()
    feed_sine(inverted, bpm=72.0, amplitude=-55.0,
              duration_ms=5000, noise=4.0)
    assert inverted.bpm is not None
    assert abs(inverted.bpm - 72.0) < 3.0

    noise_only = BeatDetector()
    generator = random.Random(42)
    for now_ms in range(250, 6000, 10):
        noise_only.process(generator.uniform(-20.0, 20.0), now_ms)
    assert noise_only.bpm is None

    print("PPG_NORMAL_SIGNAL_BPM=PASS")
    print("PPG_FIRST_RESULT_WITHIN_1_9S=PASS")
    print("PPG_LOW_PERFUSION_SIGNAL_BPM=PASS")
    print("PPG_AUTO_POLARITY_SIGNAL_BPM=PASS")
    print("PPG_NOISE_REJECTION=PASS")


if __name__ == "__main__":
    main()
