# SLCAN Tool

Direct Python command-line tool for SLCAN receive logging, DBC decoding, fixed-message transmit, ASC playback, ECU-style echo rules, and offline ASC analysis.

Default target:

- `COM22`
- serial baudrate `115200`
- CAN bitrate `500000`

The active entrypoint is:

```powershell
python tools\python_can_tool\slcan_tool.py <command> [options]
```

The archived PowerShell wrapper is kept at `tools/python_can_tool/archive/run_slcan_logger.ps1`.

## Commands

Listen and write an ASC log:

```powershell
python tools\python_can_tool\slcan_tool.py listen --duration 10 --listen-only
```

Listen with DBC decode, live decoded console output, and summary files:

```powershell
python tools\python_can_tool\slcan_tool.py listen --duration 10 --listen-only --dbc-file C:\path\vehicle.dbc --print-decoded
```

Transmit one fixed classic CAN frame:

```powershell
python tools\python_can_tool\slcan_tool.py tx --tx-id 3E8 --tx-data 02 --tx-interval-ms 100 --tx-count 50
```

Dry-run fixed transmit without opening the serial port:

```powershell
python tools\python_can_tool\slcan_tool.py tx --tx-id 3E8 --tx-data 02 --dry-run
```

Playback an ASC log once with original timing:

```powershell
python tools\python_can_tool\slcan_tool.py playback --playback-log-file C:\temp\can_log.asc
```

Playback with filters, time window, ID rewrite, loop gap, and dry-run:

```powershell
python tools\python_can_tool\slcan_tool.py playback --playback-log-file C:\temp\can_log.asc --filter-id 3E8 --start-time 0 --end-time 5 --rewrite-id 3E8:3E9 --loop-gap-ms 250 --dry-run
```

Run echo simulation from JSON rules:

```powershell
python tools\python_can_tool\slcan_tool.py echo --rules-file tools\python_can_tool\examples\echo_rules.example.json --duration 10
```

Offline decode an ASC log with a DBC file:

```powershell
python tools\python_can_tool\slcan_tool.py decode --asc-file C:\temp\can_log.asc --dbc-file C:\path\vehicle.dbc
```

Offline analyze an ASC log:

```powershell
python tools\python_can_tool\slcan_tool.py analyze --asc-file C:\temp\can_log.asc
```

## Shared Options

Hardware commands support:

- `--profile canable500`
- `--port 22`
- `--serial-baudrate 115200`
- `--can-bitrate 500000`
- `--data-bitrate 2000000`
- `--duration 10`

Filter-capable commands support:

- `--filter-id 3E8`
- `--filter-id 100-1FF`
- `--extended-only`
- `--standard-only`

Profiles are loaded from `tools/python_can_tool/profiles.json`.

## Outputs

`listen`, `decode`, and `analyze` write summary artifacts:

- `<base>_summary.json`
- `<base>_ids.csv`
- `<base>_signals.csv`

DBC decoded signal CSV columns:

- `timestamp_absolute`
- `timestamp_relative`
- `channel`
- `frame_id_hex`
- `frame_id_dec`
- `message_name`
- `signal_name`
- `value`
- `unit`

## Notes

- Transmit modes are explicit: use `tx`, `playback`, or `echo`.
- `--tx-count 0` and `--playback-count 0` run until Ctrl+C or `--duration`.
- `--playback-time-scale 1` preserves timing, `2` is twice as fast, and `0.5` is half speed.
- Supported CAN FD data rates are `2000000` and `5000000`, matching CANable2 `Y2` and `Y5`.
