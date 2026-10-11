"""Isolated ownership and save-guard checks; no live Unreal automation."""
from pathlib import Path
import base64
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "Content/Python"))
for name in ("unreal", "prk_errant", "prk_zone_biomes"):
    sys.modules[name] = types.ModuleType(name)
import prk_biome_generation_save as G


class GenerationChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(mock.patch.stopall)
        self.world = types.SimpleNamespace(get_path_name=lambda: "/Game/World.World")
        self.dirty = {"/Game/Old", "/Game/Output"}
        self.reasons = {"/Game/Output": ["generated"]}
        self.kwargs = {"component_xys": [[1, 2]], "species_paths": ["/Game/Species.Species"],
                       "dirty_before": ["/Game/Old"]}
        mock.patch.object(G.R, "_guard", return_value=(self.world, None)).start()
        mock.patch.object(G.R, "_root", return_value=Path(self.temp.name)).start()
        mock.patch.object(G.R.Z, "_generation_in_progress", return_value=False, create=True).start()
        mock.patch.object(G.R.E, "_dirty_package_names", side_effect=lambda: set(self.dirty), create=True).start()
        mock.patch.object(G, "_discover", side_effect=lambda *args: self.reasons).start()

    def test_native_component_and_required_asset_exports_are_strict(self):
        self.assertEqual(G._native_xys("((X=1,Y=2),(X=-3,Y=4))"), {(1, 2), (-3, 4)})
        self.assertEqual(G._refs('(\"/Game/Grid.Grid\")'), ["/Game/Grid.Grid"])
        with self.assertRaises(ValueError):
            G._native_xys("((X=1,Y=2),BadMetadata)")
        with self.assertRaises(ValueError):
            G._refs("(UnresolvedReference)")

    def test_native_pcg_ring_is_exactly_one_ring(self):
        ring = G._dependency_xys({(1, 2)})
        self.assertEqual(len(ring), 9)
        self.assertIn((0, 1), ring)
        self.assertIn((2, 3), ring)
        self.assertNotIn((3, 2), ring)

    def test_grid_identity_matches_native_dimensions_and_seed_not_name(self):
        values = {"NoCollisionRadius": "4.500000", "CellSize": "510", "CellEdgeSize": "64", "Seed": "2"}
        with mock.patch.object(G.R.Z, "_get_text", side_effect=lambda obj, name: values[name], create=True):
            key = (4.5, 510, 64.0, 2)
            self.assertEqual(G._matching_grid_key(object(), {key}), key)
            self.assertIsNone(G._matching_grid_key(object(), {(4.5, 510, 64.0, 3)}))
            with self.assertRaisesRegex(ValueError, "Ambiguous"):
                G._matching_grid_key(object(), {key, (4.5000001, 510, 64.0, 2)})

    def test_preexisting_dirty_package_is_preserved(self):
        result = G.plan(**self.kwargs)
        self.assertTrue(result["ok"])
        self.assertEqual(result["owned_packages"], ["/Game/Output"])
        self.assertEqual(result["baseline_scope_conflicts"], [])
        self.assertEqual(list(Path(self.temp.name).iterdir()), [])

    def test_preexisting_dirty_scope_conflict_refuses_complete_save(self):
        self.reasons["/Game/Old"] = ["also generated"]
        result = G.plan(**self.kwargs)
        self.assertFalse(result["ok"])
        self.assertEqual(result["owned_packages"], ["/Game/Output"])
        self.assertEqual(result["baseline_scope_conflicts"], ["/Game/Old"])

    def test_deleted_output_uses_native_single_cell_descriptor_only(self):
        tail = struct.pack("<Iqqq4I", 102000, -3, -4, -1, 0, 0, 0, 0)
        box = struct.pack("<6dB", -304800, -406400, 0, -202800, -304400, 100, 1)
        metadata = base64.b64encode(bytes(100) + tail + box).decode()
        self.assertEqual(G._deleted_single_cell(metadata, [-1625600, -1625600], [101600, 101600]), (13, 12))
        broad = struct.pack("<6dB", -304800, -406400, 0, -101200, -304400, 100, 1)
        with self.assertRaisesRegex(ValueError, "one exact"):
            G._deleted_single_cell(base64.b64encode(bytes(100) + tail + broad).decode(),
                                   [-1625600, -1625600], [101600, 101600])
        with self.assertRaisesRegex(ValueError, "current native"):
            G._deleted_single_cell(metadata, [1, 1], [101600, 101600])

    def test_unrecognized_new_dirty_package_refuses_plan(self):
        self.dirty.add("/Game/Foreign")
        result = G.plan(**self.kwargs)
        self.assertFalse(result["ok"])
        self.assertEqual(result["unresolved_new_dirty"], ["/Game/Foreign"])

    def test_save_rechecks_exact_output_identity_and_preserves_foreign(self):
        planned = G.plan(**self.kwargs, record=True)
        packages = {p: types.SimpleNamespace(get_name=lambda p=p: p) for p in self.dirty}
        saved = []
        def save(packages, only_dirty):
            saved.extend(p.get_name() for p in packages)
            self.dirty.difference_update(saved)
            return True
        with mock.patch.object(G, "_dirty_packages", return_value=packages), \
             mock.patch.object(G.R.E, "_package_file", return_value=None, create=True), \
             mock.patch.object(G.unreal, "EditorLoadingAndSavingUtils", types.SimpleNamespace(save_packages=save), create=True):
            result = G.save(receipt=planned["receipt"])
        self.assertTrue(result["ok"], result)
        self.assertEqual(saved, ["/Game/Output"])
        self.assertEqual(self.dirty, {"/Game/Old"})

    def test_new_dirty_after_receipt_refuses_save_before_any_writer(self):
        planned = G.plan(**self.kwargs, record=True)
        self.dirty.add("/Game/LaterWork")
        with mock.patch.object(G, "_dirty_packages") as collect:
            result = G.save(receipt=planned["receipt"])
        self.assertFalse(result["ok"])
        collect.assert_not_called()

    def test_clean_dirty_flag_does_not_hide_stale_deleted_actor_file(self):
        self.reasons["/Game/Output"] = ["destroyed native output; retained partition descriptor component [1, 2]"]
        planned = G.plan(**self.kwargs, record=True)
        packages = {p: types.SimpleNamespace(get_name=lambda p=p: p) for p in self.dirty}
        file = Path(self.temp.name) / "OldActor.uasset"
        file.write_bytes(b"old actor")
        def save(packages, only_dirty):
            self.dirty.discard("/Game/Output")
            return True
        with mock.patch.object(G, "_dirty_packages", return_value=packages), \
             mock.patch.object(G.R.E, "_package_file", return_value=str(file), create=True), \
             mock.patch.object(G.unreal, "EditorLoadingAndSavingUtils", types.SimpleNamespace(save_packages=save), create=True):
            result = G.save(receipt=planned["receipt"])
        self.assertFalse(result["ok"])
        self.assertEqual(result["deleted_files_still_exist"], ["/Game/Output"])

    def test_generation_in_progress_refuses_discovery(self):
        G.R.Z._generation_in_progress.return_value = True
        result = G.plan(**self.kwargs)
        self.assertFalse(result["ok"])
        G._discover.assert_not_called()


if __name__ == "__main__":
    unittest.main()
