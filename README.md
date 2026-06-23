# metamaterial-solver

Tiny Python worker backend for the metamaterial research app.

This pass is a contract stub only. It validates one JSON request, emits JSONL worker events, and writes deterministic placeholder artifacts. It does not run acoustic physics, topology optimization, Gmsh, STL export, or FEniCSx.

## Layout

- `src/metamaterial_solver/contract.py`: stdlib request validation.
- `src/metamaterial_solver/worker.py`: JSONL worker and stub artifact writer.
- `src/metamaterial_solver/__main__.py`: CLI entrypoint.
- `tests/test_worker_contract.py`: stdlib unittest coverage.

## Commands

From this folder, with `src` on `PYTHONPATH`:

```powershell
$env:PYTHONPATH = "src"
python -m metamaterial_solver validate path\to\request.json
python -m metamaterial_solver run path\to\request.json
```

## Frontend Contract

The PyQt6 app should start the worker with `QProcess`, read one JSON object per stdout line, and show progress/logs from those events. To cancel, create `cancel.requested` in the run folder.

The worker writes:

- `manifest.json`
- `candidate.json`
- `response.csv`
- `summary.md`

JSON is the strict backend contract. YAML remains for human-facing ICM project files.
