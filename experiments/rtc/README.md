# RTC Module Experiment

**Status:** Active hardware reference
**Hardware:** ESP32-S3 with the configured I2C RTC module

This isolated firmware verifies RTC detection, read/write behavior, and the
hardware pin configuration.

```powershell
pio run -d experiments -e rtc_module_test
```

The test succeeds when the RTC is detected and repeated reads produce plausible,
monotonic timestamps. Record board wiring, RTC model, initial time, final time,
and any oscillator or power-loss indication with future results.
