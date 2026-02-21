#!/usr/bin/env python3
"""
Small HTTP tester for sd_http_upload_ui_test web interface.

Typical use from PowerShell:
  python tools/upload_ui_tester.py --base-url http://192.168.3.50 --start --timeout 180
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Dict
from urllib.error import URLError, HTTPError
from urllib.request import build_opener, ProxyHandler


@dataclass
class RunResult:
    state: str
    error: int
    sent: int
    total: int
    rate: int
    elapsed_s: float
    samples: int
    log_path: Path
    summary_path: Path


_NO_PROXY_OPENER = build_opener(ProxyHandler({}))


def http_get_json(url: str, timeout_s: float) -> Dict[str, Any]:
    with _NO_PROXY_OPENER.open(url, timeout=timeout_s) as resp:
        data = resp.read().decode("utf-8", errors="replace")
    return json.loads(data)


def http_get_text(url: str, timeout_s: float) -> str:
    with _NO_PROXY_OPENER.open(url, timeout=timeout_s) as resp:
        return resp.read().decode("utf-8", errors="replace")


def poll_run(
    base_url: str,
    timeout_s: float,
    interval_s: float,
    req_timeout_s: float,
    log_path: Path,
) -> RunResult:
    start_t = time.time()
    samples = 0
    last: Dict[str, Any] = {"state": "UNKNOWN", "error": -999, "sent": 0, "total": 0, "rate": 0}

    with log_path.open("w", encoding="utf-8") as f:
        while True:
            now = time.time()
            elapsed = now - start_t
            if elapsed > timeout_s:
                last["state"] = "ERROR"
                last["error"] = -1000
                break

            try:
                status = http_get_json(f"{base_url}/status", req_timeout_s)
                status["ts"] = datetime.utcnow().isoformat(timespec="milliseconds") + "Z"
                f.write(json.dumps(status, separators=(",", ":")) + "\n")
                f.flush()
                samples += 1
                last = status
                state = str(status.get("state", "UNKNOWN")).upper()
                rate = int(status.get("rate", 0))
                sent = int(status.get("sent", 0))
                total = int(status.get("total", 0))
                print(
                    f"\rstate={state:11s} sent={sent:10d}/{total:10d} rate={rate:9d} B/s",
                    end="",
                    flush=True,
                )
                if state in ("DONE", "ERROR"):
                    print()
                    break
            except (URLError, HTTPError, json.JSONDecodeError, TimeoutError) as exc:
                err = {"ts": datetime.utcnow().isoformat(timespec="milliseconds") + "Z", "poll_error": str(exc)}
                f.write(json.dumps(err, separators=(",", ":")) + "\n")
                f.flush()

            time.sleep(interval_s)

    return RunResult(
        state=str(last.get("state", "UNKNOWN")),
        error=int(last.get("error", -1)),
        sent=int(last.get("sent", 0)),
        total=int(last.get("total", 0)),
        rate=int(last.get("rate", 0)),
        elapsed_s=time.time() - start_t,
        samples=samples,
        log_path=log_path,
        summary_path=log_path.with_suffix(".summary.txt"),
    )


def write_summary(result: RunResult) -> None:
    mb_sent = result.sent / (1024.0 * 1024.0)
    mb_total = result.total / (1024.0 * 1024.0) if result.total > 0 else 0.0
    avg_mb_s = (mb_sent / result.elapsed_s) if result.elapsed_s > 0 else 0.0
    text = (
        f"result_state: {result.state}\n"
        f"error_code: {result.error}\n"
        f"sent_bytes: {result.sent}\n"
        f"total_bytes: {result.total}\n"
        f"last_rate_bps: {result.rate}\n"
        f"elapsed_s: {result.elapsed_s:.3f}\n"
        f"samples: {result.samples}\n"
        f"sent_mb: {mb_sent:.3f}\n"
        f"total_mb: {mb_total:.3f}\n"
        f"avg_upload_mb_s: {avg_mb_s:.3f}\n"
        f"log_path: {result.log_path}\n"
    )
    result.summary_path.write_text(text, encoding="utf-8")
    print(text)
    print(f"summary_path: {result.summary_path}")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Poll upload UI test webinterface and log a run.")
    p.add_argument("--base-url", default="http://192.168.4.1", help="ESP web base URL (no trailing slash)")
    p.add_argument("--start", action="store_true", help="Call /start before polling")
    p.add_argument("--check-only", action="store_true", help="Only verify webservice endpoints and exit")
    p.add_argument("--timeout", type=float, default=240.0, help="Max run time in seconds")
    p.add_argument("--interval-ms", type=int, default=250, help="Poll interval in milliseconds")
    p.add_argument("--request-timeout", type=float, default=2.0, help="Single HTTP request timeout in seconds")
    p.add_argument("--log-dir", default="logs", help="Output log directory")
    p.add_argument("--prefix", default="upload_ui_test", help="Output filename prefix")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    base = args.base_url.rstrip("/")
    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = log_dir / f"{args.prefix}_{stamp}.jsonl"

    if args.check_only:
      try:
          status = http_get_json(f"{base}/status", args.request_timeout)
          root = http_get_text(f"{base}/", args.request_timeout)
      except Exception as exc:  # noqa: BLE001
          print(f"check_failed: {exc}", file=sys.stderr)
          return 2
      print(f"check_ok: status_state={status.get('state')} status_error={status.get('error')} root_len={len(root)}")
      return 0

    if args.start:
        try:
            reply = http_get_text(f"{base}/start", args.request_timeout).strip()
            print(f"start_reply: {reply}")
        except Exception as exc:  # noqa: BLE001
            print(f"failed_to_start: {exc}", file=sys.stderr)
            return 2

    result = poll_run(
        base_url=base,
        timeout_s=args.timeout,
        interval_s=max(0.01, args.interval_ms / 1000.0),
        req_timeout_s=args.request_timeout,
        log_path=log_path,
    )
    write_summary(result)

    if result.state.upper() == "DONE":
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
