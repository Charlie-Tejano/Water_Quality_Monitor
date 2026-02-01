# Water_Quality_Monitor
 - Designed a Water Quality Monitor prototype with an Arduino Uno R4 Wifi to determine the quality of water whether it's - clear, cloudy, unsafe.
 - Arduino R4 Uno WiFi (Turbidity Logger) + Python (pandas) + C++

==============================================================================================================================================================
# Data Collection Notes

Duration intiial data collection, turbidity calibration button (long-press for clear & cloudy reference) was not correctly registered due to a wiring issue.

As a result:
- 'cal_loaded = 0' for all recorded samples
- 'clear_raw' and 'cloudy_raw' remain 0
- The reported 'index' and 'status' fields reflect a fallback mapping and should NOT be interpreted as absolute turbidity values.

However:
- Raw ADC readings ('raw_median', 'ema_raw') are valid
- Sensor output was stable and consistent over time
- The dataset is suitable for relative comparison (Brita-filtered water vs. tap water)
============================================================================================================================================================


Goals:
- Reads a turbidity sensor through the ADC
- Reduce noise using median filtering
- Stabilizes readings using EMA smoothing
- Outputs consistent CSV logs for analysis

Ran repeated experiments comparing...
- Brita (Elite) filtered water vs. tap water
- Brita = (~1m 13s ran)
- Tap Water = (~1m 03s ran)
- The particulate-added solution - tap water + chili powder


=================== Hardware Specifications + Wiring ====================

Board: Arduino Uno R4 WiFi

Sensor: Analog turbidity sensor (signal -> A0 (analog), VCC -> 5V, GND -> GND)

Button: Momentary push button (D7 -> GND, using 'INPUT_PULLUP', Opposite Leg -> GND)

LED: Status LED (D8 -> 220(ohm) resistor -> LED -> GND)

LCD1602 I2C: 16x2 I2C LCD (VCC -> 5V, GND -> GND, SDA/SCL -> SDA/SCL pins)

=========================================================================














