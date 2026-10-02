/**
 * @file ui_layer.hpp
 *
 * Keyed UI layer of the layered renderer ("Independent Zoom").
 *
 * In layered mode, the UI is drawn into the regular 8-bit back buffer, which starts
 * every frame filled with `UiKeyTransparent`. Opaque drawing needs no change.
 * Half-transparent drawing over a transparent pixel turns it into `UiKeyHalf` and
 * stores the colour in the half plane; it is composited at 50% over the world.
 * Blend results are remapped away from the two key values.
 */
#pragma once

#include <array>
#include <cstdint>

#ifdef USE_SDL3
#include <SDL3/SDL_pixels.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"
#include "utils/attributes.h"
#include "utils/palette_blending.hpp"

namespace devilution {

/**
 * @brief Key values of the UI layer.
 *
 * Colors 1-127 are outside of the UI palette (see hwcursor.cpp), so UI art does not use them.
 */
inline constexpr uint8_t UiKeyTransparent = 1;
inline constexpr uint8_t UiKeyHalf = 2;

struct UiLayerState {
	/** @brief Pixel range of the UI layer (the back buffer). */
	const uint8_t *begin = nullptr;
	const uint8_t *end = nullptr;
	/** @brief Colours of the `UiKeyHalf` pixels, same layout as the UI layer. */
	uint8_t *halfPlane = nullptr;
	/** @brief Whether the last game frame was drawn in layered mode (sticky until `EndLayeredMode`). */
	bool presentLayered = false;
};

extern DVL_API_FOR_TEST UiLayerState UiLayer;

/** @brief Maps the key values to the nearest other palette entry, every other value to itself. */
extern DVL_API_FOR_TEST std::array<uint8_t, 256> UiKeyRemap;

/** @brief Whether `out` is (a part of) the UI layer. Evaluate once per primitive, not per pixel. */
[[nodiscard]] inline bool IsUiLayer(const Surface &out)
{
	const uint8_t *pixels = out.begin();
	return pixels >= UiLayer.begin && pixels < UiLayer.end;
}

/** @brief Whether drawing to `out` must use the keyed blending rules. */
[[nodiscard]] inline bool IsKeyedUiLayer(const Surface &out)
{
	return UiLayer.presentLayered && IsUiLayer(out);
}

[[nodiscard]] DVL_ALWAYS_INLINE uint8_t *UiHalfPlaneAt(const uint8_t *dst)
{
	return UiLayer.halfPlane + (dst - UiLayer.begin);
}

/**
 * @brief Blends `src` at 50% into a UI layer pixel.
 * @param dst UI layer pixel
 * @param half Half plane pixel of `dst`
 */
DVL_ALWAYS_INLINE void BlendUiPixelKeyed(uint8_t &dst, uint8_t &half, uint8_t src)
{
	if (dst == UiKeyTransparent) {
		dst = UiKeyHalf;
		half = src;
	} else if (dst == UiKeyHalf) {
		half = UiKeyRemap[paletteTransparencyLookup[src][half]];
	} else {
		dst = UiKeyRemap[paletteTransparencyLookup[src][dst]];
	}
}

/** @brief Applies a colour translation to a UI layer pixel (e.g. the red tint), keeping the keys. */
DVL_ALWAYS_INLINE void TranslateUiPixelKeyed(uint8_t &dst, uint8_t &half, const uint8_t *tbl)
{
	if (dst == UiKeyTransparent) return;
	if (dst == UiKeyHalf) {
		half = tbl[half];
	} else {
		dst = tbl[dst];
	}
}

/**
 * @brief Rebuilds `UiKeyRemap` for the given palette: each key value maps to
 * the nearest palette entry in 32..255.
 */
void RebuildUiKeyRemap(const SDL_Color *palette);

} // namespace devilution
