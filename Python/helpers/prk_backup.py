"""World Partition external-actor backup (MCP server process only, no editor call).

ErrantWorld's external actors are not in Perforce, so a scripted stamp spawn
(errant_stamp_spawn) requires a fresh copy of them: Saved/Backup_ErrantWorldActors_<stamp>/
with BACKUP_OK.json written only when the copy's file count and bytes match the source.
"""

from __future__ import annotations

import datetime
import json
import os
import shutil
import subprocess
import time
from typing import Any, Dict, Tuple

from helpers.prk_texture_preview import project_root

# world name -> external-actor folder (relative to the project) and backup folder prefix.
WORLDS = {
    "ErrantWorld": (os.path.join("Content", "__ExternalActors__", "PRK", "World", "ErrantWorld", "ErrantWorld"),
                    "Backup_ErrantWorldActors_"),
}
_LOG_NAME = "robocopy.log"
_OK_NAME = "BACKUP_OK.json"


def tree_size(path: str, skip: Tuple[str, ...] = ()) -> Tuple[int, int]:
    """(file count, bytes) under path, skipping top-level file names in `skip`."""
    files = size = 0
    for d, _dirs, names in os.walk(path):
        for n in names:
            if d == path and n in skip:
                continue
            files += 1
            size += os.path.getsize(os.path.join(d, n))
    return files, size


def backup_external_actors(world: str = "ErrantWorld", root: str = None) -> Dict[str, Any]:
    if world not in WORLDS:
        return {"success": False, "message": f"unknown world '{world}'; known: {sorted(WORLDS)}"}
    root = root or project_root()
    if root is None:
        return {"success": False, "message": "No .uproject found above the MCP server directory."}
    rel, prefix = WORLDS[world]
    src = os.path.join(root, rel)
    if not os.path.isdir(src):
        return {"success": False, "message": f"source folder missing: {src}"}
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M")
    dst = os.path.join(root, "Saved", prefix + stamp)
    if os.path.exists(dst):
        return {"success": False, "message": f"{dst} already exists; wait a minute and retry."}

    t0 = time.time()
    src_files, src_bytes = tree_size(src)
    free = shutil.disk_usage(root).free
    if free < 2 * src_bytes:
        return {"success": False, "refused": True,
                "message": f"free disk {free / 1e9:.1f} GB < 2 x source {src_bytes / 1e9:.1f} GB."}

    os.makedirs(dst)
    log = os.path.join(dst, _LOG_NAME)
    # /E copies the tree into the new, empty folder; the source is only read.
    proc = subprocess.run(
        ["robocopy", src, dst, "/E", "/COPY:DAT", "/DCOPY:DAT", "/R:1", "/W:1", "/NP", "/NFL", "/NDL",
         f"/LOG:{log}"],
        capture_output=True, text=True)
    elapsed = round(time.time() - t0, 1)
    dst_files, dst_bytes = tree_size(dst, skip=(_LOG_NAME,))
    out = {
        "world": world, "source": src, "backup": dst, "log": log, "robocopy_exit": proc.returncode,
        "source_files": src_files, "source_bytes": src_bytes, "files": dst_files, "bytes": dst_bytes,
        "elapsed_s": elapsed,
    }
    if proc.returncode >= 8 or dst_files != src_files or dst_bytes != src_bytes:
        out.update({"success": False,
                    "message": "Copy incomplete (robocopy exit >= 8 or counts differ); no BACKUP_OK.json written. "
                               "See the log."})
        return out
    now = time.time()
    record = {"world": world, "time_utc": datetime.datetime.fromtimestamp(now, datetime.timezone.utc).isoformat(
        timespec="seconds"), "unix": now, "files": dst_files, "bytes": dst_bytes, "source": src}
    with open(os.path.join(dst, _OK_NAME), "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2)
    out.update({"success": True, "ok_file": os.path.join(dst, _OK_NAME), "size_gb": round(dst_bytes / 1e9, 2)})
    return out
