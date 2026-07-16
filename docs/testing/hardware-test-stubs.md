# Hardware Experiment Guide

Hardware experiments verify real ESP32-S3, SD, CAN, Wi-Fi, RTC, and upload
behavior. They are deliberately separated from production firmware under
[`experiments/`](../../experiments/README.md).

The root `platformio.ini` contains production environments only. Build an
experiment from the repository root with:

```powershell
pio run -d experiments -e <environment>
```

The central [experiment catalog](../../experiments/README.md) lists every
environment and links to its code, prerequisites, acceptance criteria, tools,
and historical evidence. Integration experiments such as `rx_load_test` compile
the current production modules directly rather than keeping copies.

## Evidence Rules

- Store meaningful summaries with the owning experiment.
- Store raw files up to 5 MiB in `results/raw/`.
- Store larger local captures in ignored `results/local/` and record their
  path and checksum in the summary.
- Commit only example credential files.
- Use Git commits instead of creating new stage snapshots.
