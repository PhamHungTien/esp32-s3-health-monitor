#!/usr/bin/env python3
"""Kiểm tra bộ phát hiện nhịp thích nghi bằng tín hiệu PPG tổng hợp."""

from __future__ import annotations

import math
import random


class BeatDetector:
    MIN_RR_MS = 333
    MAX_RR_MS = 1500
    MIN_HALF_RR_MS = 167
    MAX_HALF_RR_MS = 750

    def __init__(self) -> None:
        self.previous_filtered = 0.0
        self.previous_slope = 0.0
        self.envelope = 0.0
        # Sau 120 ms khởi tạo, theo dõi cả hai cực tính đến khi có RR hợp lệ.
        self.beat_armed = False
        self.last_beat_ms = 0
        self.pulse_polarity = 0
        self.positive_edge_ms = 0
        self.negative_edge_ms = 0
        self.bpm: float | None = None
        self.first_result_ms: int | None = None
        self.first_confirmed_ms: int | None = None
        self.provisional = False

    def process(self, filtered: float, now_ms: int) -> None:
        self.envelope += 0.025 * (abs(filtered) - self.envelope)
        slope = filtered - self.previous_filtered
        threshold = max(22.0, self.envelope * 0.55)

        crossed_positive = self.previous_filtered <= threshold < filtered
        crossed_negative = self.previous_filtered >= -threshold > filtered
        if self.pulse_polarity == 0 and abs(filtered) < threshold * 0.35:
            self.beat_armed = True
        elif self.pulse_polarity > 0 and filtered < threshold * 0.10:
            self.beat_armed = True
        elif self.pulse_polarity < 0 and filtered > -threshold * 0.10:
            self.beat_armed = True

        if self.pulse_polarity == 0 and crossed_positive:
            if self.positive_edge_ms:
                interval = now_ms - self.positive_edge_ms
                if self.MIN_RR_MS <= interval <= self.MAX_RR_MS:
                    self.pulse_polarity = 1
                    self.last_beat_ms = now_ms
                    self.bpm = 60000.0 / interval
                    self.provisional = False
                    self.beat_armed = False
                    if self.first_confirmed_ms is None:
                        self.first_confirmed_ms = now_ms
                    if self.first_result_ms is None:
                        self.first_result_ms = now_ms
            self.positive_edge_ms = now_ms
        if self.pulse_polarity == 0 and crossed_negative:
            if self.negative_edge_ms:
                interval = now_ms - self.negative_edge_ms
                if self.MIN_RR_MS <= interval <= self.MAX_RR_MS:
                    self.pulse_polarity = -1
                    self.last_beat_ms = now_ms
                    self.bpm = 60000.0 / interval
                    self.provisional = False
                    self.beat_armed = False
                    if self.first_confirmed_ms is None:
                        self.first_confirmed_ms = now_ms
                    if self.first_result_ms is None:
                        self.first_result_ms = now_ms
            self.negative_edge_ms = now_ms

        if (
            self.pulse_polarity == 0
            and self.bpm is None
            and self.positive_edge_ms
            and self.negative_edge_ms
        ):
            half_interval = abs(self.positive_edge_ms - self.negative_edge_ms)
            if self.MIN_HALF_RR_MS <= half_interval <= self.MAX_HALF_RR_MS:
                self.bpm = 30000.0 / half_interval
                self.provisional = True
                self.first_result_ms = now_ms

        selected_edge = (
            crossed_positive if self.pulse_polarity > 0
            else crossed_negative if self.pulse_polarity < 0
            else False
        )
        outside_refractory = (
            self.last_beat_ms == 0 or now_ms - self.last_beat_ms > 280
        )
        if (
            self.beat_armed
            and selected_edge
            and outside_refractory
        ):
            self.beat_armed = False
            if self.last_beat_ms == 0:
                self.last_beat_ms = now_ms
            else:
                interval = now_ms - self.last_beat_ms
                if self.MIN_RR_MS <= interval <= self.MAX_RR_MS:
                    self.bpm = 60000.0 / interval
                    self.provisional = False
                    self.last_beat_ms = now_ms
                    if self.first_confirmed_ms is None:
                        self.first_confirmed_ms = now_ms
                elif interval > self.MAX_RR_MS:
                    self.last_beat_ms = now_ms

        self.previous_slope = slope
        self.previous_filtered = filtered


def feed_sine(detector: BeatDetector, bpm: float, amplitude: float,
              duration_ms: int, noise: float = 0.0) -> None:
    generator = random.Random(20260804)
    period_ms = 60000.0 / bpm
    for now_ms in range(120, duration_ms, 10):
        signal = amplitude * math.sin(2.0 * math.pi * now_ms / period_ms)
        signal += generator.uniform(-noise, noise)
        detector.process(signal, now_ms)


def first_spo2_result_ms() -> int | None:
    """Mô phỏng cửa sổ AC/DC 64 mẫu của firmware ở 100 Hz."""
    dc_red = 130_000.0
    dc_ir = 134_000.0
    red_square_sum = 0.0
    ir_square_sum = 0.0
    window_samples = 0
    for now_ms in range(0, 2000, 10):
        phase = 2.0 * math.pi * now_ms / 800.0
        red_value = 130_000.0 + 900.0 * math.sin(phase)
        ir_value = 134_000.0 + 1_400.0 * math.sin(phase)
        alpha = 0.20 if now_ms < 120 else 0.01
        dc_red += alpha * (red_value - dc_red)
        dc_ir += alpha * (ir_value - dc_ir)
        if now_ms < 120:
            continue
        red_ac = red_value - dc_red
        ir_ac = ir_value - dc_ir
        red_square_sum += red_ac * red_ac
        ir_square_sum += ir_ac * ir_ac
        window_samples += 1
        if window_samples == 64:
            red_rms = math.sqrt(red_square_sum / window_samples)
            ir_rms = math.sqrt(ir_square_sum / window_samples)
            perfusion = 100.0 * ir_rms / dc_ir
            ratio = (red_rms / dc_red) / (ir_rms / dc_ir)
            if perfusion > 0.08 and 0.2 < ratio < 2.0 and now_ms >= 700:
                return now_ms
    return None


def main() -> None:
    normal = BeatDetector()
    feed_sine(normal, bpm=75.0, amplitude=80.0, duration_ms=5000, noise=8.0)
    assert normal.bpm is not None
    assert abs(normal.bpm - 75.0) < 3.0
    assert normal.first_result_ms is not None and normal.first_result_ms <= 1000
    assert normal.first_confirmed_ms is not None and normal.first_confirmed_ms <= 1750

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
    for now_ms in range(120, 6000, 10):
        noise_only.process(generator.uniform(-20.0, 20.0), now_ms)
    assert noise_only.bpm is None

    spo2_result_ms = first_spo2_result_ms()
    assert spo2_result_ms is not None and spo2_result_ms <= 760

    print("PPG_NORMAL_SIGNAL_BPM=PASS")
    print("PPG_PROVISIONAL_RESULT_WITHIN_1S=PASS")
    print("PPG_CONFIRMED_RESULT_WITHIN_1_75S=PASS")
    print("PPG_LOW_PERFUSION_SIGNAL_BPM=PASS")
    print("PPG_AUTO_POLARITY_SIGNAL_BPM=PASS")
    print("PPG_NOISE_REJECTION=PASS")
    print("PPG_SPO2_FIRST_RESULT_WITHIN_760MS=PASS")


if __name__ == "__main__":
    main()
