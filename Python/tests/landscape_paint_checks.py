import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
import json, os, sys, tempfile
from unittest import mock
import numpy as np
from PIL import Image
sys.path.insert(0, _CONTENT_PY)
import prk_terrain_geom as g
import prk_landscape_paint as P
fails = []
def check(n, c):
    print(("PASS " if c else "FAIL ") + n)
    if not c: fails.append(n)

# --- pure ---------------------------------------------------------------------------------------------------------
check("pixel <-> world", g.paint_pixel_to_world(0) == -1625600 and g.paint_pixel_to_world(5264) == 480000
      and g.paint_world_to_pixel(480000) == 5264)
check("proxy = pixel // 508 (fen 10_7 at px 5264, 3801)", g.paint_rect_proxies(5264, 3801, 5264, 3801)
      == ["LandscapeStreamingProxy_10_7_0"])
check("rect on a component edge picks up the neighbour", g.paint_rect_components(508, 600, 600, 600) == [(1, 2), (2, 2)]
      and g.paint_rect_proxies(508, 600, 600, 600) == ["LandscapeStreamingProxy_0_1_0", "LandscapeStreamingProxy_1_1_0"])
check("rect at the landscape origin clamps", g.paint_rect_components(0, 0, 10, 10) == [(0, 0)])
a = np.zeros((50, 60), np.uint8); b = a.copy(); b[10:20, 30:41] = 5
check("changed bbox", g.paint_changed_bbox(a, b) == (30, 10, 40, 19) and g.paint_changed_bbox(a, a) is None)
check("union bbox pad/clip", g.paint_union_bbox([(10, 10, 20, 20), None, (5, 30, 8, 40)], pad=2) == (3, 8, 22, 42))
s = g.paint_sum_check([np.full((4, 4), 200, np.uint8), np.full((4, 4), 55, np.uint8)])
check("sum 255 ok", s["ok"] and s["bad_pixels"] == 0)
s = g.paint_sum_check([np.full((4, 4), 200, np.uint8), np.full((4, 4), 56, np.uint8)])
check("sum 256 fails", not s["ok"] and s["bad_pixels"] == 16 and s["max_sum"] == 256)
pp = g.paint_probe_points((4200, 3050, 5999, 4949))
check(">= 200 probes, ring outside the window", len(pp["inside"]) + len(pp["ring"]) >= 200 and all(
    not (4200 <= x <= 5999 and 3050 <= y <= 4949) for x, y in pp["ring"]) and pp["read_rect"] == [4136, 2986, 6063, 5013])
acts = P.checkout_actions(["A", "B", "C", "D", "E"], {
    os.path.normcase(os.path.normpath("A")): {"depotFile": "//a"},
    os.path.normcase(os.path.normpath("B")): {"depotFile": "//b", "action": "edit", "change": "default"},
    os.path.normcase(os.path.normpath("C")): {"depotFile": "//c", "action": "edit", "change": "436"},
    os.path.normcase(os.path.normpath("D")): {"depotFile": "//d", "action": "add", "change": "436"}}, 436)
check("checkout actions", [x["action"] for x in acts] == ["edit", "reopen", "none", "refuse", "refuse"])

# --- fake landscape (512^2) and editor ----------------------------------------------------------------------------
R = 512
g.PAINT_RES, g.PAINT_MAX_VERTEX = R, R - 1
tmp = tempfile.mkdtemp(prefix="prk_paint_")
def png(name, arr):
    p = os.path.join(tmp, name)
    Image.fromarray(arr.astype(np.uint8)).save(p)
    return p
grass0 = np.full((R, R), 255, np.uint8)
rock0 = np.zeros((R, R), np.uint8)
grass1, rock1 = grass0.copy(), rock0.copy()
rock1[120:181, 140:201] = 200
grass1[120:181, 140:201] = 55
prev = {"Ground_Grass": png("grass0.png", grass0), "Ground_Rock": None}
new = {"Ground_Grass": png("grass1.png", grass1), "Ground_Rock": png("rock1.png", rock1)}
class FakeEditor:
    def __init__(self):
        self.live = {"Ground_Grass": grass0.copy(), "Ground_Rock": rock0.copy(), "Ground_Sand": np.zeros((R, R), np.uint8)}
        self.calls, self.saved, self.world_ok = [], [], True
        self.dirty, self.dirty_on_read = set(), False
    def __call__(self, func, **kw):
        self.calls.append(func)
        if func == "dirty_snapshot":
            return {"ok": True, "packages": sorted("/Game/X/" + d for d in self.dirty), "proxies": sorted(self.dirty)}
        if func == "read_rects" and self.dirty_on_read:   # the old GetWeightDataFast read dirtied what it read
            x1, y1, x2, y2 = kw["rect"]
            self.dirty.update(g.paint_rect_proxies(x1, y1, x2, y2))
        if func == "paint_info":
            return {"ok": True, "extent": [0, 0, R - 1, R - 1], "component_size_quads": 254,
                    "edit_layer": {"name": "Paint", "guid": "G"}, "layers": sorted(self.live),
                    "world_guard": {"ok": True} if self.world_ok else {"ok": False, "refused": True, "error": "PIE"}}
        if func == "read_rects":
            x1, y1, x2, y2 = kw["rect"]
            os.makedirs(kw["out_dir"], exist_ok=True)
            files = {}
            for k in kw["layers"]:
                files[k] = os.path.join(kw["out_dir"], f"live_{k}.bin")
                self.live[k][y1:y2 + 1, x1:x2 + 1].tofile(files[k])
            return {"ok": True, "files": files, "rect": kw["rect"]}
        if func == "write_rects":
            for w in kw["writes"]:
                x1, y1, x2, y2 = w["rect"]
                self.live[w["layer"]][y1:y2 + 1, x1:x2 + 1] = np.fromfile(w["file"], np.uint8).reshape(y2 - y1 + 1, x2 - x1 + 1)
            return {"ok": True, "proxies": ["LandscapeStreamingProxy_0_0_0"], "results": []}
        if func == "proxy_files":
            return {"ok": True, "proxies": {l: {"loaded": True, "file": os.path.join(tmp, l + ".uasset"),
                                                "writable": True, "dirty": True} for l in kw["labels"]}}
        if func == "save_proxies":
            self.saved = list(kw["labels"])
            return {"ok": True, "saved": kw["labels"]}
        raise KeyError(func)
ed = FakeEditor()
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=436, editor=ed, out_dir=os.path.join(tmp, "w1"))
check("dry run ok, plan listed", r["ok"] and r["dry_run"] and r["window"] == [140, 120, 200, 180]
      and {w["layer"] for w in r["writes"]} == {"Ground_Grass", "Ground_Rock"} and r["proxies"] == ["LandscapeStreamingProxy_0_0_0"]
      and "write_rects" not in ed.calls)
gd = {x["guard"]: x for x in r["guards"]}
check("dry run guards: sum + probe pass", gd["sum_255_in_window"]["ok"] and gd["ledger_matches_live"]["ok"]
      and gd["ledger_matches_live"]["detail"]["points"] >= 200)
check("dry run writes nothing to the landscape", (ed.live["Ground_Rock"] == 0).all())
check("prev_ledger required per layer", P.run_import(ledger=new, prev_ledger={"Ground_Grass": prev["Ground_Grass"]},
                                                      target_cl=436, editor=ed).get("refused"))
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=None, editor=ed, out_dir=os.path.join(tmp, "w0"))
check("target_cl required", not r["ok"] and "target_cl" in r["failed_guards"])
# hand paint since the last pass, just outside the window: probe catches it, no write
ed2 = FakeEditor()
ed2.live["Ground_Grass"][100:118, 100:300] = 128
ed2.live["Ground_Sand"][100:118, 100:300] = 127
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=436, editor=ed2, dry_run=False, out_dir=os.path.join(tmp, "w2"))
check("hand paint on the ring refused, nothing written", r.get("refused") and "ledger_matches_live" in r["failed_guards"]
      and "write_rects" not in ed2.calls)
# a ledger that does not sum to 255 with the live untouched layers
bad_grass = png("grass_bad.png", grass0)
r = P.run_import(ledger={"Ground_Grass": bad_grass, "Ground_Rock": new["Ground_Rock"]}, prev_ledger=prev, target_cl=436,
                 editor=FakeEditor(), out_dir=os.path.join(tmp, "w3"))
check("sum 455 in the window fails", not r["ok"] and "sum_255_in_window" in r["failed_guards"])
ed3 = FakeEditor(); ed3.world_ok = False
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=436, editor=ed3, out_dir=os.path.join(tmp, "w4"))
check("PIE / world guard fails", not r["ok"] and "world_and_pie" in r["failed_guards"])
# real run -> verify -> checkout -> save
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=436, editor=ed, dry_run=False, out_dir=os.path.join(tmp, "w5"))
plan_file = r["plan_file"]
check("real run writes the rects", r["ok"] and r["written"] and (ed.live["Ground_Rock"] == rock1).all()
      and (ed.live["Ground_Grass"] == grass1).all())
check("save refused before verify", P.save(plan_file=plan_file, editor=ed, dry_run=False).get("refused"))
v = P.verify(plan_file=plan_file, editor=ed)
check("verify ok", v["ok"] and all(x["max_diff"] == 0 for x in v["verify"]))
check("save refused before checkout", P.save(plan_file=plan_file, editor=ed, dry_run=False).get("refused"))
fst = "".join(f"... depotFile //d/{l}\n... clientFile {os.path.join(tmp, l + '.uasset')}\n\n" for l in ["LandscapeStreamingProxy_0_0_0"])
p4calls = []
def fake_p4(args, timeout=120.0):
    p4calls.append(list(args))
    return {"ok": True, "code": 0, "stdout": fst if args[0] == "fstat" else "opened", "stderr": ""}
with mock.patch.object(P, "_p4", side_effect=fake_p4):
    c = P.checkout(plan_file=plan_file, editor=ed)
    check("checkout dry run: fstat only", c["ok"] and c["dry_run"] and [a["action"] for a in c["actions"]] == ["edit"]
          and [x[0] for x in p4calls] == ["fstat"])
    c = P.checkout(plan_file=plan_file, editor=ed, dry_run=False)
    check("checkout real: p4 edit -c 436 on exactly that file", c["ok"] and p4calls[-1][:3] == ["edit", "-c", "436"]
          and len(p4calls[-1]) == 4 and all(x[0] in ("fstat", "edit") for x in p4calls))
    check("checkout refuses default CL", P.checkout(plan_file=plan_file, editor=ed, target_cl=0).get("refused"))
s = P.save(plan_file=plan_file, editor=ed, dry_run=True)
check("save dry run lists the proxies", s["ok"] and s["would_save"] == ["LandscapeStreamingProxy_0_0_0"] and not ed.saved)
s = P.save(plan_file=plan_file, editor=ed, dry_run=False)
check("save real saves exactly the dirtied proxies", s["ok"] and ed.saved == ["LandscapeStreamingProxy_0_0_0"])
r = P.run_import(ledger={"Ground_Grass": prev["Ground_Grass"]}, prev_ledger={"Ground_Grass": prev["Ground_Grass"]},
                 target_cl=436, editor=FakeEditor())
check("no change -> nothing to write", r["ok"] and r.get("nothing_to_write"))

# --- write semantics: 5.8 SetAlphaData writes ONLY its own channel (LandscapeEditInterface.cpp:2106-2375; the
# bWeightAdjust / bTotalWeightAdjust overload is deprecated since 5.7 and forwards without them). The pre-5.7
# weight-adjusting rule redistributed the other layers; simulate both. ------------------------------------------------
def legacy_write(state, layer, value):
    """Pre-5.7 bWeightAdjust: set one layer, rescale the others proportionally to fill 255 - value."""
    out = dict(state)
    others = [k for k in state if k != layer]
    rest = sum(state[k] for k in others)
    out[layer] = float(value)
    for k in others:
        out[k] = (255.0 - value) * state[k] / rest if rest > 0 else 0.0
    return out
def channel_write(state, layer, value):
    out = dict(state); out[layer] = float(value); return out
start = {"A": 100.0, "B": 155.0, "C": 0.0}
target = {"A": 50.0, "B": 155.0, "C": 50.0}
def apply(rule, order):
    s = dict(start)
    for k in order:
        s = rule(s, k, target[k])
    return s
la, lc = apply(legacy_write, ["A", "C"]), apply(legacy_write, ["C", "A"])
check("legacy redistribution: A ends ~40, not 50", abs(la["A"] - 40.0) < 0.5 and abs(la["B"] - 165.0) < 0.5)
check("legacy redistribution is order-dependent", abs(la["A"] - lc["A"]) > 1.0 or abs(la["C"] - lc["C"]) > 1.0)
check("channel-only write (5.8) exact in both orders", apply(channel_write, ["A", "C"]) == target
      and apply(channel_write, ["C", "A"]) == target)
# the same example through the pipeline: the fake editor's write is the 5.8 channel write; a legacy-rule fake fails verify
class LegacyEditor(FakeEditor):
    def __call__(self, func, **kw):
        if func != "write_rects":
            return FakeEditor.__call__(self, func, **kw)
        for w in kw["writes"]:
            x1, y1, x2, y2 = w["rect"]
            v = np.fromfile(w["file"], np.uint8).reshape(y2 - y1 + 1, x2 - x1 + 1).astype(np.float64)
            region = {k: a[y1:y2 + 1, x1:x2 + 1].astype(np.float64) for k, a in self.live.items()}
            others = [k for k in region if k != w["layer"]]
            rest = sum(region[k] for k in others)
            for k in others:
                region[k] = np.where(rest > 0, (255.0 - v) * region[k] / np.maximum(rest, 1e-9), 0.0)
            region[w["layer"]] = v
            for k, a in region.items():
                self.live[k][y1:y2 + 1, x1:x2 + 1] = np.rint(a).astype(np.uint8)
        return {"ok": True, "proxies": ["LandscapeStreamingProxy_0_0_0"], "results": []}
def three_layer(editor_cls, reverse=False):
    e = editor_cls()
    e.live = {"A": np.zeros((R, R), np.uint8), "B": np.full((R, R), 255, np.uint8), "C": np.zeros((R, R), np.uint8)}
    e.live["A"][200:260, 200:260], e.live["B"][200:260, 200:260] = 100, 155
    a1, c1 = e.live["A"].copy(), e.live["C"].copy()
    a1[200:260, 200:260], c1[200:260, 200:260] = 50, 50
    led = {"A": png("A1.png", a1), "C": png("C1.png", c1)}
    if reverse:
        led = {"C": led["C"], "A": led["A"]}
    prv = {"A": png("A0.png", e.live["A"]), "C": None}
    r = P.run_import(ledger=led, prev_ledger=prv, target_cl=436, editor=e, dry_run=False,
                     out_dir=os.path.join(tmp, f"w3l_{editor_cls.__name__}_{reverse}"))
    return r, P.verify(plan_file=r["plan_file"], editor=e), e
r, v, e = three_layer(LegacyEditor)
check("pipeline under the legacy rule: verify fails (A != 50)", r["ok"] and not v["ok"] and int(e.live["A"][230, 230]) != 50)
for rev in (False, True):
    r, v, e = three_layer(FakeEditor, reverse=rev)
    check(f"pipeline under the 5.8 channel write: exact (order {'C,A' if rev else 'A,C'})", r["ok"] and v["ok"]
          and int(e.live["A"][230, 230]) == 50 and int(e.live["B"][230, 230]) == 155 and int(e.live["C"][230, 230]) == 50)

# --- blend groups: only weight-blended layers of the same group must sum to 255 ------------------------------------------
gs, ind, note = g.paint_blend_groups([
    {"name": "Ground_Grass", "blend_method": "ELandscapeTargetLayerBlendMethod::PremultipliedAlphaBlending", "blend_group": "Ground"},
    {"name": "Ground_Rock", "blend_method": "ELandscapeTargetLayerBlendMethod::PremultipliedAlphaBlending", "blend_group": "Ground"},
    {"name": "Path_Dirt", "blend_method": "ELandscapeTargetLayerBlendMethod::None", "blend_group": "None"},
    {"name": "Old", "blend_method": "ELandscapeTargetLayerBlendMethod::FinalWeightBlending", "blend_group": "None"}],
    ["Ground_Grass", "Ground_Rock", "Path_Dirt", "Old"])
check("blend groups", gs == {"advanced:Ground": ["Ground_Grass", "Ground_Rock"], "legacy": ["Old"]} and ind == ["Path_Dirt"] and note is None)
check("no settings -> one group", g.paint_blend_groups(None, ["A", "B"])[0] == {"all": ["A", "B"]})
class GroupEditor(FakeEditor):
    def __call__(self, func, **kw):
        out = FakeEditor.__call__(self, func, **kw)
        if func == "paint_info":
            out["layer_settings"] = [
                {"name": "Ground_Grass", "blend_method": "ELandscapeTargetLayerBlendMethod::PremultipliedAlphaBlending", "blend_group": "Ground"},
                {"name": "Ground_Rock", "blend_method": "ELandscapeTargetLayerBlendMethod::PremultipliedAlphaBlending", "blend_group": "Ground"},
                {"name": "Ground_Sand", "blend_method": "ELandscapeTargetLayerBlendMethod::None", "blend_group": "None"}]
        return out
ge = GroupEditor()
ge.live["Ground_Sand"][:] = 200                           # a non-blended overlay: must not break the sum
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=436, editor=ge, out_dir=os.path.join(tmp, "wg"))
sg = next(x for x in r["guards"] if x["guard"] == "sum_255_in_window")
check("non-weight-blended layer excluded from the sum", r["ok"] and sg["ok"] and sg["detail"]["independent_layers"] == ["Ground_Sand"]
      and list(sg["detail"]["groups"]) == ["advanced:Ground"])
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=436, editor=FakeEditor(), out_dir=os.path.join(tmp, "wg2"))
check("without settings the old all-layer rule still applies", r["ok"]
      and next(x for x in r["guards"] if x["guard"] == "sum_255_in_window")["detail"]["note"].startswith("no layer_settings"))

# --- the dry-run read must leave every package clean (2026-10-08: the first live dry run dirtied 8_6, 9_6, 10_6) -----
ed5 = FakeEditor()
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=436, editor=ed5, out_dir=os.path.join(tmp, "wd1"))
rd = next(x for x in r["guards"] if x["guard"] == "read_left_packages_clean")
check("clean read: guard passes, nothing dirtied", r["ok"] and rd["ok"] and r["packages_dirtied_by_read"]["proxies"] == []
      and r["packages_dirtied_by_read"]["read_rect_proxies"] == ["LandscapeStreamingProxy_0_0_0"])
ed6 = FakeEditor(); ed6.dirty_on_read = True
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=436, editor=ed6, out_dir=os.path.join(tmp, "wd2"))
check("a read that dirties is caught and reported", not r["ok"] and "read_left_packages_clean" in r["failed_guards"]
      and r["packages_dirtied_by_read"]["proxies"] == ["LandscapeStreamingProxy_0_0_0"])
ed7 = FakeEditor(); ed7.dirty = {"LandscapeStreamingProxy_0_0_0"}
r = P.run_import(ledger=new, prev_ledger=prev, target_cl=436, editor=ed7, out_dir=os.path.join(tmp, "wd3"))
check("already-dirty proxy is not blamed on the read", r["ok"] and r["packages_dirtied_by_read"]["proxies"] == [])

# --- "never worse" sum rule on a pre-imbalanced landscape (Eric, 2026-10-08; live: 10,913 px already off 255) -------
nw = g.paint_never_worse([np.array([[254, 255, 253]])], [np.array([[254, 254, 255]])], (100, 200))
check("never-worse pure: unchanged + improved pass, worsened fails", not nw["ok"] and nw["worse_pixels"] == 1
      and nw["worse_at"][0]["px"] == [102, 200] and nw["improved_pixels"] == 1
      and nw["preexisting_off_255"]["count"] == 2 and nw["preexisting_off_255"]["histogram"] == {254: 2})
def imbalanced(change):
    """Live: grass 254 (sum 254) on rows 130..139 of the paint window; the ledger keeps / repairs / worsens it."""
    e = FakeEditor()
    e.live["Ground_Grass"][130:140, 150:190] = 254
    g0 = e.live["Ground_Grass"].copy()
    g1, r1 = g0.copy(), rock0.copy()
    g1[120:181, 140:201] = np.where(g0[120:181, 140:201] == 254, 254, 55)
    r1[120:181, 140:201] = np.where(g0[120:181, 140:201] == 254, 0, 200)
    if change == "repair":
        g1[130:132, 150:190] = 255
    elif change == "worsen":
        g1[130, 150] = 253
    led = {"Ground_Grass": png(f"gi_{change}.png", g1), "Ground_Rock": png(f"ri_{change}.png", r1)}
    prv = {"Ground_Grass": png(f"gi0_{change}.png", g0), "Ground_Rock": None}
    out = {}
    for strict in (False, True):
        out[strict] = P.run_import(ledger=led, prev_ledger=prv, target_cl=436, editor=e, require_exact_sum=strict,
                                   out_dir=os.path.join(tmp, f"wi_{change}_{strict}"))
    return out
def sum_guard(r):
    return next(x for x in r["guards"] if x["guard"] == "sum_255_in_window")
res = imbalanced("keep")
sg = sum_guard(res[False]); grp = next(iter(sg["detail"]["groups"].values()))
check("pre-existing imbalance kept: passes, reported not failed", res[False]["ok"] and sg["ok"] and grp["worse_pixels"] == 0
      and grp["preexisting_off_255"]["count"] == 400 and grp["preexisting_off_255"]["histogram"] == {254: 400}
      and grp["preexisting_off_255"]["samples"][0]["world"] == [g.paint_pixel_to_world(150), g.paint_pixel_to_world(130)])
check("require_exact_sum keeps the strict rule (fails)", not res[True]["ok"] and "sum_255_in_window" in res[True]["failed_guards"]
      and next(iter(sum_guard(res[True])["detail"]["groups"].values()))["bad_pixels"] == 400)
res = imbalanced("repair")
grp = next(iter(sum_guard(res[False])["detail"]["groups"].values()))
check("repairing pre-existing pixels passes (improved counted)", res[False]["ok"] and grp["improved_pixels"] == 80
      and grp["planned_off_255"] == 320)
res = imbalanced("worsen")
grp = next(iter(sum_guard(res[False])["detail"]["groups"].values()))
check("worsening one pre-imbalanced pixel fails", not res[False]["ok"] and grp["worse_pixels"] == 1
      and grp["worse_at"][0]["px"] == [150, 130] and grp["worse_at"][0]["live_sum"] == 254
      and grp["worse_at"][0]["planned_sum"] == 253)

# --- MCP wrappers ----------------------------------------------------------------------------------------------------
sys.modules.setdefault("unreal", mock.MagicMock())
sys.path.insert(0, _PY); os.chdir(_PY)
import unreal_mcp_server_advanced as S
seen = []
S._run_prk_module = lambda module, func, **kw: seen.append((module, func, kw)) or {"ok": True, "dry_run": kw.get("dry_run")}
res = S.landscape_paint_import({"Ground_Rock": "x.png"}, {"Ground_Rock": None}, 436, whole_landscape=True,
                               layer_textures={"Ground_Rock": "/Game/T/T_PaintMask_X"})
check("whole_landscape route: explicit, says all 256 proxies dirty", seen[-1][1] == "whole_landscape_import"
      and seen[-1][2]["dry_run"] is True and "all 256 proxies dirty" in res["note"])
check("whole_landscape needs textures", S.landscape_paint_import({}, {}, 436, whole_landscape=True).get("refused"))
print("FAILS:", fails)
