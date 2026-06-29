"""PyQt6 shell that launches the solver worker through QProcess."""

from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path

from .helpers import (
    artifact_from_event,
    default_request,
    discover_artifacts,
    event_label,
    make_run_id,
    parse_event_line,
    progress_percent,
    write_request,
)

try:
    from PyQt6.QtCore import QProcess, QProcessEnvironment, Qt
    from PyQt6.QtWidgets import (
        QApplication,
        QFileDialog,
        QGridLayout,
        QGroupBox,
        QHBoxLayout,
        QLabel,
        QLineEdit,
        QListWidget,
        QMainWindow,
        QMessageBox,
        QPlainTextEdit,
        QProgressBar,
        QPushButton,
        QSizePolicy,
        QSplitter,
        QVBoxLayout,
        QWidget,
    )
except ImportError as exc:  # pragma: no cover - exercised by optional import smoke test.
    raise RuntimeError("PyQt6 is required to run metamaterial_app") from exc


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Metamaterial Solver")
        self.resize(1040, 680)
        self.process: QProcess | None = None
        self.stdout_buffer = ""

        self.project_dir = QLineEdit(str(Path.cwd()))
        self.run_dir = QLineEdit(str(Path.cwd() / "runs" / make_run_id()))
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.artifacts = QListWidget()
        self.run_button = QPushButton("Run")
        self.cancel_button = QPushButton("Cancel")
        self.cancel_button.setEnabled(False)

        self._build_ui()
        self._connect()
        self.refresh_artifacts()

    def _build_ui(self) -> None:
        root = QWidget()
        outer = QVBoxLayout(root)

        selectors = QGroupBox("Run setup")
        grid = QGridLayout(selectors)
        project_browse = QPushButton("Browse...")
        run_browse = QPushButton("Browse...")
        project_browse.clicked.connect(self.choose_project_dir)
        run_browse.clicked.connect(self.choose_run_dir)
        grid.addWidget(QLabel("Project directory"), 0, 0)
        grid.addWidget(self.project_dir, 0, 1)
        grid.addWidget(project_browse, 0, 2)
        grid.addWidget(QLabel("Run directory"), 1, 0)
        grid.addWidget(self.run_dir, 1, 1)
        grid.addWidget(run_browse, 1, 2)

        actions = QHBoxLayout()
        actions.addWidget(self.run_button)
        actions.addWidget(self.cancel_button)
        actions.addStretch(1)
        actions.addWidget(self.progress)
        grid.addLayout(actions, 2, 1, 1, 2)
        outer.addWidget(selectors)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.addWidget(QLabel("Log"))
        left_layout.addWidget(self.log)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.addWidget(QLabel("Artifacts"))
        right_layout.addWidget(self.artifacts)
        right_layout.addWidget(QLabel("Visualization"))
        right_layout.addWidget(self._visualization_panel())

        splitter.addWidget(left)
        splitter.addWidget(right)
        splitter.setSizes([650, 390])
        outer.addWidget(splitter, 1)

        self.setCentralWidget(root)

    def _visualization_panel(self) -> QWidget:
        if importlib.util.find_spec("pyvistaqt") is not None:
            try:
                from pyvistaqt import QtInteractor

                panel = QtInteractor(self)
                panel.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
                return panel
            except Exception:
                pass

        label = QLabel("Visualization placeholder\nDensity and mesh previews will appear here.")
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        label.setMinimumHeight(220)
        label.setStyleSheet("border: 1px solid #b8bec8; background: #f6f7f9; color: #4a5568;")
        return label

    def _connect(self) -> None:
        self.run_button.clicked.connect(self.start_run)
        self.cancel_button.clicked.connect(self.cancel_run)
        self.run_dir.editingFinished.connect(self.refresh_artifacts)

    def choose_project_dir(self) -> None:
        directory = QFileDialog.getExistingDirectory(self, "Select project directory", self.project_dir.text())
        if directory:
            self.project_dir.setText(directory)

    def choose_run_dir(self) -> None:
        directory = QFileDialog.getExistingDirectory(self, "Select run directory", self.run_dir.text())
        if directory:
            self.run_dir.setText(directory)
            self.refresh_artifacts()

    def start_run(self) -> None:
        if self.process is not None:
            return

        run_path = Path(self.run_dir.text()).expanduser()
        request = default_request(run_path, run_path.name or make_run_id())
        request_path = write_request(run_path, request)
        cancel_path = run_path / "cancel.requested"
        if cancel_path.exists():
            cancel_path.unlink()

        self.log.clear()
        self.progress.setValue(0)
        self.artifacts.clear()
        self.stdout_buffer = ""
        self.append_log(f"wrote {request_path}")

        process = QProcess(self)
        process.setProgram(sys.executable)
        process.setArguments(["-m", "metamaterial_solver", "run", str(request_path)])
        process.setWorkingDirectory(self.project_dir.text() or str(Path.cwd()))
        env = QProcessEnvironment.systemEnvironment()
        src_root = str(Path(__file__).resolve().parents[1])
        existing_pythonpath = env.value("PYTHONPATH")
        env.insert("PYTHONPATH", os.pathsep.join(filter(None, [src_root, existing_pythonpath])))
        process.setProcessEnvironment(env)
        process.readyReadStandardOutput.connect(self.read_stdout)
        process.readyReadStandardError.connect(self.read_stderr)
        process.finished.connect(self.process_finished)
        process.errorOccurred.connect(self.process_error)

        self.process = process
        self.run_button.setEnabled(False)
        self.cancel_button.setEnabled(True)
        process.start()

    def cancel_run(self) -> None:
        run_path = Path(self.run_dir.text()).expanduser()
        run_path.mkdir(parents=True, exist_ok=True)
        (run_path / "cancel.requested").write_text("", encoding="utf-8")
        self.append_log("cancel requested")

    def read_stdout(self) -> None:
        if self.process is None:
            return
        self.stdout_buffer += bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        while "\n" in self.stdout_buffer:
            line, self.stdout_buffer = self.stdout_buffer.split("\n", 1)
            self.handle_event_line(line)

    def read_stderr(self) -> None:
        if self.process is None:
            return
        text = bytes(self.process.readAllStandardError()).decode("utf-8", errors="replace").strip()
        if text:
            self.append_log(text)

    def handle_event_line(self, line: str) -> None:
        event = parse_event_line(line)
        if event is None:
            return
        self.append_log(event_label(event))
        percent = progress_percent(event)
        if percent is not None:
            self.progress.setValue(percent)
        artifact = artifact_from_event(event)
        if artifact:
            self.add_artifact(artifact)

    def process_finished(self, exit_code: int, *_args) -> None:
        if self.stdout_buffer.strip():
            self.handle_event_line(self.stdout_buffer)
        self.stdout_buffer = ""
        self.append_log(f"process exited with code {exit_code}")
        self.process = None
        self.run_button.setEnabled(True)
        self.cancel_button.setEnabled(False)
        self.refresh_artifacts()

    def process_error(self, *_args) -> None:
        if self.process is not None:
            self.append_log(f"process error: {self.process.errorString()}")

    def append_log(self, text: str) -> None:
        self.log.appendPlainText(text)

    def add_artifact(self, artifact: dict[str, str]) -> None:
        label = f"{artifact['name']} ({artifact['kind']}) - {artifact['path']}"
        existing = [self.artifacts.item(index).text() for index in range(self.artifacts.count())]
        if label not in existing:
            self.artifacts.addItem(label)

    def refresh_artifacts(self) -> None:
        self.artifacts.clear()
        for artifact in discover_artifacts(self.run_dir.text()):
            self.add_artifact(artifact)

    def closeEvent(self, event) -> None:  # noqa: N802
        if self.process is not None:
            reply = QMessageBox.question(
                self,
                "Run in progress",
                "Cancel the current run before closing?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            )
            if reply == QMessageBox.StandardButton.Yes:
                self.cancel_run()
                self.process.terminate()
            else:
                event.ignore()
                return
        super().closeEvent(event)


def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()
