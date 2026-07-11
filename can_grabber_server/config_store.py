from __future__ import annotations

import configparser
import json
from pathlib import Path
from threading import Lock

from can_grabber_server.storage_paths import APP_HOME_DIR, LEGACY_STATE_DIR, ensure_storage_layout

CONFIG_FILE = APP_HOME_DIR / "config.ini"

_CONFIG_LOCK = Lock()


def ensure_config_file() -> Path:
    ensure_storage_layout()
    with _CONFIG_LOCK:
        config = _load_config_locked()
        changed = not CONFIG_FILE.exists()
        changed = _apply_defaults(config) or changed
        changed = _migrate_legacy_json_locked(config) or changed
        if changed:
            _save_config_locked(config)
    return CONFIG_FILE


def read_config() -> configparser.ConfigParser:
    ensure_config_file()
    with _CONFIG_LOCK:
        config = _load_config_locked()
        _apply_defaults(config)
        return config


def update_section(section: str, values: dict[str, str]) -> None:
    ensure_config_file()
    with _CONFIG_LOCK:
        config = _load_config_locked()
        _apply_defaults(config)
        if not config.has_section(section):
            config.add_section(section)
        for key, value in values.items():
            config.set(section, key, value)
        _save_config_locked(config)


def _load_config_locked() -> configparser.ConfigParser:
    config = configparser.ConfigParser()
    if CONFIG_FILE.exists():
        config.read(CONFIG_FILE, encoding="utf-8")
    return config


def _save_config_locked(config: configparser.ConfigParser) -> None:
    APP_HOME_DIR.mkdir(parents=True, exist_ok=True)
    with CONFIG_FILE.open("w", encoding="utf-8") as handle:
        config.write(handle)


def _apply_defaults(config: configparser.ConfigParser) -> bool:
    changed = False
    if not config.has_section("app"):
        config.add_section("app")
        changed = True
    if not config.has_option("app", "config_version"):
        config.set("app", "config_version", "1")
        changed = True

    if not config.has_section("dbc"):
        config.add_section("dbc")
        changed = True
    for key in ("source_directory_path", "last_file_name", "last_message"):
        if not config.has_option("dbc", key):
            config.set("dbc", key, "")
            changed = True

    if not config.has_section("influx"):
        config.add_section("influx")
        changed = True
    for key in ("selected_path", "candidate_paths", "last_message", "last_started_pid", "last_started_at"):
        if not config.has_option("influx", key):
            config.set("influx", key, "")
            changed = True
    return changed


def _migrate_legacy_json_locked(config: configparser.ConfigParser) -> bool:
    changed = False
    changed = _migrate_json_file(
        config=config,
        source=LEGACY_STATE_DIR / "dbc_config.json",
        section="dbc",
        key_map={
            "source_directory_path": "source_directory_path",
            "directory_path": "source_directory_path",
            "last_file_name": "last_file_name",
            "last_message": "last_message",
        },
    ) or changed
    changed = _migrate_json_file(
        config=config,
        source=LEGACY_STATE_DIR / "config.json",
        section="influx",
        key_map={
            "selected_path": "selected_path",
            "candidate_paths": "candidate_paths",
            "last_message": "last_message",
            "last_started_pid": "last_started_pid",
            "last_started_at": "last_started_at",
        },
    ) or changed
    return changed


def _migrate_json_file(
    *,
    config: configparser.ConfigParser,
    source: Path,
    section: str,
    key_map: dict[str, str],
) -> bool:
    if not source.exists() or not source.is_file():
        return False

    try:
        payload = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False

    if not isinstance(payload, dict):
        return False

    changed = False
    for source_key, dest_key in key_map.items():
        if config.get(section, dest_key, fallback="").strip():
            continue
        if source_key not in payload:
            continue

        value = payload[source_key]
        if dest_key == "candidate_paths":
            text_value = _stringify_lines(value)
        else:
            text_value = "" if value is None else str(value)
        config.set(section, dest_key, text_value)
        changed = True
    return changed


def _stringify_lines(value: object) -> str:
    if isinstance(value, list):
        return "\n".join(str(item).strip() for item in value if str(item).strip())
    if value is None:
        return ""
    return str(value).strip()
