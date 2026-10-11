"""Isolated mocked checks for scoped registration, verification and package saves."""
from pathlib import Path
import json
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "Content/Python"))
for name in ("unreal", "prk_errant", "prk_zone_biomes"):
    sys.modules[name] = types.ModuleType(name)
import prk_biome_regions as R


class Asset:
    def __init__(self, path, kind="BiomesBiome", index=None):
        self.path, self.kind, self.index = path, kind, index

    def get_path_name(self):
        return self.path

    def get_name(self):
        return self.path

    def get_package(self):
        return types.SimpleNamespace(get_name=lambda: self.path.split(".")[0])

    def get_class(self):
        return types.SimpleNamespace(get_name=lambda: self.kind)

    def get_editor_property(self, name):
        return self.index if name == "biome_map_index" else None


class RegionChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.world = Asset("/Game/Test.World")
        self.ws = Asset("/Game/Test/ExternalSettings.Settings")
        self.biome = Asset("/Game/Biomes/EB_Mire.EB_Mire")
        self.definition = Asset("/Game/Zones/DA_Mire.DA_Mire", "PRKBiomeDefinition", 40)
        self.live = [f"/Game/Biomes/EB_{i}.EB_{i}" for i in range(10)]
        self.link = "None"
        self.dirty = {"/Game/Foreign"}
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(R, "_guard", return_value=(self.world, self.ws)).start()
        mock.patch.object(R, "_root", return_value=Path(self.temp.name)).start()
        mock.patch.object(R.Z, "_load", side_effect=lambda p: self.definition if "DA_" in p else self.biome, create=True).start()
        mock.patch.object(R.Z, "_biomes_list", side_effect=lambda w: list(self.live), create=True).start()
        mock.patch.object(R.Z, "_get_text", side_effect=lambda o, p: self.link, create=True).start()
        mock.patch.object(R.E, "_dirty_package_names", side_effect=lambda: set(self.dirty), create=True).start()
        self.array_write = mock.patch.object(R.Z, "_set_array", side_effect=self._set_list, create=True).start()
        self.link_write = mock.patch.object(R.Z, "_set_text", side_effect=self._set_link, create=True).start()
        mock.patch.object(R.Z, "soft_array_literal", side_effect=lambda v: v, create=True).start()

    def _set_list(self, obj, prop, values):
        self.live = list(values)
        self.dirty.add(self.ws.get_package().get_name())
        return True

    def _set_link(self, obj, prop, value):
        self.link = value
        self.dirty.add(self.definition.get_package().get_name())
        return True

    def test_registration_dry_run_has_no_writes_or_receipt(self):
        result = R.register(biome_path=self.biome.path, prk_biome_path=self.definition.path)
        self.assertTrue(result["ok"])
        self.assertEqual(result["eb_list_index"], 10)
        self.assertEqual(result["prk_biome_index"], 40)
        self.assertEqual(self.live, result["biomes_before"])
        self.array_write.assert_not_called()
        self.link_write.assert_not_called()
        self.assertEqual(list(Path(self.temp.name).iterdir()), [])

    def test_canonical_reads_protected_native_buffer_through_bridge(self):
        properties = {"resolution": 1, "world_origin_cm": types.SimpleNamespace(x=10, y=20),
                      "world_extent_cm": types.SimpleNamespace(x=30, y=40)}
        asset = Asset("/Game/ZoneMap.ZoneMap", "PRKZoneMapAsset")
        asset.get_editor_property = lambda name: properties[name]
        with mock.patch.object(R, "_asset", return_value=asset), \
             mock.patch.object(R.Z, "_get_text", return_value="(40,25,0,255)") as read:
            self.assertEqual(R._canonical(asset.path, 25), (bytes([25]), 1, [10, 20], [30, 40]))
            read.assert_called_once_with(asset, "MapData")

    def test_grid_uses_exact_landscape_transform_and_rejects_stale_biomes_grid(self):
        land = types.SimpleNamespace(
            get_actor_rotation=lambda: types.SimpleNamespace(pitch=0, yaw=0, roll=0),
            get_actor_scale3d=lambda: types.SimpleNamespace(x=400, y=400),
            get_actor_location=lambda: types.SimpleNamespace(x=-1625600, y=-1625600))
        ws = types.SimpleNamespace(get_editor_property=lambda name: land)
        props = {"ComponentSizeQuads": "254", "SectionBase": "(X=0,Y=0)"}
        bridge = types.SimpleNamespace(sample_errant_biomes_spawner_info=lambda xy, pie: json.dumps(
            {"generation_component": {"component_xy": [0, 0],
             "uv": [(v + 1625600) / 101600 for v in xy]}}))
        with mock.patch.object(R.Z, "_get_text", side_effect=lambda obj, prop: props[prop]), \
             mock.patch.object(R.Z, "_bridge_or_raise", return_value=bridge, create=True), \
             mock.patch.object(R.unreal, "Vector2D", side_effect=lambda *v: v, create=True):
            self.assertEqual(R._grid(self.world, ws, [-1625600, -1625600], [3251200, 3251200]),
                             ([-1625600, -1625600], [101600, 101600], 255))
            bridge.sample_errant_biomes_spawner_info = lambda xy, pie: json.dumps(
                {"generation_component": {"component_xy": [0, 0], "uv": [0, 0]}})
            with self.assertRaisesRegex(ValueError, "disagrees"):
                R._grid(self.world, ws, [-1625600, -1625600], [3251200, 3251200])

    def test_generic_plugin_mask_uses_native_property_names(self):
        mask = Asset("/Game/Test/BiomeMask.BiomeMask")
        mask.get_editor_property = lambda name: self.fail("Generic mask cannot use Python aliases")
        data = types.SimpleNamespace(asset_class_path=types.SimpleNamespace(asset_name="BiomesBiomeMaskTextureData"),
                                     get_asset=lambda: mask)
        registry = types.SimpleNamespace(get_assets_by_path=lambda path, recursive: [data])
        registry_helpers = types.SimpleNamespace(get_asset_registry=lambda: registry)
        props = {"ComponentX": "9", "ComponentY": "7"}
        with mock.patch.object(R.unreal, "AssetRegistryHelpers", registry_helpers, create=True), \
             mock.patch.object(R.Z, "_get_text", side_effect=lambda obj, prop: props[prop]) as read:
            self.assertEqual(R._masks(self.world), {(9, 7): mask})
            self.assertEqual([call.args[1] for call in read.call_args_list], ["ComponentX", "ComponentY"])

    def test_material_compile_failure_is_refused_before_fallback_can_be_sampled(self):
        nodes = []
        def node(*args):
            result = types.SimpleNamespace(properties={})
            result.set_editor_property = lambda name, value: result.properties.__setitem__(name, value)
            nodes.append(result)
            return result
        material = node()
        library = types.SimpleNamespace(create_material_expression=node,
                                        connect_material_property=lambda *args: True,
                                        connect_material_expressions=lambda *args: True,
                                        recompile_material=lambda m: ["invalid sampler"])
        texture = types.SimpleNamespace(get_editor_property=lambda name: True)
        with mock.patch.object(R.unreal, "new_object", return_value=material, create=True), \
             mock.patch.object(R.unreal, "Material", object, create=True), \
             mock.patch.object(R.unreal, "MaterialEditingLibrary", library, create=True), \
             mock.patch.object(R.unreal, "MaterialShadingModel", types.SimpleNamespace(MSM_UNLIT=0), create=True), \
             mock.patch.object(R.unreal, "BlendMode", types.SimpleNamespace(BLEND_OPAQUE=0), create=True), \
             mock.patch.object(R.unreal, "MaterialSamplerType", types.SimpleNamespace(
                 SAMPLERTYPE_COLOR=0, SAMPLERTYPE_LINEAR_COLOR=1), create=True), \
             mock.patch.object(R.unreal, "MaterialExpressionTextureSampleParameter2D", object, create=True), \
             mock.patch.object(R.unreal, "MaterialExpressionScalarParameter", object, create=True), \
             mock.patch.object(R.unreal, "MaterialExpressionLinearInterpolate", object, create=True), \
             mock.patch.object(R.unreal, "MaterialProperty", types.SimpleNamespace(MP_EMISSIVE_COLOR=0), create=True):
            with self.assertRaisesRegex(ValueError, "invalid sampler"):
                R._material(texture, texture, 10 / 255)
            self.assertEqual(nodes[1].properties["sampler_type"], 0)

    def test_render_context_reuses_targets_and_transient_mids_and_compiles_blend_once(self):
        mids = []
        def create_mid(*args, **kwargs):
            self.assertEqual(kwargs["creation_flags"], 1)
            mid = types.SimpleNamespace(set_vector_parameter_value=mock.Mock(),
                                        set_texture_parameter_value=mock.Mock(),
                                        set_scalar_parameter_value=mock.Mock())
            mids.append(mid)
            return mid
        rendering = types.SimpleNamespace(create_render_target2d=mock.Mock(side_effect=lambda *args: object()),
                                         draw_material_to_render_target=mock.Mock())
        editing = types.SimpleNamespace(get_texture_parameter_names=lambda obj: ["PotentialTex"],
                                       get_vector_parameter_names=lambda obj: ["UVOffsetScale"])
        with mock.patch.object(R, "_RENDER_CONTEXTS", {}), \
             mock.patch.object(R, "_asset", return_value=self.biome), \
             mock.patch.object(R, "_material", return_value=object()) as compile_material, \
             mock.patch.object(R.unreal, "MaterialLibrary", types.SimpleNamespace(
                 create_dynamic_material_instance=create_mid), create=True), \
             mock.patch.object(R.unreal, "MaterialEditingLibrary", editing, create=True), \
             mock.patch.object(R.unreal, "RenderingLibrary", rendering, create=True), \
             mock.patch.object(R.unreal, "MIDCreationFlags", types.SimpleNamespace(TRANSIENT=1), create=True), \
             mock.patch.object(R.unreal, "LinearColor", side_effect=lambda *v: v, create=True), \
             mock.patch.object(R.unreal, "TextureRenderTargetFormat", types.SimpleNamespace(RTF_RGBA32F=1), create=True):
            context = R._context(self.world, 255)
            self.assertIs(R._context(self.world, 255), context)
            previous, selector, destination = object(), object(), object()
            R._draw_blend(context, previous, selector, destination, 10)
            R._draw_blend(context, previous, selector, destination, 10)
            self.assertEqual(rendering.create_render_target2d.call_count, 2)
            self.assertEqual(len(mids), 2)  # stable copy + stable blend
            compile_material.assert_called_once()
            self.assertEqual(context["blend_compiles"], 1)
            self.assertIn(previous, context["held"])
            self.assertIn(selector, context["held"])
            self.assertIn(destination, context["held"])

    def test_registration_preserves_live_prefix_and_prk_index(self):
        before = list(self.live)
        result = R.register(biome_path=self.biome.path, prk_biome_path=self.definition.path, dry_run=False)
        self.assertTrue(result["verified"])
        self.assertEqual(self.live[:-1], before)
        self.assertEqual(self.definition.index, 40)
        self.assertEqual(set(result["owned_packages"]), {"/Game/Test/ExternalSettings", "/Game/Zones/DA_Mire"})
        self.assertTrue(R.verify(receipt=result["receipt"])["ok"])

    def test_already_registered_is_idempotent_and_owns_no_packages(self):
        self.live.append(self.biome.path)
        self.link = self.biome.path
        result = R.register(biome_path=self.biome.path, prk_biome_path=self.definition.path, dry_run=False)
        self.assertTrue(result["verified"])
        self.assertEqual(result["owned_packages"], [])
        self.array_write.assert_not_called()
        self.link_write.assert_not_called()

    def test_registration_refuses_duplicate_live_indices(self):
        self.live.append(self.live[0])
        result = R.register(biome_path=self.biome.path, dry_run=False)
        self.assertFalse(result["ok"])
        self.array_write.assert_not_called()

    def test_verification_refuses_changed_live_list(self):
        result = R.register(biome_path=self.biome.path, dry_run=False)
        self.live.reverse()
        self.assertFalse(R.verify(receipt=result["receipt"])["ok"])

    def test_tile_readback_detects_neighbor_and_assigned_differences(self):
        directory = Path(self.temp.name)
        baseline = directory / "baseline.gz"
        selector = directory / "selector.bin"
        R._baseline(baseline, [0, 0.125, 0.25, 0.5])
        selector.write_bytes(bytes([255, 0, 0, 255]))
        row = {"component_xy": [2, 3], "size": 2, "baseline": str(baseline), "selector_bytes": str(selector)}
        target = R._half(10 / 255)
        mock.patch.object(R, "_ready_rt", return_value=None).start()
        snapshot = mock.patch.object(R, "_snapshot", return_value=(None, [target, 0.125, 0.25, target])).start()
        self.assertTrue(R._check(self.world, row, 10)["verified"])
        snapshot.return_value = (None, [target, 0.125001, 0.25, target])
        result = R._check(self.world, row, 10)
        self.assertFalse(result["verified"])
        self.assertEqual(result["outside_differences"], 1)
        snapshot.return_value = (None, [0, 0.125, 0.25, target])
        self.assertEqual(R._check(self.world, row, 10)["inside_differences"], 1)

    def test_save_keeps_foreign_dirty_and_saves_only_receipt_packages(self):
        result = R.register(biome_path=self.biome.path, dry_run=False)
        owned = "/Game/Test/ExternalSettings"
        packages = {p: Asset(p) for p in self.dirty}
        def save_packages(objects, only_dirty):
            self.assertEqual({o.path for o in objects}, {owned})
            self.dirty.difference_update(o.path for o in objects)
            return True
        saving = types.SimpleNamespace(get_dirty_map_packages=lambda: list(packages.values()),
                                       get_dirty_content_packages=lambda: [], save_packages=save_packages)
        mock.patch.object(R.unreal, "EditorLoadingAndSavingUtils", saving, create=True).start()
        mock.patch.object(R.E, "_package_file", return_value=None, create=True).start()
        saved = R.save(receipt=result["receipt"])
        self.assertTrue(saved["ok"])
        self.assertEqual(saved["saved_packages"], [owned])
        self.assertEqual(self.dirty, {"/Game/Foreign"})

    def test_receipts_outside_owned_directory_are_refused(self):
        result = R.verify(receipt=str(Path(self.temp.name).parent / "untrusted.json"))
        self.assertFalse(result["ok"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
