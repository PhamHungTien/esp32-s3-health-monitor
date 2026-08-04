# ESP32-S3 Health Monitor

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-12355B)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino%20Core-187795)](https://github.com/espressif/arduino-esp32)
[![Dependencies](https://img.shields.io/badge/third--party%20libraries-none-287D52)](#software-dependencies)
[![License](https://img.shields.io/badge/license-MIT-F4A261)](LICENSE)
[![Status](https://img.shields.io/badge/status-academic%20prototype-F4A261)](#safety-and-limitations)

An academic wearable prototype built around the ESP32-S3 N16R8. The system measures heart rate, estimates blood oxygen saturation, counts steps, detects possible falls, receives GPS coordinates, displays live status on an OLED, and drives an audible alert.

The sensor drivers, display routines, signal processing, fall-detection state machine, step counter, and NMEA parser are implemented directly in the Arduino sketch. No third-party sensor, display, GPS, or signal-processing library is used.

> [!WARNING]
> This project is an educational prototype, not a certified medical or personal-safety device. Do not use its readings or alerts for diagnosis, treatment, emergency response, or other safety-critical decisions.

## Features

- Heart-rate measurement from MAX30102 infrared PPG samples.
- Immediate BPM output after the first valid RR interval, followed by filtered updates.
- Experimental SpO2 estimation using the RED/IR ratio-of-ratios method.
- Finger-presence detection with separate activation and release thresholds.
- Step counting from filtered three-axis acceleration.
- Fall detection using a `NORMAL -> FREE_FALL -> IMPACT -> ALERT` state machine.
- GPS reception over UART with GGA/RMC parsing and NMEA checksum validation.
- 128 x 64 SSD1306 OLED interface for BPM, SpO2, steps, GPS, and fall alerts.
- Piezo/buzzer output for startup feedback and fall warnings.
- Non-blocking task scheduling based on `millis()`.
- Automatic I2C device re-probing after a disconnection.
- Serial diagnostics at 115200 baud.

## Hardware

| Component | Purpose | Interface/address |
|---|---|---|
| ESP32-S3 N16R8 | Main controller | 16 MB flash, 8 MB PSRAM |
| SSD1306 OLED, 128 x 64 | Local user interface | I2C, `0x3C` |
| MAX30102 | RED/IR PPG acquisition | I2C, `0x57` |
| MPU-compatible IMU | Acceleration, steps, and fall detection | I2C, `0x68` |
| UART GPS module | Position and satellite data | UART1, 9600 baud |
| Piezo or active/passive buzzer | Audible alert | PWM/LEDC |

## Wiring

| ESP32-S3 pin | Connection | Notes |
|---|---|---|
| GPIO8 | OLED, MAX30102, and IMU SDA | Shared I2C data line |
| GPIO9 | OLED, MAX30102, and IMU SCL | Shared I2C clock line |
| GPIO21 | GPS TX | ESP32 UART1 RX |
| GPIO47 | GPS RX | ESP32 UART1 TX |
| GPIO42 | Piezo/buzzer positive terminal | LEDC PWM output |
| GND | All module grounds and buzzer negative terminal | A common ground is required |
| 3.3 V | OLED, MAX30102, and compatible IMU power | Confirm the voltage rating of each breakout board |

On the Freenove ESP32-S3 camera board used for this prototype, GPIO8 and GPIO9 are also associated with camera signals. The camera must remain disconnected while these pins are used as the I2C bus.

The GPS connections are crossed: GPS TX goes to ESP32 GPIO21, and GPS RX goes to ESP32 GPIO47. GPIO43 and GPIO44 are intentionally left available for the board's serial connection.

> [!IMPORTANT]
> Do not connect a low-impedance speaker directly to GPIO42. Use a suitable transistor driver and protection components. A small piezo device may be connected as described above if its electrical ratings are compatible.

## Software dependencies

The firmware only uses components supplied with the ESP32 Arduino Core:

- `Arduino.h`
- `Wire.h`
- `HardwareSerial.h`
- the standard C/C++ math functions

No Adafruit SSD1306/GFX, SparkFun MAX3010x, TinyGPS++, MPU, or external signal-processing library is required.

## Arduino configuration

Install the Espressif ESP32 board package in Arduino IDE, then select an ESP32-S3 target matching the board. The prototype was built with the following relevant settings:

| Setting | Value |
|---|---|
| Target | ESP32-S3 with N16R8 memory configuration |
| Flash size | 16 MB |
| PSRAM | OPI PSRAM, 8 MB |
| USB mode | Hardware CDC/USB native |
| USB CDC on boot | Enabled |
| Serial monitor | 115200 baud |
| Upload speed | 115200 baud when troubleshooting unstable uploads |

Exact menu names can vary between ESP32 Arduino Core releases and board definitions.

## Build and upload

1. Connect the sensors according to the wiring table.
2. Open [`donghothongminh/donghothongminh.ino`](donghothongminh/donghothongminh.ino) in Arduino IDE.
3. Select the correct ESP32-S3 board, USB port, flash size, and PSRAM mode.
4. Compile the sketch.
5. Upload it through the board's native USB connection.
6. Open Serial Monitor at 115200 baud and verify the startup status.

A normal startup should identify the OLED, IMU, MAX30102, buzzer channel, and GPS UART. The program periodically attempts to initialize an I2C device again if it becomes unavailable.

If uploading fails, hold the board's BOOT button, press and release RESET, start the upload, and then release BOOT when the connection begins. Recheck the selected port after every reset because the USB device name may change.

## Operating the prototype

1. Keep the device still during startup.
2. Place a fingertip steadily over the MAX30102 LEDs and photodiode.
3. Wait for a valid pulse interval; the first valid BPM is displayed immediately.
4. Keep the finger still while the SpO2 estimate is calculated over a longer sample window.
5. Move the GPS antenna to an open outdoor area for faster satellite acquisition.
6. Check the OLED and Serial Monitor for sensor, GPS, step, and fall-state information.

OLED GPS states:

- `WAIT`: no GPS byte has been received yet.
- `LOST`: GPS data was previously received but the UART stream stopped.
- `NOFIX`: valid NMEA data is arriving, but no current position is available.
- `FIX`: a recent valid position has been decoded.

## Algorithm overview

### Heart rate and SpO2

The MAX30102 is configured for RED/IR acquisition at 100 samples per second. A slow estimator separates the DC component, while filtered AC samples are used for pulse detection. BPM is calculated from valid RR intervals. SpO2 is estimated from the normalized RED and IR AC/DC ratio and then bounded to the prototype's display range.

### Step counting

The acceleration magnitude is separated into gravity and dynamic components. A step is accepted only after a negative-to-positive threshold sequence, a valid peak interval, and an impact rejection check. The first isolated peak is held until a walking sequence is confirmed.

### Fall detection

The fall detector looks for a timed sequence of low acceleration, impact, and post-impact inactivity. State timeouts return the detector to normal when the complete sequence is not observed.

### GPS

UART bytes are assembled into NMEA sentences. Each sentence must pass its XOR checksum before GGA or RMC fields are parsed. Latitude and longitude are converted from degrees/minutes to signed decimal degrees.

## Verification

The repository includes a lightweight host-side consistency test for the decision logic:

```bash
python3 tests/algorithm_selfcheck.py
```

The test uses only the Python standard library and mirrors the firmware thresholds. It checks algorithm behavior but does not replace measurements collected from real hardware.

## Repository structure

```text
.
├── donghothongminh/
│   └── donghothongminh.ino       # Complete ESP32-S3 firmware
├── reports_latex/                 # LaTeX source for group and individual reports
├── output/pdf/                    # Compiled submission-ready reports
├── tests/
│   └── algorithm_selfcheck.py     # Host-side algorithm checks
└── README.md
```

The final PDF reports are available in [`output/pdf`](output/pdf). To rebuild them on a system with XeLaTeX and `latexmk` installed:

```bash
cd reports_latex
./build_all.sh
```

## Safety and limitations

- SpO2 values are experimental and have not been calibrated against a certified reference device.
- Finger motion, pressure, ambient light, skin properties, and sensor placement can affect PPG results.
- Step counting requires comparison against a controlled walking trial to determine accuracy.
- Fall detection has been checked with synthetic sequences but requires carefully supervised real-world validation using mats and safety support.
- Indoor GPS reception may remain in `NOFIX` or report few satellites.
- The prototype does not yet include a battery-management circuit, enclosure, vibration motor, physical alert-cancel button, or remote emergency notification channel.

## Project team

This project was developed by students from the Faculty of Electronics and Telecommunications, University of Science, Vietnam National University Ho Chi Minh City.

| Member | Student ID | Primary responsibility |
|---|---:|---|
| Pham Hung Tien | 24207043 | System integration, I2C, OLED interface, scheduling, and upload workflow |
| Ho Si Phu | 24207101 | MAX30102 acquisition, PPG processing, heart rate, and SpO2 estimation |
| Quach Gia Thinh | 24207105 | IMU processing, step counting, fall detection, GPS, and NMEA parsing |

## License

This project is released under the [MIT License](LICENSE). You may use, copy, modify, merge, publish, distribute, sublicense, and sell copies of the software subject to the license terms.
