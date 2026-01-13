/*
  Water Quality Monitor — Analog Turbidity Sensor (Arduino Uno) - C++ / Python (PANDAS)

  Overview:
  - Reads analog turbidity data from a sensor on A0
  - Smooths noisy readings
  - Normalizes values using calibration points
  - Classifies water as CLEAR, CLOUDY, or UNSAFE
  - Outputs results to the Serial Monitor

  Note:
  - Turbidity indicates suspended particles, NOT biological safety.
*/

const int TURBIDITY_PIN = A0;
const unsigned long UPDATE_INTERVAL_MS = 500;

// Sampling & filtering
const int NUM_SAMPLES = 30;     // Averaging reduces random ADC noise
const float EMA_ALPHA = 0.25;   // Exponential moving average weight

float filteredRaw = NAN;

// Calibration values
// (Replace with your measured averages)
int clearRaw = 900;   // Brita-filtered water
int dirtyRaw = 300;   // Cloudy reference or tap water

// Classification thresholds
const int CLEAR_MAX = 20;
const int CLOUDY_MAX = 60;

// Water state definition
enum WaterState { CLEAR, CLOUDY, UNSAFE, SENSOR_ERROR };

const char* stateToString(WaterState state) {
  switch (state) {
    case CLEAR:        return "CLEAR";
    case CLOUDY:       return "CLOUDY";
    case UNSAFE:       return "UNSAFE";
    case SENSOR_ERROR: return "SENSOR ERROR";
    default:           return "UNKNOWN";
  }
}

// Read and average ADC values
int readAveragedADC(int pin, int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return sum / samples;
}

// Convert raw ADC value to voltage
float adcToVoltage(int raw) {
  return raw * (5.0 / 1023.0);
}

// Normalize ADC value to turbidity index (0–100)
int computeTurbidityIndex(float raw) {
  int minVal = min(clearRaw, dirtyRaw);
  int maxVal = max(clearRaw, dirtyRaw);

  if (abs(maxVal - minVal) < 1) return 0;

  float normalized = (raw - minVal) / (float)(maxVal - minVal);
  bool clearIsHigh = (clearRaw > dirtyRaw);

  float turbidity = clearIsHigh ? (1.0 - normalized) : normalized;
  return constrain(round(turbidity * 100), 0, 100);
}

// Classify water state
WaterState classifyWater(int index, int raw) {
  if (raw <= 5 || raw >= 1018) return SENSOR_ERROR;
  if (index <= CLEAR_MAX) return CLEAR;
  if (index <= CLOUDY_MAX) return CLOUDY;
  return UNSAFE;
}

// Arduino setup
void setup() {
  Serial.begin(9600);
  Serial.println("Water Quality Monitor Initialized");
}

// Main loop
void loop() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < UPDATE_INTERVAL_MS) return;
  lastUpdate = millis();

  // Read sensor and smooth signal
  int rawAvg = readAveragedADC(TURBIDITY_PIN, NUM_SAMPLES);
  filteredRaw = isnan(filteredRaw)
                  ? rawAvg
                  : EMA_ALPHA * rawAvg + (1 - EMA_ALPHA) * filteredRaw;

  // Compute turbidity and classification
  int turbidityIndex = computeTurbidityIndex(filteredRaw);
  WaterState state = classifyWater(turbidityIndex, rawAvg);

  // Output results
  Serial.print("Raw: ");
  Serial.print(rawAvg);
  Serial.print(" | Voltage: ");
  Serial.print(adcToVoltage(rawAvg), 3);
  Serial.print(" V | Index: ");
  Serial.print(turbidityIndex);
  Serial.print(" | State: ");
  Serial.println(stateToString(state));
}
