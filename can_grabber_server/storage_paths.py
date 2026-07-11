from __future__ import annotations

import os
import shutil
from pathlib import Path

PACKAGE_ROOT = Path(__file__).resolve().parent
LEGACY_STATE_DIR = PACKAGE_ROOT / "state"
LEGACY_RUNTIME_DIR = PACKAGE_ROOT / "runtime"
LEGACY_DBC_DIR = PACKAGE_ROOT / "dbc_files"


def resolve_app_home() -> Path:
    override = os.getenv("CAN_GRABBER_APP_HOME", "").strip()
    if override:
        candidate = Path(override).expanduser()
    elif os.name == "nt":
        base = Path(os.getenv("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
        candidate = base / ".can-grabber"
    else:
        candidate = Path.home() / ".can-grabber"

    if not candidate.is_absolute():
        candidate = (Path.cwd() / candidate).resolve()
    else:
        candidate = candidate.resolve()
    try:
        candidate.mkdir(parents=True, exist_ok=True)
        probe = candidate / ".write-test"
        probe.write_text("ok", encoding="utf-8")
        probe.unlink(missing_ok=True)
        return candidate
    except OSError:
        fallback = (PACKAGE_ROOT.parent / ".runtime" / "can-grabber-server").resolve()
        fallback.mkdir(parents=True, exist_ok=True)
        return fallback


APP_HOME_DIR = resolve_app_home()
STATE_DIR = APP_HOME_DIR / "state"
RUNTIME_DIR = APP_HOME_DIR / "runtime"
DBC_DIR = APP_HOME_DIR / "dbc"


def ensure_storage_layout() -> None:
    APP_HOME_DIR.mkdir(parents=True, exist_ok=True)
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
    DBC_DIR.mkdir(parents=True, exist_ok=True)
    _migrate_legacy_files()


def _migrate_legacy_files() -> None:
    _copy_file_if_missing(LEGACY_STATE_DIR / "config.json", STATE_DIR / "config.json")
    _copy_file_if_missing(LEGACY_STATE_DIR / "dbc_config.json", STATE_DIR / "dbc_config.json")
    _copy_file_if_missing(LEGACY_RUNTIME_DIR / "influxd.log", RUNTIME_DIR / "influxd.log")

    if LEGACY_DBC_DIR.exists():
        for legacy_file in LEGACY_DBC_DIR.glob("*.dbc"):
            _copy_file_if_missing(legacy_file, DBC_DIR / legacy_file.name)


def _copy_file_if_missing(source: Path, destination: Path) -> None:
    if not source.exists() or not source.is_file() or destination.exists():
        return

    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
