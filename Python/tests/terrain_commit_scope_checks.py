"""Offline checks for durable Errant commit ownership and scoped package saves.

Run with ordinary CPython. Unreal is replaced before either editor module loads;
every history file lives in a temporary directory and every package is faked.
"""

import contextlib
import json
import os
import sys
import tempfile
import unittest
from unittest import mock


_PY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_ROOT = os.path.abspath(os.path.join(_PY, "..", "..", ".."))
sys.path.insert(0, os.path.join(_ROOT, "Content", "Python"))
sys.modules["unreal"] = mock.MagicMock()

import prk_lake_ops as L  # noqa: E402
import prk_terrain_author as TA  # noqa: E402


class FakePackage:
    def __init__(self, name):
        self.name = name

    def get_name(self):
        return self.name


class FakeWorld:
    def get_path_name(self):
        return "/Game/CommitTest.CommitTest"


class FakeActor:
    def __init__(self, label, package, actor_name=None):
        self.label = label
        self.package = package
        self.actor_name = actor_name or label + "_UAID"

    def get_actor_label(self):
        return self.label

    def get_name(self):
        return self.actor_name

    def get_package(self):
        return FakePackage(self.package)

    def get_class(self):
        return mock.MagicMock(**{"get_name.return_value": "TestStamp_C"})

    def identity(self):
        return {"package": self.package, "actor_name": self.actor_name,
                "label": self.label}

    def modify(self):
        pass


class InvalidatingActor(FakeActor):
    """Simulate an Unreal actor whose identity access is invalid after destruction."""
    invalid = False

    def require_valid(self):
        if self.invalid:
            raise RuntimeError("Actor already destroyed")

    def get_actor_label(self):
        self.require_valid()
        return super().get_actor_label()

    def get_name(self):
        self.require_valid()
        return super().get_name()

    def get_package(self):
        self.require_valid()
        return super().get_package()


class FakeModeBridge:
    def __init__(self, active=False):
        self.active = active
        self.calls = []
        self.on_toggle = None
        self.ok = True
        self.reach_target = True

    def get_errant_landscape_edit_mode_state(self):
        return json.dumps({"editor_mode_active": self.active,
                           "subsystem_edit_mode_active": self.active, "subsystem_idle": True})

    def get_errant_landscape_status(self, details):
        return json.dumps({"brush_actors_loaded": 2, "brush_actors_on_disk": 2,
                           "world_has_registered_bridges": True})

    def set_errant_landscape_edit_mode(self, active):
        self.calls.append(active)
        if self.ok and self.reach_target:
            self.active = active
        if self.on_toggle:
            self.on_toggle(active)
        return json.dumps({"ok": self.ok, "changed": self.ok and self.reach_target})


class CommitScopeChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="terrain_commit_scope_")
        self.addCleanup(self.temp.cleanup)
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        self.world = FakeWorld()
        self.history_path = os.path.join(self.temp.name, "commit.json")
        self.bounds_path = os.path.join(self.temp.name, "bounds.json")
        self.a = FakeActor("RidgeA", "/Game/__ExternalActors__/CommitTest/StampA")
        self.b = FakeActor("ValleyB", "/Game/__ExternalActors__/CommitTest/StampB")
        self.foreign = FakeActor("ForeignBaseline", "/Game/__ExternalActors__/CommitTest/ForeignProxy")
        self.predicted = FakeActor("PredictedProxy", "/Game/__ExternalActors__/CommitTest/PredictedProxy")
        self.edge = FakeActor("CommitEdgeProxy", "/Game/__ExternalActors__/CommitTest/EdgeProxy")
        self.second_edge = FakeActor("LaterEdgeProxy", "/Game/__ExternalActors__/CommitTest/LaterEdgeProxy")
        self.proxies = [self.foreign, self.predicted, self.edge, self.second_edge]
        self.actors = {a.label: a for a in (self.a, self.b)}
        self.dirty = {self.foreign.package}
        self.proxy_class = object()
        self.patch(L, "_commit_history_path", return_value=self.history_path)
        self.patch(L, "_stamp_bounds_history_path", return_value=self.bounds_path)
        self.patch(L, "_dirty_proxy_packages", side_effect=self.dirty_proxies)
        self.patch(L, "_world_guard", return_value=(self.world, None))
        self.patch(L, "_write_guard", return_value=(self.world, None))
        self.patch(L, "_edit_mode_active", return_value=False)
        self.patch(L, "_resolve_stamp", side_effect=self.resolve_stamp)
        self.patch(L, "_stamp_snapshot", side_effect=lambda actor, comp: self.snapshot(actor))
        self.patch(L, "_all", side_effect=lambda world, cls: self.proxies if cls is self.proxy_class else [])
        self.patch(L, "_predict_proxies", return_value={"proxies": [self.predicted.label]})
        self.patch(TA.unreal, "LandscapeStreamingProxy", self.proxy_class)
        self.patch(TA._el, "_dirty_package_names", side_effect=lambda: set(self.dirty))
        self.patch(TA, "_package_path", return_value=os.path.join(self.temp.name, "not_on_disk.uasset"))
        self.patch(TA.unreal.EditorLoadingAndSavingUtils, "get_dirty_map_packages", return_value=[])
        self.patch(TA.unreal.EditorLoadingAndSavingUtils, "get_dirty_content_packages",
                   side_effect=lambda: [FakePackage(p) for p in sorted(self.dirty)])
        self.patch(TA.unreal.EditorLoadingAndSavingUtils, "save_packages", side_effect=self.save_packages)

    def patch(self, obj, attr, *args, **kwargs):
        return self.stack.enter_context(mock.patch.object(obj, attr, *args, **kwargs))

    def dirty_proxies(self, world):
        return {p.package: p.label for p in self.proxies if p.package in self.dirty}

    def resolve_stamp(self, world, label):
        actor = self.actors.get(label)
        if actor is None:
            return None, None, {"ok": False, "refused": True, "error": "Missing fake stamp"}
        return actor, object(), None

    @staticmethod
    def snapshot(actor):
        # The actual committed edge proxy is deliberately outside the predicted box.
        return {"location": [0.0, 0.0, 1000.0], "scale": [100.0, 100.0, 0.2],
                "yaw": 0.0, "priority": 10,
                "heightmap": {"falloff": True, "falloff_start_xy": [0.5, 0.5],
                              "falloff_end_xy": [1.0, 1.0], "blend_method": "MAX"}}

    def save_packages(self, packages, only_dirty):
        self.assertTrue(only_dirty)
        self.dirty.difference_update(p.get_name() for p in packages)
        return True

    def history(self):
        return L._load_commit_history(self.world)

    def batch(self):
        return self.history()["batch"]

    def committed_batch(self, actors=None):
        actors = [self.a, self.b] if actors is None else actors
        L._begin_commit_batch(self.world)
        for actor in actors:
            L._record_commit_actor(self.world, actor.identity(), "configure")
            self.dirty.add(actor.package)
        self.dirty.update([self.predicted.package, self.edge.package])
        L._finish_commit_batch(self.world)
        return self.batch()

    def deleted_batch(self, with_bounds_history=False):
        L._begin_commit_batch(self.world)
        L._record_commit_actor(self.world, self.a.identity(), "configure")
        identity = dict(self.b.identity(), box=[[-5000.0, -5000.0], [5000.0, 5000.0]])
        if with_bounds_history:
            old = dict(self.snapshot(self.b), location=[20000.0, 0.0, 1000.0])
            L._record_stamp_bounds(self.world, self.b, old, self.snapshot(self.b))
        L._record_commit_actor(self.world, identity, "delete")
        self.actors.pop(self.b.label)
        self.dirty.update([self.a.package, self.b.package, self.predicted.package, self.edge.package])
        L._finish_commit_batch(self.world)
        return self.batch()

    @contextlib.contextmanager
    def mode_bridge(self, bridge, pie=None):
        with mock.patch.object(L._el, "_bridge", return_value=bridge), \
             mock.patch.object(L._el, "_pie_guard", return_value=pie), \
             mock.patch.object(L, "_edit_mode_active", side_effect=lambda world: bridge.active), \
             mock.patch.object(L, "_backup_state", return_value={"ok": True}), \
             mock.patch.object(L, "_dirty_landscape_state", return_value={}):
            yield

    def assert_identity_owned(self, batch, actor):
        self.assertTrue(any(row.get("package") == actor.package
                            and row.get("actor_name") == actor.actor_name
                            for row in batch["actors"].values()))

    def test_new_history_is_empty_and_world_specific(self):
        history = self.history()
        self.assertEqual(history["schema_version"], 1)
        self.assertEqual(history["world"], self.world.get_path_name())
        self.assertIsNone(history["batch"])

    def test_explicit_baseline_survives_idempotent_begin(self):
        baseline = {self.foreign.package: self.foreign.label}
        L._begin_commit_batch(self.world, baseline=baseline)
        first = self.batch()
        self.dirty.add(self.edge.package)
        L._begin_commit_batch(self.world, baseline={self.edge.package: self.edge.label})
        second = self.batch()
        self.assertEqual(second["id"], first["id"])
        self.assertEqual(second["baseline_proxies"], baseline)
        self.assertTrue(second["mode_active"])

    def test_actor_identity_and_actual_new_dirty_proxies_are_persisted(self):
        batch = self.committed_batch()
        self.assert_identity_owned(batch, self.a)
        self.assert_identity_owned(batch, self.b)
        self.assertFalse(batch["mode_active"])
        self.assertEqual(batch["baseline_proxies"], {self.foreign.package: self.foreign.label})
        self.assertEqual(set(batch["committed_proxies"]), {self.predicted.package, self.edge.package})
        self.assertNotIn(self.foreign.package, batch["committed_proxies"])

    def test_finish_is_idempotent_and_preserves_initial_baseline(self):
        first = self.committed_batch()
        L._finish_commit_batch(self.world)
        second = self.batch()
        self.assertEqual(second["id"], first["id"])
        self.assertEqual(second["baseline_proxies"], first["baseline_proxies"])
        self.assertEqual(second["actors"], first["actors"])
        self.assertEqual(second["committed_proxies"], first["committed_proxies"])

    def test_unsaved_second_mode_cycle_accumulates_in_one_batch(self):
        first = self.committed_batch([self.a])
        L._begin_commit_batch(self.world)
        L._record_commit_actor(self.world, self.b, "spawn")
        self.dirty.update([self.b.package, self.second_edge.package])
        L._finish_commit_batch(self.world)
        second = self.batch()
        self.assertEqual(second["id"], first["id"])
        self.assertEqual(second["baseline_proxies"], first["baseline_proxies"])
        self.assert_identity_owned(second, self.a)
        self.assert_identity_owned(second, self.b)
        self.assertEqual(set(second["committed_proxies"]),
                         {self.predicted.package, self.edge.package, self.second_edge.package})
        self.assertGreater(second["revision"], first["revision"])

    def test_new_foreign_proxy_between_cycles_is_added_to_baseline(self):
        first = self.committed_batch([self.a])
        self.dirty.add(self.second_edge.package)
        L._begin_commit_batch(self.world)
        L._record_commit_actor(self.world, self.b, "configure")
        self.dirty.add(self.b.package)
        L._finish_commit_batch(self.world)
        second = self.batch()
        self.assertEqual(second["id"], first["id"])
        self.assertEqual(second["baseline_proxies"],
                         {self.foreign.package: self.foreign.label,
                          self.second_edge.package: self.second_edge.label})
        self.assertNotIn(self.second_edge.package, second["committed_proxies"])
        self.assertIn(self.edge.package, second["committed_proxies"])

    def test_complete_scope_includes_actual_edge_outside_predictions(self):
        self.committed_batch()
        sc, refusal = TA._scope(self.world, [self.a.label, self.b.label], None, None, None)
        self.assertIsNone(refusal)
        self.assertIn(self.edge.package, sc["owned"])
        self.assertIn(self.predicted.package, sc["owned"])
        self.assertNotIn(self.foreign.package, sc["owned"])
        self.assertIn(self.foreign.package, sc["foreign"])
        self.assertNotIn(self.edge.label, sc["predicted_proxies"])

    def test_subset_scope_is_usable_but_omits_extra_commit_proxies(self):
        self.committed_batch()
        result = TA.save_scope(stamps=[self.a.label])
        self.assertTrue(result.get("ok"), result)
        self.assertIn(self.a.package, result["owned_packages"])
        self.assertNotIn(self.edge.package, result["owned_packages"])
        omissions = result.get("commit_scope_omissions")
        self.assertTrue(omissions, result)
        described = json.dumps(omissions)
        self.assertTrue(self.b.package in described or self.b.actor_name in described, described)

    def test_same_package_replacement_identity_does_not_complete_scope(self):
        self.committed_batch()
        self.actors[self.a.label] = FakeActor(self.a.label, self.a.package, "Replacement_UAID")
        result = TA.save_scope(stamps=[self.a.label, self.b.label])
        self.assertTrue(result.get("ok"), result)
        self.assertNotIn(self.edge.package, result["owned_packages"])
        self.assertIn(self.a.actor_name, json.dumps(result.get("commit_scope_omissions")))

    def test_active_mode_scope_does_not_claim_actual_commit_extras(self):
        self.committed_batch()
        L._begin_commit_batch(self.world)
        result = TA.save_scope(stamps=[self.a.label, self.b.label])
        self.assertTrue(result.get("ok"), result)
        self.assertNotIn(self.edge.package, result["owned_packages"])
        self.assertFalse(result["actual_commit_scope"]["eligible"])
        self.assertTrue(result["actual_commit_scope"]["mode_active"])

    def test_partial_save_keeps_batch_and_foreign_dirty_baseline(self):
        batch = self.committed_batch()
        result = TA.save_packages_scoped(packages=[self.a.package], stamps=[self.a.label, self.b.label])
        self.assertTrue(result.get("ok"), result)
        self.assertEqual(self.batch()["id"], batch["id"])
        self.assertIn(self.edge.package, self.dirty)
        self.assertIn(self.foreign.package, self.dirty)

    def test_full_save_retires_only_owned_batch_and_preserves_foreign(self):
        self.committed_batch()
        scope = TA.save_scope(stamps=[self.a.label, self.b.label])
        self.assertTrue(scope.get("ok"), scope)
        result = TA.save_packages_scoped(packages=scope["owned_packages"],
                                        stamps=[self.a.label, self.b.label])
        self.assertTrue(result.get("ok"), result)
        self.assertIsNone(self.batch())
        self.assertEqual(self.dirty, {self.foreign.package})

    def test_no_recorded_actor_never_claims_global_new_dirty_proxies(self):
        L._begin_commit_batch(self.world)
        self.dirty.update([self.predicted.package, self.edge.package])
        L._finish_commit_batch(self.world)
        sc, refusal = TA._scope(self.world, [], None, None, None)
        self.assertIsNone(refusal)
        self.assertEqual(sc["owned"], set())
        self.assertIn(self.edge.package, sc["foreign"])
        batch = self.batch()
        if batch is not None:
            self.assertFalse(batch["actors"])

    def test_empty_cycle_dirty_edge_stays_foreign_after_later_owned_cycle(self):
        L._begin_commit_batch(self.world)
        self.dirty.add(self.edge.package)
        L._finish_commit_batch(self.world)
        self.assertFalse(self.batch()["actors"])
        self.assertFalse(self.batch()["committed_proxies"])
        L._begin_commit_batch(self.world)
        L._record_commit_actor(self.world, self.a.identity(), "configure")
        self.dirty.update([self.a.package, self.second_edge.package])
        L._finish_commit_batch(self.world)
        batch = self.batch()
        self.assertIn(self.edge.package, batch["baseline_proxies"])
        self.assertNotIn(self.edge.package, batch["committed_proxies"])
        self.assertIn(self.second_edge.package, batch["committed_proxies"])
        scope = TA.save_scope(stamps=[self.a.label])
        self.assertTrue(scope.get("ok"), scope)
        self.assertIn(self.second_edge.package, scope["owned_packages"])
        self.assertNotIn(self.edge.package, scope["owned_packages"])
        self.assertIn(self.edge.package, scope["foreign_dirty"])

    def test_stale_revision_cannot_retire_newer_owned_write(self):
        first = self.committed_batch([self.a])
        token = {"id": first["id"], "revision": first["revision"]}
        L._begin_commit_batch(self.world)
        L._record_commit_actor(self.world, self.b.identity(), "delete")
        L._finish_commit_batch(self.world)
        L._retire_commit_batch(self.world, token)
        current = self.batch()
        self.assertIsNotNone(current)
        self.assertEqual(current["id"], first["id"])
        self.assertGreater(current["revision"], token["revision"])
        self.assert_identity_owned(current, self.b)

    def test_history_can_be_reloaded_from_plain_json_without_cached_state(self):
        self.committed_batch()
        with open(self.history_path, encoding="utf-8") as f:
            persisted = json.load(f)
        self.assertEqual(persisted, self.history())
        persisted["batch"]["revision"] += 7
        with open(self.history_path, "w", encoding="utf-8") as f:
            json.dump(persisted, f)
        self.assertEqual(L._load_commit_history(FakeWorld()), persisted)

    def test_deleted_feature_actor_identity_completes_commit_scope(self):
        self.committed_batch()
        journal = {"spawned": [{**self.a.identity(), "box": [[-5000, -5000], [5000, 5000]]}],
                   "deleted": [{**self.b.identity(), "box": [[-5000, -5000], [5000, 5000]]}]}
        with mock.patch.object(TA, "_read_json", return_value=journal):
            result = TA.save_scope(feature="DeletedFeature")
        self.assertTrue(result.get("ok"), result)
        self.assertIn(self.edge.package, result["owned_packages"])
        self.assertIn(self.b.package, result["owned_packages"])

    def test_legacy_deleted_feature_without_identity_does_not_complete_scope(self):
        self.committed_batch()
        journal = {"spawned": [{**self.a.identity(), "box": [[-5000, -5000], [5000, 5000]]}],
                   "deleted": [{"label": self.b.label, "package": self.b.package,
                                "box": [[-5000, -5000], [5000, 5000]]}]}
        with mock.patch.object(TA, "_read_json", return_value=journal):
            result = TA.save_scope(feature="LegacyDeletedFeature")
        self.assertTrue(result.get("ok"), result)
        self.assertIn(self.b.package, result["owned_packages"])
        self.assertNotIn(self.edge.package, result["owned_packages"])
        self.assertIn(self.b.actor_name, json.dumps(result.get("commit_scope_omissions")))

    def test_direct_deleted_stamp_selector_completes_scope_and_exact_save(self):
        self.deleted_batch()
        result = TA.save_scope(stamps=[self.a.label, self.b.label])
        self.assertTrue(result.get("ok"), result)
        self.assertIn(self.edge.package, result["owned_packages"])
        self.assertIn(self.b.package, result["owned_packages"])
        by_name = TA.save_scope(stamps=[self.a.label, self.b.actor_name])
        self.assertTrue(by_name.get("ok"), by_name)
        self.assertEqual(by_name["owned_packages"], result["owned_packages"])
        deletion = next(row for row in self.batch()["actors"].values() if row["deleted"])
        self.assertEqual(deletion["box"], [[-5000.0, -5000.0], [5000.0, 5000.0]])
        saved = TA.save_packages_scoped(packages=result["owned_packages"],
                                       stamps=[self.a.label, self.b.label])
        self.assertTrue(saved.get("ok"), saved)
        deleted_files = saved["deleted_stamp_files"]
        self.assertTrue(any(row["package"] == self.b.package
                            and row["file_exists_after"] is False for row in deleted_files), deleted_files)
        self.assertIsNone(self.batch())
        self.assertEqual(self.dirty, {self.foreign.package})

    def test_direct_deleted_selector_retains_previous_configure_bounds(self):
        self.deleted_batch(with_bounds_history=True)
        calls = []

        def predict(world, boxes):
            calls.extend(boxes)
            return {"proxies": [self.predicted.label]}

        with mock.patch.object(L, "_predict_proxies", side_effect=predict):
            result = TA.save_scope(stamps=[self.a.label, self.b.label])
        self.assertTrue(result.get("ok"), result)
        self.assertEqual(len(result["pending_stamp_bounds"]), 1)
        self.assertIn(((15000.0, -5000.0), (25000.0, 5000.0)), calls)

    def test_ambiguous_deleted_label_refuses_but_unique_name_resolves(self):
        L._begin_commit_batch(self.world)
        twin = FakeActor(self.b.label, "/Game/__ExternalActors__/CommitTest/DeletedTwin", "DeletedTwin_UAID")
        for actor in (self.b, twin):
            L._record_commit_actor(self.world,
                                   dict(actor.identity(), box=[[-5000, -5000], [5000, 5000]]), "delete")
            self.dirty.add(actor.package)
        self.actors.pop(self.b.label)
        self.dirty.add(self.edge.package)
        L._finish_commit_batch(self.world)
        result = TA.save_scope(stamps=[self.b.label])
        self.assertTrue(result.get("refused"), result)
        self.assertFalse(result.get("ok"), result)
        unique = TA.save_scope(stamps=[self.b.actor_name, twin.actor_name])
        self.assertTrue(unique.get("ok"), unique)
        self.assertIn(self.edge.package, unique["owned_packages"])

    def test_native_delete_records_identity_before_actor_invalidation(self):
        actor = InvalidatingActor(self.b.label, self.b.package, self.b.actor_name)
        self.actors[self.b.label] = actor
        identity = actor.identity()
        L._begin_commit_batch(self.world)

        def destroy_actor(candidate):
            self.assertIs(candidate, actor)
            candidate.invalid = True
            self.actors.pop(self.b.label)
            self.dirty.add(self.b.package)
            return True

        subsystem = mock.MagicMock()
        subsystem.destroy_actor.side_effect = destroy_actor
        with mock.patch.object(L, "_edit_mode_active", return_value=True), \
             mock.patch.object(L, "_tagged", return_value=True), \
             mock.patch.object(L, "_footprint", return_value=object()), \
             mock.patch.object(L, "_proxies_overlapping", return_value=[]), \
             mock.patch.object(L, "_dirty_landscape_state", return_value={}), \
             mock.patch.object(L.unreal, "get_editor_subsystem", return_value=subsystem):
            result = L.stamp_delete(actor=self.b.label, dry_run=False)
        self.assertTrue(result.get("ok"), result)
        self.assertTrue(result["commit_history"].get("ok"), result)
        recorded = next(iter(self.batch()["actors"].values()))
        self.assertEqual({k: recorded[k] for k in identity}, identity)
        self.assertTrue(recorded["deleted"])
        self.assertEqual(recorded["box"], [[-5000.0, -5000.0], [5000.0, 5000.0]])
        self.assertTrue(actor.invalid)

    def test_native_failed_and_dry_run_deletes_do_not_record_ownership(self):
        L._begin_commit_batch(self.world)
        for dry_run in (True, False):
            with self.subTest(dry_run=dry_run):
                subsystem = mock.MagicMock()
                subsystem.destroy_actor.return_value = False
                with mock.patch.object(L, "_edit_mode_active", return_value=True), \
                     mock.patch.object(L, "_tagged", return_value=True), \
                     mock.patch.object(L, "_footprint", return_value=object()), \
                     mock.patch.object(L, "_proxies_overlapping", return_value=[]), \
                     mock.patch.object(L, "_dirty_landscape_state", return_value={}), \
                     mock.patch.object(L.unreal, "get_editor_subsystem", return_value=subsystem), \
                     mock.patch.object(L, "_record_commit_actor") as record:
                    result = L.stamp_delete(actor=self.b.label, dry_run=dry_run)
                record.assert_not_called()
                self.assertFalse(self.batch()["actors"])
                if dry_run:
                    self.assertTrue(result.get("ok") and result.get("dry_run"), result)
                    subsystem.destroy_actor.assert_not_called()
                else:
                    self.assertFalse(result.get("ok"), result)
                    self.assertFalse(result.get("destroyed"), result)
                    subsystem.destroy_actor.assert_called_once_with(self.b)

    def test_native_mode_entry_captures_pre_toggle_baseline_and_exit_finishes(self):
        bridge = FakeModeBridge()
        bridge.on_toggle = lambda active: self.dirty.add(self.edge.package) if active else None
        with self.mode_bridge(bridge):
            entered = L.edit_mode_set(active=True, dry_run=False, preserve_camera=False)
            self.assertTrue(entered.get("ok"), entered)
            self.assertTrue(self.batch()["mode_active"])
            self.assertEqual(self.batch()["baseline_proxies"], {self.foreign.package: self.foreign.label})
            L._record_commit_actor(self.world, self.a, "configure")
            self.dirty.update([self.a.package, self.predicted.package])
            exited = L.edit_mode_set(active=False, dry_run=False, preserve_camera=False)
        self.assertTrue(exited.get("ok"), exited)
        self.assertFalse(self.batch()["mode_active"])
        self.assertEqual(set(self.batch()["committed_proxies"]), {self.predicted.package, self.edge.package})
        self.assertEqual(bridge.calls, [True, False])

    def test_native_idempotent_mode_calls_do_not_reset_or_finish_again(self):
        bridge = FakeModeBridge()
        with self.mode_bridge(bridge):
            L.edit_mode_set(active=True, dry_run=False, preserve_camera=False)
            L._record_commit_actor(self.world, self.a, "configure")
            self.dirty.add(self.edge.package)
            before = self.history()
            repeated = L.edit_mode_set(active=True, dry_run=False, preserve_camera=False)
            self.assertTrue(repeated.get("ok") and not repeated.get("changed"), repeated)
            self.assertEqual(self.history(), before)
            self.assertEqual(bridge.calls, [True])
            L.edit_mode_set(active=False, dry_run=False, preserve_camera=False)
            finished = self.history()
            repeated = L.edit_mode_set(active=False, dry_run=False, preserve_camera=False)
            self.assertTrue(repeated.get("ok") and not repeated.get("changed"), repeated)
            self.assertEqual(self.history(), finished)
            self.assertEqual(bridge.calls, [True, False])

    def test_native_dry_run_mode_toggle_does_not_start_tracking(self):
        bridge = FakeModeBridge()
        with self.mode_bridge(bridge):
            result = L.edit_mode_set(active=True, dry_run=True, preserve_camera=False)
        self.assertTrue(result.get("ok") and result.get("dry_run"), result)
        self.assertFalse(bridge.calls)
        self.assertIsNone(self.batch())

    def test_native_refused_mode_toggle_never_tracks_or_calls_bridge(self):
        bridge = FakeModeBridge()
        with self.mode_bridge(bridge, pie={"ok": False, "refused": True, "error": "Fake PIE"}):
            result = L.edit_mode_set(active=True, dry_run=False, preserve_camera=False)
        self.assertTrue(result.get("refused"), result)
        self.assertFalse(bridge.calls)
        self.assertIsNone(self.batch())

    def test_native_failed_or_unreached_toggle_does_not_start_batch(self):
        for ok, reach_target in ((False, True), (True, False)):
            with self.subTest(ok=ok, reach_target=reach_target):
                bridge = FakeModeBridge()
                bridge.ok, bridge.reach_target = ok, reach_target
                with self.mode_bridge(bridge):
                    result = L.edit_mode_set(active=True, dry_run=False, preserve_camera=False)
                self.assertIsNone(self.batch(), result)
                self.assertEqual(bridge.calls, [True])

    def test_guard_refusals_do_not_record_commit_ownership(self):
        refusal = {"ok": False, "refused": True, "error": "Fake blocked world"}
        with mock.patch.object(L, "_record_commit_actor") as record, \
             mock.patch.object(L, "_world_guard", return_value=(None, refusal)), \
             mock.patch.object(L, "_write_guard", return_value=(None, refusal)):
            outputs = [L.stamp_configure(actor=self.a.label, location_z=1200, dry_run=False),
                       L.stamp_delete(actor=self.a.label, dry_run=False),
                       L.stamp_spawn(stamp_class="/Game/Test/Test", x=0, y=0, yaw=0,
                                     scale=[100, 100, 0.2], location_z=1200, priority=10,
                                     dry_run=False)]
            self.assertTrue(all(result.get("refused") for result in outputs), outputs)
            record.assert_not_called()
        self.assertIsNone(self.batch())

    def test_configure_dry_run_does_not_record_commit_ownership(self):
        spec = {"hm": {}, "wm": {}, "location_z": 1200.0}
        before = self.snapshot(self.a)
        planned = dict(before, location=[0.0, 0.0, 1200.0])
        with mock.patch.object(L, "_record_commit_actor") as record, \
             mock.patch.object(L, "_field_spec", return_value=(spec, None)), \
             mock.patch.object(L, "_wm_bridge_refusal", return_value=([], None)), \
             mock.patch.object(L, "_plan_fields", return_value=(planned, None, None)), \
             mock.patch.object(L, "_proxies_overlapping", return_value=[]), \
             mock.patch.object(L, "_diff", return_value=[{"field": "location", "before": before["location"],
                                                        "after": planned["location"]}]):
            result = L.stamp_configure(actor=self.a.label, location_z=1200, dry_run=True)
            self.assertTrue(result.get("ok") and result.get("dry_run"), result)
            record.assert_not_called()
        self.assertIsNone(self.batch())


if __name__ == "__main__":
    unittest.main(verbosity=2)
