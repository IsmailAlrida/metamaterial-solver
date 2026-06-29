import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))


class AppSmokeTest(unittest.TestCase):
    def test_package_import_does_not_require_pyqt(self):
        import metamaterial_app

        self.assertTrue(metamaterial_app.__version__)

    @unittest.skipIf(importlib.util.find_spec("PyQt6") is None, "PyQt6 is not installed")
    def test_main_window_imports_when_pyqt_is_available(self):
        from metamaterial_app.main_window import MainWindow

        self.assertTrue(MainWindow)


if __name__ == "__main__":
    unittest.main()
