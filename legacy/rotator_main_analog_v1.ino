/*
 * Arduino Rotátor s víceotáčkovým potenciometrem
 * Převodový poměr: 96:16 (6:1)
 * TFT displej: ST7789V 240x320
 *
 * Verze: 1.1 - stabilizované čtení A0 (buffer + RC + SW filtrace + hystereze limitů)
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// Definice pinů
#define TFT_CS    10
#define TFT_RST   9
#define TFT_DC    8
#define POT_PIN   A0
#define RELAY_LEFT_PIN  2
#define RELAY_RIGHT_PIN 3
#define BUZZER_PIN 7

// Konstanty pro potenciometr
constexpr int POT_MAX_VALUE = 1023;     // Maximální hodnota ADC (10-bit)
constexpr int POT_MAX_TURNS = 10;       // Počet otáček potenciometru
constexpr int POT_DEGREES_PER_TURN = 360;

// Konstanty pro převod (zachováno dle původního projektu)
constexpr float GEAR_RATIO = 6.0f;      // Převodový poměr (96:16)

// Limity v procentech s hysterezí (snadno laditelné)
constexpr float LOW_LIMIT_ON_PERCENT = 9.0f;
constexpr float LOW_LIMIT_OFF_PERCENT = 12.0f;
constexpr float HIGH_LIMIT_ON_PERCENT = 91.0f;
constexpr float HIGH_LIMIT_OFF_PERCENT = 88.0f;

// Filtrace analogového vstupu
constexpr uint8_t ANALOG_SAMPLES = 32;          // Oversampling 32 vzorků
constexpr uint16_t SAMPLE_DELAY_US = 250;       // Pauza mezi vzorky
constexpr float EMA_ALPHA = 0.15f;              // IIR low-pass (EMA)

// Detekce poruchy signálu
constexpr int ADC_EXTREME_LOW = 2;              // Podezřelá hodnota blízko 0
constexpr int ADC_EXTREME_HIGH = 1021;          // Podezřelá hodnota blízko 1023
constexpr uint8_t ADC_EXTREME_COUNT_TRIP = 20;  // Kolik cyklů v extrému spustí ERROR

// Časování
constexpr uint16_t LOOP_PERIOD_MS = 50;         // UI ~20 Hz
constexpr uint16_t LIMIT_BUZZER_DEBOUNCE_MS = 200;
constexpr uint16_t LIMIT_BUZZER_REPEAT_MS = 1500;

// Inicializace TFT displeje
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Globální proměnné
float currentRotatorAngle = 0.0f;
float currentPotRatio = 0.0f;
float currentPotPercent = 0.0f;

int rawAdcValue = 0;
float filteredAdcValue = 0.0f;
bool emaInitialized = false;

bool lowLimitActive = false;
bool highLimitActive = false;
bool errorState = false;
bool safetyLimitReached = false;

uint8_t extremeCounter = 0;
unsigned long limitStateSinceMs = 0;
unsigned long lastLimitBeepMs = 0;

// Proměnné pro optimalizaci vykreslování
float lastDrawnAngle = -1.0f;
int lastX1 = 0, lastY1 = 0, lastX2 = 0, lastY2 = 0, lastX3 = 0, lastY3 = 0;
String lastAngleStr = "";
String lastPercentStr = "";
String lastFilterInfoStr = "";
String lastStatusStr = "";

unsigned long lastLoopMs = 0;

float readAnalogFiltered();
void updatePositionFromAdc(float adcFiltered);
void updateSafetyState();
void applyRelayInterlock(bool blockLeft, bool blockRight);
void updateBuzzer();
void drawCompass();
void updateDisplay();
void loadInitialPosition();

void setup() {
  Serial.begin(9600);

  // Bezpečný stav relé MUSÍ být nastaven okamžitě po bootu
  pinMode(RELAY_LEFT_PIN, OUTPUT);
  pinMode(RELAY_RIGHT_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  applyRelayInterlock(true, true);  // FAIL-SAFE: blokace obou směrů

  pinMode(POT_PIN, INPUT);

  Serial.println("Rotátor - inicializace...");

  // Inicializace TFT displeje
  tft.init(240, 320);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  drawCompass();
  loadInitialPosition();

  // Vynutit překreslení při prvním zobrazení
  lastAngleStr = "INIT";
  lastPercentStr = "INIT";
  lastFilterInfoStr = "INIT";
  lastStatusStr = "INIT";
  updateDisplay();

  Serial.println("Rotátor připraven!");
}

void loop() {
  unsigned long now = millis();
  if (now - lastLoopMs < LOOP_PERIOD_MS) {
    return;
  }
  lastLoopMs = now;

  rawAdcValue = analogRead(POT_PIN);
  filteredAdcValue = readAnalogFiltered();

  // Detekce poruchy signálu (odpojený kabel, tvrdý zkrat, saturace)
  bool extreme = (rawAdcValue <= ADC_EXTREME_LOW || rawAdcValue >= ADC_EXTREME_HIGH);
  if (extreme) {
    if (extremeCounter < 255) {
      extremeCounter++;
    }
  } else {
    extremeCounter = 0;
  }

  bool invalidValue = (!isfinite(filteredAdcValue) || filteredAdcValue < 0.0f || filteredAdcValue > POT_MAX_VALUE);
  errorState = invalidValue || (extremeCounter >= ADC_EXTREME_COUNT_TRIP);

  if (!errorState) {
    updatePositionFromAdc(filteredAdcValue);
  }

  updateSafetyState();
  updateBuzzer();
  updateDisplay();

  // Debug výstup
  Serial.print("RAW: ");
  Serial.print(rawAdcValue);
  Serial.print(" | FIL: ");
  Serial.print(filteredAdcValue, 1);
  Serial.print(" | %: ");
  Serial.print(currentPotPercent, 2);
  Serial.print(" | AZ: ");
  Serial.print(currentRotatorAngle, 1);
  Serial.print(" | L/H/E: ");
  Serial.print(lowLimitActive);
  Serial.print("/");
  Serial.print(highLimitActive);
  Serial.print("/");
  Serial.println(errorState);
}

void loadInitialPosition() {
  rawAdcValue = analogRead(POT_PIN);
  filteredAdcValue = readAnalogFiltered();

  if (isfinite(filteredAdcValue) && filteredAdcValue >= 0.0f && filteredAdcValue <= POT_MAX_VALUE) {
    updatePositionFromAdc(filteredAdcValue);
    errorState = false;
  } else {
    errorState = true;
    currentPotRatio = 0.0f;
    currentPotPercent = 0.0f;
    currentRotatorAngle = 0.0f;
  }

  updateSafetyState();

  Serial.print("Počáteční pozice načtena: ");
  Serial.print(currentRotatorAngle, 1);
  Serial.println("°");
}

float readAnalogFiltered() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ANALOG_SAMPLES; i++) {
    sum += analogRead(POT_PIN);
    delayMicroseconds(SAMPLE_DELAY_US);
  }

  float avg = static_cast<float>(sum) / ANALOG_SAMPLES;

  if (!emaInitialized || !isfinite(filteredAdcValue)) {
    filteredAdcValue = avg;
    emaInitialized = true;
  } else {
    filteredAdcValue = filteredAdcValue * (1.0f - EMA_ALPHA) + avg * EMA_ALPHA;
  }

  return filteredAdcValue;
}

void updatePositionFromAdc(float adcFiltered) {
  currentPotRatio = adcFiltered / static_cast<float>(POT_MAX_VALUE);
  if (currentPotRatio < 0.0f) currentPotRatio = 0.0f;
  if (currentPotRatio > 1.0f) currentPotRatio = 1.0f;

  currentPotPercent = currentPotRatio * 100.0f;

  // Výpočet úhlu potenciometru (0° - 3600°)
  float potAngle = currentPotRatio * POT_MAX_TURNS * POT_DEGREES_PER_TURN;

  // Střed potenciometru (50% = 5 otáček = 1800°) odpovídá 180° azimutu
  float potAngleFromCenter = potAngle - 1800.0f;
  currentRotatorAngle = (potAngleFromCenter / GEAR_RATIO) + 180.0f;

  // Normalizace azimutu na 0° - 360°
  while (currentRotatorAngle < 0.0f) currentRotatorAngle += 360.0f;
  while (currentRotatorAngle >= 360.0f) currentRotatorAngle -= 360.0f;
}

void updateSafetyState() {
  bool previousAnyLimit = safetyLimitReached || errorState;

  if (!errorState) {
    // Hystereze dolního limitu
    if (!lowLimitActive && currentPotPercent <= LOW_LIMIT_ON_PERCENT) {
      lowLimitActive = true;
    } else if (lowLimitActive && currentPotPercent >= LOW_LIMIT_OFF_PERCENT) {
      lowLimitActive = false;
    }

    // Hystereze horního limitu
    if (!highLimitActive && currentPotPercent >= HIGH_LIMIT_ON_PERCENT) {
      highLimitActive = true;
    } else if (highLimitActive && currentPotPercent <= HIGH_LIMIT_OFF_PERCENT) {
      highLimitActive = false;
    }
  } else {
    // Při chybě zakázat obě směry
    lowLimitActive = true;
    highLimitActive = true;
  }

  safetyLimitReached = lowLimitActive || highLimitActive;

  // U relé logiky zachováme původní architekturu:
  // LOW = aktivace blokace daného směru, HIGH = směr povolen
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
    // Chyba = odlišné upozornění (2 krátké tóny), max 1x za 2 s
    if (now - lastLimitBeepMs > 2000) {
      tone(BUZZER_PIN, 1500, 120);
      delay(140);
      tone(BUZZER_PIN, 1100, 180);
      lastLimitBeepMs = now;
    }
    return;
  }

  if (safetyLimitReached) {
    // Debounce proti rychlému překmitu: limit musí trvat alespoň LIMIT_BUZZER_DEBOUNCE_MS
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

  // Spodní levá část: FIL procenta + RAW/FIL informace
  String percentStr = String(static_cast<int>(currentPotPercent)) + "%";
  String filterInfo = "RAW:" + String(rawAdcValue) + " FIL:" + String(static_cast<int>(filteredAdcValue));

  if (percentStr != lastPercentStr) {
    tft.fillRect(0, tft.height() - 45, 90, 22, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(8, tft.height() - 40);
    tft.print(percentStr);
    lastPercentStr = percentStr;
  }

  if (filterInfo != lastFilterInfoStr) {
    tft.fillRect(0, tft.height() - 22, 170, 22, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(2, tft.height() - 14);
    tft.print(filterInfo);
    lastFilterInfoStr = filterInfo;
  }

  // Pravý dolní roh: stav
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

void emergencyStop() {
  applyRelayInterlock(true, true);
  lowLimitActive = true;
  highLimitActive = true;
  safetyLimitReached = true;
  errorState = true;
  Serial.println("EMERGENCY STOP!");
}

void resetSafetyLimits() {
  lowLimitActive = false;
  highLimitActive = false;
  safetyLimitReached = false;
  errorState = false;
  extremeCounter = 0;
  applyRelayInterlock(false, false);
  Serial.println("Bezpečnostní limity resetovány");
}
