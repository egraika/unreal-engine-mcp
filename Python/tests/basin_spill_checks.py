import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_sys.path.insert(0, _os.path.join(_ROOT, "Content", "Python"))
import prk_terrain_geom as g

fails = []
def check(name, cond):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond: fails.append(name)

def grid(nx, ny, f):
    return [[f(c, r) for c in range(nx)] for r in range(ny)]

# --- a plane drains: no basins
plane = grid(20, 20, lambda c, r: 5000.0 + 10.0 * c)
check("tilted plane has no basin", g.basin_spill(plane, 400)["count"] == 0)
flat = grid(10, 10, lambda c, r: 4750.0)
check("flat has no basin", g.basin_spill(flat, 400)["count"] == 0)

# --- one bowl: rim 6000 cm all round, floor 5000, a notch at 5600 on the east rim
def bowl(c, r):
    if c in (0, 10) or r in (0, 10):
        return 5600.0 if (c == 10 and r == 5) else 6000.0
    return 5000.0
b = g.basin_spill(grid(11, 11, bowl), 400, min_xy=[100000, -50000])
one = b["basins"][0] if b["basins"] else {}
check("bowl: one basin", b["count"] == 1)
check("bowl: spill at the notch level", one.get("spill_z_m") == 56.0)
check("bowl: depth 6 m", one.get("max_depth_m") == 6.0)
check("bowl: area 9x9 cells of 4 m", one.get("cells") == 81 and one.get("area_m2") == 81 * 16.0)
check("bowl: volume = 81 cells x 6 m x 16 m2", one.get("volume_m3") == 81 * 6.0 * 16.0)
check("bowl: outlet is the notch", one.get("outlet_rc") == [5, 10] and one.get("outlet_to") == "edge")
check("bowl: outlet xy", one.get("outlet_xy") == [100000 + 10.5 * 400, -50000 + 5.5 * 400])

# --- two pits, the upper spills into the lower, which spills off the grid
def terrace(c, r):
    if r in (0, 8) or c in (0, 16):
        return 9000.0 if c != 0 else (5500.0 if r == 4 else 9000.0)   # west edge notch at 55 m
    if c == 8:
        return 7000.0                                                  # divide between the pits
    return 6000.0 if c > 8 else 5000.0                                 # east pit higher than west pit
t = g.basin_spill(grid(17, 9, terrace), 400)
spills = sorted(x["spill_z_m"] for x in t["basins"])
check("terrace: two basins", t["count"] == 2)
check("terrace: east pit spills over the divide at 70 m", 70.0 in spills)
check("terrace: west pit spills at the 55 m notch", 55.0 in spills)
east = next(x for x in t["basins"] if x["spill_z_m"] == 70.0)
check("terrace: east outlet on the divide, onto terrain", east["outlet_rc"][1] == 8 and east["outlet_to"] == "terrain")

# --- no data: basins are found with it as walls; one that could drain through it is flagged uncertain
def holey(c, r):
    if (c, r) == (5, 5):
        return None
    return 5000.0 if 3 <= c <= 7 and 3 <= r <= 7 else 6000.0
h = g.basin_spill(grid(11, 11, holey), 400)
check("pit with a no-data hole: one uncertain basin", h["count"] == 1 and h["basins"][0]["uncertain"]
      and h["basins"][0]["outlet_to"] == "nodata")
def hole_rim(c, r):
    if (c, r) == (5, 0):
        return None                                                     # missing sample on the north rim
    if c in (0, 10) or r in (0, 10):
        return 6000.0
    return 5000.0
hr = g.basin_spill(grid(11, 11, hole_rim), 400)
check("rim gap at no-data: uncertain, spill is the wall-bounded upper bound",
      hr["count"] == 1 and hr["basins"][0]["outlet_to"] == "nodata" and hr["basins"][0]["spill_z_m"] == 60.0)
check("certain basins say so", not b["basins"][0]["uncertain"])

# --- shallow dips under min_depth are ignored
dimple = grid(9, 9, lambda c, r: 5000.0 - (3.0 if (c, r) == (4, 4) else 0.0))
check("3 cm dimple ignored at 5 cm threshold", g.basin_spill(dimple, 400)["count"] == 0)
check("3 cm dimple found at 1 cm threshold", g.basin_spill(dimple, 400, min_depth_cm=1.0)["count"] == 1)

print("\n%d failure(s)" % len(fails))
_sys.exit(1 if fails else 0)
