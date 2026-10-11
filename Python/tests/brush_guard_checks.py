import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
import os, sys
srv = _PY
sys.path.insert(0, srv); os.chdir(srv)
import unreal_mcp_server_advanced as S
fails = []
def check(n, c):
    print(("PASS " if c else "FAIL ") + n)
    if not c: fails.append(n)
calls = []
def fake(module, func, **kw):
    calls.append((module, func, kw))
    acts = [a for a in kw.get("actors", []) if "Lake_5" in a]
    cls = [c for c in kw.get("classes", []) if "Calysto/Stamps" in c]
    return {"ok": True, "brush_classes": cls, "brush_actors": acts}
S._run_prk_module = fake
def no_dispatch(*a, **k):
    raise AssertionError("dispatched")
S._run_prk = no_dispatch
S.get_unreal_connection = lambda: None
r = S.spawn_actor("/Game/ErrantWorlds/ErrantLandscapeStampLibrary/Lake/Lake_5")
check("spawn_actor stamp library refused w/o editor call", r.get("refused") and not calls)
r = S.spawn_actor("/Game/Calysto/Stamps/BP_Mesa")
check("spawn_actor stamp elsewhere refused via editor check", r.get("refused") and calls[-1][1] == "errant_brush_check")
try:
    S.spawn_actor("/Game/PRK/Props/BP_Crate"); check("spawn_actor normal dispatched", False)
except AssertionError:
    check("spawn_actor normal dispatched", True)
r = S.destroy_actor("actor:Lake_53311149077")
check("destroy_actor brush refused", r.get("refused"))
r = S.delete_actor("Lake_53311149077")
check("delete_actor brush refused", r.get("refused"))
r = S.delete_actor("SM_Rock_12")
check("delete_actor normal proceeds", not r.get("refused"))
r = S.place_actors_batch([{"actor_class": "StaticMeshActor"}, {"type": "ElStampBrushActor"}])
check("place_actors_batch brush refused", r.get("refused") and "errant_stamp_spawn" in str(r.get("use_instead")))
r = S.place_actors_batch([{"actor_class": "StaticMeshActor"}])
check("place_actors_batch normal proceeds", not r.get("refused"))
S._run_prk_module = lambda *a, **k: {"success": False, "message": "No result"}
check("unverifiable refused", S.delete_actor("X").get("refused"))
calls.clear()
S._run_prk_module = lambda module, func, **kw: calls.append((module, func, kw)) or {"ok": True}
S.errant_path_regenerate("EpPath2049124704")
check("regen wrapper dispatch", calls[-1][:2] == ("prk_lake_ops", "path_regenerate")
      and calls[-1][2]["dry_run"] is True and calls[-1][2]["skip_biomes"] is True and calls[-1][2]["skip_physics"] is False)
S.errant_path_set_points("EpPath2049124704", level_tangents=True)
check("set_points rebuild default False", calls[-1][1] == "path_set_points" and calls[-1][2]["rebuild"] is False)
print("FAILS:", fails)
