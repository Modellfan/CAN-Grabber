# Firmware Experiments

This directory contains hardware experiments that are intentionally separated
from the production firmware. Code, tools, configuration examples, raw evidence,
and conclusions stay together so a repository cleanup can treat each experiment
as one self-contained unit.

Build an experiment from the repository root:

```powershell
pio run -d experiments -e <environment>
```

## Catalog

| Experiment | Status | Environments | Summary |
| --- | --- | --- | --- |
| [Storage performance](storage-performance/) | active reference | `sd_speed_test`, `sd_sdio_speed_test` | SPI and SDIO write behavior |
| [Network and HTTP](network-http/) | concluded/reference | `sta_ap_test`, `sd_http_download_test`, `sd_http_download_test2`, `webserver_streamfile_test`, `wifi_speed_test`, `set_content_length_test` | Wi-Fi modes and HTTP download implementations |
| [HTTP upload performance](http-upload-performance/) | concluded/reference | `sd_http_post_speed_test` | SD-to-HTTP POST throughput |
| [Upload UI](upload-ui/) | active reference | `sd_http_upload_ui_test`, versions v2-v5 | Upload queue, retry, persistence, and responsiveness |
| [CAN pipeline](can-pipeline/) | active | `rx_load_test`, `can_to_file_modules` | Production-module load and file-pipeline checks |
| [RTC](rtc/) | active reference | `rtc_module_test` | RTC wiring and module behavior |
| [Boot/network diagnostics](boot-network-diagnostics/) | diagnostic toolkit | host tools only | Repeated boot, serial capture, and reachability probes |

## Repository Policy

- Production builds use the root `platformio.ini`; experiments use this
  directory's configuration.
- Every experiment owns its code, tools, configuration examples, documentation,
  and results.
- Real credentials are local-only. Commit only `*.example` configuration files.
- Commit a result summary for every meaningful run. Raw files up to 5 MiB belong
  in `results/raw/`; larger local captures belong in ignored `results/local/`
  and must be referenced by path and checksum from the summary.
- Use Git commits for new restore points. The upload UI stage snapshots are kept
  only as historical evidence.
