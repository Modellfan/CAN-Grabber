param(
    [int]$Port = 22,
    [int]$SerialBaudrate = 115200,
    [int]$CanBitrate = 500000,
    [ValidateSet(0, 2000000, 5000000)]
    [int]$DataBitrate = 0,
    [string]$LogFile = "",
    [string]$DbcFile = "",
    [string]$DecodedLogFile = "",
    [string]$PlaybackLogFile = "",
    [double]$PlaybackTimeScale = 1.0,
    [int]$PlaybackCount = 1,
    [string]$TxId = "",
    [string]$TxData = "",
    [double]$TxIntervalMs = 1000.0,
    [int]$TxCount = 1,
    [switch]$TxExtendedId,
    [switch]$Append,
    [switch]$ListenOnly,
    [double]$InitDelaySeconds = 2.0,
    [double]$ReadTimeoutSeconds = 0.2,
    [double]$DurationSeconds = 0.0
)

$ErrorActionPreference = "Stop"

$toolRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$logDir = Join-Path $toolRoot "logs"

$usingPlayback = -not [string]::IsNullOrWhiteSpace($PlaybackLogFile)
$usingFixedTx = -not [string]::IsNullOrWhiteSpace($TxId)
$usingTransmit = $usingPlayback -or $usingFixedTx

if ($usingPlayback -and $usingFixedTx) {
    throw "Use either -PlaybackLogFile or -TxId, not both."
}

if ($usingTransmit -and $ListenOnly) {
    throw "Transmit mode cannot be used with -ListenOnly."
}

if ($usingTransmit -and (-not [string]::IsNullOrWhiteSpace($DbcFile) -or -not [string]::IsNullOrWhiteSpace($DecodedLogFile))) {
    throw "DBC decoding is only available in receive logging mode."
}

if ($usingPlayback) {
    if (-not (Test-Path -LiteralPath $PlaybackLogFile -PathType Leaf)) {
        throw "Playback log file not found: $PlaybackLogFile"
    }
    if ($PlaybackTimeScale -le 0) {
        throw "-PlaybackTimeScale must be greater than 0."
    }
    if ($PlaybackCount -lt 0) {
        throw "-PlaybackCount must be 0 for infinite playback or a positive repetition count."
    }
    $PlaybackLogFile = (Resolve-Path -LiteralPath $PlaybackLogFile).Path
}

if ($usingFixedTx) {
    if ($TxIntervalMs -lt 0) {
        throw "-TxIntervalMs must be 0 or greater."
    }
    if ($TxCount -lt 0) {
        throw "-TxCount must be 0 for infinite transmit or a positive repetition count."
    }
}

if (-not $usingTransmit -and [string]::IsNullOrWhiteSpace($LogFile)) {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $LogFile = Join-Path $logDir ("slcan_com{0}_{1}.asc" -f $Port, $timestamp)
}

if (-not $usingTransmit) {
    $logParent = Split-Path -Parent $LogFile
    if (-not [string]::IsNullOrWhiteSpace($logParent)) {
        New-Item -ItemType Directory -Force -Path $logParent | Out-Null
    }

    if (-not (Test-Path $LogFile)) {
        New-Item -ItemType File -Path $LogFile | Out-Null
    }
}

if (-not [string]::IsNullOrWhiteSpace($DbcFile)) {
    if (-not (Test-Path -LiteralPath $DbcFile -PathType Leaf)) {
        throw "DBC file not found: $DbcFile"
    }

    $DbcFile = (Resolve-Path -LiteralPath $DbcFile).Path

    if ([string]::IsNullOrWhiteSpace($DecodedLogFile)) {
        $logDirectory = Split-Path -Parent $LogFile
        $logBaseName = [System.IO.Path]::GetFileNameWithoutExtension($LogFile)
        if ([string]::IsNullOrWhiteSpace($logDirectory)) {
            $DecodedLogFile = "{0}_decoded.csv" -f $logBaseName
        } else {
            $DecodedLogFile = Join-Path $logDirectory ("{0}_decoded.csv" -f $logBaseName)
        }
    }

    $decodedLogParent = Split-Path -Parent $DecodedLogFile
    if (-not [string]::IsNullOrWhiteSpace($decodedLogParent)) {
        New-Item -ItemType Directory -Force -Path $decodedLogParent | Out-Null
    }
}

$preflight = @(
    "import can",
    "import serial",
    "import can.util",
    "print(can.__version__)",
    "print(serial.__version__)"
) -join "; "

if (-not [string]::IsNullOrWhiteSpace($DbcFile)) {
    $preflight = @(
        "import can",
        "import serial",
        "import can.util",
        "import cantools",
        "print(can.__version__)",
        "print(serial.__version__)",
        "print(cantools.__version__)"
    ) -join "; "
}

try {
    $versions = python -c $preflight
} catch {
    if (-not [string]::IsNullOrWhiteSpace($DbcFile)) {
        throw "Python preflight failed. Ensure 'python', 'python-can', 'pyserial', and 'cantools' are installed."
    }
    throw "Python preflight failed. Ensure 'python', 'python-can', and 'pyserial' are installed."
}

if ($LASTEXITCODE -ne 0) {
    if (-not [string]::IsNullOrWhiteSpace($DbcFile)) {
        throw "Python preflight failed. Ensure 'python', 'python-can', 'pyserial', and 'cantools' are installed."
    }
    throw "Python preflight failed. Ensure 'python', 'python-can', and 'pyserial' are installed."
}

$pythonCanVersion = $versions[0]
$pyserialVersion = $versions[1]

Write-Host "python-can version: $pythonCanVersion"
Write-Host "pyserial version: $pyserialVersion"
if (-not [string]::IsNullOrWhiteSpace($DbcFile)) {
    $cantoolsVersion = $versions[2]
    Write-Host "cantools version: $cantoolsVersion"
}
Write-Host "Port: COM$Port"
Write-Host "Serial baudrate: $SerialBaudrate"
Write-Host "CAN bitrate: $CanBitrate"
if ($DataBitrate -gt 0) {
    Write-Host "CAN FD data bitrate: $DataBitrate"
}
if ($usingPlayback) {
    Write-Host "Mode: playback transmit"
    Write-Host "Playback log file: $PlaybackLogFile"
    Write-Host "Playback time scale: $PlaybackTimeScale"
    Write-Host "Playback repetitions: $(if ($PlaybackCount -eq 0) { 'infinite' } else { $PlaybackCount })"
} elseif ($usingFixedTx) {
    Write-Host "Mode: fixed-message transmit"
    Write-Host "Tx ID: $TxId"
    Write-Host "Tx data: $TxData"
    Write-Host "Tx interval: $TxIntervalMs ms"
    Write-Host "Tx repetitions: $(if ($TxCount -eq 0) { 'infinite' } else { $TxCount })"
} else {
    Write-Host "Mode: $(if ($ListenOnly) { 'silent/listen-only' } else { 'normal' })"
    Write-Host "Log file: $LogFile"
}
if (-not [string]::IsNullOrWhiteSpace($DbcFile)) {
    Write-Host "DBC file: $DbcFile"
    Write-Host "Decoded signal log file: $DecodedLogFile"
}
Write-Host "Starting direct SLCAN tool..."

$pythonArgs = @(
    "--port", $Port,
    "--serial-baudrate", $SerialBaudrate,
    "--can-bitrate", $CanBitrate,
    "--data-bitrate", $DataBitrate,
    "--init-delay", $InitDelaySeconds,
    "--read-timeout", $ReadTimeoutSeconds,
    "--duration", $DurationSeconds
)

if (-not $usingTransmit) {
    $pythonArgs += @("--log-file", $LogFile)
}
if ($usingPlayback) {
    $pythonArgs += @("--playback-log-file", $PlaybackLogFile, "--playback-time-scale", $PlaybackTimeScale, "--playback-count", $PlaybackCount)
}
if ($usingFixedTx) {
    $pythonArgs += @("--tx-id", $TxId, "--tx-data", $TxData, "--tx-interval-ms", $TxIntervalMs, "--tx-count", $TxCount)
}
if ($TxExtendedId) {
    $pythonArgs += "--tx-extended-id"
}
if (-not [string]::IsNullOrWhiteSpace($DbcFile)) {
    $pythonArgs += @("--dbc-file", $DbcFile, "--decoded-log-file", $DecodedLogFile)
}
if ($ListenOnly) {
    $pythonArgs += "--listen-only"
}
if ($Append) {
    $pythonArgs += "--append"
}

@'
from __future__ import annotations

import argparse
import csv
import time
from pathlib import Path

import can
import can.util
import serial


BITRATE_COMMANDS = {
    10000: "S0",
    20000: "S1",
    50000: "S2",
    100000: "S3",
    125000: "S4",
    250000: "S5",
    500000: "S6",
    750000: "S7",
    1000000: "S8",
    83300: "S9",
}

DATA_BITRATE_COMMANDS = {
    0: None,
    2000000: "Y2",
    5000000: "Y5",
}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Capture or transmit CAN traffic through a CANable2-style SLCAN device.")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--serial-baudrate", type=int, required=True)
    parser.add_argument("--can-bitrate", type=int, required=True)
    parser.add_argument("--data-bitrate", type=int, required=True)
    parser.add_argument("--log-file", default="")
    parser.add_argument("--init-delay", type=float, required=True)
    parser.add_argument("--read-timeout", type=float, required=True)
    parser.add_argument("--duration", type=float, required=True)
    parser.add_argument("--dbc-file", default="")
    parser.add_argument("--decoded-log-file", default="")
    parser.add_argument("--playback-log-file", default="")
    parser.add_argument("--playback-time-scale", type=float, default=1.0)
    parser.add_argument("--playback-count", type=int, default=1)
    parser.add_argument("--tx-id", default="")
    parser.add_argument("--tx-data", default="")
    parser.add_argument("--tx-interval-ms", type=float, default=1000.0)
    parser.add_argument("--tx-count", type=int, default=1)
    parser.add_argument("--tx-extended-id", action="store_true")
    parser.add_argument("--listen-only", action="store_true")
    parser.add_argument("--append", action="store_true")
    return parser


def read_line(ser: serial.Serial, timeout: float) -> str | None:
    deadline = time.time() + timeout
    buffer = bytearray()
    while time.time() < deadline:
        byte = ser.read(1)
        if not byte:
            continue
        if byte == b"\r":
            return buffer.decode("ascii", errors="replace")
        buffer.extend(byte)
    return None


def read_lines_nonblocking(ser: serial.Serial, buffer: bytearray) -> list[str]:
    waiting = ser.in_waiting
    if waiting <= 0:
        return []

    chunk = ser.read(waiting)
    if not chunk:
        return []

    buffer.extend(chunk)
    lines: list[str] = []

    while True:
        try:
            terminator_index = buffer.index(0x0D)
        except ValueError:
            break

        line_bytes = bytes(buffer[:terminator_index])
        del buffer[: terminator_index + 1]
        lines.append(line_bytes.decode("ascii", errors="replace"))

    return lines


def send_command(ser: serial.Serial, command: str) -> None:
    ser.write(command.encode("ascii") + b"\r")
    ser.flush()


def flush_serial_buffers(ser: serial.Serial, buffer: bytearray) -> None:
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    buffer.clear()


def parse_slcan_line(line: str, timestamp: float) -> can.Message | None:
    if not line:
        return None

    frame_type = line[0]
    if frame_type not in ("t", "T", "r", "R", "d", "D", "b", "B"):
        return None

    is_extended = frame_type in ("T", "R", "D", "B")
    is_remote = frame_type in ("r", "R")
    is_fd = frame_type in ("d", "D", "b", "B")
    bitrate_switch = frame_type in ("b", "B")

    if is_extended:
        arbitration_id = int(line[1:9], 16)
        dlc_char_index = 9
        data_start = 10
    else:
        arbitration_id = int(line[1:4], 16)
        dlc_char_index = 4
        data_start = 5

    dlc_value = int(line[dlc_char_index], 16)
    data_length = can.util.dlc2len(dlc_value) if is_fd else dlc_value

    payload = bytearray()
    if not is_remote:
        payload_hex = line[data_start : data_start + data_length * 2]
        payload = bytearray.fromhex(payload_hex) if payload_hex else bytearray()

    return can.Message(
        timestamp=timestamp,
        arbitration_id=arbitration_id,
        is_extended_id=is_extended,
        is_remote_frame=is_remote,
        is_fd=is_fd,
        bitrate_switch=bitrate_switch,
        dlc=data_length,
        data=payload,
    )


def parse_hex_int(value: str, label: str) -> int:
    cleaned = value.strip()
    if cleaned.lower().startswith("0x"):
        cleaned = cleaned[2:]
    if not cleaned:
        raise ValueError(f"{label} must not be empty.")
    return int(cleaned, 16)


def parse_tx_data(value: str) -> bytearray:
    cleaned = value.strip()
    if not cleaned:
        return bytearray()

    for separator in (" ", ",", ":", "-", "_"):
        cleaned = cleaned.replace(separator, "")
    if len(cleaned) % 2 != 0:
        raise ValueError("Tx data must contain an even number of hex digits.")
    return bytearray.fromhex(cleaned)


def build_fixed_message(tx_id: str, tx_data: str, tx_extended_id: bool, data_bitrate: int) -> can.Message:
    arbitration_id = parse_hex_int(tx_id, "Tx ID")
    max_id = 0x1FFFFFFF if tx_extended_id else 0x7FF
    if arbitration_id < 0 or arbitration_id > max_id:
        id_type = "extended" if tx_extended_id else "standard"
        raise ValueError(f"Tx ID 0x{arbitration_id:X} is outside the {id_type} CAN ID range.")

    data = parse_tx_data(tx_data)
    if len(data) > 64:
        raise ValueError("Tx data must be 64 bytes or less.")
    if len(data) > 8 and data_bitrate <= 0:
        raise ValueError("Tx data longer than 8 bytes requires -DataBitrate for CAN FD.")

    return can.Message(
        arbitration_id=arbitration_id,
        is_extended_id=tx_extended_id,
        is_fd=len(data) > 8 or data_bitrate > 0,
        bitrate_switch=data_bitrate > 0,
        dlc=len(data),
        data=data,
    )


def encode_slcan_message(msg: can.Message) -> str:
    if msg.is_error_frame:
        raise ValueError("SLCAN transmit does not support error frames.")

    data = bytes(msg.data)
    if len(data) > 64:
        raise ValueError("SLCAN transmit data must be 64 bytes or less.")

    if msg.is_fd:
        frame_type = "B" if msg.is_extended_id and msg.bitrate_switch else "D" if msg.is_extended_id else "b" if msg.bitrate_switch else "d"
        dlc = can.util.len2dlc(len(data))
    elif msg.is_remote_frame:
        if len(data) > 8:
            raise ValueError("Classic remote frames cannot be longer than 8 bytes.")
        frame_type = "R" if msg.is_extended_id else "r"
        dlc = msg.dlc if msg.dlc is not None else len(data)
    else:
        if len(data) > 8:
            raise ValueError("Classic CAN frames cannot be longer than 8 bytes.")
        frame_type = "T" if msg.is_extended_id else "t"
        dlc = len(data)

    arbitration_id = f"{msg.arbitration_id:08X}" if msg.is_extended_id else f"{msg.arbitration_id:03X}"
    data_hex = "" if msg.is_remote_frame else data.hex().upper()
    return f"{frame_type}{arbitration_id}{dlc:X}{data_hex}"


def send_slcan_message(ser: serial.Serial, msg: can.Message) -> None:
    send_command(ser, encode_slcan_message(msg))


def iter_playback_messages(path: Path) -> list[can.Message]:
    with path.open("r", encoding="ascii", errors="replace") as handle:
        return list(can.ASCReader(handle))


def transmit_fixed_message(
    ser: serial.Serial,
    msg: can.Message,
    interval_seconds: float,
    count: int,
    duration: float,
) -> int:
    sent_count = 0
    started = time.time()
    while count == 0 or sent_count < count:
        if duration > 0 and time.time() - started >= duration:
            break

        send_slcan_message(ser, msg)
        sent_count += 1
        if sent_count <= 10 or sent_count % 100 == 0:
            print(f"{sent_count:05d}: sent {msg}")

        if interval_seconds > 0:
            time.sleep(interval_seconds)
    return sent_count


def transmit_playback(
    ser: serial.Serial,
    messages: list[can.Message],
    time_scale: float,
    count: int,
    duration: float,
) -> int:
    if not messages:
        print("Playback log contains no CAN frames.")
        return 0

    sent_count = 0
    started = time.time()
    repetition = 0

    while count == 0 or repetition < count:
        previous_timestamp = None
        for msg in messages:
            if duration > 0 and time.time() - started >= duration:
                return sent_count

            if previous_timestamp is not None:
                delay = max(0.0, (msg.timestamp - previous_timestamp) / time_scale)
                if delay > 0:
                    time.sleep(delay)
            previous_timestamp = msg.timestamp

            send_slcan_message(ser, msg)
            sent_count += 1
            if sent_count <= 10 or sent_count % 100 == 0:
                print(f"{sent_count:05d}: sent {msg}")

        repetition += 1

    return sent_count


def configure_slcan_device(args: argparse.Namespace, ser: serial.Serial, buffer: bytearray, mode_command: str) -> None:
    time.sleep(args.init_delay)
    flush_serial_buffers(ser, buffer)

    send_command(ser, "C")
    time.sleep(0.1)
    send_command(ser, "V")
    version = read_line(ser, 1.0)
    if version:
        print(f"Firmware: {version}")

    send_command(ser, BITRATE_COMMANDS[args.can_bitrate])
    if DATA_BITRATE_COMMANDS[args.data_bitrate]:
        send_command(ser, DATA_BITRATE_COMMANDS[args.data_bitrate])
    send_command(ser, mode_command)
    send_command(ser, "O")
    time.sleep(0.1)
    flush_serial_buffers(ser, buffer)


DECODED_CSV_FIELDS = [
    "timestamp_absolute",
    "timestamp_relative",
    "channel",
    "frame_id_hex",
    "frame_id_dec",
    "message_name",
    "signal_name",
    "value",
    "unit",
]


def load_dbc_database(dbc_file: str):
    if not dbc_file:
        return None

    import cantools

    return cantools.database.load_file(dbc_file)


def open_decoded_writer(decoded_log_file: str, append: bool):
    if not decoded_log_file:
        return None, None

    decoded_log_path = Path(decoded_log_file)
    decoded_log_path.parent.mkdir(parents=True, exist_ok=True)
    file_exists = decoded_log_path.exists()
    handle = decoded_log_path.open("a" if append else "w", newline="", encoding="utf-8")
    writer = csv.DictWriter(handle, fieldnames=DECODED_CSV_FIELDS)
    if not append or not file_exists or decoded_log_path.stat().st_size == 0:
        writer.writeheader()
    return handle, writer


def decode_message_to_csv_rows(database, msg: can.Message, relative_timestamp: float) -> tuple[list[dict[str, object]], str | None]:
    if database is None:
        return [], None
    if msg.is_remote_frame or msg.is_error_frame:
        return [], None

    try:
        dbc_message = database.get_message_by_frame_id(msg.arbitration_id)
    except KeyError:
        return [], "unmatched"

    try:
        decoded_signals = dbc_message.decode(bytes(msg.data), decode_choices=True)
    except Exception:
        return [], "decode_error"

    units = {signal.name: signal.unit or "" for signal in dbc_message.signals}
    rows: list[dict[str, object]] = []
    for signal_name, value in decoded_signals.items():
        rows.append(
            {
                "timestamp_absolute": f"{msg.timestamp:.6f}",
                "timestamp_relative": f"{relative_timestamp:.6f}",
                "channel": 1,
                "frame_id_hex": f"0x{msg.arbitration_id:X}",
                "frame_id_dec": msg.arbitration_id,
                "message_name": dbc_message.name,
                "signal_name": signal_name,
                "value": value,
                "unit": units.get(signal_name, ""),
            }
        )
    return rows, None


def main() -> int:
    args = build_parser().parse_args()

    if args.can_bitrate not in BITRATE_COMMANDS:
        raise SystemExit(f"Unsupported CAN bitrate: {args.can_bitrate}")
    if args.data_bitrate not in DATA_BITRATE_COMMANDS:
        raise SystemExit(f"Unsupported CAN FD data bitrate: {args.data_bitrate}")

    using_playback = bool(args.playback_log_file)
    using_fixed_tx = bool(args.tx_id)
    using_transmit = using_playback or using_fixed_tx

    if using_playback and using_fixed_tx:
        raise SystemExit("Use either playback transmit or fixed-message transmit, not both.")
    if using_transmit and args.listen_only:
        raise SystemExit("Transmit mode cannot use listen-only mode.")
    if using_transmit and (args.dbc_file or args.decoded_log_file):
        raise SystemExit("DBC decoding is only available in receive logging mode.")

    fixed_tx_message = None
    playback_messages: list[can.Message] = []
    if using_fixed_tx:
        try:
            fixed_tx_message = build_fixed_message(args.tx_id, args.tx_data, args.tx_extended_id, args.data_bitrate)
        except ValueError as exc:
            raise SystemExit(str(exc)) from exc
    elif using_playback:
        playback_messages = iter_playback_messages(Path(args.playback_log_file))

    database = None
    decoded_log_handle = None
    decoded_writer = None
    if not using_transmit:
        database = load_dbc_database(args.dbc_file)
        decoded_log_handle, decoded_writer = open_decoded_writer(args.decoded_log_file, args.append)
    if database is not None:
        print(f"Loaded DBC: {args.dbc_file}")
        print(f"Decoded signal log: {args.decoded_log_file}")

    log_path = Path(args.log_file) if args.log_file else None
    if not using_transmit and log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        if not args.append:
            log_path.write_text("", encoding="ascii")

    port_name = f"COM{args.port}"
    mode_command = "M0" if using_transmit else "M1" if args.listen_only else "M0"

    print(f"Opening {port_name} at {args.serial_baudrate} baud")
    ser = serial.Serial(
        port_name,
        args.serial_baudrate,
        timeout=args.read_timeout,
        write_timeout=1,
    )

    logger = None
    frame_count = 0
    sent_count = 0
    decoded_frame_count = 0
    decoded_signal_row_count = 0
    unmatched_frame_count = 0
    decode_error_count = 0
    buffer = bytearray()
    capture_started = None

    try:
        configure_slcan_device(args, ser, buffer, mode_command)

        if using_fixed_tx:
            print("Fixed-message transmit started. Press Ctrl+C to stop.")
            sent_count = transmit_fixed_message(
                ser,
                fixed_tx_message,
                args.tx_interval_ms / 1000.0,
                args.tx_count,
                args.duration,
            )
        elif using_playback:
            print("Playback transmit started. Press Ctrl+C to stop.")
            sent_count = transmit_playback(
                ser,
                playback_messages,
                args.playback_time_scale,
                args.playback_count,
                args.duration,
            )
        else:
            logger = can.Logger(str(log_path))
            print("Capture started. Press Ctrl+C to stop.")
            capture_started = time.time()

            while True:
                if (
                    args.duration > 0
                    and capture_started is not None
                    and (time.time() - capture_started) >= args.duration
                ):
                    break

                lines = read_lines_nonblocking(ser, buffer)
                if not lines:
                    time.sleep(0.01)
                    continue

                for line in lines:
                    msg = parse_slcan_line(line, time.time())
                    if msg is None:
                        print(f"Device: {line}")
                        continue

                    logger.on_message_received(msg)
                    frame_count += 1
                    if database is not None and decoded_writer is not None and capture_started is not None:
                        relative_timestamp = msg.timestamp - capture_started
                        decoded_rows, decode_status = decode_message_to_csv_rows(database, msg, relative_timestamp)
                        if decode_status == "unmatched":
                            unmatched_frame_count += 1
                        elif decode_status == "decode_error":
                            decode_error_count += 1
                        elif decoded_rows:
                            decoded_writer.writerows(decoded_rows)
                            decoded_frame_count += 1
                            decoded_signal_row_count += len(decoded_rows)

                    if frame_count <= 10 or frame_count % 100 == 0:
                        print(f"{frame_count:05d}: {msg}")

    except KeyboardInterrupt:
        if using_transmit:
            print("Transmit interrupted by user.")
        else:
            print("Capture interrupted by user.")
    finally:
        try:
            send_command(ser, "C")
            time.sleep(0.1)
            send_command(ser, "E")
            status = read_line(ser, 0.5)
            if status:
                print(f"Status: {status}")
        except Exception as exc:
            print(f"Status query failed: {type(exc).__name__}: {exc}")

        if logger is not None:
            try:
                logger.stop()
            except Exception as exc:
                print(f"Logger stop failed: {type(exc).__name__}: {exc}")

        if decoded_log_handle is not None:
            decoded_log_handle.close()

        ser.close()

    if using_transmit:
        print(f"Sent {sent_count} frames")
        return 0

    print(f"Captured {frame_count} frames")
    if database is not None:
        print(f"Decoded frames: {decoded_frame_count}")
        print(f"Decoded signal rows: {decoded_signal_row_count}")
        print(f"Unmatched frames: {unmatched_frame_count}")
        print(f"Decode errors: {decode_error_count}")
        print(args.decoded_log_file)
    print(log_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
'@ | python - @pythonArgs

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
