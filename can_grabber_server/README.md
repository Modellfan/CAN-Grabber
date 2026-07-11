# CAN Grabber Server

Standalone local server for CAN-Grabber desktop tooling and ESP32 file uploads.

## Current scope

- Black modern UI with an icon-based navigation rail on the left
- DBC workspace page with folder scan, file list, file open, and edit/save flow via `cantools`
- InfluxDB management page
- Integrated ESP32 upload server endpoints from `tools/can-upload-mock`
- Automatic scan for local `influxd` installations
- Manual path override if the scan does not find the correct binary
- Start button to launch the local InfluxDB server executable
- Upload server status page with LAN upload URL, Bonjour state, upload directory, auth state, and endpoint list

## Run

```powershell
python -m pip install -r can_grabber_server\requirements.txt
python -m can_grabber_server.app
```

Open `http://127.0.0.1:8787`.

## Notes

- Persistent app data now lives in a machine-local app home folder:
  - Windows default: `%LOCALAPPDATA%\.can-grabber`
  - Override for development/tests: `CAN_GRABBER_APP_HOME`
- The app writes a shared `<app-home>/config.ini` using Python's standard `configparser` library.
- Loaded/imported DBC files are copied into the persistent `dbc` subfolder of that app home.
- DBC edits are written back to the selected `.dbc` file through `cantools`.
- The scanner looks for both `influxd.exe` and `influxdb3.exe` on Windows in `PATH`, common `InfluxData` install folders, Scoop installs, and shallow portable folders in `Downloads` and `Desktop`.
- Runtime logs are written to `<app-home>/runtime/influxd.log`.
- Device uploads are accepted at `/edit`, `/upload`, `/edit-chunked`, and `/upload-chunked`.
- The original upload server UI is mounted at `/upload-server/ui`.
- Optional migration leftovers can still exist under `<app-home>/state/`, but active settings now live in `config.ini`.
