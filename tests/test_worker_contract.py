import contextlib
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from metamaterial_solver.contract import ContractError, load_request  # noqa: E402
from metamaterial_solver.worker import run  # noqa: E402


def request(run_dir: Path) -> dict:
    return {
        "schema_version": 1,
        "run_id": "test-run",
        "run_dir": str(run_dir),
        "target": {
            "frequencies_hz": [500, 1000, 1500],
            "magnitude": [1.0, 0.2, 1.0],
            "phase_rad": [0.0, 0.0, 0.0],
            "attenuation_bands_hz": [[900, 1100]],
            "preserve_bands_hz": [[400, 700], [1300, 1600]],
        },
        "domain": {"grid_shape_xyz": [8, 8, 8], "voxel_size_m": 0.002},
        "materials": {
            "air": {"density_kg_m3": 1.204, "speed_of_sound_m_s": 343.0},
            "solid": {"name": "rigid-placeholder"},
        },
        "solver": {"mode": "frequency-domain-helmholtz", "max_workers": 2},
        "optimization": {"seed": 7, "candidate_count": 3},
    }


class WorkerContractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.request_path = self.tmp / "request.json"

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def write_request(self, data):
        self.request_path.write_text(json.dumps(data), encoding="utf-8")

    def test_valid_request_loads(self):
        self.write_request(request(self.tmp / "run"))
        self.assertEqual(load_request(self.request_path)["run_id"], "test-run")

    def test_missing_required_field_fails(self):
        data = request(self.tmp / "run")
        del data["target"]
        self.write_request(data)
        with self.assertRaises(ContractError):
            load_request(self.request_path)

    def test_run_emits_jsonl_and_writes_artifacts(self):
        run_dir = self.tmp / "run"
        self.write_request(request(run_dir))
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            code = run(self.request_path)
        self.assertEqual(code, 0)
        events = [json.loads(line) for line in stdout.getvalue().splitlines()]
        self.assertEqual(events[-1]["event"], "finished")
        for name in ("manifest.json", "candidate.json", "response.csv", "density.npy", "summary.md"):
            self.assertTrue((run_dir / name).exists(), name)

    def test_cancel_sentinel_returns_cancelled(self):
        run_dir = self.tmp / "run"
        run_dir.mkdir()
        (run_dir / "cancel.requested").write_text("", encoding="utf-8")
        self.write_request(request(run_dir))
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            code = run(self.request_path)
        events = [json.loads(line) for line in stdout.getvalue().splitlines()]
        self.assertEqual(code, 2)
        self.assertEqual(events[-1]["event"], "cancelled")


if __name__ == "__main__":
    unittest.main()
