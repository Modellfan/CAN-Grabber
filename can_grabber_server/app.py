from __future__ import annotations

import os
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, File, HTTPException, Request, UploadFile
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from can_grabber_server.dbc_manager import DbcManager
from can_grabber_server.influx_manager import InfluxManager
from can_grabber_server.upload_server import upload_server

PACKAGE_ROOT = Path(__file__).resolve().parent
STATIC_DIR = PACKAGE_ROOT / "static"


@asynccontextmanager
async def lifespan(_: FastAPI):
    await upload_server.startup()
    try:
        yield
    finally:
        await upload_server.shutdown()


app = FastAPI(title="CAN Grabber Server", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")
app.mount("/upload-server", upload_server.app)

manager = InfluxManager()
dbc_manager = DbcManager()


class PathSelection(BaseModel):
    path: str


class DbcDirectorySelection(BaseModel):
    path: str


class DbcMessageUpdate(BaseModel):
    file_name: str
    current_message_name: str
    new_name: str
    comment: str = ""


class DbcSignalUpdate(BaseModel):
    file_name: str
    message_name: str
    current_signal_name: str
    new_name: str
    scale: float
    offset: float
    comment: str = ""


@app.get("/")
def index() -> FileResponse:
    return FileResponse(STATIC_DIR / "index.html")


@app.get("/api/influx/status")
def influx_status() -> dict:
    try:
        return manager.get_status()
    except Exception as exc:
        return {
            "selected_path": "",
            "selected_exists": False,
            "install_dir": "",
            "candidate_paths": [],
            "running": False,
            "running_processes": [],
            "pid": None,
            "log_path": "",
            "last_started_at": "",
            "message": f"InfluxDB status unavailable: {exc}",
        }


@app.get("/api/upload-server/status")
def upload_server_status() -> dict:
    return upload_server.root()


@app.post("/api/influx/search")
def search_influx() -> dict:
    return manager.search_installations()


@app.post("/api/influx/path")
def set_influx_path(payload: PathSelection) -> dict:
    try:
        return manager.set_selected_path(payload.path)
    except FileNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@app.post("/api/influx/start")
def start_influx() -> dict:
    try:
        return manager.start_influxdb()
    except FileNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except RuntimeError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.get("/api/dbc/status")
def dbc_status() -> dict:
    return dbc_manager.get_status()


@app.post("/api/dbc/directory")
def set_dbc_directory(payload: DbcDirectorySelection) -> dict:
    try:
        return dbc_manager.set_directory(payload.path)
    except FileNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except OSError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.post("/api/dbc/scan")
def scan_dbc_directory() -> dict:
    try:
        return dbc_manager.scan_directory()
    except OSError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.get("/api/dbc/file")
def dbc_file_details(file_name: str) -> dict:
    try:
        return dbc_manager.get_file_details(file_name)
    except FileNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.post("/api/dbc/upload-open")
async def dbc_upload_open(file: UploadFile = File(...)) -> dict:
    try:
        content = await file.read()
        return dbc_manager.import_uploaded_file(original_name=file.filename or "", content=content)
    except FileNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    finally:
        await file.close()


@app.post("/api/dbc/message")
def update_dbc_message(payload: DbcMessageUpdate) -> dict:
    try:
        return dbc_manager.update_message(
            file_name=payload.file_name,
            current_message_name=payload.current_message_name,
            new_name=payload.new_name,
            comment=payload.comment,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.post("/api/dbc/signal")
def update_dbc_signal(payload: DbcSignalUpdate) -> dict:
    try:
        return dbc_manager.update_signal(
            file_name=payload.file_name,
            message_name=payload.message_name,
            current_signal_name=payload.current_signal_name,
            new_name=payload.new_name,
            scale=payload.scale,
            offset=payload.offset,
            comment=payload.comment,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except KeyError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.get("/healthz")
def healthz() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/readyz")
def readyz():
    return upload_server.readyz()


@app.post("/edit")
async def upload_like_esp(request: Request):
    return await upload_server.upload_like_esp(request)


@app.post("/upload")
async def upload_generic(request: Request):
    return await upload_server.upload_generic(request)


@app.post("/edit-chunked")
async def upload_chunked_endpoint(request: Request):
    return await upload_server.upload_chunked_endpoint(request)


@app.post("/upload-chunked")
async def upload_chunked_alias(request: Request):
    return await upload_server.upload_chunked_alias(request)


if __name__ == "__main__":
    import uvicorn

    host = os.getenv("CAN_GRABBER_SERVER_HOST", os.getenv("CAN_DESKTOP_UI_HOST", "127.0.0.1"))
    port = int(os.getenv("CAN_GRABBER_SERVER_PORT", os.getenv("CAN_DESKTOP_UI_PORT", "8787")))
    uvicorn.run("can_grabber_server.app:app", host=host, port=port, reload=False)
