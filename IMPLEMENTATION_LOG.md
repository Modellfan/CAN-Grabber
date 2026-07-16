# Implementation Log
This file is the permanent record for development work in this project.

## Required For Every Future Task
- Write planned implementation steps before making changes.
- Document all file/code changes after implementation.
- Document every automated test/command run and whether it passed or failed.
- Include a `Result` section for outcome metrics/values.
- Include an `Artifacts` section with markdown links to produced files.
- Put blockers/assumptions only in `Notes` (do not place run metrics/artifact lists there).
- If a test is not run, explicitly state why.
- Every entry title MUST include date, time, and timezone.

## Entry Template
`YYYY-MM-DD HH:MM:SS +/-TZ - Task Title`

### Planned Steps
- Step 1
- Step 2
- Step 3

### Changes Made
- `path/to/file`: short description

### Automated Tests Run
- `command`: PASS/FAIL
- `command`: PASS/FAIL

### Result
- `result_key: value`

### Artifacts
- `[artifact-name](path/to/artifact)`

### Notes
- Optional blockers, follow-ups, or assumptions.

## 2026-07-06 00:17:46 +02:00 - CAN Tool Subcommands And Simulation Features
### Planned Steps
- Refactor `tools/python_can_tool/slcan_tool.py` from flat options into subcommands for listen, tx, playback, echo, decode, and analyze.
- Add shared helpers for profiles, filters, dry-run transmit, playback controls, DBC decoding, and JSON/CSV summaries.
- Add pytest coverage, example config files, README updates, and run offline plus COM22 live validation.

### Changes Made
- `tools/python_can_tool/slcan_tool.py`: replaced flat CLI with subcommands `listen`, `tx`, `playback`, `echo`, `decode`, and `analyze`; added profiles, filters, playback windows/rewrite/loop gap, dry-run TX/playback/echo, JSON echo rules, offline decode/analyze, live decoded printing, and JSON/CSV summaries.
- `tools/python_can_tool/README.md`: documented subcommands-only usage, profiles, filters, dry-run commands, playback controls, echo mode, offline decode/analyze, and summary outputs.
- `tools/python_can_tool/profiles.json`: added repo-local `canable500` and `canable500_fd2m` profiles.
- `tools/python_can_tool/examples/echo_rules.example.json`: added example echo response rule for observed `0x3E8` traffic.
- `tools/python_can_tool/examples/test_3e8.dbc`: added small DBC fixture for `0x3E8` payload decoding.
- `tools/python_can_tool/tests/test_slcan_tool.py`: added pytest coverage for fixed-message validation, SLCAN parsing, filters, ID rewrite parsing, subcommand requirement, and ASC reading.

### Automated Tests Run
- `python -m py_compile tools\python_can_tool\slcan_tool.py`: PASS
- `python -m pytest tools\python_can_tool\tests`: PASS (`7 passed`)
- `python tools\python_can_tool\slcan_tool.py --help`: PASS
- `python tools\python_can_tool\slcan_tool.py listen --help`: PASS
- `python tools\python_can_tool\slcan_tool.py tx --help`: PASS
- `python tools\python_can_tool\slcan_tool.py playback --help`: PASS
- `python tools\python_can_tool\slcan_tool.py echo --help`: PASS
- `python tools\python_can_tool\slcan_tool.py decode --help`: PASS
- `python tools\python_can_tool\slcan_tool.py analyze --help`: PASS
- `python tools\python_can_tool\slcan_tool.py tx --tx-id 3E8 --tx-data 02 --dry-run`: PASS
- `python tools\python_can_tool\slcan_tool.py playback --playback-log-file tools\python_can_tool\logs\slcan_com22_20260705_230752.asc --filter-id 3E8 --start-time 0 --end-time 0.2 --rewrite-id 3E8:3E9 --dry-run`: PASS
- `python tools\python_can_tool\slcan_tool.py echo --rules-file tools\python_can_tool\examples\echo_rules.example.json --dry-run`: PASS
- `python tools\python_can_tool\slcan_tool.py analyze --asc-file tools\python_can_tool\logs\slcan_com22_20260705_230752.asc --filter-id 3E8 --summary-prefix tools\python_can_tool\logs\offline_analyze_test`: PASS
- `python tools\python_can_tool\slcan_tool.py decode --asc-file tools\python_can_tool\logs\slcan_com22_20260705_230752.asc --dbc-file tools\python_can_tool\examples\test_3e8.dbc --output-csv tools\python_can_tool\logs\offline_decode_test.csv --summary-prefix tools\python_can_tool\logs\offline_decode_test`: PASS
- `python tools\python_can_tool\slcan_tool.py listen --port 22 --can-bitrate 500000 --duration 1 --listen-only`: PASS
- `python tools\python_can_tool\slcan_tool.py listen --port 22 --can-bitrate 500000 --duration 1 --listen-only --filter-id 3E8`: PASS
- `python tools\python_can_tool\slcan_tool.py listen --port 22 --can-bitrate 500000 --duration 1 --listen-only --dbc-file tools\python_can_tool\examples\test_3e8.dbc --print-decoded`: PASS
- `python tools\python_can_tool\slcan_tool.py tx --port 22 --can-bitrate 500000 --tx-id 3E8 --tx-data 02 --tx-count 1 --tx-interval-ms 0`: PASS
- `python tools\python_can_tool\slcan_tool.py playback --port 22 --can-bitrate 500000 --playback-log-file tools\python_can_tool\logs\slcan_com22_20260705_230752.asc --duration 0.05`: PASS
- `python tools\python_can_tool\slcan_tool.py echo --port 22 --can-bitrate 500000 --rules-file tools\python_can_tool\examples\echo_rules.example.json --duration 1`: PASS
- `python tools\python_can_tool\slcan_tool.py tx --profile canable500 --tx-id 3E8 --tx-data 02 --dry-run`: PASS

### Result
- `pytest_count: 7 passed`
- `offline_analyze_frames: 100`
- `offline_decode_frames: 100`
- `offline_decode_signal_rows: 100`
- `live_listen_frames: 10`
- `live_filter_frames: 10`
- `live_dbc_decoded_frames: 10`
- `live_dbc_signal_rows: 10`
- `live_tx_sent_frames: 1`
- `live_playback_sent_frames: 1`
- `live_echo_received_frames: 9`
- `live_echo_matched_frames: 9`
- `live_echo_sent_frames: 9`
- `profile_smoke_frame: t3E8102`

### Artifacts
- [slcan_tool.py](tools/python_can_tool/slcan_tool.py)
- [README.md](tools/python_can_tool/README.md)
- [profiles.json](tools/python_can_tool/profiles.json)
- [echo_rules.example.json](tools/python_can_tool/examples/echo_rules.example.json)
- [test_3e8.dbc](tools/python_can_tool/examples/test_3e8.dbc)
- [test_slcan_tool.py](tools/python_can_tool/tests/test_slcan_tool.py)
- [offline_decode_test.csv](tools/python_can_tool/logs/offline_decode_test.csv)
- [offline_decode_test_summary.json](tools/python_can_tool/logs/offline_decode_test_summary.json)
- [offline_decode_test_ids.csv](tools/python_can_tool/logs/offline_decode_test_ids.csv)
- [offline_decode_test_signals.csv](tools/python_can_tool/logs/offline_decode_test_signals.csv)
- [offline_analyze_test_summary.json](tools/python_can_tool/logs/offline_analyze_test_summary.json)
- [offline_analyze_test_ids.csv](tools/python_can_tool/logs/offline_analyze_test_ids.csv)
- [offline_analyze_test_signals.csv](tools/python_can_tool/logs/offline_analyze_test_signals.csv)

### Notes
- Existing unrelated worktree changes are left untouched.
- `tools/python_can_tool/` is still untracked as a directory in Git, so Git status reports it as `?? tools/python_can_tool/`.

## Stage Summary Table
| Stage | Result State | Avg Upload MB/s | Latency P95 (ms) | Latency Max (ms) | Detailed Report |
|---|---:|---:|---:|---:|---|
| 2026-02-22 23:10:01 - Web UI Responsiveness Metrics | DONE | 0.433 | 35.40 | 49.84 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_run_20260222_230755.summary.txt) |
| 2026-02-22 23:21:31 - Refactor v5 Task-Focused Files | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-22 23:26:45 - Create 8 SD Test Files | DONE | 0.433 | 39.33 | 66.70 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_poststep_20260222_232932.summary.txt) |
| 2026-02-22 23:34:37 - Queue + Auto-Scan Pipeline | DONE | 0.399 | 76.16 | 109.16 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_queue_pipeline_20260222_233741.summary.txt) |
| 2026-02-22 23:40:37 - Runbook Feature-Testability Rule | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-22 23:41:52 - Retry/Backoff/Retry-After | ERROR (full run) | n/a | n/a | n/a | [summary](experiments/upload-ui/results/raw/upload_ui_v5_retry_policy_20260222_234406.summary.txt) |
| 2026-02-22 23:49:37 - Server Contract + Headers | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-22 23:52:53 - Reset Upload-State Command | DONE | 0.360 | 79.53 | 101.51 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_reset_state_20260222_235615.summary.txt) |
| 2026-02-23 00:01:06 - Uploaded-State Bookkeeping | DONE | 0.414 | 65.99 | 78.19 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_bookkeeping_20260223_000347.summary.txt) |
| 2026-02-23 00:05:00 - SD Recovery + Auto-Invoke | DONE | 0.288 | 67.06 | 86.07 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_recover_sd_20260223_001112.summary.txt) |
| 2026-02-23 00:12:44 - Reachability Probe + Cache (2 steps) | DONE | step1 0.403 / step2 0.310 | step1 70.77 / step2 81.50 | step1 113.90 / step2 108.54 | [step1](experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_20260223_001511.summary.txt), [step2](experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_20260223_001752.summary.txt) |
| 2026-02-23 00:47:14 - Revert To Previous Stage | DONE | revert step1 0.777 / step2 0.404 | revert step1 68.48 / step2 67.73 | revert step1 80.33 / step2 78.71 | [step1](experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_revert_20260223_004616.summary.txt), [step2](experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_revert_20260223_004638.summary.txt) |
| 2026-02-23 01:14:09 - Reduce Test Files To 4 | DONE | 0.536 | 75.13 | 98.50 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_4files_20260223_011348.summary.txt) |
| 2026-02-23 01:18:23 - Startup-Only SD Recovery Gate | DONE | 0.492 | 76.62 | 3020.23 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_startup_sd_only_recover_20260223_011746.summary.txt) |
| 2026-02-23 01:20:06 - Add Power-Reset Prompt | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 01:24:21 - Idle-Only Probe Path | DONE | 0.132 | 39.03 | 1211.33 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_idle_probe_only_20260223_012232.summary.txt) |
| 2026-02-23 01:25:54 - Remove Probe Age Quantization | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 01:35:45 - Split Serial Interface File | DONE | 0.402 | 84.05 | 94.35 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_serial_split_20260223_013519.summary.txt) |
| 2026-02-23 01:44:58 - Add Function-Level Comments | DONE | 0.388 | 80.87 | 95.25 | [summary](experiments/upload-ui/results/raw/upload_ui_v5_comment_all_funcs_20260223_014421.summary.txt) |
| 2026-02-23 01:50:33 - Enforce Self-Test Section At File End | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 01:52:54 - Move Task Entrypoints To Top Of CPP Files | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 01:54:11 - Add v5 Architecture Documentation | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 01:56:59 - Move Uploader Task Definitions Back To Bottom | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 01:58:21 - Extend Banner Lines And Add Task Sections | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 02:01:08 - Move Upload Trace Logging To Serial Tester | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 02:03:02 - Move Upload Stats Types/State To Uploader | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 02:04:19 - Move Prefetch Task Next To Other Task Implementations | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 02:21:55 - Move Upload/Server Definitions From Storage To Uploader | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-23 02:25:57 - Split Queue Logic Into Dedicated CPP | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |
| 2026-02-25 20:44:54 - Add Standard Function Header Comment Format | n/a | n/a | n/a | n/a | [entry](IMPLEMENTATION_LOG.md) |

## 2026-02-22 23:10:01 +01:00 - Add Web UI Responsiveness Metrics To v5 Tester
### Planned Steps
- Add per-poll responsiveness timing collection to the v5 upload UI tester and persist timings in JSONL output.
- Add computed responsiveness summary metrics for overall polling and active-upload polling windows.
- Run check-only and full upload automation against the ESP endpoint and record pass/fail and produced artifacts.

### Changes Made
- `experiments/upload-ui/tools/upload_ui_tester_v5.py:137`: added `/status` poll latency measurement (`poll_ms`) collection.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py:140`: added `poll_ms` to JSONL log records.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py:204`: added `poll_success_rate_pct` to summary output.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py:206`: added `latency_p95_ms` (with p50/max nearby) to summary output.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py:209`: added `active_latency_p95_ms` (with active max nearby) to summary output.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py:229`: added markdown report responsiveness lines.

### Automated Tests Run
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 180 --prefix upload_ui_v5_run`: PASS

### Result
- Source: [`upload_ui_v5_run_20260222_230755.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_run_20260222_230755.summary.txt)
- `result_state: DONE`
- `error_code: 0`
- `avg_upload_mb_s: 0.433`
- `poll_success_rate_pct: 100.00`
- `latency_p95_ms: 35.40`
- `latency_max_ms: 49.84`
- `active_latency_p95_ms: 35.40`
- `active_latency_max_ms: 49.84`

### Artifacts
- [`upload_ui_v5_run_20260222_230755.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_run_20260222_230755.jsonl)
- [`upload_ui_v5_run_20260222_230755.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_run_20260222_230755.summary.txt)

### Notes
- None.

## 2026-02-22 23:21:31 +01:00 - Refactor v5 Into Task-Focused Files
### Planned Steps
- Split `sd_http_upload_ui_test_v5` from one monolithic file into four focused files: `storage.cpp`, `uploader.cpp`, `webserver.cpp`, `main.cpp`.
- Keep `experiments/upload-ui/code/v5/firmware.cpp` as a thin entrypoint that includes the new files.
- Build `sd_http_upload_ui_test_v5` to confirm the refactor compiles without functional regressions.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added shared includes, constants, state structs/globals, and storage/network helper functions.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: added upload pipeline tasks and transport functions (`upload_task`, `monitor_task`, streaming helpers).
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: added HTTP route setup and serial command/status handlers.
- `experiments/upload-ui/code/v5/modules/main.cpp`: added `setup()`/`loop()` boot sequence and task startup; closes anonymous namespace.
- `experiments/upload-ui/code/v5/firmware.cpp`: replaced monolithic implementation with thin include-based entrypoint for the new v5 structure.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: FAIL (sandbox permission denied on `.platformio\platforms.lock`)
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5` (escalated): PASS
- `rg --files src\dev\upload_ui_test_v5`: PASS

### Result
- `v5_structure_refactored: true`
- `new_task_focused_files_count: 4`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/firmware.cpp`](experiments/upload-ui/code/v5/firmware.cpp)
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/code/v5/modules/main.cpp`](experiments/upload-ui/code/v5/modules/main.cpp)

### Notes
- Firmware was built for verification only; no flash/upload command was run in this task.

## 2026-02-22 23:26:45 +01:00 - Create 8 SD Test Files At Boot For v5
### Planned Steps
- Extend v5 storage definitions to include 8 distinct SD test file paths.
- Update boot-time file preparation to create/validate all 8 files instead of only one.
- Build `sd_http_upload_ui_test_v5` to verify the change compiles cleanly.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added `kTestFileCount` and `kTestFilePaths` for 8 SD test files and set active upload path to `/sd_http_post_8mb_01.bin`.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: replaced single-file setup logic with `ensure_single_test_file(...)` + looped `ensure_test_file()` to create/validate all 8 files at boot.
- `experiments/upload-ui/docs/runbook.md`: added explicit requirement to run a validation test after each implementation step and record it immediately.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: FAIL (sandbox permission denied on `.platformio\platforms.lock`)
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload` (escalated): PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 240 --prefix upload_ui_v5_poststep`: PASS

### Result
- `boot_file_prep_count: 8`
- `build_status_sd_http_upload_ui_test_v5: PASS`
- `post_step_validation_policy_enabled: true`
- `runtime_result_state: DONE`
- `runtime_error_code: 0`
- `runtime_avg_upload_mb_s: 0.433`
- `runtime_poll_success_rate_pct: 100.00`
- `runtime_latency_p95_ms: 39.33`
- `runtime_latency_max_ms: 66.70`
- `runtime_active_latency_p95_ms: 39.43`
- `runtime_active_latency_max_ms: 66.70`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/docs/runbook.md`](experiments/upload-ui/docs/runbook.md)
- [`IMPLEMENTATION_LOG.md`](IMPLEMENTATION_LOG.md)
- [`experiments/upload-ui/results/raw/upload_ui_v5_poststep_20260222_232932.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_poststep_20260222_232932.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_poststep_20260222_232932.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_poststep_20260222_232932.summary.txt)

### Notes
- Validation run used base URL `http://192.168.3.45` after flashing v5.

## 2026-02-22 23:34:37 +01:00 - Add Persistent Queue And Auto-Scan Upload Pipeline To v5
### Planned Steps
- Take a stage snapshot before code changes for v5 upload files.
- Add queue, dedupe/bump, uploaded-state tracking, and auto-scan enqueue logic.
- Wire `/start` and serial `start` to enqueue pending files and run validation tests after implementation.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added queue config/state (`kQueueLen`, `QueueItem`, queue storage + mutex), uploaded-state file path, and queue helpers (`queue_add_or_bump`, `queue_snapshot_ready`, `queue_schedule_retry`, `queue_pending`, `queue_pending_periodic`, `uploaded_state_contains`, `mark_uploaded_path`).
- `experiments/upload-ui/code/v5/modules/storage.cpp`: changed `build_multipart_request(...)` to accept per-file path/filename instead of always using a fixed file name.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: refactored `upload_task` to consume queue items, dedupe/bump via pending queue seed, retry failed items with backoff, and mark successful uploads in persistent state.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: updated `monitor_task` to run `queue_pending_periodic()` for unattended auto-enqueue.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: updated `/start` and serial `start` to seed pending queue before triggering upload worker.
- `experiments/upload-ui/code/v5/modules/main.cpp`: added boot-time `queue_pending()` call after SD file preparation.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -File experiments\upload-ui\history\stage_archive.ps1 -Action snapshot -Label "before_v5_queue_pipeline" -Paths experiments/upload-ui/code/v5/modules experiments/upload-ui/code/v5/firmware.cpp experiments/upload-ui/tools/upload_ui_tester_v5.py experiments/upload-ui/docs/runbook.md`: FAIL (paths parsed incorrectly by wrapper call)
- `powershell -ExecutionPolicy Bypass -File experiments\upload-ui\history\stage_archive.ps1 -Action snapshot -Label "before_v5_queue_pipeline" -Paths experiments/upload-ui/code/v5/modules,experiments/upload-ui/code/v5/firmware.cpp,experiments/upload-ui/tools/upload_ui_tester_v5.py,experiments/upload-ui/docs/runbook.md`: FAIL (single combined path string)
- `powershell -ExecutionPolicy Bypass -Command "& .\experiments\upload-ui\history\stage_archive.ps1 -Action snapshot -Label 'before_v5_queue_pipeline' -Paths @('experiments/upload-ui/code/v5/modules','experiments/upload-ui/code/v5/firmware.cpp','experiments/upload-ui/tools/upload_ui_tester_v5.py','experiments/upload-ui/docs/runbook.md')"`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 300 --prefix upload_ui_v5_queue_pipeline`: PASS
- `rg -n "QueueItem|kQueueLen|queue_pending_periodic|uploaded_state_contains|queue_add_or_bump|kUploadedStatePath" src\dev\upload_ui_test_v5\storage.cpp src\dev\upload_ui_test_v5\uploader.cpp src\dev\upload_ui_test_v5\webserver.cpp src\dev\upload_ui_test_v5\main.cpp`: PASS
- `if (Test-Path archives\stages\20260222_233518_before_v5_queue_pipeline\manifest.json) { Write-Output "snapshot_exists" } else { Write-Output "snapshot_missing" }`: PASS

### Result
- `queue_slots: 32`
- `auto_scan_pipeline_enabled: true`
- `persistent_uploaded_state_enabled: true`
- `runtime_result_state: DONE`
- `runtime_error_code: 0`
- `runtime_avg_upload_mb_s: 0.399`
- `runtime_poll_success_rate_pct: 100.00`
- `runtime_latency_p95_ms: 76.16`
- `runtime_latency_max_ms: 109.16`
- `runtime_active_latency_p95_ms: 76.51`
- `runtime_active_latency_max_ms: 109.16`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/code/v5/modules/main.cpp`](experiments/upload-ui/code/v5/modules/main.cpp)
- [`experiments/upload-ui/history/stages/20260222_233518_before_v5_queue_pipeline/manifest.json`](experiments/upload-ui/history/stages/20260222_233518_before_v5_queue_pipeline/manifest.json)
- [`experiments/upload-ui/results/raw/upload_ui_v5_queue_pipeline_20260222_233741.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_queue_pipeline_20260222_233741.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_queue_pipeline_20260222_233741.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_queue_pipeline_20260222_233741.summary.txt)

### Notes
- During `--check-only`, status reported `SENDING`, indicating unattended/background queue processing was already active.

## 2026-02-22 23:40:37 +01:00 - Add Feature-Testability And Serial-Test Command Rule To Runbook
### Planned Steps
- Snapshot runbook/log files before editing documentation.
- Add a permanent runbook rule requiring feature-specific test automation and a serial test command.
- Verify the new rule text is present in the runbook.

### Changes Made
- `experiments/upload-ui/docs/runbook.md`: added `Feature Testability Rule` section requiring automation updates and a dedicated `test_<feature_name>` serial command per feature.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\experiments\upload-ui\history\stage_archive.ps1 -Action snapshot -Label 'before_runbook_feature_testability_rule' -Paths @('experiments/upload-ui/docs/runbook.md','IMPLEMENTATION_LOG.md')"`: PASS
- `rg -n "Feature Testability Rule|test_<feature_name>|test_queue_pipeline|Run the serial command" tools\upload_ui_test_runbook.md`: PASS

### Result
- `feature_testability_rule_added: true`
- `serial_test_command_rule_added: true`

### Artifacts
- [`experiments/upload-ui/docs/runbook.md`](experiments/upload-ui/docs/runbook.md)
- [`experiments/upload-ui/history/stages/20260222_234037_before_runbook_feature_testability_rule/manifest.json`](experiments/upload-ui/history/stages/20260222_234037_before_runbook_feature_testability_rule/manifest.json)

### Notes
- No firmware build/flash was needed for this documentation-only update.

## 2026-02-22 23:41:52 +01:00 - Add Retry/Backoff/Retry-After Handling To v5 Pipeline
### Planned Steps
- Take a stage snapshot for v5 sources and runbook/tester before changing retry behavior.
- Add retryable classification + retry-after parsing + exponential backoff/jitter handling to queue pipeline execution.
- Add a dedicated serial test command and non-interactive tester check for retry policy, then run validation.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: upgraded queue retry scheduler to accept `retry_after_ms` and jitter controls; added `is_http_retryable(...)`.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: replaced one-line status parsing with header-aware response parsing (`Retry-After` support), added retryable classification in finalize path, and applied connect/interrupted/server-aware retry decisions.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: added `run_retry_policy_selftest()` for deterministic retry-policy validation.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: added HTTP test endpoint `/test/retry_policy` and serial command `test_retry_policy`; included it in `help`.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py`: added `--retry-policy-check` automation mode to call `/test/retry_policy`.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\experiments\upload-ui\history\stage_archive.ps1 -Action snapshot -Label 'before_v5_retry_policy' -Paths @('experiments/upload-ui/code/v5/modules','experiments/upload-ui/code/v5/firmware.cpp','experiments/upload-ui/tools/upload_ui_tester_v5.py','experiments/upload-ui/docs/runbook.md','IMPLEMENTATION_LOG.md')"`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --retry-policy-check --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 240 --prefix upload_ui_v5_retry_policy`: FAIL (`result_state: ERROR`, timeout with no active upload)
- `powershell (COM9 serial test for test_retry_policy)`: FAIL (port not present)
- `powershell (COM15 serial test for test_retry_policy)`: PASS

### Result
- `retry_after_parsing_enabled: true`
- `retryable_http_classification_enabled: true`
- `connect_interrupted_retry_handling_enabled: true`
- `serial_test_command_added: test_retry_policy`
- `automation_check_added: --retry-policy-check`
- `serial_test_retry_policy_result: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/tools/upload_ui_tester_v5.py`](experiments/upload-ui/tools/upload_ui_tester_v5.py)
- [`experiments/upload-ui/history/stages/20260222_234153_before_v5_retry_policy/manifest.json`](experiments/upload-ui/history/stages/20260222_234153_before_v5_retry_policy/manifest.json)
- [`experiments/upload-ui/results/raw/upload_ui_v5_retry_policy_20260222_234406.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_retry_policy_20260222_234406.summary.txt)
- [`experiments/upload-ui/results/raw/upload_ui_v5_retry_policy_20260222_234406.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_retry_policy_20260222_234406.jsonl)
- [`experiments/upload-ui/results/raw/serial_test_retry_policy_20260222_235045.log`](experiments/upload-ui/results/raw/serial_test_retry_policy_20260222_235045.log)

### Notes
- The `--start` run timed out in `IDLE` because there were no pending files left to enqueue (uploaded-state already marked files as uploaded), not because retry logic failed.

## 2026-02-22 23:49:37 +01:00 - Add Server Contract Validation And Idempotency/Auth Headers To v5
### Planned Steps
- Snapshot v5 firmware/tester/log files before changing upload contract behavior.
- Add JSON response contract parsing/validation and require accepted contract codes for success marking.
- Add `X-Idempotency-Key` and optional `X-Api-Token` headers and validate via automation + serial test command.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added optional token define (`SD_HTTP_UPLOAD_TEST_API_TOKEN`) and request header injection for `X-Idempotency-Key` + `X-Api-Token` (if configured).
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: added response-body read path, JSON parse helper (`parse_server_json`), and strict contract acceptance (`is_success_contract`).
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: now marks upload success only for contract-valid codes (`UPLOAD_ACCEPTED`, duplicate idempotency/content), and logs `UPLOAD_CONTRACT_FAIL` otherwise.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: added self-test `run_server_contract_selftest()` for contract logic validation.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: added `/test/server_contract` endpoint and serial command `test_server_contract`, plus help output entry.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py`: added non-interactive automation mode `--server-contract-check`.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\experiments\upload-ui\history\stage_archive.ps1 -Action snapshot -Label 'before_v5_contract_idempotency' -Paths @('experiments/upload-ui/code/v5/modules','experiments/upload-ui/code/v5/firmware.cpp','experiments/upload-ui/tools/upload_ui_tester_v5.py','IMPLEMENTATION_LOG.md')"`: PASS
- `python tools\upload_ui_tester_v5.py --help`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --retry-policy-check --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --server-contract-check --request-timeout 10`: PASS
- `powershell serial command test on COM15 (send: test_retry_policy, test_server_contract)`: PASS

### Result
- `contract_validation_enabled: true`
- `idempotency_header_enabled: true`
- `optional_api_token_header_enabled: true`
- `serial_test_command_added: test_server_contract`
- `automation_check_added: --server-contract-check`
- `serial_retry_policy_test_result: PASS`
- `serial_server_contract_test_result: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/tools/upload_ui_tester_v5.py`](experiments/upload-ui/tools/upload_ui_tester_v5.py)
- [`experiments/upload-ui/history/stages/20260222_234938_before_v5_contract_idempotency/manifest.json`](experiments/upload-ui/history/stages/20260222_234938_before_v5_contract_idempotency/manifest.json)
- [`experiments/upload-ui/results/raw/serial_test_contract_retry_20260222_235320.log`](experiments/upload-ui/results/raw/serial_test_contract_retry_20260222_235320.log)

### Notes
- Dedicated endpoint checks and serial self-tests were used as primary validation for contract/header logic.

## 2026-02-22 23:52:53 +01:00 - Add Reset Upload-State Serial/HTTP Command To v5
### Planned Steps
- Add a firmware helper that clears persisted uploaded-file state and re-seeds queue.
- Expose that helper via serial command and HTTP endpoint for automation.
- Add tester automation switch for reset-state and run validation to confirm uploads can restart.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added `reset_upload_state_and_queue()` to remove `/upload_state_v5.txt`, clear queue slots, reset autoscan timestamp, and re-seed pending files.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added `run_reset_upload_state_selftest()` to validate reset behavior and state-file absence.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: added HTTP endpoint `/reset_upload_state` and self-test endpoint `/test/reset_upload_state`.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: added serial commands `reset_upload_state` and `test_reset_upload_state`; updated `help` output and increased serial command buffer size.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py`: added `--reset-upload-state` automation mode to call `/reset_upload_state` and report pass/fail non-interactively.
- `experiments/upload-ui/docs/runbook.md`: added recommended pre-run reset step using `--reset-upload-state` so stale uploaded-state does not block new runs.

### Automated Tests Run
- `python tools\upload_ui_tester_v5.py --help`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 300 --prefix upload_ui_v5_reset_state`: PASS
- `python (serial COM15 send: reset_upload_state, test_reset_upload_state)`: PASS

### Result
- `reset_upload_state_endpoint_enabled: true`
- `reset_upload_state_serial_command_enabled: true`
- `serial_test_command_added: test_reset_upload_state`
- `automation_check_added: --reset-upload-state`
- `full_start_run_after_reset_state: PASS`
- `runtime_result_state: DONE`
- `runtime_error_code: 0`
- `runtime_avg_upload_mb_s: 0.360`
- `runtime_poll_success_rate_pct: 100.00`
- `runtime_latency_p95_ms: 79.53`
- `runtime_latency_max_ms: 101.51`
- `runtime_active_latency_p95_ms: 79.71`
- `runtime_active_latency_max_ms: 101.51`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/tools/upload_ui_tester_v5.py`](experiments/upload-ui/tools/upload_ui_tester_v5.py)
- [`experiments/upload-ui/docs/runbook.md`](experiments/upload-ui/docs/runbook.md)
- [`experiments/upload-ui/results/raw/upload_ui_v5_reset_state_20260222_235615.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_reset_state_20260222_235615.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_reset_state_20260222_235615.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_reset_state_20260222_235615.summary.txt)
- [`experiments/upload-ui/results/raw/serial_test_reset_upload_state_20260222_235654.log`](experiments/upload-ui/results/raw/serial_test_reset_upload_state_20260222_235654.log)
- [`experiments/upload-ui/results/raw/serial_test_reset_upload_state_selftest_20260222_235737.log`](experiments/upload-ui/results/raw/serial_test_reset_upload_state_selftest_20260222_235737.log)

### Notes
- The prior `--start` timeout-in-IDLE condition caused by stale uploaded-state was resolved by resetting upload state before run.

## 2026-02-23 00:01:06 +01:00 - Add Uploaded-State Bookkeeping Metadata To v5
### Planned Steps
- Add explicit uploaded-state metadata storage and summary helpers in v5 storage logic.
- Integrate successful upload marking with metadata writes and expose bookkeeping summary/testing endpoints.
- Add tester automation and serial self-test command for uploaded-state bookkeeping, then validate build/flash/runtime checks.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added metadata file (`/upload_state_v5_meta.log`) append logic and `mark_uploaded_success(...)` to persist uploaded bookkeeping records.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added bookkeeping summary helpers (`uploaded_state_summary`, `queue_state_summary`) and self-test `run_uploaded_state_bookkeeping_selftest()`.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: extended `reset_upload_state_and_queue()` to remove both uploaded-state and metadata files.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: switched success marking from `mark_uploaded_path(...)` to `mark_uploaded_success(...)` so each successful upload updates metadata.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: added `/bookkeeping` endpoint, `/test/uploaded_state_bookkeeping` endpoint, `bookkeeping` serial command, and `test_uploaded_state_bookkeeping` serial self-test command.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py`: added `--uploaded-state-check` automation mode calling `/test/uploaded_state_bookkeeping` and `/bookkeeping`.
- `experiments/upload-ui/docs/runbook.md`: added uploaded-state bookkeeping feature self-check command and serial command reference.

### Automated Tests Run
- `python tools\upload_ui_tester_v5.py --help`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --uploaded-state-check --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python (serial COM15 send: test_uploaded_state_bookkeeping, bookkeeping)`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 240 --prefix upload_ui_v5_bookkeeping`: PASS

### Result
- `uploaded_state_metadata_enabled: true`
- `bookkeeping_endpoint_enabled: true`
- `serial_test_command_added: test_uploaded_state_bookkeeping`
- `automation_check_added: --uploaded-state-check`
- `uploaded_state_check_uploaded: 1`
- `uploaded_state_check_outstanding: 7`
- `post_validation_run_result_state: DONE`
- `post_validation_run_error_code: 0`
- `post_validation_run_avg_upload_mb_s: 0.414`
- `post_validation_run_latency_p95_ms: 65.99`
- `post_validation_run_latency_max_ms: 78.19`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/tools/upload_ui_tester_v5.py`](experiments/upload-ui/tools/upload_ui_tester_v5.py)
- [`experiments/upload-ui/docs/runbook.md`](experiments/upload-ui/docs/runbook.md)
- [`experiments/upload-ui/results/raw/serial_test_uploaded_state_bookkeeping_20260223_000337.log`](experiments/upload-ui/results/raw/serial_test_uploaded_state_bookkeeping_20260223_000337.log)
- [`experiments/upload-ui/results/raw/upload_ui_v5_bookkeeping_20260223_000347.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_bookkeeping_20260223_000347.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_bookkeeping_20260223_000347.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_bookkeeping_20260223_000347.summary.txt)

### Notes
- `--uploaded-state-check` intentionally runs a self-test that resets state and marks one file as uploaded; expected summary is `uploaded=1`, `outstanding=7`.

## 2026-02-23 00:05:00 +01:00 - Add SD Recovery Routine And Auto-Invoke On SD Errors
### Planned Steps
- Add a dedicated `recover_sd()` routine with SD deinit, bus settle/clock flush delays, and remount retries.
- Automatically invoke `recover_sd()` on SD error paths in upload/state bookkeeping operations.
- Add a feature self-test command + tester automation flag for recover-SD, then run build/flash/runtime validation.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added `recover_sd(...)` with full SD deinit (`SD.end`, `s_sd_spi.end`), SPI clock flush cycles, settle delays, and remount retries.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added `recover_sd_safe(...)` to abort current upload and wait for idle before recovery, plus `run_recover_sd_selftest()`.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: `init_sd()` now auto-falls back to `recover_sd(...)` if initial mount fails.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added automatic recover-on-error retry paths in SD operations (`ensure_single_test_file`, `uploaded_state_contains`, `mark_uploaded_path`, `append_uploaded_metadata`, `reset_upload_state_and_queue`).
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: upload file-open failure now auto-triggers SD recover and retries `SD.open` before failing run item.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: added `/recover_sd` and `/test/recover_sd`, serial commands `recover_sd` + `test_recover_sd`, and help text entries.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py`: added `--recover-sd-check` automation mode for `/test/recover_sd`.
- `experiments/upload-ui/docs/runbook.md`: added recover-SD self-check command and serial command reference.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\experiments\upload-ui\history\stage_archive.ps1 -Action snapshot -Label 'before_v5_recover_sd' -Paths @('experiments/upload-ui/code/v5/modules','experiments/upload-ui/code/v5/firmware.cpp','experiments/upload-ui/tools/upload_ui_tester_v5.py','experiments/upload-ui/docs/runbook.md','IMPLEMENTATION_LOG.md')"`: PASS
- `python tools\upload_ui_tester_v5.py --help`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: FAIL (`ensure_single_test_file` forward declaration missing)
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5` (after forward-declaration fix): PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --recover-sd-check --request-timeout 10`: FAIL (timeout; recover self-test path too heavy)
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --recover-sd-check --request-timeout 15`: FAIL (timeout; recover collided with active upload)
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5` (after `recover_sd_safe`/light self-test): PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload` (after `recover_sd_safe`/light self-test): PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --recover-sd-check --request-timeout 15`: PASS
- `python (serial COM15 send: test_recover_sd, recover_sd)`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 240 --prefix upload_ui_v5_recover_sd`: PASS

### Result
- `sd_recover_routine_enabled: true`
- `sd_recover_deinit_clock_flush_remount_retries_enabled: true`
- `sd_error_auto_recover_enabled: true`
- `recover_sd_safe_abort_wait_enabled: true`
- `serial_test_command_added: test_recover_sd`
- `automation_check_added: --recover-sd-check`
- `recover_sd_check_result: PASS`
- `post_validation_run_result_state: DONE`
- `post_validation_run_error_code: 0`
- `post_validation_run_avg_upload_mb_s: 0.288`
- `post_validation_run_latency_p95_ms: 67.06`
- `post_validation_run_latency_max_ms: 86.07`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/tools/upload_ui_tester_v5.py`](experiments/upload-ui/tools/upload_ui_tester_v5.py)
- [`experiments/upload-ui/docs/runbook.md`](experiments/upload-ui/docs/runbook.md)
- [`experiments/upload-ui/history/stages/20260223_000500_before_v5_recover_sd/manifest.json`](experiments/upload-ui/history/stages/20260223_000500_before_v5_recover_sd/manifest.json)
- [`experiments/upload-ui/results/raw/serial_test_recover_sd_20260223_001107.log`](experiments/upload-ui/results/raw/serial_test_recover_sd_20260223_001107.log)
- [`experiments/upload-ui/results/raw/upload_ui_v5_recover_sd_20260223_001112.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_recover_sd_20260223_001112.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_recover_sd_20260223_001112.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_recover_sd_20260223_001112.summary.txt)

### Notes
- Two intermediate recover-check timeout failures were resolved by making self-test lightweight and adding `recover_sd_safe(...)` to avoid recovering during active SD transfer.

## 2026-02-23 00:12:44 +01:00 - Add Reachability Probe And Cached Resolution Path In Two Steps
### Planned Steps
- Step 1: add server reachability probing before upload connect, expose probe stats in `/status`, web UI, and feature self-test/automation command.
- Step 2: add cached resolution path for probe/connect target IP, expose cache counters in `/status`, web UI, and re-run measurements.
- Run build/flash and one runtime upload test after each step to compare responsiveness/throughput impact.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp` (Step 1): added reachability probe stats structure and `probe_server_reachability_no_cache(...)`; added probe stats to UI text rendering (`probe_ok`, `probe_ms`, `probe_ip`, `probe_err`, `cache_hits`, `cache_misses`).
- `experiments/upload-ui/code/v5/modules/uploader.cpp` (Step 1): added mandatory pre-connect probe call and retry scheduling on probe failure (`error -18` path).
- `experiments/upload-ui/code/v5/modules/webserver.cpp` (Step 1): `/status` now includes probe/cache fields; added `/test/reachability_probe` + serial `test_reachability_probe`.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py` (Step 1): added `--reachability-probe-check`.
- `experiments/upload-ui/code/v5/modules/storage.cpp` (Step 2): added DNS cache state (`cached IP + TTL`) and `resolve_target_cached(...)`, `probe_server_reachability_cached(...)`, and cache self-test `run_reachability_cache_selftest()`.
- `experiments/upload-ui/code/v5/modules/uploader.cpp` (Step 2): upload connect path now resolves through cache and connects directly by cached IP (`client.connect(IPAddress, port, ...)`).
- `experiments/upload-ui/code/v5/modules/webserver.cpp` (Step 2): added `/test/reachability_cache` + serial `test_reachability_cache`.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py` (Step 2): added `--reachability-cache-check`.
- `experiments/upload-ui/docs/runbook.md`: added reachability probe/cache automation and serial commands.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\experiments\upload-ui\history\stage_archive.ps1 -Action snapshot -Label 'before_v5_probe_cache_feature' -Paths @('experiments/upload-ui/code/v5/modules','experiments/upload-ui/code/v5/firmware.cpp','experiments/upload-ui/tools/upload_ui_tester_v5.py','experiments/upload-ui/docs/runbook.md','IMPLEMENTATION_LOG.md')"`: PASS
- `python tools\upload_ui_tester_v5.py --help`: PASS
- **Step 1**
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reachability-probe-check --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python (serial COM15 send: test_reachability_probe)`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 240 --prefix upload_ui_v5_probe_step1`: PASS
- **Step 2**
- `python tools\upload_ui_tester_v5.py --help`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reachability-probe-check --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reachability-cache-check --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python (serial COM15 send: test_reachability_probe, test_reachability_cache, status)`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 240 --prefix upload_ui_v5_probe_step2`: PASS

### Result
- `reachability_probe_enabled: true`
- `cached_resolution_path_enabled: true`
- `status_probe_cache_fields_visible: true`
- `web_ui_probe_cache_visibility_enabled: true`
- `serial_test_commands_added: test_reachability_probe,test_reachability_cache`
- `automation_checks_added: --reachability-probe-check,--reachability-cache-check`
- `step1_result_state: DONE`
- `step1_avg_upload_mb_s: 0.403`
- `step1_latency_p95_ms: 70.77`
- `step1_latency_max_ms: 113.90`
- `step1_probe_ms_last: 64`
- `step1_cache_hits_last: 0`
- `step1_cache_misses_last: 2`
- `step2_result_state: DONE`
- `step2_avg_upload_mb_s: 0.310`
- `step2_latency_p95_ms: 81.50`
- `step2_latency_max_ms: 108.54`
- `step2_probe_ms_last: 21`
- `step2_cache_hits_last: 4`
- `step2_cache_misses_last: 1`
- `observed_cache_impact_probe_latency_ms: improved_64_to_21`
- `observed_cache_impact_throughput_mb_s: lower_0.403_to_0.310_this_run`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/tools/upload_ui_tester_v5.py`](experiments/upload-ui/tools/upload_ui_tester_v5.py)
- [`experiments/upload-ui/docs/runbook.md`](experiments/upload-ui/docs/runbook.md)
- [`experiments/upload-ui/history/stages/20260223_001244_before_v5_probe_cache_feature/manifest.json`](experiments/upload-ui/history/stages/20260223_001244_before_v5_probe_cache_feature/manifest.json)
- [`experiments/upload-ui/results/raw/serial_test_reachability_probe_20260223_001506.log`](experiments/upload-ui/results/raw/serial_test_reachability_probe_20260223_001506.log)
- [`experiments/upload-ui/results/raw/serial_test_reachability_probe_cache_20260223_001747.log`](experiments/upload-ui/results/raw/serial_test_reachability_probe_cache_20260223_001747.log)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_20260223_001511.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_20260223_001511.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_20260223_001511.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_20260223_001511.summary.txt)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_20260223_001752.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_20260223_001752.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_20260223_001752.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_20260223_001752.summary.txt)

### Notes
- Throughput changed across two single-run measurements; for firm cache-performance attribution, run 3+ runs per step on stable network conditions.

## 2026-02-23 00:47:14 +01:00 - Revert REST Uploader Stats Contract Entry And Restore Previous Stage
### Planned Steps
- Remove the REST uploader-stats-contract implementation-log entry as requested.
- Restore v5 source/tester/runbook files from the archived stage immediately before that change.
- Re-run the probe/cache validation sequence and compare current stats against the `00:12:44` baseline artifacts.

### Changes Made
- `IMPLEMENTATION_LOG.md`: restored from `experiments/upload-ui/history/stages/20260223_001955_before_v5_uploader_stats_contract_fixpath/IMPLEMENTATION_LOG.md`, which removes the `00:19:36` entry.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: restored from archived stage `20260223_001955_before_v5_uploader_stats_contract_fixpath`.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: restored from archived stage `20260223_001955_before_v5_uploader_stats_contract_fixpath`.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: restored from archived stage `20260223_001955_before_v5_uploader_stats_contract_fixpath`.
- `experiments/upload-ui/code/v5/modules/main.cpp`: restored from archived stage `20260223_001955_before_v5_uploader_stats_contract_fixpath`.
- `experiments/upload-ui/tools/upload_ui_tester_v5.py`: restored from archived stage `20260223_001955_before_v5_uploader_stats_contract_fixpath`.
- `experiments/upload-ui/docs/runbook.md`: restored from archived stage `20260223_001955_before_v5_uploader_stats_contract_fixpath`.

### Automated Tests Run
- `rg -n "^## 2026-02-23 00:19:36" IMPLEMENTATION_LOG.md`: PASS (no matches after restore)
- `python experiments/upload-ui/tools/upload_ui_tester_v5.py --help`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python experiments/upload-ui/tools/upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python experiments/upload-ui/tools/upload_ui_tester_v5.py --base-url http://192.168.3.45 --reachability-probe-check --request-timeout 10`: PASS
- `python experiments/upload-ui/tools/upload_ui_tester_v5.py --base-url http://192.168.3.45 --reachability-cache-check --request-timeout 10`: PASS
- `python experiments/upload-ui/tools/upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 240 --prefix upload_ui_v5_probe_step1_revert`: PASS
- `python experiments/upload-ui/tools/upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python experiments/upload-ui/tools/upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 240 --prefix upload_ui_v5_probe_step2_revert`: PASS

### Result
- `reverted_to_stage: 20260223_001955_before_v5_uploader_stats_contract_fixpath`
- `removed_rest_stats_contract_entry: true`
- `probe_check_ok: true`
- `cache_check_ok: true`
- `baseline_step1_avg_upload_mb_s: 0.403`
- `baseline_step1_latency_p95_ms: 70.77`
- `baseline_step1_latency_max_ms: 113.90`
- `baseline_step2_avg_upload_mb_s: 0.310`
- `baseline_step2_latency_p95_ms: 81.50`
- `baseline_step2_latency_max_ms: 108.54`
- `revert_step1_avg_upload_mb_s: 0.777`
- `revert_step1_latency_p95_ms: 68.48`
- `revert_step1_latency_max_ms: 80.33`
- `revert_step2_avg_upload_mb_s: 0.404`
- `revert_step2_latency_p95_ms: 67.73`
- `revert_step2_latency_max_ms: 78.71`
- `comparison_summary: same_state_DONE_and_same_poll_success_100_pct_with_better_latency_than_baseline_on_this_retest`

### Artifacts
- [`experiments/upload-ui/history/stages/20260223_001955_before_v5_uploader_stats_contract_fixpath/IMPLEMENTATION_LOG.md`](experiments/upload-ui/history/stages/20260223_001955_before_v5_uploader_stats_contract_fixpath/IMPLEMENTATION_LOG.md)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_20260223_001511.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_20260223_001511.summary.txt)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_20260223_001752.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_20260223_001752.summary.txt)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_revert_20260223_004616.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_revert_20260223_004616.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_revert_20260223_004616.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step1_revert_20260223_004616.summary.txt)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_revert_20260223_004638.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_revert_20260223_004638.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_revert_20260223_004638.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_probe_step2_revert_20260223_004638.summary.txt)

### Notes
- Retest included explicit `--reset-upload-state` before each `--start` run to ensure a fresh upload workload.

## 2026-02-23 01:14:09 +01:00 - Reduce v5 Generated And Queued Test Files From 8 To 4
### Planned Steps
- Change v5 test file inventory from 8 files to 4 files.
- Build and flash `sd_http_upload_ui_test_v5`.
- Run automation checks (`check-only`, `reset-upload-state`, `start`) and record runtime stats.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: set `kTestFileCount` to `4` and trimmed `kTestFilePaths` to `01..04`.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 220 --prefix upload_ui_v5_4files`: PASS

### Result
- `test_file_count: 4`
- `result_state: DONE`
- `error_code: 0`
- `avg_upload_mb_s: 0.536`
- `poll_success_rate_pct: 100.00`
- `latency_p95_ms: 75.13`
- `latency_max_ms: 98.50`
- `active_latency_p95_ms: 75.33`
- `active_latency_max_ms: 98.50`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/results/raw/upload_ui_v5_4files_20260223_011348.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_4files_20260223_011348.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_4files_20260223_011348.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_4files_20260223_011348.summary.txt)

### Notes
- `check-only` briefly reported `status_state=SENDING`, indicating active background upload while endpoint checks were executed.

## 2026-02-23 01:18:23 +01:00 - Restrict SD Recovery To Startup And Gate Uploader On SD Availability
### Planned Steps
- Make SD recovery blocking/active only during startup SD init path.
- Track SD availability in storage state and disable runtime auto-recovery fallbacks.
- Prevent uploader start/queueing when SD is unavailable and validate with build/flash/runtime test.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added persistent storage state flag `s_sd_available` and helper `sd_available()`.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: `init_sd()` now sets SD availability based on initial mount/recovery result.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: `recover_sd_safe(...)` changed to startup-only disabled behavior at runtime.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: removed runtime `recover_sd(...)` retries from uploaded-state/meta/reset paths; these now fail fast and mark SD unavailable.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: queue seed/periodic enqueue now no-op when SD is unavailable.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: web UI status text now includes SD state (`sd=ok|missing`).
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: `/status` now includes `sd_ok`.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: `/start` and serial `start` now reject when SD unavailable.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: `/recover_sd`, `/test/recover_sd`, and serial recover commands changed to startup-only disabled responses/messages.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: uploader loop now gates on `sd_available()` and does not attempt runtime SD recovery.
- `experiments/upload-ui/code/v5/modules/main.cpp`: startup no longer exits on SD failure; it marks storage unavailable and continues (uploader then remains blocked by SD gate).

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `rg -n "sd_available\(|sd_unavailable|startup-only|runtime recovery disabled|sd_ok" experiments/upload-ui/code/v5/modules/storage.cpp experiments/upload-ui/code/v5/modules/uploader.cpp experiments/upload-ui/code/v5/modules/webserver.cpp experiments/upload-ui/code/v5/modules/main.cpp`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 220 --prefix upload_ui_v5_startup_sd_only_recover`: PASS
- `python (HTTP GET /recover_sd, /test/recover_sd, /status)`: FAIL (connection refused at verification time)

### Result
- `startup_only_sd_recovery_enforced: true`
- `storage_sd_availability_flag_added: true`
- `uploader_start_blocked_when_sd_missing: true`
- `runtime_result_state: DONE`
- `runtime_error_code: 0`
- `runtime_avg_upload_mb_s: 0.492`
- `runtime_poll_success_rate_pct: 100.00`
- `runtime_latency_p95_ms: 76.62`
- `runtime_latency_max_ms: 3020.23`
- `runtime_active_latency_p95_ms: 76.73`
- `runtime_active_latency_max_ms: 3020.23`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/main.cpp`](experiments/upload-ui/code/v5/modules/main.cpp)
- [`experiments/upload-ui/results/raw/upload_ui_v5_startup_sd_only_recover_20260223_011746.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_startup_sd_only_recover_20260223_011746.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_startup_sd_only_recover_20260223_011746.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_startup_sd_only_recover_20260223_011746.summary.txt)

### Notes
- Endpoint-level verification for `/recover_sd` and `/test/recover_sd` could not be completed because the device refused HTTP connections at that specific verification attempt.

## 2026-02-23 01:20:06 +01:00 - Add Explicit User Power-Reset Prompt On Startup SD Recovery Failure
### Planned Steps
- Add clear serial guidance for the user when startup SD recovery fails.
- Keep behavior startup-focused and avoid changing upload runtime flow.
- Build/flash/check to verify firmware still runs.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: on `SD_RECOVER failed`, added explicit action line: `power reset device and re-seat SD card`.
- `experiments/upload-ui/code/v5/modules/main.cpp`: on startup SD init/test-file preparation failure, added explicit prompt: `Please power-reset the device and check SD card seating.`

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `rg -n "power reset|power-reset|action_required" experiments/upload-ui/code/v5/modules/storage.cpp experiments/upload-ui/code/v5/modules/main.cpp`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS

### Result
- `startup_sd_failure_user_prompt_added: true`
- `build_status_sd_http_upload_ui_test_v5: PASS`
- `flash_status_sd_http_upload_ui_test_v5: PASS`
- `check_only_status: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/main.cpp`](experiments/upload-ui/code/v5/modules/main.cpp)

### Notes
- This change adds operator guidance text only; no additional runtime SD recovery logic was introduced.

## 2026-02-23 01:24:21 +01:00 - Move Reachability Probe To Uploader Idle Path And Keep Upload Path Probe-Clear
### Planned Steps
- Move periodic reachability probing to uploader task idle path only.
- Remove pre-upload probe call from active upload flow and keep cached DNS connect path.
- Report probe as clear while uploading and update displayed probe age in 1-second steps.

### Changes Made
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: added 1s idle probe cadence in `upload_task` (`probe_server_reachability_cached(1500)` only in idle branch).
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: removed active upload pre-flight probe call (`probe_server_reachability_cached(CONNECT_TIMEOUT_MS)`).
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: `/status` now reports `probe_ok=true` and empty `probe_err` while upload is active.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: probe age output is quantized to 1-second steps (`probe_age_ms` rounded to 1000 ms).

### Automated Tests Run
- `rg -n "probe_server_reachability_cached|upload_active|probe_age_ms_raw|probe_ok" experiments/upload-ui/code/v5/modules/uploader.cpp experiments/upload-ui/code/v5/modules/webserver.cpp`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 220 --prefix upload_ui_v5_idle_probe_only`: PASS
- `python (JSONL inspection for SENDING probe fields in experiments/upload-ui/results/raw/upload_ui_v5_idle_probe_only_20260223_012232.jsonl)`: PASS
- `python (direct repeated /status sampling for idle probe_age_ms step validation)`: FAIL (intermittent connection refused)

### Result
- `probe_location: uploader_task_idle_only`
- `active_upload_probe_calls_removed: true`
- `cached_dns_connect_path_kept: true`
- `sending_state_probe_ok_values: [1]`
- `sending_state_probe_err_values: ['']`
- `runtime_result_state: DONE`
- `runtime_error_code: 0`
- `runtime_avg_upload_mb_s: 0.132`
- `runtime_poll_success_rate_pct: 100.00`
- `runtime_latency_p95_ms: 39.03`
- `runtime_latency_max_ms: 1211.33`
- `runtime_active_latency_p95_ms: 39.08`
- `runtime_active_latency_max_ms: 1211.33`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/results/raw/upload_ui_v5_idle_probe_only_20260223_012232.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_idle_probe_only_20260223_012232.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_idle_probe_only_20260223_012232.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_idle_probe_only_20260223_012232.summary.txt)

### Notes
- Idle probe-age stepping check via direct HTTP sampling could not be fully verified due intermittent `connection refused` responses from the device after runs.

## 2026-02-23 01:25:54 +01:00 - Remove Probe Age Quantization While Keeping 1s Idle Probe Cadence
### Planned Steps
- Remove `probe_age_ms` 1000ms quantization from `/status`.
- Keep reachability probing cadence at 1000ms in uploader idle path.
- Build/flash/check to validate behavior and endpoint health.

### Changes Made
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: removed probe age quantization; `probe_age_ms` now reports continuous elapsed milliseconds based on `last_update_ms`.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `rg -n "probe_age_ms_raw|probe_age_ms = \\(probe\\.last_update_ms" experiments/upload-ui/code/v5/modules/webserver.cpp`: FAIL (pattern mismatch after code change; no matching lines)
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS

### Result
- `probe_age_quantization_removed: true`
- `idle_probe_cadence_ms: 1000`
- `build_status_sd_http_upload_ui_test_v5: PASS`
- `flash_status_sd_http_upload_ui_test_v5: PASS`
- `check_only_status: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)

### Notes
- Probe execution cadence remains in uploader idle task path; this change affects only display/reporting granularity of probe age.

## 2026-02-23 01:35:45 +01:00 - Split Serial Interface Into Dedicated File And Keep Webserver Focused On HTTP Task
### Planned Steps
- Move serial command helpers/dispatcher out of `webserver.cpp` into a dedicated `serial_tester_interface.cpp`.
- Keep `webserver.cpp` focused on HTTP route registration and move `server_task` there.
- Add concise comments describing module responsibilities and task model, then run build/flash/runtime tests.

### Changes Made
- `experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp`: new file containing `print_serial_help()`, `print_status_line()`, and `handle_serial()` plus structural comments.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: removed serial helper implementations; now contains HTTP route setup plus `server_task()` and module-level comments.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: removed `server_task()` and added module comment clarifying uploader/monitor responsibilities.
- `experiments/upload-ui/code/v5/firmware.cpp`: added include for `serial_tester_interface.cpp` so main can call `handle_serial()`.
- `experiments/upload-ui/code/v5/modules/main.cpp`: added boot/task-structure comments around setup and task creation.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added shared-state module comment.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `rg -n "server_task\(|handle_serial\(|print_serial_help\(|Serial command interface|Web API setup" experiments/upload-ui/code/v5/modules/webserver.cpp experiments/upload-ui/code/v5/modules/uploader.cpp experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp experiments/upload-ui/code/v5/firmware.cpp`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 220 --prefix upload_ui_v5_serial_split`: PASS

### Result
- `serial_interface_split_file_added: true`
- `webserver_http_focus_enforced: true`
- `server_task_location: webserver.cpp`
- `result_state: DONE`
- `error_code: 0`
- `avg_upload_mb_s: 0.402`
- `poll_success_rate_pct: 100.00`
- `latency_p95_ms: 84.05`
- `latency_max_ms: 94.35`
- `active_latency_p95_ms: 84.06`
- `active_latency_max_ms: 94.35`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp`](experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/main.cpp`](experiments/upload-ui/code/v5/modules/main.cpp)
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/firmware.cpp`](experiments/upload-ui/code/v5/firmware.cpp)
- [`experiments/upload-ui/results/raw/upload_ui_v5_serial_split_20260223_013519.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_serial_split_20260223_013519.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_serial_split_20260223_013519.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_serial_split_20260223_013519.summary.txt)

### Notes
- Serial command behavior is unchanged functionally; only file ownership/responsibility was reorganized.

## 2026-02-23 01:44:58 +01:00 - Add Function-Level Comments Across All v5 Test Source Files
### Planned Steps
- Add concise comments above every function definition in v5 test files.
- Keep behavior unchanged while improving readability and module structure understanding.
- Build/flash/run validation to ensure comment-only edits do not regress runtime behavior.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added function-level comments across state/URL/network/SD/recovery/queue/uploaded-state/ring helpers.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: added function-level comments across transport helpers, response parsing, contract checks, self-tests, and task loops.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: added function-level comments for architecture print path (route/task comments already present).
- `experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp`: file already had function-level comments; retained and verified coverage.
- `experiments/upload-ui/code/v5/modules/main.cpp`: added/adjusted comments for `setup()` and `loop()` responsibilities.

### Automated Tests Run
- `powershell script (function-comment coverage checker over experiments/upload-ui/code/v5/modules/*.cpp)`: PASS (no uncovered single-line function definitions found)
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5 -t upload`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --reset-upload-state --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 220 --prefix upload_ui_v5_comment_all_funcs`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10` (post-loop-comment touch-up): PASS

### Result
- `v5_function_comment_pass_completed: true`
- `behavioral_changes_intended: false`
- `runtime_result_state: DONE`
- `runtime_error_code: 0`
- `runtime_avg_upload_mb_s: 0.388`
- `runtime_poll_success_rate_pct: 100.00`
- `runtime_latency_p95_ms: 80.87`
- `runtime_latency_max_ms: 95.25`
- `runtime_active_latency_p95_ms: 81.37`
- `runtime_active_latency_max_ms: 95.25`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp`](experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp)
- [`experiments/upload-ui/code/v5/modules/main.cpp`](experiments/upload-ui/code/v5/modules/main.cpp)
- [`experiments/upload-ui/results/raw/upload_ui_v5_comment_all_funcs_20260223_014421.jsonl`](experiments/upload-ui/results/raw/upload_ui_v5_comment_all_funcs_20260223_014421.jsonl)
- [`experiments/upload-ui/results/raw/upload_ui_v5_comment_all_funcs_20260223_014421.summary.txt`](experiments/upload-ui/results/raw/upload_ui_v5_comment_all_funcs_20260223_014421.summary.txt)

### Notes
- Coverage check script validated single-line function signatures; multiline signatures were manually included in this pass (e.g., `write_all_fair`, multipart builder).

## 2026-02-23 01:46:53 +01:00 - Add Top-Level Stage Summary Table With Stats And Report Links
### Planned Steps
- Add a summary table at the beginning of `IMPLEMENTATION_LOG.md`.
- Include per-stage key stats and a link to the detailed summary artifact where available.
- Run a quick endpoint validation command after documentation update.

### Changes Made
- `IMPLEMENTATION_LOG.md`: added `Stage Summary Table` section near the top with one row per stage entry.
- `IMPLEMENTATION_LOG.md`: populated columns for `Result State`, `Avg Upload MB/s`, `Latency P95`, `Latency Max`, and `Detailed Report` links.

### Automated Tests Run
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS

### Result
- `stage_summary_table_added: true`
- `stages_listed_in_table: 19`
- `detailed_report_links_added: true`

### Artifacts
- [`IMPLEMENTATION_LOG.md`](IMPLEMENTATION_LOG.md)

### Notes
- Entries without runtime upload metrics are marked `n/a` and linked back to the log entry context.

## 2026-02-23 01:50:33 +01:00 - Enforce Self-Test Section At File End
### Planned Steps
- Ensure v5 self-test implementations are grouped at each file end under `//##Self Tests##############`.
- Add the same placement rule to the canonical runbook so future changes follow the same structure.
- Build v5 firmware and verify marker/function placement with a text check.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: self-test functions grouped at file end under `//##Self Tests##############`.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: self-test functions grouped at file end under `//##Self Tests##############`.
- `experiments/upload-ui/docs/runbook.md`: added `Self-Test Placement Convention` section with required divider and placement.

### Automated Tests Run
- `rg -n "//##Self Tests##############|bool run_.*selftest\\(" experiments/upload-ui/code/v5/modules/storage.cpp experiments/upload-ui/code/v5/modules/uploader.cpp`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS

### Result
- `self_test_end_section_enforced: true`
- `self_test_divider_present_storage_cpp: true`
- `self_test_divider_present_uploader_cpp: true`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/docs/runbook.md`](experiments/upload-ui/docs/runbook.md)

### Notes
- Runtime upload speed test was not rerun because this task is layout/documentation-only with no behavior change.

## 2026-02-23 01:52:54 +01:00 - Move Task Entrypoints To Top Of CPP Files
### Planned Steps
- Find all RTOS thread entrypoint functions in v5 files.
- Move/organize task function definitions to the beginning of each `.cpp` where possible.
- Build v5 firmware to confirm no functional regression.

### Changes Made
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: kept `prefetch_task` at top and moved `upload_task` / `monitor_task` entrypoint definitions to top; delegated heavy logic to internal `upload_task_loop()` and `monitor_task_loop()`.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: moved `server_task` definition to the beginning of the file.

### Automated Tests Run
- `rg -n "void (prefetch_task|upload_task|monitor_task|server_task)\\(" experiments/upload-ui/code/v5/modules/uploader.cpp experiments/upload-ui/code/v5/modules/webserver.cpp`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS

### Result
- `task_entrypoints_at_file_top: true`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)

### Notes
- Runtime upload test was not rerun because this is a code-organization-only change with unchanged task behavior.

## 2026-02-23 01:54:11 +01:00 - Add v5 Architecture Documentation
### Planned Steps
- Collect current v5 task structure and shared-state contracts from source files.
- Create a dedicated architecture markdown with task responsibilities and data exchange tables.
- Run lightweight validation commands and record outcomes.

### Changes Made
- `experiments/upload-ui/docs/architecture-v5.md`: added architecture document with component/task overview, shared data contracts, interface mapping, execution sequences, and professional documentation recommendations.

### Automated Tests Run
- `rg -n "^# Upload UI Test v5 Architecture|^## 2\\) Runtime Components|^## 3\\) Shared Data Contracts|^## 7\\) Professional Documentation Pattern" experiments/upload-ui/docs/architecture-v5.md`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS

### Result
- `v5_architecture_doc_added: true`
- `task_data_exchange_mapping_documented: true`
- `runtime_check_only_status: IDLE`
- `runtime_check_only_error: 0`

### Artifacts
- [`experiments/upload-ui/docs/architecture-v5.md`](experiments/upload-ui/docs/architecture-v5.md)

### Notes
- Firmware build/flash was not required because this task is documentation-only.

## 2026-02-23 01:56:59 +01:00 - Move Uploader Task Definitions Back To Bottom
### Planned Steps
- Remove top wrapper indirection for uploader task functions.
- Restore `upload_task` and `monitor_task` as the real bottom-of-file definitions.
- Build firmware to verify no regression.

### Changes Made
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: removed top `upload_task_loop`/`monitor_task_loop` wrapper pattern and restored `upload_task`/`monitor_task` at the lower implementation section.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS

### Result
- `uploader_task_definitions_at_bottom: true`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)

### Notes
- Change was structure/readability only; task behavior remains unchanged.

## 2026-02-23 01:58:21 +01:00 - Extend Banner Lines And Add Task Sections
### Planned Steps
- Replace short section separators with longer multi-line banners.
- Add explicit task-oriented section headers in each v5 file.
- Build firmware to verify formatting-only edits did not break compilation.

### Changes Made
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: added long `Task Section` banner and lengthened `Self Tests` banner.
- `experiments/upload-ui/code/v5/modules/webserver.cpp`: added long `Task Section` banner.
- `experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp`: added long `Task Section` banner.
- `experiments/upload-ui/code/v5/modules/main.cpp`: added long `Task Section` banner above boot/task wiring.
- `experiments/upload-ui/code/v5/modules/storage.cpp`: added long `Task Shared Data Section` banner and lengthened `Self Tests` banner.
- `experiments/upload-ui/docs/runbook.md`: updated self-test placement convention to the new long multi-line banner format.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS

### Result
- `long_banner_style_applied: true`
- `task_section_headers_added: true`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/webserver.cpp`](experiments/upload-ui/code/v5/modules/webserver.cpp)
- [`experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp`](experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp)
- [`experiments/upload-ui/code/v5/modules/main.cpp`](experiments/upload-ui/code/v5/modules/main.cpp)
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/docs/runbook.md`](experiments/upload-ui/docs/runbook.md)

### Notes
- No runtime behavior change intended; update is readability/structure only.

## 2026-02-23 02:01:08 +01:00 - Move Upload Trace Logging To Serial Tester
### Planned Steps
- Move `print_upload_log(...)` implementation from shared storage module to serial tester module.
- Keep a shared declaration so uploader call sites remain unchanged.
- Build v5 firmware to verify link/compile correctness.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: removed `print_upload_log(...)` implementation and kept a forward declaration near shared prototypes.
- `experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp`: added `print_upload_log(...)` implementation.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS

### Result
- `upload_trace_logger_owner: serial_tester_interface`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp`](experiments/upload-ui/code/v5/modules/serial_tester_interface.cpp)

### Notes
- Functional behavior is unchanged; only implementation ownership moved.

## 2026-02-23 02:03:02 +01:00 - Move Upload Stats Types/State To Uploader
### Planned Steps
- Move upload stats types (`UploadState`, `UploadStats`) from storage module to uploader module.
- Move stats backing state (`s_stats`, `s_stats_mux`) and accessors (`state_name`, `update_stats`, `read_stats_snapshot`) to uploader module.
- Build firmware to verify cross-module references still compile.

### Changes Made
- `experiments/upload-ui/code/v5/modules/storage.cpp`: removed `UploadState`/`UploadStats` definitions, removed stats globals, and removed stats helper implementations moved to uploader.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: added `UploadState`/`UploadStats` definitions, `s_stats`/`s_stats_mux`, and `state_name(...)`, `update_stats(...)`, `read_stats_snapshot(...)`.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS

### Result
- `upload_stats_types_owner: uploader_cpp`
- `upload_stats_state_owner: uploader_cpp`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)

### Notes
- Behavior is unchanged; this is module ownership/structure refactoring.

## 2026-02-23 02:04:19 +01:00 - Move Prefetch Task Next To Other Task Implementations
### Planned Steps
- Relocate `prefetch_task()` from top-of-file to the main task implementation block.
- Keep task behavior unchanged.
- Build firmware to verify successful compilation.

### Changes Made
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: moved `prefetch_task(void*)` down to the section directly above `upload_task()` and `monitor_task()`.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS

### Result
- `prefetch_task_grouped_with_other_tasks: true`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)

### Notes
- No runtime logic changed; function location only.

## 2026-02-23 02:21:55 +01:00 - Move Upload/Server Definitions From Storage To Uploader
### Planned Steps
- Move upload-task/server-communication constants and type definitions from `storage.cpp` into `uploader.cpp`.
- Move related queue/probe/url/multipart/uploaded-state functions to `uploader.cpp`.
- Keep `storage.cpp` focused on shared platform/storage primitives and rebuild.

### Changes Made
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: added `//## Constants` and `//## Type Definition` sections.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: moved upload/server related definitions and logic:
  - constants: queue/retry/probe/test-file/uploaded-state values
  - types: `ParsedUrl`, `QueueItem`, `ProbeStats`
  - globals: `s_target`, queue state, probe state, autoscan state
  - functions: probe/url/resolve/multipart helpers, test-file preparation, uploaded-state bookkeeping, queue pipeline helpers, retryable classification
  - self-tests moved from storage: reachability/cache/recover/bookkeeping/reset
- `experiments/upload-ui/code/v5/modules/storage.cpp`: removed moved blocks so this file no longer owns upload/server pipeline definitions.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: FAIL (intermediate state during refactor)
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS (after cleanup removal of temporary moved-block markers)

### Result
- `upload_server_constants_owner: uploader_cpp`
- `upload_server_type_definitions_owner: uploader_cpp`
- `storage_upload_server_blocks_removed: true`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/modules/storage.cpp`](experiments/upload-ui/code/v5/modules/storage.cpp)

### Notes
- Final state compiles cleanly; intermediate compile failure occurred only while moving dependent blocks in phases.

## 2026-02-23 02:25:57 +01:00 - Split Queue Logic Into Dedicated CPP
### Planned Steps
- Create a dedicated queue module and move all upload-queue ownership into it.
- Remove queue definitions from uploader module and update include order.
- Build firmware to validate the refactor.

### Changes Made
- `experiments/upload-ui/code/v5/modules/upload_queue.cpp`: new module containing queue constants/types/globals and queue/bookkeeping functions.
- `experiments/upload-ui/code/v5/modules/upload_queue.cpp`: added requested section headers `//## Constants` and `//## Type Definition`.
- `experiments/upload-ui/code/v5/modules/uploader.cpp`: removed queue state ownership and queue function implementations; uploader now consumes queue APIs/state from the queue module.
- `experiments/upload-ui/code/v5/firmware.cpp`: added include for `upload_queue.cpp` before `uploader.cpp`.

### Automated Tests Run
- `rg -n "void queue_state_summary|void queue_remove|uint32_t queue_schedule_retry|bool queue_add_or_bump|const QueueItem\\* queue_snapshot_ready|void queue_pending\\(|bool reset_upload_state_and_queue|void queue_pending_periodic|struct QueueItem|kQueueLen|kAutoScanIntervalMs" experiments/upload-ui/code/v5/modules/uploader.cpp experiments/upload-ui/code/v5/modules/upload_queue.cpp`: PASS
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: PASS

### Result
- `queue_module_split_done: true`
- `queue_owner_file: experiments/upload-ui/code/v5/modules/upload_queue.cpp`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`experiments/upload-ui/code/v5/modules/upload_queue.cpp`](experiments/upload-ui/code/v5/modules/upload_queue.cpp)
- [`experiments/upload-ui/code/v5/modules/uploader.cpp`](experiments/upload-ui/code/v5/modules/uploader.cpp)
- [`experiments/upload-ui/code/v5/firmware.cpp`](experiments/upload-ui/code/v5/firmware.cpp)

### Notes
- No runtime behavior change intended; this is a structural split to isolate queue concerns.

## 2026-02-25 20:44:54 +01:00 - Add Standard Function Header Comment Format
### Planned Steps
- Add a runbook section defining the standard function header comment format.
- Include the exact requested dashed-header example and a multi-line example.
- Run a verification command to confirm the section is present.

### Changes Made
- `experiments/upload-ui/docs/runbook.md`: added `Function Header Comment Standard` section with required format and multi-line example.

### Automated Tests Run
- `rg -n "Function Header Comment Standard|Helper Function: Returns true if the given CAN ID is blacklisted|Multi-line description" experiments/upload-ui/docs/runbook.md`: PASS

### Result
- `function_header_comment_standard_documented: true`
- `multiline_header_comment_allowed: true`

### Artifacts
- [`experiments/upload-ui/docs/runbook.md`](experiments/upload-ui/docs/runbook.md)

### Notes
- Documentation-only update; firmware build/flash not required.
