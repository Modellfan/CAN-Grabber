# Network and HTTP Experiments

**Status:** Concluded experiments retained as references
**Hardware:** ESP32-S3 with Wi-Fi and, for SD tests, an SPI SD card

This group compares STA/AP operation and several HTTP download implementations:
AsyncWebServer with a reader task, manual `WebServer` streaming,
`WebServer::streamFile()`, raw Wi-Fi throughput, and an isolated Content-Length
example.

```powershell
pio run -d experiments -e sta_ap_test
pio run -d experiments -e sd_http_download_test
pio run -d experiments -e sd_http_download_test2
pio run -d experiments -e webserver_streamfile_test
pio run -d experiments -e wifi_speed_test
pio run -d experiments -e set_content_length_test
```

Copy `config/sd_http_download_secrets.h.example` to the same name without the
`.example` suffix and fill in local Wi-Fi values. That real file is ignored.

Historical notes measured roughly 440 kB/s for the async SD download variant
and roughly 230 kB/s for manual streaming. These measurements are comparison
evidence, not current production performance guarantees.
