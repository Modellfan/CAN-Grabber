# CAN Pipeline Experiments

**Status:** Active integration experiments
**Hardware:** ESP32-S3 and SD card; CAN input may be simulated

These experiments compile the current production modules directly from `src/`.
No production implementation is copied into this directory.

```powershell
pio run -d experiments -e rx_load_test
pio run -d experiments -e can_to_file_modules
```

`rx_load_test` drives synthetic per-bus traffic through the block and log-writer
path. `can_to_file_modules` exercises the assembled CAN-to-storage runtime
without the normal production entrypoint. Acceptance evidence should include
target and observed FPS, produced/consumed counts, queue depth, drops, write
rate, and storage failures.
