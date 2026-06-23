"""Stdlib-only request validation for the solver worker."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


class ContractError(ValueError):
    """Raised when a solver request does not match the backend contract."""


def load_request(path: str | Path) -> dict[str, Any]:
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ContractError(f"invalid JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise ContractError("request must be a JSON object")
    validate_request(data)
    return data


def validate_request(data: dict[str, Any]) -> None:
    required = ("schema_version", "run_id", "run_dir", "target", "domain", "materials")
    _require_keys(data, required, "request")
    if data["schema_version"] != 1:
        raise ContractError("schema_version must be 1")
    _require_string(data, "run_id", "request")
    _require_string(data, "run_dir", "request")
    _require_dict(data, "target", "request")
    _require_dict(data, "domain", "request")
    _require_dict(data, "materials", "request")

    target = data["target"]
    _require_keys(target, ("frequencies_hz", "magnitude", "phase_rad"), "target")
    frequencies = _number_list(target, "frequencies_hz", "target")
    magnitude = _number_list(target, "magnitude", "target")
    phase = _number_list(target, "phase_rad", "target")
    if not frequencies:
        raise ContractError("target.frequencies_hz must not be empty")
    if len({len(frequencies), len(magnitude), len(phase)}) != 1:
        raise ContractError("target frequency, magnitude, and phase arrays must be the same length")

    domain = data["domain"]
    _require_keys(domain, ("grid_shape_xyz", "voxel_size_m"), "domain")
    grid = domain["grid_shape_xyz"]
    if not (
        isinstance(grid, list)
        and len(grid) == 3
        and all(isinstance(v, int) and v > 0 for v in grid)
    ):
        raise ContractError("domain.grid_shape_xyz must be three positive integers")
    voxel = domain["voxel_size_m"]
    if not isinstance(voxel, (int, float)) or voxel <= 0:
        raise ContractError("domain.voxel_size_m must be a positive number")

    solver = data.get("solver", {})
    if solver is not None and not isinstance(solver, dict):
        raise ContractError("solver must be an object when provided")
    if solver:
        _positive_int(solver, "max_workers", "solver", required=False)
    optimization = data.get("optimization", {})
    if optimization is not None and not isinstance(optimization, dict):
        raise ContractError("optimization must be an object when provided")
    if optimization:
        _positive_int(optimization, "candidate_count", "optimization", required=False)


def run_dir(data: dict[str, Any]) -> Path:
    return Path(data["run_dir"])


def _require_keys(data: dict[str, Any], keys: tuple[str, ...], where: str) -> None:
    missing = [key for key in keys if key not in data]
    if missing:
        raise ContractError(f"{where} missing required keys: {', '.join(missing)}")


def _require_string(data: dict[str, Any], key: str, where: str) -> None:
    if not isinstance(data[key], str) or not data[key].strip():
        raise ContractError(f"{where}.{key} must be a non-empty string")


def _require_dict(data: dict[str, Any], key: str, where: str) -> None:
    if not isinstance(data[key], dict):
        raise ContractError(f"{where}.{key} must be an object")


def _number_list(data: dict[str, Any], key: str, where: str) -> list[float]:
    value = data[key]
    if not isinstance(value, list) or not all(isinstance(v, (int, float)) for v in value):
        raise ContractError(f"{where}.{key} must be a list of numbers")
    return [float(v) for v in value]


def _positive_int(data: dict[str, Any], key: str, where: str, required: bool = True) -> None:
    if key not in data:
        if required:
            raise ContractError(f"{where}.{key} is required")
        return
    if not isinstance(data[key], int) or data[key] <= 0:
        raise ContractError(f"{where}.{key} must be a positive integer")
