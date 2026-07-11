from __future__ import annotations

import csv
import os
import shutil
import subprocess
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from threading import Lock
from typing import Any

from can_grabber_server.config_store import read_config, update_section
from can_grabber_server.storage_paths import RUNTIME_DIR, STATE_DIR, ensure_storage_layout

LOG_FILE = RUNTIME_DIR / "influxd.log"
SERVER_BINARY_NAMES = ("influxd.exe", "influxd", "influxdb3.exe", "influxdb3")


@dataclass
class InfluxUiConfig:
    selected_path: str = ""
    candidate_paths: list[str] = field(default_factory=list)
    last_message: str = "Ready to scan for a local InfluxDB installation."
    last_started_pid: int | None = None
    last_started_at: str = ""


class InfluxManager:
    def __init__(self) -> None:
        ensure_storage_layout()
        self._lock = Lock()
        self._config = self._load_config()

    def get_status(self) -> dict[str, Any]:
        with self._lock:
            config = self._snapshot_locked()

        running_processes = self._running_processes()
        selected_path = config["selected_path"]
        selected_exists = bool(selected_path) and Path(selected_path).exists()
        install_dir = str(Path(selected_path).parent) if selected_path else ""

        return {
            "selected_path": selected_path,
            "selected_exists": selected_exists,
            "install_dir": install_dir,
            "candidate_paths": config["candidate_paths"],
            "running": bool(running_processes),
            "running_processes": running_processes,
            "pid": running_processes[0]["pid"] if running_processes else config["last_started_pid"],
            "log_path": str(LOG_FILE),
            "last_started_at": config["last_started_at"],
            "message": config["last_message"],
        }

    def search_installations(self) -> dict[str, Any]:
        candidates = self._discover_candidates()
        message = (
            f"Found {len(candidates)} InfluxDB installation(s)."
            if candidates
            else "No local InfluxDB server binary was found. Try a manual path to influxd.exe or influxdb3.exe."
        )

        with self._lock:
            self._config.candidate_paths = candidates
            if candidates and (not self._config.selected_path or self._config.selected_path not in candidates):
                self._config.selected_path = candidates[0]
            self._config.last_message = message
            self._save_config_locked()

        return self.get_status()

    def set_selected_path(self, raw_path: str) -> dict[str, Any]:
        resolved = self._resolve_binary_path(raw_path)
        resolved_text = str(resolved)

        with self._lock:
            self._config.selected_path = resolved_text
            if resolved_text not in self._config.candidate_paths:
                self._config.candidate_paths.insert(0, resolved_text)
            self._config.last_message = f"Selected InfluxDB binary: {resolved_text}"
            self._save_config_locked()

        return self.get_status()

    def start_influxdb(self) -> dict[str, Any]:
        running_processes = self._running_processes()
        if running_processes:
            with self._lock:
                self._config.last_message = f"InfluxDB is already running with PID {running_processes[0]['pid']}."
                self._save_config_locked()
            return self.get_status()

        with self._lock:
            selected_path = self._config.selected_path

        if not selected_path:
            status = self.search_installations()
            selected_path = status["selected_path"]

        if not selected_path:
            raise FileNotFoundError("No InfluxDB executable is selected. Run a scan first or paste the path manually.")

        executable = self._resolve_binary_path(selected_path)
        RUNTIME_DIR.mkdir(parents=True, exist_ok=True)

        launch_time = datetime.now().isoformat(timespec="seconds")
        with LOG_FILE.open("a", encoding="utf-8") as log_handle:
            log_handle.write(f"\n[{launch_time}] Launching {executable}\n")
            process = subprocess.Popen(
                [str(executable)],
                cwd=str(executable.parent),
                stdin=subprocess.DEVNULL,
                stdout=log_handle,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )

        time.sleep(1.0)
        exit_code = process.poll()
        if exit_code is not None:
            details = self._tail_log()
            raise RuntimeError(
                f"InfluxDB exited immediately with code {exit_code}. "
                f"{details or 'See <app-home>/runtime/influxd.log for details.'}"
            )

        with self._lock:
            self._config.selected_path = str(executable)
            self._config.last_started_pid = process.pid
            self._config.last_started_at = launch_time
            self._config.last_message = f"Started InfluxDB from {executable}."
            self._save_config_locked()

        return self.get_status()

    def _load_config(self) -> InfluxUiConfig:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        config = read_config()
        selected_path = self._config_text(config, "influx", "selected_path")
        candidate_paths = self._config_text(config, "influx", "candidate_paths")
        last_message = self._config_text(config, "influx", "last_message") or InfluxUiConfig.last_message
        last_started_pid = self._config_text(config, "influx", "last_started_pid")
        last_started_at = self._config_text(config, "influx", "last_started_at")
        return InfluxUiConfig(
            selected_path=selected_path,
            candidate_paths=[
                line.strip()
                for line in candidate_paths.splitlines()
                if line.strip()
            ],
            last_message=last_message,
            last_started_pid=self._coerce_int(last_started_pid),
            last_started_at=last_started_at,
        )

    def _save_config_locked(self) -> None:
        update_section(
            "influx",
            {
                "selected_path": self._config.selected_path,
                "candidate_paths": "\n".join(self._config.candidate_paths),
                "last_message": self._config.last_message,
                "last_started_pid": "" if self._config.last_started_pid is None else str(self._config.last_started_pid),
                "last_started_at": self._config.last_started_at,
            },
        )

    def _snapshot_locked(self) -> dict[str, Any]:
        return {
            "selected_path": self._config.selected_path,
            "candidate_paths": list(self._config.candidate_paths),
            "last_message": self._config.last_message,
            "last_started_pid": self._config.last_started_pid,
            "last_started_at": self._config.last_started_at,
        }

    def _discover_candidates(self) -> list[str]:
        candidates: list[str] = []

        def add_candidate(path_like: str | Path) -> None:
            try:
                resolved = self._resolve_binary_path(str(path_like))
            except FileNotFoundError:
                return
            normalized = str(resolved)
            if normalized not in candidates:
                candidates.append(normalized)

        for binary_name in SERVER_BINARY_NAMES:
            path_like = shutil.which(binary_name)
            if path_like:
                add_candidate(path_like)

        if os.name == "nt":
            for binary_name in SERVER_BINARY_NAMES:
                try:
                    result = subprocess.run(
                        ["where.exe", binary_name],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                except OSError:
                    continue
                if result.returncode != 0:
                    continue
                for line in result.stdout.splitlines():
                    if line.strip():
                        add_candidate(line.strip())

        for expected_path in self._expected_locations():
            add_candidate(expected_path)

        for root in self._search_roots():
            for discovered_path in self._scan_root(root):
                add_candidate(discovered_path)

        for root in self._portable_search_roots():
            for candidate_dir in self._portable_install_dirs(root):
                for discovered_path in self._scan_root(candidate_dir, max_depth=3):
                    add_candidate(discovered_path)

        return candidates

    def _expected_locations(self) -> list[Path]:
        home = Path.home()
        local_app_data = Path(os.getenv("LOCALAPPDATA", home / "AppData" / "Local"))
        program_files = Path(os.getenv("ProgramFiles", "C:/Program Files"))
        program_files_x86 = Path(os.getenv("ProgramFiles(x86)", "C:/Program Files (x86)"))

        return [
            program_files / "InfluxData" / "influxdb" / "influxd.exe",
            program_files / "InfluxData" / "influxdb2" / "influxd.exe",
            program_files / "InfluxData" / "influxdb3" / "influxdb3.exe",
            program_files / "InfluxData" / "influxdb3-core" / "influxdb3.exe",
            program_files_x86 / "InfluxData" / "influxdb" / "influxd.exe",
            program_files_x86 / "InfluxData" / "influxdb2" / "influxd.exe",
            program_files_x86 / "InfluxData" / "influxdb3" / "influxdb3.exe",
            program_files_x86 / "InfluxData" / "influxdb3-core" / "influxdb3.exe",
            local_app_data / "Programs" / "InfluxData" / "influxdb" / "influxd.exe",
            local_app_data / "Programs" / "InfluxData" / "influxdb2" / "influxd.exe",
            local_app_data / "Programs" / "InfluxData" / "influxdb3" / "influxdb3.exe",
            local_app_data / "Programs" / "InfluxData" / "influxdb3-core" / "influxdb3.exe",
            home / "scoop" / "apps" / "influxdb" / "current" / "influxd.exe",
            home / "scoop" / "apps" / "influxdb3" / "current" / "influxdb3.exe",
        ]

    def _search_roots(self) -> list[Path]:
        home = Path.home()
        local_app_data = Path(os.getenv("LOCALAPPDATA", home / "AppData" / "Local"))
        program_files = Path(os.getenv("ProgramFiles", "C:/Program Files"))
        program_files_x86 = Path(os.getenv("ProgramFiles(x86)", "C:/Program Files (x86)"))

        roots = [
            program_files / "InfluxData",
            program_files_x86 / "InfluxData",
            local_app_data / "Programs" / "InfluxData",
            local_app_data / "InfluxData",
            home / "scoop" / "apps" / "influxdb",
            home / "scoop" / "apps" / "influxdb3",
        ]

        return [root for root in roots if root.exists()]

    def _portable_search_roots(self) -> list[Path]:
        home = Path.home()
        roots = [
            home / "Downloads",
            home / "Desktop",
        ]
        return [root for root in roots if root.exists()]

    def _portable_install_dirs(self, root: Path) -> list[Path]:
        candidates: list[Path] = []
        try:
            for entry in root.iterdir():
                if not entry.is_dir():
                    continue
                if "influx" not in entry.name.lower():
                    continue
                candidates.append(entry)
        except OSError:
            return []
        return candidates

    def _scan_root(self, root: Path, max_depth: int = 4) -> list[Path]:
        discoveries: list[Path] = []
        if not root.exists():
            return discoveries

        root = root.resolve()
        target_names = {name.lower() for name in SERVER_BINARY_NAMES}

        for current_root, dir_names, file_names in os.walk(root):
            current_path = Path(current_root)
            relative_parts = current_path.relative_to(root).parts
            if len(relative_parts) >= max_depth:
                dir_names[:] = []
            else:
                dir_names[:] = [name for name in dir_names if not name.startswith(".")]

            for file_name in file_names:
                if file_name.lower() in target_names:
                    discoveries.append(current_path / file_name)

        return discoveries

    def _resolve_binary_path(self, raw_path: str) -> Path:
        cleaned = raw_path.strip().strip('"').strip("'")
        if not cleaned:
            raise FileNotFoundError("No path was provided.")

        candidate = Path(cleaned).expanduser()
        candidate = candidate.resolve() if candidate.is_absolute() else (Path.cwd() / candidate).resolve()

        if candidate.is_dir():
            direct_hits = [candidate / name for name in SERVER_BINARY_NAMES if (candidate / name).exists()]
            if direct_hits:
                return direct_hits[0]
            nested_hits = self._scan_root(candidate, max_depth=2)
            if nested_hits:
                return nested_hits[0]

        if candidate.exists() and candidate.is_file() and candidate.name.lower() in {name.lower() for name in SERVER_BINARY_NAMES}:
            return candidate

        if os.name == "nt":
            supported_names = {name.lower() for name in SERVER_BINARY_NAMES}
            if candidate.suffix.lower() != ".exe":
                exe_candidate = candidate.with_suffix(".exe")
                if exe_candidate.exists() and exe_candidate.is_file() and exe_candidate.name.lower() in supported_names:
                    return exe_candidate
            if candidate.exists() and candidate.is_file() and candidate.name.lower() in supported_names:
                return candidate

        raise FileNotFoundError(f"InfluxDB executable not found for path: {raw_path}")

    def _running_processes(self) -> list[dict[str, Any]]:
        if os.name == "nt":
            return self._running_processes_windows()
        return self._running_processes_posix()

    def _running_processes_windows(self) -> list[dict[str, Any]]:
        processes: list[dict[str, Any]] = []
        for binary_name in ("influxd.exe", "influxdb3.exe"):
            try:
                result = subprocess.run(
                    ["tasklist", "/FI", f"IMAGENAME eq {binary_name}", "/FO", "CSV", "/NH"],
                    capture_output=True,
                    text=True,
                    check=False,
                )
            except OSError:
                continue

            if result.returncode != 0:
                continue

            text = result.stdout.strip()
            if not text or text.startswith("INFO:"):
                continue

            for row in csv.reader(text.splitlines()):
                if len(row) < 2:
                    continue
                pid = self._coerce_int(row[1])
                if pid is None:
                    continue
                processes.append(
                    {
                        "name": row[0],
                        "pid": pid,
                    }
                )
        return processes

    def _running_processes_posix(self) -> list[dict[str, Any]]:
        try:
            result = subprocess.run(
                ["ps", "-A", "-o", "pid=,comm="],
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError:
            return []

        if result.returncode != 0:
            return []

        processes: list[dict[str, Any]] = []
        for line in result.stdout.splitlines():
            parts = line.strip().split(maxsplit=1)
            if len(parts) != 2:
                continue
            pid = self._coerce_int(parts[0])
            command = parts[1].strip()
            if pid is None or Path(command).name not in {"influxd", "influxdb3"}:
                continue
            processes.append({"name": command, "pid": pid})
        return processes

    def _tail_log(self, max_lines: int = 6) -> str:
        if not LOG_FILE.exists():
            return ""

        try:
            lines = LOG_FILE.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            return ""

        snippet = lines[-max_lines:]
        if not snippet:
            return ""

        return "Recent log: " + " | ".join(line.strip() for line in snippet if line.strip())

    @staticmethod
    def _coerce_int(value: Any) -> int | None:
        try:
            return int(value)
        except (TypeError, ValueError):
            return None

    @staticmethod
    def _config_text(config, section: str, option: str) -> str:
        value = config.get(section, option, fallback="")
        return "" if value is None else str(value).strip()
