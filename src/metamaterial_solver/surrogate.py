"""NumPy surrogate math before real FEM exists."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any

import numpy as np


def target_complex(target: dict[str, Any]) -> np.ndarray:
    magnitude = np.asarray(target["magnitude"], dtype=float)
    phase = np.asarray(target["phase_rad"], dtype=float)
    return magnitude * np.exp(1j * phase)


def run_candidates(request: dict[str, Any]) -> list[dict[str, Any]]:
    optimization = request.get("optimization") or {}
    solver = request.get("solver") or {}
    count = int(optimization.get("candidate_count", 1))
    max_workers = max(1, int(solver.get("max_workers", 1)))

    # ponytail: threads only prove parallel candidate plumbing; real FEM can own MPI later.
    with ThreadPoolExecutor(max_workers=max_workers) as pool:
        candidates = list(pool.map(lambda index: evaluate_candidate(request, index), range(count)))
    return sorted(candidates, key=lambda item: item["rank_score"])


def evaluate_candidate(request: dict[str, Any], index: int) -> dict[str, Any]:
    optimization = request.get("optimization") or {}
    seed = int(optimization.get("seed", 0))
    density = density_grid(request["domain"]["grid_shape_xyz"], seed + index)
    threshold = float(optimization.get("projection_threshold", request["domain"].get("projection_threshold", 0.5)))
    projected = project_density(density, threshold)

    frequencies = np.asarray(request["target"]["frequencies_hz"], dtype=float)
    target = target_complex(request["target"])
    simulated = surrogate_response(frequencies, projected)
    scores = score_response(frequencies, target, simulated, request["target"], density, projected)
    return {
        "candidate_id": f"{request['run_id']}-c{index:03d}",
        "seed": seed + index,
        "density": density,
        "projected": projected,
        "target_response": target,
        "simulated_response": simulated,
        "scores": scores,
        "rank_score": scores["transfer_function_error"]
        + scores["manufacturing_penalty"]
        + scores["robustness_penalty"]
        - 0.1 * scores["attenuation_score"]
        - 0.1 * scores["preservation_score"],
    }


def density_grid(shape: list[int], seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    density = rng.random(tuple(shape), dtype=float)
    axes = np.indices(tuple(shape), dtype=float)
    gradient = axes.sum(axis=0) / max(sum(shape) - 3, 1)
    return np.clip(0.75 * density + 0.25 * gradient, 0.0, 1.0)


def project_density(density: np.ndarray, threshold: float = 0.5) -> np.ndarray:
    return (density >= threshold).astype(np.uint8)


def surrogate_response(frequencies: np.ndarray, projected: np.ndarray) -> np.ndarray:
    fill = float(projected.mean())
    contrast = float(projected.std())
    f_min = float(frequencies.min())
    span = max(float(frequencies.max() - f_min), 1.0)
    center = f_min + span * (0.15 + 0.7 * fill)
    width = span * (0.08 + 0.3 * max(1.0 - contrast, 0.05))
    depth = float(np.clip(0.2 + contrast, 0.05, 0.95))
    notch = np.exp(-((frequencies - center) / width) ** 2)
    magnitude = np.clip(1.0 - depth * notch, 0.0, 1.2)
    phase = -0.75 * notch + (fill - 0.5) * 0.15 * ((frequencies - f_min) / span)
    return magnitude * np.exp(1j * phase)


def score_response(
    frequencies: np.ndarray,
    target: np.ndarray,
    simulated: np.ndarray,
    target_spec: dict[str, Any],
    density: np.ndarray,
    projected: np.ndarray,
) -> dict[str, float]:
    error = np.abs(simulated - target)
    attenuation_mask = band_mask(frequencies, target_spec.get("attenuation_bands_hz", []))
    preserve_mask = band_mask(frequencies, target_spec.get("preserve_bands_hz", []))

    attenuation = 0.0
    if attenuation_mask.any():
        attenuation = 1.0 - float(np.clip(np.abs(simulated[attenuation_mask]).mean(), 0.0, 1.0))

    preservation = 0.0
    if preserve_mask.any():
        preserve_error = np.abs(simulated[preserve_mask] - target[preserve_mask]).mean()
        preservation = 1.0 - float(np.clip(preserve_error, 0.0, 1.0))

    porosity = float(projected.mean())
    manufacturing_penalty = abs(porosity - 0.5)
    robustness_penalty = float(np.abs(density - projected).mean())
    return {
        "transfer_function_error": float(error.mean()),
        "attenuation_score": float(attenuation),
        "preservation_score": float(preservation),
        "manufacturing_penalty": float(manufacturing_penalty),
        "robustness_penalty": float(robustness_penalty),
    }


def band_mask(frequencies: np.ndarray, bands: list[list[float]]) -> np.ndarray:
    mask = np.zeros(frequencies.shape, dtype=bool)
    for band in bands:
        low, high = sorted((float(band[0]), float(band[1])))
        mask |= (frequencies >= low) & (frequencies <= high)
    return mask


def write_npy_float64(path: str | Path, density: np.ndarray) -> None:
    np.save(path, density)
