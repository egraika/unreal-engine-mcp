"""Zone kit catalog/compilation in the MCP server, without an editor round trip."""

import importlib.util
from pathlib import Path


def _planner():
    root = Path(__file__).resolve().parents[4]
    spec = importlib.util.spec_from_file_location("prk_zone_plan_server", root / "Content/Python/prk_zone_plan.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def catalog(recipe_id=None, details=False):
    try:
        planner = _planner()
        library = planner.load_library()
        recipes = library["recipes"]
        if recipe_id:
            recipes = {recipe_id: recipes[recipe_id]}
        if not details:
            recipes = {rid: {"purpose": r["purpose"], "stamp_class": r["stamp_class"], "fields": r["fields"],
                             "texture": r["source"]["texture"], "calibration_status": r["calibration"]["status"],
                             "measurement_role": r.get("measurement_role", "landform"),
                             "evidence": r["calibration"]["evidence"],
                             "requires_recipe_before": r.get("requires_recipe_before")}
                       for rid, r in recipes.items()}
        return {"success": True, "recipes": recipes, "compositions": library["compositions"],
                "scope": library["scope"], "editor_contacted": False}
    except Exception as error:
        return {"success": False, "error": str(error)}


def compile_plan(zone_key):
    try:
        return {"success": True, "plan": _planner().compiled(zone_key), "editor_contacted": False}
    except Exception as error:
        return {"success": False, "error": str(error)}
