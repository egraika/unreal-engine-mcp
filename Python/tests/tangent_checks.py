import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
import sys
sys.path.insert(0, _CONTENT_PY)
import prk_terrain_geom as g
fails = []
def check(n, c):
    print(("PASS " if c else "FAIL ") + n)
    if not c: fails.append(n)
check("level all", g.level_indices(True, 3) == [0, 1, 2])
check("level none", g.level_indices(False, 3) == [] and g.level_indices(None, 3) == [])
check("level list sorted", g.level_indices([2, 0], 3) == [0, 2])
for bad in ([3], [-1], [0, 0], [True], "1", 1):
    try:
        g.level_indices(bad, 3); check(f"level rejects {bad!r}", False)
    except ValueError:
        check(f"level rejects {bad!r}", True)
check("keys 2 pts", g.profile_keys(2) == [k / 10 for k in range(11)])
check("keys 3 pts end", g.profile_keys(3)[-1] == 2.0 and len(g.profile_keys(3)) == 11)
pts = [[0, 0, 100], [1000, 0, 200]]
flat = [[1000, 0, 0], [1000, 0, 0]]
check("hermite ends", g.hermite_z(pts, flat, flat, 0.0) == 100 and g.hermite_z(pts, flat, flat, 1.0) == 200)
check("hermite flat tangents mid", abs(g.hermite_z(pts, flat, flat, 0.5) - 150) < 1e-9)
lin = [[1000, 0, 100], [1000, 0, 100]]
check("hermite linear tangents linear", all(abs(g.hermite_z(pts, lin, lin, k / 10) - (100 + 10 * k)) < 1e-9 for k in range(11)))
over = [[1000, 0, 2000], [1000, 0, -2000]]
zs = [g.hermite_z(pts, over, over, k / 10) for k in range(11)]
check("steep tangents overshoot", max(zs) > 200 or min(zs) < 100)
check("leveled tangents stay in range", all(100 - 1e-9 <= g.hermite_z(pts, flat, flat, k / 10) <= 200 + 1e-9 for k in range(11)))
print("FAILS:", fails)
