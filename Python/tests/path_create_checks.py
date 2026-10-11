import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
import math, os, sys
from unittest import mock
sys.modules["unreal"] = mock.MagicMock()
sys.path.insert(0, _CONTENT_PY)
import prk_terrain_geom as g
import prk_lake_ops as L
fails = []
def check(n, c):
    print(("PASS " if c else "FAIL ") + n)
    if not c: fails.append(n)

# --- geometry -------------------------------------------------------------------------------------------
pts = g.path_points([[0, 0, 1], {"x": 1000, "y": 0, "z": 2}])
check("path_points list + dict", pts == [[0.0, 0.0, 1.0], [1000.0, 0.0, 2.0]])
for bad in ([[0, 0, 0]], "x", [[0, 0, 0], [50, 0, 0]], [[0, 0, 0], [0, 0]], [[0, 0, 0], [True, 0, 0]], None):
    try:
        g.path_points(bad); check(f"path_points rejects {bad!r}", False)
    except ValueError:
        check(f"path_points rejects {bad!r}", True)
boxes = [((0, 0), (100, 100)), ((100, 0), (200, 100))]
check("outside none (shared edge)", g.points_outside_boxes([[50, 50, 0], [100, 10, 0], [199, 99, 0]], boxes) == [])
check("outside index", g.points_outside_boxes([[50, 50, 0], [250, 50, 0], [50, -1, 0]], boxes) == [1, 2])
check("outside no boxes", g.points_outside_boxes([[0, 0, 0]], []) == [0])
check("angle 0/90/180", g.xy_angle_deg([1, 0], [5, 0]) == 0 and abs(g.xy_angle_deg([1, 0], [0, 3]) - 90) < 1e-9
      and abs(g.xy_angle_deg([1, 0], [-1, 0]) - 180) < 1e-9)
check("angle zero vec", g.xy_angle_deg([0, 0], [1, 0]) is None)
road = [[0, 0, 0], [1000, 0, 0], [2000, 100, 0]]
# our start on their END, they arrive heading +X: straight on
check("turn start-on-end straight", g.connection_turn_deg(road, "start", [500, 0, 0], "end") < 1e-9)
# our start on their START: their tangent +X points away from the joint, so they arrive heading -X: U-turn
check("turn start-on-start reversed", abs(g.connection_turn_deg(road, "start", [500, 0, 0], "start") - 180) < 1e-9)
check("turn start-on-start straight", g.connection_turn_deg(road, "start", [-500, 0, 0], "start") < 1e-9)
check("turn end-on-start", abs(g.connection_turn_deg(road, "end", [1000, 100, 0], "start")) < 1e-6)
check("turn end-on-end", abs(g.connection_turn_deg(road, "end", [-1000, -100, 0], "end")) < 1e-6)
check("turn right angle", abs(g.connection_turn_deg(road, "start", [0, 500, 0], "end") - 90) < 1e-9)
try:
    g.connection_turn_deg(road, "middle", [1, 0, 0], "end"); check("turn rejects bad end", False)
except ValueError:
    check("turn rejects bad end", True)

# --- fakes ------------------------------------------------------------------------------------------------
class V:
    def __init__(self, x=0.0, y=0.0, z=0.0): self.x, self.y, self.z = float(x), float(y), float(z)
L.unreal.Vector = V
L.unreal.Name = str
class FakeSpline:
    def __init__(self, pts): self.pts = [list(map(float, p)) for p in pts]
    def get_number_of_spline_points(self): return len(self.pts)
    def get_location_at_spline_point(self, i, s): return V(*self.pts[i])
    def get_tangent_at_spline_point(self, i, s):
        a, b = (self.pts[0], self.pts[1]) if i == 0 else (self.pts[i - 1], self.pts[i])
        return V(*[b[c] - a[c] for c in range(3)])
    def _seg(self, k):
        i = min(int(math.floor(k)), len(self.pts) - 2); return i, k - i
    def get_location_at_spline_input_key(self, k, s):
        i, t = self._seg(k); a, b = self.pts[i], self.pts[i + 1]
        return V(*[a[c] + t * (b[c] - a[c]) for c in range(3)])
    def get_tangent_at_spline_input_key(self, k, s):
        i, _ = self._seg(k); a, b = self.pts[i], self.pts[i + 1]
        return V(*[b[c] - a[c] for c in range(3)])
    def find_input_key_closest_to_world_location(self, v):
        best = (1e30, 0.0)
        for i in range(len(self.pts) - 1):
            a, b = self.pts[i], self.pts[i + 1]
            dx, dy = b[0] - a[0], b[1] - a[1]
            t = max(0.0, min(1.0, ((v.x - a[0]) * dx + (v.y - a[1]) * dy) / (dx * dx + dy * dy)))
            d = math.hypot(a[0] + t * dx - v.x, a[1] + t * dy - v.y)
            best = min(best, (d, i + t))
        return best[1]
    def modify(self): pass
    def get_name(self): return "MainPathSpline_0"
    def set_spline_points(self, vs, space, upd): self.pts = [[v.x, v.y, v.z] for v in vs]
    def update_spline(self): pass
    def get_arrive_tangent_at_spline_point(self, i, s): return V(1, 2, 3)
    def get_leave_tangent_at_spline_point(self, i, s): return V(1, 2, 3)
class FakePath:
    def __init__(self, label, name, pts, tags=()):
        self.label, self.name, self.spline, self.tags = label, name, FakeSpline(pts), list(tags)
    def get_actor_label(self): return self.label
    def get_name(self): return self.name
    def get_editor_property(self, k): return list(self.tags) if k == "tags" else None
    def modify(self): pass
class FakeWorld:
    def __init__(self, path): self.path = path
    def get_path_name(self): return self.path
W = FakeWorld(L.DEFAULT_WORLD)
W3 = FakePath("EpPath4012896657", "EpPath_UAID_W3", [[146460, -107022, 13882], [196735, -95277, 11936], [201360, -94477, 11726]])
HF = FakePath("EpPath2832843964", "EpPath_UAID_HF", [[44793, -254661, 12182], [47573, -258714, 12279], [50387, -262744, 12388], [53224, -266757, 12508]])
MINE = FakePath("R8_Quarry", "EpPath_UAID_MINE", [[0, 0, 0], [1, 0, 0]], tags=[L.SCRIPTED_PATH_TAG])
DESC = {"name": "EPD_DirtRoad", "package": "/Game/PRK/World/Paths/EPD_DirtRoad",
        "class_path": "/Game/PRK/World/Paths/EPD_DirtRoad.EPD_DirtRoad_C"}
R2A = [[202000, -94000, 11888], [202200, -90000, 11796], [211900, -77500, 12146], [226000, -40000, 12139]]
R8 = [[146000, -108000, 14263], [139700, -132100, 14813], [56000, -257400, 12445], [50000, -262000, 13327]]
STRAIGHT = [[201000, -94400, 11726], [206000, -93500, 11700], [212000, -92500, 11600]]
LAND = [((-1e6, -1e6), (1e6, 1e6))]
_REAL_TRACER = L._LandscapeTracer
class FlatTracer:
    """Landscape at the given points' own Z is unknown offline: a flat 0 ground (carve report / grade)."""
    def __init__(self, w): self.count = 0
    def z(self, x, y): self.count += 1; return 0.0
def create(_p=None, **kw):
    p = dict(pie=None, world=W, paths=[W3, HF], desc=(DESC, None), land=LAND, adjust=([], None))
    p.update(_p or {})
    tracer = L._LandscapeTracer if L._LandscapeTracer is not _REAL_TRACER else FlatTracer
    with mock.patch.object(L._el, "_pie_guard", return_value=p["pie"]), \
         mock.patch.object(L._el, "_editor_world", return_value=p["world"]), \
         mock.patch.object(L, "_all", return_value=p["paths"]), \
         mock.patch.object(L, "_main_spline", side_effect=lambda a: getattr(a, "spline", None)), \
         mock.patch.object(L, "_resolve_desc_class", return_value=p["desc"]), \
         mock.patch.object(L, "_landscape_boxes", return_value=p["land"]), \
         mock.patch.object(L, "_LandscapeTracer", tracer), \
         mock.patch.object(L, "_own_adjustments", return_value=p["adjust"]):
        args = dict(desc_class="EPD_DirtRoad", points=[list(q) for q in R2A], label="R2a_KingsRoadLink", drape=False)
        args.update(kw)
        return L.path_create(**args)

# --- path_create guards / planning (dry run) ---------------------------------------------------------------
r = create()
check("dry run ok", r["ok"] and r["dry_run"] and r["point_count"] == 4 and not r["failed_guards"])
r = create(_p={"pie": {"ok": False, "refused": True, "error": "PIE is running"}})
check("dry run reports PIE guard", not r["ok"] and "pie_stopped" in r["failed_guards"])
r = create(_p={"pie": {"ok": False, "refused": True, "error": "PIE is running"}}, dry_run=False)
check("real run refuses PIE", r.get("refused") and "PIE" in r["error"])
r = create(_p={"world": FakeWorld("/Game/Other/Map.Map")})
check("refuses other world", r.get("refused") and "expected" in r["error"])
check("refuses empty label", create(label=" ").get("refused"))
check("refuses bad tangent mode", create(tangent_mode="flat").get("refused"))
check("refuses 1 point", create(points=[[0, 0, 0]]).get("refused"))
r = create(label="EpPath4012896657")
check("label taken fails guard", not r["ok"] and "label_unused" in r["failed_guards"])
r = create(_p={"desc": (None, "'EPD_Highway' is not an allow-listed path description")})
check("desc guard", not r["ok"] and "desc_class" in r["failed_guards"])
r = create(_p={"land": [((0, 0), (1, 1))]})
check("off-landscape guard", not r["ok"] and "on_landscape" in r["failed_guards"]
      and len(next(x for x in r["guards"] if x["guard"] == "on_landscape")["detail"]["points_outside"]) == 4)
check("no landscape fails", "on_landscape" in create(_p={"land": []})["failed_guards"])
check("allow-list refuses other EPD", L._resolve_desc_class("/Game/ErrantWorlds/ErrantPathsAssetLibrary/EPD_Highway")[0] is None)
check("allow-list refuses unknown short name", L._resolve_desc_class("EPD_GoatTrack")[0] is None)
check("EPD_MountainRoad allow-listed, not draped", L.PATH_DESC_CLASSES["EPD_MountainRoad"] == "/Game/PRK/World/Paths/EPD_MountainRoad"
      and "EPD_MountainRoad" not in L.DRAPE_DESCS)

# R2a as planned turns 77 deg north off the W3 road's eastward end: refused as a connect, fine as a join
r = create(connect_start={"path": "EpPath4012896657", "end": "end"})
check("R2a as planned: connect refused at 77 deg", r.get("refused") and abs(r["plan"]["turn_deg"] - 77.3) < 0.1)
r = create(connect_start={"path": "EpPath4012896657", "end": "end", "connect": False})
cs = r.get("connect_start") or {}
check("R2a join plan", r["ok"] and cs.get("mode") == "join" and cs.get("other_end") == "end"
      and r["points"][0] == [201360.0, -94477.0, 11726.0] and "_actor" not in cs)
r = create(points=[list(q) for q in STRAIGHT], connect_start={"path": "EpPath4012896657", "end": "end"})
cs = r.get("connect_start") or {}
check("straight-on connect plan", r["ok"] and cs.get("mode") == "connect" and cs["turn_deg"] < 30)
r = create(connect_start={"path": "EpPath4012896657", "end": "end"}, connect_max_turn_deg=80)
check("connect_max_turn_deg raises the limit", r["ok"] and r["connect_start"]["mode"] == "connect")
r = create(points=[[201360, -94477, 11726], [190000, -80000, 11000]], connect_start={"path": "EpPath4012896657", "end": "end"})
check("hairpin connect refused", r.get("refused") and "connect: false" in r["error"])
r = create(points=[[201360, -94477, 11726], [190000, -80000, 11000]], connect_start={"path": "EpPath4012896657", "end": "end", "connect": False})
check("hairpin join allowed", r["ok"] and r["connect_start"]["mode"] == "join")
check("snap too far refused", create(connect_start={"path": "EpPath4012896657", "end": "end", "connect": False, "snap_max_cm": 100}).get("refused"))
check("unknown other refused", create(connect_start={"path": "Nope", "end": "end"}).get("refused"))
check("bad end refused", create(connect_start={"path": "EpPath4012896657", "end": "middle"}).get("refused"))
check("unknown key refused", create(connect_start={"path": "EpPath4012896657", "end": "end", "type": "T"}).get("refused"))
check("non-dict refused", create(connect_start="EpPath4012896657").get("refused"))
# R8: start by nearest end (W3 start, 11 m away), end ONTO the Hollow Fields road (mid spline)
r = create(points=[list(q) for q in R8], label="R8_Quarry", connect_start={"nearest_end_within_cm": 2000, "connect": False},
           connect_end={"path": "EpPath2832843964", "end": "onto"})
ce, cs = r.get("connect_end") or {}, r.get("connect_start") or {}
check("R8 nearest start join", r["ok"] and cs.get("other") == "EpPath4012896657" and cs.get("other_end") == "start"
      and r["points"][0] == [146460.0, -107022.0, 13882.0] and cs.get("ends_in_range"))
on = HF.spline.find_input_key_closest_to_world_location(V(50000, -262000))
check("R8 onto plan", ce.get("mode") == "onto" and ce.get("other_end") is None and 1 < ce["input_key"] < 3
      and abs(r["points"][-1][2] - HF.spline.get_location_at_spline_input_key(on, 0).z) < 0.1)
check("onto + connect refused", create(connect_end={"path": "EpPath2832843964", "end": "onto", "connect": True}).get("refused"))
r = create(points=[[100000, -200000, 0], [44000, -254000, 0]], connect_end={"path": "EpPath2832843964", "end": "onto"})
check("onto at their end refused", r.get("refused") and "use end" in r["error"])
check("nearest none refused", create(connect_start={"nearest_end_within_cm": 10}).get("refused"))
check("nearest + path refused", create(connect_start={"nearest_end_within_cm": 10, "path": "X"}).get("refused"))
r = create(points=[[146460, -107022, 0], [196735, -95277, 0], [201000, -94400, 0]],
           connect_start={"path": "EpPath4012896657", "end": "start", "connect": False},
           connect_end={"path": "EpPath4012896657", "end": "end"}, connect_max_turn_deg=180)
check("both ends on one path with connect refused", r.get("refused") and "same path" in r["error"])
r = create(points=[[200000, -94477, 0], [201400, -94400, 0], [210000, -80000, 0]], connect_start={"path": "EpPath4012896657", "end": "end", "connect": False})
check("short segment after snap refused", r.get("refused") and "After snapping" in r["error"])

# --- path_create real run (mocked plugin) ------------------------------------------------------------------
lib = mock.MagicMock()
L.unreal.EpPathsLibrary = lib
class NewPath(FakePath):
    def __init__(self):
        super().__init__("EpPath123", "EpPath_UAID_NEW", [[0, 0, 0]])
    def set_actor_label(self, l): self.label = l
    auto = True
    def set_editor_property(self, k, v):
        if k == "tags": self.tags = list(v)
        elif k == "allow_automatic_regeneration": self.auto = bool(v) if not self.stuck else self.auto
        else: raise KeyError(k)
    def get_editor_property(self, k):
        return self.auto if k == "allow_automatic_regeneration" else FakePath.get_editor_property(self, k)
    stuck = False
    def get_folder_path(self): return "Errant Paths"
new = NewPath()
bridge = mock.MagicMock()
def bridge_get(obj, prop):
    if prop == "PathDesc":
        return "/Game/PRK/World/Paths/EPD_DirtRoad.EPD_DirtRoad_C'/Game/X.X:PersistentLevel.EpPath_UAID_NEW.Desc'"
    if prop == "SplineConnections":
        return ('((ThisSpline="/Game/X.X:PersistentLevel.EpPath_UAID_NEW.MainPathSpline_0",'
                'OtherSplinePtr="/Game/X.X:PersistentLevel.EpPath_UAID_W3.MainPathSpline_0",OtherSplineEnd=End))')
    return None
bridge.get_property_by_path.side_effect = bridge_get
planned = {"pts": []}
def real(read_back=None, splines=None, **kw):
    lib.reset_mock(); lib.spawn_path_from_desc_class.return_value = new
    new.label, new.tags, new.auto = "EpPath123", [], True
    with mock.patch.object(L, "_path_splines", return_value=[new.spline] if splines is None else splines), \
         mock.patch.object(L._el, "_bridge", return_value=bridge), \
         mock.patch.object(L._el, "_spline_points", side_effect=lambda s: read_back if read_back is not None else
                           [[round(c, 1) for c in q] for q in planned["pts"]]), \
         mock.patch.object(L, "_tangents", return_value=[]), \
         mock.patch.object(L, "_set_projection_off", side_effect=lambda p_: proj.__setitem__("on", False) or True), \
         mock.patch.object(L, "_projection_on", side_effect=lambda p_: proj["on"]):
        return create(dry_run=False, **kw)
proj = {"on": True}
planned["pts"] = [[201360, -94477, 11726]] + [list(q) for q in STRAIGHT[1:]]
r = real(points=[list(q) for q in STRAIGHT], connect_start={"path": "EpPath4012896657", "end": "end"})
check("real run verified", r["ok"] and r["verified"] and r["actor_label"] == "R2a_KingsRoadLink" and not r["mismatches"])
check("real run spawn + tag", lib.spawn_path_from_desc_class.call_count == 1 and L.SCRIPTED_PATH_TAG in [str(t) for t in new.tags])
check("real run connect_paths(other, new)", lib.connect_paths.call_count == 1
      and lib.connect_paths.call_args[0][0] is W3 and lib.connect_paths.call_args[0][1] is new)
check("real run EP setter fired", lib.set_location_at_spline_point.call_count == 1)
check("real run auto tangents untouched", lib.set_tangents_at_spline_point.call_count == 0)
r = real(points=[list(q) for q in STRAIGHT], connect_start={"path": "EpPath4012896657", "end": "end"}, tangent_mode="level")
check("level mode sets every point's tangents", lib.set_tangents_at_spline_point.call_count == 3)
check("default keeps auto regen", r["allow_automatic_regeneration"] is True and new.auto is True)
r = real(points=[list(q) for q in STRAIGHT], connect_start={"path": "EpPath4012896657", "end": "end"}, allow_auto_regen=False)
check("allow_auto_regen False written + read back", r["ok"] and new.auto is False and r["allow_automatic_regeneration"] is False
      and r["allow_auto_regen"] is False)
new.stuck = True
r = real(points=[list(q) for q in STRAIGHT], connect_start={"path": "EpPath4012896657", "end": "end"}, allow_auto_regen=False)
new.stuck = False
check("auto regen write that does not stick is a mismatch", not r["ok"]
      and any(m.get("field") == "allow_automatic_regeneration" for m in r["mismatches"]))
check("dry run reports allow_auto_regen", create(allow_auto_regen=False)["allow_auto_regen"] is False)
r = real(read_back=[[0, 0, 0]] * 3, points=[list(q) for q in STRAIGHT], connect_start={"path": "EpPath4012896657", "end": "end"})
check("read-back mismatch reported", not r["ok"] and r["mismatches"] and "errant_path_delete" in r["error"])
planned["pts"] = [[146460, -107022, 13882], [150000, -120000, 0]]
r = real(connect_start={"path": "EpPath4012896657", "end": "start", "connect": False}, points=[[146460, -107022, 13882], [150000, -120000, 0]])
check("join makes no connect call", lib.connect_paths.call_count == 0 and r["ok"])
planned["pts"] = [list(q) for q in R2A]
r = real()
check("no connection requested -> no connection check", r["ok"])
r = real(splines=[FakeSpline([[0, 0, 0]]), FakeSpline([[0, 0, 0]])])
check("multi-spline description refused + destroyed", r.get("refused") and "path splines" in r["error"])
lib.spawn_path_from_desc_class.return_value = None
with mock.patch.object(L._el, "_bridge", return_value=bridge):
    r = create(dry_run=False)
check("spawn None -> error", not r["ok"] and "returned None" in r["error"])

# --- drape (2026-10-08) ------------------------------------------------------------------------------------
dz = g.densify_xy([[0, 0, 0], [7000, 0, 70], [7100, 0, 0]], 3000)
check("densify spacing", len(dz) == 5 and all(math.hypot(b[0] - a[0], b[1] - a[1]) <= 3000 + 1e-6 for a, b in zip(dz, dz[1:])))
check("densify keeps given points", dz[0] == [0.0, 0.0, 0.0] and dz[3] == [7000.0, 0.0, 70.0] and dz[-1] == [7100.0, 0.0, 0.0])
check("densify short segment untouched", g.densify_xy([[0, 0, 0], [500, 0, 0]], 3000) == [[0.0, 0.0, 0.0], [500.0, 0.0, 0.0]])
try:
    g.densify_xy([[0, 0, 0], [1, 0, 0]], 0); check("densify rejects 0", False)
except ValueError:
    check("densify rejects 0", True)
class Tracer:
    hole = None
    def __init__(self, w): pass
    def z(self, x, y):
        return None if Tracer.hole and abs(x - Tracer.hole[0]) < 1 and abs(y - Tracer.hole[1]) < 1 else 5000.0 + x / 1000.0
with mock.patch.object(L, "_LandscapeTracer", Tracer):
    r = create(drape=None)
    check("drape default on for dirt road", r["ok"] and r["drape"] is True and r["point_count"] > len(R2A)
          and all(abs(q[2] - (5000.0 + q[0] / 1000.0)) < 0.2 for q in r["points"]))
    check("drape spacing <= 30 m", all(math.hypot(b[0] - a[0], b[1] - a[1]) <= 3000.5 for a, b in zip(r["points"], r["points"][1:])))
    r = create(drape=None, _p={"desc": (dict(DESC, name="EPD_River"), None)})
    check("drape default off for river", r["drape"] is False and r["point_count"] == len(R2A))
    r = create(drape=None, _p={"desc": (dict(DESC, name="EPD_Stream"), None)})
    check("EPD_Stream allow-listed, drape default off", L.PATH_DESC_CLASSES["EPD_Stream"].endswith("/River/EPD_Stream")
          and r["drape"] is False)
    r = create(drape=False)
    check("drape False keeps points", r["drape"] is False and r["points"][1] == [202200.0, -90000.0, 11796.0])
    Tracer.hole = (226000, -40000)
    r = create(drape=True)
    check("drape hole fails guard", not r["ok"] and "drape_traced" in r["failed_guards"])
    Tracer.hole = None
    r = create(drape=True, connect_start={"path": "EpPath4012896657", "end": "end", "connect": False})
    check("drape after snapping: start XY on the joint", r["points"][0][:2] == [201360.0, -94477.0])
    proj["on"] = True
    planned["pts"] = [[round(c, 1) for c in q] for q in create(drape=True)["points"]]
    r = real(drape=True)
    check("real drape: projection off + read back", r["ok"] and r["project_onto_geometry"] is False and proj["on"] is False)
    planned["pts"] = [[round(c, 1) for c in q] for q in create(drape=True)["points"]]
    lib.reset_mock(); lib.spawn_path_from_desc_class.return_value = new
    new.label, new.tags, new.auto = "EpPath123", [], True
    with mock.patch.object(L, "_path_splines", return_value=[new.spline]), \
         mock.patch.object(L._el, "_bridge", return_value=bridge), \
         mock.patch.object(L._el, "_spline_points", side_effect=lambda s_: planned["pts"]), \
         mock.patch.object(L, "_tangents", return_value=[]), \
         mock.patch.object(L, "_set_projection_off", return_value=False), \
         mock.patch.object(L, "_projection_on", return_value=True):
        r = create(dry_run=False, drape=True)
    check("real drape: projection still on is a mismatch", not r["ok"]
          and any(m.get("field") == "bProjectOntoGeometry" for m in r["mismatches"]))

# --- path_replace_points ---------------------------------------------------------------------------------
import tempfile, json as _json
snapdir = tempfile.mkdtemp(prefix="prk_snap_")
MINE2 = FakePath("R2a_KingsRoadLink", "EpPath_UAID_R2A", [[201360, -94477, 11726], [210000, -80000, 12000]], tags=[L.SCRIPTED_PATH_TAG])
def replace(_p=None, conns_text=None, **kw):
    p = dict(pie=None, world=W, paths=[W3, MINE, MINE2])
    p.update(_p or {})
    br = mock.MagicMock()
    br.get_property_by_path.side_effect = lambda o, prop: conns_text if prop == "SplineConnections" else None
    with mock.patch.object(L._el, "_pie_guard", return_value=p["pie"]), \
         mock.patch.object(L._el, "_editor_world", return_value=p["world"]), \
         mock.patch.object(L, "_all", return_value=p["paths"]), \
         mock.patch.object(L, "_main_spline", side_effect=lambda a: getattr(a, "spline", None)), \
         mock.patch.object(L, "_landscape_boxes", return_value=LAND), \
         mock.patch.object(L._el, "_bridge", return_value=br), \
         mock.patch.object(L, "_LandscapeTracer", Tracer), \
         mock.patch.object(L, "_snapshot_dir", return_value=snapdir), \
         mock.patch.object(L, "_tangents", return_value=[]), \
         mock.patch.object(L._el, "_spline_points", side_effect=lambda s_: [[round(c, 1) for c in q] for q in s_.pts]), \
         mock.patch.object(L, "_set_projection_off", side_effect=lambda p_: proj.__setitem__("on", False) or True), \
         mock.patch.object(L, "_projection_on", side_effect=lambda p_: proj["on"]):
        args = dict(path="R2a_KingsRoadLink", points=[[201360, -94477, 0], [205000, -88000, 0], [210000, -80000, 0]])
        args.update(kw)
        return L.path_replace_points(**args)
r = replace(path="EpPath4012896657")
check("replace refuses untagged", r.get("refused") and L.SCRIPTED_PATH_TAG in r["error"])
check("replace refuses missing", replace(path="Nope").get("refused"))
check("replace refuses PIE real", replace(dry_run=False, _p={"pie": {"ok": False, "refused": True, "error": "PIE"}}).get("refused"))
check("replace refuses bad points", replace(points=[[0, 0, 0]]).get("refused"))
check("replace refuses bad tangent mode", replace(tangent_mode="x").get("refused"))
r = replace()
check("replace dry run drapes", r["ok"] and r["dry_run"] and r["point_count"] > 3 and r["point_count_before"] == 2
      and all(abs(q[2] - (5000.0 + q[0] / 1000.0)) < 0.2 for q in r["planned"]))
check("replace dry run writes no snapshot", not os.listdir(snapdir))
conn_start = ('((ThisSpline="/Game/X.X:PersistentLevel.EpPath_UAID_R2A.MainPathSpline_0",'
              'OtherSplinePtr="/Game/X.X:PersistentLevel.EpPath_UAID_W3.MainPathSpline_0",OtherSplineEnd=End))')
r = replace(conns_text=conn_start, points=[[201900, -94477, 0], [205000, -88000, 0], [210000, -80000, 0]])
check("replace refuses moving a connected end", r.get("refused") and r["moved_ends"][0]["end"] == "start")
r = replace(conns_text=conn_start)
check("replace drape keeps a connected end's Z", r["ok"] and r["planned"][0] == [201360.0, -94477.0, 11726.0]
      and abs(r["planned"][1][2] - (5000.0 + r["planned"][1][0] / 1000.0)) < 0.2)
r = replace(conns_text=conn_start, drape=False, points=[[201360, -94477, 11726], [205000, -88000, 0], [210000, -80000, 0]])
check("replace allows a kept connected end", r["ok"])
lib.reset_mock(); proj["on"] = True
r = replace(dry_run=False)
snap = os.path.join(snapdir, "R2a_KingsRoadLink_before.json")
check("replace real: snapshot of old points", os.path.isfile(snap)
      and _json.load(open(snap))["points"] == [[201360.0, -94477.0, 11726.0], [210000.0, -80000.0, 12000.0]])
check("replace real: written, verified, projection off", r["ok"] and r["verified"] and proj["on"] is False
      and len(MINE2.spline.pts) == r["point_count"] and lib.set_location_at_spline_point.call_count == 1)
MINE2.spline.pts = [[201360, -94477, 11726], [210000, -80000, 12000]]
r = replace(dry_run=False, drape=False, tangent_mode="level")
check("replace level tangents", lib.set_tangents_at_spline_point.call_count == 3 and r["ok"])

# --- connection read-back / layer verdict -----------------------------------------------------------------
plan = {"our_end": "start", "other_name": "EpPath_UAID_W3", "other_end": "end"}
conns = [{"other_actor": "EpPath_UAID_W3", "ThisSplineEnd": "(default)", "OtherSplineEnd": "End"}]
check("conn present", L._connection_present(conns, plan))
check("conn wrong end", not L._connection_present(conns, dict(plan, other_end="start")))
check("conn wrong our end", not L._connection_present(conns, dict(plan, our_end="end")))
check("conn none", not L._connection_present([], plan))
rows = [{"xy": [0, 0], "layers": {"Path_Dirt": 0.9}}, {"xy": [1, 0], "layers": {"Ground_Dirt": 0.6, "Path_Dirt": 0.2}}]
check("layers ok", L._layer_verdict(rows, ["Ground_Dirt", "Path_Dirt"])["verified"])
v = L._layer_verdict(rows + [{"xy": [2, 0], "layers": {"Ground_Grass": 1.0}}], ["Ground_Dirt", "Path_Dirt"])
check("layers miss", not v["verified"] and v["missing_at"] == [[2, 0]])
v0 = L._layer_verdict(rows, [])
check("layers unknown: skipped, never a failure (R2b 2026-10-08)", v0["verified"] and v0["skipped"] and not v0["reasons"])
check("layers no samples", not L._layer_verdict([], ["Path_Dirt"])["verified"])

# --- path_delete ---------------------------------------------------------------------------------------------
def delete(_p=None, **kw):
    p = dict(pie=None, world=W, paths=[W3, MINE])
    p.update(_p or {})
    with mock.patch.object(L._el, "_pie_guard", return_value=p["pie"]), \
         mock.patch.object(L._el, "_editor_world", return_value=p["world"]), \
         mock.patch.object(L, "_all", return_value=p["paths"]), \
         mock.patch.object(L, "_main_spline", side_effect=lambda a: getattr(a, "spline", None)), \
         mock.patch.object(L._el, "_bridge", return_value=bridge), \
         mock.patch.object(L, "_dirty_landscape_state", return_value={}):
        return L.path_delete(**kw)
r = delete(path="EpPath4012896657")
check("delete refuses untagged", r.get("refused") and L.SCRIPTED_PATH_TAG in r["error"])
r = delete(path="R8_Quarry")
check("delete dry run lists", r["ok"] and r["dry_run"] and r["connected_paths"][0]["name"] == "EpPath_UAID_W3")
check("delete refuses PIE real", delete(path="R8_Quarry", dry_run=False, _p={"pie": {"ok": False, "refused": True, "error": "PIE"}}).get("refused"))
check("delete refuses missing", delete(path="Nope").get("refused"))
eas = mock.MagicMock(); eas.destroy_actor.return_value = True
L.unreal.get_editor_subsystem.return_value = eas
L.unreal.find_object.return_value = None
r = delete(path="R8_Quarry", dry_run=False)
check("delete real calls destroy", eas.destroy_actor.call_count == 1 and eas.destroy_actor.call_args[0][0] is MINE)
check("delete real reports listed-still (fake world keeps it)", not r["ok"] and r["path_still_listed"] == ["EpPath_UAID_MINE"])
with mock.patch.object(L, "_resolve_path", return_value=(MINE, None)):
    r = delete(path="R8_Quarry", dry_run=False, _p={"paths": [W3]})
check("delete real ok when gone", r["ok"] and r["destroyed"])

# --- MCP wrappers ---------------------------------------------------------------------------------------------
sys.path.insert(0, _PY); os.chdir(_PY)
import unreal_mcp_server_advanced as S
calls = []
S._run_prk_module = lambda module, func, **kw: calls.append((module, func, kw)) or {"ok": True}
S.errant_path_create("EPD_River", [[0, 0, 0], [1000, 0, 0]], "G1_Stream", tangent_mode="level",
                     connect_end={"path": "EpPath1", "end": "onto"})
m, f, kw = calls[-1]
check("wrapper create dispatch", m == "prk_lake_ops" and f == "path_create" and kw["dry_run"] is True
      and kw["tangent_mode"] == "level" and kw["connect_end"]["end"] == "onto" and kw["regenerate"] is False
      and kw["expect_world"] == L.DEFAULT_WORLD and kw["allow_auto_regen"] is True)
S.errant_path_delete("G1_Stream")
check("wrapper delete dispatch", calls[-1] == ("prk_lake_ops", "path_delete",
      {"path": "G1_Stream", "dry_run": True, "expect_world": L.DEFAULT_WORLD}))
import inspect
S.errant_path_replace_points("R2a", [[0, 0, 0], [1000, 0, 0]])
check("wrapper replace dispatch", calls[-1][1] == "path_replace_points" and calls[-1][2]["dry_run"] is True
      and calls[-1][2]["drape"] is True)
check("wrapper replace signature matches", set(inspect.signature(S.errant_path_replace_points).parameters)
      == set(inspect.signature(L.path_replace_points).parameters))
check("wrapper create signature matches", set(inspect.signature(S.errant_path_create).parameters)
      == set(inspect.signature(L.path_create).parameters))
print("FAILS:", fails)
