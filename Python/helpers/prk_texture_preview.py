"""Texture thumbnail preview read straight from the .uasset (MCP server process only).

Never imports ``unreal`` and never talks to the editor: a live-editor texture export
asserts on 16-bit formats (editor-fatal 2026-10-07), so this reads the editor thumbnail
the package already carries (a JPEG, or a PNG on older packages) and decodes it with Pillow.
"""

from __future__ import annotations

import glob
import io
import os
import re
import tempfile
from typing import Any, Dict, Optional

_JPEG_START = b"\xff\xd8\xff"
_JPEG_END = b"\xff\xd9"
_PNG_START = b"\x89PNG\r\n\x1a\n"
_PNG_END = b"IEND"


def project_root(start: Optional[str] = None) -> Optional[str]:
    """Nearest ancestor directory holding a .uproject."""
    d = os.path.abspath(start or os.path.dirname(__file__))
    while True:
        if glob.glob(os.path.join(d, "*.uproject")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def package_name(asset_path: str) -> str:
    """'/Game/A/T_X.T_X', "Texture2D'/Game/A/T_X.T_X'" or '/Game/A/T_X' -> '/Game/A/T_X'."""
    p = asset_path.strip().strip('"')
    m = re.match(r"^[^']*'(.*)'$", p)
    if m:
        p = m.group(1)
    head, _, tail = p.rpartition("/")
    return f"{head}/{tail.split('.')[0]}" if head else p.split(".")[0]


def resolve_package_file(asset_path: str, root: str) -> Optional[str]:
    """Disk file of a /Game/... or /<PluginMount>/... package (.uasset or .umap)."""
    pkg = package_name(asset_path)
    parts = [s for s in pkg.split("/") if s]
    if len(parts) < 2:
        return None
    mount, rest = parts[0], parts[1:]
    content_dirs = []
    if mount == "Game":
        content_dirs.append(os.path.join(root, "Content"))
    else:
        for uplugin in glob.glob(os.path.join(root, "Plugins", "**", f"{mount}.uplugin"), recursive=True):
            content_dirs.append(os.path.join(os.path.dirname(uplugin), "Content"))
    for cdir in content_dirs:
        base = os.path.join(cdir, *rest)
        for ext in (".uasset", ".umap"):
            if os.path.isfile(base + ext):
                return base + ext
    return None


def _images(data: bytes):
    """Yield (format, bytes) for every embedded JPEG, then every embedded PNG, in file order."""
    pos = data.find(_JPEG_START)
    while pos >= 0:
        end = data.find(_JPEG_END, pos + 3)
        if end < 0:
            break
        yield "jpeg", data[pos:end + 2]
        pos = data.find(_JPEG_START, end + 2)
    pos = data.find(_PNG_START)
    while pos >= 0:
        end = data.find(_PNG_END, pos)
        if end < 0:
            break
        yield "png", data[pos:end + 8]  # IEND + CRC
        pos = data.find(_PNG_START, end + 8)


def extract_thumbnail(data: bytes, max_size: int = 512):
    """First decodable embedded image no larger than max_size on either axis (the editor
    thumbnail; a texture's PNG source bulk data is larger and is skipped)."""
    from PIL import Image  # Pillow is a declared dependency of the MCP server

    for fmt, blob in _images(data):
        try:
            img = Image.open(io.BytesIO(blob))
            img.load()
        except Exception:  # noqa: BLE001 - not an image at this offset, keep scanning
            continue
        if img.width <= max_size and img.height <= max_size:
            return fmt, img
    return None, None


def texture_preview(asset_path: str, out_dir: Optional[str] = None, root: Optional[str] = None) -> Dict[str, Any]:
    root = root or project_root()
    if root is None:
        return {"success": False, "message": "No .uproject found above the MCP server directory."}
    path = resolve_package_file(asset_path, root)
    if path is None:
        return {"success": False, "message": f"No .uasset on disk for {asset_path} under {root}."}
    out_dir = os.path.abspath(out_dir or os.path.join(tempfile.gettempdir(), "texture_preview"))
    if os.path.normcase(out_dir + os.sep).startswith(os.path.normcase(os.path.abspath(root) + os.sep)):
        return {"success": False, "message": f"out_dir {out_dir} is inside the project tree; pick a temp directory."}
    with open(path, "rb") as f:
        data = f.read()
    fmt, img = extract_thumbnail(data)
    if img is None:
        return {"success": False, "file": path,
                "message": "No embedded thumbnail found (the package was saved without one)."}

    from PIL import ImageStat

    gray = img.convert("L")
    w, h = gray.size
    lo, hi = gray.getextrema()
    cw, ch = max(1, w // 10), max(1, h // 10)
    box = ((w - cw) // 2, (h - ch) // 2, (w - cw) // 2 + cw, (h - ch) // 2 + ch)
    centre = ImageStat.Stat(gray.crop(box)).mean[0]
    bbox = gray.point(lambda v: 255 if v > 20 else 0).getbbox()

    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, os.path.basename(package_name(asset_path)) + ".png")
    img.convert("RGBA" if "A" in img.getbands() else "RGB").save(out_path)
    return {
        "success": True,
        "asset": package_name(asset_path),
        "file": path,
        "thumbnail_format": fmt,
        "size": [w, h],
        "mode": img.mode,
        "min": lo,
        "max": hi,
        "mean": round(ImageStat.Stat(gray).mean[0], 2),
        "centre_mean": round(centre, 2),
        "centre_box": list(box),
        "bbox_gt20": list(bbox) if bbox else None,
        "png": out_path,
        "note": "Editor thumbnail (8-bit, display-encoded), not the source pixels: use it for shape and "
                "polarity, not for exact heights.",
    }
