from __future__ import annotations

import importlib.util
import os
from pathlib import Path
from types import ModuleType

PACKAGE_ROOT = Path(__file__).resolve().parent
REPO_ROOT = PACKAGE_ROOT.parent
UPLOAD_SERVER_PATH = REPO_ROOT / "tools" / "can-upload-mock" / "server.py"


def _load_upload_server() -> ModuleType:
    spec = importlib.util.spec_from_file_location("can_upload_server_integrated", UPLOAD_SERVER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load upload server from {UPLOAD_SERVER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


upload_server = _load_upload_server()

SERVER_PORT = int(os.getenv("CAN_GRABBER_SERVER_PORT", os.getenv("CAN_DESKTOP_UI_PORT", "8787")))
upload_server.DEFAULT_PORT = SERVER_PORT
upload_server.advertiser.port = SERVER_PORT
