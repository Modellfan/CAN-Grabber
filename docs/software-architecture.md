# Software Architecture

## Purpose

CAN-Grabber is a task-based embedded firmware for an ESP32-S3 CAN logger. It is
designed to keep time-critical CAN receive work separate from SD storage,
network, REST, web UI, and upload work.

## System Context

```mermaid
flowchart LR
  Operator[Operator browser or desktop tool]
  UploadTarget[HTTP upload server]
  SavvyCAN[SavvyCAN / serial CAN tools]
  CANBus[Vehicle CAN buses]
  RTC[RTC module]
  SD[SD card]

  subgraph Device[ESP32-S3 CAN-Grabber]
    Firmware[Firmware runtime]
    Web[Web and REST control plane]
    USB[USB serial / GVRET path]
  end

  CANBus --> Device
  RTC --> Device
  Device <--> SD
  Operator <--> Web
  Web --> Firmware
  Firmware --> UploadTarget
  SavvyCAN <--> USB
```

## Firmware Component View

```mermaid
flowchart TD
  Main[main.cpp boot and loop] --> Config[config]
  Main --> Rtc[rtc_clock]
  Main --> Storage[storage]
  Main --> Can[can]
  Main --> Logging[logging]
  Main --> Upload[upload]
  Main --> Net[net]
  Main --> Rest[rest]
  Main --> Web[web]
  Main --> Stats[system_stats]

  Can --> Logging
  Logging --> Storage
  Logging --> Upload
  Upload --> Storage
  Upload --> Net
  Rest --> Config
  Rest --> Storage
  Rest --> Upload
  Rest --> Logging
  Rest --> Can
  Rest --> Net
  Stats --> Rtc
  Compressor[compressor optional] --> Storage
  Compressor --> Logging
```

## Boot Sequence

```mermaid
sequenceDiagram
  participant Main as setup()
  participant Stats as system_stats
  participant Config as config
  participant RTC as rtc_clock
  participant Storage as storage
  participant CAN as can
  participant Log as logging
  participant Upload as upload
  participant Net as net
  participant REST as rest
  participant Web as web

  Main->>Stats: init()
  Main->>Config: init()
  Main->>RTC: init()
  Main->>Storage: init()
  Main->>CAN: init()
  Main->>Log: init(), start()
  Main->>Upload: init(), queue_pending()
  Main->>Net: init(), connect()
  Main->>REST: init(), start()
  Main->>Web: init(), start()
  Main->>Stats: print_summary("setup_complete")
```

## CAN Logging Data Flow

```mermaid
sequenceDiagram
  participant RX as CAN RX task
  participant Ring as can::BusState.blocks
  participant Log as logging::log_task
  participant Store as storage::card()
  participant Meta as storage metadata
  participant Upload as upload queue

  RX->>Ring: append formatted SavvyCAN lines
  RX->>Ring: mark block ready
  Log->>Ring: acquire_log_block()
  Log->>Store: write block bytes
  Log->>Ring: release_log_block()
  Log->>Meta: register/finalize file
  Log->>Upload: request_upload_auto(path)
```

## Upload Data Flow

```mermaid
sequenceDiagram
  participant Producer as Log writer or REST
  participant Queue as upload::s_queue
  participant Task as upload_task
  participant Store as storage
  participant Net as WiFiClient / AsyncTCP
  participant Server as HTTP server

  Producer->>Queue: request_upload(path)
  Task->>Queue: snapshot next ready item
  Task->>Store: find file and open stream
  Task->>Net: connect and send multipart body
  Net->>Server: POST file
  Server-->>Task: HTTP + JSON contract
  Task->>Store: mark_uploaded(path)
  Task->>Queue: remove or schedule retry
```

## Runtime Task Model

| Task / Context | Owner | Responsibility |
| --- | --- | --- |
| Arduino `setup()` / `loop()` | `main.cpp` | Boot ordering, serial console, module polling |
| CAN RX tasks | `can` | Receive CAN frames and fill per-bus log blocks |
| `log_writer` | `logging` | Drain ready CAN blocks and write SD log files |
| `upload_task` | `upload` | Upload closed files and retry failures |
| `compress_task` | `compressor` | Optional background compression |
| WiFi event task | Arduino WiFi + `net` | Connection events, AP/STA state, mDNS |
| REST/Web loop | `rest`, `web` | Control plane and static UI serving |

## Shared-State Ownership

```mermaid
flowchart LR
  ConfigState[config::s_config<br/>unlocked shared config]
  CanBlocks[can block rings<br/>per-bus mux]
  LogStats[logging stats<br/>stats mux]
  StorageEntries[storage entries<br/>currently unlocked]
  UploadQueue[upload queue<br/>queue mux]
  UploadStats[upload stats<br/>stats mux]
  SystemStats[system stats<br/>stats mux]

  REST[REST/UI] --> ConfigState
  REST --> LogStats
  REST --> StorageEntries
  REST --> UploadStats
  CAN[CAN RX] --> CanBlocks --> Logging[Log writer]
  Logging --> StorageEntries
  Logging --> LogStats
  Logging --> UploadQueue
  Upload[Upload task] --> UploadQueue
  Upload --> StorageEntries
  Upload --> UploadStats
  Any[Runtime modules] --> SystemStats
```

## Current Risks And Target Direction

Strong current patterns:

- CAN-to-log-writer payload transfer has a clear block handoff contract.
- `logging::Stats`, `upload::Stats`, and `system_stats::Snapshot` use explicit
  copy-out snapshot boundaries.
- Upload retry state is owned by the upload module.

Known weak points:

- `config::s_config` is globally mutable without a lock.
- `storage::s_entries[]` is shared by logging, upload, compressor, and REST
  without a lock.
- `logging::enqueue()` is a legacy placeholder and does not feed the current
  block-based log writer.
- `can::pop_rx_frame()` is not the mainline data path.

Target cleanup direction:

- One owner per mutable runtime table.
- One explicit handoff object per task boundary.
- One public `Stats` or `Snapshot` type per UI-facing module.
- One synchronization strategy per module.

## Component Documentation

See [components/](components/) for focused module documentation.
