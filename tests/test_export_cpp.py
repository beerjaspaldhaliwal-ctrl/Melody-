"""The plugin's C++ style data must be regenerated whenever the Python library changes."""

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_cpp_style_data_is_up_to_date():
    spec = importlib.util.spec_from_file_location("export_styles_cpp", ROOT / "tools" / "export_styles_cpp.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    assert mod.OUT.read_text(encoding="utf-8") == mod.render(), "run: python tools/export_styles_cpp.py"
