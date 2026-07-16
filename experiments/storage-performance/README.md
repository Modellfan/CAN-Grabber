# Storage Performance

**Status:** Active reference
**Hardware:** ESP32-S3 with SPI SD or SD_MMC/SDIO storage

This experiment compares raw storage write behavior without starting the full
CAN-Grabber runtime.

```powershell
pio run -d experiments -e sd_speed_test
pio run -d experiments -e sd_sdio_speed_test
```

The firmware lives in `code/`. Historical serial evidence, including the SD_MMC
open-file test, is preserved byte-for-byte in `results/raw/`. A run succeeds
when the card mounts, requested bytes are written, and no write failure is
reported. Results should record card type, interface, block size, byte count,
elapsed time, and measured throughput.

The tests informed storage-interface and open-file-limit decisions; they do not
replace production storage integration testing.
