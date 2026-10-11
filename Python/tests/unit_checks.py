import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
import math, sys
sys.path.insert(0, _CONTENT_PY)
sys.path.insert(0, _PY)
import prk_terrain_geom as g
from helpers import prk_python_guard as pg
from helpers import prk_texture_preview as tp

fails = []
def check(name, cond):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond: fails.append(name)

# --- bounds
one = [[0, 0, 0], [51200, 51200, 10000]]
check("bounds one box", g.normalise_bounds(one) == [([0.0, 0.0, 0.0], [51200.0, 51200.0, 10000.0])])
two = [one, [[-10, -10, -5], [10, 10, 5]]]
check("bounds list of boxes", len(g.normalise_bounds(two)) == 2)
check("bounds list of one box", len(g.normalise_bounds([one])) == 1)
for bad in ([], [1, 2, 3], [[0, 0], [1, 1]], [[[0, 0, 0]]], [[1, 0, 0], [0, 1, 1]], "x", [[True, 0, 0], [1, 1, 1]]):
    try:
        g.normalise_bounds(bad); check(f"bounds rejects {bad!r}", False)
    except ValueError:
        check(f"bounds rejects {bad!r}", True)

# --- footprint
fp = g.Footprint(1000, 2000, 90, 51200, 20000)
x, y = fp.to_world(100, 0)
check("yaw 90: +u -> +y", abs(x - 1000) < 1e-6 and abs(y - 2100) < 1e-6)
a, b = fp.to_local(*fp.to_world(123, -456))
check("local/world round trip", abs(a - 123) < 1e-6 and abs(b + 456) < 1e-6)
check("ray extent +u", abs(fp.ray_extent(0) - 25600) < 1e-6)
check("ray extent +v", abs(fp.ray_extent(math.pi / 2) - 10000) < 1e-6)
check("ray extent diagonal", abs(fp.ray_extent(math.pi / 4) - 10000 * math.sqrt(2)) < 1e-6)
lo, hi = fp.world_aabb()
check("aabb at yaw 90 swaps extents", abs((hi[0] - lo[0]) - 20000) < 1e-6 and abs((hi[1] - lo[1]) - 51200) < 1e-6)
fp45 = g.Footprint(0, 0, 45, 100, 100)
lo, hi = fp45.world_aabb()
check("aabb at yaw 45", abs(hi[0] - 50 * math.sqrt(2)) < 1e-6)
check("contains", fp.contains(1000, 2000 + 25000) and not fp.contains(1000 + 10500, 2000))

nu, nv, cells = g.grid_cells(g.Footprint(0, 0, 0, 51200, 51200), 1000)
check("grid 52x52", (nu, nv, len(cells)) == (52, 52, 2704))
check("grid cells inside", all(abs(c[2]) < 25600 and abs(c[3]) < 25600 for c in cells))

sq = g.Footprint(0, 0, 0, 1000, 1000)
pts = g.perimeter_points(sq, 50)
check("perimeter count 80", len(pts) == 80)
check("perimeter on edge", all(abs(abs(a) - 500) < 1e-6 or abs(abs(b) - 500) < 1e-6 for a, b in pts))
check("perimeter spacing <= step", all(math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]) <= 50 + 1e-6 for i in range(len(pts))))
check("perimeter unique", len(set((round(a, 6), round(b, 6)) for a, b in pts)) == len(pts))

plan = g.plan_budget(g.Footprint(0, 0, 0, 51200, 51200), 1000, 50, 4)
print(plan)
check("budget 512 m within cap", plan["trace_estimate"] <= 5000)
plan2 = g.plan_budget(g.Footprint(0, 0, 0, 210000, 210000), 1000, 50, 4)
print(plan2)
check("budget 2.1 km coarsened within cap", plan2["trace_estimate"] <= 5000 and plan2["grid_coarsened"] and plan2["perimeter_coarsened"])
plan3 = g.plan_budget(g.Footprint(0, 0, 0, 10000, 10000), 1000, 50, 4)
check("budget small untouched", not plan3["grid_coarsened"] and not plan3["perimeter_coarsened"])

# --- flood fill
m = [[1, 1, 0, 0, 1],
     [0, 1, 0, 1, 1],
     [0, 0, 0, 0, 0],
     [1, 0, 1, 1, 1]]
m = [[bool(v) for v in r] for r in m]
check("flood fill components", g.flood_fill_components(m) == [3, 3, 3, 1])
check("flood fill diagonal not connected", g.flood_fill_components([[True, False], [False, True]]) == [1, 1])
check("flood fill empty", g.flood_fill_components([[False] * 3] * 3) == [])
big = [[True] * 300 for _ in range(300)]
check("flood fill big no recursion", g.flood_fill_components(big) == [90000])

# --- shore crossing / slope / runs
prof = [(0, 4000), (100, 4100), (200, 4500), (300, 4700), (400, 4800)]
c = g.shore_crossing(prof, 4600)
check("shore crossing interpolated", c["index"] == 3 and abs(c["distance"] - 250) < 1e-6 and not c["centre_dry"])
check("shore crossing centre dry", g.shore_crossing([(0, 5000), (100, 5100)], 4600)["centre_dry"])
check("shore never dry", g.shore_crossing([(0, 4000), (100, 4100)], 4600)["distance"] is None)
check("shore skips None", g.shore_crossing([(0, 4000), (100, None), (200, 4800)], 4600)["index"] == 2)
s = g.max_slope_deg(prof, 150, 400)
check("max slope", abs(s - math.degrees(math.atan2(200, 100))) < 1e-6)
check("runs none", g.runs_below([5, 5, 5], 1) == [])
check("runs all", g.runs_below([0, 0], 1) == [(0, 1)])
check("runs wrap", g.runs_below([0, 5, 5, 0, 0], 1) == [(3, 0)])
check("runs two", g.runs_below([5, 0, 5, 0, 0, 5, None], 1) == [(1, 1), (3, 4)])

# --- guard
deny = [
    "e = unreal.TextureExporterTGA()",
    "task = unreal.AssetExportTask(); task.object = unreal.load_asset('/Game/T_Lake_3')  # texture",
    "unreal.ElBlueprintLibrary.force_update_all_landscape2(w)",
    "ForceUpdateAllLandscape2",
    "unreal.ElBlueprintLibrary.start_edit_mode2(world)",
    "lib.end_edit_mode2(world)",
    "StartEditMode2",
    "b.set_errant_landscape_edit_mode(False)",
    "unreal.PRKEditorAIBridge.SetErrantLandscapeEditMode",
    "bridge.set_editor_property('per_landscape_settings', s)",
    "b.set_property_by_path(br, \"PerLandscapeSettings[0].WeightmapMethod\", \"WeightBlendNoDefault\")",
    "s[0].set_editor_property(\"weightmap_method\", unreal.ElLayerWeightmapMethod.WEIGHT_BLEND_NO_DEFAULT)",
    "prk_mcp_ops.set_object_property('actor:ElBridge_1', 'PerLandscapeSettings[0].WeightmapMethod', 'X')",
    "t = unreal.AssetExportTask(); t.object = unreal.load_asset('/Game/PRK/UI/T_Icon')",
    "t = unreal.AssetExportTask(); t.object = rt  # RenderTarget export",
    "task = unreal.AssetExportTask(); task.object = unreal.load_asset('/Game/Calysto/World/Texture/Heightmap/T_Lake_3')",
]
allow = [
    "print(unreal.ElBlueprintLibrary.is_edit_mode_active2(w))",
    "s = br.get_editor_property('per_landscape_settings'); print(s[0].get_editor_property('weightmap_method'))",
    "import prk_lake_ops; prk_lake_ops.stamp_configure(actor='Lake_5', dry_run=True)",
    "t = unreal.AssetExportTask(); t.object = unreal.load_asset('/Game/Meshes/SM_Rock')",
    "unreal.load_asset('/Game/Calysto/World/Texture/Heightmap/T_Lake_3')",
    "lib.request_update_landscape(comp)",
    "import prk_lake_ops as L; L.edit_mode_set(active=False, dry_run=True)",
    "print(b.get_errant_landscape_edit_mode_state())",
]
for code in deny:
    check(f"guard denies: {code[:60]}", bool(pg.check_python_source(code)))
for code in allow:
    check(f"guard allows: {code[:60]}", not pg.check_python_source(code))
r = pg.refusal(pg.check_python_source(deny[0]), "x")
check("refusal names alternative", r and any("texture_preview" in u for u in r["use_instead"]))

# --- generic-tool bridge guard
check("bridge guard denies set_object_property", bool(pg.check_bridge_write("actor:ElBridge_UAID_BC542F6150B149DB02_1696348628", "PerLandscapeSettings[0].WeightmapMethod")))
check("bridge guard denies invoke_function args (renamed bridge)", bool(pg.check_bridge_write("actor:Errant Landscape", "SetEditorProperty", ["weightmap_method", "WeightBlendNoDefault"])))
check("bridge guard denies invoke SetErrantLandscapeEditMode", bool(pg.check_bridge_write("subsystem:PRKEditorAIBridge@editor", "SetErrantLandscapeEditMode", ["false"])))
check("bridge guard denies invoke EndEditMode2", bool(pg.check_bridge_write("class:/Script/ErrantLandscape.ElBlueprintLibrary", "EndEditMode2")))
check("bridge guard allows edit-mode state read", not pg.check_bridge_write("subsystem:PRKEditorAIBridge@editor", "GetErrantLandscapeEditModeState"))
check("bridge guard allows other path", not pg.check_bridge_write("actor:ElBridge_1", "BrushStreamingBounds"))
# --- texture preview
for name, want in (("T_Lake_3", 155), ("T_Lake_5", 189)):
    res = tp.texture_preview(f"/Game/Calysto/World/Texture/Heightmap/{name}")
    print(name, res.get("centre_mean"), res.get("size"), res.get("bbox_gt20"), res.get("png"))
    check(f"texture_preview {name} centre ~{want}", res["success"] and abs(res["centre_mean"] - want) <= 3)
check("texture_preview refuses project out_dir",
      not tp.texture_preview("/Game/Calysto/World/Texture/Heightmap/T_Lake_3", _os.path.join(_ROOT, "Saved", "x"))["success"])
check("texture_preview missing asset", not tp.texture_preview("/Game/Nope/T_Missing")["success"])
check("package_name forms", tp.package_name("Texture2D'/Game/A/T_X.T_X'") == "/Game/A/T_X" and tp.package_name("/Game/A/T_X.T_X") == "/Game/A/T_X")

# --- hillshade / PNG helpers (2026-10-08)
check("grid_axis", g.grid_axis(0, 100, 25) == [0, 25, 50, 75, 100] and g.grid_axis(0, 10, 25) == [0])
check("percentile", g.percentile([4, 1, 3, 2], 50) == 2.5 and g.percentile([None, 5], 90) == 5 and g.percentile([], 50) is None)
flat = [[10.0] * 5 for _ in range(5)]
check("flat slope 0", all(v == 0.0 for row in g.grid_slopes_deg(flat, 10)[1:-1] for v in row[1:-1]))
ramp = [[float(c) * 10.0 for c in range(5)] for _ in range(5)]  # +10 m per 10 m east = 45 deg
check("ramp slope 45", abs(g.grid_slopes_deg(ramp, 10)[2][2] - 45.0) < 1e-9)
hs = g.hillshade(ramp, 10)  # facing west (downhill to -x); NW light lights it
hs_e = g.hillshade([[-v for v in row] for row in ramp], 10)
check("hillshade west-facing brighter under NW light", hs[2][2] > hs_e[2][2])
png = g.png_bytes([[(255, 0, 0), (0, 255, 0)], [(0, 0, 255), (9, 9, 9)]])
check("png signature", png[:8] == bytes([137, 80, 78, 71, 13, 10, 26, 10]) and b"IHDR" in png and png[-8:-4] == b"IEND")
a, b2, st = g.render_height_grid([[1.0, None], [2.0, 3.0]], 10, pixel_scale=3)
check("render stats", st["valid"] == 3 and st["z_min"] == 1.0 and st["z_max"] == 3.0 and st["cells"] == 4)
import struct
check("render pixel_scale", struct.unpack(">II", a[16:24]) == (6, 6))
print("FAILS:", fails)
