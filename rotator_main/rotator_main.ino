/*
 * Arduino Rotátor - verze 2.0 (UNO + NANO)
 * UNO (shack): TFT, relé, piezo, limity a FAIL-SAFE
 * NANO (u rotátoru): měření potenciometru a přenos po 1-wire UART (open-collector)
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <avr/wdt.h>
#include <avr/io.h>
#include "version.h"

// Definice pinů
#define TFT_CS    10
#define TFT_RST   9
#define TFT_DC    8
#define RELAY_LEFT_PIN  2
#define RELAY_RIGHT_PIN 3
#define BUZZER_PIN 7

#define RX_PIN 4
#define TX_PIN 5

#define DEBUG_SERIAL 1

constexpr long LINK_BAUD = 9600;
constexpr uint16_t PACKET_TIMEOUT_MS = 1000;
constexpr uint8_t RX_LINE_MAX = 32;
constexpr uint8_t LINK_STREAK_REQUIRED = 3;
constexpr uint16_t STARTUP_ANIM_MS = 1500;

// Konstanty pro potenciometr
constexpr int POT_MAX_VALUE = 1023;
constexpr int POT_MAX_TURNS = 10;
constexpr int POT_DEGREES_PER_TURN = 360;

// Konstanty pro převod
// Korekce dle měření: 360° otočení antény odpovídá změně ADC 929 -> 297.
// GEAR_RATIO = ((929-297)/1023 * 10 otáček) / 1 otáčka antény = 6.1779
constexpr float GEAR_RATIO = 6.1779081f;

// Limity v procentech s hysterezí
constexpr float LOW_LIMIT_ON_PERCENT = 9.0f;
constexpr float LOW_LIMIT_OFF_PERCENT = 12.0f;
constexpr float HIGH_LIMIT_ON_PERCENT = 91.0f;
constexpr float HIGH_LIMIT_OFF_PERCENT = 88.0f;

// Vyhlazení přijatého ADC z NANO
constexpr float EMA_ALPHA = 0.20f;

// Časování
constexpr uint16_t LOOP_PERIOD_MS = 50;
constexpr uint16_t LIMIT_BUZZER_DEBOUNCE_MS = 200;
constexpr uint16_t LIMIT_BUZZER_REPEAT_MS = 1500;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
SoftwareSerial linkSerial(RX_PIN, TX_PIN);

float currentRotatorAngle = 0.0f;
float currentPotRatio = 0.0f;
float currentPotPercent = 0.0f;

int rawAdcValue = 0;
float filteredAdcValue = 0.0f;
bool emaInitialized = false;

bool lowLimitActive = false;
bool highLimitActive = false;
bool errorState = true;
bool safetyLimitReached = false;

unsigned long lastValidPacketMs = 0;
unsigned long limitStateSinceMs = 0;
unsigned long lastLimitBeepMs = 0;
unsigned long lastLoopMs = 0;
uint8_t validPacketStreak = 0;
bool linkReady = false;
unsigned long bootStartMs = 0;
bool inBoot = true;

uint32_t packetOkCount = 0;
uint32_t packetCrcErrorCount = 0;
uint32_t packetFormatErrorCount = 0;

char rxLine[RX_LINE_MAX];
uint8_t rxPos = 0;

float lastDrawnAngle = -1.0f;
int lastX1 = 0, lastY1 = 0, lastX2 = 0, lastY2 = 0, lastX3 = 0, lastY3 = 0;
String lastAngleStr = "";
String lastPercentStr = "";
String lastFilterInfoStr = "";
String lastStatusStr = "";

uint8_t computeCrc(const char *payload);
bool parsePacketLine(const char *line, int &adcOut);
void processIncomingPackets();
void updatePositionFromAdc(float adcFiltered);
void updateSafetyState();
void applyRelayInterlock(bool blockLeft, bool blockRight);
void updateBuzzer();
void drawCompass();
void updateDisplay();
void renderBootFrame(unsigned long nowMs);

#if DEBUG_SERIAL
#define DBG_PRINTLN(x) Serial.println(x)
#define DBG_PRINT(x) Serial.print(x)
#define DBG_PRINT2(x, y) Serial.print(x, y)
#else
#define DBG_PRINTLN(x) do {} while (0)
#define DBG_PRINT(x) do {} while (0)
#define DBG_PRINT2(x, y) do {} while (0)
#endif

void setup() {
  // Guard against watchdog reset loops after brownouts
  wdt_disable();

  uint8_t mcusrFlags = MCUSR;
  MCUSR = 0;

  Serial.begin(9600);
  pinMode(RX_PIN, INPUT_PULLUP);  // Open-collector DATA musí mít klidovou úroveň HIGH.
  linkSerial.begin(LINK_BAUD);

  pinMode(RELAY_LEFT_PIN, OUTPUT);
  pinMode(RELAY_RIGHT_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  applyRelayInterlock(true, true);

  DBG_PRINTLN("Rotator UNO v2 - init");
  if (mcusrFlags & _BV(WDRF)) {
    DBG_PRINTLN("Reset reason: WDT");
  }
  if (mcusrFlags & _BV(BORF)) {
    DBG_PRINTLN("Reset reason: Brown-out");
  }
  if (mcusrFlags & _BV(EXTRF)) {
    DBG_PRINTLN("Reset reason: External");
  }
  if (mcusrFlags & _BV(PORF)) {
    DBG_PRINTLN("Reset reason: Power-on");
  }

  tft.init(240, 320);
  tft.setRotation(3);
  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_BLACK);

  bootStartMs = millis();
  inBoot = true;
  renderBootFrame(bootStartMs);
  drawCompass();

  lastAngleStr = "INIT";
  lastPercentStr = "INIT";
  lastFilterInfoStr = "INIT";
  lastStatusStr = "INIT";
  updateDisplay();

  DBG_PRINTLN("UNO waiting for data...");

  // Enable watchdog to recover from hangs due to brownouts/spikes
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  processIncomingPackets();

  unsigned long now = millis();
  if (inBoot) {
    renderBootFrame(now);
    if ((now - bootStartMs) < STARTUP_ANIM_MS) {
      return;
    }
    inBoot = false;
    drawCompass();
    lastAngleStr = "INIT";
    lastPercentStr = "INIT";
    lastFilterInfoStr = "INIT";
    lastStatusStr = "INIT";
  }
  if (now - lastLoopMs < LOOP_PERIOD_MS) {
    return;
  }
  lastLoopMs = now;

  if (!linkReady || lastValidPacketMs == 0 || ((now - lastValidPacketMs) > PACKET_TIMEOUT_MS)) {
    linkReady = false;
    validPacketStreak = 0;
    errorState = true;
  } else {
    errorState = false;
  }

  if (!errorState) {
    updatePositionFromAdc(filteredAdcValue);
  }

  updateSafetyState();
  updateBuzzer();
  updateDisplay();

  DBG_PRINT("ADC RAW/FIL: ");
  DBG_PRINT(rawAdcValue);
  DBG_PRINT("/");
  DBG_PRINT2(filteredAdcValue, 1);
  DBG_PRINT(" | %: ");
  DBG_PRINT2(currentPotPercent, 2);
  DBG_PRINT(" | AZ: ");
  DBG_PRINT2(currentRotatorAngle, 1);
  DBG_PRINT(" | PKT ok/crc/fmt: ");
  DBG_PRINT(packetOkCount);
  DBG_PRINT("/");
  DBG_PRINT(packetCrcErrorCount);
  DBG_PRINT("/");
  DBG_PRINT(packetFormatErrorCount);
  DBG_PRINT(" | E: ");
  DBG_PRINTLN(errorState);
}

void processIncomingPackets() {
  while (linkSerial.available() > 0) {
    char c = static_cast<char>(linkSerial.read());

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      rxLine[rxPos] = '\0';
      if (rxPos > 0) {
        int adc = 0;
        if (parsePacketLine(rxLine, adc)) {
          rawAdcValue = adc;
          if (!emaInitialized || !isfinite(filteredAdcValue)) {
            filteredAdcValue = static_cast<float>(adc);
            emaInitialized = true;
          } else {
            filteredAdcValue = filteredAdcValue * (1.0f - EMA_ALPHA) + adc * EMA_ALPHA;
          }

          if (validPacketStreak < 255) {
            validPacketStreak++;
          }
          if (validPacketStreak >= LINK_STREAK_REQUIRED) {
            linkReady = true;
            lastValidPacketMs = millis();
          }
          packetOkCount++;
        } else {
          validPacketStreak = 0;
        }
      }
      rxPos = 0;
      continue;
    }

    if (rxPos < (RX_LINE_MAX - 1)) {
      rxLine[rxPos++] = c;
    } else {
      // Přetečení řádku -> zahoď paket
      rxPos = 0;
      validPacketStreak = 0;
      packetFormatErrorCount++;
    }
  }
}

bool parsePacketLine(const char *line, int &adcOut) {
  // Očekáváme: P,<adc>,<crc>
  const char *firstComma = strchr(line, ',');
  if (!firstComma || line[0] != 'P') {
    packetFormatErrorCount++;
    return false;
  }

  const char *secondComma = strchr(firstComma + 1, ',');
  if (!secondComma) {
    packetFormatErrorCount++;
    return false;
  }

  if (strchr(secondComma + 1, ',') != nullptr) {
    packetFormatErrorCount++;
    return false;
  }

  char adcBuf[8];
  size_t adcLen = static_cast<size_t>(secondComma - (firstComma + 1));
  if (adcLen == 0 || adcLen >= sizeof(adcBuf)) {
    packetFormatErrorCount++;
    return false;
  }
  memcpy(adcBuf, firstComma + 1, adcLen);
  adcBuf[adcLen] = '\0';

  int adc = atoi(adcBuf);
  if (adc < 0 || adc > POT_MAX_VALUE) {
    packetFormatErrorCount++;
    return false;
  }

  int rxCrc = atoi(secondComma + 1);
  if (rxCrc < 0 || rxCrc > 255) {
    packetFormatErrorCount++;
    return false;
  }

  char payload[16];
  snprintf(payload, sizeof(payload), "P,%d", adc);
  uint8_t crc = computeCrc(payload);
  if (crc != static_cast<uint8_t>(rxCrc)) {
    packetCrcErrorCount++;
    return false;
  }

  adcOut = adc;
  return true;
}

uint8_t computeCrc(const char *payload) {
  uint8_t crc = 0;
  while (*payload) {
    crc ^= static_cast<uint8_t>(*payload++);
  }
  return crc;
}

void updatePositionFromAdc(float adcFiltered) {
  currentPotRatio = adcFiltered / static_cast<float>(POT_MAX_VALUE);
  if (currentPotRatio < 0.0f) currentPotRatio = 0.0f;
  if (currentPotRatio > 1.0f) currentPotRatio = 1.0f;

  currentPotPercent = currentPotRatio * 100.0f;

  float potAngle = currentPotRatio * POT_MAX_TURNS * POT_DEGREES_PER_TURN;
  float potAngleFromCenter = potAngle - 1800.0f;
  currentRotatorAngle = (potAngleFromCenter / GEAR_RATIO) + 180.0f;

  while (currentRotatorAngle < 0.0f) currentRotatorAngle += 360.0f;
  while (currentRotatorAngle >= 360.0f) currentRotatorAngle -= 360.0f;
}

void updateSafetyState() {
  bool previousAnyLimit = safetyLimitReached || errorState;

  if (!errorState) {
    if (!lowLimitActive && currentPotPercent <= LOW_LIMIT_ON_PERCENT) {
      lowLimitActive = true;
    } else if (lowLimitActive && currentPotPercent >= LOW_LIMIT_OFF_PERCENT) {
      lowLimitActive = false;
    }

    if (!highLimitActive && currentPotPercent >= HIGH_LIMIT_ON_PERCENT) {
      highLimitActive = true;
    } else if (highLimitActive && currentPotPercent <= HIGH_LIMIT_OFF_PERCENT) {
      highLimitActive = false;
    }
  } else {
    lowLimitActive = true;
    highLimitActive = true;
  }

  safetyLimitReached = lowLimitActive || highLimitActive;
  applyRelayInterlock(lowLimitActive, highLimitActive);

  bool currentAnyLimit = safetyLimitReached || errorState;
  if (currentAnyLimit && !previousAnyLimit) {
    limitStateSinceMs = millis();
  }
}

void applyRelayInterlock(bool blockLeft, bool blockRight) {
  digitalWrite(RELAY_LEFT_PIN, blockLeft ? LOW : HIGH);
  digitalWrite(RELAY_RIGHT_PIN, blockRight ? LOW : HIGH);
}

void updateBuzzer() {
  unsigned long now = millis();

  if (errorState) {
    if (now - lastLimitBeepMs > 2000) {
      tone(BUZZER_PIN, 1500, 120);
      delay(140);
      tone(BUZZER_PIN, 1100, 180);
      lastLimitBeepMs = now;
    }
    return;
  }

  if (safetyLimitReached) {
    if ((now - limitStateSinceMs) >= LIMIT_BUZZER_DEBOUNCE_MS &&
        (now - lastLimitBeepMs) >= LIMIT_BUZZER_REPEAT_MS) {
      tone(BUZZER_PIN, 1000, 120);
      lastLimitBeepMs = now;
    }
  }
}

void drawCompass() {
  tft.fillScreen(ST77XX_BLACK);

  int centerX = tft.width() / 2;
  int centerY = tft.height() / 2;
  int radius = min(centerX, centerY) - 27;

  tft.drawCircle(centerX, centerY, radius, ST77XX_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(centerX - 8, centerY - radius + 10);
  tft.print("N");

  tft.setCursor(centerX + radius - 25, centerY - 8);
  tft.print("E");

  tft.setCursor(centerX - 8, centerY + radius - 25);
  tft.print("S");

  tft.setCursor(centerX - radius + 10, centerY - 8);
  tft.print("W");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.fillRect(2, 2, 150, 12, ST77XX_BLACK);
  tft.setCursor(4, 4);
  tft.print(FW_VERSION_STR);
}

void renderBootFrame(unsigned long nowMs) {
  const int w = tft.width();
  const int h = tft.height();
  const int barX = 20;
  const int barY = h - 34;
  const int barW = w - 40;
  const int barH = 12;
  unsigned long elapsed = nowMs - bootStartMs;
  int progress = (static_cast<unsigned long>(barW) * elapsed) / STARTUP_ANIM_MS;

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setCursor(24, 26);
  tft.print("ROTATOR BOOT");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(24, 52);
  tft.print(FW_VERSION_STR);
  tft.setCursor(24, 64);
  tft.print("DATA LINK: 1-WIRE");

  int cx = w / 2;
  int cy = h / 2 + 20;
  int r = 44;
  tft.drawCircle(cx, cy, r, ST77XX_WHITE);
  float a = (elapsed % 1000UL) * 0.006283185f;
  int mx = cx + static_cast<int>(cos(a) * r);
  int my = cy + static_cast<int>(sin(a) * r);
  tft.fillCircle(mx, my, 5, ST77XX_RED);

  tft.drawRect(barX, barY, barW, barH, ST77XX_WHITE);
  tft.fillRect(barX + 1, barY + 1, progress > 2 ? (progress - 2) : 0, barH - 2, ST77XX_GREEN);
}

void updateDisplay() {
  int centerX = tft.width() / 2;
  int centerY = tft.height() / 2;
  int radius = min(centerX, centerY) - 27;

  if (lastDrawnAngle >= 0) {
    tft.fillTriangle(lastX1, lastY1, lastX2, lastY2, lastX3, lastY3, ST77XX_BLACK);
  }

  float arrowAngle = radians(currentRotatorAngle - 90.0f);
  int arrowLength = 15;
  int arrowOffset = 5;

  int x1 = centerX + cos(arrowAngle) * (radius + arrowLength + arrowOffset);
  int y1 = centerY + sin(arrowAngle) * (radius + arrowLength + arrowOffset);

  float leftAngle = arrowAngle - 0.12f;
  int x2 = centerX + cos(leftAngle) * (radius + 2 + arrowOffset);
  int y2 = centerY + sin(leftAngle) * (radius + 2 + arrowOffset);

  float rightAngle = arrowAngle + 0.12f;
  int x3 = centerX + cos(rightAngle) * (radius + 2 + arrowOffset);
  int y3 = centerY + sin(rightAngle) * (radius + 2 + arrowOffset);

  tft.fillTriangle(x1, y1, x2, y2, x3, y3, ST77XX_RED);
  // Restore compass outline that can get clipped by arrow erase/draw.
  tft.drawCircle(centerX, centerY, radius, ST77XX_WHITE);

  lastX1 = x1; lastY1 = y1;
  lastX2 = x2; lastY2 = y2;
  lastX3 = x3; lastY3 = y3;
  lastDrawnAngle = currentRotatorAngle;

  int angle = static_cast<int>(currentRotatorAngle);
  String angleStr = String(angle);

  if (lastAngleStr != angleStr) {
    if (lastAngleStr != "" && lastAngleStr != "INIT") {
      tft.setTextSize(6);
      tft.setTextColor(ST77XX_BLACK);
      int oldTextWidth = lastAngleStr.length() * 36;
      tft.setCursor(centerX - oldTextWidth / 2, centerY - 24);
      tft.print(lastAngleStr);
    }

    tft.setTextSize(6);
    tft.setTextColor(ST77XX_YELLOW);
    int textWidth = angleStr.length() * 36;
    tft.setCursor(centerX - textWidth / 2, centerY - 24);
    tft.print(angleStr);

    lastAngleStr = angleStr;
  }

  String percentStr = String(static_cast<int>(currentPotPercent)) + "%";
  String filterInfo = "RX:" + String(rawAdcValue) + " FIL:" + String(static_cast<int>(filteredAdcValue));

  if (percentStr != lastPercentStr) {
    tft.fillRect(0, tft.height() - 45, 90, 22, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(8, tft.height() - 40);
    tft.print(percentStr);
    lastPercentStr = percentStr;
  }

  if (filterInfo != lastFilterInfoStr) {
    tft.fillRect(0, tft.height() - 22, 190, 22, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(2, tft.height() - 14);
    tft.print(filterInfo);
    lastFilterInfoStr = filterInfo;
  }

  String statusStr = "OK";
  uint16_t statusColor = ST77XX_GREEN;

  if (errorState) {
    statusStr = "ERROR";
    statusColor = ST77XX_MAGENTA;
  } else if (highLimitActive || lowLimitActive) {
    statusStr = "LIMIT";
    statusColor = ST77XX_RED;
  }

  if (statusStr != lastStatusStr) {
    tft.fillRect(tft.width() - 90, tft.height() - 30, 90, 30, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(statusColor);
    tft.setCursor(tft.width() - 88, tft.height() - 22);
    tft.print(statusStr);
    lastStatusStr = statusStr;
  }
}
