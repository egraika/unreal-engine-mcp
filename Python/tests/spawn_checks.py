import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
import os, sys, json, tempfile, time
sys.path.insert(0, _CONTENT_PY)
sys.path.insert(0, _PY)
import prk_terrain_geom as g
from helpers import prk_python_guard as pg, prk_backup as bk
fails = []
def check(n, c):
    print(("PASS " if c else "FAIL ") + n)
    if not c: fails.append(n)
check("yaw 180", g.normalize_yaw(180) == 180 and g.normalize_yaw(-180) == 180 and g.normalize_yaw(540) == 180)
check("yaw -190", abs(g.normalize_yaw(-190) - 170) < 1e-9 and abs(g.normalize_yaw(359) + 1) < 1e-9)
check("digs invert add/sub", g.stamp_digs(True, "ADD_AND_SUBTRACT", 0.7, 0.036) is True)
check("raises non-invert", g.stamp_digs(False, "ADD_AND_SUBTRACT", 0.7, 1.0) is False)
check("digs negative multiplier", g.stamp_digs(False, "ADD_AND_SUBTRACT", -0.5, 1.0) is True)
check("max never digs", g.stamp_digs(True, "MAX", 1, 1) is False)
check("min digs", g.stamp_digs(None, "MIN", None) is True)
check("replace counted as dig", g.stamp_digs(False, "REPLACE", 1, 1) is True)
check("unknown blend -> None", g.stamp_digs(True, None, 0.7) is None)
check("unknown invert -> None", g.stamp_digs(None, "ADD_AND_SUBTRACT", 0.7) is None)
cov = [{"label": "Plain", "blend_method": "MAX", "priority": 5}, {"label": "Noise", "blend_method": "ADD_AND_SUBTRACT", "priority": 50},
       {"label": "Mesa", "blend_method": "MAX", "priority": 10}]
check("conflict at equal priority", [c["label"] for c in g.priority_conflicts(10, cov)] == ["Mesa"])
check("no conflict above", g.priority_conflicts(11, cov) == [])
now = 1_000_000.0
ents = [("a", {"world": "ErrantWorld", "unix": now - 3600 * 5}), ("b", {"world": "ErrantWorld", "unix": now - 3600}),
        ("c", {"world": "Other", "unix": now}), ("d", {"world": "ErrantWorld", "unix": "bad"})]
r = g.newest_backup(ents, now, 12, "ErrantWorld")
check("newest backup picks b", r["ok"] and r["path"] == "b" and r["age_h"] == 1.0)
check("backup too old", not g.newest_backup(ents, now + 3600 * 12, 12, "ErrantWorld")["ok"])
check("no backup", not g.newest_backup([], now, 12, "ErrantWorld")["ok"])
deny = ["eas.spawn_actor_from_class(unreal.load_class(None, '/Game/ErrantWorlds/ErrantLandscapeStampLibrary/Lake/Lake_5.Lake_5_C'), v, r)",
        "for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ElBrushActor): eas.destroy_actor(a)",
        "unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.ElStampBrushActor, v)",
        "a = find('Lake_5'); a.destroy()  # ElBrushActor"]
allow = ["eas.spawn_actor_from_class(unreal.StaticMeshActor, v, r)",
         "print(len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ElBrushActor)))",
         "import prk_lake_ops; prk_lake_ops.stamp_spawn(stamp_class='/Game/ErrantWorlds/ErrantLandscapeStampLibrary/Lake/Lake_5', x=0, y=0, yaw=0, scale=[1,1,1], location_z=-100, priority=1)"]
for c in deny: check("deny " + c[:50], any(h["rule"] == "el_brush_spawn_destroy" for h in pg.check_python_source(c)))
for c in allow: check("allow " + c[:50], not pg.check_python_source(c))
# backup on a fake project tree
root = tempfile.mkdtemp(prefix="prk_bk_")
open(os.path.join(root, "Fake.uproject"), "w").write("{}")
src = os.path.join(root, bk.WORLDS["ErrantWorld"][0])
os.makedirs(os.path.join(src, "1", "AB"))
for i in range(5):
    open(os.path.join(src, "1", "AB", f"f{i}.uasset"), "wb").write(os.urandom(1000 + i))
res = bk.backup_external_actors("ErrantWorld", root=root)
print(json.dumps(res))
check("backup ok", res["success"] and res["files"] == 5 and res["bytes"] == 5010 and os.path.isfile(res["ok_file"]))
rec = json.load(open(res["ok_file"]))
check("backup record", rec["world"] == "ErrantWorld" and rec["files"] == 5)
check("backup source untouched", bk.tree_size(src) == (5, 5010))
check("backup unknown world", not bk.backup_external_actors("Nope", root=root)["success"])
print("FAILS:", fails)
