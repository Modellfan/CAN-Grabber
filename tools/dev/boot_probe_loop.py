import argparse
import datetime as dt
import json
import subprocess
import sys
import time
from pathlib import Path

from http_reachability_probe import extract_candidates_from_log, probe_ip


def default_run_dir() -> Path:
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    return Path("logs") / f"boot_probe_loop_{stamp}"


def dedupe_candidates(candidates: list[tuple[str, str]]) -> list[tuple[str, str]]:
    deduped: list[tuple[str, str]] = []
    seen_ips: set[str] = set()
    for label, ip in candidates:
        if ip in seen_ips:
            continue
        deduped.append((label, ip))
        seen_ips.add(ip)
    return deduped


def run_serial_capture(
    script_path: Path,
    output_path: Path,
    port: str,
    baud: int,
    duration: float,
    timeout: float,
    command: str,
    start_delay: float,
) -> subprocess.Popen:
    cmd = [
        sys.executable,
        str(script_path),
        "--port",
        port,
        "--baud",
        str(baud),
        "--duration",
        str(duration),
        "--timeout",
        str(timeout),
        "--command",
        command,
        "--start-delay",
        str(start_delay),
        "--output",
        str(output_path),
    ]
    print(f"[loop] serial capture -> {output_path}")
    return subprocess.Popen(cmd)


def run_http_probe(
    log_path: Path,
    port: int,
    timeout: float,
    attempts: int,
    interval: float,
) -> dict:
    probe_results: list[dict] = []
    final_candidates: list[tuple[str, str]] = []

    for attempt in range(1, attempts + 1):
        candidates = dedupe_candidates(extract_candidates_from_log(log_path)) if log_path.exists() else []
        if candidates:
            final_candidates = candidates

        print(f"[loop] probe candidates from {log_path.name}:")
        if not candidates:
            print("[loop]   none")
        for label, ip in candidates:
            print(f"[loop]   {label}: {ip}")

        print(f"[loop] probe attempt {attempt}/{attempts}")
        for label, ip in candidates:
            result = probe_ip(ip, port, timeout)
            result["label"] = label
            result["attempt"] = attempt
            probe_results.append(result)

            root = result["paths"].get("/", {})
            status = root.get("status")
            error = root.get("error", "")
            bytes_read = root.get("bytes_read")
            content_length = root.get("content_length")
            if root.get("ok"):
                print(f"[loop]   {ip}:80 / OK {status} bytes={bytes_read} length={content_length}")
            else:
                suffix = f" status={status}" if status is not None else ""
                byte_suffix = f" bytes={bytes_read}" if bytes_read is not None else ""
                print(f"[loop]   {ip}:80 / FAIL{suffix}{byte_suffix} error={error}")
        if attempt < attempts:
            time.sleep(interval)

    return {
        "log": str(log_path),
        "candidates": [{"label": label, "ip": ip} for label, ip in final_candidates],
        "results": probe_results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reset the device, capture boot logs, and probe webpage reachability in a loop."
    )
    parser.add_argument("--loops", type=int, default=3)
    parser.add_argument("--port", default="COM15")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--capture-duration", type=float, default=20.0)
    parser.add_argument("--serial-timeout", type=float, default=0.25)
    parser.add_argument("--command", default="reset")
    parser.add_argument("--start-delay", type=float, default=1.0)
    parser.add_argument("--probe-port", type=int, default=80)
    parser.add_argument("--probe-timeout", type=float, default=4.0)
    parser.add_argument("--probe-attempts", type=int, default=4)
    parser.add_argument("--probe-interval", type=float, default=2.0)
    parser.add_argument("--probe-start-delay", type=float, default=4.0)
    parser.add_argument("--between-loops", type=float, default=2.0)
    parser.add_argument("--run-dir", type=Path, default=None)
    args = parser.parse_args()

    run_dir = args.run_dir or default_run_dir()
    run_dir.mkdir(parents=True, exist_ok=True)

    script_dir = Path(__file__).resolve().parent
    serial_script = script_dir / "serial_boot_capture.py"

    summary: dict = {
        "started_at": dt.datetime.now().isoformat(),
        "loops": [],
    }

    for index in range(1, args.loops + 1):
        print(f"\n[loop] iteration {index}/{args.loops}")
        log_path = run_dir / f"boot_{index:02d}.log"
        capture_process = run_serial_capture(
            serial_script,
            log_path,
            args.port,
            args.baud,
            args.capture_duration,
            args.serial_timeout,
            args.command,
            args.start_delay,
        )
        loop_result = {
            "iteration": index,
        }
        time.sleep(args.probe_start_delay)
        loop_result["probe"] = run_http_probe(
            log_path,
            args.probe_port,
            args.probe_timeout,
            args.probe_attempts,
            args.probe_interval,
        )
        loop_result["serial_capture_rc"] = capture_process.wait()
        summary["loops"].append(loop_result)

        summary_path = run_dir / "summary.json"
        summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(f"[loop] summary updated: {summary_path}")

        if index < args.loops:
            time.sleep(args.between_loops)

    print(f"\n[loop] done: {run_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
