import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_sys.path.insert(0, _os.path.join(_ROOT, "Content", "Python"))
import prk_terrain_geom as g

fails = []
def check(name, cond):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond: fails.append(name)

# --- rotation clip fit (2026-10-09, Shattersteppe)
check("axis aligned fits exactly", abs(g.rotation_clip_fit(0, 1500, 900) - 1.0) < 1e-9)
check("yaw 180 fits", g.rotation_clip_fit(180, 1500, 900) >= 1 - g.ROTATION_CLIP_TOL)
check("yaw 90 non-square clips", g.rotation_clip_fit(90, 1500, 900) < 0.7)
check("yaw 90 square fits", g.rotation_clip_fit(90, 1024, 1024) >= 1 - g.ROTATION_CLIP_TOL)
k = g.rotation_clip_fit(135, 1500, 900)
check("SouthEastUpland1 fit ~0.53", 0.52 < k < 0.54)
check("falloff end shrinks the rect", abs(g.rotation_clip_fit(135, 1500, 900, (k, k)) - 1.0) < 1e-6)
check("falloff off counts whole footprint", g.rotation_clip_fit(135, 1500, 900, (0.3, 0.3), False) == k)
check("Pain13 (-168.1, 1900x900) clips", g.rotation_clip_fit(-168.1, 1900, 900) < 0.75)

# --- fix keeps the ground falloff and fits
fix = g.rotation_clip_fix(135, [1500, 900, 0.4], [0.45, 0.45], [1.0, 1.0])
check("fix returned", fix is not None)
check("fix fits after", g.rotation_clip_fit(135, fix["scale"][0], fix["scale"][1], fix["falloff_end"]) > 1.0)
check("fix keeps falloff-end world size",
      abs(fix["scale"][0] * fix["falloff_end"][0] - 1500.0) < 2.0 and abs(fix["scale"][1] * fix["falloff_end"][1] - 900.0) < 2.0)
check("fix keeps falloff-start world size", abs(fix["scale"][0] * fix["falloff_start"][0] - 1500 * 0.45) < 2.0)
check("fix keeps scale z", fix["scale"][2] == 0.4)
check("no fix when aligned", g.rotation_clip_fix(0, [1500, 900, 1], [0.5, 0.5], [1, 1]) is None)

# --- order-dependent peers
cov = [{"label": "C0", "priority": 0, "blend_method": "MAX"},
       {"label": "C1", "priority": 1, "blend_method": "MAX"},
       {"label": "A0", "priority": 0, "blend_method": "ADD_AND_SUBTRACT"}]
check("add at 0 vs max at 0", [b["label"] for b in g.order_dependent_peers("ADD_AND_SUBTRACT", 0, cov)] == ["C0"])
check("add at 1 vs max at 1", [b["label"] for b in g.order_dependent_peers("ONLY_ADD", 1, cov)] == ["C1"])
check("max at 0 vs add at 0", [b["label"] for b in g.order_dependent_peers("MAX", 0, cov)] == ["A0"])
check("add at 5 clean", g.order_dependent_peers("ADD_AND_SUBTRACT", 5, cov) == [])
check("unknown blend skipped", g.order_dependent_peers(None, 0, cov) == [])
check("min counts as cap", [b["label"] for b in g.order_dependent_peers("MIN", 0, cov)] == ["A0"])

# --- strip seam statistic
flat_seam = [[4750.0] * 6 + [6100.0] * 6 for _ in range(40)]           # 13.5 m step between samples 5 and 6
check("seam detected", g.strip_step_hits(flat_seam)["hits"] == 40)
check("seam max step", g.strip_step_hits(flat_seam)["max_step_cm"] == 1350.0)
ramp = [[4750.0 + 120.0 * i for i in range(12)] for _ in range(40)]       # even 1.2 m steps: slope, not seam
check("ramp not a seam", g.strip_step_hits(ramp)["hits"] == 0)
offcentre = [[4750.0] * 2 + [6100.0] * 10 for _ in range(40)]           # step far from the edge
check("off-edge step ignored", g.strip_step_hits(offcentre)["hits"] == 0)
check("None columns skipped", g.strip_step_hits([[None] * 12, flat_seam[0]])["columns"] == 1)
s = g.strip_step_hits(flat_seam)
check("seam is one long run", s["longest_run"] == 40 and s["run_start"] == 0 and s["run_end"] == 39)
quiet = [4750.0] * 12
scattered = [flat_seam[0] if q % 4 == 0 else quiet for q in range(40)]   # cliffs crossing every 4th column
sc = g.strip_step_hits(scattered)
check("scattered cliffs: same-ish hits, short runs", sc["hits"] == 10 and sc["longest_run"] == 1)
gappy = [quiet if q in (10, 20) else flat_seam[0] for q in range(40)]   # a seam with two single-column gaps
check("run bridges single-column gaps", g.strip_step_hits(gappy)["longest_run"] == 38)
check("gap=0 splits the run", g.strip_step_hits(gappy, gap=0)["longest_run"] == 19)
check("hit_index lists the columns", g.strip_step_hits(scattered)["hit_index"] == list(range(0, 40, 4)))

print("\n%d failure(s)" % len(fails))
_sys.exit(1 if fails else 0)
