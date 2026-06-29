"""Pure helpers shared by the PyQt shell and lightweight tests."""

from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path
from typing import Any


KNOWN_ARTIFACTS = (
    ("manifest", "manifest.json", "json"),
    ("candidate", "candidate.json", "json"),
    ("response", "response.csv", "csv"),
    ("summary", "summary.md", "markdown"),
    ("density", "density.npy", "numpy"),
)


def make_run_id(now: datetime | None = None) -> str:
    stamp = (now or datetime.now()).strftime("%Y%m%d-%H%M%S")
    return f"run-{stamp}"


def default_request(run_dir: str | Path, run_id: str | None = None) -> dict[str, Any]:
    """Return a minimal valid solver request for the current backend contract."""

    return {
        "schema_version": 1,
        "run_id": run_id or make_run_id(),
        "run_dir": str(Path(run_dir)),
        "target": {
            "frequencies_hz": [500, 1000, 1500],
            "magnitude": [1.0, 0.2, 1.0],
            "phase_rad": [0.0, 0.0, 0.0],
        },
        "domain": {"grid_shape_xyz": [8, 8, 8], "voxel_size_m": 0.002},
        "materials": {
            "air": {"density_kg_m3": 1.204, "speed_of_sound_m_s": 343.0},
            "solid": {"name": "rigid-placeholder"},
        },
        "solver": {"mode": "frequency-domain-helmholtz", "max_workers": 1},
        "optimization": {"seed": 1, "candidate_count": 1},
    }


def write_request(run_dir: str | Path, request: dict[str, Any] | None = None) -> Path:
    path = Path(run_dir)
    path.mkdir(parents=True, exist_ok=True)
    payload = request or default_request(path)
    request_path = path / "request.json"
    request_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return request_path


def parse_event_line(line: str) -> dict[str, Any] | None:
    text = line.strip()
    if not text:
        return None
    try:
        event = json.loads(text)
    except json.JSONDecodeError:
        return {"event": "log", "message": text}
    return event if isinstance(event, dict) else {"event": "log", "message": text}


def event_label(event: dict[str, Any]) -> str:
    name = str(event.get("event", "log"))
    if name == "progress":
        current = event.get("current", "?")
        total = event.get("total", "?")
        stage = event.get("stage", "")
        return f"progress {current}/{total} {stage}".strip()
    if "error" in event:
        return f"{name}: {event['error']}"
    if "message" in event:
        return str(event["message"])
    if "stage" in event:
        return f"{name}: {event['stage']}"
    if "path" in event:
        return f"{name}: {event['path']}"
    return name


def progress_percent(event: dict[str, Any]) -> int | None:
    if event.get("event") != "progress":
        return None
    current = event.get("current")
    total = event.get("total")
    if not isinstance(current, (int, float)) or not isinstance(total, (int, float)) or total <= 0:
        return None
    return max(0, min(100, round((current / total) * 100)))


def artifact_from_event(event: dict[str, Any]) -> dict[str, str] | None:
    if event.get("event") != "artifact":
        return None
    path = event.get("path")
    if not isinstance(path, str) or not path:
        return None
    return {
        "name": str(event.get("name") or Path(path).stem),
        "path": path,
        "kind": str(event.get("kind") or Path(path).suffix.lstrip(".") or "file"),
    }


def discover_artifacts(run_dir: str | Path) -> list[dict[str, str]]:
    root = Path(run_dir)
    artifacts: list[dict[str, str]] = []
    seen: set[Path] = set()

    manifest_path = root / "manifest.json"
    if manifest_path.exists():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            manifest = {}
        for item in manifest.get("artifacts", []):
            if not isinstance(item, dict):
                continue
            raw_path = item.get("path")
            if not isinstance(raw_path, str):
                continue
            path = Path(raw_path)
            full_path = path if path.is_absolute() else root / path
            seen.add(full_path)
            artifacts.append(
                {
                    "name": str(item.get("name") or full_path.stem),
                    "path": str(full_path),
                    "kind": str(item.get("kind") or full_path.suffix.lstrip(".") or "file"),
                }
            )

    for name, filename, kind in KNOWN_ARTIFACTS:
        path = root / filename
        if path.exists() and path not in seen:
            artifacts.append({"name": name, "path": str(path), "kind": kind})

    return artifacts
