"""Compatibility entry point for the HTML support-table generator."""

from pathlib import Path
import runpy
import sys

target = Path(__file__).resolve().parents[1] / "project_tools"
sys.path.insert(0, str(target))
runpy.run_path(str(target / Path(__file__).name), run_name="__main__")
