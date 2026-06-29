import sys
import unittest
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from metamaterial_solver.surrogate import (  # noqa: E402
    band_mask,
    density_grid,
    project_density,
    score_response,
    target_complex,
)


class SolverMathTest(unittest.TestCase):
    def test_target_complex_uses_magnitude_and_phase(self):
        values = target_complex({"magnitude": [1.0, 2.0], "phase_rad": [0.0, np.pi / 2]})
        self.assertTrue(np.allclose(values, np.array([1.0 + 0.0j, 0.0 + 2.0j])))

    def test_band_mask_includes_points_inside_bands(self):
        mask = band_mask(np.array([100, 200, 300, 400]), [[150, 350]])
        self.assertEqual(mask.tolist(), [False, True, True, False])

    def test_score_rewards_attenuation_and_preservation(self):
        frequencies = np.array([500.0, 1000.0, 1500.0])
        target = np.array([1.0 + 0j, 0.0 + 0j, 1.0 + 0j])
        simulated = np.array([0.9 + 0j, 0.1 + 0j, 0.95 + 0j])
        density = np.full((2, 2, 2), 0.6)
        projected = project_density(density, 0.5)
        scores = score_response(
            frequencies,
            target,
            simulated,
            {"attenuation_bands_hz": [[900, 1100]], "preserve_bands_hz": [[400, 700], [1400, 1600]]},
            density,
            projected,
        )
        self.assertGreater(scores["attenuation_score"], 0.8)
        self.assertGreater(scores["preservation_score"], 0.8)

    def test_density_grid_is_deterministic(self):
        first = density_grid([3, 3, 3], 123)
        second = density_grid([3, 3, 3], 123)
        self.assertTrue(np.array_equal(first, second))


if __name__ == "__main__":
    unittest.main()
