"""Load an exported response.json and apply its measured transfer response.

Requires NumPy and SciPy. Frequencies outside the measured range are left
unchanged instead of inventing an extrapolated material response.
"""

from dataclasses import dataclass
import json
from pathlib import Path

import numpy as np
from scipy.interpolate import interp1d


@dataclass(frozen=True)
class Metamaterial:
    frequency_hz: np.ndarray
    transfer: np.ndarray
    metadata: dict

    @classmethod
    def load(cls, path: str | Path) -> "Metamaterial":
        with Path(path).open(encoding="utf-8") as result_file:
            data = json.load(result_file)

        points = [
            point for point in data["frequency_response"]
            if point["valid"]
            and point["transmission"] is not None
            and point["phase_rad"] is not None
        ]
        if len(points) < 2:
            raise ValueError("The result has fewer than two valid FFT bins.")

        frequency = np.asarray(
            [point["frequency_hz"] for point in points], dtype=float)
        transfer = np.asarray([
            point["transmission"] * np.exp(1j * point["phase_rad"])
            for point in points
        ])
        order = np.argsort(frequency)
        return cls(frequency[order], transfer[order], data)

    def response(self, frequency_hz):
        """Return complex pressure transfer H(f); outside data defaults to 1."""
        requested = np.asarray(frequency_hz, dtype=float)
        flat = np.atleast_1d(requested).ravel()
        response = np.ones(flat.shape, dtype=complex)
        measured = ((flat >= self.frequency_hz[0])
                    & (flat <= self.frequency_hz[-1]))
        response[measured] = interp1d(
            self.frequency_hz, self.transfer, kind="linear",
            bounds_error=True)(flat[measured])
        response = response.reshape(np.atleast_1d(requested).shape)
        return response.item() if requested.ndim == 0 else response

    def apply(self, samples, sample_rate_hz: float, axis: int = -1):
        """Cascade this measured response with a real sampled signal."""
        samples = np.asarray(samples, dtype=float)
        spectrum = np.fft.rfft(samples, axis=axis)
        frequency = np.fft.rfftfreq(samples.shape[axis], 1.0 / sample_rate_hz)
        transfer_shape = [1] * samples.ndim
        transfer_shape[axis] = frequency.size
        spectrum *= self.response(frequency).reshape(transfer_shape)
        return np.fft.irfft(spectrum, n=samples.shape[axis], axis=axis)
