"""Expose the shared offline analysis to the MCP host without importing Unreal."""

import importlib
from pathlib import Path
import sys


def run(function, **arguments):
    try:
        source = str(Path(__file__).resolve().parents[4] / "Content/Python")
        if source not in sys.path:
            sys.path.insert(0, source)
        analysis = importlib.import_module("prk_zone_analysis")
        result = getattr(analysis, function)(**arguments)
        result["success"] = bool(result["ok"])
        return result
    except Exception as error:
        return {"ok": False, "success": False, "error": str(error), "editor_contacted": False}
