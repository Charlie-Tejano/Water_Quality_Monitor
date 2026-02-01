# Water_Quality_Monitor
Designed a Water Quality Monitor prototype with an Arduino Uno R4 Wifi to determine the quality of water whether it's - clear, cloudy, unsafe...

### Data Collection Notes

During initial data collection, the turbidity calibration button
(long-press for clear/cloudy reference) was not correctly registered
due to a wiring/configuration issue.

As a result:
- `cal_loaded = 0` for all recorded samples
- `clear_raw` and `cloudy_raw` remain 0
- The reported `index` and `status` fields reflect a fallback mapping
  and should NOT be interpreted as absolute turbidity values

However:
- Raw ADC readings (`raw_median`, `ema_raw`) are valid
- Sensor output was stable and consistent over time
- The dataset is suitable for **relative comparison**
  (e.g., Brita-filtered water vs tap water)

A corrected calibration procedure is documented for future data
collection.




















