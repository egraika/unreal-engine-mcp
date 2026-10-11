import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
_sys.path.insert(0, _CONTENT_PY)
_sys.path.insert(0, _PY)
import json, math, shutil, tempfile
import prk_macro_geom as G
import prk_terrain_geom as g
from helpers import prk_terrain_author_host as H

fails = []
def check(name, cond):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond: fails.append(name)

def raises(fn, exc=ValueError):
    try:
        fn()
    except exc:
        return True
    return False

U = G.height_unit_cm(300.0)
K = 0.949

# --- relief math
check("unit 76800", U == 76800.0)
s = G.scale_z_for_relief(3000.0, K, 0.5, U)
check("relief round trip", abs(G.stamp_relief_cm(K, 0.5, s, False, U) - 3000.0) < 1e-6)
check("invert negates", abs(G.stamp_relief_cm(K, 0.5, s, True, U) + 3000.0) < 1e-6)
check("k*m <= 0 raises", raises(lambda: G.scale_z_for_relief(100, 0.0, 0.5, U)))
check("relief <= 0 raises", raises(lambda: G.scale_z_for_relief(0.0, K, 0.5, U)))
check("bank slope", abs(G.bank_slope_deg(3000, 60000, 0.4) - math.degrees(math.atan(3000 / (30000 * 0.8)))) < 1e-9)

# --- plan_relief
base = dict(multiplier=0.5, scale_z=0.1, actor_z=1000.0, landscape_z=-100.0, unit_cm=U, k=K)
p = G.plan_relief(blend="MAX", invert=False, crest_above_ground_m=30, ring_median_cm=5000.0, **base)
check("max crest ok", p["ok"] and abs(p["scale_z"] - 7000.0 / (K * 0.5 * U)) < 1e-9 and p["invert"] is False)
check("max crest predicted", p["predicted"]["crest_world_cm"] == 8000.0 and abs(p["relief_after_cm"] - 7000.0) < 0.01)
check("max depth refused", not G.plan_relief(blend="MAX", invert=False, depth_below_ground_m=5, ring_median_cm=5000.0, **base)["ok"])
check("max crest below base refused", not G.plan_relief(blend="MAX", invert=False, crest_above_ground_m=1,
                                                        ring_median_cm=0.0, **base)["ok"])
pm = G.plan_relief(blend="MIN", invert=True, depth_below_ground_m=10, ring_median_cm=5000.0,
                   **dict(base, actor_z=6000.0))
check("min depth ok", pm["ok"] and pm["invert"] and abs(pm["scale_z"] - 2000.0 / (K * 0.5 * U)) < 1e-9
      and pm["predicted"]["floor_world_cm"] == 4000.0)
check("min crest refused", not G.plan_relief(blend="MIN", invert=True, crest_above_ground_m=5, ring_median_cm=0.0, **base)["ok"])
check("min from raising flips -> refused",
      not G.plan_relief(blend="MIN", invert=False, depth_below_ground_m=10, ring_median_cm=5000.0, **dict(base, actor_z=6000.0))["ok"])
check("min flip allowed", G.plan_relief(blend="MIN", invert=False, depth_below_ground_m=10, ring_median_cm=5000.0,
                                        allow_flip=True, **dict(base, actor_z=6000.0))["ok"])
check("relief_m flip refused", not G.plan_relief(blend="MAX", invert=False, relief_m=-20, **base)["ok"])
pf = G.plan_relief(blend="MAX", invert=False, relief_m=-20, allow_flip=True, **base)
check("relief_m flip allowed", pf["ok"] and pf["invert"] and abs(pf["relief_after_cm"] + 2000.0) < 0.01)
s1000 = 1000.0 / (K * 0.5 * U)
pa = G.plan_relief(blend="ADD_AND_SUBTRACT", invert=False, crest_above_ground_m=20, ring_median_cm=5000.0,
                   measured_crest_cm=6200.0, **dict(base, scale_z=s1000))
check("additive crest", pa["ok"] and abs(pa["relief_after_cm"] - 1800.0) < 0.01)
pd = G.plan_relief(blend="ADD_AND_SUBTRACT", invert=True, depth_below_ground_m=10, ring_median_cm=5000.0,
                   measured_floor_cm=4500.0, **dict(base, scale_z=s1000))
check("additive depth", pd["ok"] and abs(pd["relief_after_cm"] + 1500.0) < 0.01)
check("additive crest needs measured crest", not G.plan_relief(blend="ONLY_ADD", invert=False, crest_above_ground_m=5,
                                                               ring_median_cm=1.0, **base)["ok"])
check("trough refuses terrain target", not G.plan_relief(blend="MAX", invert=False, polarity="trough",
                                                         crest_above_ground_m=5, ring_median_cm=0.0, **base)["ok"])
check("trough allows delta", G.plan_relief(blend="MAX", invert=False, polarity="trough", relief_delta_m=1, **base)["ok"])
pfac = G.plan_relief(blend="MAX", invert=False, relief_factor=2.0, **dict(base, k=None))
check("factor without k", pfac["ok"] and abs(pfac["scale_z"] - 0.2) < 1e-12 and pfac["relief_after_cm"] is None)
check("negative factor refused", not G.plan_relief(blend="MAX", invert=False, relief_factor=-1, **base)["ok"])
check("k target without k refused", not G.plan_relief(blend="MAX", invert=False, relief_delta_m=1, **dict(base, k=None))["ok"])
check("two relief targets refused", not G.plan_relief(blend="MAX", invert=False, relief_m=1, relief_factor=2, **base)["ok"])
check("no target refused", not G.plan_relief(blend="MAX", invert=False, **base)["ok"])
check("nan refused", not G.plan_relief(blend="MAX", invert=False, relief_m=float("nan"), **base)["ok"])
pl = G.plan_relief(blend="MAX", invert=False, lift_m=10, **base)
check("lift BZL -> location_z", pl["ok"] and pl["location_z"] == 900.0 and pl["lift_before_cm"] == 1100.0
      and "z_offset" not in pl)
plm = G.plan_relief(blend="ADD_AND_SUBTRACT", invert=False, lift_delta_m=2, offset_method="MANUAL", z_offset=50.0, **base)
check("lift manual -> z_offset", plm["ok"] and plm["z_offset"] == 250.0 and "location_z" not in plm)
plc = G.plan_relief(blend="MAX", invert=False, lift_m=0, crest_above_ground_m=30, ring_median_cm=5000.0, **base)
check("lift then crest uses new base", plc["ok"] and abs(plc["scale_z"] - 8100.0 / (K * 0.5 * U)) < 1e-9)
check("replace blend refuses terrain target", not G.plan_relief(blend="REPLACE", invert=False, crest_above_ground_m=5,
                                                                ring_median_cm=0.0, **base)["ok"])

# --- polylines and bead chains
pts = [[0.0, 0.0], [300000.0, 0.0]]
check("lengths", G.polyline_lengths(pts) == [0.0, 300000.0])
x, y, d = G.point_at([[0, 0], [100, 0], [100, 100]], 150)
check("point_at second segment", abs(x - 100) < 1e-9 and abs(y - 50) < 1e-9 and d == (0.0, 1.0))
check("interpolate", G.interpolate_by_arclength([10, 30], [0, 100], 25) == 15.0
      and G.interpolate_by_arclength([10, 30], [0, 100], 500) == 30.0)
check("taper ends and middle", G.taper_weight(0, 100, 30) == 0.0 and G.taper_weight(50, 100, 30) == 1.0
      and G.taper_weight(5, 100, 0) == 1.0)
chain = G.bead_chain(pts, 60000.0, 0.25, 30000.0, seed="ridge")
gaps = [b["s"] - a["s"] for a, b in zip(chain, chain[1:])]
check("chain spacing <= 0.25 width", gaps and max(gaps) <= 15000.0 + 1e-6)
check("chain end beads tapered", chain[0]["w"] < 1.0 and chain[-1]["w"] < 1.0 and min(b["w"] for b in chain) >= 0.02)
check("chain middle full", max(b["w"] for b in chain) == 1.0)
check("zero-weight ends dropped", chain[0]["s"] > 0.0 and chain[-1]["s"] < 300000.0)
check("yaws square", all(b["yaw"] in (0.0, 90.0, 180.0, 270.0) for b in chain))
check("yaws vary", len({b["yaw"] for b in chain}) > 1)
check("square beads never clip", all(g.rotation_clip_fit(yw, 600, 600) >= 1 - g.ROTATION_CLIP_TOL
                                     for yw in (0.0, 90.0, 180.0, 270.0)))
check("deterministic yaws", [b["yaw"] for b in chain] == [b["yaw"] for b in G.bead_chain(pts, 60000.0, 0.25, 30000.0, "ridge")])
one = G.bead_chain([[5.0, 7.0]], 60000.0, 0.25, 30000.0, seed="x")
check("single point -> one bead", len(one) == 1 and one[0]["w"] == 1.0 and (one[0]["x"], one[0]["y"]) == (5.0, 7.0))
check("zero length -> one bead", len(G.bead_chain([[1, 1], [1, 1]], 60000.0, 0.25, 0.0, "x")) == 1)
short = G.bead_chain([[0, 0], [1000, 0]], 60000.0, 0.25, 30000.0, "x")
check("short tapered line keeps one middle bead", len(short) == 1 and short[0]["s"] == 500.0)
untapered = G.bead_chain(pts, 60000.0, 0.25, 0.0, "x")
check("no taper keeps the ends", untapered[0]["s"] == 0.0 and untapered[-1]["s"] == 300000.0
      and all(b["w"] == 1.0 for b in untapered))
check("cummin", G.cummin([5, 3, 4, 1, 2]) == [5, 3, 3, 1, 1])

# --- bead planning
check("ridge skip small", G.plan_ridge_bead(1000, 1020, 20, K, 0.5, U) is None)
r = G.plan_ridge_bead(1000, 1020, 3000, K, 0.5, U, base_margin_cm=100)
check("ridge bead", r["location_z"] == 900 and r["crest_z"] == 4020 and abs(r["h_cm"] - 3120) < 1e-9
      and abs(r["scale_z"] - 3120 / (K * 0.5 * U)) < 1e-12)
check("valley skip shallow", G.plan_valley_bead(1200, 1000, 960, K, 0.5, U) is None)
v = G.plan_valley_bead(1200, 1000, 600, K, 0.5, U)
check("valley bead", v["location_z"] == 1200 and v["depth_cm"] == 600 and v["floor_z"] == 600)

# --- thumbnail estimates
check("thumb 249 -> 0.949", abs(G.thumbnail_k_estimate(249) - 0.949) < 0.002)
check("thumb 191 -> 0.53", abs(G.thumbnail_k_estimate(191) - 0.53) < 0.005)
check("polarity trough", G.thumbnail_polarity(190.22, 2.6) == "trough")
check("polarity peak", G.thumbnail_polarity(61.89, 202.43) == "peak")

# --- host: k precedence
tmp = tempfile.mkdtemp(prefix="terrain_author_checks_")
try:
    cfg = _os.path.join(tmp, "Config", "Editor", "ZoneAuthoring")
    _os.makedirs(cfg)
    with open(_os.path.join(cfg, "stamp_calibration.json"), "w") as f:
        json.dump({"textures": {"/Game/A/T_X.T_X": {"k_peak": 0.5, "polarity": "peak", "estimated": False,
                                                    "source": "measured"}}}, f)
    calls = []
    def fake_preview(tex, out_dir):
        calls.append(tex)
        return {"success": True, "max": 249, "mean": 60.0, "centre_mean": 200.0}
    k = H.resolve_k("/Game/A/T_X", root=tmp, preview=fake_preview)
    check("table wins over thumbnail", k["k_peak"] == 0.5 and not k["estimated"] and not calls)
    k = H.resolve_k("/Game/A/T_Y.T_Y", root=tmp, preview=fake_preview)
    check("thumbnail fallback", abs(k["k_peak"] - 0.949) < 0.002 and k["estimated"] and k["polarity"] == "peak" and calls)
    k = H.resolve_k("/Game/A/T_Z", root=tmp, preview=lambda t, o: {"success": False, "message": "nope"})
    check("no thumbnail -> None", k["k_peak"] is None)

    # --- host: capture loop
    from PIL import Image
    def png_bytes(w=4, h=3, color=(10, 20, 30)):
        import io
        buf = io.BytesIO()
        Image.new("RGB", (w, h), color).save(buf, format="PNG")
        return buf.getvalue()
    vdir = _os.path.join(tmp, "Saved", "TerrainAuthor")
    _os.makedirs(vdir)
    with open(_os.path.join(vdir, "views.json"), "w") as f:
        json.dump({"views": {"pass": {"location": [1.0, 2.0, 3.0], "rotation": [-30.0, 45.0, 0.0]}}}, f)

    class Clock:
        def __init__(self): self.t = 0.0; self.sleeps = []; self.on_sleep = None
        def clock(self): return self.t
        def sleep(self, s):
            self.sleeps.append(s); self.t += s
            if self.on_sleep: self.on_sleep()
    import datetime as _dt
    fixed_now = lambda: _dt.datetime(2026, 10, 9, 12, 0, 0)

    def make_dispatch(busy_polls, log, capture_fn=None):
        state = {"ready": 0}
        def dispatch(module, func, **kw):
            log.append(func)
            if func == "view_ready":
                state["ready"] += 1
                return {"ok": True, "subsystem_idle": state["ready"] > busy_polls, "pie": False,
                        "edit_mode_active": False}
            if func == "camera_get":
                return {"ok": True, "location": [1.0, 2.0, 3.5], "rotation": [-30.0, 45.0, 0.0]}
            if func == "capture_view" and capture_fn:
                return capture_fn(**kw)
            return {"ok": False}
        return dispatch

    clk = Clock()
    log = []
    written = {}
    def capture(path, **kw):
        written["path"] = path
        with open(path, "wb") as f:
            f.write(png_bytes()[:20])   # partial first
        def finish():
            if len(clk.sleeps) >= 6:
                with open(path, "wb") as f:
                    f.write(png_bytes())
        clk.on_sleep = finish
        return {"ok": True, "path": path, "camera_backend": "scene_capture_2d"}
    res = H.view(action="capture", dispatch=make_dispatch(2, log, capture), wait_shaders=lambda: {"complete": True},
                 name="pass", label="before", settle_s=2.0, timeout_s=60.0, root=tmp, sleep=clk.sleep,
                 clock=clk.clock, now=fixed_now)
    check("capture ok", res.get("ok") and H._capture_label(res["path"]) == "before")
    check("capture waited for idle", log.count("view_ready") == 3)
    check("capture settled", 2.0 in clk.sleeps)
    check("capture waited for complete PNG", H.complete_png(res["path"]) == res["size_bytes"])
    check("capture uses separate camera", res["camera_backend"] == "scene_capture_2d"
          and "camera_get" not in log and "camera_set" not in log)

    clk2 = Clock()
    res = H.view(action="capture", dispatch=make_dispatch(10 ** 6, [], capture), name="pass", label="x",
                 timeout_s=5.0, root=tmp, sleep=clk2.sleep, clock=clk2.clock, now=fixed_now)
    check("busy subsystem times out", not res["ok"] and "Timed out" in res["error"] and clk2.t >= 5.0)
    clk3 = Clock()
    def never_complete(path, **kw):
        open(path, "wb").close()
        return {"ok": True}
    res = H.view(action="capture", dispatch=make_dispatch(0, [], never_complete), name="pass", label="y",
                 timeout_s=5.0, root=tmp, sleep=clk3.sleep, clock=clk3.clock,
                 now=lambda: _dt.datetime(2026, 10, 9, 12, 0, 5))
    check("incomplete PNG times out", not res["ok"] and "PNG" in res["error"])
    check("unknown view refused", not H.view(action="capture", dispatch=make_dispatch(0, [], capture),
                                             name="nope", root=tmp)["ok"])

    # --- host: compare
    cdir = _os.path.join(vdir, "captures", "cmp")
    _os.makedirs(cdir)
    with open(_os.path.join(cdir, "20261009-100000_before.png"), "wb") as f:
        f.write(png_bytes(4, 3, (255, 0, 0)))
    with open(_os.path.join(cdir, "20261009-110000_after.png"), "wb") as f:
        f.write(png_bytes(8, 6, (0, 255, 0)))
    c = H.compare("cmp", root=tmp)
    check("compare stitched", c["ok"] and c["size"] == [8, 3] and _os.path.isfile(c["compare"])
          and c["compare"].endswith("_before_after.png"))
    c2 = H.compare("cmp", labels=["after", "before"], root=tmp)
    check("compare by labels", c2["ok"] and c2["compare"].endswith("_after_before.png")
          and c2["compare"] != c["compare"])
    check("compare excludes compare_ files", len(H._captures(tmp, "cmp")) == 2)

    # --- host: scoped save planning
    import prk_landscape_paint as P
    f_edit = _os.path.join(tmp, "a.uasset"); f_open = _os.path.join(tmp, "b.uasset")
    f_new = _os.path.join(tmp, "c.uasset"); f_ro = _os.path.join(tmp, "d.uasset"); f_del = _os.path.join(tmp, "e.uasset")
    fstat_text = "\n".join([
        f"... depotFile //d/a.uasset\n... clientFile {f_edit}\n... headAction edit\n",
        f"... depotFile //d/b.uasset\n... clientFile {f_open}\n... headAction edit\n... action edit\n... change 77\n",
        f"... depotFile //d/e.uasset\n... clientFile {f_del}\n... headAction edit\n... action delete\n... change 77\n",
    ])
    fst = P.parse_fstat(fstat_text)
    rows = [{"package": "/Game/a", "file": f_edit, "exists_on_disk": True, "writable": False},
            {"package": "/Game/b", "file": f_open, "exists_on_disk": True, "writable": True},
            {"package": "/Game/c", "file": f_new, "exists_on_disk": False, "writable": True},
            {"package": "/Game/d", "file": f_ro, "exists_on_disk": True, "writable": False},
            {"package": "/Game/e", "file": f_del, "exists_on_disk": True, "writable": True}]
    acts = {a["package"]: a["action"] for a in H.plan_save_actions(rows, fst, True, 123)}
    check("save actions", acts == {"/Game/a": "edit", "/Game/b": "ok", "/Game/c": "ok", "/Game/d": "refuse",
                                   "/Game/e": "refuse"})
    check("no checkout -> refuse", H.plan_save_actions(rows[:1], fst, False, 123)[0]["action"] == "refuse")
    check("cl 0 -> refuse", H.plan_save_actions(rows[:1], fst, True, 0)[0]["action"] == "refuse")
    check("default CL refused", not H.save_scoped(dispatch=lambda *a, **k: {"ok": True}, p4=None, parse_fstat=None,
                                                 changelist="default", expect_world="/Game/W")["ok"])
    p4_calls, d_calls = [], []
    def fake_p4(args):
        p4_calls.append(args)
        return {"ok": True, "stdout": fstat_text if args[0] == "fstat" else ""}
    def fake_dispatch(module, func, **kw):
        d_calls.append(func)
        if func == "save_scope":
            return {"ok": True, "rows": [dict(rows[0], dirty=True, kind="stamp"), dict(rows[1], dirty=True, kind="stamp"),
                                         dict(rows[2], dirty=True, kind="feature_spawned"),
                                         dict(rows[3], dirty=False, kind="proxy_included")]}
        if func == "save_packages_scoped":
            return {"ok": True, "saved": kw["packages"]}
        return {"ok": False}
    dry = H.save_scoped(dispatch=fake_dispatch, p4=fake_p4, parse_fstat=P.parse_fstat, stamps=["S"], checkout=True,
                        changelist=123, dry_run=True, expect_world="/Game/W")
    check("save dry run", dry["ok"] and dry["dry_run"] and "save_packages_scoped" not in d_calls
          and all(c[0] == "fstat" for c in p4_calls))
    real = H.save_scoped(dispatch=fake_dispatch, p4=fake_p4, parse_fstat=P.parse_fstat, stamps=["S"], checkout=True,
                         changelist=123, dry_run=False, expect_world="/Game/W")
    check("save real edits only the depot file", real["ok"] and ["edit", "-c", "123", f_edit] in p4_calls)
    check("save real saves dirty packages", real["save"]["saved"] == ["/Game/a", "/Game/b", "/Game/c"])
    nocheck = H.save_scoped(dispatch=fake_dispatch, p4=fake_p4, parse_fstat=P.parse_fstat, stamps=["S"],
                            dry_run=False, expect_world="/Game/W")
    check("save refuses an unopened depot file without checkout", not nocheck["ok"] and nocheck.get("refused"))

    # --- host: apply loop
    seq = []
    def apply_dispatch(module, func, **kw):
        seq.append((kw["start"], kw["replace"]))
        nxt = kw["start"] + kw["max_beads"]
        return {"ok": True, "spawned": [{"label": f"b{kw['start']}"}], "next_start": nxt if nxt < 10 else None}
    sl = []
    a = H.feature_apply(dispatch=apply_dispatch, name="r", replace=True, dry_run=False, beads_per_call=4,
                        expect_world="/Game/W", sleep=sl.append)
    check("apply loop starts", [s for s, _ in seq] == [0, 4, 8])
    check("real replace only first call", [rp for _, rp in seq] == [True, False, False])
    check("apply loop sleeps between calls", sl == [0.5, 0.5] and len(a["spawned"]) == 3 and a["ok"])
    seq.clear()
    H.feature_apply(dispatch=apply_dispatch, name="r", replace=True, dry_run=True, beads_per_call=4,
                    expect_world="/Game/W", sleep=lambda s: None)
    check("dry replace every call", [rp for _, rp in seq] == [True, True, True])
    stop = H.feature_apply(dispatch=lambda m, f, **kw: {"ok": False, "error": "guard"}, name="r",
                           expect_world="/Game/W", sleep=lambda s: None)
    check("apply loop stops on failure", not stop["ok"] and stop["error"] == "guard" and len(stop["calls"]) == 1)
    walk = []
    def dry_fail(module, func, **kw):
        walk.append(kw["start"])
        nxt = kw["start"] + kw["max_beads"]
        return {"ok": False, "error": "edit mode off", "next_start": nxt if nxt < 10 else None}
    dw = H.feature_apply(dispatch=dry_fail, name="r", dry_run=True, beads_per_call=4, expect_world="/Game/W",
                         sleep=lambda s: None)
    check("dry run walks every batch despite failures", walk == [0, 4, 8] and not dw["ok"])
    walk.clear()
    H.feature_apply(dispatch=dry_fail, name="r", dry_run=False, beads_per_call=4, expect_world="/Game/W",
                    sleep=lambda s: None)
    check("real run stops at the first failing batch", walk == [0])
    check("beads_per_call validated", not H.feature_apply(dispatch=apply_dispatch, name="r", beads_per_call=0,
                                                          expect_world="/Game/W")["ok"])
finally:
    shutil.rmtree(tmp, ignore_errors=True)

# --- presets and calibration files point at real assets
def asset_file(obj_path):
    pkg = obj_path.split(".")[0]
    return _os.path.join(_ROOT, "Content", *pkg[len("/Game/"):].split("/")) + ".uasset"
presets = H.load_presets(_ROOT)
check("presets schema", presets.get("schema_version") == 1 and set(presets["presets"]) ==
      {"woodland_hill", "rocky_shoulder", "limestone_cuesta", "broken_terrace", "eroded_ridge",
       "dry_swale", "mountain_pass", "rolling_saddle"})
for name, pr in presets["presets"].items():
    check(f"preset {name} class on disk", _os.path.isfile(asset_file(pr["stamp_class"])))
    check(f"preset {name} texture on disk", _os.path.isfile(asset_file(pr["texture"])))
    check(f"preset {name} square-safe fields", pr["mode"] in ("ridge", "valley") and pr["width_m"] > 0
          and 0 < pr["spacing_frac"] <= 1)
    check(f"preset {name} k resolvable from table", H.resolve_k(pr["texture"], root=_ROOT,
                                                                 preview=lambda t, o: {"success": False})["k_peak"])
cal = json.load(open(_os.path.join(_ROOT, "Config", "Editor", "ZoneAuthoring", "stamp_calibration.json")))
check("calibration textures on disk", all(_os.path.isfile(asset_file(t)) for t in cal["textures"]))
for name in ("limestone_cuesta", "broken_terrace"):
    pr = H.resolve_preset(name, _ROOT, preview=lambda t, o: {"success": False})
    entry = cal["textures"][pr["texture"]]
    check(f"{name} keeps source-model status and provenance",
          pr["k_estimated"] is True and "not live-measured" in pr["k_source"]
          and entry["evidence"].startswith("Saved/BadlandsTerrain/2026-10-08/source_Terrace_"))
check("eroded ridge reuses measured Plain12 response",
      H.resolve_preset("eroded_ridge", _ROOT, preview=lambda t, o: {"success": False})["k_estimated"] is False)
smooth = H.resolve_preset("rolling_saddle", _ROOT, preview=lambda t, o: {"success": False})
check("SmoothHill valleys use source peak rather than centre response",
      smooth["k_peak"] > 0.99 and smooth["k_estimated"] is True
      and "not live-measured" in smooth["k_source"]
      and cal["textures"][smooth["texture"]]["evidence"].endswith("source_SmoothHill.json"))

# --- editor module imports (unreal mocked) and its pure helpers
from unittest import mock
_sys.modules["unreal"] = mock.MagicMock()
import prk_terrain_author as TA
args = {"x": 100.0, "y": 200.0, "location_z": 50.0, "scale": [600, 600, 0.2], "yaw": 270.0, "priority": 9,
        "invert": False, "multiplier": 0.5, "blend_method": "Max"}
snap = {"location": [100.2, 200.0, 50.0], "scale": [600.0, 600.0, 0.2], "yaw": -90.0, "priority": 9,
        "heightmap": {"invert": False, "multiplier": 0.5, "blend_method": "MAX"}}
check("bead snapshot matches plan", TA._matches(snap, args) == [])
check("bead snapshot differs", TA._matches(dict(snap, priority=3), args) == ["priority"])
render_args = dict(args, falloff=True, falloff_start=0.4, falloff_end=1.0, falloff_noise=0.0,
                   z_offset=0.0, lod_bias=3, offset_method="BrushZLocation")
render_texture = "/Game/Test/Peak.Peak"
render_snap = dict(snap, heightmap=dict(snap["heightmap"], falloff=True, falloff_start_xy=[0.4, 0.4],
                    falloff_end_xy=[1.0, 1.0], falloff_noise=0.0, z_offset=0.0, lod_bias=3,
                    offset_method="BRUSH_Z_LOCATION", source="TEXTURE", texture=render_texture))
check("bead rendering settings match normalized plan", TA._matches(render_snap, render_args, render_texture) == [])
for arg, field, changed in [("falloff", "falloff", False), ("falloff_start", "falloff_start_xy", [0.7, 0.7]),
                            ("falloff_end", "falloff_end_xy", [0.9, 0.9]), ("falloff_noise", "falloff_noise", 0.2),
                            ("z_offset", "z_offset", 10), ("lod_bias", "lod_bias", 5),
                            ("offset_method", "offset_method", "MANUAL"),
                            ("texture", "texture", "/Game/Test/Other.Other"), ("source", "source", "NOISE")]:
    changed_snap = dict(render_snap, heightmap=dict(render_snap["heightmap"], **{field: changed}))
    check("bead resume catches changed " + arg, TA._matches(changed_snap, render_args, render_texture) == [arg])
check("name regex", TA._NAME_RE.match("Pass_01") and not TA._NAME_RE.match("bad name"))

# --- editor module logic with the world faked (no unreal calls reach the mock)
L = TA.L
class FakeWorld:
    def get_path_name(self): return "/Game/W.W"
class FlatTracer:  # ground: a 20 m bump around x = 150000 on a 1000 cm plain
    def __init__(self, world): self.count = 0
    def z(self, x, y):
        self.count += 1
        return 1000.0 + 2000.0 * max(0.0, 1.0 - abs(x - 150000.0) / 50000.0)
written = {}
patches = {
    (L, "_world_guard"): lambda ew: (FakeWorld(), None),
    (L, "_write_guard"): lambda ew: (FakeWorld(), None),
    (L, "_edit_mode_active"): lambda w: False,
    (L, "_LandscapeTracer"): FlatTracer,
    (L, "_brushes_covering"): lambda w, pts: [{"label": "Continents_1", "priority": 6, "blend_method": "MAX"},
                                             {"label": "PRK_Ridge_03", "priority": 40, "blend_method": "MAX"}],
    (L, "_predict_proxies"): lambda w, boxes: {"proxies": ["P1", "P2"]},
    (TA, "_landscape_info"): lambda w: (-100.0, 300.0),
    (TA, "_bridge_state"): lambda: {"editor_mode_active": False, "subsystem_edit_mode_active": False,
                                  "subsystem_idle": True},
    (TA, "_write_json"): lambda path, data: written.__setitem__(path, data),
}
saved = {k: getattr(*k) for k in patches}
for (mod, attr), fn in patches.items():
    setattr(mod, attr, fn)
try:
    pres = dict(presets["presets"]["woodland_hill"], k_peak=0.949, k_source="test")
    r = TA.feature_plan(name="Ridge", preset=pres, points=[[0.0, 0.0], [300000.0, 0.0]])
    check("feature_plan ok", r.get("ok") and r["beads"] >= 10 and r["mode"] == "ridge")
    check("feature_plan priority above covering, own chain excluded", r["priority"] == 7)
    plan = next(iter(written.values()))
    b0 = plan["beads"][len(plan["beads"]) // 2]
    check("feature_plan bead args", b0["args"]["blend_method"] == "Max" and b0["args"]["scale"][:2] == [600.0, 600.0]
          and b0["args"]["yaw"] in (0.0, 90.0, 180.0, 270.0) and b0["args"]["offset_method"] == "BrushZLocation")
    check("feature_plan crest = ref + height x w", abs(b0["plan"]["crest_z"] - (b0["ground"]["ref"] + 3000.0 * b0["w"])) < 0.1)
    check("feature_plan base below bead ground", b0["args"]["location_z"] <= b0["ground"]["min"])
    check("feature_plan labels", all(b["label"].startswith("PRK_Ridge_") for b in plan["beads"]))
    check("feature_plan bad name refused", not TA.feature_plan(name="bad name", preset=pres, points=[[0, 0]])["ok"])
    check("feature_plan needs k", not TA.feature_plan(name="R", preset=dict(pres, k_peak=None), points=[[0, 0]])["ok"])
    written.clear()
    vpres = dict(presets["presets"]["mountain_pass"], k_peak=0.496, k_source="test")
    r = TA.feature_plan(name="Pass", preset=vpres, points=[[0.0, 0.0], [300000.0, 0.0]])
    plan = next(iter(written.values()))
    floors = [b["floor_cm"] for b in plan["beads"]]
    check("valley grade floor", r.get("ok") and all(f <= 1000.0 + 1e-6 for f in floors))
    bump = min(plan["beads"], key=lambda b: abs(b["x"] - 150000.0))
    check("valley cuts >= height below local ground", bump["floor_cm"] <= bump["ground"]["ref"] - 2000.0 + 0.1)
    check("valley rim above bead ground", all(b["args"]["location_z"] >= b["ground"]["max"] for b in plan["beads"]))
    check("valley invert / Min", all(b["args"]["invert"] and b["args"]["blend_method"] == "Min" for b in plan["beads"]))

    # A narrow Max levee covers a rounded, rotated corner, but not the bead centre. The planner and
    # guarded spawn must probe exactly the same footprint, and an illegal explicit priority must not
    # produce an applyable chain that only fails after its first actor has already been written.
    corner_xy = [1234.56, 2345.67]
    corner_yaw = G.bead_yaw("CornerChannel", 0)
    corner_fp = g.Footprint(round(corner_xy[0], 1), round(corner_xy[1], 1), corner_yaw, 32000, 32000)
    narrow_corner = corner_fp.to_world(-corner_fp.half_u, -corner_fp.half_v)
    corner_probe_calls = []
    def corner_covering(world, points):
        corner_probe_calls.append(points)
        has_corner = any(abs(p[0] - narrow_corner[0]) < 1e-6 and abs(p[1] - narrow_corner[1]) < 1e-6
                         for p in points)
        return [{"label": "Base", "priority": 71, "blend_method": "MIN"}] + (
            [{"label": "NarrowLevee", "priority": 90, "blend_method": "MAX"}] if has_corner else [])
    with mock.patch.object(L, "_brushes_covering", side_effect=corner_covering):
        written.clear()
        corner_plan = TA.feature_plan(name="CornerChannel", preset=vpres, points=[corner_xy],
                                      width_m=320, height_m=30, taper_m=0, priority=72)
        corner_data = next(iter(written.values()))
        corner_args = corner_data["beads"][0]["args"]
        check("planner probes exact guarded spawn centre and rotated corners",
              corner_plan.get("ok") and corner_probe_calls[0] == L._priority_probe_points(corner_fp))
        check("off-centre Max levee raises illegal explicit channel priority before apply",
              corner_plan.get("priority") == 91 and corner_plan.get("requested_priority") == 72
              and corner_plan.get("priority_adjusted") and corner_args["priority"] == 91
              and corner_data["inputs"]["priority"] == 72)
        guarded = L._dig_priority_check(FakeWorld(), {"location": [corner_args["x"], corner_args["y"], 0],
                    "scale": corner_args["scale"], "yaw": corner_args["yaw"], "priority": corner_args["priority"],
                    "heightmap": {"blend_method": "MIN", "invert": True, "multiplier": corner_args["multiplier"]}})
        check("planned off-centre channel satisfies actual dig guard", not guarded["conflicts"]
              and any(c["label"] == "NarrowLevee" for c in guarded["covering"]))
        written.clear()
        explicit_safe = TA.feature_plan(name="CornerChannel", preset=vpres, points=[corner_xy],
                                        width_m=320, height_m=30, taper_m=0, priority=101)
        check("planner preserves legal explicit channel priority", explicit_safe.get("priority") == 101
              and explicit_safe.get("priority_adjusted") is False)
    with mock.patch.object(L, "_brushes_covering", return_value=[
            {"label": "LaterMin", "priority": 100, "blend_method": "MIN"}]):
        legal_below_cap = TA.feature_plan(name="LegalBelowMin", preset=vpres, points=[corner_xy],
                                          width_m=320, height_m=30, taper_m=0, priority=72)
        check("legal explicit priority below same-kind Min is preserved", legal_below_cap.get("priority") == 72
              and legal_below_cap.get("priority_adjusted") is False)
    with mock.patch.object(L, "_brushes_covering", return_value=[
            {"label": "NarrowLevee", "priority": 90, "blend_method": "MAX"},
            {"label": "Lift", "priority": 91, "blend_method": "ONLY_ADD"},
            {"label": "Lift2", "priority": 92, "blend_method": "ADD_AND_SUBTRACT"}]):
        avoid_lift_tie = TA.feature_plan(name="AvoidLiftTie", preset=vpres, points=[corner_xy],
                                        width_m=320, height_m=30, taper_m=0, priority=72)
        check("priority adjustment also avoids existing equal-priority ordering guard",
              avoid_lift_tie.get("priority") == 93)

    # An absolute rising route over 100 m ground must not receive another 30 m cut or a descending clamp.
    class RaisedFlatTracer:
        def __init__(self, world): self.count = 0
        def z(self, x, y):
            self.count += 1
            return 10000.0
    L._LandscapeTracer = RaisedFlatTracer
    try:
        written.clear()
        r = TA.feature_plan(name="ExactGrade", preset=vpres, points=[[0.0, 0.0], [200000.0, 0.0]],
                            width_m=400, spacing_frac=0.25, height_m=30, taper_m=200,
                            descending=True, floor_z_m=[85, 95])
        plan = next(iter(written.values()))
        grade_floors = {b["x"]: b["plan"]["floor_z"] for b in plan["beads"]}
        check("explicit grade overrides relative depth, taper and descending shaping",
              r.get("ok") and grade_floors[10000.0] == 8550.0 and grade_floors[100000.0] == 9000.0
              and grade_floors[190000.0] == 9450.0)
        written.clear()
        r = TA.feature_plan(name="ExactGradeLight", preset=vpres, points=[[0.0, 0.0], [200000.0, 0.0]],
                            width_m=400, spacing_frac=0.25, height_m=3, taper_m=400, floor_z_m=[85, 95])
        plan = next(iter(written.values()))
        check("explicit route profile remains fixed when relative depth and taper change",
              r.get("ok") and all(b["plan"]["floor_z"] == grade_floors[b["x"]] for b in plan["beads"]))
        written.clear()
        explicit_swale = dict(presets["presets"]["dry_swale"], k_peak=0.949, k_source="test")
        r = TA.feature_plan(name="ExactSwale", preset=explicit_swale, points=[[0.0, 0.0], [200000.0, 0.0]],
                            width_m=400, spacing_frac=0.25, taper_m=200, floor_z_m=[85, 95])
        plan = next(iter(written.values()))
        check("explicit floor activates absolute grade on a relative descending preset",
              r.get("ok") and plan["grade"]["source"] == "floor_z_m"
              and all(b["plan"]["floor_z"] == grade_floors[b["x"]] for b in plan["beads"]))

        # Reusing a name for a no-cut plan must replace the old on-disk plan, not apply its old digging beads.
        written.clear()
        r = TA.feature_plan(name="NoCutReuse", preset=vpres, points=[[0.0, 0.0], [200000.0, 0.0]],
                            width_m=400, taper_m=0, floor_z_m=[85, 85])
        old_plan = written[TA._plan_path("NoCutReuse")]
        check("no-cut regression starts with useful old plan", r.get("ok") and bool(old_plan["beads"]))
        r = TA.feature_plan(name="NoCutReuse", preset=vpres, points=[[0.0, 0.0], [200000.0, 0.0]],
                            width_m=400, taper_m=0, floor_z_m=[105, 105])
        newest = written[TA._plan_path("NoCutReuse")]
        check("same-name no-cut replan replaces old plan", r.get("ok") and r.get("no_cut_needed")
              and r.get("nothing_to_apply") and not newest["beads"] and newest is not old_plan)
        with mock.patch.object(TA, "_read_json", side_effect=lambda p: written.get(p)), \
             mock.patch.object(L, "stamp_spawn") as noop_spawn, \
             mock.patch.object(L, "stamp_delete") as noop_delete, \
             mock.patch.object(TA, "_chain_actors") as noop_chain:
            applied = TA.feature_apply(name="NoCutReuse", dry_run=False)
            check("ordinary empty latest plan applies as no-op", applied.get("ok")
                  and applied.get("nothing_to_apply") and applied.get("no_cut_needed")
                  and applied["total"] == 0 and applied["spawned"] == [] and applied["deleted"] == []
                  and not noop_spawn.called and not noop_delete.called and not noop_chain.called)
            host = H.feature_apply(dispatch=lambda *a, **kw: applied, name="NoCutReuse",
                                   dry_run=False, expect_world="/Game/W.W", sleep=lambda s: None)
            check("host preserves no-op semantics and row arrays", host.get("ok")
                  and host.get("nothing_to_apply") and host.get("no_cut_needed")
                  and host["spawned"] == [] and host["total"] == 0 and len(host["calls"]) == 1)

        # Explicit replacement must remove an obsolete lower cut even when the higher-floor plan is empty.
        old_label = old_plan["beads"][0]["label"]
        old_pkg = "/Game/__ExternalActors__/OldNoCutBead"
        old_actor = mock.MagicMock(**{"get_package.return_value.get_name.return_value": old_pkg})
        old_journal = {"name": "NoCutReuse", "spawned": [{"label": old_label, "package": old_pkg}], "deleted": []}
        with mock.patch.object(TA, "_read_json", side_effect=lambda p: written.get(p)), \
             mock.patch.object(TA, "_load_applied", return_value=old_journal), \
             mock.patch.object(TA, "_chain_actors", return_value={old_label: old_actor}), \
             mock.patch.object(L, "_tagged", return_value=True), \
             mock.patch.object(TA._el, "_stamp_component", return_value=object()), \
             mock.patch.object(L, "_stamp_snapshot", return_value=render_snap), \
             mock.patch.object(L, "_dirty_landscape_state", return_value={}), \
             mock.patch.object(L, "stamp_spawn") as empty_spawn, \
             mock.patch.object(L, "stamp_delete", return_value={"ok": True}) as empty_delete:
            dry_replace = TA.feature_apply(name="NoCutReuse", replace=True, dry_run=True)
            check("empty replacement dry-run previews deletion without journal writes", dry_replace.get("ok")
                  and not dry_replace.get("nothing_to_apply") and len(dry_replace["deleted"]) == 1
                  and empty_delete.call_args.kwargs["dry_run"] is True
                  and TA._applied_path("NoCutReuse") not in written and old_journal["spawned"])
            empty_delete.reset_mock()
            retired = TA.feature_apply(name="NoCutReuse", replace=True, dry_run=False)
            deletion_journal = written[TA._applied_path("NoCutReuse")]
            check("valid empty replacement retires owned beads and journals package plus box", retired.get("ok")
                  and retired["total"] == 0 and not retired.get("nothing_to_apply") and not empty_spawn.called
                  and empty_delete.call_args.kwargs == {"actor": old_label, "dry_run": False,
                                                       "expect_world": TA.DEFAULT_WORLD}
                  and not deletion_journal["spawned"] and deletion_journal["deleted"][0]["package"] == old_pkg
                  and deletion_journal["deleted"][0]["box"])
            with mock.patch.object(TA._el, "_dirty_package_names", return_value={old_pkg}), \
                 mock.patch.object(TA, "_package_path", return_value=None), \
                 mock.patch.object(L, "_all", return_value=[]):
                deletion_scope = TA.save_scope(feature="NoCutReuse")
                check("empty replacement keeps deleted actor package in scoped save", deletion_scope.get("ok")
                      and deletion_scope["owned_packages"] == [old_pkg]
                      and deletion_scope["rows"][0]["kind"] == "feature_deleted")

        for state, label in [({"editor_mode_active": True, "subsystem_idle": True}, "active mode"),
                             ({"editor_mode_active": False, "subsystem_idle": False}, "busy subsystem")]:
            with mock.patch.object(TA, "_bridge_state", return_value=state), \
                 mock.patch.object(TA, "_bead_ground") as no_trace:
                refused = TA.feature_plan(name="NoCutReuse", preset=vpres, points=[[0.0, 0.0]],
                                          floor_z_m=[85])
                check("feature planning refuses " + label + " before trace or plan rewrite",
                      not refused.get("ok") and refused.get("refused") and not no_trace.called
                      and written[TA._plan_path("NoCutReuse")] is newest)

        class MissingTracer:
            def __init__(self, world): self.count = 0
            def z(self, x, y):
                self.count += 1
                return None
        with mock.patch.object(L, "_LandscapeTracer", MissingTracer):
            missing = TA.feature_plan(name="NoCutReuse", preset=vpres, points=[[0.0, 0.0]], floor_z_m=[85])
        with mock.patch.object(TA, "_read_json", side_effect=lambda p: written.get(p)), \
             mock.patch.object(L, "stamp_spawn") as missing_spawn, \
             mock.patch.object(L, "stamp_delete") as missing_delete, \
             mock.patch.object(TA, "_chain_actors") as missing_lookup:
            unapplied = TA.feature_apply(name="NoCutReuse", replace=True, dry_run=False)
            check("all-missing ground refuses and invalidates older plan", not missing.get("ok")
                  and written[TA._plan_path("NoCutReuse")]["status"] == "unavailable_ground"
                  and not unapplied.get("ok") and not missing_spawn.called
                  and not missing_delete.called and not missing_lookup.called)
    finally:
        L._LandscapeTracer = FlatTracer

    written.clear()
    spres = dict(presets["presets"]["dry_swale"], k_peak=0.949, k_source="test")
    r = TA.feature_plan(name="Swale", preset=spres, points=[[100000.0, 0.0], [200000.0, 0.0]], height_m=[4, 4])
    plan = next(iter(written.values()))
    fl = [b["floor_cm"] for b in plan["beads"]]
    check("descending swale never rises", r.get("ok") and all(b <= a + 1e-6 for a, b in zip(fl, fl[1:])))

    # stamp_relief -> stamp_configure args
    cfg_calls = []
    src = {"actor": "Hill", "texture": "/Game/X/T.T", "blend_method": "MAX", "invert": False, "multiplier": 0.5,
           "scale": [600.0, 600.0, 0.1], "location": [0.0, 0.0, 900.0], "offset_method": "BRUSH_Z_LOCATION",
           "z_offset": 0.0, "landscape_z": -100.0, "unit_cm": U, "edit_mode_active": True}
    saved2 = {k: getattr(*k) for k in ((L, "_resolve_stamp"), (TA, "_source"), (L, "stamp_configure"),
                                       (TA, "_calibration_entry"))}
    L._resolve_stamp = lambda w, a: (mock.MagicMock(**{"get_actor_label.return_value": "Hill"}), object(), None)
    TA._source = lambda w, a, c: dict(src)
    TA._calibration_entry = lambda t: None
    L.stamp_configure = lambda **kw: (cfg_calls.append(kw), {"ok": True, "dry_run": kw["dry_run"]})[1]
    try:
        rr = TA.stamp_relief(actor="Hill", relief_factor=1.5)
        check("relief factor -> configure scale", rr["ok"] and abs(cfg_calls[-1]["scale"] - 0.15) < 1e-12
              and cfg_calls[-1]["dry_run"] is True and "location_z" not in cfg_calls[-1])
        rr = TA.stamp_relief(actor="Hill", lift_m=5, relief_delta_m=10, k_peak=K)
        check("relief delta + lift -> configure", rr["ok"] and cfg_calls[-1]["location_z"] == 400.0
              and cfg_calls[-1]["dry_run"] is True)
        rr = TA.stamp_relief(actor="Hill", crest_above_ground_m=10, k_peak=K)
        check("terrain target refused in edit mode", not rr["ok"] and rr.get("refused"))
        rr = TA.stamp_relief(actor="Hill", relief_delta_m=10)
        check("k-dependent target without k refused", not rr["ok"] and len(cfg_calls) == 2)
    finally:
        for (mod, attr), fn in saved2.items():
            setattr(mod, attr, fn)

    # feature_apply: spawn through stamp_spawn, resume, refuse a different existing bead
    fplan = {"beads": [{"label": f"PRK_Ridge_{i:02d}", "i": i, "args": dict(args, label=f"PRK_Ridge_{i:02d}", x=100.0 * i)}
                       for i in range(3)]}
    world_beads, spawn_calls, journal_writes = {}, [], []
    class FakeActor:
        def __init__(self, label): self.label = label
        def get_package(self): return mock.MagicMock(**{"get_name.return_value": f"/Game/__ExternalActors__/{self.label}"})
        def get_class(self): return mock.MagicMock(**{"get_path_name.return_value": "/Game/Test/Old.Old_C"})
    saved3 = {k: getattr(*k) for k in ((TA, "_load_plan"), (TA, "_chain_actors"), (L, "stamp_spawn"), (L, "_tagged"),
                                       (TA, "_load_applied"), (L, "_stamp_snapshot"), (TA._el, "_stamp_component"),
                                       (L, "_dirty_landscape_state"), (TA, "_applied_path"))}
    TA._load_plan = lambda n: (fplan, None)
    TA._chain_actors = lambda w, p: {k: v for k, v in world_beads.items() if k.startswith(p)}
    TA._load_applied = lambda n: {"name": n, "spawned": [], "deleted": []}
    TA._applied_path = lambda n: "applied.json"
    TA._el._stamp_component = lambda a: object()
    L._tagged = lambda a: True
    L._dirty_landscape_state = lambda w: {}
    def fake_spawn(**kw):
        spawn_calls.append(kw)
        if not kw["dry_run"]:
            world_beads[kw["label"]] = FakeActor(kw["label"])
        return {"ok": True, "dry_run": kw["dry_run"], "actor_name": kw["label"] + "_N"}
    L.stamp_spawn = fake_spawn
    written.clear()
    try:
        d = TA.feature_apply(name="Ridge", dry_run=True)
        check("apply dry run spawns dry, writes nothing", d["ok"] and len(spawn_calls) == 3
              and all(c["dry_run"] for c in spawn_calls) and not written)
        a1 = TA.feature_apply(name="Ridge", start=0, max_beads=2, dry_run=False)
        check("apply real batch", a1["ok"] and a1["next_start"] == 2 and set(world_beads) == {"PRK_Ridge_00", "PRK_Ridge_01"}
              and written.get("applied.json", {}).get("spawned", [{}])[-1].get("package", "").endswith("PRK_Ridge_01"))

        # Real partial application: 00 exists, 01 is refused, 02 is never attempted. The refused
        # attempt stays in failed/calls, not spawned or the applied journal / scoped-save ownership.
        world_beads.clear()
        spawn_calls.clear()
        written.clear()
        def refuse_second_spawn(**kw):
            if kw["label"] == "PRK_Ridge_01":
                spawn_calls.append(kw)
                return {"ok": False, "refused": True, "error": "dig_priority", "failed_guards": ["dig_priority"]}
            return fake_spawn(**kw)
        L.stamp_spawn = refuse_second_spawn
        partial = H.feature_apply(dispatch=lambda module, fn, **kw: getattr(TA, fn)(**kw), name="Ridge",
                                  dry_run=False, beads_per_call=3, expect_world="/Game/W", sleep=lambda s: None)
        partial_journal = written["applied.json"]
        check("partial aggregate spawned contains only successful actor writes",
              not partial["ok"] and [r["label"] for r in partial["spawned"]] == ["PRK_Ridge_00"]
              and partial["failed"]["label"] == "PRK_Ridge_01" and partial["calls"][0]["ok"] is False
              and [c["label"] for c in spawn_calls] == ["PRK_Ridge_00", "PRK_Ridge_01"])
        check("refused spawn never enters applied journal", set(world_beads) == {"PRK_Ridge_00"}
              and [r["label"] for r in partial_journal["spawned"]] == ["PRK_Ridge_00"])
        success_pkg = "/Game/__ExternalActors__/PRK_Ridge_00"
        refused_pkg = "/Game/__ExternalActors__/PRK_Ridge_01"
        with mock.patch.object(TA, "_read_json", return_value=partial_journal), \
             mock.patch.object(TA._el, "_dirty_package_names", return_value={success_pkg, refused_pkg}), \
             mock.patch.object(TA, "_package_path", return_value="/nonexistent/refused_spawn.uasset"), \
             mock.patch.object(L, "_all", return_value=[]):
            scope, refusal = TA._scope(FakeWorld(), None, "Ridge", None, None)
            check("partial feature scope owns only successful spawn packages", refusal is None
                  and scope["owned"] == {success_pkg} and scope["foreign"] == [refused_pkg])
        world_beads.clear()
        spawn_calls.clear()
        written.clear()
        dry_partial = TA.feature_apply(name="Ridge", dry_run=True)
        check("dry-run refusal stays out of spawned and creates no journal", not dry_partial["ok"]
              and [r["label"] for r in dry_partial["spawned"]] == ["PRK_Ridge_00", "PRK_Ridge_02"]
              and dry_partial["failures"] and not written)
        L.stamp_spawn = fake_spawn
        spawn_calls.clear()
        TA.feature_apply(name="Ridge", start=0, max_beads=2, dry_run=False)
        L._stamp_snapshot = lambda a, c: {"location": [100.0 * int(a.label[-2:]), 200.0, 50.0], "scale": [600, 600, 0.2],
                                          "yaw": -90.0, "priority": 9,
                                          "heightmap": {"invert": False, "multiplier": 0.5, "blend_method": "MAX"}}
        spawn_calls.clear()
        a2 = TA.feature_apply(name="Ridge", start=0, dry_run=False)
        check("apply resumes matching beads", a2["ok"] and a2["skipped_existing"] == ["PRK_Ridge_00", "PRK_Ridge_01"]
              and [c["label"] for c in spawn_calls] == ["PRK_Ridge_02"])
        spawn_calls.clear()
        fplan["beads"][0]["args"]["falloff_start"] = 0.4
        changed_falloff = TA.feature_apply(name="Ridge", dry_run=False)
        check("apply refuses same-label stale falloff rather than resuming", not changed_falloff.get("ok")
              and changed_falloff["conflicts"][0]["differs"] == ["falloff_start"] and not spawn_calls)
        del fplan["beads"][0]["args"]["falloff_start"]
        fplan["beads"][0]["args"]["stamp_class"] = "/Game/Test/Old"
        same_class_alias = TA.feature_apply(name="Ridge", dry_run=False)
        check("apply normalizes blueprint class aliases before resume", same_class_alias.get("ok")
              and len(same_class_alias["skipped_existing"]) == 3 and not spawn_calls)
        fplan["beads"][0]["args"]["stamp_class"] = "/Game/Test/New.New_C"
        changed_class = TA.feature_apply(name="Ridge", dry_run=False)
        check("apply refuses same-label stale class rather than resuming", not changed_class.get("ok")
              and changed_class["conflicts"][0]["differs"] == ["stamp_class"] and not spawn_calls)
        del fplan["beads"][0]["args"]["stamp_class"]
        L._stamp_snapshot = lambda a, c: {"location": [0.0, 0.0, 0.0], "scale": [600, 600, 0.2], "yaw": 0.0,
                                          "priority": 1, "heightmap": {"invert": False, "multiplier": 0.5,
                                                                       "blend_method": "MAX"}}
        a3 = TA.feature_apply(name="Ridge", dry_run=False)
        check("apply refuses different existing beads", not a3["ok"] and a3.get("conflicts"))
    finally:
        for (mod, attr), fn in saved3.items():
            setattr(mod, attr, fn)

    # Direct configure ownership survives movement/shrink and editor-module reloads. A partial
    # save keeps old footprints pending; a complete scoped save retires only the saved actor's
    # current revision, preserving unrelated actors' history and foreign dirty packages.
    class BoundsActor:
        def __init__(self, label, name, package):
            self.label, self.name, self.package = label, name, package
        def get_actor_label(self): return self.label
        def get_name(self): return self.name
        def get_package(self): return mock.MagicMock(**{"get_name.return_value": self.package})
    bounds_actor = BoundsActor("LargePlain", "Plain_UAID_original", "/Game/__ExternalActors__/LargePlain")
    other_bounds_actor = BoundsActor("OtherPlain", "Plain_UAID_other", "/Game/__ExternalActors__/OtherPlain")
    old_snapshot = {"location": [0, 0, 0], "scale": [11000, 11000, .1]}
    new_snapshot = {"location": [700000, 0, 0], "scale": [2800, 2800, .1]}
    other_snapshot = {"location": [1600000, 0, 0], "scale": [1000, 1000, .1]}
    old_proxy = BoundsActor("OldProxy", "OldProxy_N", "/Game/__ExternalActors__/OldProxy")
    new_proxy = BoundsActor("NewProxy", "NewProxy_N", "/Game/__ExternalActors__/NewProxy")
    foreign_proxy = BoundsActor("ForeignProxy", "ForeignProxy_N", "/Game/__ExternalActors__/ForeignProxy")
    bounds_proxies = [old_proxy, new_proxy, foreign_proxy]
    proxy_boxes = [("OldProxy", ((-500000, -50000), (-400000, 50000))),
                   ("NewProxy", ((600000, -50000), (700000, 50000))),
                   ("ForeignProxy", ((1550000, -50000), (1650000, 50000)))]
    history_dirty = {a.package for a in [bounds_actor, other_bounds_actor, *bounds_proxies]} | {"/Game/Foreign/Biome"}
    saved_history_packages = []
    def save_history_packages(objs, only_dirty):
        packages = {p.get_name() for p in objs}
        saved_history_packages.extend(sorted(packages))
        history_dirty.difference_update(packages)
        return True
    with tempfile.TemporaryDirectory(prefix="prk_stamp_bounds_") as history_temp, \
         mock.patch.object(L, "_stamp_bounds_history_path", return_value=_os.path.join(history_temp, "bounds.json")), \
         mock.patch.object(L, "_resolve_stamp", return_value=(bounds_actor, object(), None)), \
         mock.patch.object(L, "_stamp_snapshot", return_value=new_snapshot), \
         mock.patch.object(L, "_all", return_value=bounds_proxies), \
         mock.patch.object(L, "_predict_proxies", side_effect=lambda w, boxes: g.predict_update_proxies(boxes, proxy_boxes, [], 0)), \
         mock.patch.object(TA._el, "_dirty_package_names", side_effect=lambda: set(history_dirty)), \
         mock.patch.object(TA, "_package_path", return_value=_os.path.join(history_temp, "missing.uasset")), \
         mock.patch.object(TA.unreal.EditorLoadingAndSavingUtils, "get_dirty_map_packages", return_value=[]), \
         mock.patch.object(TA.unreal.EditorLoadingAndSavingUtils, "get_dirty_content_packages",
                           side_effect=lambda: [mock.MagicMock(**{"get_name.return_value": p}) for p in history_dirty]), \
         mock.patch.object(TA.unreal.EditorLoadingAndSavingUtils, "save_packages", side_effect=save_history_packages):
        rec = L._record_stamp_bounds(FakeWorld(), bounds_actor, old_snapshot, new_snapshot)
        other_rec = L._record_stamp_bounds(FakeWorld(), other_bounds_actor, other_snapshot, other_snapshot)
        durable = L._load_stamp_bounds_history(FakeWorld())
        check("direct bounds history durably stores unrotated old and moved/shrunken new boxes",
              rec["ok"] and other_rec["ok"] and rec["boxes"] == 2
              and durable["actors"][bounds_actor.package]["boxes"] == [
                  [[-550000.0, -550000.0], [550000.0, 550000.0]], [[560000.0, -140000.0], [840000.0, 140000.0]]])
        check("history requires exact actor identity as well as package",
              L._stamp_bounds_history_entry(durable, bounds_actor.package, "ReplacementActor") is None)
        bounds_scope = TA.save_scope(stamps=["LargePlain"])
        expected_bounds_owned = {bounds_actor.package, old_proxy.package, new_proxy.package}
        expected_bounds_foreign = {other_bounds_actor.package, foreign_proxy.package, "/Game/Foreign/Biome"}
        check("moved/shrunken direct stamp scope includes dirty old-only and new-only proxies",
              bounds_scope["ok"] and set(bounds_scope["owned_packages"]) == expected_bounds_owned
              and set(bounds_scope["predicted_proxies"]) == {"OldProxy", "NewProxy"})
        check("unnamed actor history never broadens named stamp ownership",
              set(bounds_scope["foreign_dirty"]) == expected_bounds_foreign
              and len(bounds_scope["pending_stamp_bounds"]) == 1)
        feature_journal = {"spawned": [{"label": bounds_actor.label, "actor_name": bounds_actor.name,
                                        "package": bounds_actor.package,
                                        "box": g.brush_box(0, 0, 11000, 11000)}], "deleted": []}
        with mock.patch.object(TA, "_read_json", return_value=feature_journal):
            feature_scope = TA.save_scope(feature="MovedFeature")
            check("feature journal joins successful direct bead edits' pending footprints",
                  feature_scope["ok"] and set(feature_scope["owned_packages"]) == expected_bounds_owned)
        partial_save = TA.save_packages_scoped(packages=[bounds_actor.package], stamps=["LargePlain"])
        check("partial save retains pending old footprint until affected proxies are clean",
              partial_save["ok"] and not partial_save["bounds_history"]["retired"]
              and partial_save["bounds_history"]["pending_unsaved"] == [bounds_actor.package]
              and bounds_actor.package in L._load_stamp_bounds_history(FakeWorld())["actors"])
        sc_internal, _ = TA._scope(FakeWorld(), ["LargePlain"], None, None, None)
        revision_token = sc_internal["pending_bounds"][0]
        L._record_stamp_bounds(FakeWorld(), bounds_actor, new_snapshot, new_snapshot)
        retired_stale = L._retire_stamp_bounds(FakeWorld(), [revision_token])
        check("history retirement cannot clear a newer edit revision", retired_stale["ok"]
              and retired_stale["newer_edits_retained"] == [bounds_actor.package]
              and bounds_actor.package in L._load_stamp_bounds_history(FakeWorld())["actors"])
        complete_save = TA.save_packages_scoped(packages=sorted(expected_bounds_owned), stamps=["LargePlain"])
        check("complete scoped save writes old/new proxies and retires only fully saved bounds history",
              complete_save["ok"] and complete_save["bounds_history"]["retired"] == [bounds_actor.package]
              and set(saved_history_packages) == expected_bounds_owned
              and set(L._load_stamp_bounds_history(FakeWorld())["actors"]) == {other_bounds_actor.package}
              and history_dirty == expected_bounds_foreign)
        history_dirty.add(old_proxy.package)
        next_scope = TA.save_scope(stamps=["LargePlain"])
        check("saved old footprint cannot own future unrelated dirty proxies", next_scope["ok"]
              and old_proxy.package in next_scope["foreign_dirty"]
              and old_proxy.package not in next_scope["owned_packages"])
        # Existing foreign-dirty protection still runs before history retirement.
        L._record_stamp_bounds(FakeWorld(), bounds_actor, old_snapshot, new_snapshot)
        history_dirty.update(expected_bounds_owned)
        def bad_save_history(objs, only_dirty):
            save_history_packages(objs, only_dirty)
            history_dirty.discard("/Game/Foreign/Biome")
            return True
        with mock.patch.object(TA.unreal.EditorLoadingAndSavingUtils, "save_packages", side_effect=bad_save_history):
            foreign_failure = TA.save_packages_scoped(packages=sorted(expected_bounds_owned), stamps=["LargePlain"])
        check("foreign dirty violation prevents history retirement", not foreign_failure["ok"]
              and foreign_failure["foreign_cleaned"] == ["/Game/Foreign/Biome"]
              and bounds_actor.package in L._load_stamp_bounds_history(FakeWorld())["actors"])

    # Explicit settlement placeholders can be saved with terrain, without broadening map ownership.
    marker_pkg = "/Game/__ExternalActors__/PRK/World/ErrantWorld/ErrantWorld/Marker"
    foreign_pkg = "/Game/Foreign/UnsavedBiome"
    world_pkg = "/Game/PRK/World/ErrantWorld/ErrantWorld"
    class FakeScopedActor:
        def __init__(self, label, package):
            self.label, self.package = label, package
        def get_actor_label(self): return self.label
        def get_package(self): return mock.MagicMock(**{"get_name.return_value": self.package})
    marker = FakeScopedActor("NorthShorePlaceholder", marker_pkg)
    map_marker = FakeScopedActor("NonExternalMarker", world_pkg)
    marker_class, brush_class, proxy_class = object(), object(), object()
    dirty = {marker_pkg, foreign_pkg, world_pkg}
    saved_packages = []
    def save_fake_packages(objs, only_dirty):
        names = {p.get_name() for p in objs}
        saved_packages.extend(sorted(names))
        dirty.difference_update(names)
        return True
    with mock.patch.object(TA.unreal, "PRKSettlementMarker", marker_class), \
         mock.patch.object(TA.unreal, "ElBrushActor", brush_class), \
         mock.patch.object(TA.unreal, "LandscapeStreamingProxy", proxy_class), \
         mock.patch.object(L, "_all", side_effect=lambda w, cls: [marker, map_marker] if cls is marker_class else []), \
         mock.patch.object(TA._el, "_dirty_package_names", side_effect=lambda: set(dirty)), \
         mock.patch.object(TA, "_package_path", side_effect=lambda p: "/nonexistent/scoped_marker.uasset"), \
         mock.patch.object(TA.unreal.EditorLoadingAndSavingUtils, "get_dirty_map_packages", return_value=[]), \
         mock.patch.object(TA.unreal.EditorLoadingAndSavingUtils, "get_dirty_content_packages",
                           side_effect=lambda: [mock.MagicMock(**{"get_name.return_value": p}) for p in dirty]), \
         mock.patch.object(TA.unreal.EditorLoadingAndSavingUtils, "save_packages", side_effect=save_fake_packages):
        sc = TA.save_scope(extra_packages=[marker_pkg])
        check("scope accepts explicitly named external settlement marker", sc["ok"]
              and sc["owned_packages"] == [marker_pkg] and sc["rows"][0]["kind"] == "settlement_marker_extra")
        check("scope preserves foreign map and biome ownership", set(sc["foreign_dirty"]) == {foreign_pkg, world_pkg})
        check("scope refuses marker in shared world package", not TA.save_scope(extra_packages=[world_pkg])["ok"])
        check("scope refuses unrelated package", not TA.save_scope(extra_packages=[foreign_pkg])["ok"])
        ss = TA.save_packages_scoped(packages=[marker_pkg], extra_packages=[marker_pkg])
        check("scoped marker save writes only marker", ss["ok"] and saved_packages == [marker_pkg]
              and dirty == {foreign_pkg, world_pkg} and ss["foreign_dirty_count"] == 2)
finally:
    for (mod, attr), fn in saved.items():
        setattr(mod, attr, fn)

print("\n%d failure(s)" % len(fails))
_sys.exit(1 if fails else 0)
