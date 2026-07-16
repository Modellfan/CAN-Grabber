import argparse
import http.client
import json
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


IP_PATTERNS = [
    ("sta", re.compile(r"\bSTA IP:\s*(\d+\.\d+\.\d+\.\d+)")),
    ("mdns", re.compile(r"\bmDNS ready: .*->\s*(\d+\.\d+\.\d+\.\d+)")),
    ("ap", re.compile(r"\bAP IP:\s*(\d+\.\d+\.\d+\.\d+)")),
]


def extract_candidates_from_log(path: Path) -> list[tuple[str, str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    found: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()
    for label, pattern in IP_PATTERNS:
        for match in pattern.finditer(text):
            item = (label, match.group(1))
            if item not in seen:
                found.append(item)
                seen.add(item)
    return found


def fetch(url: str, timeout: float) -> dict:
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": "CAN-Grabber-Reachability-Probe/1.0",
            "Accept": "*/*",
        },
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        body = response.read()
        content_length_header = response.headers.get("Content-Length")
        content_length = None
        if content_length_header:
            try:
                content_length = int(content_length_header)
            except ValueError:
                content_length = None
        complete = content_length is None or len(body) == content_length
        return {
            "ok": complete,
            "http_ok": True,
            "status": response.status,
            "content_type": response.headers.get("Content-Type", ""),
            "content_length": content_length,
            "bytes_read": len(body),
            "complete": complete,
            "error": "" if complete else "short body",
            "body_preview": body[:512].decode("utf-8", errors="replace"),
        }


def probe_ip(ip: str, port: int, timeout: float) -> dict:
    result = {"ip": ip, "port": port, "paths": {}}
    for path in ("/", "/api/status"):
        url = f"http://{ip}:{port}{path}"
        try:
            response = fetch(url, timeout)
            result["paths"][path] = response
        except http.client.IncompleteRead as exc:
            partial = exc.partial or b""
            result["paths"][path] = {
                "ok": False,
                "http_ok": False,
                "error": f"IncompleteRead({len(partial)} bytes)",
                "bytes_read": len(partial),
                "body_preview": partial[:512].decode("utf-8", errors="replace"),
            }
        except urllib.error.HTTPError as exc:
            result["paths"][path] = {
                "ok": False,
                "http_ok": False,
                "status": exc.code,
                "error": f"HTTP {exc.code}",
            }
        except Exception as exc:  # noqa: BLE001
            result["paths"][path] = {"ok": False, "http_ok": False, "error": str(exc)}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Probe HTTP reachability using an explicit IP or IPs extracted from a serial boot log."
    )
    parser.add_argument("--ip", action="append", default=[])
    parser.add_argument("--log", type=Path)
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--attempts", type=int, default=3)
    parser.add_argument("--interval", type=float, default=3.0)
    args = parser.parse_args()

    candidates: list[tuple[str, str]] = []
    if args.log:
      candidates.extend(extract_candidates_from_log(args.log))
    for ip in args.ip:
        candidates.append(("cli", ip))

    deduped: list[tuple[str, str]] = []
    seen_ips: set[str] = set()
    for label, ip in candidates:
        if ip in seen_ips:
            continue
        deduped.append((label, ip))
        seen_ips.add(ip)

    if not deduped:
        print("No IP candidates found. Use --ip or --log.")
        return 1

    print("IP candidates:")
    for label, ip in deduped:
        print(f"  {label}: {ip}")

    all_results = []
    for attempt in range(1, args.attempts + 1):
        print(f"\nAttempt {attempt}/{args.attempts}")
        for label, ip in deduped:
            result = probe_ip(ip, args.port, args.timeout)
            result["label"] = label
            result["attempt"] = attempt
            all_results.append(result)
            print(f"  {label} {ip}:{args.port}")
            for path, path_result in result["paths"].items():
                if path_result.get("ok"):
                    preview = path_result.get("body_preview", "").replace("\n", " ")[:120]
                    print(
                        f"    {path}: OK status={path_result['status']} type={path_result['content_type']} bytes={path_result.get('bytes_read')} length={path_result.get('content_length')} preview={preview}"
                    )
                else:
                    status = path_result.get("status")
                    error = path_result.get("error", "unknown")
                    suffix = f" status={status}" if status is not None else ""
                    extra = ""
                    if path_result.get("bytes_read") is not None:
                        extra = f" bytes={path_result.get('bytes_read')}"
                    print(f"    {path}: FAIL{suffix}{extra} error={error}")
        if attempt < args.attempts:
            time.sleep(args.interval)

    print("\nSummary JSON:")
    print(json.dumps(all_results, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
