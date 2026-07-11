# CAN Grabber Current State

Snapshot date: 2026-07-07

## Repository State

- Current branch: `docs/clean-architecture-structure`
- Working tree: not clean
- Git status required a per-command `safe.directory` override because the repository owner is `Win11 Pro` and the sandbox user is `CodexSandboxOffline`.
- Modified tracked files:
  - `IMPLEMENTATION_LOG.md`
  - `README.md`
  - `include/upload/upload_manager.h`
  - `src/logging/log_writer.cpp`
  - `src/main.cpp`
  - `src/net/net_manager.cpp`
  - `src/rest/rest_api.cpp`
  - `src/storage/storage_manager.cpp`
  - `src/upload/upload_manager.cpp`
  - `tools/dev/http_reachability_probe.py`
  - `tools/esp32-arduino-lib-builder` submodule/worktree marker
- Major untracked additions:
  - `can_grabber_server/`
  - `docs/`
  - `include/system/`
  - `src/system/`
  - `tools/python_can_tool/`
  - new boot, WiFi, and SD/MMC test logs under `logs/`
  - `tools/dev/boot_probe_loop.py`

## Project Shape

CAN-Grabber is an ESP32-S3 based multi-bus CAN logger. The firmware captures CAN frames, writes SavvyCAN-compatible logs to SD storage, exposes a local web and REST control plane, and can upload completed log files to an HTTP endpoint.

The repository now has a cleaner English documentation structure:

- `README.md` is reduced to a high-level project map.
- `docs/README.md` is the documentation index.
- `docs/software-architecture.md` documents the firmware topology.
- `docs/runtime_shared_state_architecture.md` documents current shared-state ownership and synchronization.
- `docs/components/` contains focused component notes.
- `docs/testing/` contains test-related documentation.
- `docs/history/implementation-log.md` preserves implementation history.

## Firmware State

The production firmware remains under `src/` and `include/`.

Main runtime modules:

- `config`: NVS-backed app, bus, WiFi, upload, and compressor configuration.
- `rtc`: RTC-backed runtime clock.
- `storage`: SD card and file metadata management.
- `can`: CAN receive tasks and block handoff to logging.
- `logging`: log writer task for `.sav` files.
- `upload`: upload scheduler, retry handling, and upload statistics.
- `net`: WiFi STA/AP behavior, scan cache, and mDNS.
- `rest`: embedded HTTP REST control plane.
- `web`: embedded static UI serving.
- `system_stats`: new diagnostic snapshot plane for component/task heap and stack sampling.

Important current architecture observations:

- The CAN receive to log writer path has a clear guarded block handoff.
- Logging, upload, compressor, and system diagnostics use snapshot-style status APIs.
- `config::s_config` is still globally mutable without a lock.
- `storage::s_entries[]` is still shared between logging, upload, compressor, REST, and cleanup without a lock.
- `net_manager` exposes useful state but does not yet have one clean public stats snapshot.
- Some APIs are legacy or not part of the mainline path, especially `logging::enqueue()` and `can::pop_rx_frame()`.

## Embedded REST/UI State

The embedded REST API in `src/rest/rest_api.cpp` exposes:

- `/api/status`
- `/api/config`
- `/api/time`
- `/api/wifi/scan`
- `/api/can/stats`
- `/api/storage/stats`
- `/api/buffers`
- `/api/files`
- `/api/files/{id}/download`
- `/api/files/{id}/upload`
- `/api/files/{id}/delete`
- `/api/control/start_logging`
- `/api/control/stop_logging`
- `/api/control/close_active_file`

The embedded browser UI under `data/` consumes those endpoints for status, configuration, file actions, logging control, upload actions, and time setting.

## CAN Grabber Server State

The desktop-side CAN Grabber Server is present under `can_grabber_server/`.

Current implementation:

- FastAPI backend in `can_grabber_server/app.py`
- Static frontend in `can_grabber_server/static/index.html`, `app.js`, and `styles.css`
- Persistent app-home storage via `can_grabber_server/storage_paths.py`
- Shared config file via `can_grabber_server/config_store.py`
- DBC workflow via `can_grabber_server/dbc_manager.py`
- InfluxDB workflow via `can_grabber_server/influx_manager.py`
- Integrated upload server wrapper in `can_grabber_server/upload_server.py`

Current server UI pages:

- Overview
- DBC Studio
- InfluxDB
- Upload Server
- Roadmap placeholder

Current desktop API:

- `GET /`
- `GET /healthz`
- `GET /api/influx/status`
- `GET /api/upload-server/status`
- `POST /api/influx/search`
- `POST /api/influx/path`
- `POST /api/influx/start`
- `GET /api/dbc/status`
- `POST /api/dbc/directory`
- `POST /api/dbc/scan`
- `GET /api/dbc/file`
- `POST /api/dbc/upload-open`
- `POST /api/dbc/message`
- `POST /api/dbc/signal`
- `GET /readyz`
- `POST /edit`
- `POST /upload`
- `POST /edit-chunked`
- `POST /upload-chunked`

Current desktop capabilities:

- Scan for local `influxd` or `influxdb3` installations.
- Save a selected InfluxDB executable path.
- Start a local InfluxDB process and write its runtime log.
- Store desktop app state in a machine-local app home folder:
  - Windows default: `%LOCALAPPDATA%\.can-grabber`
  - Override: `CAN_GRABBER_APP_HOME`
- Import a single `.dbc` file into persistent storage.
- Import all `.dbc` files from a selected folder.
- List stored DBC files.
- Open a DBC with `cantools`.
- Display messages and signals.
- Edit message name/comment.
- Edit signal name, scale, offset, and comment.
- Write edited DBC data back to the stored copy.
- Accept ESP32 multipart uploads at `/edit` and `/upload`.
- Accept raw stream uploads at `/edit-chunked` and `/upload-chunked`.
- Keep the original upload server UI mounted at `/upload-server/ui`.

Run command:

```powershell
python -m pip install -r can_grabber_server\requirements.txt
python -m can_grabber_server.app
```

Expected local URL:

```text
http://127.0.0.1:8787
```

## Tools State

Additional tooling is present:

- `tools/python_can_tool/`: Python SLCAN command-line tool for listening, logging, DBC decoding, transmit, playback, echo rules, and offline analysis.
- `tools/can-upload-mock/`: upload mock server.
- `tools/dev/boot_probe_loop.py`: boot probe automation.
- `tools/upload_ui_tester_v5.py` and related scripts: upload UI/hardware test support.

## Verification State

No build, server startup, browser check, or test run was performed for this snapshot.

Known verification gaps:

- `platformio run -e esp32s3` should be run before firmware changes are considered stable.
- `platformio run -e sd_http_upload_ui_test_v5` should be run before upload UI work is considered stable.
- The CAN Grabber Server should be started locally and checked at `http://127.0.0.1:8787`.
- DBC upload/edit/save should be tested with a real `.dbc` file.
- InfluxDB detection/start should be tested on the target Windows machine.
- There are no visible automated tests for `can_grabber_server/` yet.

## Immediate Risks

- The working tree contains many uncommitted changes and untracked files, so future edits should be scoped carefully.
- Some logs and generated cache files are untracked; decide what should be kept before committing.
- `tools/dev/__pycache__/http_reachability_probe.cpython-314.pyc` is untracked generated output and likely should not be committed.
- CAN Grabber Server startup writes to the machine-local app home folder, not the repository.
- The desktop DBC editor rewrites DBC files via `cantools`; real file round-trip behavior needs validation.
- InfluxDB startup uses `subprocess.Popen`; process lifecycle management is minimal.

## Suggested Next Steps

1. Decide whether the active priority is CAN Grabber Server or embedded firmware cleanup.
2. For server work, add a small automated test layer for config storage, DBC import/edit, upload intake, and Influx path resolution.
3. Run the desktop app and verify UI behavior in a browser.
4. Clean or intentionally archive untracked generated files before committing.
5. Normalize shared firmware state next, starting with `storage::s_entries[]` and then `config::s_config`.
