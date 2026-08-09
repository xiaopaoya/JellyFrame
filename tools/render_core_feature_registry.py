"""Compatibility module and CLI entry point for the moved feature registry."""

from __future__ import annotations

import importlib.util
import runpy
import sys
from pathlib import Path


_TARGET_DIR = Path(__file__).resolve().parents[1] / "project_tools"
_TARGET = _TARGET_DIR / "render_core_feature_registry.py"
_SPEC = importlib.util.spec_from_file_location("jellyframe_project_feature_registry", _TARGET)
if _SPEC is None or _SPEC.loader is None:
    raise ImportError(f"cannot load project feature registry: {_TARGET}")
_MODULE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_MODULE)
for _name in dir(_MODULE):
    if not _name.startswith("_"):
        globals()[_name] = getattr(_MODULE, _name)

if __name__ == "__main__":
    sys.path.insert(0, str(_TARGET_DIR))
    runpy.run_path(str(_TARGET), run_name="__main__")
