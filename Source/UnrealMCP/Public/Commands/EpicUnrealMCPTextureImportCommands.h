// EpicUnrealMCPTextureImportCommands.h
// MCP command handlers for importing textures with role-aware presets.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Handles texture import commands via MCP.
 *
 * Two entry points:
 *   - import_texture_for_icon: UI-only legacy path (TC_EditorIcon, sRGB, NeverStream).
 *   - import_texture:          role-aware import. Role drives compression, sRGB,
 *                              sampler type, LOD group, and mip generation so
 *                              callers don't have to remember the per-role
 *                              import quirks (ORD silently breaks if imported
 *                              as Color; Normal silently breaks if imported as
 *                              Default with sRGB on; etc.).
 *
 * See HandleImportTexture for the full role table.
 */
class UNREALMCP_API FEpicUnrealMCPTextureImportCommands
{
public:
	FEpicUnrealMCPTextureImportCommands() = default;
	~FEpicUnrealMCPTextureImportCommands() = default;

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	/**
	 * Legacy UI-icon import path. Hardcodes TC_EditorIcon + UI group.
	 */
	TSharedPtr<FJsonObject> HandleImportTextureForIcon(const TSharedPtr<FJsonObject>& Params);

	/**
	 * Role-aware texture import.
	 *
	 * Params:
	 *   source_file (string, required) — Absolute path to source image.
	 *   destination_path (string, required) — Content path (e.g. "/Game/Textures/Landscape").
	 *   asset_name (string, required) — Asset name (no extension, no slashes).
	 *   role (string, required) — One of:
	 *       "Color"        : sRGB=true,  TC_Default,   default sampler,   World group, mips on
	 *       "Normal"       : sRGB=false, TC_Normalmap, SAMPLERTYPE_Normal, World group, mips on
	 *       "ORD"          : sRGB=false, TC_Masks,     SAMPLERTYPE_Masks,  World group, mips on
	 *       "Mask"         : sRGB=false, TC_Masks,     SAMPLERTYPE_Masks,  World group, mips on
	 *       "Displacement" : sRGB=false, TC_Grayscale, SAMPLERTYPE_Grayscale, World group, mips on
	 *       "BlueNoise"    : sRGB=false, TC_Masks,     SAMPLERTYPE_Masks,  World group, mips OFF
	 *       "UI"           : sRGB=true,  TC_EditorIcon (UserInterface2D), UI group, mips OFF, NeverStream
	 *
	 *   replace_existing (bool, default true)
	 *   delete_source_after_import (bool, default false)
	 *
	 *   Per-role overrides (any field, omit to use role default):
	 *     srgb (bool), compression (string: Default/Normalmap/Masks/Grayscale/Alpha/EditorIcon),
	 *     never_stream (bool), no_mips (bool), lod_group (string: World/UI/WorldNormalMap/...).
	 */
	TSharedPtr<FJsonObject> HandleImportTexture(const TSharedPtr<FJsonObject>& Params);
};
