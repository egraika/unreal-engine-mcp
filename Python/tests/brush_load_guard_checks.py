"""Isolated checks for pending Errant brush deletions; never contacts Unreal."""

import contextlib
import os
import sys
import unittest
from unittest import mock


_PY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_ROOT = os.path.abspath(os.path.join(_PY, "..", "..", ".."))
sys.path.insert(0, os.path.join(_ROOT, "Content", "Python"))
sys.modules["unreal"] = mock.MagicMock()

import prk_lake_ops as L  # noqa: E402


class Actor:
    def __init__(self, package, name):
        self.package, self.name = package, name

    def get_package(self):
        return mock.Mock(**{"get_name.return_value": self.package})

    def get_name(self):
        return self.name


class Desc:
    def __init__(self, package, name, native):
        self.fields = {"actor_package": package, "name": name, "native_class": native}

    def get_editor_property(self, field):
        return self.fields[field]


def deleted(package, name):
    return {"package": package, "actor_name": name, "action": "delete", "deleted": True}


class BrushLoadGuardChecks(unittest.TestCase):
    def setUp(self):
        self.world, self.native = object(), object()
        self.live = [Actor("/Game/Actors/A", "A_UAID")]
        self.descs = [Desc("/Game/Actors/A", "A_UAID", self.native),
                      Desc("/Game/Actors/B", "B_UAID", self.native)]
        self.rows = [deleted("/Game/Actors/B", "B_UAID")]
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        self.history = self.stack.enter_context(mock.patch.object(
            L, "_load_commit_history", side_effect=lambda world:
            {"batch": {"actors": {str(i): row for i, row in enumerate(self.rows)}}}))
        self.stack.enter_context(mock.patch.object(L, "_all", side_effect=lambda world, cls: self.live))
        self.enumerate = self.stack.enter_context(mock.patch.object(
            L.unreal.WorldPartitionBlueprintLibrary, "get_actor_descs",
            side_effect=lambda: (True, self.descs)))
        self.stack.enter_context(mock.patch.object(
            L.unreal.MathLibrary, "class_is_child_of", side_effect=lambda native, base: native is self.native))

    def test_three_deleted_descriptors_and_one_unsaved_new_actor(self):
        self.live.append(Actor("/Game/Actors/New", "New_UAID"))
        for name in ("C", "D"):
            self.descs.append(Desc("/Game/Actors/" + name, name + "_UAID", self.native))
            self.rows.append(deleted("/Game/Actors/" + name, name + "_UAID"))
        ok, detail = L._brush_load_guard(self.world, 2, 4)
        self.assertTrue(ok)
        self.assertEqual(detail["effective_on_disk"], 1)
        self.assertEqual(len(detail["pending_deleted_on_disk"]), 3)

    def test_unsaved_actor_cannot_mask_genuinely_unloaded_brush(self):
        self.live.append(Actor("/Game/Actors/New", "New_UAID"))
        self.descs.append(Desc("/Game/Actors/Missing", "Missing_UAID", self.native))
        ok, detail = L._brush_load_guard(self.world, 2, 3)
        self.assertFalse(ok)
        self.assertEqual(detail["unloaded_brushes"],
                         [{"package": "/Game/Actors/Missing", "actor_name": "Missing_UAID"}])

    def test_pending_deletes_use_identities_even_when_raw_counts_match(self):
        self.live.extend([Actor("/Game/Actors/New1", "New1_UAID"),
                          Actor("/Game/Actors/New2", "New2_UAID")])
        self.descs.append(Desc("/Game/Actors/Missing", "Missing_UAID", self.native))
        self.assertFalse(L._brush_load_guard(self.world, 3, 3)[0])

    def test_live_count_mismatch_fails_closed(self):
        self.assertFalse(L._brush_load_guard(self.world, 2, 2)[0])

    def test_stale_journal_row_does_not_discount_an_unrelated_descriptor(self):
        self.rows = [deleted("/Game/Actors/Retired", "Retired_UAID")]
        ok, detail = L._brush_load_guard(self.world, 1, 2)
        self.assertFalse(ok)
        self.assertEqual(detail["pending_deleted_on_disk"], [])
        self.assertEqual(detail["effective_on_disk"], 2)

    def test_deletion_requires_exact_package_and_actor_name(self):
        for package, name in (("/Game/Actors/Other", "B_UAID"),
                              ("/Game/Actors/B", "Other_UAID")):
            with self.subTest(package=package, name=name):
                self.rows = [deleted(package, name)]
                ok, detail = L._brush_load_guard(self.world, 1, 2)
                self.assertFalse(ok)
                self.assertEqual(detail["pending_deleted_on_disk"], [])

    def test_restored_live_actor_is_not_exempted_as_deleted(self):
        self.live.append(Actor("/Game/Actors/B", "B_UAID"))
        ok, detail = L._brush_load_guard(self.world, 2, 2)
        self.assertTrue(ok)
        self.assertEqual(detail["pending_deleted_on_disk"], [])
        self.assertEqual(detail["effective_on_disk"], 2)

    def test_descriptor_count_mismatch_fails_closed(self):
        self.assertFalse(L._brush_load_guard(self.world, 1, 3)[0])

    def test_descriptor_enumeration_failure_fails_closed(self):
        self.enumerate.side_effect = lambda: (False, [])
        ok, detail = L._brush_load_guard(self.world, 1, 2)
        self.assertFalse(ok)
        self.assertIn("enumeration failed", detail["pending_deletion_check_error"])

    def test_history_error_fails_closed_even_with_matching_counts(self):
        self.history.side_effect = ValueError("Bad history")
        ok, detail = L._brush_load_guard(self.world, 2, 2)
        self.assertFalse(ok)
        self.assertIn("Bad history", detail["pending_deletion_check_error"])

    def test_ambiguous_descriptor_identity_fails_closed(self):
        self.descs.append(self.descs[0])
        ok, detail = L._brush_load_guard(self.world, 1, 3)
        self.assertFalse(ok)
        self.assertIn("ambiguous", detail["pending_deletion_check_error"])

    def test_no_pending_deletes_preserves_normal_count_guard(self):
        self.rows = []
        self.assertTrue(L._brush_load_guard(self.world, 2, 2)[0])
        self.assertFalse(L._brush_load_guard(self.world, 1, 2)[0])
        self.enumerate.assert_not_called()


if __name__ == "__main__":
    unittest.main()
