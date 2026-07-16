import asyncio
import base64
from collections import defaultdict, deque
from contextlib import asynccontextmanager
from datetime import datetime, timezone
import html
import hashlib
import json
import logging
import os
import re
import socket
from pathlib import Path
import tempfile
import threading
import time
from typing import Any, Deque, Dict, List, Tuple
from urllib.parse import quote_plus
from urllib.parse import urlencode
from urllib.request import Request as UrlRequest
from urllib.request import urlopen
from urllib.error import HTTPError, URLError

try:
    import httpx
except ModuleNotFoundError:
    httpx = None
from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse
from starlette.requests import ClientDisconnect
from starlette.datastructures import UploadFile as StarletteUploadFile
from zeroconf import IPVersion, ServiceInfo
from zeroconf.asyncio import AsyncZeroconf

DESKTOP_UPLOAD_DIR = Path.home() / "Desktop" / "CAN Upload"
UPLOAD_DIR = Path(os.getenv("CAN_UPLOAD_DIR", str(DESKTOP_UPLOAD_DIR)))
SERVICE_TYPE = "_http._tcp.local."
DEFAULT_PORT = int(os.getenv("CAN_UPLOAD_PORT", "8000"))
DEFAULT_BONJOUR_NAME = os.getenv("CAN_UPLOAD_BONJOUR_NAME", "can-upload")
BONJOUR_ENABLED = os.getenv("CAN_UPLOAD_ENABLE_BONJOUR", "1").strip().lower() not in {
    "0",
    "false",
    "no",
    "off",
}
API_TOKEN = os.getenv("CAN_UPLOAD_API_TOKEN", "").strip()
MAX_UPLOAD_BYTES = max(1, int(os.getenv("CAN_UPLOAD_MAX_BYTES", str(256 * 1024 * 1024))))
RATE_LIMIT_WINDOW_SEC = max(1, int(os.getenv("CAN_UPLOAD_RATE_LIMIT_WINDOW_SEC", "60")))
RATE_LIMIT_MAX_REQUESTS = max(1, int(os.getenv("CAN_UPLOAD_RATE_LIMIT_MAX_REQUESTS", "30")))
AUDIT_LOG_FILE = os.getenv("CAN_UPLOAD_AUDIT_LOG_FILE", "upload_audit.jsonl")
FAIL_WITH_503 = os.getenv("CAN_UPLOAD_FORCE_503", "0").strip().lower() in {"1", "true", "yes", "on"}
FAIL_RETRY_AFTER_SEC = max(1, int(os.getenv("CAN_UPLOAD_FAIL_RETRY_AFTER_SEC", "10")))
IDEMPOTENCY_TTL_SEC = max(60, int(os.getenv("CAN_UPLOAD_IDEMPOTENCY_TTL_SEC", "86400")))
ONEDRIVE_CONFIG_FILE = os.getenv("CAN_UPLOAD_ONEDRIVE_CONFIG_FILE", "onedrive_config.json")
ONEDRIVE_SCOPE = os.getenv("CAN_UPLOAD_ONEDRIVE_SCOPE", "offline_access Files.ReadWrite")
DDNS_ENABLED = os.getenv("CAN_UPLOAD_DDNS_ENABLE", "0").strip().lower() in {"1", "true", "yes", "on"}
DDNS_PROVIDER = os.getenv("CAN_UPLOAD_DDNS_PROVIDER", "noip").strip().lower()
DDNS_HOSTNAME = os.getenv("CAN_UPLOAD_DDNS_HOSTNAME", "").strip()
DDNS_USERNAME = os.getenv("CAN_UPLOAD_DDNS_USERNAME", "").strip()
DDNS_PASSWORD = os.getenv("CAN_UPLOAD_DDNS_PASSWORD", "").strip()
DDNS_UPDATE_URL = os.getenv("CAN_UPLOAD_DDNS_UPDATE_URL", "https://dynupdate.no-ip.com/nic/update").strip()
DDNS_INTERVAL_SEC = max(0, int(os.getenv("CAN_UPLOAD_DDNS_INTERVAL_SEC", "600")))
DDNS_USER_AGENT = os.getenv("CAN_UPLOAD_DDNS_USER_AGENT", "can-upload-mock/1.0").strip() or "can-upload-mock/1.0"
DDNS_MYIP = os.getenv("CAN_UPLOAD_DDNS_MYIP", "").strip()

logger = logging.getLogger("can_upload_mock")
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s - %(message)s")


def _parse_csv_env(name: str, default: str) -> set[str]:
    raw = os.getenv(name, default)
    values = {part.strip().lower() for part in raw.split(",") if part.strip()}
    return values or {"*"}


def require_httpx_for_onedrive() -> None:
    if httpx is None:
        raise RuntimeError(
            "OneDrive integration requires 'httpx'. Install with: "
            "pip install -r requirements.txt"
        )


ALLOWED_EXTENSIONS = _parse_csv_env("CAN_UPLOAD_ALLOWED_EXTENSIONS", "*")
ALLOWED_MIME_TYPES = _parse_csv_env("CAN_UPLOAD_ALLOWED_MIME_TYPES", "*")

REQUIRED_METADATA_FIELDS = {"bus_id", "start_ms", "end_ms"}
OPTIONAL_METADATA_FIELDS = {"flags", "source", "device_id"}
KNOWN_METADATA_FIELDS = REQUIRED_METADATA_FIELDS | OPTIONAL_METADATA_FIELDS
REQUIRED_NUMERIC_FIELDS = {"bus_id", "start_ms", "end_ms"}

rate_limit_state: Dict[str, Deque[float]] = defaultdict(deque)
rate_limit_lock = threading.Lock()
audit_lock = threading.Lock()
idempotency_lock = threading.Lock()
idempotency_store: Dict[str, Tuple[float, Dict[str, Any]]] = {}
content_fingerprint_store: Dict[str, Dict[str, Any]] = {}
onedrive_lock = threading.Lock()
onedrive_config: Dict[str, Any] = {
    "enabled": False,
    "client_id": "",
    "tenant_id": "common",
    "folder_path": "",
    "refresh_token": "",
    "access_token": "",
    "access_token_expires_at": 0.0,
    "scope": ONEDRIVE_SCOPE,
}
onedrive_sync_status: Dict[str, Dict[str, Any]] = {}
ddns_task: asyncio.Task | None = None
ddns_status: Dict[str, Any] = {
    "enabled": DDNS_ENABLED,
    "provider": DDNS_PROVIDER,
    "hostname": DDNS_HOSTNAME,
    "last_run": "",
    "last_ok": False,
    "last_message": "not started",
    "last_response": "",
}


class BonjourAdvertiser:
    def __init__(self, service_name: str, port: int) -> None:
        self.service_name = self._sanitize_name(service_name)
        self.port = port
        self.async_zeroconf: AsyncZeroconf | None = None
        self.info: ServiceInfo | None = None
        self.registered: bool = False
        self.local_ip: str = self._best_local_ip()
        self.last_error: str = ""

    @staticmethod
    def _sanitize_name(name: str) -> str:
        safe = re.sub(r"[^a-zA-Z0-9-]", "-", name).strip("-").lower()
        return safe or "can-upload"

    @staticmethod
    def _best_local_ip() -> str:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.connect(("8.8.8.8", 80))
            return sock.getsockname()[0]
        except OSError:
            return "127.0.0.1"
        finally:
            sock.close()

    async def register(self) -> bool:
        self.local_ip = self._best_local_ip()
        self.last_error = ""
        self.registered = False
        try:
            self.async_zeroconf = AsyncZeroconf(ip_version=IPVersion.V4Only)
            self.info = ServiceInfo(
                type_=SERVICE_TYPE,
                name=f"{self.service_name}.{SERVICE_TYPE}",
                addresses=[socket.inet_aton(self.local_ip)],
                port=self.port,
                properties={"path": "/", "app": "can-upload-mock"},
                server=f"{self.service_name}.local.",
            )
            await self.async_zeroconf.async_register_service(self.info)
            self.registered = True
            return True
        except Exception as exc:
            self.last_error = str(exc) or exc.__class__.__name__
            logger.warning("Bonjour registration failed: %s", self.last_error)
            await self.unregister()
            return False

    async def unregister(self) -> None:
        if self.async_zeroconf and self.info and self.registered:
            try:
                await self.async_zeroconf.async_unregister_service(self.info)
            except Exception:
                logger.exception("Failed to unregister Bonjour service cleanly")
        if self.async_zeroconf:
            try:
                await self.async_zeroconf.async_close()
            except Exception:
                logger.exception("Failed to close Bonjour advertiser cleanly")
        self.async_zeroconf = None
        self.info = None
        self.registered = False


advertiser = BonjourAdvertiser(DEFAULT_BONJOUR_NAME, DEFAULT_PORT)


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def ensure_upload_dir() -> None:
    UPLOAD_DIR.mkdir(parents=True, exist_ok=True)


def get_audit_path() -> Path:
    return UPLOAD_DIR / AUDIT_LOG_FILE


def get_onedrive_config_path() -> Path:
    return UPLOAD_DIR / ONEDRIVE_CONFIG_FILE


def append_audit(event: Dict[str, Any]) -> None:
    event["timestamp"] = utc_now_iso()
    audit_path = get_audit_path()
    line = json.dumps(event, ensure_ascii=True)
    with audit_lock:
        with audit_path.open("a", encoding="utf-8") as handle:
            handle.write(line + "\n")


def _apply_onedrive_config_loaded(data: Dict[str, Any]) -> None:
    allowed = {
        "enabled",
        "client_id",
        "tenant_id",
        "folder_path",
        "refresh_token",
        "access_token",
        "access_token_expires_at",
        "scope",
    }
    with onedrive_lock:
        for key in allowed:
            if key in data:
                onedrive_config[key] = data[key]
        onedrive_config["enabled"] = bool(onedrive_config.get("enabled", False))
        onedrive_config["client_id"] = str(onedrive_config.get("client_id", "")).strip()
        onedrive_config["tenant_id"] = str(onedrive_config.get("tenant_id", "common")).strip() or "common"
        onedrive_config["folder_path"] = str(onedrive_config.get("folder_path", "")).strip().strip("/")
        onedrive_config["refresh_token"] = str(onedrive_config.get("refresh_token", "")).strip()
        onedrive_config["access_token"] = str(onedrive_config.get("access_token", "")).strip()
        try:
            onedrive_config["access_token_expires_at"] = float(onedrive_config.get("access_token_expires_at", 0.0))
        except (TypeError, ValueError):
            onedrive_config["access_token_expires_at"] = 0.0
        onedrive_config["scope"] = str(onedrive_config.get("scope", ONEDRIVE_SCOPE)).strip() or ONEDRIVE_SCOPE


def load_onedrive_config() -> None:
    path = get_onedrive_config_path()
    if not path.exists():
        save_onedrive_config()
        return
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            _apply_onedrive_config_loaded(data)
    except Exception as exc:
        logger.warning("Failed to load OneDrive config, using defaults: %s", exc)


def save_onedrive_config() -> None:
    path = get_onedrive_config_path()
    with onedrive_lock:
        data = dict(onedrive_config)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=True), encoding="utf-8")


def get_onedrive_snapshot() -> Dict[str, Any]:
    with onedrive_lock:
        return dict(onedrive_config)


def set_onedrive_sync_status(filename: str, state: str, message: str, web_url: str = "") -> None:
    onedrive_sync_status[filename] = {
        "state": state,
        "message": message,
        "web_url": web_url,
        "updated_at": utc_now_iso(),
    }


def is_onedrive_enabled_and_configured() -> bool:
    cfg = get_onedrive_snapshot()
    return bool(cfg.get("enabled")) and bool(cfg.get("client_id"))


def choose_upload_method(file_size: int) -> str:
    return "simple" if file_size <= 4 * 1024 * 1024 else "chunked"


async def onedrive_start_device_code() -> Dict[str, Any]:
    require_httpx_for_onedrive()
    cfg = get_onedrive_snapshot()
    client_id = cfg.get("client_id", "")
    tenant = cfg.get("tenant_id", "common")
    scope = cfg.get("scope", ONEDRIVE_SCOPE)
    if not client_id:
        raise ValueError("OneDrive client_id is not configured")

    url = f"https://login.microsoftonline.com/{tenant}/oauth2/v2.0/devicecode"
    data = {"client_id": client_id, "scope": scope}
    async with httpx.AsyncClient(timeout=30.0) as client:
        resp = await client.post(url, data=data)
    if resp.status_code >= 400:
        raise RuntimeError(f"Device code start failed ({resp.status_code}): {resp.text}")
    payload = resp.json()
    return payload


async def onedrive_poll_device_code(device_code: str) -> Dict[str, Any]:
    require_httpx_for_onedrive()
    cfg = get_onedrive_snapshot()
    client_id = cfg.get("client_id", "")
    tenant = cfg.get("tenant_id", "common")
    if not client_id:
        raise ValueError("OneDrive client_id is not configured")
    url = f"https://login.microsoftonline.com/{tenant}/oauth2/v2.0/token"
    data = {
        "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
        "client_id": client_id,
        "device_code": device_code,
    }
    async with httpx.AsyncClient(timeout=30.0) as client:
        resp = await client.post(url, data=data)
    if resp.status_code == 200:
        token = resp.json()
        expires_in = int(token.get("expires_in", 3600))
        with onedrive_lock:
            onedrive_config["access_token"] = token.get("access_token", "")
            onedrive_config["refresh_token"] = token.get("refresh_token", onedrive_config.get("refresh_token", ""))
            onedrive_config["access_token_expires_at"] = time.time() + max(60, expires_in - 60)
        save_onedrive_config()
        return {"status": "authorized"}
    payload = resp.json() if resp.headers.get("content-type", "").startswith("application/json") else {}
    error = payload.get("error", "unknown_error")
    if error in {"authorization_pending", "slow_down"}:
        return {"status": "pending", "error": error}
    raise RuntimeError(f"Device code polling failed: {payload or resp.text}")


async def onedrive_refresh_access_token() -> str:
    require_httpx_for_onedrive()
    cfg = get_onedrive_snapshot()
    refresh_token = cfg.get("refresh_token", "")
    client_id = cfg.get("client_id", "")
    tenant = cfg.get("tenant_id", "common")
    scope = cfg.get("scope", ONEDRIVE_SCOPE)
    if not client_id or not refresh_token:
        raise RuntimeError("Missing OneDrive client_id or refresh_token")
    url = f"https://login.microsoftonline.com/{tenant}/oauth2/v2.0/token"
    data = {
        "grant_type": "refresh_token",
        "client_id": client_id,
        "refresh_token": refresh_token,
        "scope": scope,
    }
    async with httpx.AsyncClient(timeout=30.0) as client:
        resp = await client.post(url, data=data)
    if resp.status_code >= 400:
        raise RuntimeError(f"Refresh token failed ({resp.status_code}): {resp.text}")
    payload = resp.json()
    access_token = payload.get("access_token", "")
    if not access_token:
        raise RuntimeError("No access_token returned by refresh flow")
    expires_in = int(payload.get("expires_in", 3600))
    with onedrive_lock:
        onedrive_config["access_token"] = access_token
        if payload.get("refresh_token"):
            onedrive_config["refresh_token"] = payload["refresh_token"]
        onedrive_config["access_token_expires_at"] = time.time() + max(60, expires_in - 60)
    save_onedrive_config()
    return access_token


async def get_valid_onedrive_access_token() -> str:
    cfg = get_onedrive_snapshot()
    token = cfg.get("access_token", "")
    expires_at = float(cfg.get("access_token_expires_at", 0.0))
    if token and expires_at > (time.time() + 30):
        return token
    return await onedrive_refresh_access_token()


async def graph_simple_upload(
    graph_access_token: str,
    local_path: Path,
    drive_folder_path: str = "",
) -> Dict[str, Any]:
    require_httpx_for_onedrive()
    filename = local_path.name
    remote_path = f"{drive_folder_path.strip('/')}/{filename}" if drive_folder_path else filename
    url = f"https://graph.microsoft.com/v1.0/me/drive/root:/{remote_path}:/content"
    headers = {"Authorization": f"Bearer {graph_access_token}"}
    async with httpx.AsyncClient(timeout=120.0) as client:
        with local_path.open("rb") as handle:
            resp = await client.put(url, headers=headers, content=handle)
    if resp.status_code >= 400:
        raise RuntimeError(f"Graph simple upload failed ({resp.status_code}): {resp.text}")
    return resp.json()


async def graph_create_upload_session(
    graph_access_token: str,
    filename: str,
    drive_folder_path: str = "",
) -> str:
    require_httpx_for_onedrive()
    remote_path = f"{drive_folder_path.strip('/')}/{filename}" if drive_folder_path else filename
    url = f"https://graph.microsoft.com/v1.0/me/drive/root:/{remote_path}:/createUploadSession"
    headers = {"Authorization": f"Bearer {graph_access_token}", "Content-Type": "application/json"}
    body = {"item": {"@microsoft.graph.conflictBehavior": "replace", "name": filename}}
    async with httpx.AsyncClient(timeout=60.0) as client:
        resp = await client.post(url, headers=headers, json=body)
    if resp.status_code >= 400:
        raise RuntimeError(f"Graph upload session failed ({resp.status_code}): {resp.text}")
    data = resp.json()
    upload_url = data.get("uploadUrl", "")
    if not upload_url:
        raise RuntimeError("Graph did not return uploadUrl")
    return upload_url


async def graph_chunked_upload(
    graph_access_token: str,
    local_path: Path,
    drive_folder_path: str = "",
    chunk_size: int = 10 * 1024 * 1024,
) -> Dict[str, Any]:
    require_httpx_for_onedrive()
    upload_url = await graph_create_upload_session(graph_access_token, local_path.name, drive_folder_path)
    total_size = local_path.stat().st_size
    num_chunks = max(1, (total_size + chunk_size - 1) // chunk_size)
    async with httpx.AsyncClient(timeout=300.0) as client:
        with local_path.open("rb") as handle:
            for idx in range(num_chunks):
                start = idx * chunk_size
                end = min(start + chunk_size, total_size) - 1
                length = end - start + 1
                handle.seek(start)
                chunk = handle.read(length)
                headers = {
                    "Content-Length": str(length),
                    "Content-Range": f"bytes {start}-{end}/{total_size}",
                }
                resp = await client.put(upload_url, headers=headers, content=chunk)
                if resp.status_code in (200, 201):
                    return resp.json()
                if resp.status_code == 202:
                    continue
                raise RuntimeError(f"Graph chunk upload failed ({resp.status_code}): {resp.text}")
    raise RuntimeError("Graph chunked upload ended unexpectedly")


async def upload_local_file_to_onedrive(local_path: Path) -> Dict[str, Any]:
    cfg = get_onedrive_snapshot()
    folder = str(cfg.get("folder_path", "")).strip().strip("/")
    token = await get_valid_onedrive_access_token()
    method = choose_upload_method(local_path.stat().st_size)
    if method == "simple":
        result = await graph_simple_upload(token, local_path, folder)
    else:
        result = await graph_chunked_upload(token, local_path, folder)
    return {
        "upload_method": method,
        "id": result.get("id"),
        "name": result.get("name"),
        "webUrl": result.get("webUrl"),
        "size": result.get("size"),
    }


def list_local_upload_files() -> List[Dict[str, Any]]:
    files: List[Dict[str, Any]] = []
    config_name = get_onedrive_config_path().name
    for path in sorted(UPLOAD_DIR.glob("*"), key=lambda p: p.stat().st_mtime, reverse=True):
        if not path.is_file():
            continue
        if path.name in {AUDIT_LOG_FILE, config_name}:
            continue
        status = onedrive_sync_status.get(path.name, {})
        files.append(
            {
                "name": path.name,
                "size": path.stat().st_size,
                "modified": datetime.fromtimestamp(path.stat().st_mtime).isoformat(timespec="seconds"),
                "sync_state": status.get("state", "not_synced"),
                "sync_message": status.get("message", ""),
                "web_url": status.get("web_url", ""),
            }
        )
    return files


async def auto_sync_file_to_onedrive(local_path: Path) -> None:
    filename = local_path.name
    if not is_onedrive_enabled_and_configured():
        set_onedrive_sync_status(filename, "skipped", "OneDrive auto-sync not enabled/configured")
        return
    try:
        set_onedrive_sync_status(filename, "uploading", "Uploading to OneDrive...")
        result = await upload_local_file_to_onedrive(local_path)
        web_url = str(result.get("webUrl", "") or "")
        set_onedrive_sync_status(filename, "synced", "Uploaded to OneDrive", web_url=web_url)
        append_audit(
            {
                "outcome": "onedrive_synced",
                "filename": filename,
                "onedrive_item_id": result.get("id"),
                "onedrive_web_url": web_url,
            }
        )
    except Exception as exc:
        set_onedrive_sync_status(filename, "error", str(exc))
        append_audit({"outcome": "onedrive_sync_error", "filename": filename, "error": str(exc)})


async def build_final_upload_response(
    *,
    client_ip: str,
    metadata: Dict[str, Any],
    saved_files: List[Dict[str, Any]],
    idempotency_key: str,
) -> JSONResponse:
    if not saved_files:
        append_audit({"client_ip": client_ip, "outcome": "no_files"})
        return response_error(code="NO_FILES", message="no files were provided", status_code=400)

    for file_result in saved_files:
        filename = str(file_result.get("saved_as", "upload.bin"))
        dedupe_key = build_duplicate_key(filename, metadata, str(file_result.get("sha256", "")))
        existing_duplicate = content_fingerprint_store.get(dedupe_key)
        if existing_duplicate:
            duplicate_path = Path(str(file_result["path"]))
            duplicate_path.unlink(missing_ok=True)
            duplicate_data = {
                "filename": filename,
                "existing": existing_duplicate,
                "metadata": metadata,
            }
            append_audit(
                {
                    "client_ip": client_ip,
                    "outcome": "duplicate_content",
                    "filename": filename,
                    "sha256": file_result.get("sha256", ""),
                    "metadata": metadata,
                }
            )
            return response_ok(
                code="DUPLICATE_CONTENT",
                message="duplicate content detected; not stored again",
                data=duplicate_data,
                status_code=200,
            )
        content_fingerprint_store[dedupe_key] = file_result

    response_data = {
        "count": len(saved_files),
        "files": saved_files,
        "metadata": metadata,
        "idempotency_key": idempotency_key or None,
    }
    onedrive_results: List[Dict[str, Any]] = []
    for file_info in saved_files:
        try:
            local_path = Path(str(file_info["path"]))
            await auto_sync_file_to_onedrive(local_path)
            sync = onedrive_sync_status.get(local_path.name, {})
            onedrive_results.append(
                {
                    "filename": local_path.name,
                    "state": sync.get("state", "unknown"),
                    "message": sync.get("message", ""),
                    "web_url": sync.get("web_url", ""),
                }
            )
        except Exception as exc:
            onedrive_results.append(
                {
                    "filename": str(file_info.get("saved_as", "")),
                    "state": "error",
                    "message": str(exc),
                    "web_url": "",
                }
            )
    response_data["onedrive"] = onedrive_results
    if idempotency_key:
        with idempotency_lock:
            idempotency_store[idempotency_key] = (time.time(), response_data)
    append_audit(
        {
            "client_ip": client_ip,
            "outcome": "accepted",
            "metadata": metadata,
            "idempotency_key": idempotency_key or None,
            "files": saved_files,
        }
    )
    return response_ok(
        code="UPLOAD_ACCEPTED",
        message="upload stored successfully",
        data=response_data,
        status_code=201,
    )


def _cleanup_old_idempotency_entries(now: float) -> None:
    expired_keys = [key for key, (ts, _) in idempotency_store.items() if now - ts > IDEMPOTENCY_TTL_SEC]
    for key in expired_keys:
        idempotency_store.pop(key, None)


def response_ok(code: str, message: str, data: Dict[str, Any], status_code: int = 200) -> JSONResponse:
    body = {"status": "ok", "code": code, "message": message, "data": data}
    return JSONResponse(status_code=status_code, content=body)


def response_error(
    code: str,
    message: str,
    status_code: int,
    retry_after: int | None = None,
    data: Dict[str, Any] | None = None,
) -> JSONResponse:
    body = {"status": "error", "code": code, "message": message, "data": data or {}}
    headers: Dict[str, str] = {}
    if retry_after is not None:
        headers["Retry-After"] = str(retry_after)
    return JSONResponse(status_code=status_code, content=body, headers=headers)


def request_client_ip(request: Request) -> str:
    if request.client and request.client.host:
        return request.client.host
    return "unknown"


def auth_failed(request: Request) -> bool:
    if not API_TOKEN:
        return False
    supplied = request.headers.get("X-Api-Token", "").strip()
    if not supplied:
        auth = request.headers.get("Authorization", "")
        if auth.lower().startswith("bearer "):
            supplied = auth[7:].strip()
    return supplied != API_TOKEN


def is_rate_limited(client_ip: str) -> Tuple[bool, int]:
    now = time.time()
    with rate_limit_lock:
        bucket = rate_limit_state[client_ip]
        while bucket and now - bucket[0] > RATE_LIMIT_WINDOW_SEC:
            bucket.popleft()
        if len(bucket) >= RATE_LIMIT_MAX_REQUESTS:
            retry_after = max(1, int(RATE_LIMIT_WINDOW_SEC - (now - bucket[0])))
            return True, retry_after
        bucket.append(now)
        return False, 0


def is_extension_allowed(filename: str) -> bool:
    if "*" in ALLOWED_EXTENSIONS:
        return True
    suffix = Path(filename).suffix.lower()
    return suffix in ALLOWED_EXTENSIONS


def is_mime_allowed(content_type: str) -> bool:
    if "*" in ALLOWED_MIME_TYPES:
        return True
    return content_type.lower() in ALLOWED_MIME_TYPES


def parse_metadata(form_items: List[Tuple[str, Any]]) -> Tuple[Dict[str, Any], List[str]]:
    metadata: Dict[str, Any] = {}
    errors: List[str] = []
    for key, value in form_items:
        if isinstance(value, StarletteUploadFile):
            continue
        if key in KNOWN_METADATA_FIELDS:
            metadata[key] = str(value).strip()
    missing = [field for field in REQUIRED_METADATA_FIELDS if not metadata.get(field)]
    if missing:
        errors.append(f"Missing required metadata fields: {', '.join(sorted(missing))}")
    for field in REQUIRED_NUMERIC_FIELDS:
        if field in metadata and metadata.get(field):
            try:
                metadata[field] = int(metadata[field])
            except ValueError:
                errors.append(f"Metadata field '{field}' must be an integer")
    return metadata, errors


def parse_metadata_from_headers(request: Request) -> Tuple[Dict[str, Any], List[str]]:
    metadata: Dict[str, Any] = {}
    errors: List[str] = []
    header_map = {
        "bus_id": "x-bus-id",
        "start_ms": "x-start-ms",
        "end_ms": "x-end-ms",
        "flags": "x-flags",
        "source": "x-source",
        "device_id": "x-device-id",
    }
    for key, header_name in header_map.items():
        value = request.headers.get(header_name, "").strip()
        if value:
            metadata[key] = value
    missing = [field for field in REQUIRED_METADATA_FIELDS if not metadata.get(field)]
    if missing:
        errors.append(f"Missing required metadata headers: {', '.join(sorted(missing))}")
    for field in REQUIRED_NUMERIC_FIELDS:
        if field in metadata and metadata.get(field):
            try:
                metadata[field] = int(metadata[field])
            except ValueError:
                errors.append(f"Metadata header '{field}' must be an integer")
    if "flags" in metadata:
        try:
            metadata["flags"] = int(metadata["flags"])
        except ValueError:
            errors.append("Metadata header 'flags' must be an integer")
    return metadata, errors


def build_duplicate_key(filename: str, metadata: Dict[str, Any], sha256_hex: str) -> str:
    bus_id = metadata.get("bus_id", "")
    start_ms = metadata.get("start_ms", "")
    end_ms = metadata.get("end_ms", "")
    return f"{filename}|{bus_id}|{start_ms}|{end_ms}|{sha256_hex}"


def safe_filename(name: str) -> str:
    return Path(name or "upload.bin").name


async def save_upload_atomic(upload: StarletteUploadFile, filename: str) -> Dict[str, Any]:
    destination = UPLOAD_DIR / filename
    sha = hashlib.sha256()
    total = 0
    fd, temp_path_str = tempfile.mkstemp(prefix=f".{filename}.", suffix=".part", dir=str(UPLOAD_DIR))
    temp_path = Path(temp_path_str)
    try:
        with os.fdopen(fd, "wb") as out:
            while True:
                chunk = await upload.read(1024 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > MAX_UPLOAD_BYTES:
                    raise ValueError(f"Upload exceeds max size of {MAX_UPLOAD_BYTES} bytes")
                sha.update(chunk)
                out.write(chunk)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temp_path, destination)
    except Exception:
        if temp_path.exists():
            temp_path.unlink(missing_ok=True)
        raise
    return {
        "path": str(destination),
        "saved_as": destination.name,
        "bytes": total,
        "sha256": sha.hexdigest(),
    }


async def save_stream_atomic(request: Request, filename: str) -> Dict[str, Any]:
    destination = UPLOAD_DIR / filename
    sha = hashlib.sha256()
    total = 0
    fd, temp_path_str = tempfile.mkstemp(prefix=f".{filename}.", suffix=".part", dir=str(UPLOAD_DIR))
    temp_path = Path(temp_path_str)
    try:
        with os.fdopen(fd, "wb") as out:
            async for chunk in request.stream():
                if not chunk:
                    continue
                total += len(chunk)
                if total > MAX_UPLOAD_BYTES:
                    raise ValueError(f"Upload exceeds max size of {MAX_UPLOAD_BYTES} bytes")
                sha.update(chunk)
                out.write(chunk)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temp_path, destination)
    except Exception:
        if temp_path.exists():
            temp_path.unlink(missing_ok=True)
        raise
    return {
        "path": str(destination),
        "saved_as": destination.name,
        "bytes": total,
        "sha256": sha.hexdigest(),
    }


def get_lan_upload_url() -> str:
    return f"http://{advertiser.local_ip}:{advertiser.port}/edit"


def _ddns_set_status(ok: bool, message: str, response: str = "") -> None:
    ddns_status["last_run"] = utc_now_iso()
    ddns_status["last_ok"] = ok
    ddns_status["last_message"] = message
    ddns_status["last_response"] = response


def _ddns_is_configured() -> tuple[bool, str]:
    if not DDNS_ENABLED:
        return False, "disabled"
    if DDNS_PROVIDER != "noip":
        return False, f"unsupported provider '{DDNS_PROVIDER}' (only 'noip' is supported)"
    if not DDNS_HOSTNAME:
        return False, "missing CAN_UPLOAD_DDNS_HOSTNAME"
    if not DDNS_USERNAME:
        return False, "missing CAN_UPLOAD_DDNS_USERNAME"
    if not DDNS_PASSWORD:
        return False, "missing CAN_UPLOAD_DDNS_PASSWORD"
    if not DDNS_UPDATE_URL:
        return False, "missing CAN_UPLOAD_DDNS_UPDATE_URL"
    return True, "ok"


def _noip_update_once_sync() -> tuple[bool, str, str]:
    params = {"hostname": DDNS_HOSTNAME}
    if DDNS_MYIP:
        params["myip"] = DDNS_MYIP
    url = f"{DDNS_UPDATE_URL}?{urlencode(params)}"
    auth = base64.b64encode(f"{DDNS_USERNAME}:{DDNS_PASSWORD}".encode("utf-8")).decode("ascii")
    req = UrlRequest(url, method="GET")
    req.add_header("Authorization", f"Basic {auth}")
    req.add_header("User-Agent", DDNS_USER_AGENT)
    try:
        with urlopen(req, timeout=10) as resp:
            body = resp.read().decode("utf-8", errors="replace").strip()
        token = body.split()[0].lower() if body else ""
        if token in {"good", "nochg"}:
            return True, f"no-ip update ok ({token})", body
        return False, f"no-ip update failed ({token or 'empty response'})", body
    except HTTPError as exc:
        return False, f"HTTP error {exc.code}", ""
    except URLError as exc:
        return False, f"network error: {exc.reason}", ""
    except Exception as exc:
        return False, f"unexpected error: {exc}", ""


async def ddns_update_once() -> None:
    ok, message, response = await asyncio.to_thread(_noip_update_once_sync)
    _ddns_set_status(ok, message, response)
    if ok:
        logger.info("DynDNS update succeeded: %s", message)
    else:
        logger.warning("DynDNS update failed: %s; response='%s'", message, response)


async def ddns_update_loop() -> None:
    while True:
        await asyncio.sleep(max(1, DDNS_INTERVAL_SEC))
        await ddns_update_once()


async def startup() -> None:
    global ddns_task
    ensure_upload_dir()
    load_onedrive_config()
    logger.info("Upload directory: %s", UPLOAD_DIR)
    logger.info("LAN upload URL: %s", get_lan_upload_url())
    if FAIL_WITH_503:
        logger.warning(
            "Forced 503 mode is enabled (CAN_UPLOAD_FORCE_503=1). All /edit uploads will return 503 with Retry-After=%ss.",
            FAIL_RETRY_AFTER_SEC,
        )
    ddns_ok, ddns_message = _ddns_is_configured()
    if DDNS_ENABLED:
        if ddns_ok:
            logger.info("DynDNS enabled (%s): hostname=%s", DDNS_PROVIDER, DDNS_HOSTNAME)
            await ddns_update_once()
            if DDNS_INTERVAL_SEC > 0:
                ddns_task = asyncio.create_task(ddns_update_loop(), name="ddns-update-loop")
                logger.info("DynDNS periodic refresh enabled: every %ss", DDNS_INTERVAL_SEC)
            else:
                logger.info("DynDNS periodic refresh disabled (CAN_UPLOAD_DDNS_INTERVAL_SEC=0)")
        else:
            _ddns_set_status(False, ddns_message)
            logger.warning("DynDNS enabled but not configured: %s", ddns_message)
    if not BONJOUR_ENABLED:
        logger.info("Bonjour registration disabled by CAN_UPLOAD_ENABLE_BONJOUR")
        return
    ok = await advertiser.register()
    if ok:
        logger.info(
            "Bonjour self-check: OK (advertised as http://%s.local:%d/edit)",
            advertiser.service_name,
            advertiser.port,
        )
    else:
        logger.warning("Bonjour self-check: FAILED (%s)", advertiser.last_error or "unknown error")


async def shutdown() -> None:
    global ddns_task
    if ddns_task:
        ddns_task.cancel()
        try:
            await ddns_task
        except asyncio.CancelledError:
            pass
        ddns_task = None
    await advertiser.unregister()


@asynccontextmanager
async def lifespan(_: FastAPI):
    await startup()
    try:
        yield
    except asyncio.CancelledError:
        # Uvicorn may cancel lifespan tasks during Ctrl+C shutdown on Windows.
        pass
    finally:
        try:
            await shutdown()
        except asyncio.CancelledError:
            pass


app = FastAPI(title="CAN Upload Mock Server", lifespan=lifespan)


@app.get("/")
def root() -> Dict[str, Any]:
    return {
        "service": "can-upload-mock",
        "post_endpoints": ["/edit", "/upload", "/edit-chunked", "/upload-chunked"],
        "bonjour_url": f"http://{advertiser.service_name}.local:{advertiser.port}/",
        "lan_upload_url": get_lan_upload_url(),
        "bonjour_enabled": BONJOUR_ENABLED,
        "bonjour_registered": advertiser.registered,
        "bonjour_error": advertiser.last_error,
        "bonjour_self_check": "ok" if advertiser.registered else "failed",
        "upload_directory": str(UPLOAD_DIR),
        "requires_auth_token": bool(API_TOKEN),
        "onedrive_httpx_available": httpx is not None,
        "ddns": {
            "enabled": DDNS_ENABLED,
            "provider": DDNS_PROVIDER,
            "hostname": DDNS_HOSTNAME,
            "interval_sec": DDNS_INTERVAL_SEC,
            "last_run": ddns_status.get("last_run", ""),
            "last_ok": ddns_status.get("last_ok", False),
            "last_message": ddns_status.get("last_message", ""),
            "last_response": ddns_status.get("last_response", ""),
        },
        "max_upload_bytes": MAX_UPLOAD_BYTES,
        "allowed_extensions": sorted(ALLOWED_EXTENSIONS),
        "allowed_mime_types": sorted(ALLOWED_MIME_TYPES),
    }


def ui_redirect(message: str, is_error: bool = False) -> RedirectResponse:
    target = f"/ui?msg={quote_plus(message)}&err={'1' if is_error else '0'}"
    return RedirectResponse(url=target, status_code=303)


def render_ui() -> str:
    cfg = get_onedrive_snapshot()
    files = list_local_upload_files()
    enabled_checked = "checked" if cfg.get("enabled") else ""
    token_ok = bool(cfg.get("refresh_token"))
    token_status = "ready" if token_ok else "missing"
    folder_path = html.escape(str(cfg.get("folder_path", "")))
    file_rows = []
    for entry in files:
        name = html.escape(entry["name"])
        sync_state = html.escape(entry["sync_state"])
        sync_msg = html.escape(entry["sync_message"])
        web_url = entry.get("web_url", "")
        web_cell = f'<a href="{html.escape(web_url)}" target="_blank">open</a>' if web_url else "-"
        row = (
            f"<tr><td>{name}</td><td>{entry['size']}</td><td>{html.escape(entry['modified'])}</td>"
            f"<td>{sync_state}</td><td>{sync_msg}</td><td>{web_cell}</td>"
            f"<td><form method='post' action='/ui/onedrive/upload-file'>"
            f"<input type='hidden' name='filename' value='{name}' />"
            "<button type='submit'>Upload now</button></form></td></tr>"
        )
        file_rows.append(row)
    file_table = "\n".join(file_rows) if file_rows else "<tr><td colspan='7'>No files yet</td></tr>"
    return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>CAN Upload Server UI</title>
  <style>
    body {{ font-family: sans-serif; margin: 20px; }}
    .menu {{ display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }}
    .card {{ border: 1px solid #ddd; border-radius: 8px; padding: 12px; }}
    label {{ display: block; margin-top: 8px; font-size: 0.9rem; }}
    input[type=text] {{ width: 100%; padding: 6px; }}
    table {{ width: 100%; border-collapse: collapse; }}
    th, td {{ border: 1px solid #ddd; padding: 6px; text-align: left; font-size: 0.9rem; }}
    .muted {{ color: #666; }}
  </style>
</head>
<body>
  <h1>CAN Upload Server</h1>
  <p class="muted">LAN upload URL: <code>{html.escape(get_lan_upload_url())}</code></p>
  <div class="menu">
    <div class="card">
      <h2>OneDrive Setup Menu</h2>
      <p>Step 1: Save your Azure app settings and desired OneDrive folder.</p>
      <form method="post" action="/ui/onedrive/config">
        <label>Client ID
          <input type="text" name="client_id" value="{html.escape(str(cfg.get("client_id", "")))}" />
        </label>
        <label>Tenant ID
          <input type="text" name="tenant_id" value="{html.escape(str(cfg.get("tenant_id", "common")))}" />
        </label>
        <label>Target OneDrive folder (optional)
          <input type="text" name="folder_path" value="{folder_path}" placeholder="incoming/can-logs" />
        </label>
        <label><input type="checkbox" name="enabled" {enabled_checked} /> Enable automatic OneDrive upload</label>
        <button type="submit">Save OneDrive config</button>
      </form>
      <p>Step 2: Start device login and open the provided verification URL.</p>
      <form method="post" action="/ui/onedrive/device/start"><button type="submit">Start device login</button></form>
      <p>Step 3: After entering code in browser, finalize token.</p>
      <form method="post" action="/ui/onedrive/device/poll">
        <label>Device code
          <input type="text" name="device_code" placeholder="paste device_code from step 2" />
        </label>
        <button type="submit">Finalize OneDrive login</button>
      </form>
      <p>Token status: <strong>{token_status}</strong></p>
      <form method="post" action="/ui/onedrive/refresh"><button type="submit">Refresh access token now</button></form>
    </div>
    <div class="card">
      <h2>Current Status</h2>
      <ul>
        <li>Auto upload enabled: <strong>{'yes' if cfg.get('enabled') else 'no'}</strong></li>
        <li>Client ID configured: <strong>{'yes' if cfg.get('client_id') else 'no'}</strong></li>
        <li>Refresh token saved: <strong>{'yes' if cfg.get('refresh_token') else 'no'}</strong></li>
        <li>Folder path: <strong>{folder_path or '(root)'}</strong></li>
      </ul>
      <form method="post" action="/ui/onedrive/upload-all"><button type="submit">Upload all local files now</button></form>
      <p class="muted">Files uploaded via ESP32 API are auto-synced when enabled.</p>
    </div>
  </div>
  <h2>Local Files</h2>
  <table>
    <thead><tr><th>Name</th><th>Size (bytes)</th><th>Modified</th><th>Sync state</th><th>Message</th><th>OneDrive</th><th>Action</th></tr></thead>
    <tbody>{file_table}</tbody>
  </table>
</body>
</html>"""


@app.get("/ui", response_class=HTMLResponse)
def ui_page(request: Request) -> HTMLResponse:
    msg = request.query_params.get("msg", "")
    err = request.query_params.get("err", "0") == "1"
    banner = ""
    if msg:
        color = "#a40000" if err else "#0a7a1f"
        banner = f"<div style='padding:8px;border:1px solid {color};color:{color};margin-bottom:8px'>{html.escape(msg)}</div>"
    page = render_ui().replace("<body>", f"<body>{banner}", 1)
    return HTMLResponse(page)


@app.post("/ui/onedrive/config")
async def ui_onedrive_config(request: Request) -> RedirectResponse:
    form = await request.form()
    with onedrive_lock:
        onedrive_config["client_id"] = str(form.get("client_id", "")).strip()
        onedrive_config["tenant_id"] = str(form.get("tenant_id", "common")).strip() or "common"
        onedrive_config["folder_path"] = str(form.get("folder_path", "")).strip().strip("/")
        onedrive_config["enabled"] = form.get("enabled") == "on"
    save_onedrive_config()
    return ui_redirect("OneDrive configuration saved")


@app.post("/ui/onedrive/device/start")
async def ui_onedrive_device_start() -> RedirectResponse:
    try:
        payload = await onedrive_start_device_code()
    except Exception as exc:
        return ui_redirect(str(exc), is_error=True)
    user_code = payload.get("user_code", "")
    verify_uri = payload.get("verification_uri", "")
    device_code = payload.get("device_code", "")
    msg = f"Device login started. user_code={user_code} verify_url={verify_uri} device_code={device_code}"
    return ui_redirect(msg)


@app.post("/ui/onedrive/device/poll")
async def ui_onedrive_device_poll(request: Request) -> RedirectResponse:
    form = await request.form()
    device_code = str(form.get("device_code", "")).strip()
    if not device_code:
        return ui_redirect("Missing device_code", is_error=True)
    try:
        result = await onedrive_poll_device_code(device_code)
    except Exception as exc:
        return ui_redirect(str(exc), is_error=True)
    if result.get("status") == "authorized":
        return ui_redirect("OneDrive authorization completed")
    return ui_redirect(f"Device login pending: {result.get('error', 'authorization_pending')}")


@app.post("/ui/onedrive/refresh")
async def ui_onedrive_refresh() -> RedirectResponse:
    try:
        await onedrive_refresh_access_token()
        return ui_redirect("Access token refreshed")
    except Exception as exc:
        return ui_redirect(str(exc), is_error=True)


@app.post("/ui/onedrive/upload-file")
async def ui_onedrive_upload_file(request: Request) -> RedirectResponse:
    form = await request.form()
    filename = str(form.get("filename", "")).strip()
    if not filename:
        return ui_redirect("Missing filename", is_error=True)
    local_path = UPLOAD_DIR / filename
    if not local_path.exists() or not local_path.is_file():
        return ui_redirect(f"File not found: {filename}", is_error=True)
    try:
        await auto_sync_file_to_onedrive(local_path)
        status = onedrive_sync_status.get(filename, {})
        if status.get("state") == "synced":
            return ui_redirect(f"Uploaded {filename} to OneDrive")
        return ui_redirect(f"Upload failed: {status.get('message', 'unknown')}", is_error=True)
    except Exception as exc:
        return ui_redirect(str(exc), is_error=True)


@app.post("/ui/onedrive/upload-all")
async def ui_onedrive_upload_all() -> RedirectResponse:
    files = list_local_upload_files()
    if not files:
        return ui_redirect("No files to upload")
    for entry in files:
        await auto_sync_file_to_onedrive(UPLOAD_DIR / entry["name"])
    return ui_redirect("Triggered upload for all local files")


@app.get("/api/onedrive/status")
def onedrive_status() -> Dict[str, Any]:
    cfg = get_onedrive_snapshot()
    return {
        "enabled": bool(cfg.get("enabled")),
        "configured": bool(cfg.get("client_id")),
        "has_refresh_token": bool(cfg.get("refresh_token")),
        "folder_path": cfg.get("folder_path", ""),
        "files": list_local_upload_files(),
    }


@app.get("/healthz")
def healthz() -> JSONResponse:
    return response_ok(
        code="HEALTHY",
        message="service healthy",
        data={"time": utc_now_iso(), "service": "can-upload-mock"},
    )


@app.get("/readyz")
def readyz() -> JSONResponse:
    try:
        ensure_upload_dir()
        test_file = UPLOAD_DIR / ".readycheck"
        with test_file.open("w", encoding="utf-8") as handle:
            handle.write("ok")
        test_file.unlink(missing_ok=True)
    except Exception as exc:
        return response_error(
            code="NOT_READY",
            message=f"upload directory not writable: {exc}",
            status_code=503,
            retry_after=3,
        )
    return response_ok(
        code="READY",
        message="service ready",
        data={"upload_directory": str(UPLOAD_DIR)},
    )


async def upload_chunked_core(request: Request, client_ip: str) -> JSONResponse:
    metadata, metadata_errors = parse_metadata_from_headers(request)
    if metadata_errors:
        append_audit({"client_ip": client_ip, "outcome": "invalid_metadata_headers", "errors": metadata_errors})
        return response_error(
            code="INVALID_METADATA",
            message="; ".join(metadata_errors),
            status_code=400,
            data={"required_headers": ["x-bus-id", "x-start-ms", "x-end-ms"]},
        )

    idempotency_key = request.headers.get("X-Idempotency-Key", "").strip()
    now = time.time()
    if idempotency_key:
        with idempotency_lock:
            _cleanup_old_idempotency_entries(now)
            existing = idempotency_store.get(idempotency_key)
            if existing:
                _, existing_response = existing
                append_audit(
                    {
                        "client_ip": client_ip,
                        "outcome": "idempotency_replay",
                        "idempotency_key": idempotency_key,
                    }
                )
                return response_ok(
                    code="DUPLICATE_IDEMPOTENCY_KEY",
                    message="request already processed for idempotency key",
                    data=existing_response,
                    status_code=200,
                )

    filename = safe_filename(request.headers.get("x-filename", "chunked-upload.bin"))
    content_type = request.headers.get("content-type", "application/octet-stream")
    if not is_extension_allowed(filename):
        append_audit({"client_ip": client_ip, "outcome": "blocked_extension", "filename": filename})
        return response_error(
            code="UNSUPPORTED_FILE_EXTENSION",
            message=f"extension not allowed for file '{filename}'",
            status_code=415,
        )
    if not is_mime_allowed(content_type):
        append_audit({"client_ip": client_ip, "outcome": "blocked_mime", "filename": filename})
        return response_error(
            code="UNSUPPORTED_MEDIA_TYPE",
            message=f"MIME type not allowed for file '{filename}'",
            status_code=415,
        )
    try:
        saved = await save_stream_atomic(request, filename)
    except ClientDisconnect:
        append_audit({"client_ip": client_ip, "outcome": "client_disconnect_during_stream_write", "filename": filename})
        return response_error(
            code="CLIENT_DISCONNECT",
            message="client disconnected during chunked upload stream",
            status_code=499,
        )
    except asyncio.CancelledError:
        return response_error(
            code="REQUEST_CANCELLED",
            message="request cancelled during chunked upload stream",
            status_code=499,
        )
    except ValueError as exc:
        append_audit({"client_ip": client_ip, "outcome": "too_large", "filename": filename})
        return response_error(code="PAYLOAD_TOO_LARGE", message=str(exc), status_code=413)
    except Exception as exc:
        append_audit({"client_ip": client_ip, "outcome": "write_error", "filename": filename, "error": str(exc)})
        return response_error(
            code="UPLOAD_WRITE_FAILED",
            message=f"failed to save '{filename}': {exc}",
            status_code=500,
        )

    saved_files = [
        {
            "field": "stream",
            "filename": filename,
            "content_type": content_type,
            "saved_as": saved["saved_as"],
            "bytes": saved["bytes"],
            "sha256": saved["sha256"],
            "path": saved["path"],
        }
    ]
    return await build_final_upload_response(
        client_ip=client_ip,
        metadata=metadata,
        saved_files=saved_files,
        idempotency_key=idempotency_key,
    )


@app.post("/edit")
async def upload_like_esp(request: Request) -> JSONResponse:
    client_ip = request_client_ip(request)
    if FAIL_WITH_503:
        append_audit({"client_ip": client_ip, "outcome": "forced_503"})
        return response_error(
            code="TEMPORARY_UNAVAILABLE",
            message="mock temporary outage",
            status_code=503,
            retry_after=FAIL_RETRY_AFTER_SEC,
        )

    if auth_failed(request):
        append_audit({"client_ip": client_ip, "outcome": "auth_failed"})
        return response_error(code="AUTH_FAILED", message="invalid or missing API token", status_code=401)

    limited, retry_after = is_rate_limited(client_ip)
    if limited:
        append_audit({"client_ip": client_ip, "outcome": "rate_limited", "retry_after": retry_after})
        return response_error(
            code="RATE_LIMITED",
            message="too many requests from client",
            status_code=429,
            retry_after=retry_after,
        )

    upload_mode = request.headers.get("x-upload-mode", "").strip().lower()
    content_type = request.headers.get("content-type", "").strip().lower()
    if upload_mode in {"chunked", "raw"} or content_type.startswith("application/octet-stream"):
        return await upload_chunked_core(request, client_ip)

    try:
        form = await request.form()
    except ClientDisconnect:
        append_audit({"client_ip": client_ip, "outcome": "client_disconnect_during_form_parse"})
        return response_error(
            code="CLIENT_DISCONNECT",
            message="client disconnected while streaming multipart body",
            status_code=499,
        )
    except asyncio.CancelledError:
        return response_error(
            code="REQUEST_CANCELLED",
            message="request cancelled while reading multipart body",
            status_code=499,
        )
    items = list(form.multi_items())
    metadata, metadata_errors = parse_metadata(items)
    if metadata_errors:
        append_audit({"client_ip": client_ip, "outcome": "invalid_metadata", "errors": metadata_errors})
        return response_error(
            code="INVALID_METADATA",
            message="; ".join(metadata_errors),
            status_code=400,
            data={"required_fields": sorted(REQUIRED_METADATA_FIELDS)},
        )

    idempotency_key = request.headers.get("X-Idempotency-Key", "").strip()
    if not idempotency_key:
        value = form.get("idempotency_key")
        if value is not None:
            idempotency_key = str(value).strip()

    now = time.time()
    if idempotency_key:
        with idempotency_lock:
            _cleanup_old_idempotency_entries(now)
            existing = idempotency_store.get(idempotency_key)
            if existing:
                _, existing_response = existing
                append_audit(
                    {
                        "client_ip": client_ip,
                        "outcome": "idempotency_replay",
                        "idempotency_key": idempotency_key,
                    }
                )
                return response_ok(
                    code="DUPLICATE_IDEMPOTENCY_KEY",
                    message="request already processed for idempotency key",
                    data=existing_response,
                    status_code=200,
                )

    saved_files: List[Dict[str, Any]] = []
    for key, value in items:
        if not isinstance(value, StarletteUploadFile):
            continue
        filename = safe_filename(value.filename or "upload.bin")
        content_type = value.content_type or "application/octet-stream"
        if not is_extension_allowed(filename):
            append_audit(
                {
                    "client_ip": client_ip,
                    "outcome": "blocked_extension",
                    "filename": filename,
                    "content_type": content_type,
                }
            )
            return response_error(
                code="UNSUPPORTED_FILE_EXTENSION",
                message=f"extension not allowed for file '{filename}'",
                status_code=415,
            )
        if not is_mime_allowed(content_type):
            append_audit(
                {
                    "client_ip": client_ip,
                    "outcome": "blocked_mime",
                    "filename": filename,
                    "content_type": content_type,
                }
            )
            return response_error(
                code="UNSUPPORTED_MEDIA_TYPE",
                message=f"MIME type not allowed for file '{filename}'",
                status_code=415,
            )
        try:
            saved = await save_upload_atomic(value, filename)
        except ClientDisconnect:
            append_audit(
                {
                    "client_ip": client_ip,
                    "outcome": "client_disconnect_during_file_write",
                    "filename": filename,
                }
            )
            return response_error(
                code="CLIENT_DISCONNECT",
                message="client disconnected during file upload",
                status_code=499,
            )
        except asyncio.CancelledError:
            return response_error(
                code="REQUEST_CANCELLED",
                message="request cancelled during file write",
                status_code=499,
            )
        except ValueError as exc:
            append_audit({"client_ip": client_ip, "outcome": "too_large", "filename": filename})
            return response_error(code="PAYLOAD_TOO_LARGE", message=str(exc), status_code=413)
        except Exception as exc:
            append_audit(
                {
                    "client_ip": client_ip,
                    "outcome": "write_error",
                    "filename": filename,
                    "error": str(exc),
                }
            )
            return response_error(
                code="UPLOAD_WRITE_FAILED",
                message=f"failed to save '{filename}': {exc}",
                status_code=500,
            )

        file_result = {
            "field": key,
            "filename": value.filename,
            "content_type": content_type,
            "saved_as": saved["saved_as"],
            "bytes": saved["bytes"],
            "sha256": saved["sha256"],
            "path": saved["path"],
        }
        saved_files.append(file_result)

    return await build_final_upload_response(
        client_ip=client_ip,
        metadata=metadata,
        saved_files=saved_files,
        idempotency_key=idempotency_key,
    )


@app.post("/upload")
async def upload_generic(request: Request) -> JSONResponse:
    return await upload_like_esp(request)


@app.post("/edit-chunked")
async def upload_chunked_endpoint(request: Request) -> JSONResponse:
    client_ip = request_client_ip(request)
    if FAIL_WITH_503:
        append_audit({"client_ip": client_ip, "outcome": "forced_503"})
        return response_error(
            code="TEMPORARY_UNAVAILABLE",
            message="mock temporary outage",
            status_code=503,
            retry_after=FAIL_RETRY_AFTER_SEC,
        )
    if auth_failed(request):
        append_audit({"client_ip": client_ip, "outcome": "auth_failed"})
        return response_error(code="AUTH_FAILED", message="invalid or missing API token", status_code=401)
    limited, retry_after = is_rate_limited(client_ip)
    if limited:
        append_audit({"client_ip": client_ip, "outcome": "rate_limited", "retry_after": retry_after})
        return response_error(
            code="RATE_LIMITED",
            message="too many requests from client",
            status_code=429,
            retry_after=retry_after,
        )
    return await upload_chunked_core(request, client_ip)


@app.post("/upload-chunked")
async def upload_chunked_alias(request: Request) -> JSONResponse:
    return await upload_chunked_endpoint(request)


if __name__ == "__main__":
    import uvicorn

    host = os.getenv("CAN_UPLOAD_HOST", "0.0.0.0")
    lan_ip = BonjourAdvertiser._best_local_ip()
    print(f"Starting CAN upload mock server on {host}:{DEFAULT_PORT}")
    print(f"Bonjour URL: http://{advertiser.service_name}.local:{DEFAULT_PORT}/")
    print(f"LAN upload URL: http://{lan_ip}:{DEFAULT_PORT}/edit")
    print(f"Web UI: http://{lan_ip}:{DEFAULT_PORT}/ui")
    if DDNS_ENABLED:
        print(f"DynDNS: enabled ({DDNS_PROVIDER}) for hostname '{DDNS_HOSTNAME}'")
    else:
        print("DynDNS: disabled")
    if FAIL_WITH_503:
        print(
            f"WARNING: Forced 503 mode is enabled (CAN_UPLOAD_FORCE_503=1, Retry-After={FAIL_RETRY_AFTER_SEC}s)"
        )
    if httpx is None:
        print("OneDrive note: httpx not installed, OneDrive features disabled until you install requirements.")
    print(f"Saved files directory: {UPLOAD_DIR}")
    uvicorn.run(app, host=host, port=DEFAULT_PORT)
