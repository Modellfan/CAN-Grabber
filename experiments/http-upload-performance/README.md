# HTTP Upload Performance

**Status:** Concluded benchmark retained as a baseline
**Hardware:** ESP32-S3, SPI SD card, Wi-Fi, and an HTTP upload target

This experiment benchmarks multipart-style SD-to-HTTP upload behavior and
records serial and summary output.

```powershell
pio run -d experiments -e sd_http_post_speed_test
```

Copy `config/sd_http_post_speed_secrets.h.example` without the `.example`
suffix for local credentials and the target URL. `tools/serial-run.ps1` captures
serial output. All historical `sd_http_post_speed_*` logs are preserved in
`results/raw/`.

A valid run reaches its terminal result, reports transferred bytes and elapsed
time, and contains no SD read, socket write, timeout, or response-contract
failure. This benchmark provided the throughput baseline used by the upload UI
experiments.
