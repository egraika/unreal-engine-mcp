"""
Helpers for constructing FSlateBrush from Python.

Why this exists
---------------
In UE 5.5+, FSlateBrush.ImageSize is `FDeprecateSlateVector2D` — a USTRUCT that
inherits FVector2f but is registered under its own FName ("DeprecateSlateVector2D").
The Python type bridge will not auto-nativize either of the obvious inputs:

    brush.set_editor_property("image_size", unreal.Vector2D(x, y))
        -> "Cannot nativize 'Vector2D' as 'DeprecateSlateVector2D'"

    brush.set_editor_property("image_size", (x, y))
        -> "Struct has 0 initialization parameters, but the given sequence had 2"

Earlier conversations sometimes succeeded with `unreal.Vector2D(...)` — that was
incidental (SerializeFromMismatchedTag fallbacks fired during script-vm marshal),
not officially supported. Don't rely on it.

The reliable workaround is `StructBase.import_text(...)`. UScriptStruct's text
import only overwrites named fields (others retain their current value), so it
composes cleanly with `set_editor_property` for the rest of the brush:

    b = unreal.SlateBrush()
    b.set_editor_property("resource_object", tex)            # works
    b.set_editor_property("draw_as", unreal.SlateBrushDrawType.IMAGE)
    b.set_editor_property("tiling", unreal.SlateBrushTileType.BOTH)
    set_brush_image_size(b, 128, 128)                        # this module

Does NOT depend on the MCP server — runs in any in-editor Python context (Python
tab, EditorUtilityWidget, run_python tool).
"""
from __future__ import annotations

import unreal


def set_brush_image_size(brush: "unreal.SlateBrush", x: float, y: float) -> bool:
    """
    Write FSlateBrush.ImageSize, bypassing the FDeprecateSlateVector2D Python
    bridge gap.

    Returns True if UE's ImportText consumed the input (the brush's other
    fields are left untouched — only ImageSize is overwritten).
    """
    text = "(ImageSize=(X={x:.6f},Y={y:.6f}))".format(x=float(x), y=float(y))
    return bool(brush.import_text(text))


def make_tiled_brush(
    texture: "unreal.Texture2D",
    tile_cell_px: tuple,
    tiling: "unreal.SlateBrushTileType" = None,
    tint: tuple = (1.0, 1.0, 1.0, 1.0),
) -> "unreal.SlateBrush":
    """
    Construct an FSlateBrush configured for tile-cell rendering.

    `tile_cell_px` is the per-cell repeat size in screen pixels — INDEPENDENT
    of the source texture's resolution. Setting ImageSize to the source
    texture's native dimensions makes one repeat fill the whole panel; for
    backgrounds you almost always want a smaller cell.
    """
    if tiling is None:
        tiling = unreal.SlateBrushTileType.BOTH

    b = unreal.SlateBrush()
    b.set_editor_property("resource_object", texture)
    b.set_editor_property("draw_as", unreal.SlateBrushDrawType.IMAGE)
    b.set_editor_property("tiling", tiling)

    color = unreal.SlateColor()
    color.set_editor_property("specified_color", unreal.LinearColor(*tint))
    color.set_editor_property("color_use_rule", unreal.SlateColorStylingMode.USE_COLOR_SPECIFIED)
    b.set_editor_property("tint_color", color)

    set_brush_image_size(b, tile_cell_px[0], tile_cell_px[1])
    return b


def make_image_brush(
    texture: "unreal.Texture2D",
    image_size_px: tuple = None,
    tint: tuple = (1.0, 1.0, 1.0, 1.0),
    margin: tuple = None,
) -> "unreal.SlateBrush":
    """
    Construct a non-tiled FSlateBrush (DrawAs=Image, Tiling=NoTile).

    `image_size_px` is optional; if omitted, the brush uses the source
    texture's native size. `margin` is the 9-slice margin (left, top, right,
    bottom) — only meaningful when `draw_as` is later changed to Box/Border.
    """
    b = unreal.SlateBrush()
    b.set_editor_property("resource_object", texture)
    b.set_editor_property("draw_as", unreal.SlateBrushDrawType.IMAGE)
    b.set_editor_property("tiling", unreal.SlateBrushTileType.NO_TILE)

    color = unreal.SlateColor()
    color.set_editor_property("specified_color", unreal.LinearColor(*tint))
    color.set_editor_property("color_use_rule", unreal.SlateColorStylingMode.USE_COLOR_SPECIFIED)
    b.set_editor_property("tint_color", color)

    if image_size_px is not None:
        set_brush_image_size(b, image_size_px[0], image_size_px[1])

    if margin is not None:
        text = ("(Margin=(Left={l:.6f},Top={t:.6f},Right={r:.6f},Bottom={bo:.6f}))"
                .format(l=float(margin[0]), t=float(margin[1]),
                        r=float(margin[2]), bo=float(margin[3])))
        b.import_text(text)

    return b
