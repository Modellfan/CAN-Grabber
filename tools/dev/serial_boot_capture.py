import argparse
import datetime as dt
import sys
import time
from pathlib import Path

import serial


def timestamp() -> str:
    return dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def default_output_path() -> Path:
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    return Path("logs") / f"serial_boot_{stamp}.log"


def open_serial(port: str, baudrate: int, timeout: float) -> serial.Serial:
    ser = serial.Serial(port=port, baudrate=baudrate, timeout=timeout, write_timeout=1)
    ser.dtr = False
    ser.rts = False
    return ser


def write_log(handle, message: str) -> None:
    handle.write(message + "\n")
    handle.flush()
    safe = message.encode("utf-8", errors="replace").decode("utf-8", errors="replace")
    print(safe)


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    parser = argparse.ArgumentParser(
        description="Open a serial console, send reset, and capture boot logs."
    )
    parser.add_argument("--port", default="COM15")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=45.0)
    parser.add_argument("--timeout", type=float, default=0.25)
    parser.add_argument("--command", default="reset")
    parser.add_argument("--start-delay", type=float, default=1.0)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    output_path = args.output or default_output_path()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    ser = None
    reset_sent = False
    reset_sent_at = 0.0
    started_at = time.monotonic()
    deadline = started_at + args.duration

    with output_path.open("w", encoding="utf-8") as log_handle:
        write_log(
            log_handle,
            f"{timestamp()} | opening {args.port} at {args.baud} baud, duration={args.duration:.1f}s",
        )

        while time.monotonic() < deadline:
            if ser is None:
                try:
                    ser = open_serial(args.port, args.baud, args.timeout)
                    ser.reset_input_buffer()
                    ser.reset_output_buffer()
                    write_log(log_handle, f"{timestamp()} | serial connected")
                    time.sleep(args.start_delay)
                except serial.SerialException as exc:
                    write_log(log_handle, f"{timestamp()} | serial open failed: {exc}")
                    time.sleep(0.5)
                    continue

            if not reset_sent:
                try:
                    payload = (args.command.strip() + "\n").encode("utf-8")
                    ser.write(payload)
                    ser.flush()
                    reset_sent = True
                    reset_sent_at = time.monotonic()
                    write_log(log_handle, f"{timestamp()} | sent command: {args.command.strip()}")
                except serial.SerialException as exc:
                    write_log(log_handle, f"{timestamp()} | serial write failed: {exc}")
                    try:
                        ser.close()
                    except serial.SerialException:
                        pass
                    ser = None
                    time.sleep(0.5)
                    continue

            try:
                raw = ser.readline()
                if raw:
                    line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                    write_log(log_handle, f"{timestamp()} | {line}")
                    continue
            except serial.SerialException as exc:
                write_log(log_handle, f"{timestamp()} | serial read failed: {exc}")
                try:
                    ser.close()
                except serial.SerialException:
                    pass
                ser = None
                time.sleep(0.5)
                continue

            if reset_sent and (time.monotonic() - reset_sent_at) > args.duration:
                break

        if ser is not None:
            try:
                ser.close()
            except serial.SerialException:
                pass

        write_log(log_handle, f"{timestamp()} | log saved to {output_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
