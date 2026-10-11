import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
import copy, json, math, os, sys, tempfile, types
from unittest import mock
sys.modules["unreal"] = mock.MagicMock()
sys.path.insert(0, _CONTENT_PY)
import prk_terrain_geom as g
import prk_lake_ops as L
import prk_zone_terrain_place as Z
fails = []
def check(n, c):
    print(("PASS " if c else "FAIL ") + n)
    if not c: fails.append(n)

# --- 1. render probe (pure) ------------------------------------------------------------------------------------
fp = g.Footprint(0, 0, 0, 1000, 400)
pts = g.render_probe_points([fp])
check("probe 9 points per footprint", len(pts) == 9 and (0.0, 0.0) in pts and (250.0, 100.0) in pts
      and (-250.0, 0.0) in pts and (0.0, -100.0) in pts)
check("probe dedups a repeated footprint", len(g.render_probe_points([fp, fp])) == 9)
check("probe moved footprint adds points", len(g.render_probe_points([fp, g.Footprint(5000, 0, 0, 1000, 400)])) == 18)
m = g.probe_moved([100.0, 100.0, None], [100.5, 103.0, 50.0], 2.0)
check("probe_moved rendered", m["rendered"] and m["moved_points"] == 1 and m["missing"] == 1 and m["max_abs_dz_cm"] == 3.0)
check("probe_moved stale", not g.probe_moved([1.0, 2.0], [1.5, 2.5], 2.0)["rendered"])
check("lod toggle up / down at 8", g.lod_toggle_value(6) == 7 and g.lod_toggle_value(8) == 7 and g.lod_toggle_value(0) == 1)

# --- 4. update-region prediction, calibrated on the 2026-10-08 placements ---------------------------------------
# ErrantWorld proxies: 2032 m squares, proxy (i, j) spans x [(i-8), (i-7)] x 203200 cm (proxy_base_*.json grids).
P = 203200.0
PROXIES = [(f"{i}_{j}", (((i - 8) * P, (j - 8) * P), ((i - 7) * P, (j - 7) * P))) for i in range(16) for j in range(16)]
def predict(x, y, final_scale, default_scale=None, non_partial=(), expand=0.0):
    boxes = [g.brush_box(x, y, *final_scale)]
    if default_scale:
        boxes.append(g.brush_box(x, y, *default_scale))
    return set(g.predict_update_proxies(boxes, PROXIES, list(non_partial), expand)["proxies"])
def block(i0, i1, j0, j1):
    return {f"{i}_{j}" for i in range(i0, i1 + 1) for j in range(j0, j1 + 1)}
# HW-0a spawn (IcelandicTerrain, default scale 4065): footprint 10_8 10_9 11_8 11_9; observed NEW dirt 9_10 10_10 11_10
# (9_7..9_9 / 10_7 / 11_7 were already dirty from HW-U1).
hw0a = predict(560000, 210000, (3000, 3000), (4065, 4065))
check("HW-0a predicted = 9..11 x 8..10", hw0a == block(9, 11, 8, 10))
check("HW-0a explains 9_10 10_10 11_10", {"9_10", "10_10", "11_10"} <= hw0a)
check("HW-0a footprint alone does not", predict(560000, 210000, (3000, 3000)) == block(10, 11, 8, 9))
# HW-0b (IcelandicTerrain, Revision 2 morning): observed adjacent row 9_6 10_6 11_6
check("HW-0b explains 9_6 10_6 11_6", {"9_6", "10_6", "11_6"} <= predict(540000, -10000, (3000, 3000), (4065, 4065)))
# SW-R1b (TilingRockyTerrain, default 4065): observed NEW 9_5 10_5 11_5 11_6 besides its own 9_6 10_6
r1b = predict(450000, -285000, (1000, 1000), (4065, 4065))
check("SW-R1b explains 9_5 10_5 11_5 11_6", {"9_5", "10_5", "11_5", "11_6", "9_6", "10_6"} <= r1b and r1b == block(9, 11, 5, 7))
# SW-R1a (TilingRockyTerrain): observed NEW 8_5 8_6 8_7 9_5 10_5
check("SW-R1a explains 8_5 8_6 8_7 9_5 10_5", {"8_5", "8_6", "8_7", "9_5", "10_5"} <= predict(395000, -258000, (1100, 1100), (4065, 4065)))
# Revision 2 configures (no class-default box): each dirtied only its own proxies
check("HW-0a configure stays on its own 4", predict(560000, 210000, (3000, 3000)) == {"10_8", "10_9", "11_8", "11_9"})
g11 = set(g.predict_update_proxies([g.brush_box(550800, 141400, 457, 130), g.brush_box(551600, 142900, 1090, 380)],
                                   PROXIES)["proxies"])
check("HW-G1.1 configure move stays on 10_8", g11 == {"10_8"})
# non-partial (blur) brush: within one component of the request it is merged, and the region grows by E
E = 102000.0
r = g.predict_update_proxies([g.brush_box(560000, 210000, 3000, 3000)], PROXIES,
                             [("Blur", g.brush_box(560000, 450000, 100, 100))], E)
check("non-partial within E merges", r["non_partial_added"] == ["Blur"] and r["region"][1][1] > 462000 - 1
      and set(r["proxies"]) == block(9, 11, 7, 10))
r = g.predict_update_proxies([g.brush_box(560000, 210000, 3000, 3000)], PROXIES,
                             [("Far", g.brush_box(560000, 900000, 100, 100))], E)
check("non-partial beyond E ignored", r["non_partial_added"] == [] and set(r["proxies"]) == block(10, 11, 8, 9))
r = g.predict_update_proxies([g.brush_box(0, 0, 100, 100)], PROXIES,
                             [("B1", g.brush_box(100000, 0, 100, 100)), ("B2", g.brush_box(200000, 0, 100, 100))], E)
check("non-partial chain", r["non_partial_added"] == ["B1", "B2"])
check("touching proxies not counted", set(g.predict_update_proxies([((0, 0), (P, P))], PROXIES)["proxies"]) == {"8_8"})

# --- 5. grades, carve report ------------------------------------------------------------------------------------
s = [0, 1000, 2000, 3000, 4000]
st = g.grade_stats(s, [0, 100, 200, 200, 200], 2000)
check("grade 20 m windows", st["windows"] == 3 and st["max_pct"] == 10.0)
check("grade short profile", g.grade_stats([0, 500], [0, 50], 2000)["max_pct"] == 10.0)
check("signed stats", g.signed_stats([-3, 1, 2])["abs_max"] == 3 and g.signed_stats([])["count"] == 0)
check("carve table", L.PATH_DESC_CARVES["EPD_DirtRoad"] is False and L.PATH_DESC_CARVES["EPD_MountainRoad"] is True
      and set(L.PATH_DESC_CARVES) == set(L.PATH_DESC_CLASSES))
class Ground:
    def __init__(self, w): self.count = 0
    def z(self, x, y): self.count += 1; return 0.0
with mock.patch.object(L, "_LandscapeTracer", Ground):
    rep = L._carve_report(None, "EPD_DirtRoad", [[0, 0, 300], [10000, 0, 300]], drape=False)
    check("dirt road off ground warns", rep["carves"] is False and rep["warnings"] and "does not carve" in rep["warnings"][0])
    check("dirt road on ground quiet", not L._carve_report(None, "EPD_DirtRoad", [[0, 0, 20], [10000, 0, 20]], False)["warnings"])
    check("draped dirt road quiet", not L._carve_report(None, "EPD_DirtRoad", [[0, 0, 300], [10000, 0, 300]], True)["warnings"])
    check("mountain road no float warning", not L._carve_report(None, "EPD_MountainRoad", [[0, 0, 300], [10000, 0, 300]], False)["warnings"])

# --- 6. road verdict --------------------------------------------------------------------------------------------
rows = [{"xy": [i, 0], "spline_z": 1000.0, "ground_z": 1000.0 + (200 if i == 3 else 10), "layers": {"Path_Dirt": 0.9}}
        for i in range(20)]
v = L._road_verdict(rows, ["Path_Dirt"], True, 150.0, False, 0.9)
check("carving road height outside tol", not v["verified"] and v["height"]["outside_tol"] == 1)
check("stale skips the height check", L._road_verdict(rows, ["Path_Dirt"], True, 150.0, True, 0.9)["verified"])
check("paint road ignores height", L._road_verdict(rows, ["Path_Dirt"], False, 150.0, False, 0.9)["verified"])
rows2 = [dict(r, ground_z=r["spline_z"], layers={} if i == 0 else r["layers"]) for i, r in enumerate(rows)]
check("coverage 0.95 passes 0.9", L._road_verdict(rows2, ["Path_Dirt"], True, 150, False, 0.9)["verified"])
rows3 = [dict(r, layers={}) if i < 19 else r for i, r in enumerate(rows2)]
v = L._road_verdict(rows3, ["Path_Dirt"], True, 150, False, 0.9)
check("R8 1 % paint fails", not v["verified"] and v["paint"]["coverage"] == 0.05)
check("path kinds", L._path_kind("EPD_Stream") == "water" and L._path_kind("EPD_StoneRoad") == "carving_road"
      and L._path_kind("EPD_DirtRoad") == "paint_road" and L._path_kind(None) == "other")

# path_regenerate retries once and reports both attempts
class FakeWorld:
    def __init__(self, path): self.path = path
    def get_path_name(self): return self.path
W = FakeWorld(L.DEFAULT_WORLD)
class FP:
    def get_actor_label(self): return "R8_QuarryRoad"
    def get_name(self): return "EpPath_UAID_R8"
lib = mock.MagicMock()
L.unreal.EpPathsLibrary = lib
base_snap = {"adjustment_state": {"update_in_progress": False}, "main_z": [0.0] * 11, "post_spawn_z": [0.0] * 11,
             "surface": {"surface": None, "meshes": {}}}
bad = dict(base_snap, centreline=rows3)
good = dict(base_snap, centreline=rows2)
WATER_OK = {"segments": [], "segments_total": 0}
def regen(snaps, desc="EPD_MountainRoad", own=(["Path_Dirt"], True), template_layers=(), water=None, **kw):
    own_seq = list(own) if isinstance(own, list) else None
    with mock.patch.object(L, "_write_guard", return_value=(W, None)), \
         mock.patch.object(L._el, "_bridge", return_value=mock.MagicMock()), \
         mock.patch.object(L, "_all", return_value=[FP()]), \
         mock.patch.object(L, "_main_spline", return_value=object()), \
         mock.patch.object(L, "_compiled_all_mask", return_value=32767), \
         mock.patch.object(L, "_path_snapshot", side_effect=list(snaps)), \
         mock.patch.object(L, "_desc_name", return_value=desc), \
         mock.patch.object(L, "_own_adjustments", **({"side_effect": own_seq} if own_seq else {"return_value": own})), \
         mock.patch.object(L, "_desc_template_layers", return_value=list(template_layers)), \
         mock.patch.dict(L._TEMPLATE_LAYER_CACHE, {}, clear=True), \
         mock.patch.object(L, "_water_surface", side_effect=list(water) if water else lambda *a, **k: dict(WATER_OK)), \
         mock.patch.object(L, "_edit_mode_active", return_value=False):
        return L.path_regenerate(path="R8_QuarryRoad", dry_run=False, **kw)
lib.reset_mock(); lib.update_path_based_on_mask.return_value = object()
r = regen([base_snap, bad, good])
check("regen retries once and verifies", r["ok"] and r["retried"] and len(r["attempts"]) == 2
      and lib.update_path_based_on_mask.call_count == 2 and r["verdict_kind"] == "carving_road")
lib.reset_mock()
r = regen([base_snap, bad, bad])
check("regen reports both failed attempts", not r["ok"] and len(r["attempts"]) == 2 and "Not verified after 2" in r["error"])
lib.reset_mock()
r = regen([base_snap, bad], retry=False)
check("regen retry=False runs once", len(r["attempts"]) == 1 and lib.update_path_based_on_mask.call_count == 1)
lib.reset_mock()
r = regen([base_snap, good])
check("regen verified first time", r["ok"] and not r["retried"] and r["layer_verdict"]["coverage"] == 0.95)
check("road retry has a reason", regen([base_snap, bad, good])["retry_reason"])
# water: retry only for missing output, never for a geometric deviation
surf = {"surface": "SM_Water", "meshes": {"SM_Water": {"count": 9, "top_min": 0.0, "top_max": 0.0}}}
w_dev = dict(base_snap, post_spawn_z=[0.0] * 5 + [21.0] + [0.0] * 5, surface=surf, centreline=rows2)
w_ok = dict(base_snap, surface=surf, centreline=rows2)
w_none = dict(base_snap, centreline=rows2)            # no spawned surface meshes
def regen_water(snaps, **kw):
    return regen(snaps, desc="EPD_Stream", **kw)
lib.reset_mock()
r = regen_water([base_snap, w_dev, w_ok])
check("water 21 cm deviation: no retry, not verified", not r["ok"] and not r["retried"] and r["retry_reason"] is None
      and lib.update_path_based_on_mask.call_count == 1 and r["verdict_kind"] == "water")
lib.reset_mock()
r = regen_water([base_snap, w_none, w_ok])
check("water with no surface meshes is retried", r["ok"] and r["retried"] and "no spawned surface meshes" in r["retry_reason"]
      and lib.update_path_based_on_mask.call_count == 2)
check("water paint shortfall is a retry reason", L._retry_reason("water", {"reasons": [], "paint": {"verified": False,
      "reasons": ["coverage 0.01"]}}) == "coverage 0.01")

# --- 7. cusps -----------------------------------------------------------------------------------------------------
spur = [[146000, -200000, 0], [151000, -220000, 0], [151300, -234000, 0], [151100, -221000, 0], [146500, -205000, 0]]
cs = g.find_cusps(spur)
check("R8 W1 spur shape flagged", len(cs) == 1 and cs[0]["turn_deg"] > 150 and abs(cs[0]["xy"][1] + 234000) < 300)
def hairpin(r_cm):
    return ([[-r_cm, -5000, 0], [-r_cm, 0, 0]] + [[-r_cm * math.cos(t), r_cm * math.sin(t), 0]
             for t in [k * math.pi / 36 for k in range(1, 36)]] + [[r_cm, 0, 0], [r_cm, -5000, 0]])
check("12 m hairpin passes", g.find_cusps(hairpin(1200)) == [])
check("6 m hairpin flagged", len(g.find_cusps(hairpin(600))) == 1)
check("straight + gentle curve passes", g.find_cusps([[0, 0, 0], [10000, 0, 0], [20000, 3000, 0], [30000, 9000, 0]]) == [])
check("zig-zag S-bends net out", g.find_cusps([[0, 0, 0], [2000, 1000, 0], [4000, 0, 0], [6000, 1000, 0], [8000, 0, 0]]) == [])

# --- 9a. grade-capped profile -------------------------------------------------------------------------------------
s = [k * 1000.0 for k in range(101)]                       # 1 km every 10 m
ground = [0.0 if k < 40 else (3000.0 if k > 60 else 150.0 * (k - 40)) for k in range(101)]  # 30 m over 200 m: 15 %
fit = g.fit_road_profile(s, ground, max_grade=0.12, smoothing_cm=5000, pin_start=0.0, pin_end=3000.0)
check("fit caps grade", fit["max_segment_grade_pct"] <= 12.0001 and fit["grade_20m"]["max_pct"] <= 12.0)
check("fit keeps pins", fit["z"][0] == 0.0 and fit["z"][-1] == 3000.0 and fit["ok"])
check("fit reports cut/fill", fit["cut_fill_cm"]["max_fill"] > 0 and fit["cut_fill_cm"]["max_cut"] < 0)
fit = g.fit_road_profile(s, ground, max_grade=0.12, smoothing_cm=0, level_ranges=[[45000, 55000, 1500.0]],
                         pin_start=0.0, pin_end=3000.0)
check("level range held", all(fit["z"][k] == 1500.0 for k in range(45, 56)) and fit["max_segment_grade_pct"] <= 12.0001)
fit = g.fit_road_profile(s, ground, max_grade=0.02, smoothing_cm=0, pin_start=0.0, pin_end=3000.0)
check("infeasible pins reported", not fit["ok"] and fit["infeasible_pins"][0]["grade_needed_pct"] == 3.0)
fit = g.fit_road_profile(s, [100.0 * math.sin(k / 5.0) for k in range(101)], max_grade=0.5, smoothing_cm=0,
                         monotonic="up")
check("monotonic up never descends", all(b >= a - 1e-6 for a, b in zip(fit["z"], fit["z"][1:])))
check("monotonic conflict reported", "monotonic_conflict" in g.fit_road_profile(
    s, ground, max_grade=0.5, smoothing_cm=0, monotonic="down", pin_start=0.0, pin_end=3000.0))
try:
    g.fit_road_profile(s, ground[:-1]); check("fit rejects length mismatch", False)
except ValueError:
    check("fit rejects length mismatch", True)

# --- 9b. ghyll floor tracking -------------------------------------------------------------------------------------
def floor_y(x): return 600.0 * math.sin(x / 8000.0)      # the floor meanders +-6 m about the guide (y = 0)
def valley(x, y): return 7500.0 - x * 0.01 + 0.2 * abs(y - floor_y(x)) + (40.0 if 39000 < x < 41000 else 0.0)
res = g.ghyll_floor_trace([[0, 0, 0], [100000, 0, 0]], valley, station_cm=2000, half_width_cm=2000,
                          cross_step_cm=100, max_drift_cm=1250, max_traces=6000)
zs = [p[2] for p in res["points"]]
check("ghyll never rises", all(b <= a + 1e-6 for a, b in zip(zs, zs[1:])))
check("ghyll never above ground", all(p[2] <= valley(p[0], p[1]) + 0.5 for p in res["points"]))
check("ghyll tracks the floor", all(abs(p[1] - floor_y(p[0])) <= 100.0 for p in res["points"]))
check("ghyll clamps the bump", res["rises_clamped"] >= 1 and res["missing_stations"] == 0)
try:
    g.ghyll_floor_trace([[0, 0, 0], [100000, 0, 0]], valley, station_cm=2000, max_traces=100)
    check("ghyll refuses over max_traces", False)
except ValueError:
    check("ghyll refuses over max_traces", True)

# --- 10. height-shape rule + text parsing ---------------------------------------------------------------------------
xs = [-1.0 + k / 20.0 for k in range(41)]
river_keys = [(-1, 450), (-0.18, 0), (0.18, 0), (1, 450)]
stream_keys = [(-1, 120), (-0.45, 0), (0.45, 0), (1, 120)]
def ent(name, off, curve, keys):
    return {"name": name, "offset_z": off, "curve": curve, "values": [(x, g.curve_value_linear(keys, x)) for x in xs]}
R = "/Game/PRK/World/ErrantWorld/Paths/River/HeightShapeCurve_River.HeightShapeCurve_River"
S = "/Game/PRK/World/ErrantWorld/Paths/River/HeightShapeCurve_Stream.HeightShapeCurve_Stream"
check("river -525 + river curve ok", g.height_shape_check([ent("river", -525, R, river_keys)])["ok"])
hs = g.height_shape_check([ent("river", -525, R, river_keys), ent("stream", -140, R, river_keys)])
check("stream on the river curve: levee + shared", not hs["ok"]
      and {f["flag"] for f in hs["flags"]} == {"levee", "shared_curve"}
      and next(f for f in hs["flags"] if f["flag"] == "levee")["peak_cm"] == 310.0)
hs = g.height_shape_check([ent("river", -525, R, river_keys), ent("stream", -140, S, stream_keys)])
check("fixed stream ok", hs["ok"] and next(e for e in hs["entries"] if e["name"] == "stream")["peak_cm"] == -20.0)
check("vector text", g.parse_ue_vector("(X=0.000000,Y=1.500000,Z=-140.000000)") == (0.0, 1.5, -140.0)
      and g.parse_ue_vector("None") is None)
ov = ("(bAdjustToPathSplines=True,HeightAdjustmentModifiers=(bModifyExpand=True,ExpandModifier=(Operator=Set,"
      "Modifier=400.000000),bModifyOffset=True,OffsetModifier=(Operator=Set,Modifier=(X=0.000000,Y=0.000000,"
      "Z=-140.000000))),WeightmapAdjustmentModifiers=((WeightmapName=\"Ground_Sand\")))")
hm = g.height_modifiers(ov)
check("height modifiers parsed", hm["offset"] == ("Set", (0.0, 0.0, -140.0)) and hm["expand"] == ("Set", 400.0))
check("default modifiers omitted", g.height_modifiers("(HeightAdjustmentModifiers=(bModifyOffset=True))")["offset"] == ("Add", (0.0, 0.0, 0.0))
      and g.height_modifiers("()") == {"offset": None, "expand": None})
check("apply modifier ops", g.apply_modifier("Set", -525, -140) == -140 and g.apply_modifier("Add", -525, 10) == -515
      and g.apply_modifier("EEpModifierOp::Multiply", -525, 0.5) == -262.5)

# --- 8. p4 fstat parsing + path_change_desc ------------------------------------------------------------------------
fst = ("... depotFile //PRK/Content/__ExternalActors__/A/B/X1.uasset\n... clientFile D:\\P\\Content\\X1.uasset\n"
       "... headAction add\n... action add\n... change default\n\n"
       "... depotFile //PRK/Content/__ExternalActors__/A/B/X2.uasset\n... clientFile D:\\P\\Content\\X2.uasset\n"
       "... headAction add\n... headRev 1\n")
pf = L._parse_fstat(fst)
check("fstat parse", pf[L._norm_file("D:\\P\\Content\\X1.uasset")]["action"] == "add"
      and "action" not in pf[L._norm_file("D:/P/Content/X2.uasset")])
class Old:
    def get_actor_label(self): return "R8_QuarryRoad"
    def get_name(self): return "EpPath_UAID_OLD"
    def get_editor_property(self, k): return True if k == "allow_automatic_regeneration" else [L.SCRIPTED_PATH_TAG]
OLD = Old()
OLD_PTS = [[146460.0, -107022.0, 13882.0], [140000.0, -130000.0, 14500.0], [100000.0, -200000.0, 13000.0]]
calls = {"create": [], "delete": [], "p4": []}
def fake_create(**kw):
    calls["create"].append(kw)
    if kw["dry_run"]:
        return {"ok": False, "failed_guards": ["label_unused"], "carves": True, "cusps": []}
    return {"ok": True, "verified": True, "actor_name": "EpPath_UAID_NEW"}
def fake_p4(args, timeout=60.0):
    calls["p4"].append(list(args))
    if args[0] == "fstat":
        return {"ok": True, "code": 0, "stdout": fst, "stderr": ""}
    return {"ok": True, "code": 0, "stdout": "reverted", "stderr": ""}
conns = [{"other_actor": "EpPath_UAID_W3", "other_label": "EpPath4012896657", "ThisSplineEnd": "(default)",
          "OtherSplineEnd": "End", "OtherSplinePtr": "x"}]
snapdir = tempfile.mkdtemp(prefix="prk_chg_")
def change(**kw):
    calls["create"].clear(); calls["delete"].clear(); calls["p4"].clear()
    br = mock.MagicMock()
    with mock.patch.object(L, "_world_guard", return_value=(W, None)), \
         mock.patch.object(L, "_write_guard", return_value=(W, None)), \
         mock.patch.object(L, "_resolve_path", return_value=(OLD, None)), \
         mock.patch.object(L, "_resolve_desc_class", return_value=({"name": "EPD_MountainRoad", "package": "/Game/P/EPD_MountainRoad"}, None)), \
         mock.patch.object(L, "_desc_name", return_value="EPD_DirtRoad"), \
         mock.patch.object(L, "_main_spline", return_value=object()), \
         mock.patch.object(L._el, "_spline_points", return_value=copy.deepcopy(OLD_PTS)), \
         mock.patch.object(L, "_tangents", return_value=[{"arrive": [1, 2, 3], "leave": [1, 2, 3]}] * 3), \
         mock.patch.object(L._el, "_bridge", return_value=br), \
         mock.patch.object(L._el, "_parse_spline_connections", return_value=copy.deepcopy(conns)), \
         mock.patch.object(L, "_projection_on", return_value=False), \
         mock.patch.object(L, "_path_package_files", return_value={"packages": ["/Game/X1", "/Game/X2"],
                           "files": ["D:\\P\\Content\\X1.uasset", "D:/P/Content/X2.uasset"], "never_saved_packages": [],
                           "unloaded_sub_actors": 0}), \
         mock.patch.object(L, "_p4", side_effect=fake_p4), \
         mock.patch.object(L, "path_create", side_effect=fake_create), \
         mock.patch.object(L, "path_delete", side_effect=lambda **k: calls["delete"].append(k) or {"ok": True}), \
         mock.patch.object(L, "_snapshot_dir", return_value=snapdir):
        args = dict(path="R8_QuarryRoad", desc_class="EPD_MountainRoad")
        args.update(kw)
        return L.path_change_desc(**args)
r = change()
ca = calls["create"][0]
check("change dry run plans the recreate", r["ok"] and r["dry_run"] and not calls["delete"]
      and ca["dry_run"] and ca["label"] == "R8_QuarryRoad" and ca["drape"] is False and ca["points"] == OLD_PTS
      and ca["connect_start"] == {"path": "EpPath_UAID_W3", "end": "end", "snap_max_cm": 100.0}
      and ca["connect_end"] is None and ca["projection_off"] is True and ca["tangent_mode"] == "auto")
check("change dry run lists the adds it would revert", r["p4_would_revert"] == ["D:\\P\\Content\\X1.uasset"]
      and all(c[0] == "fstat" for c in calls["p4"]) and not os.listdir(snapdir))
r = change(dry_run=False)
snap = os.path.join(snapdir, "R8_QuarryRoad_before_EPD_MountainRoad.json")
check("change real: snapshot, delete, create", r["ok"] and os.path.isfile(snap) and len(calls["delete"]) == 1
      and calls["create"][-1]["dry_run"] is False and json.load(open(snap))["desc_old"] == "EPD_DirtRoad")
check("change real: reverts only the add", r["p4"]["reverted"] == ["D:\\P\\Content\\X1.uasset"]
      and calls["p4"][-1] == ["revert", "D:\\P\\Content\\X1.uasset"]
      and r["p4"]["left_alone"] == {"D:/P/Content/X2.uasset": None})
r = change(dry_run=False, p4_revert_adds=False)
check("change real without p4", "p4" not in r and all(c[0] == "fstat" for c in calls["p4"]))
plan_refusal = {"ok": False, "refused": True, "error": "carves, and the points contain 1 cusp"}
with mock.patch.object(L, "_p4", side_effect=fake_p4):
    orig_create = fake_create
    def refusing_create(**kw):
        calls["create"].append(kw)
        return dict(plan_refusal)
    fake_create = refusing_create
    r = change(dry_run=False)
    fake_create = orig_create
check("change refuses before deleting when the recreate would refuse", r.get("refused") and not calls["delete"]
      and "cusp" in r["error"])

# --- 1 + 3. stamp_render_check phases and the step() runner -------------------------------------------------------
class Stamp:
    def get_actor_label(self): return "PRK_SW-Q1"
state = {"lod": 6, "z": [100.0] * 9, "writes": []}
class T:
    def __init__(self, w): self.count = 0
    def z(self, x, y): return state["z"][0]
def fake_configure(**kw):
    state["writes"].append(kw["lod_bias"]); state["lod"] = kw["lod_bias"]
    return {"ok": True, "verified": True, "diff": [{"field": "heightmap.lod_bias"}], "update_requested": True}
probe0 = {"actor": "PRK_SW-Q1", "phase": "check", "points": [[0.0, 0.0]] * 9, "z_before": [100.0] * 9,
          "expect_change": True, "lod_bias_original": 6}
def rcheck(probe, **kw):
    with mock.patch.object(L, "_world_guard", return_value=(W, None)), \
         mock.patch.object(L, "_write_guard", return_value=(W, None)), \
         mock.patch.object(L, "_resolve_stamp", return_value=(Stamp(), object(), None)), \
         mock.patch.object(L, "_edit_mode_active", return_value=kw.pop("edit", False)), \
         mock.patch.object(L, "_lod_bias", side_effect=lambda c: state["lod"]), \
         mock.patch.object(L, "_LandscapeTracer", T), \
         mock.patch.object(L, "stamp_configure", side_effect=fake_configure):
        return L.stamp_render_check(actor="PRK_SW-Q1", probe=probe, **kw)
r = rcheck(probe0)
check("render check dry stale reports the toggle", r["verdict"] == "stale" and r["would_write_lod_bias"] == 7
      and not state["writes"] and r["probe"]["phase"] == "check")
r = rcheck(probe0, dry_run=False)
check("check stale -> toggled (write 7)", r["verdict"] == "stale_toggled" and state["writes"] == [7]
      and r["probe"]["phase"] == "toggled")
r = rcheck(r["probe"], dry_run=False)
check("toggled -> restored (write 6)", r["verdict"] == "restored" and state["writes"] == [7, 6]
      and r["probe"]["phase"] == "restored")
state["z"] = [97.0] * 9
r = rcheck(r["probe"], dry_run=False)
check("restored -> rendered", r["verdict"] == "rendered" and r["probe"]["phase"] == "done" and r["measure"]["moved_points"] == 9)
state.update(z=[100.0] * 9, lod=6, writes=[])
p2 = rcheck(rcheck(probe0, dry_run=False)["probe"], dry_run=False)["probe"]
r = rcheck(p2, dry_run=False)
check("still stale after toggle", r["verdict"] == "still_stale" and "priority-only" in r["note"])
state.update(z=[97.0] * 9, lod=6, writes=[])
r = rcheck(probe0, dry_run=False)
check("check rendered -> done, no write", r["verdict"] == "rendered" and not state["writes"])
state.update(z=[100.0] * 9, lod=6, writes=[])
check("not expected -> done", rcheck(dict(probe0, expect_change=False), dry_run=False)["verdict"] == "not_expected")
check("edit mode -> stale_heights, no write", rcheck(probe0, dry_run=False, edit=True)["verdict"] == "stale_heights"
      and not state["writes"])
state["lod"] = 5
check("unexpected LodBias refused", rcheck(probe0, dry_run=False).get("refused") and not state["writes"])
state["lod"] = 6
check("probe for another actor refused", rcheck(dict(probe0, actor="PRK_X")).get("refused"))
check("bad probe refused", rcheck({"phase": "check"}).get("refused"))

# stamp_configure: predicted-proxy budget, render probe before the write, texture wait
snap_q1 = {"location": [162000.0, -240000.0, 27540.0], "scale": [240.0, 240.0, 1.3], "yaw": 0.0, "priority": 6,
           "heightmap": {"invert": False, "multiplier": 1.0, "falloff": True, "falloff_start_xy": [0.5, 0.5],
                         "falloff_end_xy": [1.0, 1.0], "falloff_noise": 0.0, "offset_method": "BRUSH_Z_LOCATION",
                         "z_offset": 0.0, "levels": {"input": [0, 1], "output": [0, 1]}, "clamp_output": False,
                         "clamp": [-1, 1], "blend_method": "ADD_AND_SUBTRACT", "lod_bias": 6, "source": "TEXTURE",
                         "texture": "/T", "channel": "RED"}, "weightmaps": {}}
class Q1:
    def get_actor_label(self): return "PRK_SW-Q1"
    def get_class(self):
        c = mock.MagicMock(); c.get_name.return_value = "Plain_1_C"; return c
    def get_editor_property(self, k): return mock.MagicMock()
    def modify(self): pass
bounds_record_calls = []
commit_record_calls = []
def conf(after=None, predicted=None, edit=False, resident=True, can_wait=True, **kw):
    after_snap = after or copy.deepcopy(snap_q1)
    region = {"proxies": predicted or ["LandscapeStreamingProxy_7_6_0"], "region": None}
    with mock.patch.object(L, "_world_guard", return_value=(W, None)), \
         mock.patch.object(L, "_write_guard", return_value=(W, None)), \
         mock.patch.object(L, "_resolve_stamp", return_value=(Q1(), mock.MagicMock(), None)), \
         mock.patch.object(L, "_stamp_snapshot", side_effect=[copy.deepcopy(snap_q1), after_snap]), \
         mock.patch.object(L, "_proxies_overlapping", return_value=["LandscapeStreamingProxy_7_6_0"]), \
         mock.patch.object(L, "_predict_proxies", return_value=region), \
         mock.patch.object(L, "_edit_mode_active", return_value=edit), \
         mock.patch.object(L, "_wm_bridge_refusal", return_value=([], None)), \
         mock.patch.object(L, "_brushes_covering", return_value=[]), \
         mock.patch.object(L, "_dirty_landscape_state", return_value={}), \
         mock.patch.object(L, "_write_fields", return_value=None), \
         mock.patch.object(L, "_record_stamp_bounds", side_effect=lambda w, a, before, after: (
             bounds_record_calls.append((before, after)), {"ok": True, "boxes": 2})[1]), \
         mock.patch.object(L, "_record_commit_actor", side_effect=lambda w, a, action: (
             commit_record_calls.append(action), {"ok": True, "tracked": True})[1]), \
         mock.patch.object(L, "_LandscapeTracer", Ground), \
         mock.patch.object(L, "_can_wait_for_compile", return_value=can_wait), \
         mock.patch.object(L, "_finish_asset_compilation", return_value={"waited": True, "remaining": 0}), \
         mock.patch.object(L, "_load_checked", return_value=object()), \
         mock.patch.object(L.unreal, "find_object", return_value=object() if resident else None), \
         mock.patch.object(L.unreal.EditorAssetLibrary, "does_asset_exist", return_value=True):
        return L.stamp_configure(actor="PRK_SW-Q1", **kw)
after = copy.deepcopy(snap_q1); after["location"][2] = 26920.0
r = conf(after=after, location_z=26920.0, dry_run=False)
check("configure outside EL mode returns a render probe", r["ok"] and r["render_probe"]["phase"] == "check"
      and r["render_probe"]["expect_change"] is True and len(r["render_probe"]["points"]) == 9
      and r["render_probe"]["lod_bias_original"] == 6 and "dirty_after_call_note" in r)
r = conf(after=after, location_z=26920.0, dry_run=False, edit=True)
check("configure in EL mode: no probe, note", r["ok"] and r["render_probe"] is None and "mode exit" in r["render_probe_note"])
moved = copy.deepcopy(snap_q1); moved["location"][0] = 170000.0
r = conf(after=moved, location_xy=[170000.0, -240000.0], dry_run=False)
check("configure move probes old + new footprints", len(r["render_probe"]["points"]) == 18)
check("successful guarded configure records actual old/new boxes", r["bounds_history"]["ok"]
      and bounds_record_calls[-1] == (snap_q1, moved))
check("successful guarded configure records commit actor ownership", r["commit_history"]["tracked"]
      and commit_record_calls == ["configure", "configure", "configure"])
recorded_before = len(bounds_record_calls)
commit_recorded_before = len(commit_record_calls)
r = conf(location_z=26920.0, dry_run=True)
check("dry-run configure never records ownership", r["ok"] and len(bounds_record_calls) == recorded_before
      and len(commit_record_calls) == commit_recorded_before)
r = conf(location_z=snap_q1["location"][2], dry_run=False)
check("no-change configure never records ownership", r["ok"] and not r["update_requested"]
      and len(bounds_record_calls) == recorded_before)
check("no-change configure never records commit ownership", len(commit_record_calls) == commit_recorded_before)
r = conf(location_z=26920.0, dry_run=False, edit=True)
check("unverified configure never records successful ownership", not r["ok"]
      and len(bounds_record_calls) == recorded_before)
check("unverified configure never records commit ownership", len(commit_record_calls) == commit_recorded_before)
r = conf(predicted=[f"LandscapeStreamingProxy_{i}_0_0" for i in range(13)], location_z=26920.0)
check("configure budget uses predicted proxies", r.get("refused") and "predicted to dirty 13" in r["error"]
      and r["footprint_proxies"] == ["LandscapeStreamingProxy_7_6_0"])
check("refused configure never records ownership", len(bounds_record_calls) == recorded_before)
check("refused configure never records commit ownership", len(commit_record_calls) == commit_recorded_before)
wm = {"layer": "Ground_Sand", "texture": "/Game/T/T_Sand"}
r = conf(weightmap=wm, dry_run=False, resident=False, can_wait=False)
check("configure refuses an unbuilt texture without the bridge wait", r.get("refused") and "wait_for_asset_compile" in r["error"])

# stamp_spawn dry run: predicted proxies include the class-default box; texture-wait guard
def spawn(resident=True, can_wait=True, default=(4065.0, 4065.0, 1.0), **kw):
    info = {"package": "/Game/ErrantWorlds/ErrantLandscapeStampLibrary/IcelandicTerrain/IcelandicTerrain",
            "loaded_class": object() if resident else None}
    with mock.patch.object(L, "_world_guard", return_value=(W, None)), \
         mock.patch.object(L._el, "_pie_guard", return_value=None), \
         mock.patch.object(L, "_edit_mode_active", return_value=True), \
         mock.patch.object(L, "_brush_load_state", return_value=(10, 10)), \
         mock.patch.object(L, "_backup_state", return_value={"ok": True}), \
         mock.patch.object(L, "_resolve_stamp_class", return_value=(info, None)), \
         mock.patch.object(L, "_proxies_overlapping", return_value=["10_8", "10_9", "11_8", "11_9"]), \
         mock.patch.object(L, "_proxy_boxes", return_value=PROXIES), \
         mock.patch.object(L, "_non_partial_brushes", return_value=[]), \
         mock.patch.object(L, "_landscape_expand_cm", return_value=(E, "test")), \
         mock.patch.object(L, "_cdo_scale", return_value=list(default)), \
         mock.patch.object(L, "_cdo_heightmap", return_value={"blend_method": "ADD_AND_SUBTRACT", "invert": False,
                                                               "multiplier": 0.6}), \
         mock.patch.object(L, "_brushes_covering", return_value=[]), \
         mock.patch.object(L, "_can_wait_for_compile", return_value=can_wait), \
         mock.patch.object(L, "_all", return_value=[]):
        args = dict(stamp_class=info["package"], x=560000, y=210000, yaw=0, scale=[3000, 3000, 0.035], location_z=-455,
                    priority=5)
        args.update(kw)
        return L.stamp_spawn(**args)
r = spawn()
check("spawn predicts the class-default box", r["ok"] and set(r["predicted_proxies"]) == block(9, 11, 8, 10)
      and len(r["footprint_proxies"]) == 4)
r = spawn(max_dirty_proxies=8)
check("spawn budget uses predicted proxies", not r["ok"] and "proxy_budget" in r["failed_guards"])
r = spawn(resident=False, can_wait=False)
check("spawn without the compile wait refuses a cold class", not r["ok"] and "asset_compile_wait" in r["failed_guards"]
      and set(r["predicted_proxies"]) == block(10, 11, 8, 9))
check("spawn cold class ok with the bridge wait", spawn(resident=False, can_wait=True)["ok"])

# step(): the previous real step's dirty list lands, its render check advances, then this step runs.
log = os.path.join(tempfile.mkdtemp(prefix="prk_step_"), "step_log.json")
plan = [("configure", "SW-Q1"), ("spawn", "SW-H1")]
fake = types.SimpleNamespace()
fake.dirty = ["9_6"]
fake.dirty_landscape = lambda: {"dirty_proxies": [{"label": f"LandscapeStreamingProxy_{p}_0"} for p in fake.dirty]}
fake.rc_queue = []
fake.rc_calls = []
def _rc(**kw):
    fake.rc_calls.append(kw)
    return fake.rc_queue.pop(0)
fake.stamp_render_check = _rc
fake.stamp_configure = lambda **kw: {"ok": True, "verified": True, "diff": [{"field": "location"}],
                                     "expected_proxies": ["LandscapeStreamingProxy_9_6_0"],
                                     "predicted_proxies": ["LandscapeStreamingProxy_9_6_0"],
                                     "render_probe": dict(probe0, actor="PRK_SW-Q1")}
fake.stamp_spawn = lambda **kw: {"ok": True, "verified": True, "actor_name": "WideCanyon_C_1",
                                 "footprint_proxies": ["LandscapeStreamingProxy_8_8_0"]}
rows = {"SW-Q1": {"Class": "Plain_1_C"}, "SW-H1": {"Class": "WideCanyon_C"}}
def step(i, **kw):
    with mock.patch.dict(sys.modules, {"prk_lake_ops": fake}), \
         mock.patch.object(Z.importlib, "reload", lambda m: m), \
         mock.patch.object(Z, "configure_args", return_value={"actor": "PRK_SW-Q1"}), \
         mock.patch.object(Z, "spawn_args", return_value={}), \
         mock.patch.object(Z, "class_paths", return_value={}):
        return Z.step(plan, rows, i, log, **kw)
r = step(0, dry_run=True)
check("step dry run writes no log", r["ok"] and not os.path.exists(log))
r = step(0, dry_run=False)
check("step 0 recorded with its probe", r["ok"] and Z.read_log(log)["steps"][0]["render_probe"]["phase"] == "check"
      and Z.read_log(log)["steps"][0]["dirty_before"] == ["9_6"])
check("step refuses a repeat", step(0, dry_run=False).get("refused"))
fake.dirty = ["9_6", "10_6"]
fake.rc_queue = [{"ok": True, "verdict": "stale_toggled", "probe": dict(probe0, phase="toggled")}]
r = step(1, dry_run=False)
s0 = Z.read_log(log)["steps"][0]
check("step 1 waits on the toggle", r.get("pending_render_check") and s0["dirty_landed"] == ["10_6", "9_6"]
      and s0["dirty_new"] == ["10_6"] and s0["render_probe"]["phase"] == "toggled" and len(Z.read_log(log)["steps"]) == 1)
fake.rc_queue = [{"ok": True, "verdict": "restored", "probe": dict(probe0, phase="restored")}]
check("step 1 still waits (restored)", step(1, dry_run=False).get("pending_render_check"))
fake.rc_queue = [{"ok": True, "verdict": "still_stale", "probe": dict(probe0, phase="done", verdict="still_stale")}]
r = step(1, dry_run=False)
check("still_stale stops", r.get("refused") and len(Z.read_log(log)["steps"]) == 1)
r = step(1, dry_run=False, accept_stale=True)
check("accept_stale continues; spawn recorded", r["ok"] and len(Z.read_log(log)["steps"]) == 2
      and Z.read_log(log)["steps"][1]["footprint_proxies"] == ["8_8"] and len(fake.rc_calls) == 3)
recs = []
with mock.patch.dict(sys.modules, {"prk_lake_ops": fake}), mock.patch.object(Z.importlib, "reload", lambda m: m), \
     mock.patch.object(Z, "configure_args", return_value={}), mock.patch.object(Z, "class_paths", return_value={}):
    recs = Z.run_plan([("configure", "SW-Q1")], rows, dry_run=False)
check("run_plan labels the same-call dirty list", "dirty_same_call_unlanded" in recs[0] and "dirty_proxies" not in recs[0]
      and "step()" in recs[0]["dirty_note"])

# --- MCP wrappers ----------------------------------------------------------------------------------------------------
sys.path.insert(0, _PY); os.chdir(_PY)
import inspect
import unreal_mcp_server_advanced as S
pairs = [("errant_stamp_render_check", "stamp_render_check"), ("errant_path_change_desc", "path_change_desc"),
         ("errant_road_profile_fit", "road_profile_fit"), ("errant_ghyll_floor_trace", "ghyll_floor_trace"),
         ("errant_road_qa", "road_qa"), ("errant_path_height_shape_check", "path_height_shape_check"),
         ("errant_path_regenerate", "path_regenerate"), ("errant_stamp_configure", "stamp_configure"),
         ("errant_stamp_spawn", "stamp_spawn"), ("errant_stamp_delete", "stamp_delete"),
         ("errant_edit_mode_set", "edit_mode_set"), ("errant_stamp_seam_scan", "stamp_seam_scan")]
for w, f in pairs:
    check(f"wrapper {w} signature matches", set(inspect.signature(getattr(S, w)).parameters)
          == set(inspect.signature(getattr(L, f)).parameters))
seen = []
S._run_prk_module = lambda module, func, **kw: seen.append((module, func, kw)) or {"ok": True}
S.errant_path_change_desc("R8_QuarryRoad", "EPD_MountainRoad")
check("change_desc wrapper dry by default", seen[-1][1] == "path_change_desc" and seen[-1][2]["dry_run"] is True
      and seen[-1][2]["p4_revert_adds"] is True)
S.errant_stamp_render_check("PRK_SW-Q1", probe0)
check("render_check wrapper dry by default", seen[-1][1] == "stamp_render_check" and seen[-1][2]["dry_run"] is True)
S.errant_path_regenerate("R8")
check("regenerate wrapper retries by default", seen[-1][2]["retry"] is True and seen[-1][2]["height_tol_cm"] == 150.0)
# ======================================= ROUND 2 (2026-10-08 afternoon) ===========================================
# 1. R2b: a recreated DirtRoad has no adjust lists yet; the table / templates supply the layers, no retry.
dirt_rows = [{"xy": [i, 0], "spline_z": 0.0, "ground_z": 0.0, "layers": {"Path_Dirt": 0.9, "Ground_Dirt": 0.05}}
             for i in range(20)]
dirt = dict(base_snap, centreline=dirt_rows)
lib.reset_mock()
r = regen([base_snap, dirt], desc="EPD_DirtRoad", own=([], None))
check("R2b: empty adjust lists -> table layers, verified, no retry", r["ok"] and not r["retried"]
      and lib.update_path_based_on_mask.call_count == 1 and r["own_layers"] == ["Ground_Dirt", "Path_Dirt"]
      and r["verdict"]["paint"]["coverage"] == 1.0 and r["own_layer_sources"]["adjust_lists"] == [])
lib.reset_mock()
r = regen([base_snap, dict(base_snap, centreline=[dict(x, layers={}) for x in dirt_rows])], desc="EPD_StoneRoad",
          own=([], None))
check("unknown layers never fail or retry", r["ok"] and not r["retried"] and r["verdict"]["paint"]["skipped"]
      and lib.update_path_based_on_mask.call_count == 1)
lib.reset_mock()
r = regen([base_snap, dirt], desc="EPD_StoneRoad", own=[([], None), (["Path_Dirt"], True)])
check("own layers re-read after the run", r["ok"] and r["own_layers"] == ["Path_Dirt"])
check("retry_reason ignores a skipped paint check", L._retry_reason("water", {"reasons": [], "paint": L._layer_verdict([], [])}) is None)
# 2. road_qa false misses: one own layer >= 0.5 is enough; a partial own list is completed by the union.
straight = [{"xy": [i, -220000], "layers": {"Path_Dirt": 1.0, "Ground_Dirt": 0.0}} for i in range(13)]
check("MAX over own layers: Path_Dirt 1, Ground_Dirt 0 is covered",
      L._layer_verdict(straight, ["Ground_Dirt", "Path_Dirt"], min_coverage=0.0)["coverage"] == 1.0)
check("a partial own list produces the 13 false misses",
      len(L._layer_verdict(straight, ["Ground_Dirt"], min_coverage=0.0)["missing_at"]) == 13)
with mock.patch.object(L, "_own_adjustments", return_value=(["Ground_Dirt"], False)), \
     mock.patch.object(L, "_desc_template_layers", return_value=[]), \
     mock.patch.dict(L._TEMPLATE_LAYER_CACHE, {}, clear=True):
    lay, aff, src = L._resolve_own_layers(FP(), "EPD_DirtRoad")
check("union completes a partial adjust list", lay == ["Ground_Dirt", "Path_Dirt"] and src["table"] == ["Ground_Dirt", "Path_Dirt"])
with mock.patch.object(L._el, "paths_describe", return_value={"ok": True, "adjust_lists": []}):
    check("no adjust lists -> affects unknown (None), not False", L._own_adjustments(FP()) == ([], None))
check("miss detail carries the layer weights", L._layer_verdict(straight, ["Ground_Dirt"])["missing_detail"][0]["layers"]
      == {"Path_Dirt": 1.0, "Ground_Dirt": 0.0})
# 3. change_desc dry run reports no label_unused failure
r = change()
check("change dry run: label_unused exempt in the report", r["ok"] and r["create_plan"]["failed_guards"] == []
      and r["create_plan"]["ok"] is True and "label_unused" in r["create_plan"]["exempt_guards"][0])
# 4. water surface verdict
def seg(surface, bed, edges, width=650.0):
    return {"xy": [0, 0], "surface_z": surface, "bed_z": bed, "width_cm": width, "edge_ground_z": edges}
under = {"segments": [seg(1000.0 - 190.0, 1000.0, [1100.0, 1100.0]) for _ in range(10)], "segments_total": 219}
wv = L._water_surface_verdict(under)
check("water under the bed: not verified, template fault", not wv["verified"] and wv.get("template_fault")
      and "under the landscape bed" in wv["reasons"][0] and wv["depth_cm"]["min"] == -190.0)
floating = {"segments": [seg(1000.0, 950.0, [900.0, 990.0]) for _ in range(10)]}
check("floating edges flagged", not L._water_surface_verdict(floating)["verified"]
      and "edges float" in L._water_surface_verdict(floating)["reasons"][0])
good_w = {"segments": [seg(1000.0, 952.0, [1010.0, 1020.0]) for _ in range(10)]}
gw = L._water_surface_verdict(good_w)
check("G2-like water verified (48 cm deep, edges under the banks)", gw["verified"] and gw["depth_cm"]["p50"] == 48.0
      and gw["width_cm"]["p50"] == 650.0)
w_good_snap = dict(base_snap, surface=surf, centreline=rows2)
lib.reset_mock()
r = regen([base_snap, w_good_snap, w_good_snap], desc="EPD_Stream", water=[dict(good_w), dict(under), dict(good_w)])
check("water under the bed: no retry, template_fault reported", not r["ok"] and not r["retried"]
      and r["retry_reason"] is None and r.get("template_fault") and lib.update_path_based_on_mask.call_count == 1)
lib.reset_mock()
r = regen([base_snap, w_good_snap], desc="EPD_Stream", water=[dict(good_w), dict(good_w)])
check("good water verified with the surface check", r["ok"] and r["verdict"]["water"]["verified"])
# 5. template spawn offsets
off = lambda z: g.parse_random_offset(f"(bRandomizeLocation=True,RandomLocation=(Min=(X=0,Y=0,Z={z}),Max=(X=0,Y=0,Z={z})))")
stream_bad = {"name": "EPD_Stream/EPT_Stream_C", "water": True, "bed_cm": -135.0,
              "meshes": [{"component": "Water_GEN_VARIABLE", "mesh": "SM_River", "offset": off(-325)}]}
stream_ok = dict(stream_bad, meshes=[{"component": "Water_GEN_VARIABLE", "mesh": "SM_River", "offset": dict(
    g.parse_random_offset("(bRandomizeLocation=True,bRandomizeScale=True,RandomLocation=(Min=(X=0,Y=0,Z=-90),"
                          "Max=(X=0,Y=0,Z=-90)),RandomScale=(Min=(X=1,Y=0.18,Z=1),Max=(X=1,Y=0.18,Z=1)))"))}])
river = {"name": "EPD_River/EPT_River_C", "water": True, "bed_cm": -525.0,
         "meshes": [{"component": "Water_GEN_VARIABLE", "mesh": "SM_River", "offset": off(-325)}]}
tc = g.template_offset_check([river, stream_bad])
check("EPT_Stream -325 over -135: water_below_bed + copied", {f["flag"] for f in tc["flags"]} == {"water_below_bed", "copied_offset"}
      and next(f for f in tc["flags"] if f["flag"] == "water_below_bed")["template"] == "EPD_Stream/EPT_Stream_C")
tc = g.template_offset_check([river, stream_ok])
check("EPT_Stream -90: offset ok, but its RandomScale Y 0.18 is flagged as ignored on a spline mesh",
      [f["flag"] for f in tc["flags"]] == ["random_scale_ignored_spline_mesh"]
      and tc["flags"][0]["template"] == "EPD_Stream/EPT_Stream_C"
      and tc["meshes"][1]["random_scale"][0] == (1.0, 0.18, 1.0) and tc["meshes"][1]["random_scale_applied"] is False)
check("EPT_River (no RandomScale): no flag", g.template_offset_check([river])["ok"])
river_w = dict(river, meshes=[dict(river["meshes"][0], mesh_width_cm=3600.0)])
stream_w = dict(stream_ok, meshes=[dict(stream_ok["meshes"][0], mesh_width_cm=3600.0)])
tw = g.template_offset_check([river_w, stream_w])
check("effective width is the mesh's own (G1/G2 water 36 m)", all(m["effective_width_cm"] == 3600.0 for m in tw["meshes"])
      and tw["flags"][0]["effective_width_cm"] == 3600.0)
check("a non-spline mesh keeps its RandomScale (no flag)", g.template_offset_check([dict(stream_ok, meshes=[dict(
      stream_ok["meshes"][0], spline_mesh=False)])])["ok"])
check("no randomize -> offset 0", g.template_offset_check([dict(stream_bad, meshes=[{"component": "c", "mesh": "m",
      "offset": g.parse_random_offset("")}])])["meshes"][0]["offset_z"] == [0.0, 0.0])
check("road templates never water_below_bed", g.template_offset_check([dict(stream_bad, water=False)])["ok"])
# 8. foliage bands
line = [[0, 0], [100000, 0]]                               # 1 km along +X: left = +Y
inst = ([(50000.0, 5000.0, "conifer")] * 30 + [(50000.0, -5000.0, "stone")] * 20 + [(-3000.0, 1000.0, "conifer")]
        + [(50000.0, 30000.0, "conifer")])
bd = g.band_density(line, inst, [[-80, -24], [24, 80]])
check("band density: left/right by sign, ends and far excluded", bd["counts"] == {"conifer": [0, 30], "stone": [20, 0]}
      and bd["instances_in_bands"] == 50 and bd["per_ha"]["conifer"][1] == round(30 / (56 * 1000 / 1e4), 2))
C = L.FOLIAGE_CATEGORIES
check("categories: hemlock, broadleaf, deadwood, rubble, ore; ForestFloor is not ore",
      g.categorize("ES_BF_Hemlock SM_Hemlock_02", C) == "conifer" and g.categorize("ES_Oak SM_Oak_01", C) == "broadleaf"
      and g.categorize("ES_Birch", C) == "broadleaf" and g.categorize("ES_BF_FallenTree", C) == "deadwood"
      and g.categorize("SM_Stump_03", C) == "deadwood" and g.categorize("SM_Rubble_02", C) == "stone"
      and g.categorize("ES_IronOre_Vein SM_Ore_01", C) == "ore" and g.categorize("ES_ForestFloor_Ferns", C) == "other")
check("default bands", L.DEFAULT_FOLIAGE_BANDS_M == [[-60, -20], [-20, -6], [-6, 6], [6, 20], [20, 60]]
      and inspect.signature(L.foliage_band_density).parameters["bands_m"].default is None)
check("categorize", g.categorize("ES_Riverbank_Conifers SM_Pine_03", L.FOLIAGE_CATEGORIES) == "conifer"
      and g.categorize("ES_River_Snags", L.FOLIAGE_CATEGORIES) == "snag" and g.categorize("x", L.FOLIAGE_CATEGORIES) == "other")
idx = g.PolylineIndex([[0, 0], [1000, 0], [1000, 1000]], 300)
check("polyline index nearest segment + sign", abs(idx.query(1100, 500)[0] + 100) < 1e-6 and idx.query(500, 100)[0] > 0)
try:
    g.band_density(line, [], [[10, 5]]); check("bands validated", False)
except ValueError:
    check("bands validated", True)
# 10. StoneRoad only inside settlements
town = g.Footprint(0, 0, 30, 20000, 10000)
with mock.patch.object(L, "_settlement_footprints", return_value=([town], [{"marker": "Crossroads"}])):
    ok_in, d_in = L._stone_road_check(None, [[0, 0, 0], [5000, 2000, 0]], False)
    ok_out, d_out = L._stone_road_check(None, [[0, 0, 0], [50000, 0, 0]], False)
    ok_flag, _ = L._stone_road_check(None, [[0, 0, 0], [50000, 0, 0]], True)
check("stone inside a town ok", ok_in and d_in["points_outside"] == 0)
check("stone outside refused", not ok_out and d_out["points_outside"] == 1 and "towns and cities" in d_out["rule"])
check("allow_stone_outside_settlement overrides", ok_flag)
with mock.patch.object(L, "_settlement_footprints", return_value=([], [])):
    ok_none, d_none = L._stone_road_check(None, [[0, 0, 0]], False)
check("no footprint resolved -> refused with the flag note", not ok_none and "allow_stone_outside_settlement" in d_none["note"])
check("rotated footprint containment", g.points_outside_footprints([[0, 4000, 0], [9000, 0, 0]], [g.Footprint(0, 0, 90, 10000, 1000)]) == [1])
# 11. bridge signature noted in the wrappers
check("layer-weight helpers document the bridge signature", "no world parameter" in L._layer_weights.__doc__
      and "no world parameter" in L._centreline.__doc__ and "no world parameter" in L.road_qa.__doc__)
for w, f in [("errant_foliage_band_density", "foliage_band_density"), ("errant_path_create", "path_create"),
             ("errant_path_change_desc", "path_change_desc"), ("errant_path_regenerate", "path_regenerate")]:
    check(f"r2 wrapper {w} signature matches", set(inspect.signature(getattr(S, w)).parameters)
          == set(inspect.signature(getattr(L, f)).parameters))
# --- vertex-row probe precision (road_qa false misses on R11b at Y -112000, R12 at X 386000; 2026-10-08) -----------
import struct
def f32(v): return struct.unpack("f", struct.pack("f", v))[0]
PAINT_ROW = 3784                        # vertex row at Y = -1625600 + 400 * 3784 = -112000: Path_Dirt 1, rows beside 0
def engine_weight(y, fixed=False):
    """ULandscapeComponent::GetLayerWeightAtLocation along Y: floor/ceil of the double, lerp fraction of a FLOAT cast
    (the engine) or of the same double (the bridge fix)."""
    ly = (y + 1625600.0) / 400.0
    y1, y2 = math.floor(ly), math.ceil(ly)
    frac = (ly - y1) if fixed else (f32(ly) - math.floor(f32(ly)))
    row = lambda r: 1.0 if r == PAINT_ROW else 0.0
    return row(y1) * (1 - frac) + row(y2) * frac
class RowBridge:
    def sample_landscape_layer_weights(self, v, editing, pie):
        return json.dumps({"ok": True, "found": True, "layers": {"Path_Dirt": engine_weight(v.y), "Ground_Grass": 1 - engine_weight(v.y)}})
check("engine float-fraction bug reproduced: -112000.0001 reads the row below", engine_weight(-112000.0001) == 0.0
      and engine_weight(-112000.0) == 1.0)
check("bridge fix (double fraction) reads the row", abs(engine_weight(-112000.0001, fixed=True) - 1.0) < 1e-6)
check("snap rounds to 0.1 cm", g.snap_probe_xy(585971.04, -112000.0001) == (585971.0, -112000.0))
V2 = lambda x, y: types.SimpleNamespace(x=x, y=y)
with mock.patch.object(L._el.unreal, "Vector2D", V2), mock.patch.object(L.unreal, "Vector2D", V2):
    r = L._el.sample_layer_weights(RowBridge(), 585971.0, -112000.0001)
    check("sample_layer_weights: a spline XY of -112000.0001 counts as painted", r["layers"]["Path_Dirt"] == 1.0)
    check("a point 0.6 mm off still lands near the row", L._el.sample_layer_weights(RowBridge(), 0, -112000.06)["layers"]["Path_Dirt"] > 0.99)
    rows = L._layer_weights(RowBridge(), [(585000.0 + 1000 * i, -112000.0001) for i in range(26)])
    v = L._layer_verdict(rows, ["Ground_Dirt", "Path_Dirt"], min_coverage=0.0)
    check("R11b row: 26 samples on Y -112000 all covered", v["coverage"] == 1.0 and not v["missing_at"])
src_lake = open(os.path.join(_CONTENT_PY, "prk_lake_ops.py"), encoding="utf-8").read()
src_el = open(os.path.join(_CONTENT_PY, "prk_errant.py"), encoding="utf-8").read()
check("every layer probe goes through the one helper", "sample_landscape_layer_weights(" not in src_lake
      and src_el.count("sample_landscape_layer_weights(") == 1)
print("FAILS:", fails)
