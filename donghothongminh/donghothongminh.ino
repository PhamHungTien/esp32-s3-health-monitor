/*
 * Thiết bị theo dõi sức khỏe dùng ESP32-S3 N16R8.
 * Phần cứng: SSD1306, MAX30102, IMU, GPS UART và piezo.
 * Phụ thuộc: Arduino Core, Wire và HardwareSerial; không dùng thư viện cảm biến
 * hoặc thư viện xử lý tín hiệu bên ngoài.
 */

#include <Arduino.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <math.h>
#include "web_dashboard.h"

// Cấu hình phần cứng và chu kỳ tác vụ
constexpr uint8_t I2C_SDA = 8;
constexpr uint8_t I2C_SCL = 9;
constexpr uint32_t I2C_SENSOR_HZ = 100000;
constexpr uint32_t I2C_DISPLAY_HZ = 400000;

constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t IMU_ADDR = 0x68;
constexpr uint8_t MAX30102_ADDR = 0x57;

constexpr uint32_t IMU_PERIOD_MS = 20;
constexpr uint32_t DISPLAY_PERIOD_MS = 100;
constexpr uint32_t STATUS_PERIOD_MS = 1000;
constexpr uint32_t REPROBE_PERIOD_MS = 2000;

// UART1 dành cho GPS; GPIO43/44 được giữ lại cho cổng nối tiếp của bo.
constexpr int8_t GPS_RX = 21;
constexpr int8_t GPS_TX = 47;
HardwareSerial gpsSerial(1);

// Piezo hai dây: cực dương vào GPIO42, cực âm vào GND.
// Loa trở kháng thấp cần tầng transistor khuếch đại riêng.
constexpr uint8_t BUZZER_PIN = 42;
bool buzzerOK = false;

// Cổng theo dõi cục bộ: ESP32-S3 tự phát Wi-Fi, không cần router/Internet.
constexpr char WIFI_AP_SSID[] = "health-monitor";
constexpr char WIFI_AP_PASSWORD[] = "99999999";
IPAddress wifiLocalIP(192, 168, 4, 1);
IPAddress wifiGateway(192, 168, 4, 1);
IPAddress wifiSubnet(255, 255, 255, 0);
// Chỉ dùng lớp truyền tải của ESP32 Arduino Core. Bộ phân tích HTTP và gói DNS
// ở bên dưới được nhóm tự viết, không dùng WebServer hoặc DNSServer có sẵn.
WiFiServer httpSocket(80);
WiFiUDP dnsSocket;
WiFiClient activeHttpClient;
char httpRequestLine[160] = {0};
uint16_t httpRequestLength = 0;
uint32_t httpClientStartMs = 0;
bool wifiPortalOK = false;

// Framebuffer OLED 128 x 64, mỗi bit tương ứng một điểm ảnh.
uint8_t oledBuffer[1024];

bool oledOK = false;
bool imuOK = false;
bool maxOK = false;

uint32_t gpsByteCount = 0;
uint32_t lastGPSByteMs = 0;
char gpsWorking[96] = {0};
uint8_t gpsWorkingLength = 0;

bool gpsFix = false;
bool gpsPositionKnown = false;
double gpsLatitude = 0.0;
double gpsLongitude = 0.0;
uint8_t gpsSatellites = 0;
uint32_t lastGPSFixMs = 0;

struct PPGMetrics {
  bool fingerPresent;
  bool heartRateValid;
  bool heartRateProvisional;
  bool spo2Valid;
  float bpm;
  float spo2;
  float signalQuality;
  uint32_t redRaw;
  uint32_t irRaw;
  uint32_t lastBeatMs;
};

PPGMetrics ppg = {};

struct PPGFilterState {
  float dcRed;
  float dcIr;
  float filtered;
  float previousFiltered;
  float previousSlope;
  float envelope;
  double redSquareSum;
  double irSquareSum;
  uint16_t windowSamples;
  uint32_t fingerStartMs;
  uint32_t fingerReleaseStartMs;
  bool beatArmed;
  bool replaceBpmOnNextInterval;
  int8_t pulsePolarity;
  uint32_t positiveEdgeMs;
  uint32_t negativeEdgeMs;
  uint32_t provisionalBpmMs;
};

PPGFilterState ppgFilter = {};

enum FallState : uint8_t {
  FALL_NORMAL,
  FALL_FREE_FALL,
  FALL_IMPACT,
  FALL_ALERT
};

FallState fallState = FALL_NORMAL;
uint32_t fallStateMs = 0;
float accelerationG = 1.0f;
float motionLevel = 0.0f;
uint32_t stepCount = 0;

// Giao tiếp I2C
bool I2C_DevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool I2C_WriteRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool I2C_ReadRegister(uint8_t address, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}

// Hiển thị OLED SSD1306
bool OLED_WriteCommand(uint8_t cmd) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00); // Byte điều khiển cho lệnh SSD1306.
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

bool OLED_Init() {
  // Chuỗi lệnh cho OLED 128 x 64, địa chỉ ngang và charge pump nội.
  static const uint8_t initSequence[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
    0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
  };
  for (uint8_t command : initSequence) {
    if (!OLED_WriteCommand(command)) return false;
  }
  return true;
}

bool OLED_Update() {
  if (!oledOK) return false;
  // Dùng 400 kHz khi truyền framebuffer để rút ngắn thời gian cập nhật.
  Wire.setClock(I2C_DISPLAY_HZ);
  bool success = OLED_WriteCommand(0x21) && OLED_WriteCommand(0) &&
                 OLED_WriteCommand(127) && OLED_WriteCommand(0x22) &&
                 OLED_WriteCommand(0) && OLED_WriteCommand(7);

  // Gửi framebuffer theo từng khối 16 byte.
  for (int i = 0; success && i < 1024; i += 16) {
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x40); // Byte điều khiển cho dữ liệu hiển thị.
    for (int j = 0; j < 16; j++) {
      Wire.write(oledBuffer[i + j]);
    }
    success = Wire.endTransmission() == 0;
  }
  Wire.setClock(I2C_SENSOR_HZ);
  return success;
}

void OLED_Clear() {
  memset(oledBuffer, 0x00, sizeof(oledBuffer));
}

// Font 5 x 7 cho ký tự ASCII từ dấu cách đến chữ Z.
const uint8_t FONT5X7[][5] = {
  {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00},
  {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00},
  {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
  {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
  {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E},
  {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41},
  {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x0C,0x02,0x7F},
  {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E},
  {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07},
  {0x61,0x51,0x49,0x45,0x43}
};

void OLED_DrawPixel(int16_t x, int16_t y, bool on = true) {
  if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
  uint16_t index = x + (y / 8) * 128;
  uint8_t mask = 1U << (y & 7);
  if (on) oledBuffer[index] |= mask;
  else oledBuffer[index] &= ~mask;
}

void OLED_DrawChar(int16_t x, int16_t y, char c) {
  if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
  if (c < 32 || c > 90) c = '?';
  const uint8_t *glyph = FONT5X7[c - 32];

  for (uint8_t column = 0; column < 5; column++) {
    uint8_t bits = glyph[column];
    for (uint8_t row = 0; row < 7; row++) {
      if (bits & (1U << row)) OLED_DrawPixel(x + column, y + row);
    }
  }
}

void OLED_DrawText(int16_t x, int16_t y, const char *text) {
  while (*text && x <= 122) {
    OLED_DrawChar(x, y, *text++);
    x += 6;
  }
}

void OLED_DrawCharScaled(int16_t x, int16_t y, char c, uint8_t scale) {
  if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
  if (c < 32 || c > 90) c = '?';
  const uint8_t *glyph = FONT5X7[c - 32];
  for (uint8_t column = 0; column < 5; column++) {
    for (uint8_t row = 0; row < 7; row++) {
      if (glyph[column] & (1U << row)) {
        for (uint8_t dx = 0; dx < scale; dx++) {
          for (uint8_t dy = 0; dy < scale; dy++) {
            OLED_DrawPixel(x + column * scale + dx, y + row * scale + dy);
          }
        }
      }
    }
  }
}

void OLED_DrawTextScaled(int16_t x, int16_t y, const char *text, uint8_t scale) {
  int16_t step = 6 * scale;
  while (*text && x + 5 * scale <= 127) {
    OLED_DrawCharScaled(x, y, *text++, scale);
    x += step;
  }
}

void OLED_DrawHLine(int16_t x, int16_t y, int16_t width) {
  for (int16_t i = 0; i < width; i++) OLED_DrawPixel(x + i, y);
}

void OLED_DrawVLine(int16_t x, int16_t y, int16_t height) {
  for (int16_t i = 0; i < height; i++) OLED_DrawPixel(x, y + i);
}

void OLED_DrawRect(int16_t x, int16_t y, int16_t width, int16_t height) {
  OLED_DrawHLine(x, y, width);
  OLED_DrawHLine(x, y + height - 1, width);
  for (int16_t i = 0; i < height; i++) {
    OLED_DrawPixel(x, y + i);
    OLED_DrawPixel(x + width - 1, y + i);
  }
}

void Buzzer_Init() {
  buzzerOK = ledcAttach(BUZZER_PIN, 1800, 8);
  if (buzzerOK) {
    ledcWriteTone(BUZZER_PIN, 1800);
    delay(90); // Một tiếng ngắn xác nhận loa đã được khởi tạo.
    ledcWriteTone(BUZZER_PIN, 0);
  }
}

void Buzzer_Update() {
  static bool wasAlert = false;
  static uint32_t lastPhase = UINT32_MAX;
  if (!buzzerOK) return;

  bool alert = fallState == FALL_ALERT;
  if (!alert) {
    if (wasAlert) ledcWriteTone(BUZZER_PIN, 0);
    wasAlert = false;
    lastPhase = UINT32_MAX;
    return;
  }

  uint32_t phase = (millis() / 250UL) & 3U;
  if (!wasAlert || phase != lastPhase) {
    // Hai tần số xen kẽ, mỗi pha dài 250 ms.
    ledcWriteTone(BUZZER_PIN, phase == 0 ? 1700 : (phase == 2 ? 2300 : 0));
    lastPhase = phase;
  }
  wasAlert = true;
}

void OLED_DrawHeart(int16_t x, int16_t y) {
  static const uint8_t heart[7] = {
    0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08, 0x00
  };
  for (uint8_t row = 0; row < 7; row++) {
    for (uint8_t column = 0; column < 7; column++) {
      if (heart[row] & (1U << column)) OLED_DrawPixel(x + column, y + row);
    }
  }
}

// Cảm biến gia tốc
bool IMU_Init() {
  uint8_t whoAmI = 0;
  if (!I2C_DevicePresent(IMU_ADDR)) return false;
  if (!I2C_ReadRegister(IMU_ADDR, 0x75, whoAmI)) return false;
  Serial.printf("[IMU] WHO_AM_I=0x%02X\n", whoAmI);
  // Các mã 0x68, 0x70 và 0x74 dùng chung nhóm thanh ghi gia tốc.
  if (whoAmI != 0x68 && whoAmI != 0x70 && whoAmI != 0x74) return false;

  // Clock nội, thang đo +/-2 g và bộ lọc số khoảng 44 Hz.
  if (!I2C_WriteRegister(IMU_ADDR, 0x6B, 0x00)) return false;
  if (!I2C_WriteRegister(IMU_ADDR, 0x1C, 0x00)) return false;
  if (!I2C_WriteRegister(IMU_ADDR, 0x1A, 0x03)) return false;
  return true;
}

bool IMU_ReadAccel(int16_t &ax, int16_t &ay, int16_t &az) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x3B); // Thanh ghi đầu tiên chứa dữ liệu Gia tốc ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;

  Wire.requestFrom((uint8_t)IMU_ADDR, (size_t)6); // Đọc liên tiếp 6 byte (X, Y, Z)
  if (Wire.available() >= 6) {
    ax = (Wire.read() << 8) | Wire.read();
    ay = (Wire.read() << 8) | Wire.read();
    az = (Wire.read() << 8) | Wire.read();
    return true;
  }
  return false;
}

const char *Fall_StateText() {
  switch (fallState) {
    case FALL_FREE_FALL: return "FREE";
    case FALL_IMPACT: return "CHECK";
    case FALL_ALERT: return "FALL";
    default: return "OK";
  }
}

void Step_Process(float totalG, uint32_t now) {
  static float gravity = 1.0f;
  static float dynamicFiltered = 0.0f;
  static bool peakArmed = true;
  static uint32_t lastPeakMs = 0;
  static bool walkingSequence = false;

  gravity += 0.03f * (totalG - gravity);
  float dynamicG = totalG - gravity;
  dynamicFiltered += 0.25f * (dynamicG - dynamicFiltered);

  if (dynamicFiltered < -0.05f) peakArmed = true;

  if (peakArmed && dynamicFiltered > 0.11f &&
      now - lastPeakMs > 280 && totalG < 1.8f) {
    uint32_t interval = lastPeakMs == 0 ? 0 : now - lastPeakMs;
    if (interval >= 280 && interval <= 1800) {
      if (walkingSequence) {
        stepCount++;
      } else {
        // Đỉnh trước là bước đầu tiên của chuỗi.
        stepCount += 2;
        walkingSequence = true;
      }
    } else {
      walkingSequence = false;
    }
    lastPeakMs = now;
    peakArmed = false;
  }

  if (lastPeakMs != 0 && now - lastPeakMs > 2200) {
    walkingSequence = false;
  }
}

void Fall_Process(int16_t ax, int16_t ay, int16_t az) {
  static uint32_t lowGStartMs = 0;
  static float previousG = 1.0f;
  uint32_t now = millis();

  float gx = ax / 16384.0f;
  float gy = ay / 16384.0f;
  float gz = az / 16384.0f;
  accelerationG = sqrtf(gx * gx + gy * gy + gz * gz);
  float movement = fabsf(accelerationG - previousG);
  motionLevel += 0.15f * (movement - motionLevel);
  previousG = accelerationG;
  Step_Process(accelerationG, now);

  switch (fallState) {
    case FALL_NORMAL:
      if (accelerationG < 0.45f) {
        if (lowGStartMs == 0) lowGStartMs = now;
        if (now - lowGStartMs >= 120) {
          fallState = FALL_FREE_FALL;
          fallStateMs = now;
        }
      } else {
        lowGStartMs = 0;
      }
      // Va đập trên 2,8 g có thể bắt đầu chuỗi kiểm tra ngay.
      if (accelerationG > 2.8f) {
        fallState = FALL_IMPACT;
        fallStateMs = now;
      }
      break;

    case FALL_FREE_FALL:
      if (accelerationG > 2.2f) {
        fallState = FALL_IMPACT;
        fallStateMs = now;
      } else if (now - fallStateMs > 1200) {
        fallState = FALL_NORMAL;
        lowGStartMs = 0;
      }
      break;

    case FALL_IMPACT:
      // Xác nhận cảnh báo khi thiết bị ít chuyển động sau va đập.
      if (now - fallStateMs > 1800 && motionLevel < 0.08f &&
          accelerationG > 0.72f && accelerationG < 1.28f) {
        fallState = FALL_ALERT;
        fallStateMs = now;
        Serial.println("[ALERT] PHAT HIEN TE NGA - CAN KIEM TRA NGUOI DUNG");
      } else if (now - fallStateMs > 5000) {
        fallState = FALL_NORMAL;
      }
      break;

    case FALL_ALERT:
      // Giữ cảnh báo cho đến khi khởi động lại thiết bị.
      break;
  }
}

// Cảm biến quang MAX30102
bool MAX30102_Init() {
  uint8_t partID = 0;
  if (!I2C_DevicePresent(MAX30102_ADDR)) return false;
  if (!I2C_ReadRegister(MAX30102_ADDR, 0xFF, partID)) return false;
  if (partID != 0x15) return false; // PART_ID chuẩn của MAX30102

  // Reset và xóa trạng thái cũ.
  if (!I2C_WriteRegister(MAX30102_ADDR, 0x09, 0x40)) return false;
  delay(100);

  // Đưa các con trỏ FIFO về đầu bộ đệm.
  if (!I2C_WriteRegister(MAX30102_ADDR, 0x04, 0x00)) return false;
  if (!I2C_WriteRegister(MAX30102_ADDR, 0x05, 0x00)) return false;
  if (!I2C_WriteRegister(MAX30102_ADDR, 0x06, 0x00)) return false;

  // Trung bình bốn mẫu ngay trong MAX30102 để giảm nhiễu quang/điện. ADC chạy
  // 400 Hz nên sau khi decimate, FIFO vẫn trả đúng 100 mẫu/giây cho thuật toán.
  if (!I2C_WriteRegister(MAX30102_ADDR, 0x08, 0x5F)) return false;
  // ADC 4096 nA, 400 mẫu/giây, độ rộng xung 411 us.
  if (!I2C_WriteRegister(MAX30102_ADDR, 0x0A, 0x2F)) return false;
  // 0x32 tương ứng khoảng 10 mA, là điểm khởi đầu hãng khuyến nghị. Mức DC đo
  // thực tế khoảng 62% toàn thang, vẫn nằm trong vùng mục tiêu 1/4--3/4 ADC.
  if (!I2C_WriteRegister(MAX30102_ADDR, 0x0C, 0x32)) return false;
  if (!I2C_WriteRegister(MAX30102_ADDR, 0x0D, 0x32)) return false;
  // Chế độ SpO2 trả về lần lượt mẫu RED và IR.
  if (!I2C_WriteRegister(MAX30102_ADDR, 0x09, 0x03)) return false;
  return true;
}

void PPG_ResetMetrics() {
  ppg.fingerPresent = false;
  ppg.heartRateValid = false;
  ppg.heartRateProvisional = false;
  ppg.spo2Valid = false;
  ppg.bpm = 0.0f;
  ppg.spo2 = 0.0f;
  ppg.signalQuality = 0.0f;
  ppg.lastBeatMs = 0;
  ppgFilter = {};
}

void PPG_ProcessSample(uint32_t redValue, uint32_t irValue, uint32_t sampleTimeMs) {
  PPGFilterState &state = ppgFilter;
  // Khi vừa chạm, IR có thể tăng từ ngưỡng nhận tay lên hơn 100.000 chỉ trong
  // vài chục mẫu. Bám nền nhanh đủ 350 ms để xung đặt tay không làm phồng
  // đường bao và che mất các nhịp thật ngay sau đó.
  constexpr uint32_t PPG_SETTLE_MS = 350;
  // Hụt dưới 180 ms thường chỉ do ngón tay rung. Nếu tín hiệu mất lâu hơn rồi
  // quay lại, coi đó là một lần đặt tay mới để không dùng trạng thái nhịp cũ.
  constexpr uint32_t FINGER_GAP_NEW_SESSION_MS = 180;
  constexpr uint32_t FINGER_RELEASE_CONFIRM_MS = 450;
  constexpr uint32_t BEAT_ACQUIRE_RETRY_MS = 3500;
  constexpr uint32_t MIN_RR_MS = 333;
  constexpr uint32_t MAX_RR_MS = 1500;
  constexpr uint32_t MIN_HALF_RR_MS = 167;
  constexpr uint32_t MAX_HALF_RR_MS = 750;
  constexpr float MIN_PROVISIONAL_BPM = 45.0f;
  constexpr float MAX_PROVISIONAL_BPM = 140.0f;

  ppg.redRaw = redValue;
  ppg.irRaw = irValue;

  // Hai ngưỡng khác nhau khi nhận và nhả tay giúp trạng thái không dao động.
  bool hasFinger = ppg.fingerPresent ? (irValue > 7000) : (irValue > 10000);
  if (!hasFinger) {
    if (!ppg.fingerPresent) {
      state.dcRed = redValue;
      state.dcIr = irValue;
      return;
    }

    // Không xóa số đo chỉ vì một vài mẫu bị hụt do ngón tay dịch chuyển.
    if (state.fingerReleaseStartMs == 0) {
      state.fingerReleaseStartMs = sampleTimeMs;
    }
    if (sampleTimeMs - state.fingerReleaseStartMs >= FINGER_RELEASE_CONFIRM_MS) {
      PPG_ResetMetrics();
      ppgFilter.dcRed = redValue;
      ppgFilter.dcIr = irValue;
    }
    return;
  }
  if (ppg.fingerPresent && state.fingerReleaseStartMs != 0 &&
      sampleTimeMs - state.fingerReleaseStartMs >= FINGER_GAP_NEW_SESSION_MS) {
    // Ngón tay đã rời đủ lâu rồi xuất hiện lại trước thời hạn xác nhận nhả.
    // Bắt đầu phiên mới và xóa cực tính/cạnh/RR cũ; nếu không, SpO2 có thể
    // tính lại nhưng BPM sẽ chờ trên một trạng thái không còn phù hợp.
    PPG_ResetMetrics();
    ppg.redRaw = redValue;
    ppg.irRaw = irValue;
  }
  state.fingerReleaseStartMs = 0;

  if (!ppg.fingerPresent) {
    ppg.fingerPresent = true;
    state.fingerStartMs = sampleTimeMs;
    state.dcRed = redValue;
    state.dcIr = irValue;
  }

  uint32_t now = sampleTimeMs;
  uint32_t fingerAgeMs = now - state.fingerStartMs;

  // Tách DC/AC bằng bộ lọc IIR tại tốc độ mẫu 100 Hz.
  float dcAlpha = fingerAgeMs < PPG_SETTLE_MS ? 0.20f : 0.01f;
  state.dcRed += dcAlpha * ((float)redValue - state.dcRed);
  state.dcIr += dcAlpha * ((float)irValue - state.dcIr);
  float redAC = (float)redValue - state.dcRed;
  float irAC = (float)irValue - state.dcIr;
  state.filtered += 0.25f * (irAC - state.filtered);

  // Không đưa quá trình đặt tay vào ngưỡng nhịp và cửa sổ SpO2.
  if (fingerAgeMs < PPG_SETTLE_MS) {
    state.filtered = 0.0f;
    state.previousFiltered = 0.0f;
    state.previousSlope = 0.0f;
    state.envelope = 0.0f;
    state.redSquareSum = state.irSquareSum = 0.0;
    state.windowSamples = 0;
    // Chờ tín hiệu đi qua vùng giữa để tránh tính nửa chu kỳ lúc vừa hết khởi tạo.
    state.beatArmed = false;
    return;
  }

  state.envelope += 0.025f * (fabsf(state.filtered) - state.envelope);

  state.redSquareSum += (double)redAC * redAC;
  state.irSquareSum += (double)irAC * irAC;
  state.windowSamples++;

  // Theo dõi đồng thời hai cực tính. Chỉ khóa cực tính sau khi một phía tạo
  // được khoảng RR hợp lệ, tránh trường hợp nhiễu đầu phiên làm khóa nhầm.
  float slope = state.filtered - state.previousFiltered;
  // Ngưỡng thích nghi thấp giúp nhận mạch yếu nhưng vẫn cao hơn nhiễu nền.
  float peakThreshold = state.envelope * 0.55f;
  if (peakThreshold < 22.0f) peakThreshold = 22.0f;

  bool crossedPositive = state.previousFiltered <= peakThreshold &&
                         state.filtered > peakThreshold;
  bool crossedNegative = state.previousFiltered >= -peakThreshold &&
                         state.filtered < -peakThreshold;
  if (state.pulsePolarity == 0 && fabsf(state.filtered) < peakThreshold * 0.35f) {
    state.beatArmed = true;
  } else if (state.pulsePolarity > 0 &&
             state.filtered < peakThreshold * 0.10f) {
    state.beatArmed = true;
  } else if (state.pulsePolarity < 0 &&
             state.filtered > -peakThreshold * 0.10f) {
    state.beatArmed = true;
  }

  // Khi chưa biết cực tính, lưu mốc của cả cạnh dương và cạnh âm. Cạnh nào
  // hoàn thành một chu kỳ hợp lệ trước sẽ được dùng cho toàn bộ phiên đo.
  if (state.pulsePolarity == 0) {
    if (crossedPositive) {
      if (state.positiveEdgeMs != 0) {
        uint32_t interval = now - state.positiveEdgeMs;
        if (interval >= MIN_RR_MS && interval <= MAX_RR_MS) {
          state.pulsePolarity = 1;
          ppg.lastBeatMs = now;
          ppg.bpm = 60000.0f / interval;
          ppg.heartRateValid = true;
          ppg.heartRateProvisional = false;
          state.replaceBpmOnNextInterval = false;
          state.beatArmed = false;
        }
      }
      state.positiveEdgeMs = now;
    }
    if (state.pulsePolarity == 0 && crossedNegative) {
      if (state.negativeEdgeMs != 0) {
        uint32_t interval = now - state.negativeEdgeMs;
        if (interval >= MIN_RR_MS && interval <= MAX_RR_MS) {
          state.pulsePolarity = -1;
          ppg.lastBeatMs = now;
          ppg.bpm = 60000.0f / interval;
          ppg.heartRateValid = true;
          ppg.heartRateProvisional = false;
          state.replaceBpmOnNextInterval = false;
          state.beatArmed = false;
        }
      }
      state.negativeEdgeMs = now;
    }

    // Hai cạnh trái dấu cách nhau gần nửa chu kỳ cho một BPM sơ bộ. Giá trị
    // này chỉ dùng trong phiên hiện tại và sẽ được thay bằng RR đầy đủ ngay
    // khi cùng một cực tính xuất hiện lần thứ hai.
    if (!ppg.heartRateValid && state.positiveEdgeMs != 0 &&
        state.negativeEdgeMs != 0) {
      uint32_t halfInterval = state.positiveEdgeMs > state.negativeEdgeMs ?
                                state.positiveEdgeMs - state.negativeEdgeMs :
                                state.negativeEdgeMs - state.positiveEdgeMs;
      if (halfInterval >= MIN_HALF_RR_MS && halfInterval <= MAX_HALF_RR_MS) {
        float provisionalBPM = 30000.0f / halfInterval;
        // Cạnh giả lúc ngón tay vừa dịch chuyển thường tạo số sơ bộ rất cao.
        // Chỉ công bố vùng sinh lý thực tế của nguyên mẫu; RR đầy đủ sau đó
        // vẫn giữ miền 40--180 BPM để không bỏ sót nhịp đã xác nhận.
        if (provisionalBPM >= MIN_PROVISIONAL_BPM &&
            provisionalBPM <= MAX_PROVISIONAL_BPM) {
          ppg.bpm = provisionalBPM;
          ppg.heartRateValid = true;
          ppg.heartRateProvisional = true;
          state.provisionalBpmMs = now;
        }
      }
    }
  }

  bool crossedSelectedEdge = state.pulsePolarity > 0 ? crossedPositive :
                              state.pulsePolarity < 0 ? crossedNegative : false;
  if (state.beatArmed && crossedSelectedEdge &&
      (ppg.lastBeatMs == 0 || now - ppg.lastBeatMs > 280)) {
    state.beatArmed = false;
    if (ppg.lastBeatMs == 0) {
      ppg.lastBeatMs = now;
    } else {
      uint32_t interval = now - ppg.lastBeatMs;
      if (interval >= MIN_RR_MS && interval <= MAX_RR_MS) {
        float instantBPM = 60000.0f / interval;
        if (ppg.heartRateProvisional || !ppg.heartRateValid ||
            state.replaceBpmOnNextInterval) {
          // Khoảng RR hợp lệ đầu tiên được hiển thị ngay.
          ppg.bpm = instantBPM;
          ppg.heartRateValid = true;
          ppg.heartRateProvisional = false;
          state.replaceBpmOnNextInterval = false;
        } else if (fabsf(instantBPM - ppg.bpm) <= 18.0f) {
          // Làm mượt mạnh hơn để OLED không đổi nhiều đơn vị chỉ vì một
          // khoảng RR ngắn hoặc dài hơn do ngón tay rung nhẹ.
          ppg.bpm = 0.90f * ppg.bpm + 0.10f * instantBPM;
        }
        ppg.lastBeatMs = now;
      } else if (interval > MAX_RR_MS) {
        // Khoảng quá dài bắt đầu một cặp nhịp mới; xung quá gần bị bỏ qua.
        ppg.lastBeatMs = now;
      }
    }
  }
  state.previousSlope = slope;
  state.previousFiltered = state.filtered;

  if (ppg.heartRateProvisional && state.provisionalBpmMs != 0 &&
      now - state.provisionalBpmMs > 1800) {
    // Không giữ ước lượng nửa chu kỳ nếu không có RR đầy đủ xác nhận tiếp theo.
    // Xóa luôn các mốc cạnh cũ để bộ dò bắt đầu một lần tìm mới thay vì mắc
    // kẹt với những mốc đã sinh ra lúc ngón tay còn đang dịch chuyển.
    ppg.bpm = 0.0f;
    ppg.heartRateValid = false;
    ppg.heartRateProvisional = false;
    ppg.lastBeatMs = 0;
    state.pulsePolarity = 0;
    state.positiveEdgeMs = 0;
    state.negativeEdgeMs = 0;
    state.beatArmed = false;
    state.provisionalBpmMs = 0;
  }

  if (ppg.lastBeatMs != 0 && now - ppg.lastBeatMs > 3000) {
    // Giữ BPM gần nhất trên màn hình và bắt đầu lại việc tìm một cặp nhịp mới.
    ppg.lastBeatMs = 0;
    state.replaceBpmOnNextInterval = true;
  }

  if (!ppg.heartRateValid && fingerAgeMs >= BEAT_ACQUIRE_RETRY_MS) {
    // Nếu không tìm được BPM, tự làm mới riêng bộ dò thay vì phụ thuộc việc
    // mở Serial Monitor (thao tác đó chỉ vô tình reset toàn bộ bo qua USB).
    // Giữ trạng thái có tay nhưng xóa nền/cạnh/cực tính đã khóa sai.
    state.dcRed = redValue;
    state.dcIr = irValue;
    state.filtered = 0.0f;
    state.previousFiltered = 0.0f;
    state.previousSlope = 0.0f;
    state.envelope = 0.0f;
    state.beatArmed = false;
    state.replaceBpmOnNextInterval = false;
    state.pulsePolarity = 0;
    state.positiveEdgeMs = 0;
    state.negativeEdgeMs = 0;
    state.provisionalBpmMs = 0;
    state.redSquareSum = 0.0;
    state.irSquareSum = 0.0;
    state.windowSamples = 0;
    state.fingerStartMs = now;
  }

  // Cửa sổ 64 mẫu tạo kết quả đầu tiên sau khoảng 0,76 giây kể từ lúc chạm.
  // SpO2 được tính độc lập với BPM vì hai đại lượng dùng điều kiện khác nhau.
  if (state.windowSamples >= 64) {
    float redRms = sqrtf((float)(state.redSquareSum / state.windowSamples));
    float irRms = sqrtf((float)(state.irSquareSum / state.windowSamples));
    float perfusion = (state.dcIr > 1.0f) ?
                        (100.0f * irRms / state.dcIr) : 0.0f;
    ppg.signalQuality = constrain(perfusion * 35.0f, 0.0f, 99.0f);

    if (state.dcRed > 1.0f && state.dcIr > 1.0f &&
        redRms > 12.0f && irRms > 12.0f) {
      float ratio = (redRms / state.dcRed) / (irRms / state.dcIr);
      if (ratio > 0.2f && ratio < 2.0f) {
        float estimatedSpO2 = constrain(110.0f - 25.0f * ratio, 70.0f, 100.0f);
        // Chỉ cập nhật bằng cửa sổ đủ chất lượng; cửa sổ lỗi không xóa kết quả cũ.
        bool estimateValid = perfusion > 0.08f &&
                             state.fingerStartMs != 0 &&
                             now - state.fingerStartMs >= 700;
        if (estimateValid) {
          ppg.spo2 = ppg.spo2Valid ?
                       (0.8f * ppg.spo2 + 0.2f * estimatedSpO2) : estimatedSpO2;
          ppg.spo2Valid = true;
        }
      }
    }
    state.redSquareSum = state.irSquareSum = 0.0;
    state.windowSamples = 0;
  }
}

constexpr int8_t FIFO_NO_DATA = 0;
constexpr int8_t FIFO_SAMPLES_READ = 1;
constexpr int8_t FIFO_BUS_ERROR = -1;

int8_t MAX30102_ReadFIFO() {
  static uint32_t sampleClockMs = 0;
  uint8_t writePointer = 0;
  uint8_t readPointer = 0;
  if (!I2C_ReadRegister(MAX30102_ADDR, 0x04, writePointer)) {
    return FIFO_BUS_ERROR;
  }
  if (!I2C_ReadRegister(MAX30102_ADDR, 0x06, readPointer)) {
    return FIFO_BUS_ERROR;
  }
  if (writePointer == readPointer) return FIFO_NO_DATA;

  uint8_t sampleCount = (writePointer - readPointer) & 0x1F;
  if (sampleCount > 16) sampleCount = 16; // 16 mẫu = 96 byte, vừa buffer Wire.

  Wire.beginTransmission(MAX30102_ADDR);
  Wire.write(0x07); // Thanh ghi dữ liệu FIFO Data Register
  if (Wire.endTransmission(false) != 0) return FIFO_BUS_ERROR;

  size_t bytesNeeded = (size_t)sampleCount * 6;
  size_t received = Wire.requestFrom((uint8_t)MAX30102_ADDR, bytesNeeded);
  if (received != bytesNeeded || (size_t)Wire.available() < bytesNeeded) {
    return FIFO_BUS_ERROR;
  }

  uint32_t now = millis();
  if (sampleClockMs == 0 || (int32_t)(now - sampleClockMs) > 500) {
    sampleClockMs = now - (uint32_t)sampleCount * 10;
  }

  for (uint8_t sample = 0; sample < sampleCount; sample++) {
    uint32_t redValue = ((uint32_t)Wire.read() << 16);
    redValue |= ((uint32_t)Wire.read() << 8);
    redValue |= Wire.read();
    uint32_t irValue = ((uint32_t)Wire.read() << 16);
    irValue |= ((uint32_t)Wire.read() << 8);
    irValue |= Wire.read();
    sampleClockMs += 10; // MAX30102 đang cấu hình 100 mẫu/giây.
    PPG_ProcessSample(redValue & 0x03FFFF, irValue & 0x03FFFF, sampleClockMs);
  }
  return FIFO_SAMPLES_READ;
}

// Định vị GPS và vòng lặp chính
int8_t NMEA_HexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool NMEA_ChecksumOK(const char *sentence) {
  if (!sentence || sentence[0] != '$') return false;
  const char *star = strchr(sentence, '*');
  if (!star || strlen(star) < 3) return false;
  int8_t high = NMEA_HexNibble(star[1]);
  int8_t low = NMEA_HexNibble(star[2]);
  if (high < 0 || low < 0) return false;
  uint8_t checksum = 0;
  for (const char *p = sentence + 1; p < star; p++) checksum ^= (uint8_t)*p;
  uint8_t expected = (uint8_t)((high << 4) | low);
  return checksum == expected;
}

double NMEA_ToDegrees(const char *value, char hemisphere) {
  if (!value || !*value) return 0.0;
  double raw = atof(value);
  int degrees = (int)(raw / 100.0);
  double minutes = raw - degrees * 100.0;
  double result = degrees + minutes / 60.0;
  if (hemisphere == 'S' || hemisphere == 'W') result = -result;
  return result;
}

bool GPS_StorePosition(const char *latitude, char northSouth,
                       const char *longitude, char eastWest) {
  if (!latitude || !longitude || !latitude[0] || !longitude[0]) return false;
  if (northSouth != 'N' && northSouth != 'S') return false;
  if (eastWest != 'E' && eastWest != 'W') return false;

  double parsedLatitude = NMEA_ToDegrees(latitude, northSouth);
  double parsedLongitude = NMEA_ToDegrees(longitude, eastWest);
  if (fabs(parsedLatitude) > 90.0 || fabs(parsedLongitude) > 180.0) return false;

  gpsLatitude = parsedLatitude;
  gpsLongitude = parsedLongitude;
  gpsFix = true;
  gpsPositionKnown = true;
  lastGPSFixMs = millis();
  return true;
}

void GPS_ParseSentence(const char *sentence) {
  if (!NMEA_ChecksumOK(sentence)) return;

  char copy[96];
  strncpy(copy, sentence, sizeof(copy));
  copy[sizeof(copy) - 1] = '\0';
  char *star = strchr(copy, '*');
  if (star) *star = '\0';

  char *fields[16] = {nullptr};
  uint8_t count = 1;
  fields[0] = copy;
  for (char *p = copy; *p && count < 16; p++) {
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    }
  }
  if (count == 0) return;

  bool isGGA = strstr(fields[0], "GGA") != nullptr;
  bool isRMC = strstr(fields[0], "RMC") != nullptr;

  if (isGGA && count > 7) {
    uint8_t quality = (uint8_t)atoi(fields[6]);
    gpsSatellites = (uint8_t)atoi(fields[7]);
    if (quality == 0 || !GPS_StorePosition(fields[2], fields[3][0],
                                            fields[4], fields[5][0])) {
      gpsFix = false;
    }
  } else if (isRMC && count > 6) {
    if (fields[2][0] != 'A' ||
        !GPS_StorePosition(fields[3], fields[4][0], fields[5], fields[6][0])) {
      gpsFix = false;
    }
  }
}

void GPS_Poll() {
  while (gpsSerial.available() > 0) {
    char c = (char)gpsSerial.read();
    gpsByteCount++;
    lastGPSByteMs = millis();

    if (c == '$') {
      gpsWorkingLength = 0;
      gpsWorking[gpsWorkingLength++] = c;
    } else if (c == '\n') {
      if (gpsWorkingLength > 0) {
        gpsWorking[gpsWorkingLength] = '\0';
        GPS_ParseSentence(gpsWorking);
        gpsWorkingLength = 0;
      }
    } else if (c != '\r' && gpsWorkingLength > 0 &&
               gpsWorkingLength < sizeof(gpsWorking) - 1) {
      gpsWorking[gpsWorkingLength++] = c;
    }
  }
}

const char *GPS_StatusText() {
  if (gpsByteCount == 0) return "WAIT";
  if (millis() - lastGPSByteMs > 3000) return "LOST";
  if (gpsFix && millis() - lastGPSFixMs < 10000) return "FIX";
  return "NOFIX";
}

void HTTP_SendHeader(WiFiClient &client, const char *status,
                     const char *contentType, size_t contentLength) {
  // Tự tạo từng trường của phản hồi HTTP/1.1 để không phụ thuộc WebServer.
  client.print("HTTP/1.1 ");
  client.println(status);
  client.print("Content-Type: ");
  client.println(contentType);
  client.print("Content-Length: ");
  client.println(contentLength);
  client.println("Cache-Control: no-store, max-age=0");
  client.println("Connection: close");
  client.println();
}

void HTTP_SendDashboard(WiFiClient &client) {
  size_t htmlLength = strlen_P(WEB_DASHBOARD_HTML);
  HTTP_SendHeader(client, "200 OK", "text/html; charset=utf-8", htmlLength);
  // Flash của ESP32-S3 được ánh xạ bộ nhớ nên có thể truyền thẳng trang nhúng.
  client.write((const uint8_t *)WEB_DASHBOARD_HTML, htmlLength);
}

void HTTP_SendStatus(WiFiClient &client) {
  char json[1400];
  snprintf(
    json, sizeof(json),
    "{\"uptime\":%lu,\"clients\":%u,"
    "\"oledOK\":%s,\"imuOK\":%s,\"maxOK\":%s,\"buzzerOK\":%s,"
    "\"fingerPresent\":%s,\"heartRateValid\":%s,"
    "\"heartRateProvisional\":%s,\"spo2Valid\":%s,"
    "\"bpm\":%.2f,\"spo2\":%.2f,\"signalQuality\":%.2f,"
    "\"redRaw\":%lu,\"irRaw\":%lu,\"steps\":%lu,"
    "\"fall\":\"%s\",\"acceleration\":%.3f,\"motion\":%.3f,"
    "\"gpsState\":\"%s\",\"satellites\":%u,\"positionKnown\":%s,"
    "\"latitude\":%.7f,\"longitude\":%.7f,\"gpsBytes\":%lu}",
    (unsigned long)(millis() / 1000UL), WiFi.softAPgetStationNum(),
    oledOK ? "true" : "false", imuOK ? "true" : "false",
    maxOK ? "true" : "false", buzzerOK ? "true" : "false",
    ppg.fingerPresent ? "true" : "false",
    ppg.heartRateValid ? "true" : "false",
    ppg.heartRateProvisional ? "true" : "false",
    ppg.spo2Valid ? "true" : "false",
    ppg.bpm, ppg.spo2, ppg.signalQuality,
    (unsigned long)ppg.redRaw, (unsigned long)ppg.irRaw,
    (unsigned long)stepCount, Fall_StateText(), accelerationG, motionLevel,
    GPS_StatusText(), gpsSatellites,
    gpsPositionKnown ? "true" : "false", gpsLatitude, gpsLongitude,
    (unsigned long)gpsByteCount
  );
  size_t jsonLength = strlen(json);
  HTTP_SendHeader(client, "200 OK", "application/json; charset=utf-8",
                  jsonLength);
  client.write((const uint8_t *)json, jsonLength);
}

void HTTP_SendOK(WiFiClient &client) {
  static const char response[] = "{\"ok\":true}";
  HTTP_SendHeader(client, "200 OK", "application/json; charset=utf-8",
                  sizeof(response) - 1);
  client.write((const uint8_t *)response, sizeof(response) - 1);
}

void Web_ResetSteps(WiFiClient &client) {
  stepCount = 0;
  HTTP_SendOK(client);
}

void Web_ResetAlert(WiFiClient &client) {
  fallState = FALL_NORMAL;
  fallStateMs = millis();
  Buzzer_Update();
  HTTP_SendOK(client);
}

void HTTP_RouteRequest(WiFiClient &client, char *requestLine) {
  char method[8] = {0};
  char path[96] = {0};
  if (sscanf(requestLine, "%7s %95s", method, path) != 2) {
    HTTP_SendDashboard(client);
    return;
  }

  // Bỏ chuỗi truy vấn để bộ định tuyến chỉ so sánh phần đường dẫn.
  char *query = strchr(path, '?');
  if (query != nullptr) *query = '\0';

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
    HTTP_SendStatus(client);
  } else if (strcmp(method, "POST") == 0 &&
             strcmp(path, "/api/reset-steps") == 0) {
    Web_ResetSteps(client);
  } else if (strcmp(method, "POST") == 0 &&
             strcmp(path, "/api/reset-alert") == 0) {
    Web_ResetAlert(client);
  } else {
    // Các địa chỉ thăm dò captive portal và đường dẫn lạ đều nhận dashboard.
    HTTP_SendDashboard(client);
  }
}

void HTTP_Poll() {
  // Mỗi lần lặp chỉ đọc các byte đã có sẵn, không chờ người dùng gửi tiếp.
  if (!activeHttpClient || !activeHttpClient.connected()) {
    if (activeHttpClient) activeHttpClient.stop();
    activeHttpClient = httpSocket.available();
    httpRequestLength = 0;
    httpClientStartMs = millis();
  }
  if (!activeHttpClient) return;

  while (activeHttpClient.available() > 0) {
    char c = (char)activeHttpClient.read();
    if (c == '\n') {
      httpRequestLine[httpRequestLength] = '\0';
      HTTP_RouteRequest(activeHttpClient, httpRequestLine);
      activeHttpClient.stop();
      httpRequestLength = 0;
      return;
    }
    if (c != '\r' && httpRequestLength < sizeof(httpRequestLine) - 1) {
      httpRequestLine[httpRequestLength++] = c;
    }
  }

  // Ngắt kết nối gửi dở để một máy khách không chiếm cổng HTTP quá lâu.
  if (millis() - httpClientStartMs > 300) {
    activeHttpClient.stop();
    httpRequestLength = 0;
  }
}

void DNS_Poll() {
  uint8_t packet[256];
  int packetSize = dnsSocket.parsePacket();
  if (packetSize <= 0) return;
  int length = dnsSocket.read(packet, sizeof(packet));
  if (length < 17 || (packet[2] & 0x80) != 0 || packet[5] == 0) return;

  // Tìm cuối tên miền dạng nhãn trong câu hỏi DNS đầu tiên.
  uint16_t cursor = 12;
  while (cursor < (uint16_t)length && packet[cursor] != 0) {
    uint8_t labelLength = packet[cursor];
    if (labelLength > 63 || cursor + labelLength + 1 >= (uint16_t)length) return;
    cursor += labelLength + 1;
  }
  if (cursor + 5 > (uint16_t)length) return;
  uint16_t questionEnd = cursor + 5;
  uint16_t queryType = ((uint16_t)packet[cursor + 1] << 8) | packet[cursor + 2];
  bool answerIPv4 = queryType == 1;

  // Giữ nguyên mã giao dịch và câu hỏi, sau đó tự ghép bản ghi A trỏ về ESP32.
  uint8_t response[300];
  memcpy(response, packet, questionEnd);
  response[2] = 0x81;
  response[3] = 0x80;
  response[4] = 0;
  response[5] = 1;
  response[6] = 0;
  response[7] = answerIPv4 ? 1 : 0;
  response[8] = response[9] = response[10] = response[11] = 0;
  uint16_t responseLength = questionEnd;

  if (answerIPv4) {
    response[responseLength++] = 0xC0;
    response[responseLength++] = 0x0C;
    response[responseLength++] = 0x00;
    response[responseLength++] = 0x01;
    response[responseLength++] = 0x00;
    response[responseLength++] = 0x01;
    response[responseLength++] = 0x00;
    response[responseLength++] = 0x00;
    response[responseLength++] = 0x00;
    response[responseLength++] = 0x3C;
    response[responseLength++] = 0x00;
    response[responseLength++] = 0x04;
    for (uint8_t i = 0; i < 4; i++) response[responseLength++] = wifiLocalIP[i];
  }

  dnsSocket.beginPacket(dnsSocket.remoteIP(), dnsSocket.remotePort());
  dnsSocket.write(response, responseLength);
  dnsSocket.endPacket();
}

void WiFiPortal_Init() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  bool configured = WiFi.softAPConfig(wifiLocalIP, wifiGateway, wifiSubnet);
  bool started = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  wifiPortalOK = configured && started;
  if (!wifiPortalOK) {
    Serial.println("[WIFI] Khong the khoi tao Access Point");
    return;
  }

  // Nhóm tự xử lý DNS wildcard và định tuyến HTTP trên socket TCP/UDP thô.
  bool dnsStarted = dnsSocket.begin(53) == 1;
  httpSocket.begin();
  httpSocket.setNoDelay(true);
  wifiPortalOK = wifiPortalOK && dnsStarted;
  Serial.printf("[WIFI] SSID=%s PASS=%s URL=http://%s\n",
                WIFI_AP_SSID, WIFI_AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());
}

void OLED_RenderStatus() {
  if (!oledOK) return;

  char line[32];
  OLED_Clear();
  if (fallState == FALL_ALERT) {
    // Mười sáu hàng đầu của loại OLED này là vùng màu vàng.
    OLED_DrawHeart(3, 4);
    OLED_DrawText(15, 4, "CANH BAO KHAN");
    OLED_DrawText(117, 4, "!");
    OLED_DrawHLine(0, 15, 128);

    // Nội dung chính nằm trong vùng màu xanh từ hàng 16 đến 63.
    OLED_DrawTextScaled(31, 19, "TE NGA", 2);
    OLED_DrawHLine(8, 36, 112);
    snprintf(line, sizeof(line), "GPS:%s  SAT:%u", GPS_StatusText(), gpsSatellites);
    OLED_DrawText(8, 40, line);
    if (gpsPositionKnown) {
      bool showLatitude = ((millis() / 2000UL) & 1U) == 0;
      const char *label = gpsFix ? (showLatitude ? "LAT" : "LON")
                                 : (showLatitude ? "L-LAT" : "L-LON");
      snprintf(line, sizeof(line), "%s:%.5f", label,
               showLatitude ? gpsLatitude : gpsLongitude);
      OLED_DrawText(8, 48, line);
    } else {
      OLED_DrawText(8, 48, "DANG CHO VI TRI");
    }
    OLED_DrawText(10, 57, "NHAN RESET DE HUY");
    if (!OLED_Update()) oledOK = false;
    return;
  }

  // Thanh trạng thái nằm trong dải vàng, số đo nằm trong dải xanh.
  OLED_DrawHeart(2, 4);
  OLED_DrawText(14, 4, "SMART HEALTH");
  OLED_DrawText(105, 4, gpsFix ? "GPS" : "---");
  OLED_DrawHLine(0, 15, 128);

  OLED_DrawRect(1, 17, 61, 23);
  OLED_DrawRect(66, 17, 61, 23);
  OLED_DrawText(19, 19, "BPM");
  OLED_DrawText(79, 19, "SPO2");
  if (ppg.fingerPresent && ppg.heartRateValid) {
    snprintf(line, sizeof(line), "%3d", (int)(ppg.bpm + 0.5f));
  } else {
    strcpy(line, ppg.fingerPresent ? "..." : " --");
  }
  OLED_DrawTextScaled(11, 26, line, 2);

  if (ppg.fingerPresent && ppg.spo2Valid) {
    snprintf(line, sizeof(line), "%3d", (int)(ppg.spo2 + 0.5f));
  } else {
    strcpy(line, ppg.fingerPresent ? "..." : " --");
  }
  OLED_DrawTextScaled(72, 26, line, 2);
  OLED_DrawText(111, 31, "%");

  if (!ppg.fingerPresent) {
    OLED_DrawText(4, 42, "DAT NGON TAY LEN CAM");
  } else if (!ppg.heartRateValid) {
    OLED_DrawText(4, 42, ppg.lastBeatMs == 0 ? "DANG BAT MACH..." :
                                               "DANG TINH BPM...");
  } else if (ppg.heartRateProvisional) {
    OLED_DrawText(4, 42, "BPM SO BO - DANG XAC NHAN");
  } else if (!ppg.spo2Valid) {
    OLED_DrawText(4, 42, "BPM OK - DANG DO SPO2");
  } else {
    OLED_DrawText(4, 42, "DANG THEO DOI...");
  }

  snprintf(line, sizeof(line), "BUOC:%lu FALL:%s",
           (unsigned long)stepCount, Fall_StateText());
  OLED_DrawText(4, 50, line);

  if (gpsFix) {
    bool showLatitude = ((millis() / 2000UL) & 1U) == 0;
    snprintf(line, sizeof(line), "%s:%.5f", showLatitude ? "LAT" : "LON",
             showLatitude ? gpsLatitude : gpsLongitude);
  } else {
    snprintf(line, sizeof(line), "GPS:%s SAT:%u A:%.1fG",
             GPS_StatusText(), gpsSatellites, accelerationG);
  }
  OLED_DrawText(4, 57, line);
  if (!OLED_Update()) oledOK = false;
}

void I2C_PrintScan() {
  Serial.println("[I2C] Dang quet dia chi...");
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; address++) {
    if (I2C_DevicePresent(address)) {
      Serial.printf("[I2C] Tim thay thiet bi tai 0x%02X\n", address);
      found++;
    }
  }
  if (found == 0) Serial.println("[I2C] KHONG tim thay thiet bi nao");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== HE THONG THEO DOI SUC KHOE ESP32-S3 ===");

  WiFiPortal_Init();

  // Bus I2C dùng chung cho OLED, MAX30102 và IMU.
  Wire.begin(I2C_SDA, I2C_SCL);
  // 100 kHz phù hợp với dây nối thử nghiệm.
  Wire.setClock(I2C_SENSOR_HZ);
  I2C_PrintScan();

  // Kiểm tra từng địa chỉ trước khi khởi tạo.
  oledOK = I2C_DevicePresent(OLED_ADDR) && OLED_Init();
  if (oledOK) {
    OLED_Clear();
    OLED_DrawText(0, 0, "DANG KHOI DONG...");
    if (!OLED_Update()) oledOK = false;
  }
  imuOK = IMU_Init();
  maxOK = MAX30102_Init();

  // GPS truyền NMEA qua UART1 ở 9600 baud.
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  Buzzer_Init();

  Serial.printf("[STATUS] OLED=%s IMU=%s MAX30102=%s BUZZER=%s "
                "GPS=UART1@9600 WIFI=%s\n",
                oledOK ? "OK" : "LOI", imuOK ? "OK" : "LOI",
                maxOK ? "OK" : "LOI", buzzerOK ? "OK" : "LOI",
                wifiPortalOK ? "AP@192.168.4.1" : "LOI");
  OLED_RenderStatus();
}

void loop() {
  static int16_t ax = 0, ay = 0, az = 0;
  static uint32_t lastIMUMs = 0;
  static uint32_t lastDisplayMs = 0;
  static uint32_t lastPrintMs = 0;
  static uint32_t lastProbeMs = 0;

  if (wifiPortalOK) {
    DNS_Poll();
    HTTP_Poll();
  }
  GPS_Poll();
  Buzzer_Update();

  // Đọc tối đa ba nhóm FIFO trong một vòng lặp.
  if (maxOK) {
    for (uint8_t batch = 0; batch < 3; batch++) {
      int8_t result = MAX30102_ReadFIFO();
      if (result == FIFO_NO_DATA) break;
      if (result == FIFO_BUS_ERROR) {
        maxOK = false;
        PPG_ResetMetrics();
        ppg.redRaw = ppg.irRaw = 0;
        Serial.println("[MAX30102] Mat ket noi I2C");
        break;
      }
    }
  }

  // IMU được cập nhật ở 50 Hz.
  if (millis() - lastIMUMs >= IMU_PERIOD_MS) {
    lastIMUMs = millis();
    if (imuOK) {
      if (IMU_ReadAccel(ax, ay, az)) Fall_Process(ax, ay, az);
      else {
        imuOK = false;
        Serial.println("[IMU] Mat ket noi I2C");
      }
    }
  }

  if (gpsFix && millis() - lastGPSFixMs > 15000) gpsFix = false;

  // Thử kết nối lại thiết bị I2C sau mỗi hai giây.
  if (millis() - lastProbeMs >= REPROBE_PERIOD_MS) {
    lastProbeMs = millis();
    if (!oledOK && I2C_DevicePresent(OLED_ADDR)) {
      oledOK = OLED_Init();
      if (oledOK) Serial.println("[OLED] Da ket noi lai");
    }
    if (!imuOK && IMU_Init()) {
      imuOK = true;
      Serial.println("[IMU] Da ket noi lai");
    }
    if (!maxOK && MAX30102_Init()) {
      PPG_ResetMetrics();
      maxOK = true;
      Serial.println("[MAX30102] Da ket noi lai");
    }
  }

  // OLED được làm mới ở 10 Hz.
  if (millis() - lastDisplayMs >= DISPLAY_PERIOD_MS) {
    lastDisplayMs = millis();
    OLED_RenderStatus();
  }

  // USB CDC chỉ phục vụ chẩn đoán. Không ghi khi máy tính đã đóng Serial
  // Monitor để bộ đệm USB không thể ảnh hưởng nhịp đọc FIFO của MAX30102.
  if (Serial && millis() - lastPrintMs >= STATUS_PERIOD_MS) {
    lastPrintMs = millis();
    Serial.printf(
                  "OLED:%s IMU:%s MAX:%s | FINGER:%s BPM:%.1f(%s) "
                  "SPO2:%.1f(%s) Q:%.1f IR:%lu | STEP:%lu FALL:%s "
                  "A:%.2fg | GPS:%s SAT:%u LAT:%.6f LON:%.6f BYTE:%lu\n",
                  oledOK ? "OK" : "ERR", imuOK ? "OK" : "ERR",
                  maxOK ? "OK" : "ERR", ppg.fingerPresent ? "YES" : "NO",
                  ppg.bpm, ppg.heartRateValid ?
                             (ppg.heartRateProvisional ? "FAST" : "OK") :
                             "WAIT",
                  ppg.spo2, ppg.spo2Valid ? "OK" : "WAIT",
                  ppg.signalQuality, (unsigned long)ppg.irRaw,
                  (unsigned long)stepCount, Fall_StateText(), accelerationG,
                  GPS_StatusText(), gpsSatellites,
                  gpsLatitude, gpsLongitude, (unsigned long)gpsByteCount);
  }

  delay(5);
}
