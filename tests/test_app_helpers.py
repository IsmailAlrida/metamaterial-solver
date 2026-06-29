import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from metamaterial_app.helpers import (  # noqa: E402
    artifact_from_event,
    default_request,
    discover_artifacts,
    event_label,
    parse_event_line,
    progress_percent,
    write_request,
)


class AppHelpersTest(unittest.TestCase):
    def test_default_request_and_write_request(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = Path(tmp) / "run-a"
            request = default_request(run_dir, "run-a")
            request_path = write_request(run_dir, request)
            data = json.loads(request_path.read_text(encoding="utf-8"))
            self.assertEqual(data["run_id"], "run-a")
            self.assertEqual(data["run_dir"], str(run_dir))

    def test_event_parsing_progress_and_artifact(self):
        progress = parse_event_line('{"event":"progress","current":2,"total":4,"stage":"mesh"}')
        self.assertEqual(progress_percent(progress), 50)
        self.assertEqual(event_label(progress), "progress 2/4 mesh")

        artifact = parse_event_line('{"event":"artifact","name":"summary","path":"summary.md","kind":"markdown"}')
        self.assertEqual(artifact_from_event(artifact)["name"], "summary")

    def test_non_json_stdout_becomes_log_event(self):
        event = parse_event_line("plain worker output")
        self.assertEqual(event["event"], "log")
        self.assertIn("plain worker output", event_label(event))

    def test_discover_artifacts_uses_manifest_and_known_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = Path(tmp)
            (run_dir / "manifest.json").write_text(
                json.dumps({"artifacts": [{"name": "summary", "path": "summary.md", "kind": "markdown"}]}),
                encoding="utf-8",
            )
            (run_dir / "summary.md").write_text("# Summary\n", encoding="utf-8")
            (run_dir / "candidate.json").write_text("{}\n", encoding="utf-8")
            artifacts = discover_artifacts(run_dir)
            self.assertEqual([item["name"] for item in artifacts], ["summary", "manifest", "candidate"])


if __name__ == "__main__":
    unittest.main()
