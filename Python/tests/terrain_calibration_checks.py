"""Offline regression: composed centres and manually retuned beads cannot corrupt texture peak calibration."""
import copy
import os
import sys
from unittest import mock

plugin_python = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
project = os.path.abspath(os.path.join(plugin_python, "..", "..", ".."))
sys.path.insert(0, os.path.join(project, "Content", "Python"))
sys.modules["unreal"] = mock.MagicMock()
import prk_terrain_author as TA

smooth = "/Game/ErrantWorlds/ErrantLandscapeStampLibrary/SmoothHill/T_SmoothHill_H.T_SmoothHill_H"
rocky = "/Game/ErrantWorlds/ErrantLandscapeStampLibrary/RockyHill/T_RockyHill_H.T_RockyHill_H"
args = {"x": 100.0, "y": 200.0, "location_z": 1000.0, "scale": [600.0, 600.0, 0.2],
        "yaw": 0.0, "priority": 9, "invert": False, "multiplier": 0.5, "blend_method": "Max",
        "falloff": True, "falloff_start": 0.4, "falloff_end": 1.0, "falloff_noise": 0.0,
        "offset_method": "BrushZLocation", "z_offset": 0.0, "lod_bias": 4}
snapshot = {"location": [100.0, 200.0, 1000.0], "scale": [600.0, 600.0, 0.2], "yaw": 0.0,
            "priority": 9, "heightmap": {"texture": smooth, "source": "TEXTURE", "invert": False,
            "multiplier": 0.5, "blend_method": "MAX", "falloff": True, "falloff_start_xy": [0.4, 0.4],
            "falloff_end_xy": [1.0, 1.0], "falloff_noise": 0.0, "offset_method": "BRUSH_Z_LOCATION",
            "z_offset": 0.0, "lod_bias": 4}}
plan = {"preset": {"texture": smooth}, "k_peak": 0.949, "mode": "ridge",
        "beads": [{"label": "PRK_Test_00", "x": 100.0, "y": 200.0, "args": args,
                   "plan": {"h_cm": 2000.0, "location_z": 1000.0, "crest_z": 3000.0}}]}


def calibrate(snap, *, record=True, missing=False, centre_z=3000.0):
    writes, traces = [], []
    class Tracer:
        def __init__(self, world):
            pass
        def z(self, x, y):
            traces.append((x, y))
            return centre_z
    resolution = (None, None, {"ok": False, "error": "Missing"}) if missing else (object(), object(), None)
    with mock.patch.object(TA, "_load_plan", return_value=(copy.deepcopy(plan), None)), \
         mock.patch.object(TA, "_load_applied", return_value={"spawned": [{"label": "PRK_Test_00"}]}), \
         mock.patch.object(TA.L, "_world_guard", return_value=(object(), None)), \
         mock.patch.object(TA.L, "_edit_mode_active", return_value=False), \
         mock.patch.object(TA.L, "_resolve_stamp", return_value=resolution), \
         mock.patch.object(TA.L, "_stamp_snapshot", return_value=snap), \
         mock.patch.object(TA.L, "_LandscapeTracer", Tracer), \
         mock.patch.object(TA, "_write_json", side_effect=lambda path, data: writes.append(copy.deepcopy(data))):
        result = TA.feature_calibrate(name="Test", record=record)
    return result, writes, traces


r, writes, traces = calibrate(copy.deepcopy(snapshot), record=False, centre_z=2600.0)
assert r["ok"] and not r["recorded"] and not writes and len(traces) == 1
assert r["median_ratio"] == 0.8 and r["centre_response_k"] == 0.7592
assert r["measurement_kind"] == "composite_centres" and r["height_source"] == "collision"
assert "k_corrected" not in r and "k_actual" not in r["beads"][0]
print("PASS below-peak centre remains a labelled composed-centre report")

r, writes, traces = calibrate(copy.deepcopy(snapshot), record=True, centre_z=2600.0)
assert not r["ok"] and r["refused"] and not r["recorded"] and not writes and len(traces) == 1
assert r["centre_response_k"] == 0.7592 and "isolated peak" in r["error"]
print("PASS centre response cannot overwrite a texture peak even with unchanged live beads")

changed = copy.deepcopy(snapshot)
changed["heightmap"]["texture"] = rocky
for record in (False, True):
    r, writes, traces = calibrate(changed, record=record)
    assert not r["ok"] and r["refused"] and not r["recorded"] and not writes and not traces
    assert r["conflicts"][0]["differs"] == ["texture"]
    assert r["conflicts"][0]["actual_texture"] == rocky
print("PASS SmoothHill-to-RockyHill swap cannot report or record SmoothHill calibration")

for field, value in (("multiplier", 0.7), ("invert", True), ("blend_method", "MIN"),
                     ("source", "RENDER_TARGET"), ("offset_method", "MANUAL"), ("z_offset", 100.0),
                     ("falloff", False), ("falloff_start_xy", [0.6, 0.6]),
                     ("falloff_end_xy", [0.8, 0.8]), ("falloff_noise", 0.2), ("lod_bias", 5)):
    changed = copy.deepcopy(snapshot)
    changed["heightmap"][field] = value
    r, writes, traces = calibrate(changed)
    assert not r["ok"] and r["conflicts"] and not writes and not traces, (field, r)
print("PASS changed blend, source, offset, falloff and LOD refuse before sampling or recording")

for field, value in (("scale", [600.0, 600.0, 0.3]), ("location", [100.0, 200.0, 1200.0]),
                     ("yaw", 90.0), ("priority", 12)):
    changed = copy.deepcopy(snapshot)
    changed[field] = value
    r, writes, traces = calibrate(changed)
    assert not r["ok"] and field in r["conflicts"][0]["differs"] and not writes and not traces
print("PASS changed scale, base, orientation and priority cannot use the old response model")

r, writes, traces = calibrate(copy.deepcopy(snapshot), missing=True)
assert not r["ok"] and r["conflicts"][0]["differs"] == ["missing live stamp"] and not writes and not traces
print("PASS missing applied bead refuses without touching calibration")

r, writes, traces = calibrate(copy.deepcopy(snapshot), record=False)
assert r["ok"] and not r["recorded"] and not writes and len(traces) == 1
print("PASS matching read-only calibration remains available")
