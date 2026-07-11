from __future__ import annotations

import argparse
import csv
import json
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable

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

TOOL_ROOT = Path(__file__).resolve().parent
LOG_DIR = TOOL_ROOT / "logs"
PROFILES_PATH = TOOL_ROOT / "profiles.json"
PROFILE_DEFAULTS = {
    "port": 22,
    "serial_baudrate": 115200,
    "can_bitrate": 500000,
    "data_bitrate": 0,
    "init_delay": 2.0,
    "read_timeout": 0.2,
    "duration": 0.0,
}
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


@dataclass(frozen=True)
class IdFilter:
    start: int
    end: int

    def matches(self, arbitration_id: int) -> bool:
        return self.start <= arbitration_id <= self.end


@dataclass(frozen=True)
class EchoRule:
    match_id: int
    match_data_prefix: bytes
    extended: bool | None
    response: can.Message
    delay_ms: float


class SummaryStats:
    def __init__(self) -> None:
        self.frame_count = 0
        self.first_timestamp: float | None = None
        self.last_timestamp: float | None = None
        self.per_id: dict[str, dict[str, Any]] = {}
        self.signals: dict[str, dict[str, Any]] = {}

    def add_frame(self, msg: can.Message) -> None:
        self.frame_count += 1
        timestamp = float(msg.timestamp)
        self.first_timestamp = timestamp if self.first_timestamp is None else min(self.first_timestamp, timestamp)
        self.last_timestamp = timestamp if self.last_timestamp is None else max(self.last_timestamp, timestamp)
        key = frame_key(msg)
        stat = self.per_id.setdefault(
            key,
            {
                "frame_id_hex": f"0x{msg.arbitration_id:X}",
                "frame_id_dec": msg.arbitration_id,
                "extended": msg.is_extended_id,
                "count": 0,
                "first_timestamp": None,
                "last_timestamp": None,
                "last_seen": None,
                "period_min": None,
                "period_max": None,
                "period_sum": 0.0,
                "period_count": 0,
            },
        )
        stat["count"] += 1
        if stat["first_timestamp"] is None:
            stat["first_timestamp"] = timestamp
        stat["last_timestamp"] = timestamp
        if stat["last_seen"] is not None:
            period = timestamp - stat["last_seen"]
            stat["period_min"] = period if stat["period_min"] is None else min(stat["period_min"], period)
            stat["period_max"] = period if stat["period_max"] is None else max(stat["period_max"], period)
            stat["period_sum"] += period
            stat["period_count"] += 1
        stat["last_seen"] = timestamp

    def add_signal_rows(self, rows: Iterable[dict[str, object]]) -> None:
        for row in rows:
            key = f"{row['message_name']}.{row['signal_name']}"
            value = row["value"]
            stat = self.signals.setdefault(
                key,
                {
                    "message_name": row["message_name"],
                    "signal_name": row["signal_name"],
                    "unit": row["unit"],
                    "count": 0,
                    "latest": None,
                    "minimum": None,
                    "maximum": None,
                },
            )
            stat["count"] += 1
            stat["latest"] = value
            if isinstance(value, (int, float)):
                stat["minimum"] = value if stat["minimum"] is None else min(stat["minimum"], value)
                stat["maximum"] = value if stat["maximum"] is None else max(stat["maximum"], value)

    def as_dict(self) -> dict[str, Any]:
        per_id = []
        for stat in sorted(self.per_id.values(), key=lambda item: item["frame_id_dec"]):
            period_count = stat["period_count"]
            per_id.append(
                {
                    "frame_id_hex": stat["frame_id_hex"],
                    "frame_id_dec": stat["frame_id_dec"],
                    "extended": stat["extended"],
                    "count": stat["count"],
                    "first_timestamp": stat["first_timestamp"],
                    "last_timestamp": stat["last_timestamp"],
                    "period_min": stat["period_min"],
                    "period_max": stat["period_max"],
                    "period_avg": stat["period_sum"] / period_count if period_count else None,
                }
            )
        return {
            "frame_count": self.frame_count,
            "first_timestamp": self.first_timestamp,
            "last_timestamp": self.last_timestamp,
            "duration": (
                self.last_timestamp - self.first_timestamp
                if self.first_timestamp is not None and self.last_timestamp is not None
                else 0.0
            ),
            "per_id": per_id,
            "signals": sorted(self.signals.values(), key=lambda item: (str(item["message_name"]), str(item["signal_name"]))),
        }


def frame_key(msg: can.Message) -> str:
    return f"{'x' if msg.is_extended_id else 's'}:{msg.arbitration_id:X}"


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


def normalize_path(value: str) -> Path:
    path = Path(value).expanduser()
    return path if path.is_absolute() else Path.cwd() / path


def default_log_path(port: int) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return LOG_DIR / f"slcan_com{port}_{timestamp}.asc"


def parse_id_filter(value: str) -> IdFilter:
    if "-" in value:
        start, end = value.split("-", 1)
        parsed = IdFilter(parse_hex_int(start, "filter start"), parse_hex_int(end, "filter end"))
        if parsed.end < parsed.start:
            raise argparse.ArgumentTypeError("filter range end must be >= start")
        return parsed
    parsed = parse_hex_int(value, "filter ID")
    return IdFilter(parsed, parsed)


def parse_rewrite_id(value: str) -> tuple[int, int]:
    if ":" not in value:
        raise argparse.ArgumentTypeError("--rewrite-id must use OLD:NEW format")
    old, new = value.split(":", 1)
    return parse_hex_int(old, "rewrite old ID"), parse_hex_int(new, "rewrite new ID")


def message_matches_filters(msg: can.Message, args: argparse.Namespace) -> bool:
    filters: list[IdFilter] = getattr(args, "filter_id", []) or []
    if filters and not any(item.matches(msg.arbitration_id) for item in filters):
        return False
    if getattr(args, "extended_only", False) and not msg.is_extended_id:
        return False
    if getattr(args, "standard_only", False) and msg.is_extended_id:
        return False
    return True


def validate_bit_rates(args: argparse.Namespace) -> None:
    if args.can_bitrate not in BITRATE_COMMANDS:
        raise SystemExit(f"Unsupported CAN bitrate: {args.can_bitrate}")
    if args.data_bitrate not in DATA_BITRATE_COMMANDS:
        raise SystemExit(f"Unsupported CAN FD data bitrate: {args.data_bitrate}")


def load_profiles() -> dict[str, dict[str, Any]]:
    if not PROFILES_PATH.exists():
        return {}
    with PROFILES_PATH.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise SystemExit(f"Profile file must contain a JSON object: {PROFILES_PATH}")
    return {str(name): dict(value) for name, value in data.items() if isinstance(value, dict)}


def apply_profile(args: argparse.Namespace, parser: argparse.ArgumentParser) -> None:
    profile_name = getattr(args, "profile", "")
    if not profile_name:
        return
    profiles = load_profiles()
    if profile_name not in profiles:
        raise SystemExit(f"Profile not found: {profile_name}")
    for key, value in profiles[profile_name].items():
        attr = key.replace("-", "_")
        if not hasattr(args, attr):
            continue
        if getattr(args, attr) == PROFILE_DEFAULTS.get(attr):
            setattr(args, attr, value)


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
        raise ValueError("Tx data longer than 8 bytes requires --data-bitrate for CAN FD.")
    return can.Message(
        arbitration_id=arbitration_id,
        is_extended_id=tx_extended_id,
        is_fd=len(data) > 8 or data_bitrate > 0,
        bitrate_switch=data_bitrate > 0,
        dlc=len(data),
        data=data,
    )


def encode_slcan_message(msg: can.Message) -> str:
    data = bytes(msg.data)
    if msg.is_error_frame:
        raise ValueError("SLCAN transmit does not support error frames.")
    if len(data) > 64:
        raise ValueError("SLCAN transmit data must be 64 bytes or less.")
    if msg.is_fd:
        if msg.is_extended_id:
            frame_type = "B" if msg.bitrate_switch else "D"
        else:
            frame_type = "b" if msg.bitrate_switch else "d"
        dlc = can.util.len2dlc(len(data))
    elif msg.is_remote_frame:
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


def send_command(ser: serial.Serial, command: str) -> None:
    ser.write(command.encode("ascii") + b"\r")
    ser.flush()


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


def configure_slcan_device(args: argparse.Namespace, ser: serial.Serial, buffer: bytearray, mode_command: str) -> None:
    time.sleep(args.init_delay)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    buffer.clear()
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
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    buffer.clear()


def open_serial(args: argparse.Namespace, mode_command: str) -> tuple[serial.Serial, bytearray]:
    port_name = f"COM{args.port}"
    print(f"Opening {port_name} at {args.serial_baudrate} baud")
    ser = serial.Serial(port_name, args.serial_baudrate, timeout=args.read_timeout, write_timeout=1)
    buffer = bytearray()
    configure_slcan_device(args, ser, buffer, mode_command)
    return ser, buffer


def close_serial(ser: serial.Serial) -> None:
    try:
        send_command(ser, "C")
        time.sleep(0.1)
        send_command(ser, "E")
        status = read_line(ser, 0.5)
        if status and status[0] not in ("t", "T", "r", "R", "d", "D", "b", "B"):
            print(f"Status: {status}")
    except Exception as exc:
        print(f"Status query failed: {type(exc).__name__}: {exc}")
    ser.close()


def send_slcan_message(ser: serial.Serial, msg: can.Message) -> None:
    send_command(ser, encode_slcan_message(msg))


def iter_asc_messages(path: Path) -> list[can.Message]:
    with path.open("r", encoding="ascii", errors="replace") as handle:
        return list(can.ASCReader(handle))


def load_dbc_database(dbc_file: str):
    if not dbc_file:
        return None
    import cantools

    return cantools.database.load_file(dbc_file)


def decode_message_to_csv_rows(database, msg: can.Message, relative_timestamp: float) -> tuple[list[dict[str, object]], str | None]:
    if database is None or msg.is_remote_frame or msg.is_error_frame:
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


def open_decoded_writer(path: Path | None, append: bool) -> tuple[Any, csv.DictWriter | None]:
    if path is None:
        return None, None
    path.parent.mkdir(parents=True, exist_ok=True)
    existed = path.exists()
    handle = path.open("a" if append else "w", newline="", encoding="utf-8")
    writer = csv.DictWriter(handle, fieldnames=DECODED_CSV_FIELDS)
    if not append or not existed or path.stat().st_size == 0:
        writer.writeheader()
    return handle, writer


def summary_paths(prefix: str | None, fallback_base: Path) -> tuple[Path, Path, Path]:
    base = normalize_path(prefix) if prefix else fallback_base
    return (
        base.with_name(f"{base.name}_summary.json"),
        base.with_name(f"{base.name}_ids.csv"),
        base.with_name(f"{base.name}_signals.csv"),
    )


def write_summary(stats: SummaryStats, prefix: str | None, fallback_base: Path) -> tuple[Path, Path, Path]:
    json_path, ids_path, signals_path = summary_paths(prefix, fallback_base)
    for path in (json_path, ids_path, signals_path):
        path.parent.mkdir(parents=True, exist_ok=True)
    data = stats.as_dict()
    json_path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    with ids_path.open("w", newline="", encoding="utf-8") as handle:
        fields = ["frame_id_hex", "frame_id_dec", "extended", "count", "first_timestamp", "last_timestamp", "period_min", "period_max", "period_avg"]
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(data["per_id"])
    with signals_path.open("w", newline="", encoding="utf-8") as handle:
        fields = ["message_name", "signal_name", "unit", "count", "latest", "minimum", "maximum"]
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(data["signals"])
    return json_path, ids_path, signals_path


def filtered_playback_messages(args: argparse.Namespace) -> list[can.Message]:
    messages = iter_asc_messages(normalize_path(args.playback_log_file))
    rewrite = dict(args.rewrite_id or [])
    filtered: list[can.Message] = []
    for msg in messages:
        if args.start_time is not None and msg.timestamp < args.start_time:
            continue
        if args.end_time is not None and msg.timestamp > args.end_time:
            continue
        if not message_matches_filters(msg, args):
            continue
        if msg.arbitration_id in rewrite:
            msg = can.Message(
                timestamp=msg.timestamp,
                arbitration_id=rewrite[msg.arbitration_id],
                is_extended_id=msg.is_extended_id,
                is_remote_frame=msg.is_remote_frame,
                is_fd=msg.is_fd,
                bitrate_switch=msg.bitrate_switch,
                dlc=msg.dlc,
                data=bytes(msg.data),
            )
        filtered.append(msg)
    return filtered


def transmit_fixed(args: argparse.Namespace) -> int:
    msg = build_fixed_message(args.tx_id, args.tx_data, args.tx_extended_id, args.data_bitrate)
    if args.dry_run:
        print(f"DRY-RUN fixed frame: {encode_slcan_message(msg)}")
        return 0
    ser, _buffer = open_serial(args, "M0")
    sent = 0
    try:
        started = time.time()
        while args.tx_count == 0 or sent < args.tx_count:
            if args.duration > 0 and time.time() - started >= args.duration:
                break
            send_slcan_message(ser, msg)
            sent += 1
            if sent <= 10 or sent % 100 == 0:
                print(f"{sent:05d}: sent {msg}")
            if args.tx_interval_ms > 0:
                time.sleep(args.tx_interval_ms / 1000.0)
    finally:
        close_serial(ser)
    print(f"Sent {sent} frames")
    return sent


def transmit_playback(args: argparse.Namespace) -> int:
    messages = filtered_playback_messages(args)
    if args.dry_run:
        print(f"DRY-RUN playback frames: {len(messages)}")
        for index, msg in enumerate(messages[:10], start=1):
            print(f"{index:05d}: {msg.timestamp:.6f}s {encode_slcan_message(msg)}")
        return 0
    ser, _buffer = open_serial(args, "M0")
    sent = 0
    try:
        started = time.time()
        repetition = 0
        while args.playback_count == 0 or repetition < args.playback_count:
            previous_timestamp = None
            for msg in messages:
                if args.duration > 0 and time.time() - started >= args.duration:
                    print(f"Sent {sent} frames")
                    return sent
                if previous_timestamp is not None:
                    delay = max(0.0, (msg.timestamp - previous_timestamp) / args.playback_time_scale)
                    if delay > 0:
                        time.sleep(delay)
                    if args.duration > 0 and time.time() - started >= args.duration:
                        print(f"Sent {sent} frames")
                        return sent
                previous_timestamp = msg.timestamp
                send_slcan_message(ser, msg)
                sent += 1
                if sent <= 10 or sent % 100 == 0:
                    print(f"{sent:05d}: sent {msg}")
            repetition += 1
            if args.loop_gap_ms > 0 and (args.playback_count == 0 or repetition < args.playback_count):
                time.sleep(args.loop_gap_ms / 1000.0)
    finally:
        close_serial(ser)
    print(f"Sent {sent} frames")
    return sent


def load_echo_rules(path: Path, data_bitrate: int) -> list[EchoRule]:
    data = json.loads(path.read_text(encoding="utf-8"))
    rules = []
    for item in data.get("rules", []):
        response = build_fixed_message(
            str(item["response_id"]),
            str(item.get("response_data", "")),
            bool(item.get("response_extended", False)),
            data_bitrate,
        )
        rules.append(
            EchoRule(
                match_id=parse_hex_int(str(item["match_id"]), "match_id"),
                match_data_prefix=bytes(parse_tx_data(str(item.get("match_data_prefix", "")))),
                extended=item.get("extended"),
                response=response,
                delay_ms=float(item.get("delay_ms", 0)),
            )
        )
    return rules


def find_echo_rule(rules: list[EchoRule], msg: can.Message) -> EchoRule | None:
    payload = bytes(msg.data)
    for rule in rules:
        if msg.arbitration_id != rule.match_id:
            continue
        if rule.extended is not None and msg.is_extended_id != rule.extended:
            continue
        if rule.match_data_prefix and not payload.startswith(rule.match_data_prefix):
            continue
        return rule
    return None


def run_echo(args: argparse.Namespace) -> dict[str, int]:
    rules = load_echo_rules(normalize_path(args.rules_file), args.data_bitrate)
    if args.dry_run:
        print(f"DRY-RUN echo rules: {len(rules)}")
        for rule in rules:
            print(f"match 0x{rule.match_id:X} -> {encode_slcan_message(rule.response)} after {rule.delay_ms} ms")
        return {"received": 0, "matched": 0, "sent": 0, "unmatched": 0}
    ser, buffer = open_serial(args, "M0")
    counts = {"received": 0, "matched": 0, "sent": 0, "unmatched": 0}
    started = time.time()
    try:
        while args.duration <= 0 or time.time() - started < args.duration:
            lines = read_lines_nonblocking(ser, buffer)
            if not lines:
                time.sleep(0.01)
                continue
            for line in lines:
                msg = parse_slcan_line(line, time.time())
                if msg is None:
                    continue
                counts["received"] += 1
                rule = find_echo_rule(rules, msg)
                if rule is None:
                    counts["unmatched"] += 1
                    continue
                counts["matched"] += 1
                if rule.delay_ms > 0:
                    time.sleep(rule.delay_ms / 1000.0)
                send_slcan_message(ser, rule.response)
                counts["sent"] += 1
    finally:
        close_serial(ser)
    print(json.dumps(counts, indent=2))
    return counts


def run_listen(args: argparse.Namespace) -> int:
    database = load_dbc_database(args.dbc_file)
    log_path = normalize_path(args.log_file) if args.log_file else default_log_path(args.port)
    decoded_path = normalize_path(args.decoded_log_file) if args.decoded_log_file else log_path.with_name(f"{log_path.stem}_decoded.csv") if args.dbc_file else None
    log_path.parent.mkdir(parents=True, exist_ok=True)
    if not args.append:
        log_path.write_text("", encoding="ascii")
    decoded_handle, decoded_writer = open_decoded_writer(decoded_path, args.append)
    stats = SummaryStats()
    ser, buffer = open_serial(args, "M1" if args.listen_only else "M0")
    logger = can.Logger(str(log_path))
    frame_count = decoded_frames = decoded_rows_count = unmatched = decode_errors = 0
    started = time.time()
    try:
        while args.duration <= 0 or time.time() - started < args.duration:
            lines = read_lines_nonblocking(ser, buffer)
            if not lines:
                time.sleep(0.01)
                continue
            for line in lines:
                msg = parse_slcan_line(line, time.time())
                if msg is None or not message_matches_filters(msg, args):
                    continue
                logger.on_message_received(msg)
                stats.add_frame(msg)
                frame_count += 1
                if database is not None:
                    rows, status = decode_message_to_csv_rows(database, msg, msg.timestamp - started)
                    if status == "unmatched":
                        unmatched += 1
                    elif status == "decode_error":
                        decode_errors += 1
                    elif rows:
                        decoded_frames += 1
                        decoded_rows_count += len(rows)
                        stats.add_signal_rows(rows)
                        if decoded_writer is not None:
                            decoded_writer.writerows(rows)
                        if args.print_decoded:
                            values = ", ".join(f"{row['signal_name']}={row['value']}{row['unit']}" for row in rows)
                            print(f"{msg.timestamp - started:.6f}s 0x{msg.arbitration_id:X} {values}")
                if frame_count <= 10 or frame_count % 100 == 0:
                    print(f"{frame_count:05d}: {msg}")
    finally:
        logger.stop()
        if decoded_handle is not None:
            decoded_handle.close()
        close_serial(ser)
    summary = write_summary(stats, args.summary_prefix, log_path.with_suffix(""))
    print(f"Captured {frame_count} frames")
    print(f"Decoded frames: {decoded_frames}")
    print(f"Decoded signal rows: {decoded_rows_count}")
    print(f"Unmatched frames: {unmatched}")
    print(f"Decode errors: {decode_errors}")
    print(log_path)
    if decoded_path is not None:
        print(decoded_path)
    for path in summary:
        print(path)
    return frame_count


def decode_offline(args: argparse.Namespace) -> int:
    database = load_dbc_database(args.dbc_file)
    messages = [msg for msg in iter_asc_messages(normalize_path(args.asc_file)) if message_matches_filters(msg, args)]
    output = normalize_path(args.output_csv) if args.output_csv else normalize_path(args.asc_file).with_name(f"{normalize_path(args.asc_file).stem}_decoded.csv")
    output.parent.mkdir(parents=True, exist_ok=True)
    stats = SummaryStats()
    decoded_frames = decoded_rows_count = unmatched = decode_errors = 0
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=DECODED_CSV_FIELDS)
        writer.writeheader()
        first_timestamp = messages[0].timestamp if messages else 0.0
        for msg in messages:
            stats.add_frame(msg)
            rows, status = decode_message_to_csv_rows(database, msg, msg.timestamp - first_timestamp)
            if status == "unmatched":
                unmatched += 1
            elif status == "decode_error":
                decode_errors += 1
            elif rows:
                decoded_frames += 1
                decoded_rows_count += len(rows)
                stats.add_signal_rows(rows)
                writer.writerows(rows)
    summary = write_summary(stats, args.summary_prefix, output.with_suffix(""))
    print(f"Decoded frames: {decoded_frames}")
    print(f"Decoded signal rows: {decoded_rows_count}")
    print(f"Unmatched frames: {unmatched}")
    print(f"Decode errors: {decode_errors}")
    print(output)
    for path in summary:
        print(path)
    return decoded_rows_count


def analyze_offline(args: argparse.Namespace) -> int:
    messages = [msg for msg in iter_asc_messages(normalize_path(args.asc_file)) if message_matches_filters(msg, args)]
    stats = SummaryStats()
    for msg in messages:
        stats.add_frame(msg)
    summary = write_summary(stats, args.summary_prefix, normalize_path(args.asc_file).with_suffix(""))
    print(f"Analyzed frames: {len(messages)}")
    for path in summary:
        print(path)
    return len(messages)


def add_common_hardware_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--profile", default="")
    parser.add_argument("--port", type=int, default=22)
    parser.add_argument("--serial-baudrate", type=int, default=115200)
    parser.add_argument("--can-bitrate", type=int, default=500000)
    parser.add_argument("--data-bitrate", type=int, default=0)
    parser.add_argument("--init-delay", type=float, default=2.0)
    parser.add_argument("--read-timeout", type=float, default=0.2)
    parser.add_argument("--duration", type=float, default=0.0)


def add_filter_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--filter-id", type=parse_id_filter, action="append", default=[])
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--extended-only", action="store_true")
    group.add_argument("--standard-only", action="store_true")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="SLCAN receive, transmit, playback, decode, and analysis tool.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    listen = subparsers.add_parser("listen", help="Receive CAN frames and write ASC logs.")
    add_common_hardware_args(listen)
    add_filter_args(listen)
    listen.add_argument("--log-file", default="")
    listen.add_argument("--append", action="store_true")
    listen.add_argument("--listen-only", action="store_true")
    listen.add_argument("--dbc-file", default="")
    listen.add_argument("--decoded-log-file", default="")
    listen.add_argument("--print-decoded", action="store_true")
    listen.add_argument("--summary-prefix", default="")
    listen.set_defaults(func=run_listen)

    tx = subparsers.add_parser("tx", help="Transmit one fixed CAN frame periodically.")
    add_common_hardware_args(tx)
    tx.add_argument("--tx-id", required=True)
    tx.add_argument("--tx-data", default="")
    tx.add_argument("--tx-interval-ms", type=float, default=1000.0)
    tx.add_argument("--tx-count", type=int, default=1)
    tx.add_argument("--tx-extended-id", action="store_true")
    tx.add_argument("--dry-run", action="store_true")
    tx.set_defaults(func=lambda args: transmit_fixed(args))

    playback = subparsers.add_parser("playback", help="Transmit frames from an ASC log.")
    add_common_hardware_args(playback)
    add_filter_args(playback)
    playback.add_argument("--playback-log-file", required=True)
    playback.add_argument("--playback-time-scale", type=float, default=1.0)
    playback.add_argument("--playback-count", type=int, default=1)
    playback.add_argument("--start-time", type=float)
    playback.add_argument("--end-time", type=float)
    playback.add_argument("--loop-gap-ms", type=float, default=0.0)
    playback.add_argument("--rewrite-id", type=parse_rewrite_id, action="append", default=[])
    playback.add_argument("--dry-run", action="store_true")
    playback.set_defaults(func=lambda args: transmit_playback(args))

    echo = subparsers.add_parser("echo", help="Respond to received frames using JSON rules.")
    add_common_hardware_args(echo)
    echo.add_argument("--rules-file", required=True)
    echo.add_argument("--dry-run", action="store_true")
    echo.set_defaults(func=run_echo)

    decode = subparsers.add_parser("decode", help="Offline ASC plus DBC decoding to tall CSV.")
    add_filter_args(decode)
    decode.add_argument("--asc-file", required=True)
    decode.add_argument("--dbc-file", required=True)
    decode.add_argument("--output-csv", default="")
    decode.add_argument("--summary-prefix", default="")
    decode.set_defaults(func=decode_offline)

    analyze = subparsers.add_parser("analyze", help="Offline ASC summary generation.")
    add_filter_args(analyze)
    analyze.add_argument("--asc-file", required=True)
    analyze.add_argument("--summary-prefix", default="")
    analyze.set_defaults(func=analyze_offline)
    return parser


def validate_args(args: argparse.Namespace) -> None:
    if hasattr(args, "can_bitrate"):
        validate_bit_rates(args)
    if getattr(args, "tx_interval_ms", 0) < 0:
        raise SystemExit("--tx-interval-ms must be 0 or greater.")
    if getattr(args, "tx_count", 1) < 0:
        raise SystemExit("--tx-count must be 0 for infinite transmit or a positive repetition count.")
    if getattr(args, "playback_time_scale", 1.0) <= 0:
        raise SystemExit("--playback-time-scale must be greater than 0.")
    if getattr(args, "playback_count", 1) < 0:
        raise SystemExit("--playback-count must be 0 for infinite playback or a positive repetition count.")
    if getattr(args, "loop_gap_ms", 0.0) < 0:
        raise SystemExit("--loop-gap-ms must be 0 or greater.")
    if getattr(args, "start_time", None) is not None and getattr(args, "end_time", None) is not None and args.end_time < args.start_time:
        raise SystemExit("--end-time must be >= --start-time.")


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    apply_profile(args, parser)
    validate_args(args)
    if hasattr(args, "port"):
        print(f"python-can version: {can.__version__}")
        print(f"pyserial version: {serial.__version__}")
        print(f"Port: COM{args.port}")
        print(f"Serial baudrate: {args.serial_baudrate}")
        print(f"CAN bitrate: {args.can_bitrate}")
        if args.data_bitrate > 0:
            print(f"CAN FD data bitrate: {args.data_bitrate}")
    args.func(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
