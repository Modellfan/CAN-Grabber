# Upload UI + POST Test Runbook

This runbook defines how agents should execute and evaluate the ESP32 upload tests.

## Scope

- Main target: `sd_http_upload_ui_test`
- Baseline target: `sd_http_post_speed_test`
- Mock upload server example: `http://192.168.0.37:8000/edit`

## Required Config

Set these files before running:

1. `include/dev/sd_http_upload_ui_secrets.h`
- `SD_HTTP_UPLOAD_TEST_SSID`
- `SD_HTTP_UPLOAD_TEST_PASSWORD`
- `SD_HTTP_UPLOAD_TEST_URL`

2. `include/dev/sd_http_post_speed_secrets.h`
- `SD_HTTP_TEST_SSID`
- `SD_HTTP_TEST_PASSWORD`
- `SD_HTTP_POST_TEST_URL`

Both URLs should point to the same server endpoint during comparison.

## Tools Used

- Firmware build/flash: PlatformIO
- UI test runner: `tools/upload_ui_tester.py`
- Speed baseline serial runner: `tools/serial-run.ps1`

## Execution Procedure

### A) Flash and verify UI firmware

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test -t upload
```

Find ESP IP via serial boot (`Wi-Fi IP: ...`) or reuse known DHCP lease.

### B) Webservice availability check (fast health check)

```powershell
python tools/upload_ui_tester.py --base-url http://<ESP_IP> --check-only --request-timeout 10
```

Expected: `check_ok: ...`

### C) Execute one upload run (UI + upload in parallel)

```powershell
python tools/upload_ui_tester.py --base-url http://<ESP_IP> --start --request-timeout 10 --timeout 180 --prefix upload_ui_run
```

Artifacts:
- JSONL samples: `logs/upload_ui_run_*.jsonl`
- Summary: `logs/upload_ui_run_*.summary.txt`

### D) Baseline run with original speed test

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload -e sd_http_post_speed_test
powershell -ExecutionPolicy Bypass -File tools\serial-run.ps1 -EnvName sd_http_post_speed_test -NoFlash -TimeoutSec 240
```

Artifacts:
- Serial log: `logs/sd_http_post_speed_*.log`
- Summary: `logs/sd_http_post_speed_*.summary.txt`

## Pass/Fail Thresholds

Apply these thresholds to each `sd_http_upload_ui_test` run.

### 1) Functional pass (mandatory)

All must be true:

- `result_state: DONE` in `upload_ui_run_*.summary.txt`
- `error_code: 0`
- `sent_bytes == total_bytes`
- Serial log contains end line similar to:
  - `UPLOAD_LOG tag=end ... state=DONE ... http=201 ... error=0`

If any fail: run is **FAIL**.

### 2) Throughput pass (network-dependent but required)

- `avg_upload_mb_s >= 0.25`

Note: recent validated runs were around `0.29-0.37 MB/s` on this setup.

### 3) UI responsiveness pass while upload is active

Measure with `/status` polling during upload. Pass criteria:

- success rate `>= 99%` (timeouts/failures <= 1%)
- p95 latency `<= 100 ms`
- max latency `<= 1500 ms`

If p95 exceeds 100 ms consistently or failures exceed 1%, mark **DEGRADED**.

### 4) Stability over repeated runs

Run at least 3 consecutive runs:

- minimum 3/3 functional pass (or 2/3 only acceptable with explicit note)
- no unhandled firmware reset/panic

## Recommended 3-run sequence

```powershell
python tools/upload_ui_tester.py --base-url http://<ESP_IP> --start --request-timeout 10 --timeout 180 --prefix upload_ui_r1
python tools/upload_ui_tester.py --base-url http://<ESP_IP> --start --request-timeout 10 --timeout 180 --prefix upload_ui_r2
python tools/upload_ui_tester.py --base-url http://<ESP_IP> --start --request-timeout 10 --timeout 180 --prefix upload_ui_r3
```

## Known Failure Signatures

1. Early connection reset (server rejects request format)
- Serial: `write(): ... errno: 104, "Connection reset by peer"`
- Usually indicates wrong POST wire format.

2. Poller misses terminal state
- UI falls back to `IDLE` too quickly.
- Firmware now keeps terminal state briefly; still verify via serial `UPLOAD_LOG tag=end`.

3. Python connectivity failures with proxy env vars
- Environment may set `HTTP_PROXY` to invalid localhost.
- `tools/upload_ui_tester.py` already bypasses proxies explicitly.

## Reporting Template

For each run, report:

1. Firmware env + git ref
2. Target URL
3. `result_state`, `error_code`, `avg_upload_mb_s`
4. `sent_bytes/total_bytes`
5. UI responsiveness stats (`success`, `p95`, `max`)
6. Paths to summary/log files

