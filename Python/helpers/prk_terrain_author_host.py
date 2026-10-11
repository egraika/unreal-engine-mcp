"""MCP-host side of the macro terrain authoring tools (prk_terrain_author in the editor).

Runs in the MCP SERVER process: preset / calibration files, k resolution (calibration table, else a thumbnail
estimate), the bead-apply loop, the view store and capture polling, side-by-side comparisons, and the Perforce
half of the scoped save. Every editor operation is one ``dispatch(module, func, **kwargs)`` call (the server
passes _run_prk_module); every wait sleeps HERE so the editor ticks between calls. ``sleep`` / ``clock`` /
``now`` / ``wait_shaders`` / ``p4`` are injected so the loops are testable offline
(tests/terrain_author_checks.py).
"""

from __future__ import annotations

import datetime
import importlib
import json
import math
import os
import re
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

from helpers import prk_texture_preview

EDITOR_MODULE = "prk_terrain_author"
PNG_END = b"\0\0\0\0IEND\xaeB`\x82"
_LABEL_RE = re.compile(r"^[A-Za-z0-9_-]{1,40}$")
_POLL_S = 0.5


def _root(root: Optional[str] = None) -> str:
    r = root or prk_texture_preview.project_root()
    if not r:
        raise RuntimeError("No .uproject found above the MCP server directory.")
    return r


def _macro_geom(root: Optional[str] = None):
    path = os.path.join(_root(root), "Content", "Python")
    if path not in sys.path:
        sys.path.insert(0, path)
    import prk_macro_geom
    return importlib.reload(prk_macro_geom)


def _read_json(path: str) -> Optional[Dict[str, Any]]:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return None


def _write_json(path: str, data: Dict[str, Any]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix="." + os.path.basename(path) + ".", suffix=".tmp",
                               dir=os.path.dirname(path))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, allow_nan=False)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def _object_path(path: str) -> str:
    path = path.strip()
    tail = path.rsplit("/", 1)[-1]
    return path if "." in tail else f"{path}.{tail}"


# ---------------------------------------------------------------------------
# Presets and k
# ---------------------------------------------------------------------------

def presets_path(root: Optional[str] = None) -> str:
    return os.path.join(_root(root), "Config", "Editor", "ZoneAuthoring", "macro_presets.json")


def calibration_path(root: Optional[str] = None) -> str:
    return os.path.join(_root(root), "Config", "Editor", "ZoneAuthoring", "stamp_calibration.json")


def load_presets(root: Optional[str] = None) -> Dict[str, Any]:
    data = _read_json(presets_path(root))
    if data is None:
        raise FileNotFoundError(presets_path(root))
    return data


def resolve_k(texture: Optional[str], root: Optional[str] = None, preview: Optional[Callable] = None,
              out_dir: Optional[str] = None) -> Dict[str, Any]:
    """{k_peak, polarity, source, estimated} for a heightmap texture: the calibration table entry (measured or
    estimated) first, else a thumbnail estimate (k = (max/255)^2.2, polarity from centre vs mean)."""
    if not texture:
        return {"k_peak": None, "polarity": "peak", "source": "no texture", "estimated": True}
    table = (_read_json(calibration_path(root)) or {}).get("textures", {})
    entry = table.get(texture) or table.get(_object_path(texture))
    if entry and entry.get("k_peak"):
        return {"k_peak": float(entry["k_peak"]), "polarity": entry.get("polarity", "peak"),
                "source": f"calibration: {entry.get('source', '')}".strip(), "estimated": bool(entry.get("estimated"))}
    preview = preview or prk_texture_preview.texture_preview
    res = preview(texture, out_dir or os.path.join(tempfile.gettempdir(), "prk_terrain_author_k"))
    if not res.get("success"):
        return {"k_peak": None, "polarity": "peak", "estimated": True,
                "source": f"thumbnail unavailable: {res.get('message')}"}
    g = _macro_geom(root)
    return {"k_peak": round(g.thumbnail_k_estimate(res["max"]), 4),
            "polarity": g.thumbnail_polarity(res["mean"], res["centre_mean"]),
            "source": f"thumbnail estimate (gamma 2.2): max {res['max']}, mean {res['mean']}, "
                      f"centre {res['centre_mean']}", "estimated": True}


def resolve_preset(name: str, root: Optional[str] = None, preview: Optional[Callable] = None) -> Dict[str, Any]:
    presets = load_presets(root).get("presets", {})
    if name not in presets:
        raise KeyError(f"Unknown preset '{name}'. Known: {sorted(presets)}")
    p = dict(presets[name])
    k = resolve_k(p.get("texture"), root, preview)
    p.update(name=name, k_peak=k["k_peak"], k_source=k["source"], k_estimated=k["estimated"],
             polarity=k["polarity"])
    return p


def macro_presets(root: Optional[str] = None, preview: Optional[Callable] = None) -> Dict[str, Any]:
    data = load_presets(root)
    out = {name: resolve_preset(name, root, preview) for name in data.get("presets", {})}
    return {"ok": True, "note": data.get("note"), "presets": out, "presets_path": presets_path(root),
            "calibration_path": calibration_path(root)}


# ---------------------------------------------------------------------------
# Relief, plan, apply
# ---------------------------------------------------------------------------

def stamp_relief(*, dispatch: Callable, actor: str, k_peak: Optional[float] = None, root: Optional[str] = None,
                 preview: Optional[Callable] = None, expect_world: str,
                 sleep: Callable = time.sleep, clock: Callable = time.monotonic,
                 timeout_s: float = 30.0, **kwargs) -> Dict[str, Any]:
    """With committed terrain, plan first and apply in a guarded, camera-preserving edit-mode cycle.

    Calls already in edit mode retain batch behaviour; terrain-relative targets still refuse stale heights.
    Sleeps happen on the host so brush updates can tick before the guarded mode exit commits collision.
    """
    src = dispatch(EDITOR_MODULE, "stamp_source", actor=actor, expect_world=expect_world)
    if not src.get("ok"):
        return src
    if k_peak is not None:
        k = {"k_peak": float(k_peak), "polarity": "peak", "source": "caller", "estimated": False}
        cal = resolve_k(src.get("texture"), root, preview) if src.get("texture") else {}
        k["polarity"] = cal.get("polarity", "peak")
    else:
        k = resolve_k(src.get("texture"), root, preview)
    managed_mode = not kwargs.get("dry_run", True) and src.get("edit_mode_active") is False
    relief_args = dict(kwargs)
    if managed_mode:
        relief_args["dry_run"] = True
    res = dispatch(EDITOR_MODULE, "stamp_relief", actor=actor, k_peak=k["k_peak"], k_source=k["source"],
                   polarity=k["polarity"], expect_world=expect_world, **relief_args)
    res["k_estimated"] = k["estimated"]
    if k["estimated"]:
        res.setdefault("notes", []).append("k_peak is a source-model or thumbnail estimate; tune the actual "
                                           "terrain in the viewport. Optional errant_feature_calibrate(record=False) "
                                           "reports composed bead centres and cannot establish or record a texture peak.")
    if not managed_mode or not res.get("ok"):
        return res

    mode_args = {"dry_run": False,
                 "expect_world": expect_world, "preserve_camera": True}
    entered = dispatch("prk_lake_ops", "edit_mode_set", active=True, **mode_args)
    res.update(dry_run=False, mode_enter=entered, applied=False, committed=False)
    if not entered.get("ok"):
        res.update(ok=False, error=entered.get("error") or "Could not enter Errant edit mode.")
        return res

    try:
        cfg = dispatch("prk_lake_ops", "stamp_configure", actor=src.get("actor") or actor,
                       dry_run=False, expect_world=expect_world,
                       max_dirty_proxies=kwargs.get("max_dirty_proxies", 12), **res["configure_args"])
    except Exception as e:  # still leave the mode we entered; never replay an uncertain write
        cfg = {"ok": False, "error": f"{type(e).__name__}: {e}", "write_uncertain": True}
    res.update(configure=cfg, applied=bool(cfg.get("ok")), ok=bool(cfg.get("ok")))
    if not cfg.get("ok"):
        res.update(error=cfg.get("error") or "Stamp configure failed.", refused=cfg.get("refused", False))

    deadline = clock() + float(timeout_s)
    while True:
        try:
            ready = dispatch(EDITOR_MODULE, "view_ready")
        except Exception as e:
            res.update(ok=False, error=f"Could not read brush readiness before mode exit: {type(e).__name__}: {e}")
            return res
        if ready.get("ok") and ready.get("subsystem_idle") is not False and not ready.get("pie"):
            break
        if clock() >= deadline:
            res.update(ok=False, error="Timed out waiting for brush updates before leaving Errant edit mode.",
                       view_ready=ready)
            return res
        sleep(_POLL_S)
    try:
        exited = dispatch("prk_lake_ops", "edit_mode_set", active=False, **mode_args)
    except Exception as e:
        res.update(ok=False, error=f"Mode exit response uncertain: {type(e).__name__}: {e}",
                   commit_uncertain=True)
        return res
    res.update(mode_exit=exited, committed=bool(exited.get("ok")),
               ok=bool(cfg.get("ok")) and bool(exited.get("ok")))
    if not exited.get("ok"):
        res["error"] = exited.get("error") or "Stamp applied, but could not leave Errant edit mode."
    return res


def feature_plan(*, dispatch: Callable, preset: str, root: Optional[str] = None, preview: Optional[Callable] = None,
                 **kwargs) -> Dict[str, Any]:
    p = resolve_preset(preset, root, preview)
    if not p.get("k_peak"):
        return {"ok": False, "refused": True, "error": f"No k_peak for {p.get('texture')}: {p.get('k_source')}"}
    return dispatch(EDITOR_MODULE, "feature_plan", preset=p, **kwargs)


def feature_apply(*, dispatch: Callable, name: str, replace: bool = False, dry_run: bool = True,
                  beads_per_call: int = 4, expect_world: str,
                  sleep: Callable = time.sleep) -> Dict[str, Any]:
    """Loop prk_terrain_author.feature_apply over start indices, beads_per_call at a time, sleeping 0.5 s between
    calls so the editor ticks. A real replace deletes only in the first call (later calls resume by label).
    spawned / deleted are row arrays; skipped_existing is a label array. total is the planned bead count.
    A newest valid empty plan is one no-op call without replace; explicit replace=True retires the old owned
    beads and preserves their deletion journal for scoped save. Unavailable-ground plans never delete."""
    if isinstance(beads_per_call, bool) or not isinstance(beads_per_call, int) or not 1 <= beads_per_call <= 20:
        return {"ok": False, "refused": True, "error": "beads_per_call must be an int in 1..20."}
    start, first, calls = 0, True, []
    agg: Dict[str, Any] = {"spawned": [], "deleted": [], "skipped_existing": [], "warnings": []}
    res: Dict[str, Any] = {}
    while True:
        res = dispatch(EDITOR_MODULE, "feature_apply", name=name, start=start, max_beads=beads_per_call,
                       replace=bool(replace) and (first or dry_run), dry_run=dry_run,
                       expect_world=expect_world)
        calls.append({"start": start, "ok": res.get("ok"), "error": res.get("error")})
        for key in ("spawned", "deleted", "skipped_existing"):
            agg[key] += res.get(key) or []
        for w in res.get("warnings") or []:
            if w not in agg["warnings"]:
                agg["warnings"].append(w)
        if not res.get("ok") and not (dry_run and res.get("next_start") is not None):
            break  # a dry run walks every batch so every guard is reported
        first = False
        nxt = res.get("next_start")
        if nxt is None or nxt <= start:
            break
        start = nxt
        sleep(_POLL_S)
    out = {"ok": all(c["ok"] for c in calls), "dry_run": dry_run, "name": name, "calls": calls}
    out.update(agg)
    for key in ("error", "failed", "conflicts", "applied_path", "dirty_after_call", "note", "edit_mode_active",
                "total", "status", "nothing_to_apply", "no_cut_needed"):
        if key in res:
            out[key] = res[key]
    return out


# ---------------------------------------------------------------------------
# Views
# ---------------------------------------------------------------------------

def _views_path(root: Optional[str]) -> str:
    return os.path.join(_root(root), "Saved", "TerrainAuthor", "views.json")


def _views_dir(root: Optional[str]) -> str:
    return os.path.join(_root(root), "Saved", "TerrainAuthor", "views")


def _view_path(root: Optional[str], name: str) -> str:
    return os.path.join(_views_dir(root), name.casefold() + ".json")


def _load_views(root: Optional[str]) -> Dict[str, Any]:
    # Existing named views remain readable. New saves are independent atomic files,
    # so different MCP processes never race a shared read/modify/write operation.
    views = {name.casefold(): value
             for name, value in (_read_json(_views_path(root)) or {}).get("views", {}).items()}
    directory = _views_dir(root)
    if os.path.isdir(directory):
        for path in sorted(Path(directory).glob("*.json")):
            if _LABEL_RE.fullmatch(path.stem):
                value = _read_json(str(path))
                if value is not None:
                    views[path.stem.casefold()] = value
    return views


def _camera_pose(location: Any, rotation: Any, fov: Any) -> Dict[str, Any]:
    def vector(value: Any, label: str) -> List[float]:
        if not isinstance(value, (list, tuple)) or len(value) != 3:
            raise ValueError(f"{label} must contain three finite numbers.")
        if any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in value):
            raise ValueError(f"{label} must contain three finite numbers.")
        return [float(v) for v in value]
    if isinstance(fov, bool) or not isinstance(fov, (int, float)) or not math.isfinite(fov) or not 5 <= fov <= 170:
        raise ValueError("fov must be a finite number in 5..170 degrees.")
    return {"location": vector(location, "location"), "rotation": vector(rotation, "rotation"), "fov": float(fov)}


def _capture_id(now: Callable = datetime.datetime.now) -> str:
    return now().strftime("%Y%m%d-%H%M%S-%f") + "-" + uuid.uuid4().hex


def _captures_dir(root: Optional[str], name: str) -> str:
    return os.path.join(_root(root), "Saved", "TerrainAuthor", "captures", name.casefold())


def complete_png(path: str) -> Optional[int]:
    """File size when the PNG is complete (signature + IEND chunk at the end), else None."""
    p = Path(path)
    try:
        if not p.is_file():
            return None
        size = p.stat().st_size
        if size < 20:
            return None
        with p.open("rb") as f:
            head = f.read(8)
            f.seek(-12, 2)
            tail = f.read()
        return size if head == b"\x89PNG\r\n\x1a\n" and tail == PNG_END else None
    except OSError:
        return None  # Unreal holds the file exclusively while writing


def _captures(root: Optional[str], name: str) -> List[str]:
    d = _captures_dir(root, name)
    if not os.path.isdir(d):
        return []
    return sorted(os.path.join(d, f) for f in os.listdir(d)
                  if f.endswith(".png") and not f.startswith("compare_") and complete_png(os.path.join(d, f)))


def _capture_label(path: str) -> str:
    stem = os.path.splitext(os.path.basename(path))[0]
    return stem.split("_", 1)[1] if "_" in stem else stem  # <timestamp>_<label>; the timestamp has no "_"


def compare(name: str, labels: Optional[List[str]] = None, root: Optional[str] = None) -> Dict[str, Any]:
    """Stitch two captures of a view side by side (default the latest two; labels [a, b] picks the latest
    capture with each label)."""
    from PIL import Image
    name = name.casefold()
    caps = _captures(root, name)
    if labels:
        if len(labels) != 2:
            return {"ok": False, "error": "labels must be [a, b]."}
        picked = []
        for lbl in labels:
            hits = [c for c in caps if _capture_label(c) == lbl]
            if not hits:
                return {"ok": False, "error": f"No capture labelled '{lbl}' for view '{name}'."}
            picked.append(hits[-1])
    else:
        if len(caps) < 2:
            return {"ok": False, "error": f"View '{name}' has {len(caps)} capture(s); compare needs two."}
        picked = caps[-2:]
    a, b = (Image.open(p).convert("RGB") for p in picked)
    if b.height != a.height:
        b = b.resize((max(1, round(b.width * a.height / b.height)), a.height))
    out = Image.new("RGB", (a.width + b.width, a.height))
    out.paste(a, (0, 0))
    out.paste(b, (a.width, 0))
    la, lb = (_capture_label(p) for p in picked)
    path = os.path.join(_captures_dir(root, name), f"compare_{_capture_id()}_{la}_{lb}.png")
    out.save(path)
    return {"ok": True, "left": picked[0], "right": picked[1], "compare": path, "size": [out.width, out.height]}


def view(*, action: str, dispatch: Callable,
         wait_shaders: Optional[Callable] = None, name: str = "", label: str = "", settle_s: float = 2.0,
         timeout_s: float = 60.0, resolution_x: int = 1920, resolution_y: int = 1080,
         labels: Optional[List[str]] = None, location: Optional[List[float]] = None,
         rotation: Optional[List[float]] = None, fov: Optional[float] = None,
         root: Optional[str] = None, sleep: Callable = time.sleep,
         clock: Callable = time.monotonic, now: Callable = datetime.datetime.now) -> Dict[str, Any]:
    """Named independent capture cameras. No action moves or restores the user's editor viewport.

    Save can read the current viewport pose, or use explicit location / rotation. Capture freezes a named
    pose (optionally overridden for this request) and renders it through a transient SceneCapture2D.
    View names are case-insensitive and stored in lowercase. Individual atomic view files and unique image
    names support separate MCP processes.
    """
    views = _load_views(root)
    if action == "list":
        return {"ok": True, "views": {n: dict(v, captures=_captures(root, n)) for n, v in views.items()},
                "views_path": _views_dir(root), "legacy_views_path": _views_path(root)}
    if not _LABEL_RE.fullmatch(name or ""):
        return {"ok": False, "error": "name must match ^[A-Za-z0-9_-]{1,40}$."}
    name = name.casefold()
    if (location is None) != (rotation is None):
        return {"ok": False, "error": "Provide location and rotation together."}
    if action == "save":
        if location is None:
            cam = dispatch(EDITOR_MODULE, "camera_get")
            if not cam.get("ok"):
                return cam
            location, rotation = cam["location"], cam["rotation"]
        try:
            pose = _camera_pose(location, rotation, 90.0 if fov is None else fov)
        except ValueError as e:
            return {"ok": False, "error": str(e)}
        pose["saved"] = now().isoformat(timespec="microseconds")
        path = _view_path(root, name)
        _write_json(path, pose)
        return {"ok": True, "name": name, "view": pose, "views_path": path,
                "editor_viewport_changed": False}
    if action == "compare":
        return compare(name, labels, root)
    if action not in ("restore", "capture"):
        return {"ok": False, "error": "action must be save, restore, list, capture or compare."}
    if name not in views and location is None:
        return {"ok": False, "error": f"Unknown view '{name}'. Save it first (action='save')."}
    v = dict(views.get(name, {}))
    if location is not None:
        v.update(location=location, rotation=rotation)
    try:
        v.update(_camera_pose(v.get("location"), v.get("rotation"), v.get("fov", 90.0) if fov is None else fov))
    except ValueError as e:
        return {"ok": False, "error": str(e)}
    if action == "restore":
        return {"ok": True, "name": name, "view": v, "camera_backend": "scene_capture_2d",
                "editor_viewport_changed": False,
                "note": "This pose belongs to the named capture camera; the editor viewport is unchanged."}
    label = label or "capture"
    if not _LABEL_RE.fullmatch(label):
        return {"ok": False, "error": "label must match ^[A-Za-z0-9_-]{1,40}$."}
    if any(isinstance(x, bool) or not isinstance(x, int) or not 16 <= x <= 8192
           for x in (resolution_x, resolution_y)) or resolution_x * resolution_y > 16777216:
        return {"ok": False, "error": "Resolution must be integer dimensions in 16..8192, at most 16777216 pixels."}
    if any(isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x)
           for x in (timeout_s, settle_s)) or timeout_s <= 0 or settle_s < 0 or settle_s >= timeout_s:
        return {"ok": False, "error": "Need finite timeout_s > 0 and 0 <= settle_s < timeout_s."}
    deadline = clock() + float(timeout_s)
    ready: Dict[str, Any] = {}
    while True:
        ready = dispatch(EDITOR_MODULE, "view_ready")
        if ready.get("ok") and ready.get("subsystem_idle") is not False and not ready.get("pie"):
            break
        if clock() >= deadline:
            return {"ok": False, "error": "Timed out waiting for an idle Errant subsystem with PIE stopped.",
                    "view_ready": ready}
        sleep(_POLL_S)
    shaders = wait_shaders() if wait_shaders is not None else None
    shader_state = shaders.get("result", shaders) if isinstance(shaders, dict) else None
    if shaders is not None and (shaders.get("status") == "error" or shaders.get("success") is False
                                or shader_state.get("complete") is False or shader_state.get("success") is False):
        return {"ok": False, "error": "Shaders did not finish before capture.", "shaders": shaders}
    sleep(float(settle_s))
    d = _captures_dir(root, name)
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, f"{_capture_id(now)}_{label}.png")
    cap = dispatch(EDITOR_MODULE, "capture_view", path=path, location=v["location"], rotation=v["rotation"],
                   fov=v["fov"], resolution_x=resolution_x, resolution_y=resolution_y)
    if not cap.get("success", cap.get("ok", False)):
        return {"ok": False, "error": cap.get("error") or "Independent capture failed.", "capture": cap,
                "editor_viewport_changed": False}
    prev = None
    while True:
        size = complete_png(path)
        if size is not None and size == prev:
            break
        prev = size
        if clock() >= deadline:
            return {"ok": False, "error": "Timed out waiting for the capture PNG to complete.", "path": path}
        sleep(_POLL_S)
    out = {"ok": True, "name": name, "label": label, "path": path, "size_bytes": prev, "view": v,
           "camera_backend": cap.get("camera_backend", "scene_capture_2d"), "capture": cap,
           "editor_viewport_changed": False,
           "edit_mode_active": cap.get("edit_mode_active", ready.get("edit_mode_active")), "shaders": shaders}
    if out["edit_mode_active"]:
        out["note"] = ("Errant edit mode was ACTIVE: the render is current, but heights / collision are not "
                       "committed until the mode exits.")
    return out


# ---------------------------------------------------------------------------
# Scoped save (Perforce half)
# ---------------------------------------------------------------------------

def _norm(f: str) -> str:
    return os.path.normcase(os.path.normpath(f))


def valid_changelist(cl: Any) -> bool:
    return isinstance(cl, int) and not isinstance(cl, bool) and cl > 0


def plan_save_actions(rows: List[Dict[str, Any]], fstat: Dict[str, Dict[str, str]], checkout: bool,
                      changelist: Any) -> List[Dict[str, Any]]:
    """Pure: per owned dirty file, what Perforce needs before the save.
    edit = in the depot, not opened (needs checkout=True and a numbered changelist, never default);
    ok = opened for edit/add, or not in the depot and writable or not on disk yet (a new file);
    refuse = anything else (opened for another action, read-only outside the depot, edit without permission)."""
    out = []
    for r in rows:
        f = r.get("file")
        row = {"package": r["package"], "file": f, "kind": r.get("kind")}
        if not f:
            out.append(dict(row, action="refuse", why="no disk path for this package"))
            continue
        rec = fstat.get(_norm(f))
        in_depot = bool(rec and rec.get("depotFile") and "delete" not in (rec.get("headAction") or ""))
        opened = (rec or {}).get("action")
        if opened:
            if opened in ("edit", "add"):
                out.append(dict(row, action="ok", why=f"opened for {opened} in {rec.get('change')}"))
            else:
                out.append(dict(row, action="refuse", why=f"opened for {opened}"))
        elif in_depot:
            if checkout and valid_changelist(changelist):
                out.append(dict(row, action="edit", why=f"p4 edit -c {changelist}"))
            else:
                out.append(dict(row, action="refuse",
                                why="in the depot and not opened: pass checkout=True and a numbered changelist"))
        elif r.get("exists_on_disk") and not r.get("writable"):
            out.append(dict(row, action="refuse", why="read-only and not in the depot"))
        else:
            out.append(dict(row, action="ok", why="new file (not in the depot)", new=True))
        if r.get("kind") == "feature_deleted" and out[-1]["action"] in ("edit", "ok"):
            out[-1]["note"] = ("deleted bead: the save is expected to remove the file (unverified); a depot file then "
                               "needs p4 delete / reconcile by hand")
    return out


def save_scoped(*, dispatch: Callable, p4: Callable, parse_fstat: Callable, stamps=None, feature=None,
                include_proxies=None, extra_packages=None, checkout: bool = False, changelist: Any = None,
                dry_run: bool = True, expect_world: str) -> Dict[str, Any]:
    """Save exact terrain packages plus explicitly listed external PRKSettlementMarker packages."""
    scope_kw = dict(stamps=stamps, feature=feature, include_proxies=include_proxies, extra_packages=extra_packages,
                    expect_world=expect_world)
    if changelist is not None and not valid_changelist(changelist):
        return {"ok": False, "refused": True, "error": "changelist must be a numbered pending CL (never default)."}
    scope = dispatch(EDITOR_MODULE, "save_scope", **scope_kw)
    if not scope.get("ok"):
        return scope
    dirty_rows = [r for r in scope["rows"] if r.get("dirty")]
    files = [r["file"] for r in dirty_rows if r.get("file")]
    fstat = parse_fstat(p4(["fstat", *files])["stdout"]) if files else {}
    actions = plan_save_actions(dirty_rows, fstat, checkout, changelist)
    refused = [a for a in actions if a["action"] == "refuse"]
    out: Dict[str, Any] = {"ok": not refused, "dry_run": dry_run, "scope": scope, "actions": actions,
                           "packages": [r["package"] for r in dirty_rows], "changelist": changelist}
    if refused:
        out["refused"] = True
        out["error"] = f"{len(refused)} file(s) cannot be saved as they stand: see actions."
        return out
    if dry_run or not dirty_rows:
        if not dirty_rows:
            out["note"] = "Nothing in the scope is dirty."
        return out
    edits = [a["file"] for a in actions if a["action"] == "edit"]
    if edits:
        out["p4_edit"] = p4(["edit", "-c", str(changelist), *edits])
        if not out["p4_edit"].get("ok"):
            out.update(ok=False, error="p4 edit failed; nothing saved.")
            return out
    res = dispatch(EDITOR_MODULE, "save_packages_scoped", packages=out["packages"],
                   **scope_kw)
    out["save"] = res
    out["ok"] = bool(res.get("ok"))
    new_files = [a["file"] for a in actions if a.get("new") and os.path.isfile(a["file"])]
    if new_files:
        post = parse_fstat(p4(["fstat", *new_files])["stdout"])
        default_adds = [f for f in new_files
                        if (post.get(_norm(f)) or {}).get("action") == "add"
                        and (post.get(_norm(f)) or {}).get("change") == "default"]
        out["new_files"] = new_files
        out["new_files_opened_in_default"] = default_adds
        if default_adds and valid_changelist(changelist):
            out["p4_reopen"] = p4(["reopen", "-c", str(changelist), *default_adds])
        out["new_files_not_opened"] = [f for f in new_files if not (post.get(_norm(f)) or {}).get("action")]
    if not res.get("ok"):
        out["error"] = res.get("error") or "save_packages_scoped failed"
    return out
