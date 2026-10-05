/**
 * @file layered_present.hpp
 *
 * Layered renderer ("Independent Zoom", SDL2 only): the world is drawn into its own 8-bit surface
 * (`WorldBuffer()`, size W of the current `WorldView`), the UI into the keyed back buffer (`PalSurface`,
 * see `ui_layer.hpp`). At present time both are expanded through the palette into ARGB textures and the
 * GPU draws the UI (with alpha) over the world.
 */
#pragma once

#include <cstdint>
#include <span>

#ifdef USE_SDL3
#include <SDL3/SDL_pixels.h>
#else
#include <SDL.h>
#endif

#include "engine/render/ui_layer.hpp"
#include "engine/render/world_view.hpp"
#include "engine/size.hpp"
#include "engine/surface.hpp"

namespace devilution {

/** @brief Builds `lut[c] = 0x00RRGGBB` for every palette colour. */
void BuildPaletteLut32(const SDL_Color *palette, uint32_t lut[256]);

/**
 * @brief Expands 8-bit pixels to opaque ARGB8888: `lut[c] | 0xFF000000`.
 * @param dstPitch Destination pitch in bytes.
 */
void ExpandIndexed(const uint8_t *src, int srcPitch, uint32_t *dst, int dstPitch, int width, int height, const uint32_t lut[256]);

/**
 * @brief Expands the keyed UI layer to ARGB8888: `UiKeyTransparent` is fully transparent, `UiKeyHalf` is
 * the half plane colour at alpha 0x80, any other colour is opaque.
 * @param half Half plane, same pitch as `src`.
 * @param dstPitch Destination pitch in bytes.
 */
void ExpandUiKeyed(const uint8_t *src, const uint8_t *half, int srcPitch, uint32_t *dst, int dstPitch, int width, int height, const uint32_t lut[256]);

/** @brief The 8-bit pixels of a layered frame. */
struct LayeredFrame {
	/** @brief World layer, `view.size` pixels. */
	const uint8_t *world;
	int worldPitch;
	/** @brief Keyed UI layer, `view.uiSize` pixels. */
	const uint8_t *ui;
	/** @brief Half plane of the UI layer, same pitch as `ui`. */
	const uint8_t *half;
	int uiPitch;
};

/**
 * @brief CPU reference of the GPU composite: the world scaled by `view.ws` at `view.originPx`, the UI
 * scaled by `view.uiScale` into its viewport and blended over it, black elsewhere.
 *
 * Sampling is nearest at the output pixel centre. The output is opaque ARGB8888, row-major.
 * @param out At least `output.width * output.height` pixels.
 */
void ComposeLayersCpu(const WorldView &view, const LayeredFrame &frame, const uint32_t lut[256], Size output, std::span<uint32_t> out);

#ifdef DVL_LAYERED_RENDERER
/** @brief The world surface of the layered renderer (W of the current view). Empty when not allocated. */
Surface WorldBuffer();

/**
 * @brief Starts a layered frame: refreshes the view if the output changed, fills the UI layer with
 * `UiKeyTransparent` and enables the layered present mode.
 *
 * May turn the layered renderer off (e.g. the renderer is gone), so callers re-check `IsLayeredActive()`.
 */
void BeginLayeredFrame();

/** @brief Leaves the layered present mode. The next present uses the upstream path. */
void EndLayeredMode();

/** @brief Rebuilds the key remap table from `logical_palette`. Call after the blending table changes. */
void RebuildUiKeyRemap();

/** @brief (Re)allocates or frees the world surface and the half plane for the current view. Called by `RecalcWorldView()`. */
void UpdateLayeredBuffers();

/** @brief Frees everything owned by the layered renderer. Must run before the renderer is destroyed. */
void FreeLayeredResources();

/**
 * @brief Presents the layered frame: expands both layers into their textures and composites them.
 * @return false if the layered frame cannot be presented; the layered present mode is then ended.
 */
bool LayeredPresent();

/** @brief `ComposeLayersCpu` of the current layered frame with the current system palette. */
void ComposeLayersCpu(Size output, std::span<uint32_t> out);
#else
inline Surface WorldBuffer()
{
	return {};
}
inline void BeginLayeredFrame()
{
}
inline void EndLayeredMode()
{
}
inline void RebuildUiKeyRemap()
{
}
inline void UpdateLayeredBuffers()
{
}
inline void FreeLayeredResources()
{
}
inline bool LayeredPresent()
{
	return false;
}
#endif

} // namespace devilution
