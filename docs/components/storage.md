# Storage Component

## Purpose

The `storage` component owns SD card mounting, file metadata, storage capacity
queries, and lifecycle state for log files.

## Public API

- Header: `include/storage/storage_manager.h`
- Implementation: `src/storage/storage_manager.cpp`
- Main types: `storage::Stats`, `storage::FileInfo`

## Owned State

- SD_MMC filesystem returned by `storage::card()`
- In-memory file table `s_entries[]`
- Persisted metadata file `/meta/file_status.json`
- File flags for downloaded, uploaded, and active files

## Collaborators

```mermaid
flowchart LR
  Logging[logging] --> Storage[storage]
  Upload[upload] --> Storage
  REST[rest] --> Storage
  Compressor[compressor] --> Storage
  Storage --> SD[SD_MMC / SD card]
  Storage --> Meta[/meta/file_status.json]
```

## Runtime Behavior

- `storage::init()` mounts SD_MMC and loads metadata.
- `register_log_file()` creates or updates an active metadata entry.
- `finalize_log_file()` closes the lifecycle and clears the active flag.
- `mark_downloaded()` and `mark_uploaded()` update flags.
- `ensure_space()` deletes old eligible log files when free space is low.

## Failure Modes

- SD card missing or mount failure makes `storage::is_ready()` false.
- Metadata JSON parse failure can hide previously known file state.
- `s_entries[]` is currently shared without a lock across logging, upload,
  compressor, and REST paths.
- File table capacity is fixed.

## Test Strategy

- Raw storage performance: `sd_speed_test` and `sd_sdio_speed_test`.
- Production-path storage performance: planned `sd_storage_write_perf_test`
  should use `storage::init()` and `storage::card()`.
- Integration: `rx_load_test` verifies log-writer writes through storage.
- Metadata tests should verify active, finalized, uploaded, downloaded, and
  delete/cleanup transitions.
