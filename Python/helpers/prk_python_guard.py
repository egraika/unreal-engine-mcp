"""Deny-list for run_python / run_python_file (runs in the MCP server, never in the editor).

Each entry names an editor-fatal or world-destroying call that already cost an editor or
a map, and the safe tool to use instead. Matching is a plain case-insensitive regex over
the source text, so camelCase C++ names and their snake_case Python bindings both match.
It catches accidents; it does not try to defeat deliberate obfuscation.
"""

from __future__ import annotations

import re
from typing import Any, Dict, List, Optional

# (id, pattern, why, use instead)
_DENY = [
    (
        "texture_exporter",
        re.compile(r"texture_?exporter", re.IGNORECASE),
        "Texture exporters assert on formats they cannot write (16-bit heightmaps): "
        "TextureExporterTGA killed the editor on 2026-10-07.",
        "texture_preview (reads the .uasset thumbnail in the MCP server, no editor call)",
    ),
    (
        "force_update_all_landscape",
        re.compile(r"force_?update_?all_?landscape", re.IGNORECASE),
        "An unattended ForceUpdateAllLandscape wiped every landscape component's weightmap "
        "allocations on 2026-09-01.",
        "errant_stamp_configure (one stamp brush, bounded per-brush update)",
    ),
    (
        "el_edit_mode",
        re.compile(r"(?:start|end)_?edit_?mode_?2", re.IGNORECASE),
        "StartEditMode2/EndEditMode2 flip only the brush-subsystem flag and leave the editor mode out of "
        "step (and StartEditMode2 + ForceUpdateAllLandscape2 was the 2026-09-01 weightmap wipe).",
        "errant_edit_mode_set (the toolbar's mode-manager path, guarded: brushes loaded, bridges, backup, idle)",
    ),
    (
        "el_edit_mode_raw_bridge",
        re.compile(r"set_?errant_?landscape_?edit_?mode", re.IGNORECASE),
        "The bridge's SetErrantLandscapeEditMode skips the content guards (full brush set, bridges "
        "registered, backup, idle subsystem) that a mode exit's synchronous update needs.",
        "errant_edit_mode_set",
    ),
    (
        "el_bridge_settings_write",
        re.compile(r"set_\w*\([^)]*per_?landscape_?settings"
                   r"|set_editor_property\(\s*['\"]weightmap_method['\"]", re.IGNORECASE),
        "Writing ElBridge PerLandscapeSettings dirtied all 256 landscape proxies and rebuilt every "
        "stamp texture on 2026-10-07.",
        "ask Eric to change the ElBridge in the Details panel; errant_landscape_bridge_status reads it",
    ),
]

_SPAWN_OR_DESTROY = re.compile(r"spawn_?actor\w*|destroy_?actor\w*|\.destroy\(\)", re.IGNORECASE)
_EL_BRUSH = re.compile(r"ErrantLandscapeStampLibrary|El\w*Brush\w*Actor|El_?Stamp_?Brush|/ErrantLandscape/",
                       re.IGNORECASE)

_EXPORT_TASK = re.compile(r"asset_?export_?task", re.IGNORECASE)
# "texture" anywhere (TextureRenderTarget included), a render target, or a texture-named
# asset (a path segment or name starting with T_).
_TEXTURE = re.compile(r"(?i:texture|render_?target)|[/'\".]T_\w+")


def check_python_source(code: str) -> List[Dict[str, str]]:
    """Every deny-list entry the source matches (empty list = allowed)."""
    hits = []
    for rule_id, pattern, why, alt in _DENY:
        if pattern.search(code or ""):
            hits.append({"rule": rule_id, "why": why, "use_instead": alt})
    if _EXPORT_TASK.search(code or "") and _TEXTURE.search(code or ""):
        hits.append({
            "rule": "asset_export_task_texture",
            "why": "AssetExportTask on a texture runs a TextureExporter, which asserts on unsupported "
                   "formats (editor-fatal on 2026-10-07).",
            "use_instead": "texture_preview (reads the .uasset thumbnail in the MCP server, no editor call)",
        })
    if _SPAWN_OR_DESTROY.search(code or "") and _EL_BRUSH.search(code or ""):
        hits.append({
            "rule": "el_brush_spawn_destroy",
            "why": "Spawning or destroying Errant Landscape brushes from a raw script re-renders the region "
                   "and erased canyon stamps on 2026-09-01.",
            "use_instead": "errant_stamp_spawn / errant_stamp_delete (guarded: edit mode, all brushes loaded, "
                           "backup, priority, proxy budget)",
        })
    return hits


_BRIDGE_FIELD = re.compile(r"per_?landscape_?settings|weightmap_?method", re.IGNORECASE)
_EDIT_MODE_FN = re.compile(r"(?:set_?errant_?landscape_?edit_?mode|(?:start|end)_?edit_?mode_?2)", re.IGNORECASE)


def check_bridge_write(target: str, member: str, args: Optional[List[Any]] = None) -> List[Dict[str, str]]:
    """Deny-list for the generic set_object_property / set_array_property / invoke_function
    tools: refuse any write whose property path, function name or args touch ElBridge
    PerLandscapeSettings / WeightmapMethod. The target string is not required to say
    "ElBridge": AElBridge::UpdateActorLabel renames bridges after their landscape layer
    (e.g. "Errant Landscape"), and these fields exist only on ElBridge."""
    text = " ".join([member or ""] + [str(a) for a in (args or [])])
    if _EDIT_MODE_FN.search(member or ""):
        return [{
            "rule": "el_edit_mode_raw_bridge",
            "why": "SetErrantLandscapeEditMode called directly skips the content guards (full brush set, "
                   "bridges registered, backup, idle subsystem).",
            "use_instead": "errant_edit_mode_set",
        }]
    if not _BRIDGE_FIELD.search(text):
        return []
    return [{
        "rule": "el_bridge_settings_write",
        "why": f"Write to ElBridge PerLandscapeSettings / WeightmapMethod on '{target}': a bridge edit "
               "dirtied all 256 landscape proxies and rebuilt every stamp texture on 2026-10-07.",
        "use_instead": "ask Eric to change the ElBridge in the Details panel; errant_landscape_bridge_status reads it",
    }]


def brush_tool_hit(detail: str) -> Dict[str, str]:
    return {
        "rule": "el_brush_spawn_destroy",
        "why": f"{detail}: spawning or destroying Errant Landscape brushes outside the guarded tools "
               "re-renders the region and erased canyon stamps on 2026-09-01.",
        "use_instead": "errant_stamp_spawn / errant_stamp_delete (guarded: edit mode, all brushes loaded, "
                       "backup, priority, proxy budget)",
    }


def check_brush_class_text(classes: List[Any]) -> List[Dict[str, str]]:
    """Text-only check for the generic spawn tools (spawn_actor / place_actors_batch): class strings
    naming an Errant brush class or the stamp library. The editor-side errant_brush_check catches
    stamp classes elsewhere by their native parent."""
    named = [str(c) for c in classes if c and _EL_BRUSH.search(str(c))]
    return [brush_tool_hit(f"Errant brush class {named}")] if named else []


def refusal(hits: List[Dict[str, str]], source: str) -> Optional[Dict[str, Any]]:
    if not hits:
        return None
    return {
        "success": False,
        "refused": True,
        "message": f"Refused before reaching the editor: {source} matches the run_python deny-list ("
                   + ", ".join(h["rule"] for h in hits) + ").",
        "matched": hits,
        "use_instead": sorted({h["use_instead"] for h in hits}),
    }
