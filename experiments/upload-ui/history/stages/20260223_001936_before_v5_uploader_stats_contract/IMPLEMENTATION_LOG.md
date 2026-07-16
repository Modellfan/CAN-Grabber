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

## 2026-02-22 23:10:01 +01:00 - Add Web UI Responsiveness Metrics To v5 Tester
### Planned Steps
- Add per-poll responsiveness timing collection to the v5 upload UI tester and persist timings in JSONL output.
- Add computed responsiveness summary metrics for overall polling and active-upload polling windows.
- Run check-only and full upload automation against the ESP endpoint and record pass/fail and produced artifacts.

### Changes Made
- `tools/upload_ui_tester_v5.py:137`: added `/status` poll latency measurement (`poll_ms`) collection.
- `tools/upload_ui_tester_v5.py:140`: added `poll_ms` to JSONL log records.
- `tools/upload_ui_tester_v5.py:204`: added `poll_success_rate_pct` to summary output.
- `tools/upload_ui_tester_v5.py:206`: added `latency_p95_ms` (with p50/max nearby) to summary output.
- `tools/upload_ui_tester_v5.py:209`: added `active_latency_p95_ms` (with active max nearby) to summary output.
- `tools/upload_ui_tester_v5.py:229`: added markdown report responsiveness lines.

### Automated Tests Run
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --check-only --request-timeout 10`: PASS
- `python tools\upload_ui_tester_v5.py --base-url http://192.168.3.45 --start --request-timeout 10 --timeout 180 --prefix upload_ui_v5_run`: PASS

### Result
- Source: [`upload_ui_v5_run_20260222_230755.summary.txt`](logs/upload_ui_v5_run_20260222_230755.summary.txt)
- `result_state: DONE`
- `error_code: 0`
- `avg_upload_mb_s: 0.433`
- `poll_success_rate_pct: 100.00`
- `latency_p95_ms: 35.40`
- `latency_max_ms: 49.84`
- `active_latency_p95_ms: 35.40`
- `active_latency_max_ms: 49.84`

### Artifacts
- [`upload_ui_v5_run_20260222_230755.jsonl`](logs/upload_ui_v5_run_20260222_230755.jsonl)
- [`upload_ui_v5_run_20260222_230755.summary.txt`](logs/upload_ui_v5_run_20260222_230755.summary.txt)

### Notes
- None.

## 2026-02-22 23:21:31 +01:00 - Refactor v5 Into Task-Focused Files
### Planned Steps
- Split `sd_http_upload_ui_test_v5` from one monolithic file into four focused files: `storage.cpp`, `uploader.cpp`, `webserver.cpp`, `main.cpp`.
- Keep `src/dev/sd_http_upload_ui_test_v5.cpp` as a thin entrypoint that includes the new files.
- Build `sd_http_upload_ui_test_v5` to confirm the refactor compiles without functional regressions.

### Changes Made
- `src/dev/upload_ui_test_v5/storage.cpp`: added shared includes, constants, state structs/globals, and storage/network helper functions.
- `src/dev/upload_ui_test_v5/uploader.cpp`: added upload pipeline tasks and transport functions (`upload_task`, `monitor_task`, streaming helpers).
- `src/dev/upload_ui_test_v5/webserver.cpp`: added HTTP route setup and serial command/status handlers.
- `src/dev/upload_ui_test_v5/main.cpp`: added `setup()`/`loop()` boot sequence and task startup; closes anonymous namespace.
- `src/dev/sd_http_upload_ui_test_v5.cpp`: replaced monolithic implementation with thin include-based entrypoint for the new v5 structure.

### Automated Tests Run
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5`: FAIL (sandbox permission denied on `.platformio\platforms.lock`)
- `& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e sd_http_upload_ui_test_v5` (escalated): PASS
- `rg --files src\dev\upload_ui_test_v5`: PASS

### Result
- `v5_structure_refactored: true`
- `new_task_focused_files_count: 4`
- `build_status_sd_http_upload_ui_test_v5: PASS`

### Artifacts
- [`src/dev/sd_http_upload_ui_test_v5.cpp`](src/dev/sd_http_upload_ui_test_v5.cpp)
- [`src/dev/upload_ui_test_v5/storage.cpp`](src/dev/upload_ui_test_v5/storage.cpp)
- [`src/dev/upload_ui_test_v5/uploader.cpp`](src/dev/upload_ui_test_v5/uploader.cpp)
- [`src/dev/upload_ui_test_v5/webserver.cpp`](src/dev/upload_ui_test_v5/webserver.cpp)
- [`src/dev/upload_ui_test_v5/main.cpp`](src/dev/upload_ui_test_v5/main.cpp)

### Notes
- Firmware was built for verification only; no flash/upload command was run in this task.

## 2026-02-22 23:26:45 +01:00 - Create 8 SD Test Files At Boot For v5
### Planned Steps
- Extend v5 storage definitions to include 8 distinct SD test file paths.
- Update boot-time file preparation to create/validate all 8 files instead of only one.
- Build `sd_http_upload_ui_test_v5` to verify the change compiles cleanly.

### Changes Made
- `src/dev/upload_ui_test_v5/storage.cpp`: added `kTestFileCount` and `kTestFilePaths` for 8 SD test files and set active upload path to `/sd_http_post_8mb_01.bin`.
- `src/dev/upload_ui_test_v5/storage.cpp`: replaced single-file setup logic with `ensure_single_test_file(...)` + looped `ensure_test_file()` to create/validate all 8 files at boot.
- `tools/upload_ui_test_runbook.md`: added explicit requirement to run a validation test after each implementation step and record it immediately.

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
- [`src/dev/upload_ui_test_v5/storage.cpp`](src/dev/upload_ui_test_v5/storage.cpp)
- [`tools/upload_ui_test_runbook.md`](tools/upload_ui_test_runbook.md)
- [`IMPLEMENTATION_LOG.md`](IMPLEMENTATION_LOG.md)
- [`logs/upload_ui_v5_poststep_20260222_232932.jsonl`](logs/upload_ui_v5_poststep_20260222_232932.jsonl)
- [`logs/upload_ui_v5_poststep_20260222_232932.summary.txt`](logs/upload_ui_v5_poststep_20260222_232932.summary.txt)

### Notes
- Validation run used base URL `http://192.168.3.45` after flashing v5.

## 2026-02-22 23:34:37 +01:00 - Add Persistent Queue And Auto-Scan Upload Pipeline To v5
### Planned Steps
- Take a stage snapshot before code changes for v5 upload files.
- Add queue, dedupe/bump, uploaded-state tracking, and auto-scan enqueue logic.
- Wire `/start` and serial `start` to enqueue pending files and run validation tests after implementation.

### Changes Made
- `src/dev/upload_ui_test_v5/storage.cpp`: added queue config/state (`kQueueLen`, `QueueItem`, queue storage + mutex), uploaded-state file path, and queue helpers (`queue_add_or_bump`, `queue_snapshot_ready`, `queue_schedule_retry`, `queue_pending`, `queue_pending_periodic`, `uploaded_state_contains`, `mark_uploaded_path`).
- `src/dev/upload_ui_test_v5/storage.cpp`: changed `build_multipart_request(...)` to accept per-file path/filename instead of always using a fixed file name.
- `src/dev/upload_ui_test_v5/uploader.cpp`: refactored `upload_task` to consume queue items, dedupe/bump via pending queue seed, retry failed items with backoff, and mark successful uploads in persistent state.
- `src/dev/upload_ui_test_v5/uploader.cpp`: updated `monitor_task` to run `queue_pending_periodic()` for unattended auto-enqueue.
- `src/dev/upload_ui_test_v5/webserver.cpp`: updated `/start` and serial `start` to seed pending queue before triggering upload worker.
- `src/dev/upload_ui_test_v5/main.cpp`: added boot-time `queue_pending()` call after SD file preparation.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -File tools\stage_archive.ps1 -Action snapshot -Label "before_v5_queue_pipeline" -Paths src/dev/upload_ui_test_v5 src/dev/sd_http_upload_ui_test_v5.cpp tools/upload_ui_tester_v5.py tools/upload_ui_test_runbook.md`: FAIL (paths parsed incorrectly by wrapper call)
- `powershell -ExecutionPolicy Bypass -File tools\stage_archive.ps1 -Action snapshot -Label "before_v5_queue_pipeline" -Paths src/dev/upload_ui_test_v5,src/dev/sd_http_upload_ui_test_v5.cpp,tools/upload_ui_tester_v5.py,tools/upload_ui_test_runbook.md`: FAIL (single combined path string)
- `powershell -ExecutionPolicy Bypass -Command "& .\tools\stage_archive.ps1 -Action snapshot -Label 'before_v5_queue_pipeline' -Paths @('src/dev/upload_ui_test_v5','src/dev/sd_http_upload_ui_test_v5.cpp','tools/upload_ui_tester_v5.py','tools/upload_ui_test_runbook.md')"`: PASS
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
- [`src/dev/upload_ui_test_v5/storage.cpp`](src/dev/upload_ui_test_v5/storage.cpp)
- [`src/dev/upload_ui_test_v5/uploader.cpp`](src/dev/upload_ui_test_v5/uploader.cpp)
- [`src/dev/upload_ui_test_v5/webserver.cpp`](src/dev/upload_ui_test_v5/webserver.cpp)
- [`src/dev/upload_ui_test_v5/main.cpp`](src/dev/upload_ui_test_v5/main.cpp)
- [`archives/stages/20260222_233518_before_v5_queue_pipeline/manifest.json`](archives/stages/20260222_233518_before_v5_queue_pipeline/manifest.json)
- [`logs/upload_ui_v5_queue_pipeline_20260222_233741.jsonl`](logs/upload_ui_v5_queue_pipeline_20260222_233741.jsonl)
- [`logs/upload_ui_v5_queue_pipeline_20260222_233741.summary.txt`](logs/upload_ui_v5_queue_pipeline_20260222_233741.summary.txt)

### Notes
- During `--check-only`, status reported `SENDING`, indicating unattended/background queue processing was already active.

## 2026-02-22 23:40:37 +01:00 - Add Feature-Testability And Serial-Test Command Rule To Runbook
### Planned Steps
- Snapshot runbook/log files before editing documentation.
- Add a permanent runbook rule requiring feature-specific test automation and a serial test command.
- Verify the new rule text is present in the runbook.

### Changes Made
- `tools/upload_ui_test_runbook.md`: added `Feature Testability Rule` section requiring automation updates and a dedicated `test_<feature_name>` serial command per feature.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\tools\stage_archive.ps1 -Action snapshot -Label 'before_runbook_feature_testability_rule' -Paths @('tools/upload_ui_test_runbook.md','IMPLEMENTATION_LOG.md')"`: PASS
- `rg -n "Feature Testability Rule|test_<feature_name>|test_queue_pipeline|Run the serial command" tools\upload_ui_test_runbook.md`: PASS

### Result
- `feature_testability_rule_added: true`
- `serial_test_command_rule_added: true`

### Artifacts
- [`tools/upload_ui_test_runbook.md`](tools/upload_ui_test_runbook.md)
- [`archives/stages/20260222_234037_before_runbook_feature_testability_rule/manifest.json`](archives/stages/20260222_234037_before_runbook_feature_testability_rule/manifest.json)

### Notes
- No firmware build/flash was needed for this documentation-only update.

## 2026-02-22 23:41:52 +01:00 - Add Retry/Backoff/Retry-After Handling To v5 Pipeline
### Planned Steps
- Take a stage snapshot for v5 sources and runbook/tester before changing retry behavior.
- Add retryable classification + retry-after parsing + exponential backoff/jitter handling to queue pipeline execution.
- Add a dedicated serial test command and non-interactive tester check for retry policy, then run validation.

### Changes Made
- `src/dev/upload_ui_test_v5/storage.cpp`: upgraded queue retry scheduler to accept `retry_after_ms` and jitter controls; added `is_http_retryable(...)`.
- `src/dev/upload_ui_test_v5/uploader.cpp`: replaced one-line status parsing with header-aware response parsing (`Retry-After` support), added retryable classification in finalize path, and applied connect/interrupted/server-aware retry decisions.
- `src/dev/upload_ui_test_v5/uploader.cpp`: added `run_retry_policy_selftest()` for deterministic retry-policy validation.
- `src/dev/upload_ui_test_v5/webserver.cpp`: added HTTP test endpoint `/test/retry_policy` and serial command `test_retry_policy`; included it in `help`.
- `tools/upload_ui_tester_v5.py`: added `--retry-policy-check` automation mode to call `/test/retry_policy`.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\tools\stage_archive.ps1 -Action snapshot -Label 'before_v5_retry_policy' -Paths @('src/dev/upload_ui_test_v5','src/dev/sd_http_upload_ui_test_v5.cpp','tools/upload_ui_tester_v5.py','tools/upload_ui_test_runbook.md','IMPLEMENTATION_LOG.md')"`: PASS
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
- [`src/dev/upload_ui_test_v5/storage.cpp`](src/dev/upload_ui_test_v5/storage.cpp)
- [`src/dev/upload_ui_test_v5/uploader.cpp`](src/dev/upload_ui_test_v5/uploader.cpp)
- [`src/dev/upload_ui_test_v5/webserver.cpp`](src/dev/upload_ui_test_v5/webserver.cpp)
- [`tools/upload_ui_tester_v5.py`](tools/upload_ui_tester_v5.py)
- [`archives/stages/20260222_234153_before_v5_retry_policy/manifest.json`](archives/stages/20260222_234153_before_v5_retry_policy/manifest.json)
- [`logs/upload_ui_v5_retry_policy_20260222_234406.summary.txt`](logs/upload_ui_v5_retry_policy_20260222_234406.summary.txt)
- [`logs/upload_ui_v5_retry_policy_20260222_234406.jsonl`](logs/upload_ui_v5_retry_policy_20260222_234406.jsonl)
- [`logs/serial_test_retry_policy_20260222_235045.log`](logs/serial_test_retry_policy_20260222_235045.log)

### Notes
- The `--start` run timed out in `IDLE` because there were no pending files left to enqueue (uploaded-state already marked files as uploaded), not because retry logic failed.

## 2026-02-22 23:49:37 +01:00 - Add Server Contract Validation And Idempotency/Auth Headers To v5
### Planned Steps
- Snapshot v5 firmware/tester/log files before changing upload contract behavior.
- Add JSON response contract parsing/validation and require accepted contract codes for success marking.
- Add `X-Idempotency-Key` and optional `X-Api-Token` headers and validate via automation + serial test command.

### Changes Made
- `src/dev/upload_ui_test_v5/storage.cpp`: added optional token define (`SD_HTTP_UPLOAD_TEST_API_TOKEN`) and request header injection for `X-Idempotency-Key` + `X-Api-Token` (if configured).
- `src/dev/upload_ui_test_v5/uploader.cpp`: added response-body read path, JSON parse helper (`parse_server_json`), and strict contract acceptance (`is_success_contract`).
- `src/dev/upload_ui_test_v5/uploader.cpp`: now marks upload success only for contract-valid codes (`UPLOAD_ACCEPTED`, duplicate idempotency/content), and logs `UPLOAD_CONTRACT_FAIL` otherwise.
- `src/dev/upload_ui_test_v5/uploader.cpp`: added self-test `run_server_contract_selftest()` for contract logic validation.
- `src/dev/upload_ui_test_v5/webserver.cpp`: added `/test/server_contract` endpoint and serial command `test_server_contract`, plus help output entry.
- `tools/upload_ui_tester_v5.py`: added non-interactive automation mode `--server-contract-check`.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\tools\stage_archive.ps1 -Action snapshot -Label 'before_v5_contract_idempotency' -Paths @('src/dev/upload_ui_test_v5','src/dev/sd_http_upload_ui_test_v5.cpp','tools/upload_ui_tester_v5.py','IMPLEMENTATION_LOG.md')"`: PASS
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
- [`src/dev/upload_ui_test_v5/storage.cpp`](src/dev/upload_ui_test_v5/storage.cpp)
- [`src/dev/upload_ui_test_v5/uploader.cpp`](src/dev/upload_ui_test_v5/uploader.cpp)
- [`src/dev/upload_ui_test_v5/webserver.cpp`](src/dev/upload_ui_test_v5/webserver.cpp)
- [`tools/upload_ui_tester_v5.py`](tools/upload_ui_tester_v5.py)
- [`archives/stages/20260222_234938_before_v5_contract_idempotency/manifest.json`](archives/stages/20260222_234938_before_v5_contract_idempotency/manifest.json)
- [`logs/serial_test_contract_retry_20260222_235320.log`](logs/serial_test_contract_retry_20260222_235320.log)

### Notes
- Dedicated endpoint checks and serial self-tests were used as primary validation for contract/header logic.

## 2026-02-22 23:52:53 +01:00 - Add Reset Upload-State Serial/HTTP Command To v5
### Planned Steps
- Add a firmware helper that clears persisted uploaded-file state and re-seeds queue.
- Expose that helper via serial command and HTTP endpoint for automation.
- Add tester automation switch for reset-state and run validation to confirm uploads can restart.

### Changes Made
- `src/dev/upload_ui_test_v5/storage.cpp`: added `reset_upload_state_and_queue()` to remove `/upload_state_v5.txt`, clear queue slots, reset autoscan timestamp, and re-seed pending files.
- `src/dev/upload_ui_test_v5/storage.cpp`: added `run_reset_upload_state_selftest()` to validate reset behavior and state-file absence.
- `src/dev/upload_ui_test_v5/webserver.cpp`: added HTTP endpoint `/reset_upload_state` and self-test endpoint `/test/reset_upload_state`.
- `src/dev/upload_ui_test_v5/webserver.cpp`: added serial commands `reset_upload_state` and `test_reset_upload_state`; updated `help` output and increased serial command buffer size.
- `tools/upload_ui_tester_v5.py`: added `--reset-upload-state` automation mode to call `/reset_upload_state` and report pass/fail non-interactively.
- `tools/upload_ui_test_runbook.md`: added recommended pre-run reset step using `--reset-upload-state` so stale uploaded-state does not block new runs.

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
- [`src/dev/upload_ui_test_v5/storage.cpp`](src/dev/upload_ui_test_v5/storage.cpp)
- [`src/dev/upload_ui_test_v5/webserver.cpp`](src/dev/upload_ui_test_v5/webserver.cpp)
- [`tools/upload_ui_tester_v5.py`](tools/upload_ui_tester_v5.py)
- [`tools/upload_ui_test_runbook.md`](tools/upload_ui_test_runbook.md)
- [`logs/upload_ui_v5_reset_state_20260222_235615.jsonl`](logs/upload_ui_v5_reset_state_20260222_235615.jsonl)
- [`logs/upload_ui_v5_reset_state_20260222_235615.summary.txt`](logs/upload_ui_v5_reset_state_20260222_235615.summary.txt)
- [`logs/serial_test_reset_upload_state_20260222_235654.log`](logs/serial_test_reset_upload_state_20260222_235654.log)
- [`logs/serial_test_reset_upload_state_selftest_20260222_235737.log`](logs/serial_test_reset_upload_state_selftest_20260222_235737.log)

### Notes
- The prior `--start` timeout-in-IDLE condition caused by stale uploaded-state was resolved by resetting upload state before run.

## 2026-02-23 00:01:06 +01:00 - Add Uploaded-State Bookkeeping Metadata To v5
### Planned Steps
- Add explicit uploaded-state metadata storage and summary helpers in v5 storage logic.
- Integrate successful upload marking with metadata writes and expose bookkeeping summary/testing endpoints.
- Add tester automation and serial self-test command for uploaded-state bookkeeping, then validate build/flash/runtime checks.

### Changes Made
- `src/dev/upload_ui_test_v5/storage.cpp`: added metadata file (`/upload_state_v5_meta.log`) append logic and `mark_uploaded_success(...)` to persist uploaded bookkeeping records.
- `src/dev/upload_ui_test_v5/storage.cpp`: added bookkeeping summary helpers (`uploaded_state_summary`, `queue_state_summary`) and self-test `run_uploaded_state_bookkeeping_selftest()`.
- `src/dev/upload_ui_test_v5/storage.cpp`: extended `reset_upload_state_and_queue()` to remove both uploaded-state and metadata files.
- `src/dev/upload_ui_test_v5/uploader.cpp`: switched success marking from `mark_uploaded_path(...)` to `mark_uploaded_success(...)` so each successful upload updates metadata.
- `src/dev/upload_ui_test_v5/webserver.cpp`: added `/bookkeeping` endpoint, `/test/uploaded_state_bookkeeping` endpoint, `bookkeeping` serial command, and `test_uploaded_state_bookkeeping` serial self-test command.
- `tools/upload_ui_tester_v5.py`: added `--uploaded-state-check` automation mode calling `/test/uploaded_state_bookkeeping` and `/bookkeeping`.
- `tools/upload_ui_test_runbook.md`: added uploaded-state bookkeeping feature self-check command and serial command reference.

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
- [`src/dev/upload_ui_test_v5/storage.cpp`](src/dev/upload_ui_test_v5/storage.cpp)
- [`src/dev/upload_ui_test_v5/uploader.cpp`](src/dev/upload_ui_test_v5/uploader.cpp)
- [`src/dev/upload_ui_test_v5/webserver.cpp`](src/dev/upload_ui_test_v5/webserver.cpp)
- [`tools/upload_ui_tester_v5.py`](tools/upload_ui_tester_v5.py)
- [`tools/upload_ui_test_runbook.md`](tools/upload_ui_test_runbook.md)
- [`logs/serial_test_uploaded_state_bookkeeping_20260223_000337.log`](logs/serial_test_uploaded_state_bookkeeping_20260223_000337.log)
- [`logs/upload_ui_v5_bookkeeping_20260223_000347.jsonl`](logs/upload_ui_v5_bookkeeping_20260223_000347.jsonl)
- [`logs/upload_ui_v5_bookkeeping_20260223_000347.summary.txt`](logs/upload_ui_v5_bookkeeping_20260223_000347.summary.txt)

### Notes
- `--uploaded-state-check` intentionally runs a self-test that resets state and marks one file as uploaded; expected summary is `uploaded=1`, `outstanding=7`.

## 2026-02-23 00:05:00 +01:00 - Add SD Recovery Routine And Auto-Invoke On SD Errors
### Planned Steps
- Add a dedicated `recover_sd()` routine with SD deinit, bus settle/clock flush delays, and remount retries.
- Automatically invoke `recover_sd()` on SD error paths in upload/state bookkeeping operations.
- Add a feature self-test command + tester automation flag for recover-SD, then run build/flash/runtime validation.

### Changes Made
- `src/dev/upload_ui_test_v5/storage.cpp`: added `recover_sd(...)` with full SD deinit (`SD.end`, `s_sd_spi.end`), SPI clock flush cycles, settle delays, and remount retries.
- `src/dev/upload_ui_test_v5/storage.cpp`: added `recover_sd_safe(...)` to abort current upload and wait for idle before recovery, plus `run_recover_sd_selftest()`.
- `src/dev/upload_ui_test_v5/storage.cpp`: `init_sd()` now auto-falls back to `recover_sd(...)` if initial mount fails.
- `src/dev/upload_ui_test_v5/storage.cpp`: added automatic recover-on-error retry paths in SD operations (`ensure_single_test_file`, `uploaded_state_contains`, `mark_uploaded_path`, `append_uploaded_metadata`, `reset_upload_state_and_queue`).
- `src/dev/upload_ui_test_v5/uploader.cpp`: upload file-open failure now auto-triggers SD recover and retries `SD.open` before failing run item.
- `src/dev/upload_ui_test_v5/webserver.cpp`: added `/recover_sd` and `/test/recover_sd`, serial commands `recover_sd` + `test_recover_sd`, and help text entries.
- `tools/upload_ui_tester_v5.py`: added `--recover-sd-check` automation mode for `/test/recover_sd`.
- `tools/upload_ui_test_runbook.md`: added recover-SD self-check command and serial command reference.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\tools\stage_archive.ps1 -Action snapshot -Label 'before_v5_recover_sd' -Paths @('src/dev/upload_ui_test_v5','src/dev/sd_http_upload_ui_test_v5.cpp','tools/upload_ui_tester_v5.py','tools/upload_ui_test_runbook.md','IMPLEMENTATION_LOG.md')"`: PASS
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
- [`src/dev/upload_ui_test_v5/storage.cpp`](src/dev/upload_ui_test_v5/storage.cpp)
- [`src/dev/upload_ui_test_v5/uploader.cpp`](src/dev/upload_ui_test_v5/uploader.cpp)
- [`src/dev/upload_ui_test_v5/webserver.cpp`](src/dev/upload_ui_test_v5/webserver.cpp)
- [`tools/upload_ui_tester_v5.py`](tools/upload_ui_tester_v5.py)
- [`tools/upload_ui_test_runbook.md`](tools/upload_ui_test_runbook.md)
- [`archives/stages/20260223_000500_before_v5_recover_sd/manifest.json`](archives/stages/20260223_000500_before_v5_recover_sd/manifest.json)
- [`logs/serial_test_recover_sd_20260223_001107.log`](logs/serial_test_recover_sd_20260223_001107.log)
- [`logs/upload_ui_v5_recover_sd_20260223_001112.jsonl`](logs/upload_ui_v5_recover_sd_20260223_001112.jsonl)
- [`logs/upload_ui_v5_recover_sd_20260223_001112.summary.txt`](logs/upload_ui_v5_recover_sd_20260223_001112.summary.txt)

### Notes
- Two intermediate recover-check timeout failures were resolved by making self-test lightweight and adding `recover_sd_safe(...)` to avoid recovering during active SD transfer.

## 2026-02-23 00:12:44 +01:00 - Add Reachability Probe And Cached Resolution Path In Two Steps
### Planned Steps
- Step 1: add server reachability probing before upload connect, expose probe stats in `/status`, web UI, and feature self-test/automation command.
- Step 2: add cached resolution path for probe/connect target IP, expose cache counters in `/status`, web UI, and re-run measurements.
- Run build/flash and one runtime upload test after each step to compare responsiveness/throughput impact.

### Changes Made
- `src/dev/upload_ui_test_v5/storage.cpp` (Step 1): added reachability probe stats structure and `probe_server_reachability_no_cache(...)`; added probe stats to UI text rendering (`probe_ok`, `probe_ms`, `probe_ip`, `probe_err`, `cache_hits`, `cache_misses`).
- `src/dev/upload_ui_test_v5/uploader.cpp` (Step 1): added mandatory pre-connect probe call and retry scheduling on probe failure (`error -18` path).
- `src/dev/upload_ui_test_v5/webserver.cpp` (Step 1): `/status` now includes probe/cache fields; added `/test/reachability_probe` + serial `test_reachability_probe`.
- `tools/upload_ui_tester_v5.py` (Step 1): added `--reachability-probe-check`.
- `src/dev/upload_ui_test_v5/storage.cpp` (Step 2): added DNS cache state (`cached IP + TTL`) and `resolve_target_cached(...)`, `probe_server_reachability_cached(...)`, and cache self-test `run_reachability_cache_selftest()`.
- `src/dev/upload_ui_test_v5/uploader.cpp` (Step 2): upload connect path now resolves through cache and connects directly by cached IP (`client.connect(IPAddress, port, ...)`).
- `src/dev/upload_ui_test_v5/webserver.cpp` (Step 2): added `/test/reachability_cache` + serial `test_reachability_cache`.
- `tools/upload_ui_tester_v5.py` (Step 2): added `--reachability-cache-check`.
- `tools/upload_ui_test_runbook.md`: added reachability probe/cache automation and serial commands.

### Automated Tests Run
- `powershell -ExecutionPolicy Bypass -Command "& .\tools\stage_archive.ps1 -Action snapshot -Label 'before_v5_probe_cache_feature' -Paths @('src/dev/upload_ui_test_v5','src/dev/sd_http_upload_ui_test_v5.cpp','tools/upload_ui_tester_v5.py','tools/upload_ui_test_runbook.md','IMPLEMENTATION_LOG.md')"`: PASS
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
- [`src/dev/upload_ui_test_v5/storage.cpp`](src/dev/upload_ui_test_v5/storage.cpp)
- [`src/dev/upload_ui_test_v5/uploader.cpp`](src/dev/upload_ui_test_v5/uploader.cpp)
- [`src/dev/upload_ui_test_v5/webserver.cpp`](src/dev/upload_ui_test_v5/webserver.cpp)
- [`tools/upload_ui_tester_v5.py`](tools/upload_ui_tester_v5.py)
- [`tools/upload_ui_test_runbook.md`](tools/upload_ui_test_runbook.md)
- [`archives/stages/20260223_001244_before_v5_probe_cache_feature/manifest.json`](archives/stages/20260223_001244_before_v5_probe_cache_feature/manifest.json)
- [`logs/serial_test_reachability_probe_20260223_001506.log`](logs/serial_test_reachability_probe_20260223_001506.log)
- [`logs/serial_test_reachability_probe_cache_20260223_001747.log`](logs/serial_test_reachability_probe_cache_20260223_001747.log)
- [`logs/upload_ui_v5_probe_step1_20260223_001511.jsonl`](logs/upload_ui_v5_probe_step1_20260223_001511.jsonl)
- [`logs/upload_ui_v5_probe_step1_20260223_001511.summary.txt`](logs/upload_ui_v5_probe_step1_20260223_001511.summary.txt)
- [`logs/upload_ui_v5_probe_step2_20260223_001752.jsonl`](logs/upload_ui_v5_probe_step2_20260223_001752.jsonl)
- [`logs/upload_ui_v5_probe_step2_20260223_001752.summary.txt`](logs/upload_ui_v5_probe_step2_20260223_001752.summary.txt)

### Notes
- Throughput changed across two single-run measurements; for firm cache-performance attribution, run 3+ runs per step on stable network conditions.
