import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_sys.path.insert(0, _os.path.join(_ROOT, "Content", "Python"))
import prk_zone_terrain_place as Z
fails = []
def check(n, c):
    print(("PASS " if c else "FAIL ") + n)
    if not c: fails.append(n)
doc = _os.path.join(_ROOT, "Docs", "zone-terrain-stonewaste-hollow-weald-2026-10-07.md")
hdr, rows = Z.parse_table(doc, "### Stamp table (52 stamps)", "### Composite result")
check("52 rows", len(rows) == 52 and hdr[0] == "ID")
paths = {r["Class"]: "/Game/X/" + r["Class"][:-2] for r in rows.values()}
a = Z.spawn_args(rows["HW-G1.1"], paths)
check("G1.1 args", a["x"] == 550800 and a["y"] == 141400 and a["yaw"] == -136.9 and a["scale"] == [457, 130, 0.141]
      and a["location_z"] == 7502 and a["blend_method"] == "Min" and a["priority"] == 6 and a["z_offset"] == 0
      and a["falloff_start"] == 0.9 and a["falloff_noise"] == 0 and a["lod_bias"] == 6 and a["label"] == "PRK_HW-G1.1")
h = Z.spawn_args(rows["HW-H2"], paths)
check("H2 args", h["invert"] and h["falloff_start"] == 0 and h["falloff_noise"] == 1 and h["blend_method"] == "AddAndSubtract")
check("R1c negative Y", Z.spawn_args(rows["SW-R1c"], paths)["y"] == -335000)
try:
    Z.parse_table(doc, "## 1. Design intent", "## 2.")
    check("no table raises", False)
except ValueError:
    check("no table raises", True)

# --- Revision 2 (2026-10-08)
check("falloff cell plain", Z.falloff_cell("0.50") == (0.5, 0.0))
check("falloff cell noise 1", Z.falloff_cell("0.00, noise 1") == (0.0, 1.0))
check("falloff cell noise 0.5", Z.falloff_cell("0.50, noise 0.5") == (0.5, 0.5))
try:
    Z.falloff_cell("half"); check("falloff cell bad raises", False)
except ValueError:
    check("falloff cell bad raises", True)
h2, rows2 = Z.parse_table(doc, *Z.REV2_TABLE)
check("rev2 59 rows", len(rows2) == 59)
paths2 = {r["Class"]: "/Game/X/" + r["Class"][:-2] for r in rows2.values()}
g = Z.spawn_args(rows2["HW-H1"], paths2)
check("rev2 grove noise 0.5", g["falloff_start"] == 0.5 and g["falloff_noise"] == 0.5 and g["priority"] == 7)
c = Z.spawn_args(rows2["HW-U1c"], paths2)
check("rev2 cap", c["multiplier"] == 0 and c["blend_method"] == "Min" and c["location_z"] == 9800 and c["priority"] == 6)
u = Z.configure_args(rows2["HW-U1"])
check("rev2 U1 configure", u["actor"] == "PRK_HW-U1" and u["location_xy"] == [560000, 120000] and u["scale"] == [3200, 3200, 2]
      and u["yaw"] == 45 and u["falloff_start"] == 0.85 and "x" not in u and "stamp_class" not in u)
ids = [i for _, i in Z.REV2_MORNING]
check("morning covers all new rev2 stamps", sorted(set(rows2) - set(ids)) == sorted(["HW-H2", "HW-0a", "HW-G1.1", "SW-R1b", "SW-R3"])
      and len(ids) == len(set(ids)))
check("morning cap before U1", ids.index("HW-U1c") < ids.index("HW-U1") and ids.index("HW-U2c") < ids.index("HW-U2"))
print("FAILS:", fails)
