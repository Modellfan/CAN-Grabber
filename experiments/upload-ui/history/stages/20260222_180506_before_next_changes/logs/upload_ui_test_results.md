# Upload UI Test Results

## Historical Runs (2026-02-22)
- Context: Feature-by-feature bring-up of `sd_http_upload_ui_test_v2`.

| Step | Change | Time (Local) | Result | Avg MB/s | Summary |
| --- | --- | --- | --- | ---: | --- |
| 1 | Queue + auto-scan + dedupe/bump | 2026-02-22 14:33:57 | DONE / error=0 | 0.054 | `logs/upload_ui_step1_queue_20260222_143357.summary.txt` |
| 2 | Retry/backoff/jitter/retry-after | 2026-02-22 14:38:39 | DONE / error=0 | 0.642 | `logs/upload_ui_step2_retry_20260222_143839.summary.txt` |
| 3 | JSON contract validation | 2026-02-22 14:40:10 | DONE / error=0 | 0.082 | `logs/upload_ui_step3_contract_20260222_144010.summary.txt` |
| 4 | Idempotency + auth headers | 2026-02-22 14:42:45 | DONE / error=0 | 0.171 | `logs/upload_ui_step4_headers_20260222_144245.summary.txt` |
| 5 | Uploaded-state bookkeeping (final fixed run) | 2026-02-22 14:50:54 | DONE / error=0 | 0.405 | `logs/upload_ui_step5_state_fixed_20260222_145054.summary.txt` |

## Ongoing Policy
- Every future `tools/upload_ui_tester_v2.py` run/check appends a timestamped markdown entry here.
- Entries include:
  - local + UTC time
  - git short hash
  - current working tree changes
  - test/check result metrics

## Check
- Local Time: `2026-02-22 14:53:28`
- UTC Time: `2026-02-22 13:53:28Z`
- Git Ref: `19bec8c`
- Changes:
```text
M include/dev/sd_http_post_speed_secrets.h
 M include/dev/sd_http_upload_ui_secrets.h
 M platformio.ini
 m tools/esp32-arduino-lib-builder
?? logs/sd_http_post_speed_20260222_134926.clean.log
?? logs/sd_http_post_speed_20260222_134926.log
?? logs/sd_http_post_speed_20260222_135755.clean.log
?? logs/sd_http_post_speed_20260222_135755.log
?? logs/sd_http_post_speed_20260222_135925.clean.log
?? logs/sd_http_post_speed_20260222_135925.log
?? logs/sd_http_post_speed_20260222_140031.clean.log
?? logs/sd_http_post_speed_20260222_140031.log
?? logs/sd_http_post_speed_20260222_140332.clean.log
?? logs/sd_http_post_speed_20260222_140332.log
?? logs/sd_http_post_speed_20260222_141124.clean.log
?? logs/sd_http_post_speed_20260222_141124.log
?? logs/sd_http_post_speed_20260222_142301.clean.log
?? logs/sd_http_post_speed_20260222_142301.log
?? logs/sd_http_post_speed_20260222_142942.clean.log
?? logs/sd_http_post_speed_20260222_142942.log
?? logs/sd_http_post_speed_20260222_144451.clean.log
?? logs/sd_http_post_speed_20260222_144451.log
?? logs/sd_http_post_speed_20260222_144626.clean.log
?? logs/sd_http_post_speed_20260222_144626.log
?? logs/sd_http_post_speed_20260222_144829.clean.log
?? logs/sd_http_post_speed_20260222_144829.log
?? logs/upload_ui_run_20260222_135031.jsonl
?? logs/upload_ui_run_20260222_135031.summary.txt
?? logs/upload_ui_run_20260222_140448.jsonl
?? logs/upload_ui_run_20260222_140448.summary.txt
?? logs/upload_ui_run_v2_20260222_141236.jsonl
?? logs/upload_ui_run_v2_20260222_141236.summary.txt
?? logs/upload_ui_run_v2_retry1_20260222_141557.jsonl
?? logs/upload_ui_run_v2_retry1_20260222_141557.summary.txt
?? logs/upload_ui_run_v2_split_20260222_142357.jsonl
?? logs/upload_ui_run_v2_split_20260222_142357.summary.txt
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.jsonl
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.summary.txt
?? logs/upload_ui_step1_queue_20260222_143357.jsonl
?? logs/upload_ui_step1_queue_20260222_143357.summary.txt
?? logs/upload_ui_step2_retry_20260222_143839.jsonl
?? logs/upload_ui_step2_retry_20260222_143839.summary.txt
?? logs/upload_ui_step3_contract_20260222_144010.jsonl
?? logs/upload_ui_step3_contract_20260222_144010.summary.txt
?? logs/upload_ui_step4_headers_20260222_144245.jsonl
?? logs/upload_ui_step4_headers_20260222_144245.summary.txt
?? logs/upload_ui_step5_state_fixed_20260222_145054.jsonl
?? logs/upload_ui_step5_state_fixed_20260222_145054.summary.txt
?? logs/upload_ui_test_results.md
?? src/dev/sd_http_upload_ui_test_v2.cpp
?? src/dev/upload_ui_test_v2/
?? tools/upload_ui_test_runbook.md
?? tools/upload_ui_tester_v2.py
```
- Env: `sd_http_upload_ui_test_v2`
- Base URL: `http://192.168.3.45`
- Check Result: `OK`
- Detail: `status_state=IDLE status_error=0 root_len=1212`
## Check
- Local Time: `2026-02-22 17:38:57`
- UTC Time: `2026-02-22 16:38:57Z`
- Git Ref: `19bec8c`
- Changes:
```text
M include/dev/sd_http_post_speed_secrets.h
 M include/dev/sd_http_upload_ui_secrets.h
 M platformio.ini
 m tools/esp32-arduino-lib-builder
?? logs/sd_http_post_speed_20260222_134926.clean.log
?? logs/sd_http_post_speed_20260222_134926.log
?? logs/sd_http_post_speed_20260222_135755.clean.log
?? logs/sd_http_post_speed_20260222_135755.log
?? logs/sd_http_post_speed_20260222_135925.clean.log
?? logs/sd_http_post_speed_20260222_135925.log
?? logs/sd_http_post_speed_20260222_140031.clean.log
?? logs/sd_http_post_speed_20260222_140031.log
?? logs/sd_http_post_speed_20260222_140332.clean.log
?? logs/sd_http_post_speed_20260222_140332.log
?? logs/sd_http_post_speed_20260222_141124.clean.log
?? logs/sd_http_post_speed_20260222_141124.log
?? logs/sd_http_post_speed_20260222_142301.clean.log
?? logs/sd_http_post_speed_20260222_142301.log
?? logs/sd_http_post_speed_20260222_142942.clean.log
?? logs/sd_http_post_speed_20260222_142942.log
?? logs/sd_http_post_speed_20260222_144451.clean.log
?? logs/sd_http_post_speed_20260222_144451.log
?? logs/sd_http_post_speed_20260222_144626.clean.log
?? logs/sd_http_post_speed_20260222_144626.log
?? logs/sd_http_post_speed_20260222_144829.clean.log
?? logs/sd_http_post_speed_20260222_144829.log
?? logs/upload_ui_run_20260222_135031.jsonl
?? logs/upload_ui_run_20260222_135031.summary.txt
?? logs/upload_ui_run_20260222_140448.jsonl
?? logs/upload_ui_run_20260222_140448.summary.txt
?? logs/upload_ui_run_v2_20260222_141236.jsonl
?? logs/upload_ui_run_v2_20260222_141236.summary.txt
?? logs/upload_ui_run_v2_retry1_20260222_141557.jsonl
?? logs/upload_ui_run_v2_retry1_20260222_141557.summary.txt
?? logs/upload_ui_run_v2_split_20260222_142357.jsonl
?? logs/upload_ui_run_v2_split_20260222_142357.summary.txt
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.jsonl
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.summary.txt
?? logs/upload_ui_step1_queue_20260222_143357.jsonl
?? logs/upload_ui_step1_queue_20260222_143357.summary.txt
?? logs/upload_ui_step2_retry_20260222_143839.jsonl
?? logs/upload_ui_step2_retry_20260222_143839.summary.txt
?? logs/upload_ui_step3_contract_20260222_144010.jsonl
?? logs/upload_ui_step3_contract_20260222_144010.summary.txt
?? logs/upload_ui_step4_headers_20260222_144245.jsonl
?? logs/upload_ui_step4_headers_20260222_144245.summary.txt
?? logs/upload_ui_step5_state_fixed_20260222_145054.jsonl
?? logs/upload_ui_step5_state_fixed_20260222_145054.summary.txt
?? logs/upload_ui_test_results.md
?? src/dev/sd_http_upload_ui_test_v2.cpp
?? src/dev/upload_ui_test_v2/
?? tools/upload_ui_test_runbook.md
?? tools/upload_ui_tester_v2.py
```
- Env: `sd_http_upload_ui_test_v2`
- Base URL: `http://192.168.3.45`
- Check Result: `OK`
- Detail: `status_state=IDLE status_error=0 root_len=1490`
## Run
- Local Time: `2026-02-22 17:49:04`
- UTC Time: `2026-02-22 16:49:04Z`
- Git Ref: `19bec8c`
- Changes:
```text
M include/dev/sd_http_post_speed_secrets.h
 M include/dev/sd_http_upload_ui_secrets.h
 M platformio.ini
 m tools/esp32-arduino-lib-builder
?? logs/sd_http_post_speed_20260222_134926.clean.log
?? logs/sd_http_post_speed_20260222_134926.log
?? logs/sd_http_post_speed_20260222_135755.clean.log
?? logs/sd_http_post_speed_20260222_135755.log
?? logs/sd_http_post_speed_20260222_135925.clean.log
?? logs/sd_http_post_speed_20260222_135925.log
?? logs/sd_http_post_speed_20260222_140031.clean.log
?? logs/sd_http_post_speed_20260222_140031.log
?? logs/sd_http_post_speed_20260222_140332.clean.log
?? logs/sd_http_post_speed_20260222_140332.log
?? logs/sd_http_post_speed_20260222_141124.clean.log
?? logs/sd_http_post_speed_20260222_141124.log
?? logs/sd_http_post_speed_20260222_142301.clean.log
?? logs/sd_http_post_speed_20260222_142301.log
?? logs/sd_http_post_speed_20260222_142942.clean.log
?? logs/sd_http_post_speed_20260222_142942.log
?? logs/sd_http_post_speed_20260222_144451.clean.log
?? logs/sd_http_post_speed_20260222_144451.log
?? logs/sd_http_post_speed_20260222_144626.clean.log
?? logs/sd_http_post_speed_20260222_144626.log
?? logs/sd_http_post_speed_20260222_144829.clean.log
?? logs/sd_http_post_speed_20260222_144829.log
?? logs/upload_ui_probe_v2_20260222_173904.jsonl
?? logs/upload_ui_probe_v2_20260222_173904.summary.txt
?? logs/upload_ui_run_20260222_135031.jsonl
?? logs/upload_ui_run_20260222_135031.summary.txt
?? logs/upload_ui_run_20260222_140448.jsonl
?? logs/upload_ui_run_20260222_140448.summary.txt
?? logs/upload_ui_run_v2_20260222_141236.jsonl
?? logs/upload_ui_run_v2_20260222_141236.summary.txt
?? logs/upload_ui_run_v2_retry1_20260222_141557.jsonl
?? logs/upload_ui_run_v2_retry1_20260222_141557.summary.txt
?? logs/upload_ui_run_v2_split_20260222_142357.jsonl
?? logs/upload_ui_run_v2_split_20260222_142357.summary.txt
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.jsonl
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.summary.txt
?? logs/upload_ui_step1_queue_20260222_143357.jsonl
?? logs/upload_ui_step1_queue_20260222_143357.summary.txt
?? logs/upload_ui_step2_retry_20260222_143839.jsonl
?? logs/upload_ui_step2_retry_20260222_143839.summary.txt
?? logs/upload_ui_step3_contract_20260222_144010.jsonl
?? logs/upload_ui_step3_contract_20260222_144010.summary.txt
?? logs/upload_ui_step4_headers_20260222_144245.jsonl
?? logs/upload_ui_step4_headers_20260222_144245.summary.txt
?? logs/upload_ui_step5_state_fixed_20260222_145054.jsonl
?? logs/upload_ui_step5_state_fixed_20260222_145054.summary.txt
?? logs/upload_ui_test_results.md
?? src/dev/sd_http_upload_ui_test_v2.cpp
?? src/dev/upload_ui_test_v2/
?? tools/upload_ui_test_runbook.md
?? tools/upload_ui_tester_v2.py
```
- Env: `sd_http_upload_ui_test_v2`
- Base URL: `http://192.168.3.45`
- Result: `state=ERROR` `error=-1000`
- Throughput: `avg_upload_mb_s=0.000`
- Bytes: `0/0`
- Sample Count: `2109`
- Log: `logs\upload_ui_probe_v2_20260222_173904.jsonl`
- Summary: `logs\upload_ui_probe_v2_20260222_173904.summary.txt`
## Check
- Local Time: `2026-02-22 17:51:34`
- UTC Time: `2026-02-22 16:51:34Z`
- Git Ref: `19bec8c`
- Changes:
```text
M include/dev/sd_http_post_speed_secrets.h
 M include/dev/sd_http_upload_ui_secrets.h
 M platformio.ini
 m tools/esp32-arduino-lib-builder
?? logs/sd_http_post_speed_20260222_134926.clean.log
?? logs/sd_http_post_speed_20260222_134926.log
?? logs/sd_http_post_speed_20260222_135755.clean.log
?? logs/sd_http_post_speed_20260222_135755.log
?? logs/sd_http_post_speed_20260222_135925.clean.log
?? logs/sd_http_post_speed_20260222_135925.log
?? logs/sd_http_post_speed_20260222_140031.clean.log
?? logs/sd_http_post_speed_20260222_140031.log
?? logs/sd_http_post_speed_20260222_140332.clean.log
?? logs/sd_http_post_speed_20260222_140332.log
?? logs/sd_http_post_speed_20260222_141124.clean.log
?? logs/sd_http_post_speed_20260222_141124.log
?? logs/sd_http_post_speed_20260222_142301.clean.log
?? logs/sd_http_post_speed_20260222_142301.log
?? logs/sd_http_post_speed_20260222_142942.clean.log
?? logs/sd_http_post_speed_20260222_142942.log
?? logs/sd_http_post_speed_20260222_144451.clean.log
?? logs/sd_http_post_speed_20260222_144451.log
?? logs/sd_http_post_speed_20260222_144626.clean.log
?? logs/sd_http_post_speed_20260222_144626.log
?? logs/sd_http_post_speed_20260222_144829.clean.log
?? logs/sd_http_post_speed_20260222_144829.log
?? logs/sd_http_post_speed_20260222_174924.log
?? logs/upload_ui_probe_v2_20260222_173904.jsonl
?? logs/upload_ui_probe_v2_20260222_173904.summary.txt
?? logs/upload_ui_run_20260222_135031.jsonl
?? logs/upload_ui_run_20260222_135031.summary.txt
?? logs/upload_ui_run_20260222_140448.jsonl
?? logs/upload_ui_run_20260222_140448.summary.txt
?? logs/upload_ui_run_v2_20260222_141236.jsonl
?? logs/upload_ui_run_v2_20260222_141236.summary.txt
?? logs/upload_ui_run_v2_retry1_20260222_141557.jsonl
?? logs/upload_ui_run_v2_retry1_20260222_141557.summary.txt
?? logs/upload_ui_run_v2_split_20260222_142357.jsonl
?? logs/upload_ui_run_v2_split_20260222_142357.summary.txt
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.jsonl
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.summary.txt
?? logs/upload_ui_step1_queue_20260222_143357.jsonl
?? logs/upload_ui_step1_queue_20260222_143357.summary.txt
?? logs/upload_ui_step2_retry_20260222_143839.jsonl
?? logs/upload_ui_step2_retry_20260222_143839.summary.txt
?? logs/upload_ui_step3_contract_20260222_144010.jsonl
?? logs/upload_ui_step3_contract_20260222_144010.summary.txt
?? logs/upload_ui_step4_headers_20260222_144245.jsonl
?? logs/upload_ui_step4_headers_20260222_144245.summary.txt
?? logs/upload_ui_step5_state_fixed_20260222_145054.jsonl
?? logs/upload_ui_step5_state_fixed_20260222_145054.summary.txt
?? logs/upload_ui_test_results.md
?? src/dev/sd_http_upload_ui_test_v2.cpp
?? src/dev/upload_ui_test_v2/
?? tools/upload_ui_test_runbook.md
?? tools/upload_ui_tester_v2.py
```
- Env: `sd_http_upload_ui_test_v2`
- Base URL: `http://192.168.3.45`
- Check Result: `OK`
- Detail: `status_state=IDLE status_error=0 root_len=1490`
## Check
- Local Time: `2026-02-22 17:56:27`
- UTC Time: `2026-02-22 16:56:27Z`
- Git Ref: `19bec8c`
- Changes:
```text
M include/dev/sd_http_post_speed_secrets.h
 M include/dev/sd_http_upload_ui_secrets.h
 M platformio.ini
 m tools/esp32-arduino-lib-builder
?? logs/sd_http_post_speed_20260222_134926.clean.log
?? logs/sd_http_post_speed_20260222_134926.log
?? logs/sd_http_post_speed_20260222_135755.clean.log
?? logs/sd_http_post_speed_20260222_135755.log
?? logs/sd_http_post_speed_20260222_135925.clean.log
?? logs/sd_http_post_speed_20260222_135925.log
?? logs/sd_http_post_speed_20260222_140031.clean.log
?? logs/sd_http_post_speed_20260222_140031.log
?? logs/sd_http_post_speed_20260222_140332.clean.log
?? logs/sd_http_post_speed_20260222_140332.log
?? logs/sd_http_post_speed_20260222_141124.clean.log
?? logs/sd_http_post_speed_20260222_141124.log
?? logs/sd_http_post_speed_20260222_142301.clean.log
?? logs/sd_http_post_speed_20260222_142301.log
?? logs/sd_http_post_speed_20260222_142942.clean.log
?? logs/sd_http_post_speed_20260222_142942.log
?? logs/sd_http_post_speed_20260222_144451.clean.log
?? logs/sd_http_post_speed_20260222_144451.log
?? logs/sd_http_post_speed_20260222_144626.clean.log
?? logs/sd_http_post_speed_20260222_144626.log
?? logs/sd_http_post_speed_20260222_144829.clean.log
?? logs/sd_http_post_speed_20260222_144829.log
?? logs/sd_http_post_speed_20260222_174924.log
?? logs/upload_ui_probe_v2_20260222_173904.jsonl
?? logs/upload_ui_probe_v2_20260222_173904.summary.txt
?? logs/upload_ui_run_20260222_135031.jsonl
?? logs/upload_ui_run_20260222_135031.summary.txt
?? logs/upload_ui_run_20260222_140448.jsonl
?? logs/upload_ui_run_20260222_140448.summary.txt
?? logs/upload_ui_run_v2_20260222_141236.jsonl
?? logs/upload_ui_run_v2_20260222_141236.summary.txt
?? logs/upload_ui_run_v2_retry1_20260222_141557.jsonl
?? logs/upload_ui_run_v2_retry1_20260222_141557.summary.txt
?? logs/upload_ui_run_v2_split_20260222_142357.jsonl
?? logs/upload_ui_run_v2_split_20260222_142357.summary.txt
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.jsonl
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.summary.txt
?? logs/upload_ui_step1_queue_20260222_143357.jsonl
?? logs/upload_ui_step1_queue_20260222_143357.summary.txt
?? logs/upload_ui_step2_retry_20260222_143839.jsonl
?? logs/upload_ui_step2_retry_20260222_143839.summary.txt
?? logs/upload_ui_step3_contract_20260222_144010.jsonl
?? logs/upload_ui_step3_contract_20260222_144010.summary.txt
?? logs/upload_ui_step4_headers_20260222_144245.jsonl
?? logs/upload_ui_step4_headers_20260222_144245.summary.txt
?? logs/upload_ui_step5_state_fixed_20260222_145054.jsonl
?? logs/upload_ui_step5_state_fixed_20260222_145054.summary.txt
?? logs/upload_ui_test_results.md
?? src/dev/sd_http_upload_ui_test_v2.cpp
?? src/dev/upload_ui_test_v2/
?? tools/upload_ui_test_runbook.md
?? tools/upload_ui_tester_v2.py
```
- Env: `sd_http_upload_ui_test_v2`
- Base URL: `http://192.168.3.45`
- Check Result: `FAIL`
- Detail: `<urlopen error [WinError 10061] Es konnte keine Verbindung hergestellt werden, da der Zielcomputer die Verbindung verweigerte>`
## Check
- Local Time: `2026-02-22 17:59:32`
- UTC Time: `2026-02-22 16:59:32Z`
- Git Ref: `19bec8c`
- Changes:
```text
M include/dev/sd_http_post_speed_secrets.h
 M include/dev/sd_http_upload_ui_secrets.h
 M platformio.ini
 m tools/esp32-arduino-lib-builder
?? logs/sd_http_post_speed_20260222_134926.clean.log
?? logs/sd_http_post_speed_20260222_134926.log
?? logs/sd_http_post_speed_20260222_135755.clean.log
?? logs/sd_http_post_speed_20260222_135755.log
?? logs/sd_http_post_speed_20260222_135925.clean.log
?? logs/sd_http_post_speed_20260222_135925.log
?? logs/sd_http_post_speed_20260222_140031.clean.log
?? logs/sd_http_post_speed_20260222_140031.log
?? logs/sd_http_post_speed_20260222_140332.clean.log
?? logs/sd_http_post_speed_20260222_140332.log
?? logs/sd_http_post_speed_20260222_141124.clean.log
?? logs/sd_http_post_speed_20260222_141124.log
?? logs/sd_http_post_speed_20260222_142301.clean.log
?? logs/sd_http_post_speed_20260222_142301.log
?? logs/sd_http_post_speed_20260222_142942.clean.log
?? logs/sd_http_post_speed_20260222_142942.log
?? logs/sd_http_post_speed_20260222_144451.clean.log
?? logs/sd_http_post_speed_20260222_144451.log
?? logs/sd_http_post_speed_20260222_144626.clean.log
?? logs/sd_http_post_speed_20260222_144626.log
?? logs/sd_http_post_speed_20260222_144829.clean.log
?? logs/sd_http_post_speed_20260222_144829.log
?? logs/sd_http_post_speed_20260222_174924.log
?? logs/sd_http_post_speed_20260222_175633.log
?? logs/upload_ui_probe_v2_20260222_173904.jsonl
?? logs/upload_ui_probe_v2_20260222_173904.summary.txt
?? logs/upload_ui_run_20260222_135031.jsonl
?? logs/upload_ui_run_20260222_135031.summary.txt
?? logs/upload_ui_run_20260222_140448.jsonl
?? logs/upload_ui_run_20260222_140448.summary.txt
?? logs/upload_ui_run_v2_20260222_141236.jsonl
?? logs/upload_ui_run_v2_20260222_141236.summary.txt
?? logs/upload_ui_run_v2_retry1_20260222_141557.jsonl
?? logs/upload_ui_run_v2_retry1_20260222_141557.summary.txt
?? logs/upload_ui_run_v2_split_20260222_142357.jsonl
?? logs/upload_ui_run_v2_split_20260222_142357.summary.txt
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.jsonl
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.summary.txt
?? logs/upload_ui_step1_queue_20260222_143357.jsonl
?? logs/upload_ui_step1_queue_20260222_143357.summary.txt
?? logs/upload_ui_step2_retry_20260222_143839.jsonl
?? logs/upload_ui_step2_retry_20260222_143839.summary.txt
?? logs/upload_ui_step3_contract_20260222_144010.jsonl
?? logs/upload_ui_step3_contract_20260222_144010.summary.txt
?? logs/upload_ui_step4_headers_20260222_144245.jsonl
?? logs/upload_ui_step4_headers_20260222_144245.summary.txt
?? logs/upload_ui_step5_state_fixed_20260222_145054.jsonl
?? logs/upload_ui_step5_state_fixed_20260222_145054.summary.txt
?? logs/upload_ui_test_results.md
?? src/dev/sd_http_upload_ui_test_v2.cpp
?? src/dev/upload_ui_test_v2/
?? tools/upload_ui_test_runbook.md
?? tools/upload_ui_tester_v2.py
```
- Env: `sd_http_upload_ui_test_v2`
- Base URL: `http://192.168.3.45`
- Check Result: `FAIL`
- Detail: `<urlopen error [WinError 10061] Es konnte keine Verbindung hergestellt werden, da der Zielcomputer die Verbindung verweigerte>`
## Check
- Local Time: `2026-02-22 17:59:39`
- UTC Time: `2026-02-22 16:59:39Z`
- Git Ref: `19bec8c`
- Changes:
```text
M include/dev/sd_http_post_speed_secrets.h
 M include/dev/sd_http_upload_ui_secrets.h
 M platformio.ini
 m tools/esp32-arduino-lib-builder
?? logs/sd_http_post_speed_20260222_134926.clean.log
?? logs/sd_http_post_speed_20260222_134926.log
?? logs/sd_http_post_speed_20260222_135755.clean.log
?? logs/sd_http_post_speed_20260222_135755.log
?? logs/sd_http_post_speed_20260222_135925.clean.log
?? logs/sd_http_post_speed_20260222_135925.log
?? logs/sd_http_post_speed_20260222_140031.clean.log
?? logs/sd_http_post_speed_20260222_140031.log
?? logs/sd_http_post_speed_20260222_140332.clean.log
?? logs/sd_http_post_speed_20260222_140332.log
?? logs/sd_http_post_speed_20260222_141124.clean.log
?? logs/sd_http_post_speed_20260222_141124.log
?? logs/sd_http_post_speed_20260222_142301.clean.log
?? logs/sd_http_post_speed_20260222_142301.log
?? logs/sd_http_post_speed_20260222_142942.clean.log
?? logs/sd_http_post_speed_20260222_142942.log
?? logs/sd_http_post_speed_20260222_144451.clean.log
?? logs/sd_http_post_speed_20260222_144451.log
?? logs/sd_http_post_speed_20260222_144626.clean.log
?? logs/sd_http_post_speed_20260222_144626.log
?? logs/sd_http_post_speed_20260222_144829.clean.log
?? logs/sd_http_post_speed_20260222_144829.log
?? logs/sd_http_post_speed_20260222_174924.log
?? logs/sd_http_post_speed_20260222_175633.log
?? logs/upload_ui_probe_v2_20260222_173904.jsonl
?? logs/upload_ui_probe_v2_20260222_173904.summary.txt
?? logs/upload_ui_run_20260222_135031.jsonl
?? logs/upload_ui_run_20260222_135031.summary.txt
?? logs/upload_ui_run_20260222_140448.jsonl
?? logs/upload_ui_run_20260222_140448.summary.txt
?? logs/upload_ui_run_v2_20260222_141236.jsonl
?? logs/upload_ui_run_v2_20260222_141236.summary.txt
?? logs/upload_ui_run_v2_retry1_20260222_141557.jsonl
?? logs/upload_ui_run_v2_retry1_20260222_141557.summary.txt
?? logs/upload_ui_run_v2_split_20260222_142357.jsonl
?? logs/upload_ui_run_v2_split_20260222_142357.summary.txt
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.jsonl
?? logs/upload_ui_run_v2_tasksplit_20260222_142712.summary.txt
?? logs/upload_ui_step1_queue_20260222_143357.jsonl
?? logs/upload_ui_step1_queue_20260222_143357.summary.txt
?? logs/upload_ui_step2_retry_20260222_143839.jsonl
?? logs/upload_ui_step2_retry_20260222_143839.summary.txt
?? logs/upload_ui_step3_contract_20260222_144010.jsonl
?? logs/upload_ui_step3_contract_20260222_144010.summary.txt
?? logs/upload_ui_step4_headers_20260222_144245.jsonl
?? logs/upload_ui_step4_headers_20260222_144245.summary.txt
?? logs/upload_ui_step5_state_fixed_20260222_145054.jsonl
?? logs/upload_ui_step5_state_fixed_20260222_145054.summary.txt
?? logs/upload_ui_test_results.md
?? src/dev/sd_http_upload_ui_test_v2.cpp
?? src/dev/upload_ui_test_v2/
?? tools/upload_ui_test_runbook.md
?? tools/upload_ui_tester_v2.py
```
- Env: `sd_http_upload_ui_test_v2`
- Base URL: `http://192.168.3.45`
- Check Result: `OK`
- Detail: `status_state=IDLE status_error=0 root_len=1490`
