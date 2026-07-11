from __future__ import annotations

import shutil
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from threading import Lock
from typing import Any

import cantools

from can_grabber_server.config_store import read_config, update_section
from can_grabber_server.storage_paths import DBC_DIR, STATE_DIR, ensure_storage_layout


@dataclass
class DbcUiConfig:
    source_directory_path: str = ""
    last_file_name: str = ""
    last_message: str = "DBC workspace ready."


class DbcManager:
    def __init__(self) -> None:
        ensure_storage_layout()
        self._lock = Lock()
        self._config = self._load_config()
        self._ensure_storage_directory()

    def get_status(self) -> dict[str, Any]:
        with self._lock:
            source_directory_path = self._config.source_directory_path
            last_file_name = self._config.last_file_name
            message = self._config.last_message

        storage_directory = self._ensure_storage_directory()
        files = self._scan_files(storage_directory)

        return {
            "directory_path": str(storage_directory),
            "storage_directory_path": str(storage_directory),
            "source_directory_path": source_directory_path,
            "directory_exists": storage_directory.exists(),
            "file_count": len(files),
            "files": files,
            "selected_file_name": last_file_name if any(item["name"] == last_file_name for item in files) else "",
            "message": message,
        }

    def set_directory(self, raw_path: str) -> dict[str, Any]:
        source_directory = self._resolve_source_directory(raw_path)
        imported_count = self._import_directory(source_directory)

        with self._lock:
            self._config.source_directory_path = str(source_directory)
            self._config.last_message = (
                f"Imported {imported_count} DBC file(s) from {source_directory} into {DBC_DIR}."
                if imported_count
                else f"No DBC files found in {source_directory}. Storage stays at {DBC_DIR}."
            )
            self._save_config_locked()

        return self.get_status()

    def scan_directory(self) -> dict[str, Any]:
        storage_directory = self._ensure_storage_directory()
        files = self._scan_files(storage_directory)

        with self._lock:
            self._config.last_message = (
                f"Scanned persistent DBC store {storage_directory} and found {len(files)} DBC file(s)."
                if files
                else f"Scanned persistent DBC store {storage_directory}. No DBC files found yet."
            )
            self._save_config_locked()

        return self.get_status()

    def get_file_details(self, file_name: str) -> dict[str, Any]:
        path = self._resolve_file(file_name)
        database = cantools.database.load_file(path)
        details = self._database_details(path, database)

        with self._lock:
            self._config.last_file_name = path.name
            self._config.last_message = f"Loaded DBC file: {path.name}"
            self._save_config_locked()

        return details

    def import_uploaded_file(self, *, original_name: str, content: bytes) -> dict[str, Any]:
        safe_name = Path(original_name or "").name.strip()
        if not safe_name:
            raise FileNotFoundError("No DBC file name was provided.")
        if Path(safe_name).suffix.lower() != ".dbc":
            raise ValueError("Only .dbc files are supported.")
        if not content:
            raise ValueError("The uploaded DBC file is empty.")

        storage_directory = self._ensure_storage_directory()
        destination = (storage_directory / safe_name).resolve()
        try:
            destination.relative_to(storage_directory.resolve())
        except ValueError as exc:
            raise FileNotFoundError("Uploaded DBC file must stay inside the persistent store.") from exc

        temp_path = storage_directory / f".{safe_name}.upload.tmp.dbc"
        try:
            temp_path.write_bytes(content)
            database = cantools.database.load_file(temp_path)
            temp_path.replace(destination)
        finally:
            if temp_path.exists():
                temp_path.unlink(missing_ok=True)

        details = self._database_details(destination, database)
        with self._lock:
            self._config.last_file_name = destination.name
            self._config.last_message = f"Imported and opened {destination.name} in {storage_directory}."
            self._save_config_locked()

        return details

    def update_message(
        self,
        *,
        file_name: str,
        current_message_name: str,
        new_name: str,
        comment: str,
    ) -> dict[str, Any]:
        path = self._resolve_file(file_name)
        database = cantools.database.load_file(path)
        message = self._find_message(database, current_message_name)

        message.name = new_name.strip() or message.name
        message.comment = comment
        database.refresh()
        path.write_text(database.as_dbc_string(), encoding="utf-8")

        with self._lock:
            self._config.last_file_name = path.name
            self._config.last_message = f"Saved message '{message.name}' in {path.name}."
            self._save_config_locked()

        return self._database_details(path, cantools.database.load_file(path))

    def update_signal(
        self,
        *,
        file_name: str,
        message_name: str,
        current_signal_name: str,
        new_name: str,
        scale: float,
        offset: float,
        comment: str,
    ) -> dict[str, Any]:
        path = self._resolve_file(file_name)
        database = cantools.database.load_file(path)
        message = self._find_message(database, message_name)
        signal = self._find_signal(message, current_signal_name)

        signal.name = new_name.strip() or signal.name
        signal.scale = scale
        signal.offset = offset
        signal.comment = comment
        database.refresh()
        path.write_text(database.as_dbc_string(), encoding="utf-8")

        with self._lock:
            self._config.last_file_name = path.name
            self._config.last_message = f"Saved signal '{signal.name}' in {path.name}."
            self._save_config_locked()

        return self._database_details(path, cantools.database.load_file(path))

    def _load_config(self) -> DbcUiConfig:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        config = read_config()
        return DbcUiConfig(
            source_directory_path=self._config_text(config, "dbc", "source_directory_path"),
            last_file_name=self._config_text(config, "dbc", "last_file_name"),
            last_message=self._config_text(config, "dbc", "last_message") or DbcUiConfig.last_message,
        )

    def _save_config_locked(self) -> None:
        update_section(
            "dbc",
            {
                "source_directory_path": self._config.source_directory_path,
                "last_file_name": self._config.last_file_name,
                "last_message": self._config.last_message,
            },
        )

    def _ensure_storage_directory(self) -> Path:
        DBC_DIR.mkdir(parents=True, exist_ok=True)
        return DBC_DIR

    def _resolve_source_directory(self, raw_path: str) -> Path:
        cleaned = raw_path.strip()
        if not cleaned:
            raise FileNotFoundError("No DBC source directory was provided.")

        candidate = Path(cleaned).expanduser()
        if not candidate.is_absolute():
            candidate = (Path.cwd() / candidate).resolve()
        else:
            candidate = candidate.resolve()

        if not candidate.exists() or not candidate.is_dir():
            raise FileNotFoundError(f"DBC source directory not found: {raw_path}")
        return candidate

    def _import_directory(self, source_directory: Path) -> int:
        storage_directory = self._ensure_storage_directory()
        imported_count = 0

        for source_file in sorted(source_directory.glob("*.dbc"), key=lambda item: item.name.lower()):
            destination = storage_directory / source_file.name
            if source_file.resolve() != destination.resolve():
                shutil.copy2(source_file, destination)
            imported_count += 1

        return imported_count

    def _scan_files(self, directory: Path) -> list[dict[str, Any]]:
        if not directory.exists():
            return []

        files: list[dict[str, Any]] = []
        for path in sorted(directory.iterdir(), key=lambda item: item.name.lower()):
            if not path.is_file() or path.suffix.lower() != ".dbc":
                continue
            stat = path.stat()
            files.append(
                {
                    "name": path.name,
                    "size": stat.st_size,
                    "modified": datetime.fromtimestamp(stat.st_mtime).isoformat(timespec="seconds"),
                    "path": str(path),
                }
            )
        return files

    def _resolve_file(self, file_name: str) -> Path:
        if not file_name.strip():
            raise FileNotFoundError("No DBC file name was provided.")

        directory = self._ensure_storage_directory().resolve()
        candidate = (directory / file_name).resolve()
        try:
            candidate.relative_to(directory)
        except ValueError as exc:
            raise FileNotFoundError("DBC file must stay inside the selected directory.") from exc

        if not candidate.exists() or not candidate.is_file():
            raise FileNotFoundError(f"DBC file not found: {file_name}")
        if candidate.suffix.lower() != ".dbc":
            raise FileNotFoundError("Only .dbc files are supported by this example.")
        return candidate

    def _database_details(self, path: Path, database: cantools.database.can.Database) -> dict[str, Any]:
        messages: list[dict[str, Any]] = []
        for message in database.messages:
            messages.append(
                {
                    "name": message.name,
                    "frame_id": message.frame_id,
                    "frame_id_hex": f"0x{message.frame_id:X}",
                    "length": message.length,
                    "comment": message.comment or "",
                    "signal_count": len(message.signals),
                    "signals": [
                        {
                            "name": signal.name,
                            "scale": signal.scale,
                            "offset": signal.offset,
                            "unit": signal.unit or "",
                            "comment": signal.comment or "",
                            "minimum": signal.minimum,
                            "maximum": signal.maximum,
                            "is_signed": signal.is_signed,
                        }
                        for signal in message.signals
                    ],
                }
            )

        return {
            "file_name": path.name,
            "file_path": str(path),
            "message_count": len(messages),
            "messages": messages,
            "message": f"Opened {path.name} with {len(messages)} message(s).",
        }

    @staticmethod
    def _find_message(database: cantools.database.can.Database, message_name: str):
        for message in database.messages:
            if message.name == message_name:
                return message
        raise KeyError(f"Message not found: {message_name}")

    @staticmethod
    def _find_signal(message: cantools.database.can.Message, signal_name: str):
        for signal in message.signals:
            if signal.name == signal_name:
                return signal
        raise KeyError(f"Signal not found: {signal_name}")

    @staticmethod
    def _config_text(config, section: str, option: str) -> str:
        value = config.get(section, option, fallback="")
        return "" if value is None else str(value).strip()
