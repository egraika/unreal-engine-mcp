"""Offline independent-camera and cross-process view-store checks.

Run with normal Python; this module never imports Unreal or contacts the editor.
"""

import concurrent.futures
import datetime
import io
import json
import multiprocessing
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from helpers import prk_terrain_author_host as H
from PIL import Image


POSE = {"location": [110.0, 220.0, 330.0], "rotation": [-20.0, 40.0, 0.0]}
FIXED_NOW = datetime.datetime(2026, 10, 10, 12, 0, 0, 123456)


def _never_dispatch(*args, **kwargs):
    raise AssertionError("This operation must not contact the editor")


def _png(width=8, height=6, color=(10, 20, 30)):
    stream = io.BytesIO()
    Image.new("RGB", (width, height), color).save(stream, format="PNG")
    return stream.getvalue()


def _save_worker(root, worker, start, results):
    """Top-level worker so Windows uses real independent server processes."""
    try:
        start.wait(15)
        for index in range(6):
            name = f"agent{worker}_view{index}"
            location = [float(worker), float(index), 123.0]
            result = H.view(action="save", name=name, location=location,
                            rotation=POSE["rotation"], fov=73.0,
                            dispatch=_never_dispatch, root=root)
            if not result.get("ok"):
                raise AssertionError(result)
        results.put((worker, None))
    except BaseException as exc:
        results.put((worker, repr(exc)))


class FakeEditor:
    def __init__(self, *, capture_error=None, barrier=None, busy=False, partial=False):
        self.calls = []
        self.time = 0.0
        self.capture_error = capture_error
        self.barrier = barrier
        self.busy = busy
        self.partial = partial

    def sleep(self, seconds):
        self.time += seconds

    def clock(self):
        return self.time

    def dispatch(self, module, function, **kwargs):
        self.calls.append((module, function, kwargs))
        if function == "camera_set":
            raise AssertionError("Independent views must never move the editor camera")
        if function == "camera_get":
            return {"ok": True, **POSE}
        if function == "view_ready":
            return {"ok": True, "subsystem_idle": not self.busy,
                    "pie": False, "edit_mode_active": False}
        if function == "capture_view":
            if self.capture_error is not None:
                return {"ok": False, "error": self.capture_error}
            if self.barrier:
                self.barrier.wait(10)
            path = kwargs["path"]
            Path(path).write_bytes(_png()[:20] if self.partial else _png())
            return {"ok": True, "path": path, "camera_backend": "scene_capture_2d",
                    "editor_viewport_changed": False,
                    "camera": {k: kwargs[k] for k in ("location", "rotation", "fov")}}
        raise AssertionError(f"Unexpected editor operation: {function}")

    def view(self, root, **kwargs):
        return H.view(dispatch=self.dispatch, root=root, sleep=self.sleep,
                      clock=self.clock, now=lambda: FIXED_NOW, **kwargs)


class TerrainViewChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="terrain_view_checks_")
        self.root = self.temp.name

    def tearDown(self):
        self.temp.cleanup()

    def save(self, name="shared", fov=73.0):
        result = H.view(action="save", name=name, **POSE, fov=fov,
                        dispatch=_never_dispatch, root=self.root)
        self.assertTrue(result.get("ok"), result)
        return result

    def test_explicit_save_and_restore_do_not_contact_editor(self):
        self.save()
        listed = H.view(action="list", dispatch=_never_dispatch, root=self.root)
        self.assertEqual(listed["views"]["shared"]["location"], POSE["location"])
        self.assertEqual(listed["views"]["shared"]["fov"], 73.0)
        restored = H.view(action="restore", name="shared", dispatch=_never_dispatch, root=self.root)
        self.assertTrue(restored.get("ok"), restored)
        self.assertEqual(restored.get("camera_backend"), "scene_capture_2d")
        self.assertIs(restored.get("editor_viewport_changed"), False)

    def test_save_current_pose_only_reads_editor_camera(self):
        editor = FakeEditor()
        result = editor.view(self.root, action="save", name="current")
        self.assertTrue(result.get("ok"), result)
        self.assertEqual([call[1] for call in editor.calls], ["camera_get"])
        self.assertEqual(result["view"]["location"], POSE["location"])
        self.assertEqual(result["view"]["fov"], 90.0)

    def test_capture_uses_saved_pose_with_no_viewport_operations(self):
        self.save()
        editor = FakeEditor()
        result = editor.view(self.root, action="capture", name="shared", label="ridge_before",
                             settle_s=0.0, resolution_x=640, resolution_y=360)
        self.assertTrue(result.get("ok"), result)
        capture_calls = [call for call in editor.calls if call[1] == "capture_view"]
        self.assertEqual(len(capture_calls), 1)
        module, _, arguments = capture_calls[0]
        self.assertEqual(module, "prk_terrain_author")
        self.assertEqual(arguments["location"], POSE["location"])
        self.assertEqual(arguments["rotation"], POSE["rotation"])
        self.assertEqual(arguments["fov"], 73.0)
        self.assertEqual((arguments["resolution_x"], arguments["resolution_y"]), (640, 360))
        self.assertNotIn("camera_get", [call[1] for call in editor.calls])
        self.assertNotIn("camera_set", [call[1] for call in editor.calls])
        self.assertEqual(H._capture_label(result["path"]), "ridge_before")
        self.assertEqual(H.complete_png(result["path"]), result["size_bytes"])
        self.assertIs(result.get("editor_viewport_changed"), False)

    def test_explicit_capture_pose_does_not_modify_saved_pose(self):
        self.save()
        editor = FakeEditor()
        override = {"location": [400.0, 500.0, 600.0], "rotation": [-30.0, 80.0, 0.0]}
        result = editor.view(self.root, action="capture", name="shared", label="alternate",
                             **override, fov=55.0, settle_s=0.0)
        self.assertTrue(result.get("ok"), result)
        arguments = next(call[2] for call in editor.calls if call[1] == "capture_view")
        self.assertEqual(arguments["location"], override["location"])
        self.assertEqual(arguments["rotation"], override["rotation"])
        self.assertEqual(arguments["fov"], 55.0)
        listed = H.view(action="list", dispatch=_never_dispatch, root=self.root)
        self.assertEqual(listed["views"]["shared"]["location"], POSE["location"])
        self.assertEqual(listed["views"]["shared"]["fov"], 73.0)

    def test_capture_failure_has_no_viewport_fallback(self):
        self.save()
        editor = FakeEditor(capture_error="render failed")
        result = editor.view(self.root, action="capture", name="shared", settle_s=0.0)
        self.assertFalse(result.get("ok"), result)
        functions = [call[1] for call in editor.calls]
        self.assertEqual(functions.count("capture_view"), 1)
        self.assertNotIn("camera_get", functions)
        self.assertNotIn("camera_set", functions)

    def test_partial_capture_times_out_without_viewport_fallback(self):
        self.save()
        editor = FakeEditor(partial=True)
        result = editor.view(self.root, action="capture", name="shared", settle_s=0.0, timeout_s=2.0)
        self.assertFalse(result.get("ok"), result)
        self.assertIn("PNG", result.get("error", ""))
        self.assertGreaterEqual(editor.time, 2.0)
        self.assertNotIn("camera_set", [call[1] for call in editor.calls])

    def test_busy_subsystem_does_not_start_capture(self):
        self.save()
        editor = FakeEditor(busy=True)
        result = editor.view(self.root, action="capture", name="shared", settle_s=0.0, timeout_s=2.0)
        self.assertFalse(result.get("ok"), result)
        self.assertNotIn("capture_view", [call[1] for call in editor.calls])

    def test_incomplete_shader_wait_envelope_does_not_start_capture(self):
        self.save()
        editor = FakeEditor()
        shaders = {"status": "success", "result": {"success": True, "complete": False}}
        result = editor.view(self.root, action="capture", name="shared", settle_s=0.0,
                             wait_shaders=lambda: shaders)
        self.assertFalse(result.get("ok"), result)
        self.assertNotIn("capture_view", [call[1] for call in editor.calls])

    def test_failed_shader_wait_envelope_does_not_start_capture(self):
        self.save()
        editor = FakeEditor()
        shaders = {"status": "error", "error": "Shader wait failed"}
        result = editor.view(self.root, action="capture", name="shared", settle_s=0.0,
                             wait_shaders=lambda: shaders)
        self.assertFalse(result.get("ok"), result)
        self.assertNotIn("capture_view", [call[1] for call in editor.calls])

    def test_simultaneous_same_timestamp_captures_have_unique_outputs(self):
        self.save()
        barrier = threading.Barrier(4)
        editors = [FakeEditor(barrier=barrier) for _ in range(4)]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            futures = [pool.submit(editor.view, self.root, action="capture", name="shared",
                                   label="same_label", settle_s=0.0) for editor in editors]
            results = [future.result(timeout=15) for future in futures]
        for result in results:
            self.assertTrue(result.get("ok"), result)
            self.assertEqual(H._capture_label(result["path"]), "same_label")
            self.assertIsNotNone(H.complete_png(result["path"]))
        self.assertEqual(len({result["path"] for result in results}), 4)

    def test_different_name_saves_from_independent_processes_are_preserved(self):
        context = multiprocessing.get_context("spawn")
        start = context.Event()
        results = context.Queue()
        workers = [context.Process(target=_save_worker, args=(self.root, index, start, results))
                   for index in range(4)]
        try:
            for worker in workers:
                worker.start()
            start.set()
            replies = [results.get(timeout=30) for _ in workers]
            for index, error in replies:
                self.assertIsNone(error, f"worker {index}: {error}")
            for worker in workers:
                worker.join(timeout=10)
                self.assertEqual(worker.exitcode, 0)
            listed = H.view(action="list", dispatch=_never_dispatch, root=self.root)
            self.assertTrue(listed.get("ok"), listed)
            self.assertEqual(len(listed["views"]), 24)
            directory = Path(self.root, "Saved", "TerrainAuthor", "views")
            self.assertEqual(len(list(directory.glob("*.json"))), 24)
            for worker in range(4):
                for index in range(6):
                    name = f"agent{worker}_view{index}"
                    self.assertEqual(listed["views"][name]["location"], [float(worker), float(index), 123.0])
                    with (directory / f"{name}.json").open(encoding="utf-8") as stream:
                        self.assertIsInstance(json.load(stream), dict)
        finally:
            # Only these disposable test workers belong to this test.
            for worker in workers:
                if worker.is_alive():
                    worker.terminate()
                worker.join(timeout=5)
            results.close()

    def test_legacy_views_merge_with_independent_named_views(self):
        directory = Path(self.root, "Saved", "TerrainAuthor")
        directory.mkdir(parents=True)
        legacy = {"views": {"old": dict(POSE), "shared": {"location": [0, 0, 0], "rotation": [0, 0, 0]}}}
        (directory / "views.json").write_text(json.dumps(legacy), encoding="utf-8")
        self.save()
        listed = H.view(action="list", dispatch=_never_dispatch, root=self.root)
        self.assertEqual(set(listed["views"]), {"old", "shared"})
        self.assertEqual(listed["views"]["shared"]["location"], POSE["location"])
        editor = FakeEditor()
        result = editor.view(self.root, action="capture", name="old", settle_s=0.0)
        self.assertTrue(result.get("ok"), result)
        arguments = next(call[2] for call in editor.calls if call[1] == "capture_view")
        self.assertEqual(arguments["fov"], 90.0)
        self.assertEqual(json.loads((directory / "views.json").read_text(encoding="utf-8")), legacy)

    def test_legacy_mixed_case_names_and_file_stems_are_case_insensitive(self):
        directory = Path(self.root, "Saved", "TerrainAuthor")
        directory.mkdir(parents=True)
        legacy = {"views": {"HC_Old": dict(POSE), "Ridge_View": {"location": [0, 0, 0], "rotation": [0, 0, 0]}}}
        (directory / "views.json").write_text(json.dumps(legacy), encoding="utf-8")
        (directory / "views").mkdir()
        (directory / "views" / "RIDGE_View.json").write_text(json.dumps({**POSE, "fov": 60.0}), encoding="utf-8")
        listed = H.view(action="list", dispatch=_never_dispatch, root=self.root)
        self.assertEqual(set(listed["views"]), {"hc_old", "ridge_view"})
        for variant in ("hc_old", "HC_OLD", "Hc_Old"):
            restored = H.view(action="restore", name=variant, dispatch=_never_dispatch, root=self.root)
            self.assertTrue(restored.get("ok"), restored)
            self.assertEqual(restored["name"], "hc_old")
            self.assertEqual(restored["view"]["location"], POSE["location"])
        restored = H.view(action="restore", name="Ridge_VIEW", dispatch=_never_dispatch, root=self.root)
        self.assertEqual(restored["view"]["fov"], 60.0)
        editor = FakeEditor()
        captured = editor.view(self.root, action="capture", name="HC_old", label="Before", settle_s=0.0)
        self.assertTrue(captured.get("ok"), captured)
        self.assertEqual(captured["name"], "hc_old")
        self.assertEqual(Path(captured["path"]).parent.name, "hc_old")
        self.assertEqual(H._capture_label(captured["path"]), "Before")
        self.assertEqual(json.loads((directory / "views.json").read_text(encoding="utf-8")), legacy)

    def test_case_variant_saves_restore_and_capture_share_one_camera(self):
        first = self.save(name="HC_View")
        self.assertEqual(first["name"], "hc_view")
        replacement = {"location": [400.0, 500.0, 600.0], "rotation": [-30.0, 80.0, 0.0]}
        saved = H.view(action="save", name="hc_vIEW", **replacement, fov=65.0,
                       dispatch=_never_dispatch, root=self.root)
        self.assertTrue(saved.get("ok"), saved)
        self.assertEqual(saved["views_path"], first["views_path"])
        directory = Path(self.root, "Saved", "TerrainAuthor", "views")
        self.assertEqual([path.name for path in directory.glob("*.json")], ["hc_view.json"])
        listed = H.view(action="list", dispatch=_never_dispatch, root=self.root)
        self.assertEqual(set(listed["views"]), {"hc_view"})
        restored = H.view(action="restore", name="HC_VIEW", dispatch=_never_dispatch, root=self.root)
        self.assertEqual(restored["view"]["location"], replacement["location"])
        editor = FakeEditor()
        before = editor.view(self.root, action="capture", name="Hc_View", label="Before", settle_s=0.0)
        after = editor.view(self.root, action="capture", name="hC_vIeW", label="After", settle_s=0.0)
        for captured in (before, after):
            self.assertTrue(captured.get("ok"), captured)
            self.assertEqual(captured["name"], "hc_view")
            self.assertEqual(Path(captured["path"]).parent.name, "hc_view")
        arguments = next(call[2] for call in editor.calls if call[1] == "capture_view")
        self.assertEqual(arguments["location"], replacement["location"])
        comparison = H.view(action="compare", name="HC_vIEW", labels=["Before", "After"],
                            dispatch=_never_dispatch, root=self.root)
        self.assertTrue(comparison.get("ok"), comparison)
        self.assertEqual(comparison["left"], before["path"])
        self.assertEqual(comparison["right"], after["path"])
        self.assertEqual(Path(comparison["compare"]).parent.name, "hc_view")

    def test_compare_labels_accept_legacy_capture_names_and_unique_outputs(self):
        directory = Path(self.root, "Saved", "TerrainAuthor", "captures", "comparison")
        directory.mkdir(parents=True)
        before = directory / "20261009-100000_ridge_before.png"
        after = directory / "20261009-110000_ridge_after.png"
        before.write_bytes(_png(8, 6, (255, 0, 0)))
        after.write_bytes(_png(16, 12, (0, 255, 0)))
        # A concurrently writing PNG must never become a selected comparison source.
        (directory / "99999999-999999_ridge_after.png").write_bytes(_png()[:20])
        first = H.compare("comparison", labels=["ridge_before", "ridge_after"], root=self.root)
        second = H.compare("comparison", labels=["ridge_before", "ridge_after"], root=self.root)
        for result in (first, second):
            self.assertTrue(result.get("ok"), result)
            self.assertEqual(result["left"], str(before))
            self.assertEqual(result["right"], str(after))
            self.assertEqual(result["size"], [16, 6])
            self.assertTrue(Path(result["compare"]).name.startswith("compare_"))
            self.assertIsNotNone(H.complete_png(result["compare"]))
        self.assertNotEqual(first["compare"], second["compare"])

    def test_invalid_camera_inputs_fail_before_editor_contact(self):
        invalid = [
            {"location": [True, 2, 3]}, {"location": [float("nan"), 2, 3]},
            {"location": [float("inf"), 2, 3]}, {"location": ["1", 2, 3]},
            {"location": [1, 2]}, {"rotation": [0, False, 0]},
            {"rotation": [0, float("nan"), 0]}, {"rotation": [0, float("inf"), 0]},
            {"rotation": [0, "40", 0]}, {"rotation": [0, 40]},
            {"fov": True}, {"fov": "90"}, {"fov": float("nan")},
            {"fov": float("inf")}, {"fov": 0}, {"fov": 180},
        ]
        for bad in invalid:
            arguments = {**POSE, "fov": 90.0, **bad}
            with self.subTest(bad=bad):
                result = H.view(action="save", name="invalid", **arguments,
                                dispatch=_never_dispatch, root=self.root)
                self.assertFalse(result.get("ok"), result)
        for partial in ({"location": POSE["location"]}, {"rotation": POSE["rotation"]}):
            with self.subTest(partial=partial):
                result = H.view(action="save", name="invalid", **partial,
                                dispatch=_never_dispatch, root=self.root)
                self.assertFalse(result.get("ok"), result)

    def test_invalid_capture_dimensions_fail_before_editor_contact(self):
        self.save()
        for width, height in ((True, 16), (16, False), (15, 16), (16, 15),
                              (8193, 16), (16, 8193), (8192, 8192),
                              (16.5, 16), (16, "16"), (float("inf"), 16)):
            with self.subTest(width=width, height=height):
                result = H.view(action="capture", name="shared", resolution_x=width,
                                resolution_y=height, dispatch=_never_dispatch, root=self.root)
                self.assertFalse(result.get("ok"), result)


if __name__ == "__main__":
    unittest.main(verbosity=2)
