#!/usr/bin/env python3
"""Kiểm tra lặp lại các ngưỡng đếm bước và té ngã của firmware.

Tệp chỉ dùng thư viện chuẩn Python và chạy trên máy tính.
Nó kiểm tra logic trạng thái bằng dữ liệu tổng hợp.
Kết quả không thay thế phép thử khi đeo thiết bị thật.
"""

from __future__ import annotations

import math
import random


class StepDetector:
    def __init__(self) -> None:
        self.gravity = 1.0
        self.dynamic_filtered = 0.0
        self.peak_armed = True
        self.last_peak_ms = 0
        self.walking_sequence = False
        self.step_count = 0

    def process(self, total_g: float, now_ms: int) -> None:
        self.gravity += 0.03 * (total_g - self.gravity)
        dynamic_g = total_g - self.gravity
        self.dynamic_filtered += 0.25 * (dynamic_g - self.dynamic_filtered)

        if self.dynamic_filtered < -0.05:
            self.peak_armed = True

        enough_time = now_ms - self.last_peak_ms > 280
        if (
            self.peak_armed
            and self.dynamic_filtered > 0.11
            and enough_time
            and total_g < 1.8
        ):
            interval = 0 if self.last_peak_ms == 0 else now_ms - self.last_peak_ms
            if 280 <= interval <= 1800:
                if self.walking_sequence:
                    self.step_count += 1
                else:
                    self.step_count += 2
                    self.walking_sequence = True
            else:
                self.walking_sequence = False
            self.last_peak_ms = now_ms
            self.peak_armed = False

        if self.last_peak_ms and now_ms - self.last_peak_ms > 2200:
            self.walking_sequence = False


class FallDetector:
    NORMAL = 0
    FREEFALL = 1
    IMPACT = 2
    ALERT = 3
    NAMES = ("NORMAL", "FREEFALL", "IMPACT", "ALERT")

    def __init__(self) -> None:
        self.state = self.NORMAL
        self.state_ms = 0
        self.low_g_start_ms = 0
        self.previous_g = 1.0
        self.motion_level = 0.0

    def process(self, acceleration_g: float, now_ms: int) -> None:
        movement = abs(acceleration_g - self.previous_g)
        self.motion_level += 0.15 * (movement - self.motion_level)
        self.previous_g = acceleration_g

        if self.state == self.NORMAL:
            if acceleration_g < 0.45:
                if self.low_g_start_ms == 0:
                    self.low_g_start_ms = now_ms
                if now_ms - self.low_g_start_ms >= 120:
                    self.state = self.FREEFALL
                    self.state_ms = now_ms
            else:
                self.low_g_start_ms = 0
            if acceleration_g > 2.8:
                self.state = self.IMPACT
                self.state_ms = now_ms

        elif self.state == self.FREEFALL:
            if acceleration_g > 2.2:
                self.state = self.IMPACT
                self.state_ms = now_ms
            elif now_ms - self.state_ms > 1200:
                self.state = self.NORMAL
                self.low_g_start_ms = 0

        elif self.state == self.IMPACT:
            is_still = (
                self.motion_level < 0.08 and 0.72 < acceleration_g < 1.28
            )
            if now_ms - self.state_ms > 1800 and is_still:
                self.state = self.ALERT
                self.state_ms = now_ms
            elif now_ms - self.state_ms > 5000:
                self.state = self.NORMAL


def feed(detector, samples: list[float], now_ms: int = 0) -> int:
    for value in samples:
        detector.process(value, now_ms)
        now_ms += 20
    return now_ms


def main() -> None:
    rest = StepDetector()
    feed(rest, [1.08] * 1500)
    assert rest.step_count == 0

    generator = random.Random(20260804)
    noise = StepDetector()
    feed(noise, [1.08 + generator.uniform(-0.02, 0.02) for _ in range(1500)])
    assert noise.step_count == 0

    isolated = StepDetector()
    feed(isolated, [1.0] * 100 + [0.85] * 8 + [1.35] * 8 + [1.0] * 100)
    assert isolated.step_count == 0

    walking = StepDetector()
    samples = [1.0] * 100
    for _ in range(20):
        samples.extend(
            1.0 + 0.24 * math.sin(2 * math.pi * index / 30 - math.pi / 2)
            for index in range(30)
        )
    feed(walking, samples)
    assert walking.step_count == 20

    fall = FallDetector()
    feed(fall, [1.0] * 100 + [0.20] * 10 + [3.10] * 2 + [1.0] * 150)
    assert fall.state == FallDetector.ALERT

    incomplete = FallDetector()
    feed(incomplete, [1.0] * 100 + [0.20] * 10 + [1.0] * 80)
    assert incomplete.state == FallDetector.NORMAL

    print("REST_30S_STEPS=0")
    print("NOISE_30S_STEPS=0")
    print("ISOLATED_MOTION_STEPS=0")
    print("SYNTHETIC_20_CYCLES_STEPS=20")
    print("SYNTHETIC_FALL_STATE=ALERT")
    print("INCOMPLETE_EVENT_STATE=NORMAL")


if __name__ == "__main__":
    main()
