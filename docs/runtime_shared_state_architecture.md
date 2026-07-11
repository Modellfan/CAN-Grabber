# Runtime Shared-State Architecture

## Purpose

This note documents the current runtime architecture of the firmware with a
specific focus on data that is shared across tasks or exchanged between
components. The intent is to make ownership explicit:

- who owns a structure
- who is allowed to write it
- who reads it
- how data crosses task boundaries
- what synchronization exists today

This document describes the code as it exists today. It does not normalize or
idealize the design.

## Runtime Topology

The main firmware runs as a small set of long-lived task domains:

| Task / Context | Owner Module | Role |
| --- | --- | --- |
| `loopTask` / `setup()` / `loop()` | `main.cpp` | Boot orchestration, serial commands, service polling, REST/Web loop |
| `can_rx0`, `can_rx1` | `can_manager.cpp` | High-priority CAN receive and line formatting |
| `log_writer` | `log_writer.cpp` | Consumes CAN log blocks and writes `.sav` files |
| `upload_task` | `upload_manager.cpp` | Upload scheduler and async HTTP file transfer |
| `compress_task` | `compressor.cpp` | Background compression of closed log files |
| Arduino WiFi event callback / `arduino_events` task | WiFi core + `net_manager.cpp` | STA/AP events, IP state transitions, mDNS start/stop |

The web UI itself does not own state directly. The UI reads JSON snapshots from
REST handlers, and those handlers aggregate state from multiple modules.

## Shared-State Categories

The current codebase uses three distinct patterns for shared runtime data:

1. `xxx_config` structs
   These represent user configuration persisted in NVS and reused during boot or
   runtime reconfiguration.

2. `xxx_stats` structs
   These are read-mostly status snapshots for diagnostics and UI consumption.
   The healthy pattern in the codebase is "module-private scalars guarded by a
   `portMUX_TYPE`, copied into a public stats struct on demand."

3. Work queues / buffers
   These move payload between tasks. The firmware does not currently use
   FreeRTOS `QueueHandle_t` queues for the main data path. Instead it uses
   fixed-size module-owned arrays and ring buffers guarded by `portMUX_TYPE`.

## Configuration State

### `config::Config`

Location:

- `include/config/app_config.h`
- `src/config/app_config.cpp`

Primary structs:

- `config::Config`
- `config::GlobalConfig`
- `config::BusConfig`
- `config::WifiConfig`

Ownership:

- Owner: `config` module
- Backing store: `config::s_config`
- Persistence: NVS via `Preferences`

Writers:

- `config::init()` loads from NVS at boot
- `config::reset_defaults()` replaces the full config
- REST control plane mutates `config::get_mutable()` in `rest_api.cpp`
- serial debug commands in `main.cpp` mutate WiFi config
- network failover logic in `net_manager.cpp` can disable STA mode and persist it

Readers:

- `main.cpp` during startup
- `can_manager.cpp` for bus enable/bitrate/logging settings
- `log_writer.cpp` for active bus list and file limits
- `net_manager.cpp` for SSIDs, STA enable, retry behavior
- `upload_manager.cpp` for upload URL and feature enable
- `compressor.cpp` for feature enable and storage thresholds
- `rtc_clock.cpp` for manual fallback epoch
- `rest_api.cpp` for config readback and UI serialization

Synchronization:

- No lock
- No critical section
- No versioned snapshot

Exchange model:

- This is a global in-memory control-plane object.
- Components read it directly on demand.
- REST writes it in place and then calls `config::save()`.
- Runtime reconfiguration is therefore shared-memory mutation, not message
  passing.

Engineering note:

- This is a real concurrency gap in the current design. Most code assumes config
  writes are rare and occur from the control plane while readers tolerate
  eventually consistent values.

## Time Base Service

### RTC-backed runtime clock

Location:

- `include/rtc/rtc_clock.h`
- `src/rtc/rtc_clock.cpp`

Primary state:

- `s_base_unix_sec`
- `s_base_millis`
- `s_available`
- `s_running`
- `s_valid`
- `s_source`

Ownership:

- Owner: `rtc_clock` module

Writers:

- `rtc_clock::init()` once at boot
- `rtc_clock::set_unix_epoch()` from REST `/api/time`

Readers:

- `can_manager.cpp` when generating SavvyCAN timestamps
- `rest_api.cpp` when serving `/api/status`
- `system_stats.cpp` when annotating samples with Unix time

Synchronization:

- No lock

Exchange model:

- Shared service state
- "Read current time" is computed from boot epoch plus `millis()`

Engineering note:

- This is cross-task shared state but not queue-based data exchange.
- Like `config::s_config`, it relies on rare writes and frequent lock-free reads.

## CAN Receive to Logger Data Path

### `can::BusState`, `can::LogBlockState`, `can::LogBlock`

Location:

- `include/can/can_manager.h`
- `src/can/can_manager.cpp`

Primary structs:

- `LogBlockState`
- `BusState`
- public handoff struct `can::LogBlock`

Ownership:

- Owner: `can` module
- One `BusState` per logical CAN bus in `s_buses[]`

Writers:

- Per-bus RX tasks (`can_rx0`, `can_rx1`) own production into `BusState.blocks`
- `append_line_to_block()` appends formatted SavvyCAN lines into the active block

Readers:

- `logging::log_task()` consumes ready blocks via `can::acquire_log_block()`
- REST/UI reads queue depth, drops, totals, and load via getters

Synchronization:

- `s_ring_mux[bus_id]` per bus
- All queue state transitions happen under that mux

Exchange model:

- Producer: RX task formats a frame into a textual SavvyCAN line
- Buffer: `LogBlockState.data[kBlockSize]`
- State machine:
  - `0 = free`
  - `1 = ready`
  - `2 = logger_in_use`
  - `3 = rx_active`
- Consumer: `logging::log_task()` takes a `can::LogBlock`, writes it to storage,
  then calls `can::release_log_block()`

Task-to-task exchange:

- `can_rx*` task -> `log_writer` task
- Medium: module-owned double buffer per bus
- Contract: producer never writes a block once it has transitioned to `ready`;
  consumer never touches a block until it sees `ready`

This is the primary payload queue in the system.

## Logging Runtime State

### `logging::BusLogState`

Location:

- `src/logging/log_writer.cpp`

Primary struct:

- `BusLogState`

Ownership:

- Owner: `logging` module
- Operational owner: `log_writer` task

Writers:

- `open_log_file()`
- `close_log_file()`
- `reopen_log_file()`
- `write_bytes()`
- `flush_buffer()`

Readers:

- Effectively private to the logging module
- Other modules should not access it directly

Synchronization:

- No dedicated mux around `s_bus_logs[]`
- Safe in practice because the `log_writer` task is the operational owner and
  lifecycle helpers are called from the same module control path

Exchange model:

- Not a cross-task exchange struct by itself
- It is the sink-side private state for the CAN-to-storage pipeline

### `logging::Stats`

Location:

- `include/logging/log_writer.h`
- `src/logging/log_writer.cpp`

Ownership:

- Owner: `logging` module
- Backing counters are module-private globals guarded by `s_stats_mux`

Writers:

- `log_writer` task updates totals, failures, rate window, and bus count

Readers:

- `rest_api.cpp` for `/api/status` and `/api/buffers`
- `compressor.cpp` to decide whether compression should defer under write load

Synchronization:

- `portMUX_TYPE s_stats_mux`
- `logging::get_stats()` copies a snapshot out under the mux

Exchange model:

- Status plane only
- No payload movement
- This is a textbook "module-owned counters -> snapshot struct for UI" pattern

## Storage Metadata Plane

### `storage::FileStatusEntry`, `storage::FileInfo`, `storage::Stats`

Location:

- `include/storage/storage_manager.h`
- `src/storage/storage_manager.cpp`

Primary structs:

- private `FileStatusEntry`
- public `FileInfo`
- public `Stats`

Ownership:

- Owner: `storage` module
- Backing store:
  - in-memory table `s_entries[]`
  - persisted file `/meta/file_status.json`

Writers:

- `logging::open_log_file()` -> `storage::register_log_file()`
- `logging::close_log_file()` -> `storage::finalize_log_file()`
- `upload_manager.cpp` -> `storage::mark_uploaded()`
- REST file-download path -> `storage::mark_downloaded()`
- storage cleanup logic -> `delete_file()`, `ensure_space()`

Readers:

- `upload_manager.cpp` scans pending and uploaded files
- `compressor.cpp` scans compression candidates
- `rest_api.cpp` exposes file list and storage stats
- `logging.cpp` checks free space before opening or rotating files

Synchronization:

- No lock around `s_entries[]`
- No lock around `s_entry_count`

Exchange model:

- Indirect cross-task exchange through shared metadata rather than a queue
- Example:
  - `log_writer` closes a file and clears `kFlagActive`
  - `upload_task` later discovers the same file through `storage::find_file_info()`
  - `compress_task` independently discovers the same closed file through
    `storage::get_file_info()`

Engineering note:

- This table is globally shared mutable state and currently unsynchronized.
- The code relies on low contention and simple access patterns, not on a strong
  concurrency guarantee.

## Upload Scheduler and Upload Status

### `upload::QueueItem`

Location:

- `src/upload/upload_manager.cpp`

Primary struct:

- `QueueItem`

Ownership:

- Owner: `upload` module
- Backing store: fixed array `s_queue[kQueueLen]`

Writers:

- `logging::close_log_file()` enqueues via `upload::request_upload_auto()`
- REST handlers enqueue via `upload::request_upload()`
- `upload_task` updates retry counters and removes completed items

Readers:

- `upload_task` selects the next ready item via `queue_snapshot_ready()`

Synchronization:

- `portMUX_TYPE s_queue_mux`

Exchange model:

- Producer tasks append or bump work items by path
- Consumer task snapshots the next eligible item and performs the upload
- Retry timing is kept in the same queue entry via `next_attempt_ms`

Task-to-task exchange:

- `log_writer` task -> `upload_task`
- REST / loopTask -> `upload_task`
- Medium: fixed array scheduler, not a FreeRTOS queue

### `upload::Stats`

Location:

- `include/upload/upload_manager.h`
- `src/upload/upload_manager.cpp`

Ownership:

- Owner: `upload` module
- Backing fields are private globals under `s_stats_mux`

Writers:

- `upload_task`
- reachability probe helpers inside the upload module

Readers:

- `rest_api.cpp` serializes this into `/api/status`

Synchronization:

- `portMUX_TYPE s_stats_mux`
- `upload::get_stats()` returns a copy

Exchange model:

- Status plane for the UI
- Current file, speed, error state, server reachability, and cumulative upload
  totals

Important detail:

- `upload::get_stats()` is split into two phases:
  - copy protected counters under `s_stats_mux`
  - derive outstanding file counts by walking `storage::FileInfo`

That means UI status is partly latched and partly computed.

## Compressor Background State

### `compressor::Stats`

Location:

- `include/compress/compressor.h`
- `src/compress/compressor.cpp`

Ownership:

- Owner: `compressor` module
- Operational owner: `compress_task`

Writers:

- `compress_task` updates `s_active`, `s_current_done`, `s_current_total`,
  cumulative totals

Readers:

- `rest_api.cpp` for `/api/status`

Synchronization:

- `portMUX_TYPE s_mux`
- `compressor::get_stats()` copies protected fields under the mux

Exchange model:

- Status plane for UI
- Work discovery is indirect through the storage metadata table, not an explicit
  queue

Task-to-task relationship:

- `compress_task` does not receive a push notification
- It polls `storage::FileInfo` and defers when logging or CAN load is high

## Network Scan and Network Status

### `net::WifiScanEntry`

Location:

- `include/net/net_manager.h`
- `src/net/net_manager.cpp`

Ownership:

- Owner: `net` module
- Backing store: `s_scan_results[]`, `s_scan_count`

Writers:

- `net::poll_scan()`

Readers:

- `rest_api.cpp` via `net::wifi_scan_count()` and `net::wifi_scan_entry()`

Synchronization:

- `portMUX_TYPE s_scan_mutex`

Exchange model:

- The network module owns an internal scan result cache
- REST/UI reads a snapshot entry-by-entry

### Other network state

State such as:

- `s_connecting`
- `s_ap_active`
- `s_mdns_started`
- `s_ssid_index`
- `s_sta_failures[]`

is shared inside `net_manager.cpp`, but it is not exposed as a formal `Stats`
struct and is not uniformly lock-protected. It is coordinated between:

- the WiFi event callback path
- `net::loop()`
- REST/UI getters such as `is_connected()`, `rssi_dbm()`, `ap_clients()`

Engineering note:

- This is another area where the current design is operationally acceptable but
  not architecturally clean.

## System Diagnostics Plane

### `system_stats::ComponentStat`, `TaskStat`, `Snapshot`

Location:

- `include/system/system_stats.h`
- `src/system/system_stats.cpp`

Ownership:

- Owner: `system_stats` module

Writers:

- `main.cpp` during boot and loop
- `upload_manager.cpp`
- `net_manager.cpp`
- `rest_api.cpp`
- any module that calls `system_stats::sample()`

Readers:

- serial console via `sys-stats`
- currently not consumed by REST/UI

Synchronization:

- `portMUX_TYPE s_stats_mux`

Exchange model:

- This is a diagnostic snapshot plane
- It captures component-level heap/stack minima and per-task stack watermarks
- `Snapshot` is the copy-out boundary

This is the cleanest shared-state design currently in the firmware.

## REST/UI Aggregation Layer

Location:

- `src/rest/rest_api.cpp`

Role:

- Reader and serializer
- Not an owner of subsystem state

The REST layer builds UI payloads by aggregating:

- `config::Config`
- `rtc_clock` state
- `logging::Stats`
- `storage::Stats`
- `upload::Stats`
- `compressor::Stats`
- `can` queue and error counters
- `net` getters and WiFi scan cache

Important property:

- UI payloads are assembled as best-effort snapshots across modules.
- There is no system-wide transactional snapshot. Each module is read
  independently.

## Current Cross-Task Exchange Map

| Shared Data | Owner | Producer / Writer | Consumer / Reader | Sync | Exchange Type |
| --- | --- | --- | --- | --- | --- |
| `config::s_config` | `config` | REST, serial, boot loader | all runtime modules | none | shared control plane |
| RTC base epoch scalars | `rtc_clock` | boot, `/api/time` | CAN, REST, diagnostics | none | shared service state |
| `can::BusState.blocks[]` | `can` | `can_rx*` tasks | `log_writer` task | `s_ring_mux[bus]` | payload ring buffer |
| `logging` counters -> `logging::Stats` | `logging` | `log_writer` task | REST, compressor | `s_stats_mux` | status snapshot |
| `storage::s_entries[]` | `storage` | logging, upload, REST, cleanup | upload, compressor, REST, logging | none | shared metadata table |
| `upload::s_queue[]` | `upload` | logging, REST | `upload_task` | `s_queue_mux` | work queue |
| `upload` counters -> `upload::Stats` | `upload` | `upload_task` | REST | `s_stats_mux` | status snapshot |
| compressor counters -> `compressor::Stats` | `compressor` | `compress_task` | REST | `s_mux` | status snapshot |
| `net::s_scan_results[]` | `net` | scan completion path | REST | `s_scan_mutex` | cached scan results |
| `system_stats` arrays -> `Snapshot` | `system_stats` | multiple modules | serial diagnostics | `s_stats_mux` | diagnostic snapshot |

## Architectural Observations

### What is strong today

- The CAN-to-logger payload path has explicit ownership and a real handoff
  contract.
- Upload status and logging status follow a good "private counters + locked
  snapshot" pattern.
- `system_stats` is a strong example of how new observability/state modules
  should be built.

### What is weak today

- `config::s_config` is globally mutable and unsynchronized.
- `storage::s_entries[]` is shared metadata without a lock despite being touched
  by multiple runtime tasks.
- `net_manager` keeps meaningful shared state without a dedicated public stats
  snapshot or a single synchronization strategy.
- Some public APIs are legacy placeholders:
  - `logging::enqueue()` is effectively unused in the current block-based design
  - `can::pop_rx_frame()` is a stub in the current mainline path

### Refactoring direction

If the next step is architectural cleanup, the natural target model is:

1. one owner per mutable runtime table
2. one explicit handoff object per task boundary
3. one `Stats` snapshot struct per UI-facing module
4. one synchronization strategy per module

The two modules that should be normalized first are `config` and `storage`,
because they are global shared state used by several tasks without an explicit
concurrency contract.
