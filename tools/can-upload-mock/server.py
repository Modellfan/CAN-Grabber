from contextlib import asynccontextmanager
import logging
import os
import re
import socket
from pathlib import Path
from typing import Any, Dict, List

from fastapi import FastAPI, HTTPException, Request
from starlette.datastructures import UploadFile as StarletteUploadFile
from zeroconf import IPVersion, ServiceInfo, Zeroconf

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
logger = logging.getLogger("can_upload_mock")


class BonjourAdvertiser:
    def __init__(self, service_name: str, port: int) -> None:
        self.service_name = self._sanitize_name(service_name)
        self.port = port
        self.zeroconf: Zeroconf | None = None
        self.info: ServiceInfo | None = None
        self.registered: bool = False

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

    def register(self) -> None:
        ip = self._best_local_ip()
        self.zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
        self.info = ServiceInfo(
            type_=SERVICE_TYPE,
            name=f"{self.service_name}.{SERVICE_TYPE}",
            addresses=[socket.inet_aton(ip)],
            port=self.port,
            properties={"path": "/", "app": "can-upload-mock"},
            server=f"{self.service_name}.local.",
        )
        self.zeroconf.register_service(self.info)
        self.registered = True

    def unregister(self) -> None:
        if self.zeroconf and self.info and self.registered:
            try:
                self.zeroconf.unregister_service(self.info)
            except Exception:
                logger.exception("Failed to unregister Bonjour service cleanly")
        if self.zeroconf:
            try:
                self.zeroconf.close()
            except Exception:
                logger.exception("Failed to close Bonjour advertiser cleanly")
        self.zeroconf = None
        self.info = None
        self.registered = False


advertiser = BonjourAdvertiser(DEFAULT_BONJOUR_NAME, DEFAULT_PORT)


def ensure_upload_dir() -> None:
    UPLOAD_DIR.mkdir(parents=True, exist_ok=True)


async def _save_upload(upload: StarletteUploadFile, field_name: str) -> Dict[str, Any]:
    filename = Path(upload.filename or "upload.bin").name
    destination = UPLOAD_DIR / filename

    total = 0
    with destination.open("wb") as out:
        while True:
            chunk = await upload.read(1024 * 1024)
            if not chunk:
                break
            total += len(chunk)
            out.write(chunk)

    return {
        "field": field_name,
        "filename": upload.filename,
        "saved_as": destination.name,
        "bytes": total,
        "path": str(destination),
    }


def startup() -> None:
    ensure_upload_dir()
    if not BONJOUR_ENABLED:
        logger.info("Bonjour registration disabled by CAN_UPLOAD_ENABLE_BONJOUR")
        return
    try:
        advertiser.register()
    except Exception as exc:
        logger.warning("Bonjour registration failed, continuing without mDNS: %s", exc)
        advertiser.unregister()


def shutdown() -> None:
    advertiser.unregister()


@asynccontextmanager
async def lifespan(_: FastAPI):
    startup()
    yield
    shutdown()


app = FastAPI(title="CAN Upload Mock Server", lifespan=lifespan)


@app.get("/")
def root() -> Dict[str, Any]:
    return {
        "service": "can-upload-mock",
        "post_endpoints": ["/edit", "/upload"],
        "bonjour_url": f"http://{advertiser.service_name}.local:{advertiser.port}/",
        "bonjour_enabled": BONJOUR_ENABLED,
        "bonjour_registered": advertiser.registered,
        "upload_directory": str(UPLOAD_DIR),
    }


@app.post("/edit")
async def upload_like_esp(request: Request) -> Dict[str, Any]:
    form = await request.form()
    saved: List[Dict[str, Any]] = []

    for key, value in form.multi_items():
        if isinstance(value, StarletteUploadFile):
            saved.append(await _save_upload(value, key))

    if not saved:
        raise HTTPException(status_code=400, detail="No files in multipart form data")

    return {"saved": saved, "count": len(saved)}


@app.post("/upload")
async def upload_generic(request: Request) -> Dict[str, Any]:
    return await upload_like_esp(request)


if __name__ == "__main__":
    import uvicorn

    host = os.getenv("CAN_UPLOAD_HOST", "0.0.0.0")
    print(f"Starting CAN upload mock server on {host}:{DEFAULT_PORT}")
    print(f"Bonjour URL: http://{advertiser.service_name}.local:{DEFAULT_PORT}/")
    print(f"Saved files directory: {UPLOAD_DIR}")
    uvicorn.run(app, host=host, port=DEFAULT_PORT)
