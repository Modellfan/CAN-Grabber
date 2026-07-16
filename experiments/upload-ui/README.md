# Upload UI Experiment

**Status:** v5 active reference; v1-v4 superseded
**Hardware:** ESP32-S3, SPI SD card, Wi-Fi, and an HTTP upload target

This experiment evolved the upload UI through five firmware versions. Version
5 validates queue seeding, retry/backoff, endpoint contracts, uploaded-state
persistence, SD recovery, reachability probing, and UI responsiveness.

```powershell
pio run -d experiments -e sd_http_upload_ui_test_v5
python experiments/upload-ui/tools/upload_ui_tester_v5.py --base-url http://<esp-ip> --start
```

Copy `config/sd_http_upload_ui_secrets.h.example` without `.example` for local
credentials. The mock target is `tools/can-upload-mock/server.py`. Detailed
instructions and architecture are in `docs/`; all historical JSONL, summaries,
serial logs, and the accumulated Markdown report are in `results/raw/`.

The original timestamped restore points are preserved unchanged in
`history/stages/`. They are historical evidence only; use normal Git commits
for future changes. A v5 run succeeds when the requested test reaches `DONE`,
the UI remains reachable, persistence checks pass, and no contract or SD error
is reported.
