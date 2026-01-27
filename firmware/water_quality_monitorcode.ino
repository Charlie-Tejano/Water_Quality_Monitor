/*
  ------------------------------------------------------------------------
  Water Quality / Turbidity Comparator (Tap vs Brita Elite) — Arduino Uno R4 WiFi
  Made by - Charlie Tejano
  01/26/2026
  ------------------------------------------------------------------------

  What this project demonstrates (recruiter-friendly):
    1) Robust analog sensing:
       - Uses the Uno R4's 12-bit ADC (0..4095) for higher resolution readings.
       - Uses median filtering + EMA smoothing to reduce noise and spikes.

    2) Repeatable calibration:
       - Stores two calibration points in EEPROM:
         * "CLEAR"  (Brita Elite filtered water)
         * "CLOUDY" (tap water)
       - Converts raw sensor values into a normalized 0..100 turbidity index.
       - Includes a simple data integrity check so corrupted EEPROM doesn't break behavior.

    3) Embedded UX:
       - One-button long press calibration flow.
       - LCD status display + Serial CSV logging for analysis in Python/pandas.

  ------------------------------------------------------------------------
  Hardware wiring:
    - Turbidity sensor board:
        (+)  -> 5V
        (-)  -> GND
        (D/A)-> A0   (analog output)

    - Button:
        One leg -> D7
        Other leg -> GND
        (uses INPUT_PULLUP: reads HIGH normally, LOW when pressed)

    - LED:
        D8 -> 220Ω resistor -> LED anode (long leg)
        LED cathode (short leg) -> GND

    - LCD1602 I2C:
        VCC -> 5V
        GND -> GND
        SDA -> SDA
        SCL -> SCL

  ------------------------------------------------------------------------
  Libraries:
    - Wire (built-in)
    - EEPROM (built-in)
    - LiquidCrystal_I2C (install via Library Manager)
*/

#include <Wire.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>

// ======================================================================
// Pin assignments (kept simple & readable for reviewers)
// ======================================================================
static const int TURBIDITY_PIN  = A0; // analog input
static const int BUTTON_PIN     = 7;  // active-low button to GND
static const int STATUS_LED_PIN = 8;  // status LED

// ======================================================================
// Sampling & filtering configuration
// ======================================================================
//
// Why filtering?
//  - Turbidity sensor outputs can be noisy due to:
//      * ADC quantization noise
//      * sensor electrical noise
//      * tiny bubbles / motion in water
//      * ambient light leakage (depending on module design)
//  - We want readings stable enough for comparing tap vs filtered.
//
// Strategy:
//  1) Median filter on a burst of samples: rejects outliers (spikes).
//  2) EMA smoothing over time: produces a stable, slowly changing signal.
//
static const uint8_t  MEDIAN_SAMPLE_COUNT = 31; // odd -> well-defined median
static const uint16_t SAMPLE_SPACING_MS   = 3;  // small delay between samples
static const float    EMA_ALPHA           = 0.20f; // 0.1..0.3 typical

// ======================================================================
// Button behavior config
// ======================================================================
//
// We use a long-press to avoid accidental calibration writes to EEPROM.
// The button is debounced using a simple time-based approach.
//
static const uint16_t DEBOUNCE_MS    = 40;
static const uint16_t LONG_PRESS_MS  = 2000;

// ======================================================================
// LCD config
// ======================================================================
//
// LCD backpacks commonly show up at 0x27 or 0x3F (but not always).
// We'll scan I2C at startup and pick the first LCD-like device found.
//
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ======================================================================
// Calibration storage in EEPROM
// ======================================================================
//
// We store:
//  - clearRaw:  sensor reading for "clear" reference water
//              (for your demo: Brita Elite filtered water)
//  - cloudyRaw: sensor reading for "cloudier" reference
//              (for your demo: tap water)
//
// We also store:
//  - magic: identifies that EEPROM contains our expected struct
//  - crc:   simple checksum to detect random corruption
//
struct CalData {
  uint32_t magic;
  uint16_t clearRaw;
  uint16_t cloudyRaw;
  uint16_t crc;
};

static const uint32_t CAL_MAGIC   = 0x54555242; // 'TURB'
static const int      EEPROM_ADDR = 0;

static CalData cal {};
static bool    calLoaded = false;

// ======================================================================
// Runtime state
// ======================================================================
static float emaRaw = -1.0f; // EMA state; -1 indicates "uninitialized"

enum CalStage { CAL_NONE, CAL_WAIT_CLEAR, CAL_WAIT_CLOUDY, CAL_DONE };
static CalStage calStage = CAL_NONE;

// ======================================================================
// Helper: checksum for EEPROM data integrity
// ======================================================================
//
// Not cryptographic—just a lightweight guard against accidental corruption.
//
static uint16_t checksum16(const CalData &d) {
  uint32_t sum = 0;
  sum += (uint16_t)(d.magic & 0xFFFF);
  sum += (uint16_t)((d.magic >> 16) & 0xFFFF);
  sum += d.clearRaw;
  sum += d.cloudyRaw;
  return (uint16_t)(sum & 0xFFFF);
}

// ======================================================================
// EEPROM: load calibration
// ======================================================================
static void loadCalibration() {
  EEPROM.get(EEPROM_ADDR, cal);

  // 1) Check magic signature
  if (cal.magic != CAL_MAGIC) {
    calLoaded = false;
    return;
  }

  // 2) Check checksum
  if (cal.crc != checksum16(cal)) {
    calLoaded = false;
    return;
  }

  // 3) Reject degenerate calibration (same values)
  if (cal.clearRaw == cal.cloudyRaw) {
    calLoaded = false;
    return;
  }

  calLoaded = true;
}

// ======================================================================
// EEPROM: save calibration
// ======================================================================
static void saveCalibration(uint16_t clearRaw, uint16_t cloudyRaw) {
  cal.magic    = CAL_MAGIC;
  cal.clearRaw = clearRaw;
  cal.cloudyRaw = cloudyRaw;
  cal.crc      = checksum16(cal);

  EEPROM.put(EEPROM_ADDR, cal);
  calLoaded = true;
}

// ======================================================================
// I2C helper: detect device present at address
// ======================================================================
static bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

// ======================================================================
// LCD address scan: find likely LCD backpack address
// ======================================================================
static uint8_t findLcdAddress() {
  // Common addresses first:
  const uint8_t candidates[] = { 0x27, 0x3F, 0x26, 0x20, 0x21 };
  for (uint8_t addr : candidates) {
    if (i2cDevicePresent(addr)) return addr;
  }

  // Fallback scan in typical LCD backpack range:
  for (uint8_t addr = 0x20; addr <= 0x3F; addr++) {
    if (i2cDevicePresent(addr)) return addr;
  }

  return 0; // not found
}

// ======================================================================
// Median filter utilities
// ======================================================================
//
// Median works great for rejecting spikes.
// Example: If one sample glitches high/low, median ignores it.
//
static void insertionSort(uint16_t *arr, uint8_t n) {
  for (uint8_t i = 1; i < n; i++) {
    uint16_t key = arr[i];
    int j = i - 1;
    while (j >= 0 && arr[j] > key) {
      arr[j + 1] = arr[j];
      j--;
    }
    arr[j + 1] = key;
  }
}

// Read a "median of burst" value from the sensor
static uint16_t readTurbidityMedian() {
  uint16_t samples[MEDIAN_SAMPLE_COUNT];

  for (uint8_t i = 0; i < MEDIAN_SAMPLE_COUNT; i++) {
    samples[i] = (uint16_t)analogRead(TURBIDITY_PIN);
    delay(SAMPLE_SPACING_MS);
  }

  insertionSort(samples, MEDIAN_SAMPLE_COUNT);
  return samples[MEDIAN_SAMPLE_COUNT / 2];
}

// ======================================================================
// Normalize raw ADC -> turbidity index (0..100)
// ======================================================================
//
// We define turbidity index as:
///   0   = "as clear as your CLEAR reference" (Brita filtered)
///   100 = "as cloudy as your CLOUDY reference" (tap water for this demo)
//
// Note: Many turbidity modules have outputs where "clear" can be higher or lower
// depending on the module design. We detect direction using the calibration points.
//
static int computeTurbidityIndex(uint16_t raw) {
  if (!calLoaded) {
    // If no calibration, still output something (debug-friendly).
    // This mapping is arbitrary but prevents "blank" output.
    long idx = map(raw, 0, 4095, 100, 0);
    if (idx < 0) idx = 0;
    if (idx > 100) idx = 100;
    return (int)idx;
  }

  const float clearV  = (float)cal.clearRaw;
  const float cloudyV = (float)cal.cloudyRaw;

  float t = 0.0f;

  // Determine whether raw increases or decreases with turbidity
  if (cloudyV > clearV) {
    // raw increases as water becomes "cloudier"
    t = ((float)raw - clearV) / (cloudyV - clearV);
  } else {
    // raw decreases as water becomes "cloudier"
    t = (clearV - (float)raw) / (clearV - cloudyV);
  }

  // Clamp to [0..1]
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  // Convert to [0..100]
  int idx = (int)(t * 100.0f + 0.5f);
  if (idx < 0) idx = 0;
  if (idx > 100) idx = 100;
  return idx;
}

// ======================================================================
// Human-readable classification (simple thresholds)
// ======================================================================
//
// Recruiter note:
//  - This is intentionally simple. In real products you'd tune these thresholds
//    using known NTU references or lab measurements.
//
static const int THRESH_CLEAR    = 30;
static const int THRESH_MODERATE = 70;

static const char* classifyStatus(int idx) {
  if (idx < THRESH_CLEAR)    return "CLEAR";
  if (idx < THRESH_MODERATE) return "MODERATE";
  return "TURBID";
}

// ======================================================================
// LED status behavior
// ======================================================================
//
// CLEAR    -> LED off
// MODERATE -> LED slow blink
// TURBID   -> LED on
//
static void updateLed(int idx) {
  static uint32_t lastToggle = 0;
  static bool ledState = false;

  if (idx < THRESH_CLEAR) {
    digitalWrite(PIN_LED, LOW);
    return;
  }

  if (idx < THRESH_MODERATE) {
    uint32_t now = millis();
    if (now - lastToggle >= 700) {
      lastToggle = now;
      ledState = !ledState;
      digitalWrite(PIN_LED, ledState ? HIGH : LOW);
    }
    return;
  }

  digitalWrite(PIN_LED, HIGH);
}

// ======================================================================
// Button handling: stable long-press detector
// ======================================================================
//
// We debounce and then detect "held down for LONG_PRESS_MS" events.
// Returns true once per long press (not continuously).
//
static bool buttonLongPressEvent() {
  static bool lastRaw = false;
  static bool debounced = false;
  static uint32_t lastChange = 0;

  static uint32_t pressStart = 0;
  static bool longFired = false;

  // Button is active LOW due to INPUT_PULLUP
  bool rawPressed = (digitalRead(BUTTON_PIN) == LOW);
  uint32_t now = millis();

  // Debounce: track when raw signal changes
  if (rawPressed != lastRaw) {
    lastRaw = rawPressed;
    lastChange = now;
  }

  // If stable long enough, accept it as debounced state
  if (now - lastChange > DEBOUNCE_MS) {
    debounced = lastRaw;
  }

  // Track press duration
  if (debounced && pressStart == 0) {
    pressStart = now;
    longFired = false;
  }

  // Reset when released
  if (!debounced) {
    pressStart = 0;
    longFired = false;
  }

  // Fire event once when held long enough
  if (debounced && !longFired && pressStart != 0 && (now - pressStart >= LONG_PRESS_MS)) {
    longFired = true;
    return true;
  }

  return false;
}

// ======================================================================
// LCD helper utilities
// ======================================================================
static bool lcdAvailable = false;

static void lcdPrint16(const String &line, uint8_t row) {
  lcd.setCursor(0, row);
  String s = line;

  // Ensure the line exactly fills 16 chars to prevent leftover characters
  if (s.length() < 16) s += String(' ', 16 - s.length());
  if (s.length() > 16) s = s.substring(0, 16);

  lcd.print(s);
}

static void showNormalScreen(uint16_t rawMedian, uint16_t emaRounded, int idx) {
  const char* status = classifyStatus(idx);

  // Top line: turbidity index + label
  char line1[17];
  snprintf(line1, sizeof(line1), "Turb:%3d %s", idx, status);
  lcdPrint16(String(line1), 0);

  // Bottom line: raw + EMA
  char line2[17];
  snprintf(line2, sizeof(line2), "R:%4u E:%4u", rawMedian, emaRounded);
  lcdPrint16(String(line2), 1);
}

static void showCalScreen(const char* top, const char* bottom) {
  lcdPrint16(String(top), 0);
  lcdPrint16(String(bottom), 1);
}

// ======================================================================
// Setup: initialize peripherals and load calibration
// ======================================================================
void setup() {
  // --- GPIO setup ---
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // --- Serial for logging / analysis ---
  Serial.begin(115200);
  delay(200);

  // --- ADC resolution: Uno R4 supports 12-bit ---
  // This improves sensitivity for small differences (e.g., tap vs filtered).
  analogReadResolution(12); // 0..4095

  // --- I2C / LCD ---
  Wire.begin();
  uint8_t addr = findLcdAddress();
  if (addr != 0) {
    lcd = LiquidCrystal_I2C(addr, 16, 2);
    lcd.init();
    lcd.backlight();
    lcdAvailable = true;
  }

  // --- Load calibration from EEPROM ---
  loadCalibration();

  // --- Startup message ---
  if (lcdAvailable) {
    showCalScreen("Turbidity Monitor", calLoaded ? "Cal: LOADED" : "Cal: NOT SET");
    delay(1200);
  }

  // --- CSV header (easy for pandas) ---
  Serial.println("ms,raw_median,ema_raw,index,status,cal_loaded,clear_raw,cloudy_raw");
}

// ======================================================================
// Main loop: calibration flow + sampling + UI + logging
// ======================================================================
void loop() {
  // ------------------------------------------------------------
  // 1) Decide whether we should be in calibration mode
  // ------------------------------------------------------------
  //
  // If the device has never been calibrated (fresh EEPROM),
  // we automatically prompt the user to calibrate.
  //
  if (!calLoaded && calStage == CAL_NONE) {
    calStage = CAL_WAIT_CLEAR;
  }

  // ------------------------------------------------------------
  // 2) Handle calibration actions (long press)
  // ------------------------------------------------------------
  //
  // Calibration procedure for your experiment:
  //   CLEAR  = Brita Elite filtered water
  //   CLOUDY = tap water
  //
  // Each long press captures a stable median reading.
  //
  if (buttonLongPressEvent()) {

    // A) Capture CLEAR reference
    if (calStage == CAL_WAIT_CLEAR) {
      uint16_t clearRaw = readTurbidityMedian();
      cal.clearRaw = clearRaw; // store temporarily in RAM
      calStage = CAL_WAIT_CLOUDY;

      Serial.print("CAL: Saved CLEAR (Brita) RAW = ");
      Serial.println(clearRaw);

      if (lcdAvailable) {
        showCalScreen("Saved CLEAR (Brita)", ("R:" + String(clearRaw)).c_str());
      }
      delay(900);
    }

    // B) Capture CLOUDY reference
    else if (calStage == CAL_WAIT_CLOUDY) {
      uint16_t cloudyRaw = readTurbidityMedian();
      saveCalibration(cal.clearRaw, cloudyRaw);
      calStage = CAL_DONE;

      Serial.print("CAL: Saved CLOUDY (Tap) RAW = ");
      Serial.println(cloudyRaw);

      if (lcdAvailable) {
        showCalScreen("Saved CLOUDY (Tap)", ("R:" + String(cloudyRaw)).c_str());
      }
      delay(900);
    }

    // C) If already calibrated, long press restarts calibration
    else {
      calLoaded = false;
      calStage = CAL_WAIT_CLEAR;

      Serial.println("CAL: Recalibration started. Hold button in Brita filtered water.");

      if (lcdAvailable) {
        showCalScreen("Recalibration", "Hold in Brita...");
      }
      delay(900);
    }
  }

  // ------------------------------------------------------------
  // 3) If in calibration stages, show instructions
  // ------------------------------------------------------------
  if (lcdAvailable) {
    if (calStage == CAL_WAIT_CLEAR) {
      showCalScreen("CAL: Brita CLEAR", "Hold btn 2s...");
    } else if (calStage == CAL_WAIT_CLOUDY) {
      showCalScreen("CAL: Tap WATER", "Hold btn 2s...");
    }
  }

  // ------------------------------------------------------------
  // 4) Read sensor (median burst)
  // ------------------------------------------------------------
  uint16_t rawMedian = readTurbidityMedian();

  // ------------------------------------------------------------
  // 5) Smooth via EMA (stabilizes display & logging)
  // ------------------------------------------------------------
  //
  // EMA provides temporal smoothing:
  //   ema = alpha*new + (1-alpha)*old
  //
  if (emaRaw < 0.0f) emaRaw = (float)rawMedian;
  emaRaw = EMA_ALPHA * (float)rawMedian + (1.0f - EMA_ALPHA) * emaRaw;

  uint16_t emaRounded = (uint16_t)(emaRaw + 0.5f);

  // ------------------------------------------------------------
  // 6) Convert to normalized turbidity index
  // ------------------------------------------------------------
  int idx = computeTurbidityIndex(emaRounded);
  const char* status = classifyStatus(idx);

  // ------------------------------------------------------------
  // 7) Update LED output
  // ------------------------------------------------------------
  updateLed(idx);

  // ------------------------------------------------------------
  // 8) Update LCD (only if not actively prompting calibration)
  // ------------------------------------------------------------
  if (lcdAvailable) {
    if (calStage != CAL_WAIT_CLEAR && calStage != CAL_WAIT_CLOUDY) {
      showNormalScreen(rawMedian, emaRounded, idx);
    }
  }

  // ------------------------------------------------------------
  // 9) Serial CSV logging (for Python/pandas later)
  // ------------------------------------------------------------
  Serial.print(millis());
  Serial.print(",");
  Serial.print(rawMedian);
  Serial.print(",");
  Serial.print(emaRounded);
  Serial.print(",");
  Serial.print(idx);
  Serial.print(",");
  Serial.print(status);
  Serial.print(",");
  Serial.print(calLoaded ? "1" : "0");
  Serial.print(",");
  Serial.print(calLoaded ? cal.clearRaw : 0);
  Serial.print(",");
  Serial.println(calLoaded ? cal.cloudyRaw : 0);

  // ------------------------------------------------------------
  // 10) Loop pacing
  // ------------------------------------------------------------
  //
  // Keep loop rate human-friendly for LCD + Serial.
  // If you want higher-rate logging for analysis, reduce delay.
  //
  delay(350);
}

/*
  ------------------------------------------------------------------------
  Recommended demo run (tap vs Brita Elite):

  1) Put sensor in Brita filtered water
  2) Hold button 2 seconds -> saves CLEAR (Brita)
  3) Move sensor to tap water
  4) Hold button 2 seconds -> saves CLOUDY (Tap)
  5) Now the index:
       0   ~ close to Brita
       100 ~ close to tap
     (Your results depend on sensor module + water differences)

  Tip:
    - Keep water still for each calibration capture (median is robust,
      but motion/bubbles can still influence readings).
  ------------------------------------------------------------------------
*/