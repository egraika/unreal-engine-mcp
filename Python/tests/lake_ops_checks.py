import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
import sys, types
from unittest import mock
sys.modules["unreal"] = mock.MagicMock()
sys.path.insert(0, _CONTENT_PY)
import prk_lake_ops as L
ok = True
def check(n, c):
    global ok
    print(("PASS " if c else "FAIL ") + n); ok &= bool(c)
check("enum AddAndSubtract", L._enum_key("AddAndSubtract") == "ADD_AND_SUBTRACT")
check("enum Max", L._enum_key("Max") == "MAX")
check("enum snake", L._enum_key("replace_if_over_threshold") == "REPLACE_IF_OVER_THRESHOLD")
check("enum upper", L._enum_key("WEIGHT_BLEND_NO_DEFAULT") == "WEIGHT_BLEND_NO_DEFAULT")
check("enum Red", L._enum_key("Red") == "RED")
check("object path", L._object_path("/Game/A/T_X") == "/Game/A/T_X.T_X" and L._object_path("/Game/A/T_X.T_X") == "/Game/A/T_X.T_X")
snap = {"invert": False, "multiplier": 0.7, "falloff": False, "falloff_start_xy": [0.5, 0.5], "falloff_end_xy": [1, 1],
        "falloff_noise": 0.0, "offset_method": "BRUSH_Z_LOCATION", "z_offset": 0.0,
        "levels": {"input": [0, 1], "output": [0, 1]}, "clamp_output": False, "clamp": [-1, 1], "blend_method": "ADD_AND_SUBTRACT"}
import copy
p = copy.deepcopy(snap)
L._plan_blend(p, {"invert": True, "multiplier": 0.036, "falloff_start": 0, "levels": [[0, 0.5], [0, 1]], "clamp": [0, 1], "blend_method": "Max"})
check("plan blend", p["invert"] and p["falloff_start_xy"] == [0.0, 0.0] and p["levels"]["input"] == [0.0, 0.5] and p["clamp_output"] and p["blend_method"] == "MAX")
d = L._diff({"a": snap}, {"a": p})
check("diff fields", {r["field"] for r in d} == {"a.invert", "a.multiplier", "a.falloff_start_xy", "a.levels.input", "a.clamp_output", "a.clamp", "a.blend_method"} or print(d))
check("close float32", L._close(0.035999998450279236, 0.036))
check("close str", not L._close("MAX", "MIN"))
check("hits None", L._hits(None) == [] and L._hits(False) == [] and L._hits((False, None)) == [])
# --- path_regenerate (guards, dry run, verdict)
check("all mask 32767", L._all_update_mask() == 32767 and L._EP_UPDATE_FLAGS.index("RespawnActor") == 15)
flat = [4749.0] * 11
good = {"main_z": flat, "post_spawn_z": [4749.2] * 11,
        "surface": {"surface": "SM_R", "meshes": {"SM_R": {"count": 50, "top_min": 4426.2, "top_max": 4426.6}}}}
check("verdict good", L._regen_verdict(good)["verified"])
sag = dict(good, post_spawn_z=[4749.2, 4440.2, 4260.8, 4190.3, 4203.6, 4276.1, 4384.4, 4507.1, 4623.2, 4711.3, 4749.0])
v = L._regen_verdict(sag)
check("verdict sag fails", not v["verified"] and v["post_spawn_max_dev_cm"] == 558.7)
hump = dict(good, surface={"surface": "SM_R", "meshes": {"SM_R": {"count": 9, "top_min": 4465.2, "top_max": 4954.8},
                                                         "SM_Rock": {"count": 2, "top_min": 0, "top_max": 1}}})
check("verdict level spread fails", not L._regen_verdict(hump)["verified"])
slope = {"main_z": [4749.0 - 3 * k for k in range(11)], "post_spawn_z": [4749.0 - 3 * k for k in range(11)],
         "surface": hump["surface"]}
check("verdict non-level ignores spread", L._regen_verdict(slope)["verified"] and not L._regen_verdict(slope)["level_path"])
check("verdict no meshes fails", not L._regen_verdict(dict(good, surface={"surface": None, "meshes": {}}))["verified"])
check("verdict no post fails", not L._regen_verdict(dict(good, post_spawn_z=None))["verified"])

class FakeWorld:
    def __init__(self, path): self.path = path
    def get_path_name(self): return self.path
class FakePath:
    def __init__(self, label, name): self.label, self.name = label, name
    def get_actor_label(self): return self.label
    def get_name(self): return self.name
W = FakeWorld(L.DEFAULT_WORLD)
P = FakePath("EpPath2049124704", "EpPath_UAID_X_1673543300")
bridge = mock.MagicMock()
lib = mock.MagicMock()
L.unreal.EpPathsLibrary = lib
snap = dict(good, adjustment_state={"update_in_progress": False})
def run(**kw):
    patches = dict(pie=None, world=W, actors=[P], compiled=32767, snap=snap)
    patches.update(kw.pop("_p", {}))
    with mock.patch.object(L._el, "_pie_guard", return_value=patches["pie"]),          mock.patch.object(L._el, "_editor_world", return_value=patches["world"]),          mock.patch.object(L._el, "_bridge", return_value=bridge),          mock.patch.object(L, "_all", return_value=patches["actors"]),          mock.patch.object(L, "_main_spline", return_value=object()),          mock.patch.object(L, "_compiled_all_mask", return_value=patches["compiled"]),          mock.patch.object(L, "_path_snapshot", return_value=patches["snap"]),          mock.patch.object(L, "_edit_mode_active", return_value=False):
        return L.path_regenerate(**kw)
r = run(path="EpPath2049124704", _p={"pie": {"ok": False, "refused": True, "error": "PIE is running"}})
check("regen refuses PIE (dry run too)", r.get("refused") and "PIE" in r["error"])
r = run(path="EpPath2049124704", _p={"world": FakeWorld("/Game/Other/Map.Map")})
check("regen refuses other world", r.get("refused") and "expected" in r["error"])
r = run(path="Nope")
check("regen refuses no match", r.get("refused") and "No loaded EpPath" in r["error"])
r = run(path="EpPath2049124704", _p={"actors": [P, FakePath("EpPath2049124704", "Other")]})
check("regen refuses two matches", r.get("refused") and "matches 2" in r["error"])
r = run(path="EpPath2049124704", _p={"compiled": 65535})
check("regen refuses mask mismatch", r.get("refused") and "compiled" in r["error"])
r = run(path="EpPath2049124704", _p={"snap": dict(snap, adjustment_state={"update_in_progress": True})})
check("regen refuses update in progress", r.get("refused") and "in progress" in r["error"])
lib.reset_mock()
r = run(path="EpPath_UAID_X_1673543300")
check("regen dry run by name", r["ok"] and r["dry_run"] and r["update_mask"] == 32767 and r["path"] == "EpPath2049124704")
check("regen dry run makes no call", not lib.update_path_based_on_mask.called)
check("regen dry run defaults", r["skip_biomes"] is True and r["skip_physics"] is False and r["verdict_before"]["verified"])
r = run(path="EpPath2049124704", dry_run=False)
check("regen real run calls lib once", lib.update_path_based_on_mask.call_count == 1
      and lib.update_path_based_on_mask.call_args[0][1:] == (32767, True, False) and r["verified"])
lib.update_path_based_on_mask.return_value = None
r = run(path="EpPath2049124704", dry_run=False)
check("regen None result not verified", not r["ok"] and not r["verified"] and "returned None" in r["error"])

# --- heightmap blend_method / offset_method / z_offset (2026-10-08)
spec, g = L._field_spec(blend_method="Min", offset_method="BrushZLocation", z_offset=0)
check("field_spec hm blend/offset/z", g is None and spec["hm"] == {"blend_method": "MIN", "offset_method": "BRUSH_Z_LOCATION", "z_offset": 0.0})
spec, g = L._field_spec(blend_method="AddAndSubtract")
check("field_spec AddAndSubtract", g is None and spec["hm"]["blend_method"] == "ADD_AND_SUBTRACT")
check("field_spec z_offset alone not empty", not L._spec_empty(L._field_spec(z_offset=-15700)[0]))
for bad in ({"blend_method": "Minimum"}, {"blend_method": 3}, {"offset_method": "Absolute"}, {"z_offset": "0"}, {"z_offset": True}):
    s_, g = L._field_spec(**bad)
    check(f"field_spec refuses {bad}", s_ is None and g.get("refused"))
p = copy.deepcopy(snap); p["z_offset"] = -15700.0; p["offset_method"] = "MANUAL"
L._plan_blend(p, {"blend_method": "MIN", "offset_method": "BRUSH_Z_LOCATION", "z_offset": 0.0})
check("plan hm blend/offset/z", p["blend_method"] == "MIN" and p["offset_method"] == "BRUSH_Z_LOCATION" and p["z_offset"] == 0.0)
bs = mock.MagicMock()
L._apply_blend(bs, {"blend_method": "MIN", "offset_method": "BRUSH_Z_LOCATION", "z_offset": 0.0})
names = [c.args[0] for c in bs.set_editor_property.call_args_list]
check("apply hm blend/offset/z", names == ["blend_method", "offset_method", "z_offset"]
      and bs.set_editor_property.call_args_list[2].args[1] == 0.0)
# spawn dig guard: a passed blend_method overrides the class default
cdo = {"blend_method": "ADD_AND_SUBTRACT", "invert": False, "multiplier": 0.6}
maxb = [{"label": "Continent", "component": "c", "priority": 5, "blend_method": "MAX"}]
def spawn(**kw):
    with mock.patch.object(L, "_world_guard", return_value=(W, None)), \
         mock.patch.object(L._el, "_pie_guard", return_value=None), \
         mock.patch.object(L, "_edit_mode_active", return_value=True), \
         mock.patch.object(L, "_brush_load_state", return_value=(10, 10)), \
         mock.patch.object(L, "_backup_state", return_value={"ok": True}), \
         mock.patch.object(L, "_resolve_stamp_class", return_value=({"package": "/Game/ErrantWorlds/ErrantLandscapeStampLibrary/X/WideCanyon", "loaded_class": object()}, None)), \
         mock.patch.object(L, "_proxies_overlapping", return_value=["P_10_8"]), \
         mock.patch.object(L, "_cdo_heightmap", return_value=cdo), \
         mock.patch.object(L, "_brushes_covering", return_value=maxb), \
         mock.patch.object(L, "_all", return_value=[]):
        # The fixture is a real rotated stamp (yaw -136.9), clipped by its unrotated bounds: these checks are about
        # the dig rule, so the rotation clip is allowed unless a check says otherwise.
        args = dict(stamp_class="/Game/ErrantWorlds/ErrantLandscapeStampLibrary/X/WideCanyon", x=550800, y=141400,
                    yaw=-136.9, scale=[457, 130, 0.141], location_z=7502, priority=6, invert=False, multiplier=0.6,
                    allow_rotation_clip=True)
        args.update(kw)
        return L.stamp_spawn(**args)
def dig_guard(r):
    return next(gg for gg in r["guards"] if gg["guard"] == "dig_priority")
r = spawn(priority=5)
check("spawn add at 5 over Max 5: order-dependent, refused", not r["ok"] and dig_guard(r)["detail"]["digs"] is False
      and r["failed_guards"] == ["equal_priority"])
r = spawn(priority=5, allow_equal_priority=True)
check("spawn add at 5 over Max 5 allowed: no dig, ok", r["ok"] and dig_guard(r)["detail"]["digs"] is False)
r = spawn(priority=6, allow_rotation_clip=False)
check("spawn rotated fixture: rotation_clip refused", not r["ok"] and r["failed_guards"] == ["rotation_clip"])
r = spawn(priority=6, allow_rotation_clip=False, fit_falloff_to_bounds=True)
rc = next(gg for gg in r["guards"] if gg["guard"] == "rotation_clip")["detail"]
check("spawn fit_falloff_to_bounds fits", r["ok"] and not rc["clipped"] and rc["applied_fix"]["scale"][0] > 457)
r = spawn(priority=5, allow_rotation_clip=False)
check("every caller enforces clip and order without a lock argument",
      not r["ok"] and r["failed_guards"] == ["rotation_clip", "equal_priority"])
r = spawn(priority=5, blend_method="Min")
check("spawn Min at 5 over Max 5: dig conflict", not r["ok"] and dig_guard(r)["detail"]["digs"] is True
      and "dig_priority" in r["failed_guards"])
r = spawn(priority=6, blend_method="Min", offset_method="BrushZLocation", z_offset=0)
check("spawn Min at 6 over Max 5: ok", r["ok"] and dig_guard(r)["detail"]["digs"] is True and not r["failed_guards"])
r = spawn(priority=6, blend_method="Mn")
check("spawn refuses bad blend", r.get("refused") and "blend_method" in r["error"])

# --- stamp_configure priority + dig guard; multiplier 0 (2026-10-08, Revision 2)
spec, g = L._field_spec(multiplier=0, blend_method="Min")
check("multiplier 0 accepted", g is None and spec["hm"]["multiplier"] == 0)
snap0 = {"location": [550800.0, 141400.0, 7502.0], "scale": [457.0, 130.0, 0.141], "yaw": -136.9, "priority": 6,
         "heightmap": {"invert": False, "multiplier": 0.6, "falloff": True, "falloff_start_xy": [0.9, 0.9],
                       "falloff_end_xy": [1.0, 1.0], "falloff_noise": 0.0, "offset_method": "BRUSH_Z_LOCATION",
                       "z_offset": 0.0, "levels": {"input": [0, 1], "output": [0, 1]}, "clamp_output": False,
                       "clamp": [-1, 1], "blend_method": "MIN", "lod_bias": 6, "source": "TEXTURE",
                       "texture": "/T", "channel": "RED"},
         "weightmaps": {}}
class FakeActor:
    def get_actor_label(self): return "PRK_HW-G1.1"
    def get_class(self):
        c = mock.MagicMock(); c.get_name.return_value = "WideCanyon_C"; return c
cov = [{"label": "Cont", "component": "c", "priority": 7, "blend_method": "MAX"},
       {"label": "PRK_HW-G1.1", "component": "s", "priority": 6, "blend_method": "MIN"}]
def conf(**kw):
    with mock.patch.object(L, "_world_guard", return_value=(W, None)), \
         mock.patch.object(L, "_resolve_stamp", return_value=(FakeActor(), object(), None)), \
         mock.patch.object(L, "_stamp_snapshot", return_value=copy.deepcopy(snap0)), \
         mock.patch.object(L, "_proxies_overlapping", return_value=["P_10_8"]), \
         mock.patch.object(L, "_edit_mode_active", return_value=False), \
         mock.patch.object(L, "_wm_bridge_refusal", return_value=([], None)), \
         mock.patch.object(L, "_brushes_covering", return_value=copy.deepcopy(cov)):
        return L.stamp_configure(actor="PRK_HW-G1.1", **kw)
r = conf(priority=7)
check("configure dig at 7 under Max 7 refused", r.get("refused") and r["dig_priority"]["conflicts"][0]["label"] == "Cont")
r = conf(priority=8)
check("configure dig at 8 over Max 7 ok", r["ok"] and r["dry_run"] and not r["dig_priority"]["conflicts"]
      and {"field": "priority", "before": 6, "after": 8} in r["diff"])
check("configure guard excludes the stamp itself", all(c["label"] != "PRK_HW-G1.1" for c in r["dig_priority"]["covering"]))
r = conf(lod_bias=4)
check("configure without dig fields skips the guard", r["ok"] and "dig_priority" not in r)
r = conf(priority=-1)
check("configure refuses negative priority", r.get("refused"))
r = conf(priority=True)
check("configure refuses bool priority", r.get("refused"))
r = conf(blend_method="AddAndSubtract", invert=False)
check("configure non-dig skips conflicts", r["ok"] and r["dig_priority"]["digs"] is False)
r = spawn(priority=6, blend_method="Min", multiplier=0, scale=[3360, 3360, 0.1], location_z=9800)
check("spawn cap multiplier 0 Min ok", r["ok"] and dig_guard(r)["detail"]["digs"] is True)
# Custom numerical height sources must retain the existing brush guards and the
# detached-struct write-back contract.
for bad in ({}, {"source": "NOISE"}, {"texture": "C:/height.png"},
            {"input_type": "LandscapeHeightmap"}, {"input_type": 0}):
    spec, refusal = L._field_spec(heightmap=bad)
    check(f"heightmap refuses {bad}", spec is None and refusal.get("refused"))
with mock.patch.object(L.unreal.EditorAssetLibrary, "does_asset_exist", return_value=False):
    spec, refusal = L._field_spec(heightmap={"texture": "/Game/Missing"})
    check("heightmap missing asset refused", spec is None and refusal.get("refused"))
spec, refusal = L._field_spec(heightmap={"texture": "/Game/T_Grade", "input_type": "StandardFullRange"})
check("heightmap source alone is a change", refusal is None and not L._spec_empty(spec))
planned, _, refusal = L._plan_fields(copy.deepcopy(snap0), object(), spec)
check("heightmap plans grayscale signed source", refusal is None and planned["heightmap"]["texture"] == "/Game/T_Grade.T_Grade"
      and planned["heightmap"]["input_type"] == "STANDARD_FULL_RANGE" and planned["heightmap"]["channel"] == "RED")
with mock.patch.object(L, "_brushes_covering", return_value=cov):
    planned["heightmap"].update(blend_method="ADD_AND_SUBTRACT", invert=False)
    check("signed texture still obeys dig priority", L._dig_priority_check(W, planned)["conflicts"])
r = spawn(priority=5, heightmap={"input_type": "StandardFullRange"})
check("signed spawn cannot hide a dig behind invert false", not r["ok"] and "dig_priority" in r["failed_guards"])
with mock.patch.object(L, "_write_guard", return_value=(W, None)):
    r = conf(heightmap={"input_type": "StandardFullRange"}, dry_run=False)
    check("heightmap real change refused outside edit mode", r.get("refused") and "active" in r["error"])

class Detached:
    def __init__(self, **values): self.values = values
    def get_editor_property(self, key): return copy.deepcopy(self.values[key])
    def set_editor_property(self, key, value): self.values[key] = copy.deepcopy(value)
settings = Detached(input_type="old", lod_bias=6, uv_scale="preserved")
hp = Detached(blend_settings2=Detached(multiplier=.7), stamp_settings2=settings,
              texture_source_params=Detached(texture="old", color_channel="old"), source="old")
comp = Detached(heightmap_params=hp, weightmaps_params={})
L._write_fields(None, comp, spec, None, None, "new-texture")
written = comp.values["heightmap_params"].values
check("heightmap writes entire texture and sampling structs", written["texture_source_params"].values["texture"] == "new-texture"
      and written["stamp_settings2"].values["input_type"] == L.unreal.ErrantLandscapeStampBrushInput.STANDARD_FULL_RANGE
      and written["stamp_settings2"].values["uv_scale"] == "preserved"
      and written["blend_settings2"].values["multiplier"] == .7 and comp.values["weightmaps_params"] == {})

# Heightmap levels/clamp reuse the guarded blend path, rather than swapping the source.
blend_spec, refusal = L._field_spec(heightmap={"levels": {"input": [.02, .08], "output": [0, 1]}, "clamp": [0, .8]})
check("heightmap levels/clamp accepted without source replacement", refusal is None and "heightmap_source" not in blend_spec
      and blend_spec["hm"] == {"levels": {"input": [.02, .08], "output": [0., 1.]}, "clamp": [0., .8]}
      and not L._spec_empty(blend_spec))
planned, _, refusal = L._plan_fields(copy.deepcopy(snap0), object(), blend_spec)
check("heightmap levels/clamp planned and source preserved", refusal is None
      and planned["heightmap"]["levels"] == blend_spec["hm"]["levels"]
      and planned["heightmap"]["clamp_output"] and planned["heightmap"]["clamp"] == [0., .8]
      and planned["heightmap"]["texture"] == snap0["heightmap"]["texture"])
diff_fields = {d["field"] for d in L._diff(snap0, planned)}
check("heightmap levels/clamp included in readback diff", diff_fields == {"heightmap.levels.input", "heightmap.clamp", "heightmap.clamp_output"})
disable_spec, refusal = L._field_spec(heightmap={"clamp": None})
check("heightmap null disables clamp", refusal is None and disable_spec["hm"] == {"clamp": None})
L._plan_blend(planned["heightmap"], disable_spec["hm"])
check("heightmap null plans clamp disabled without discarding limits", not planned["heightmap"]["clamp_output"]
      and planned["heightmap"]["clamp"] == [0., .8])
partial_spec, refusal = L._field_spec(heightmap={"levels": {"input": [.01, .1]}})
check("heightmap omitted output defaults to identity", refusal is None and partial_spec["hm"]["levels"]["output"] == [0., 1.])
_, refusal = L._field_spec(heightmap={"levels": {"output": [1, 0]}})
check("heightmap finite output can be reversed", refusal is None)
_, refusal = L._field_spec(heightmap={"clamp": [.1, .1]})
check("heightmap constant output clamp accepted", refusal is None)
bad_levels = [None, [], "bad", {}, {"unknown": [0, 1]},
              {"input": None}, {"input": [0]}, {"input": [0, 1, 2]},
              {"input": [False, 1]}, {"input": ["0", 1]},
              {"input": [0, float("nan")]}, {"input": [0, float("inf")]},
              {"input": [0, 0]}, {"input": [1, 0]},
              {"output": [0, float("inf")]}, {"output": [float("nan"), 1]},
              {"output": [0, "1"]}, {"output": [0, True]}]
for index, value in enumerate(bad_levels):
    invalid_spec, refusal = L._field_spec(heightmap={"levels": value})
    check(f"heightmap malformed/nonfinite levels {index} refused", invalid_spec is None and refusal.get("refused"))
for index, value in enumerate(([0], [0, 1, 2], [1, 0], [0, "1"], [0, True], [0, float("nan")], [0, float("inf")])):
    invalid_spec, refusal = L._field_spec(heightmap={"clamp": value})
    check(f"heightmap malformed/nonfinite clamp {index} refused", invalid_spec is None and refusal.get("refused"))
vector2 = lambda x, y: (x, y)
hp = Detached(blend_settings2=Detached(levels=Detached(input_levels=(0, 1), output_levels=(0, 1)),
                                     multiplier=.7, clamp_output=False, clamp=(-1, 1)),
              stamp_settings2=settings, texture_source_params=Detached(texture="preserved"), source="preserved")
comp = Detached(heightmap_params=hp, weightmaps_params={})
with mock.patch.object(L.unreal, "Vector2D", side_effect=vector2):
    L._write_fields(None, comp, blend_spec, None, None)
written = comp.values["heightmap_params"].values
check("heightmap levels/clamp detached structs written back", written["blend_settings2"].values["levels"].values
      == {"input_levels": (.02, .08), "output_levels": (0., 1.)}
      and written["blend_settings2"].values["clamp_output"] and written["blend_settings2"].values["clamp"] == (0., .8)
      and written["texture_source_params"].values["texture"] == "preserved")
r = conf(heightmap={"levels": {"input": [.01, .1]}})
check("heightmap levels changes recheck Min dig priority", r.get("refused") and r["dig_priority"]["conflicts"])
r = conf(blend_method="Max", heightmap={"clamp": [0, .8]})
check("final output clamp on Max still obeys dig priority", r.get("refused") and r["dig_priority"]["digs"])
r = conf(blend_method="Max", heightmap={"clamp": None})
check("disabled output clamp allows nondig Max", r["ok"] and not r["dig_priority"]["digs"])
r = spawn(priority=5, blend_method="Max", heightmap={"clamp": [0, .8]})
check("spawn Max output clamp obeys dig priority", not r["ok"] and "dig_priority" in r["failed_guards"])
print("ALL OK" if ok else "SOME FAILED")
if not ok: raise SystemExit(1)
