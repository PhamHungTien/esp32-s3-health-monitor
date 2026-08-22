#!/usr/bin/env python3
"""Kiểm tra quy tắc giữ kết quả PPG khi tín hiệu bị hụt tạm thời."""


class PPGDisplayState:
    GAP_NEW_SESSION_MS = 180
    RELEASE_CONFIRM_MS = 450

    def __init__(self) -> None:
        self.finger_present = True
        self.heart_rate_valid = True
        self.spo2_valid = True
        self.bpm = 78.0
        self.spo2 = 97.0
        self.release_start_ms: int | None = None
        self.replace_bpm_on_next_interval = False

    def process_ir(self, ir_value: int, now_ms: int) -> None:
        has_finger = ir_value > (7000 if self.finger_present else 10000)
        if has_finger:
            if (
                self.finger_present
                and self.release_start_ms is not None
                and now_ms - self.release_start_ms >= self.GAP_NEW_SESSION_MS
            ):
                self.heart_rate_valid = False
                self.spo2_valid = False
                self.bpm = 0.0
                self.spo2 = 0.0
            self.release_start_ms = None
            self.finger_present = True
            return

        if not self.finger_present:
            return

        if self.release_start_ms is None:
            self.release_start_ms = now_ms
        elif now_ms - self.release_start_ms >= self.RELEASE_CONFIRM_MS:
            self.finger_present = False
            self.heart_rate_valid = False
            self.spo2_valid = False
            self.bpm = 0.0
            self.spo2 = 0.0

    def handle_pulse_timeout(self) -> None:
        self.replace_bpm_on_next_interval = True

    def accept_bpm(self, value: float) -> None:
        if not self.heart_rate_valid or self.replace_bpm_on_next_interval:
            self.bpm = value
            self.heart_rate_valid = True
            self.replace_bpm_on_next_interval = False

    def update_spo2(self, value: float, window_valid: bool) -> None:
        if window_valid:
            self.spo2 = 0.8 * self.spo2 + 0.2 * value
            self.spo2_valid = True


def main() -> None:
    state = PPGDisplayState()

    state.handle_pulse_timeout()
    assert state.heart_rate_valid and state.bpm == 78.0

    state.update_spo2(70.0, window_valid=False)
    assert state.spo2_valid and state.spo2 == 97.0

    state.process_ir(5000, 1000)
    state.process_ir(120000, 1120)
    assert state.finger_present
    assert state.heart_rate_valid and state.spo2_valid

    state.accept_bpm(92.0)
    assert state.bpm == 92.0

    # Rút rồi đặt lại nhanh phải tạo phiên mới, không dùng bộ dò BPM cũ.
    state.process_ir(5000, 2000)
    state.process_ir(120000, 2250)
    assert state.finger_present
    assert not state.heart_rate_valid and not state.spo2_valid

    state.heart_rate_valid = True
    state.spo2_valid = True
    state.bpm = 80.0
    state.spo2 = 98.0
    state.process_ir(5000, 3000)
    state.process_ir(5000, 3450)
    assert not state.finger_present
    assert not state.heart_rate_valid and not state.spo2_valid

    print("PPG_TRANSIENT_GAP_RETAINS_VALUES=PASS")
    print("PPG_QUICK_REINSERT_STARTS_NEW_SESSION=PASS")
    print("PPG_PULSE_TIMEOUT_RETAINS_BPM=PASS")
    print("PPG_BAD_SPO2_WINDOW_RETAINS_VALUE=PASS")
    print("PPG_CONFIRMED_FINGER_RELEASE_CLEARS_VALUES=PASS")


if __name__ == "__main__":
    main()
