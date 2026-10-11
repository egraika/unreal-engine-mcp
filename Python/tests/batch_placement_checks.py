"""Offline regression checks for batch-placement transform marshaling; no editor calls."""

import ast
import copy
import math
from pathlib import Path
import struct
from typing import Any, Dict, List
import unittest
from unittest import mock


# Execute only the actual wrapper and its pure helpers. This keeps the checks
# independent of FastMCP versions, sockets, and the interactive editor.
source = Path(__file__).resolve().parents[1] / "unreal_mcp_server_advanced.py"
names = {"_normalize_batch_transform", "_normalize_batch_actor_entries", "place_actors_batch"}
definitions = []
for node in ast.parse(source.read_text(encoding="utf-8")).body:
    if isinstance(node, ast.FunctionDef) and node.name in names:
        node.decorator_list = []
        definitions.append(node)
assert len(definitions) == len(names), "Batch-placement wrapper or helper is missing"
namespace = {"Any": Any, "Dict": Dict, "List": List, "math": math, "struct": struct}
exec(compile(ast.Module(body=definitions, type_ignores=[]), str(source), "exec"), namespace)


class BatchPlacementChecks(unittest.TestCase):
    def setUp(self):
        self.connection = mock.Mock()
        self.connection.send_command.return_value = {"success": True}
        self.guard = mock.Mock(return_value=None)
        self.get_connection = mock.Mock(return_value=self.connection)
        namespace.update(_errant_brush_guard=self.guard,
                         get_unreal_connection=self.get_connection,
                         logger=mock.Mock())
        self.place = namespace["place_actors_batch"]

    def test_dictionary_transforms_reach_native_command_as_ordered_arrays(self):
        entry = {"actor_class": "StaticMeshActor", "name": "RockA",
                 "location": {"z": 30, "x": 10, "y": 20},
                 "rotation": {"Yaw": 45, "Roll": 5, "Pitch": -10},
                 "scale": {"X": 2, "Y": 3, "Z": 4},
                 "static_mesh": "/Game/PRK/Environment/DesertRocks/SM_Rock",
                 "properties": {"bHidden": False}}
        original = copy.deepcopy(entry)
        self.assertTrue(self.place([entry], stop_on_error=False, select_spawned=True)["success"])
        command, payload = self.connection.send_command.call_args.args
        self.assertEqual(command, "place_actors_batch")
        self.assertEqual(payload["actors"][0], dict(entry, location=[10, 20, 30],
                                                   rotation=[-10, 45, 5], scale=[2, 3, 4]))
        self.assertFalse(payload["stop_on_error"])
        self.assertTrue(payload["select_spawned"])
        self.assertEqual(entry, original)

    def test_arrays_and_negative_scale_remain_supported(self):
        entry = {"type": "StaticMeshActor", "location": [-1, 0, 7],
                 "rotation": [1, 2, 3], "scale": [-2, 1, 0.25]}
        self.assertTrue(self.place([entry])["success"])
        self.assertEqual(self.connection.send_command.call_args.args[1]["actors"], [entry])

    def test_omitted_transforms_keep_native_defaults(self):
        entry = {"actor_class": "StaticMeshActor"}
        self.assertTrue(self.place([entry])["success"])
        self.assertEqual(self.connection.send_command.call_args.args[1]["actors"], [entry])

    def test_invalid_transforms_refuse_entire_batch_before_any_dispatch(self):
        invalid = [("location", {"x": 1, "y": 2}),
                   ("location", {"x": 1, "X": 1, "y": 2, "z": 3}),
                   ("rotation", {"x": 1, "y": 2, "z": 3}),
                   ("rotation", [0, "90", 0]), ("location", [0, 0]),
                   ("scale", [1, 0, 1]), ("scale", [1, -0.0, 1]),
                   ("scale", [True, 1, 1]), ("location", [0, math.nan, 0]),
                   ("rotation", [0, math.inf, 0]), ("scale", [1, 1, -math.inf]),
                   ("location", None), ("location", [0, 10**1000, 0]),
                   ("location", [0, 1e300, 0]), ("scale", [1e-300, 1, 1])]
        for field, value in invalid:
            with self.subTest(field=field, value=value):
                result = self.place([{"actor_class": "StaticMeshActor"},
                                     {"actor_class": "StaticMeshActor", field: value}],
                                    stop_on_error=False)
                self.assertFalse(result["success"])
                self.assertTrue(result["refused"])
                self.assertIn(f"actors[1].{field}", result["message"])
                self.guard.assert_not_called()
                self.get_connection.assert_not_called()
                self.connection.send_command.assert_not_called()

    def test_invalid_entries_refuse_without_native_dispatch(self):
        for actors in (None, {"actor_class": "StaticMeshActor"}, [None], ["StaticMeshActor"]):
            with self.subTest(actors=actors):
                self.assertTrue(self.place(actors)["refused"])
                self.connection.send_command.assert_not_called()

    def test_errant_brush_guard_still_prevents_spawn(self):
        self.guard.return_value = {"success": False, "refused": True}
        self.assertTrue(self.place([{"actor_class": "ElStampBrushActor"}])["refused"])
        self.guard.assert_called_once_with(["ElStampBrushActor"], [], "place_actors_batch")
        self.connection.send_command.assert_not_called()


if __name__ == "__main__":
    unittest.main()
