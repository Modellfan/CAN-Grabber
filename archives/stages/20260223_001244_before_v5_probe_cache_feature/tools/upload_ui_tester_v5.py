#!/usr/bin/env python3
"""
Small HTTP tester for sd_http_upload_ui_test_v5 web interface.

Typical use from PowerShell:
  python tools/upload_ui_tester_v5.py --base-url http://192.168.3.50 --start --timeout 180
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import UTC, datetime
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
    poll_attempts: int
    poll_errors: int
    poll_success_rate_pct: float
    latency_p50_ms: float
    latency_p95_ms: float
    latency_max_ms: float
    active_samples: int
    active_latency_p95_ms: float
    active_latency_max_ms: float
    log_path: Path
    summary_path: Path


def _percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    if len(values) == 1:
        return values[0]
    vals = sorted(values)
    rank = (p / 100.0) * (len(vals) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(vals) - 1)
    frac = rank - lo
    return vals[lo] + (vals[hi] - vals[lo]) * frac


def _git_output(args: list[str]) -> str:
    try:
        proc = subprocess.run(args, check=False, capture_output=True, text=True, timeout=2)
        return proc.stdout.strip()
    except Exception:  # noqa: BLE001
        return ""


def _append_report(report_md: Path, title: str, lines: list[str]) -> None:
    report_md.parent.mkdir(parents=True, exist_ok=True)
    local_ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    utc_ts = datetime.now(UTC).strftime("%Y-%m-%d %H:%M:%SZ")
    git_ref = _git_output(["git", "rev-parse", "--short", "HEAD"]) or "unknown"
    git_status = _git_output(["git", "status", "--short"])

    block = [
        f"## {title}",
        f"- Local Time: `{local_ts}`",
        f"- UTC Time: `{utc_ts}`",
        f"- Git Ref: `{git_ref}`",
        "- Changes:",
    ]
    if git_status:
        block.append("```text")
        block.append(git_status)
        block.append("```")
    else:
        block.append("  - working tree clean")
    block.extend(lines)
    block.append("")
    report_md.write_text(report_md.read_text(encoding="utf-8") + "\n".join(block), encoding="utf-8") if report_md.exists() else report_md.write_text(
        "# Upload UI Test Results\n\n" + "\n".join(block), encoding="utf-8"
    )


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
    poll_attempts = 0
    poll_errors = 0
    last: Dict[str, Any] = {"state": "UNKNOWN", "error": -999, "sent": 0, "total": 0, "rate": 0}
    poll_latencies_ms: list[float] = []
    active_latencies_ms: list[float] = []
    active_states = {"CONNECTING", "SENDING", "FINALIZING"}

    with log_path.open("w", encoding="utf-8") as f:
        while True:
            now = time.time()
            elapsed = now - start_t
            if elapsed > timeout_s:
                last["state"] = "ERROR"
                last["error"] = -1000
                break

            poll_attempts += 1
            try:
                req_start = time.perf_counter()
                status = http_get_json(f"{base_url}/status", req_timeout_s)
                poll_ms = (time.perf_counter() - req_start) * 1000.0
                poll_latencies_ms.append(poll_ms)
                status["ts"] = datetime.now(UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z")
                status["poll_ms"] = round(poll_ms, 3)
                f.write(json.dumps(status, separators=(",", ":")) + "\n")
                f.flush()
                samples += 1
                last = status
                state = str(status.get("state", "UNKNOWN")).upper()
                if state in active_states:
                    active_latencies_ms.append(poll_ms)
                rate = int(status.get("rate", 0))
                sent = int(status.get("sent", 0))
                total = int(status.get("total", 0))
                print(
                    f"\rstate={state:11s} sent={sent:10d}/{total:10d} rate={rate:9d} B/s poll={poll_ms:6.1f} ms",
                    end="",
                    flush=True,
                )
                if state in ("DONE", "ERROR"):
                    print()
                    break
            except (URLError, HTTPError, json.JSONDecodeError, TimeoutError, ConnectionResetError, OSError) as exc:
                poll_errors += 1
                err = {"ts": datetime.now(UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z"), "poll_error": str(exc)}
                f.write(json.dumps(err, separators=(",", ":")) + "\n")
                f.flush()

            time.sleep(interval_s)

    success_rate = ((samples / poll_attempts) * 100.0) if poll_attempts > 0 else 0.0
    return RunResult(
        state=str(last.get("state", "UNKNOWN")),
        error=int(last.get("error", -1)),
        sent=int(last.get("sent", 0)),
        total=int(last.get("total", 0)),
        rate=int(last.get("rate", 0)),
        elapsed_s=time.time() - start_t,
        samples=samples,
        poll_attempts=poll_attempts,
        poll_errors=poll_errors,
        poll_success_rate_pct=success_rate,
        latency_p50_ms=_percentile(poll_latencies_ms, 50.0),
        latency_p95_ms=_percentile(poll_latencies_ms, 95.0),
        latency_max_ms=max(poll_latencies_ms) if poll_latencies_ms else 0.0,
        active_samples=len(active_latencies_ms),
        active_latency_p95_ms=_percentile(active_latencies_ms, 95.0),
        active_latency_max_ms=max(active_latencies_ms) if active_latencies_ms else 0.0,
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
        f"samples_ok: {result.samples}\n"
        f"poll_attempts: {result.poll_attempts}\n"
        f"poll_errors: {result.poll_errors}\n"
        f"poll_success_rate_pct: {result.poll_success_rate_pct:.2f}\n"
        f"latency_p50_ms: {result.latency_p50_ms:.2f}\n"
        f"latency_p95_ms: {result.latency_p95_ms:.2f}\n"
        f"latency_max_ms: {result.latency_max_ms:.2f}\n"
        f"active_samples_ok: {result.active_samples}\n"
        f"active_latency_p95_ms: {result.active_latency_p95_ms:.2f}\n"
        f"active_latency_max_ms: {result.active_latency_max_ms:.2f}\n"
        f"sent_mb: {mb_sent:.3f}\n"
        f"total_mb: {mb_total:.3f}\n"
        f"avg_upload_mb_s: {avg_mb_s:.3f}\n"
        f"log_path: {result.log_path}\n"
    )
    result.summary_path.write_text(text, encoding="utf-8")
    print(text)
    print(f"summary_path: {result.summary_path}")


def append_run_report(report_md: Path, args: argparse.Namespace, result: RunResult) -> None:
    mb_sent = result.sent / (1024.0 * 1024.0)
    avg_mb_s = (mb_sent / result.elapsed_s) if result.elapsed_s > 0 else 0.0
    lines = [
        f"- Env: `{args.env_name}`",
        f"- Base URL: `{args.base_url}`",
        f"- Result: `state={result.state}` `error={result.error}`",
        f"- Throughput: `avg_upload_mb_s={avg_mb_s:.3f}`",
        f"- Responsiveness: `success={result.poll_success_rate_pct:.2f}%` `p95={result.latency_p95_ms:.2f} ms` `max={result.latency_max_ms:.2f} ms`",
        f"- Active Upload Responsiveness: `samples={result.active_samples}` `p95={result.active_latency_p95_ms:.2f} ms` `max={result.active_latency_max_ms:.2f} ms`",
        f"- Bytes: `{result.sent}/{result.total}`",
        f"- Sample Count: `{result.samples}`",
        f"- Log: `{result.log_path}`",
        f"- Summary: `{result.summary_path}`",
    ]
    _append_report(report_md, "Run", lines)


def append_check_report(report_md: Path, args: argparse.Namespace, ok: bool, detail: str) -> None:
    lines = [
        f"- Env: `{args.env_name}`",
        f"- Base URL: `{args.base_url}`",
        f"- Check Result: `{'OK' if ok else 'FAIL'}`",
        f"- Detail: `{detail}`",
    ]
    _append_report(report_md, "Check", lines)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Poll upload UI test webinterface and log a run.")
    p.add_argument("--base-url", default="http://192.168.4.1", help="ESP web base URL (no trailing slash)")
    p.add_argument("--start", action="store_true", help="Call /start before polling")
    p.add_argument("--check-only", action="store_true", help="Only verify webservice endpoints and exit")
    p.add_argument("--reset-upload-state", action="store_true", help="Call /reset_upload_state and exit")
    p.add_argument("--retry-policy-check", action="store_true", help="Call /test/retry_policy and exit")
    p.add_argument("--server-contract-check", action="store_true", help="Call /test/server_contract and exit")
    p.add_argument("--uploaded-state-check", action="store_true", help="Call /test/uploaded_state_bookkeeping and /bookkeeping")
    p.add_argument("--recover-sd-check", action="store_true", help="Call /test/recover_sd and exit")
    p.add_argument("--timeout", type=float, default=240.0, help="Max run time in seconds")
    p.add_argument("--interval-ms", type=int, default=250, help="Poll interval in milliseconds")
    p.add_argument("--request-timeout", type=float, default=2.0, help="Single HTTP request timeout in seconds")
    p.add_argument("--sim-fps", type=int, default=0, help="If >0, set CAN simulated load fps via /sim/load?fps=N before start")
    p.add_argument("--log-dir", default="logs", help="Output log directory")
    p.add_argument("--prefix", default="upload_ui_test_v5", help="Output filename prefix")
    p.add_argument("--env-name", default="sd_http_upload_ui_test_v5", help="Firmware environment label for reports")
    p.add_argument("--report-md", default="logs/upload_ui_test_results.md", help="Markdown report output path")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    base = args.base_url.rstrip("/")
    log_dir = Path(args.log_dir)
    report_md = Path(args.report_md)
    log_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = log_dir / f"{args.prefix}_{stamp}.jsonl"

    if args.check_only:
      try:
          status = http_get_json(f"{base}/status", args.request_timeout)
          root = http_get_text(f"{base}/", args.request_timeout)
      except Exception as exc:  # noqa: BLE001
          append_check_report(report_md, args, False, str(exc))
          print(f"check_failed: {exc}", file=sys.stderr)
          return 2
      detail = f"status_state={status.get('state')} status_error={status.get('error')} root_len={len(root)}"
      append_check_report(report_md, args, True, detail)
      print(f"check_ok: {detail}")
      return 0

    if args.retry_policy_check:
      try:
          reply = http_get_json(f"{base}/test/retry_policy", args.request_timeout)
      except Exception as exc:  # noqa: BLE001
          append_check_report(report_md, args, False, f"retry_policy_check_failed: {exc}")
          print(f"retry_policy_check_failed: {exc}", file=sys.stderr)
          return 2
      ok = bool(reply.get("ok", False))
      append_check_report(report_md, args, ok, f"retry_policy_ok={ok}")
      print(f"retry_policy_check_ok: {ok}")
      return 0 if ok else 1

    if args.reset_upload_state:
      try:
          reply = http_get_json(f"{base}/reset_upload_state", args.request_timeout)
      except Exception as exc:  # noqa: BLE001
          append_check_report(report_md, args, False, f"reset_upload_state_failed: {exc}")
          print(f"reset_upload_state_failed: {exc}", file=sys.stderr)
          return 2
      ok = bool(reply.get("ok", False))
      removed = bool(reply.get("state_file_removed", False))
      append_check_report(report_md, args, ok, f"reset_upload_state_ok={ok} state_file_removed={removed}")
      print(f"reset_upload_state_ok: {ok} state_file_removed: {removed}")
      return 0 if ok else 1

    if args.server_contract_check:
      try:
          reply = http_get_json(f"{base}/test/server_contract", args.request_timeout)
      except Exception as exc:  # noqa: BLE001
          append_check_report(report_md, args, False, f"server_contract_check_failed: {exc}")
          print(f"server_contract_check_failed: {exc}", file=sys.stderr)
          return 2
      ok = bool(reply.get("ok", False))
      append_check_report(report_md, args, ok, f"server_contract_ok={ok}")
      print(f"server_contract_check_ok: {ok}")
      return 0 if ok else 1

    if args.uploaded_state_check:
      try:
          reply = http_get_json(f"{base}/test/uploaded_state_bookkeeping", args.request_timeout)
          summary = http_get_json(f"{base}/bookkeeping", args.request_timeout)
      except Exception as exc:  # noqa: BLE001
          append_check_report(report_md, args, False, f"uploaded_state_check_failed: {exc}")
          print(f"uploaded_state_check_failed: {exc}", file=sys.stderr)
          return 2
      ok = bool(reply.get("ok", False))
      uploaded = int(summary.get("uploaded", -1))
      outstanding = int(summary.get("outstanding", -1))
      queue_ready = int(summary.get("queue_ready", -1))
      queue_delayed = int(summary.get("queue_delayed", -1))
      detail = (
          f"uploaded_state_ok={ok} uploaded={uploaded} outstanding={outstanding} "
          f"queue_ready={queue_ready} queue_delayed={queue_delayed}"
      )
      append_check_report(report_md, args, ok, detail)
      print(f"uploaded_state_check_ok: {ok} uploaded={uploaded} outstanding={outstanding} queue_ready={queue_ready} queue_delayed={queue_delayed}")
      return 0 if ok else 1

    if args.recover_sd_check:
      try:
          reply = http_get_json(f"{base}/test/recover_sd", args.request_timeout)
      except Exception as exc:  # noqa: BLE001
          append_check_report(report_md, args, False, f"recover_sd_check_failed: {exc}")
          print(f"recover_sd_check_failed: {exc}", file=sys.stderr)
          return 2
      ok = bool(reply.get("ok", False))
      append_check_report(report_md, args, ok, f"recover_sd_ok={ok}")
      print(f"recover_sd_check_ok: {ok}")
      return 0 if ok else 1

    if args.start:
        if args.sim_fps > 0:
            try:
                sim_reply = http_get_text(f"{base}/sim/load?fps={args.sim_fps}", args.request_timeout).strip()
                print(f"sim_reply: {sim_reply}")
            except Exception as exc:  # noqa: BLE001
                append_check_report(report_md, args, False, f"sim_load_failed: {exc}")
                print(f"failed_to_set_sim_load: {exc}", file=sys.stderr)
                return 2
        try:
            reply = http_get_text(f"{base}/start", args.request_timeout).strip()
            print(f"start_reply: {reply}")
        except Exception as exc:  # noqa: BLE001
            append_check_report(report_md, args, False, f"start_failed: {exc}")
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
    append_run_report(report_md, args, result)

    if result.state.upper() == "DONE":
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
