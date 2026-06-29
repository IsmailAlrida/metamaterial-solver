"""JSONL worker used by the future PyQt6 app."""

from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .contract import ContractError, load_request, run_dir
from .surrogate import run_candidates, write_npy_float64


ARTIFACTS = (
    ("manifest", "manifest.json", "json"),
    ("candidate", "candidate.json", "json"),
    ("response", "response.csv", "csv"),
    ("density", "density.npy", "npy"),
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

    best_candidate: dict[str, Any] | None = None
    stages = ("target", "surrogate_topology", "response", "report")
    for index, stage in enumerate(stages, start=1):
        emit("stage_started", run_id=run_id, stage=stage)
        if stage == "surrogate_topology":
            candidates = run_candidates(request)
            best_candidate = candidates[0]
            _write_density(out_dir, best_candidate)
            _write_candidate(out_dir, request, best_candidate)
        elif stage == "response" and best_candidate is not None:
            _write_response(out_dir, request, best_candidate)
        elif stage == "report":
            _write_summary(out_dir, request, best_candidate)

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


def _write_json(path: Path, data: dict[str, Any]) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _write_candidate(out_dir: Path, request: dict[str, Any], candidate: dict[str, Any]) -> None:
    _write_json(
        out_dir / "candidate.json",
        {
            "schema_version": 1,
            "run_id": request["run_id"],
            "status": "surrogate",
            "topology_method": "acoustic-density-projection",
            "candidate_id": candidate["candidate_id"],
            "seed": candidate["seed"],
            "grid_shape_xyz": request["domain"]["grid_shape_xyz"],
            "scores": _round_scores(candidate["scores"]),
            "artifacts": {
                "density_path": str(out_dir / "density.npy"),
                "surface_stl_path": "",
                "mesh_msh_path": "",
            },
            "notes": "Surrogate scoring only. No FEM, STL export, or Gmsh mesh was run.",
        },
    )


def _write_density(out_dir: Path, candidate: dict[str, Any]) -> None:
    write_npy_float64(out_dir / "density.npy", candidate["density"])


def _write_response(out_dir: Path, request: dict[str, Any], candidate: dict[str, Any]) -> None:
    target = request["target"]
    lines = [
        "frequency_hz,target_real,target_imag,target_magnitude,target_phase_rad,"
        "surrogate_real,surrogate_imag,surrogate_magnitude,surrogate_phase_rad,error_abs"
    ]
    for frequency, target_value, simulated_value, magnitude, phase in zip(
        target["frequencies_hz"],
        candidate["target_response"],
        candidate["simulated_response"],
        target["magnitude"],
        target["phase_rad"],
    ):
        error = abs(simulated_value - target_value)
        lines.append(
            ",".join(
                str(value)
                for value in (
                    frequency,
                    target_value.real,
                    target_value.imag,
                    magnitude,
                    phase,
                    simulated_value.real,
                    simulated_value.imag,
                    abs(simulated_value),
                    _phase(simulated_value),
                    error,
                )
            )
        )
    (out_dir / "response.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_summary(out_dir: Path, request: dict[str, Any], candidate: dict[str, Any] | None) -> None:
    scores = _round_scores(candidate["scores"]) if candidate else {}
    text = "\n".join(
        [
            f"# Run {request['run_id']}",
            "",
            "Status: surrogate backend run.",
            "",
            "This run uses lightweight surrogate math only. No FEM, FEniCSx, Gmsh, STL export, or acoustic field solve was run.",
            "",
            f"Best candidate: {candidate['candidate_id'] if candidate else 'none'}",
            "",
            "Scores:",
            *[f"- {key}: {value}" for key, value in scores.items()],
        ]
    )
    (out_dir / "summary.md").write_text(text + "\n", encoding="utf-8")


def _round_scores(scores: dict[str, float]) -> dict[str, float]:
    return {key: round(float(value), 6) for key, value in scores.items()}


def _phase(value: complex) -> float:
    import math

    return math.atan2(value.imag, value.real)


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
