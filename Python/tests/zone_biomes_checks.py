import os as _os, sys as _sys
_PY = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))  # Plugins/UnrealMCP/Python
_ROOT = _os.path.abspath(_os.path.join(_PY, "..", "..", ".."))          # project root
_CONTENT_PY = _os.path.join(_ROOT, "Content", "Python")
import sys, struct, math
from unittest import mock
sys.modules["unreal"] = mock.MagicMock()
sys.path.insert(0, _CONTENT_PY)
import prk_zone_biomes as Z

ok = True
def check(n, c):
    global ok
    print(("PASS " if c else "FAIL ") + n); ok &= bool(c)

# --- synthetic zone map: res 12, zone 7 = an L shape + a 1-texel orphan, zone 3 elsewhere
res = 12
g = bytearray([3] * res * res)
for y in range(0, 9):
    for x in range(0, 6):
        g[y * res + x] = 7
for y in range(6, 9):
    for x in range(6, 12):
        g[y * res + x] = 7
g[11 * res + 11] = 7  # orphan
payload = bytearray(res * res * 4)
payload[1::4] = g
data = b"\0" * 20 + struct.pack("<i", res * res * 4) + bytes(payload)
check("read G channel", Z.read_zone_channel(data, res, 20) == bytes(g))
try:
    Z.read_zone_channel(data, res + 1, 20); check("layout guard", False)
except ValueError:
    check("layout guard", True)
comp, sizes = Z.largest_component(bytes(g), res, 7)
check("largest component", len(comp) == 54 + 18 and sizes == [72, 1])
check("orphan dropped", (11 * res + 11) not in comp)
cells = Z.downsample(comp, res, 3)
check("downsample L", cells == {(r, c) for r in range(3) for c in range(2)} | {(2, 2), (2, 3)})
rects = Z.rect_cover(cells)
covered = set()
disjoint = True
for r0, c0, r1, c1 in rects:
    for r in range(r0, r1):
        for c in range(c0, c1):
            disjoint &= (r, c) not in covered
            covered.add((r, c))
check("rect disjoint", disjoint)
check("rect exact cover", covered == cells)
check("rect greedy count", len(rects) == 2 and max((r1 - r0) * (c1 - c0) for r0, c0, r1, c1 in rects) == 6)
# half-in rule: a cell with 4 of 9 texels is out, 5 of 9 in
check("downsample 4/9 out", Z.downsample({0, 1, 2, 12}, 12, 3) == set())
check("downsample 5/9 in", Z.downsample({0, 1, 2, 12, 13}, 12, 3) == {(0, 0)})
# partial edge cell: res 4 -> cell (0,1) has 1x3 texels; 2 of 3 is in
check("downsample edge cell", Z.downsample({3, 7}, 4, 3) == {(0, 1)})

# --- world mapping (texel edges, row = Y, col = X, no flips)
lo, hi = Z.rect_world((0, 0, 1, 1))
check("rect world origin", lo == [-1625600.0, -1625600.0] and math.isclose(hi[0], -1625600 + 3 * Z.TEXEL))
lo, hi = Z.rect_world((417, 417, 418, 418))
check("rect world clamps to res", math.isclose(hi[0], 1625600.0) and math.isclose(lo[0], -1625600 + 1251 * Z.TEXEL))
lo2, hi2 = Z.rect_world((0, 0, 1, 1), inset_cm=1000.0)
check("inset", math.isclose(lo2[0], -1625600 + 1000) and math.isclose(hi2[0], -1625600 + 3 * Z.TEXEL - 1000))
lo3, hi3 = Z.rect_world((0, 0, 1, 1), inset_cm=1e9)
check("inset never inverts", lo3[0] < hi3[0] and lo3[1] < hi3[1])
check("texel recipe centre", math.isclose(Z.texel_edge(0.5), -1624303.668261563, abs_tol=1e-3))
check("world_to_texel", Z.world_to_texel(-1624303.67) == 0 and Z.world_to_texel(1625599.0) == 1253)
check("component index", Z.component_index(-1625600) == 0 and Z.component_index(0.0) == 16 and Z.component_index(2e6) == 31)
check("components single", Z.components_for_boxes([([0.0, 0.0], [10.0, 10.0])], margin_cm=0.0) == [[16, 16]])
check("components dab margin crosses",
      Z.components_for_boxes([([101590.0, 50.0], [101595.0, 60.0])], margin_cm=20.0) == [[16, 16], [17, 16]])

# --- discs
strips = Z.disc_rects(0.0, 0.0, 100.0, 50.0)
check("disc strips count", len(strips) == 4)
check("disc inscribed", all(max(abs(lo[0]), abs(hi[0])) ** 2 + min(abs(lo[1]), abs(hi[1])) ** 2 <= 100.0 ** 2 + 1e-6
                            for lo, hi in strips))
check("disc spans", math.isclose(strips[0][0][1], -100.0) and math.isclose(strips[-1][1][1], 100.0))

# --- literals / parsing
check("object path", Z.object_path("/Game/A/X") == "/Game/A/X.X" and Z.object_path("/Game/A/X.X") == "/Game/A/X.X")
txt = '("/Game/P/EB_Plains.EB_Plains","/Game/P/EB_W.EB_W",None)'
check("parse soft array", Z.parse_soft_paths(txt) == ["/Game/P/EB_Plains.EB_Plains", "/Game/P/EB_W.EB_W", ""])
check("parse soft array unquoted", Z.parse_soft_paths("(/Game/A/B.B,/Game/C/D.D)") == ["/Game/A/B.B", "/Game/C/D.D"])
check("soft literal", Z.soft_array_literal(["/Game/A/B", "/Game/C/D.D"]) == '("/Game/A/B.B","/Game/C/D.D")')
check("asset params literal", Z.asset_params_literal(["/Game/M/SM_A"]) ==
      '((Asset="/Game/M/SM_A.SM_A",AssetProbability=1.000000,bUseAssetBounds=True,AssetRadius=100.000000))')
check("parse asset groups", Z.parse_soft_paths_assets(
    '((Assets=((Asset="/Game/M/SM_A.SM_A",AssetProbability=1.0),(Asset="/Game/M/SM_B.SM_B"))))')
      == ["/Game/M/SM_A.SM_A", "/Game/M/SM_B.SM_B"])
pairs = Z._map_pairs('(("/Game/M/SM_A.SM_A", "/Game/D/DA_X.DA_X"),("/Game/M/SM_B.SM_B","/Game/D/DA_Y.DA_Y"))')
check("map pairs", pairs == [("/Game/M/SM_A.SM_A", "/Game/D/DA_X.DA_X"), ("/Game/M/SM_B.SM_B", "/Game/D/DA_Y.DA_Y")])

# --- species table
names = [s["name"] for s in Z.SPECIES]
check("species unique", len(set(names)) == len(names))
check("species order resolves", all(n in names or n in Z.REUSED_SPECIES for n in Z.STONEWASTE_SPECIES_ORDER))
check("every new species in biome", all(n in Z.STONEWASTE_SPECIES_ORDER for n in names))
wm, dropped = Z.species_weightmaps({"gravel": True}, ["Path_Dirt", "Ground_Dirt", "Ground_Sand", "Ground_Gravel"])
check("weightmaps drop missing layer",
      wm == ["Path_Dirt", "Ground_Dirt", "Ground_Sand", "Ground_Gravel"]
      and dropped == ["Settlement_Flattened", "Ground_Rock"])
wm, _ = Z.species_weightmaps({"gravel": True}, None)
check("vegetation avoids rock (quarry floors)", Z.ROCK in wm and Z.GRAVEL in wm)
wm, _ = Z.species_weightmaps({"gravel": False}, None)
check("weightmaps no gravel", Z.GRAVEL not in wm and Z.ROCK not in wm)
harv = {s["name"] for s in Z.SPECIES if s.get("hism") == "PRKHarvestableBiomesHISMComponent"}
check("harvestable lineage", harv == {"ES_SW_HardyTrees", "ES_SW_StoneNodes", "ES_SW_LimestoneNodes", "ES_SW_CopperTrace"})
check("stone node mesh", next(s for s in Z.SPECIES if s["name"] == "ES_SW_StoneNodes")["meshes"] == [Z.STONE_NODE_MESH])
check("quarry debris 10 meshes", len(next(s for s in Z.SPECIES if s["name"] == "ES_SW_QuarryDebris")["meshes"]) == 10)
p = Z._plan_species(Z.SPECIES[0], None)
check("plan species dont_spawn ends with mask",
      p["dont_spawn"][-1] == Z.object_path(Z.EM_REMOVE_FOLIAGE) and p["scalars"]["MaxSlope"] == 0.5)
check("srgb->linear", math.isclose(Z.srgb_to_linear(1.0), 1.0) and Z.srgb_to_linear(0.5) < 0.22)

# --- real DA_ZoneMap (offline): both zones, rectangles cover exactly the downsampled largest component
path = _os.path.join(_ROOT, "Content", "PRK", "Core", "Data", "World", "DA_ZoneMap.uasset")
raw = open(path, "rb").read()
G = Z.read_zone_channel(raw)
for zone, area in ((36, 15.10), (5, 15.27)):
    plan = Z.compute_zone_plan(raw, zone)
    cells = {tuple(c) for c in plan["cells"]}
    cov = set()
    for r0, c0, r1, c1 in plan["rects"]:
        cov |= {(r, c) for r in range(r0, r1) for c in range(c0, c1)}
    check(f"zone {zone} cover == cells", cov == cells)
    check(f"zone {zone} area", abs(plan["area_km2"] - area) < 0.02)
    inside = Z.cell_lookup(plan)
    check(f"zone {zone} interior pts in zone",
          len(plan["interior_points"]) == 3 and all(
              G[Z.world_to_texel(y) * 1254 + Z.world_to_texel(x)] == zone and inside(x, y) for x, y in plan["interior_points"]))
    ox, oy, oz = plan["outside_point"]
    check(f"zone {zone} outside control", G[Z.world_to_texel(oy) * 1254 + Z.world_to_texel(ox)] == oz != zone)
    total = sum(1 for v in G if v == zone)
    hit = sum(1 for k in range(len(G)) if G[k] == zone and inside(Z.texel_edge(k % 1254 + 0.5), Z.texel_edge(k // 1254 + 0.5)))
    check(f"zone {zone} texel recall {hit}/{total}", hit >= 0.97 * total)
    check(f"zone {zone} boxes valid", all(lo[0] < hi[0] and lo[1] < hi[1] for lo, hi in plan["boxes"]))
p36 = Z.compute_zone_plan(raw, 36)
check("doc bbox Stonewaste", min(lo[0] for lo, hi in p36["boxes"]) < 80000 and max(hi[0] for lo, hi in p36["boxes"]) > 570000)
check("batches", Z.batches([[i, 0] for i in range(30)], 12) ==
      [[[i, 0] for i in range(12)], [[i, 0] for i in range(12, 24)], [[i, 0] for i in range(24, 30)]])

# --- guards (editor mocked)
class W:
    def __init__(s, p): s.p = p
    def get_path_name(s): return s.p
Z._el = mock.MagicMock()
Z._el._pie_guard.return_value = {"ok": False, "refused": True, "error": "PIE"}
Z._el._editor_world.return_value = W("/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld")
w, gd = Z._guard(False, Z.DEFAULT_WORLD)
check("PIE refuses writes", gd is not None and gd.get("refused"))
w, gd = Z._guard(True, Z.DEFAULT_WORLD)
check("PIE allows dry run", gd is None)
Z._el._pie_guard.return_value = None
Z._el._editor_world.return_value = W("/Game/Maps/Other.Other")
w, gd = Z._guard(False, Z.DEFAULT_WORLD)
check("wrong world refuses", gd is not None and gd.get("refused"))
Z._el._editor_world.return_value = None
w, gd = Z._guard(True, Z.DEFAULT_WORLD)
check("no world refuses", gd is not None)
check("create_biome refuses without world", Z.create_biome(dry_run=False).get("refused"))
check("paint unknown zone", Z.paint_zone_biome(99).get("ok") is False)

# --- generation wait keyed to the kick time; survives PRK.log rotation (2026-10-08) ---------------------------------
import os, tempfile, time as _time, datetime
t = Z.parse_log_line("[2026.10.08-17.06.55:869][824]LogBiomes: Generation Done\n")
check("log line parsed as UTC", t is not None and t[1] == "LogBiomes" and t[2] == "Generation Done"
      and abs(t[0] - datetime.datetime(2026, 10, 8, 17, 6, 55, 869000, tzinfo=datetime.timezone.utc).timestamp()) < 1e-6)
check("non-stamped line ignored", Z.parse_log_line("LogWindows: Failed to load") is None)
def stamp(unix):
    d = datetime.datetime.fromtimestamp(unix, datetime.timezone.utc)
    return d.strftime("[%Y.%m.%d-%H.%M.%S:") + f"{d.microsecond // 1000:03d}][ 12]"
logs = tempfile.mkdtemp(prefix="prk_logs_")
kick_t = 1791480000.0
with open(os.path.join(logs, "PRK.log"), "w") as f:
    f.write(stamp(kick_t - 100) + "LogBiomes: Generation Done\n")          # an older run: ignored
kick = Z.kick_marker(logs, now=kick_t)
check("kick records time and log position", kick["unix"] == kick_t and kick["log_size"] > 0)
st = Z.generation_status(kick, logs)
check("old Done before the kick does not count", st["ok"] and st["dones"] == 0 and not st["complete"])
with open(os.path.join(logs, "PRK.log"), "a") as f:
    f.write(stamp(kick_t + 1) + "LogBiomes: Starting generation of 12 components\n")
st = Z.generation_status(kick, logs)
check("started, not complete", st["starts"] == 1 and st["components_started"] == [12] and not st["complete"])
# the editor restarts: PRK.log becomes a backup, a new PRK.log starts (the old count would reset here)
os.replace(os.path.join(logs, "PRK.log"), os.path.join(logs, "PRK-backup-2026.10.08-17.30.00.log"))
with open(os.path.join(logs, "PRK.log"), "w") as f:
    f.write("Log file open\n" + stamp(kick_t + 50) + "LogBiomes: Generation Done\n")
st = Z.generation_status(kick, logs)
check("rotation: Done found in the new log, complete, rotated flagged", st["complete"] and st["dones"] == 1
      and st["rotated"] and len(st["files"]) == 2)
clock = {"t": 0.0}
waited = Z.wait_generation(kick, logs, timeout_s=30, poll_s=5, sleep=lambda s: clock.__setitem__("t", clock["t"] + s),
                           clock=lambda: clock["t"])
check("wait returns at once when complete", waited["complete"] and waited["waited_s"] == 0.0)
with open(os.path.join(logs, "PRK.log"), "a") as f:
    f.write(stamp(kick_t + 60) + "LogBiomes: Starting generation of 4 components\n")
clock["t"] = 0.0
waited = Z.wait_generation(kick, logs, timeout_s=30, poll_s=5, sleep=lambda s: clock.__setitem__("t", clock["t"] + s),
                           clock=lambda: clock["t"])
check("a later Start without Done: wait times out", waited.get("timed_out") and not waited["complete"])
kf = os.path.join(logs, "k.json")
Z.save_kick(kick, kf)
check("kick saved and loaded", Z.load_kick(kf)["unix"] == kick_t)
check("no kick -> error", Z.generation_status(None, logs)["ok"] is False if Z.load_kick() is None else True)
# regenerate(components=...) first-class
with mock.patch.object(Z, "_guard", return_value=(object(), None)):
    r = Z.regenerate(components=[[17, 15], [16, 15]], dry_run=True)
    check("regenerate components only", r["ok"] and r["components"] == [[16, 15], [17, 15]] and r["zones"] == [])
    check("regenerate nothing refused", Z.regenerate(dry_run=True).get("refused"))
    r = Z.regenerate(extra_components=[[1, 2]], components=[[1, 2], [3, 4]], dry_run=True)
    check("extra_components still added (deduped)", r["components"] == [[1, 2], [3, 4]])

print("ALL PASS" if ok else "SOME FAILED")
