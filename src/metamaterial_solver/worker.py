"""JSONL worker used by the future PyQt6 app."""

from __future__ import annotations

import json
import random
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .contract import ContractError, load_request, run_dir


ARTIFACTS = (
    ("manifest", "manifest.json", "json"),
    ("candidate", "candidate.json", "json"),
    ("response", "response.csv", "csv"),
    ("summary", "summary.md", "markdown"),
)


def validate(path: str | Path) -> int:
    try:
        request = load_request(path)
    except ContractError as exc:
        emit("failed", error=str(exc))
        return 1
    emit("validated", run_id=request["run_id"])
    return 0


def run(path: str | Path) -> int:
    try:
        request = load_request(path)
    except ContractError as exc:
        emit("failed", error=str(exc))
        return 1

    out_dir = run_dir(request)
    out_dir.mkdir(parents=True, exist_ok=True)
    run_id = request["run_id"]
    emit("started", run_id=run_id, run_dir=str(out_dir))

    if _cancelled(out_dir):
        _write_manifest(out_dir, run_id, "cancelled")
        emit("cancelled", run_id=run_id)
        return 2

    stages = ("target", "topology_stub", "export_stub", "report")
    for index, stage in enumerate(stages, start=1):
        emit("stage_started", run_id=run_id, stage=stage)
        if stage == "topology_stub":
            candidates = _stub_candidates(request)
            _write_candidate(out_dir, request, candidates[0])
        elif stage == "export_stub":
            _write_response(out_dir, request)
        elif stage == "report":
            _write_summary(out_dir, request)

        emit("progress", run_id=run_id, stage=stage, current=index, total=len(stages))
        if _cancelled(out_dir):
            _write_manifest(out_dir, run_id, "cancelled")
            emit("cancelled", run_id=run_id, stage=stage)
            return 2
        emit("stage_finished", run_id=run_id, stage=stage)

    _write_manifest(out_dir, run_id, "finished")
    for name, path_name, kind in ARTIFACTS:
        emit("artifact", run_id=run_id, name=name, path=str(out_dir / path_name), kind=kind)
    emit("finished", run_id=run_id)
    return 0


def emit(event: str, **fields: Any) -> None:
    payload = {
        "schema_version": 1,
        "event": event,
        "time_utc": datetime.now(timezone.utc).isoformat(),
        **fields,
    }
    print(json.dumps(payload, sort_keys=True), flush=True)


def _cancelled(out_dir: Path) -> bool:
    return (out_dir / "cancel.requested").exists()


def _stub_candidates(request: dict[str, Any]) -> list[dict[str, Any]]:
    optimization = request.get("optimization") or {}
    solver = request.get("solver") or {}
    count = int(optimization.get("candidate_count", 1))
    seed = int(optimization.get("seed", 0))
    max_workers = max(1, int(solver.get("max_workers", 1)))

    # ponytail: threads only prove the worker contract; real FEM can own MPI/processes later.
    with ThreadPoolExecutor(max_workers=max_workers) as pool:
        candidates = list(pool.map(lambda index: _one_candidate(request["run_id"], seed, index), range(count)))
    return sorted(candidates, key=lambda item: item["scores"]["transfer_function_error"])


def _one_candidate(run_id: str, seed: int, index: int) -> dict[str, Any]:
    rng = random.Random(seed + index)
    error = round(0.1 + rng.random() * 0.9, 6)
    return {
        "candidate_id": f"{run_id}-c{index:03d}",
        "scores": {
            "transfer_function_error": error,
            "attenuation_score": round(1.0 - error / 2.0, 6),
            "preservation_score": round(1.0 - error / 3.0, 6),
        },
    }


def _write_json(path: Path, data: dict[str, Any]) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _write_candidate(out_dir: Path, request: dict[str, Any], candidate: dict[str, Any]) -> None:
    _write_json(
        out_dir / "candidate.json",
        {
            "schema_version": 1,
            "run_id": request["run_id"],
            "status": "stub",
            "topology_method": "acoustic-density-projection",
            **candidate,
            "artifacts": {
                "density_path": "",
                "surface_stl_path": "",
                "mesh_msh_path": "",
            },
        },
    )


def _write_response(out_dir: Path, request: dict[str, Any]) -> None:
    target = request["target"]
    lines = ["frequency_hz,target_magnitude,target_phase_rad,stub_magnitude,stub_phase_rad"]
    for frequency, magnitude, phase in zip(
        target["frequencies_hz"], target["magnitude"], target["phase_rad"]
    ):
        lines.append(f"{frequency},{magnitude},{phase},{magnitude},{phase}")
    (out_dir / "response.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_summary(out_dir: Path, request: dict[str, Any]) -> None:
    text = "\n".join(
        [
            f"# Run {request['run_id']}",
            "",
            "Status: stub backend run.",
            "",
            "No acoustic physics, topology optimization, STL export, or Gmsh mesh was run.",
        ]
    )
    (out_dir / "summary.md").write_text(text + "\n", encoding="utf-8")


def _write_manifest(out_dir: Path, run_id: str, status: str) -> None:
    _write_json(
        out_dir / "manifest.json",
        {
            "schema_version": 1,
            "run_id": run_id,
            "status": status,
            "artifacts": [
                {"name": name, "path": path_name, "kind": kind}
                for name, path_name, kind in ARTIFACTS
                if status == "finished" or name == "manifest"
            ],
        },
    )


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) != 2 or args[0] not in {"validate", "run"}:
        print("usage: python -m metamaterial_solver [validate|run] request.json", file=sys.stderr)
        return 64
    return validate(args[1]) if args[0] == "validate" else run(args[1])
