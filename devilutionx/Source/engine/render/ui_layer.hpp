/**
 * @file ui_layer.hpp
 *
 * State of the keyed UI layer of the layered renderer ("Independent Zoom").
 *
 * While a layered frame is presented, the 8-bit back buffer (`PalSurface`) holds only the UI. Pixels
 * the UI does not cover hold `UiKeyTransparent`; pixels covered by a half-transparent UI element hold
 * `UiKeyHalf`, and the colour blended over the world is kept in a separate "half plane" with the same
 * pitch. The world is drawn into its own surface and composited below the UI at present time.
 *
 * Blending primitives (`DrawHalfTransparentRectTo`, `SetHalfTransparentPixel`, `ClxDrawBlended`, ...)
 * check `IsKeyedUiLayer(out)` once per call and then apply the keyed rule of `BlendKeyedPixel`.
 *
 * This module has no dependency on the renderer so that the render object libraries can use it.
 */
#pragma once

#include <array>
#include <cstdint>

#include "engine/surface.hpp"
#include "utils/attributes.h"
#include "utils/palette_blending.hpp"

// The layered renderer needs SDL2 >= 2.0.12 (`SDL_RenderCopyF`, `SDL_SetTextureScaleMode`).
// SDL1 and SDL3 builds keep the upstream renderer.
#if !defined(USE_SDL1) && !defined(USE_SDL3)
#if SDL_VERSION_ATLEAST(2, 0, 12)
#define DVL_LAYERED_RENDERER
#endif
#endif

namespace devilution {

/** @brief UI layer pixel that shows the world. Upstream UI art never uses colours 1..127. */
inline constexpr uint8_t UiKeyTransparent = 1;

/** @brief UI layer pixel that shows the half plane colour blended 50 % over the world. */
inline constexpr uint8_t UiKeyHalf = 2;

struct UiLayerState {
	/** @brief Whether the frame being drawn / presented is layered (sticky until `EndLayeredMode()`). */
	bool presentLayered = false;
	/** @brief Pixels of the UI layer surface (`PalSurface->pixels`). */
	const void *uiPixels = nullptr;
	/** @brief Half plane: the colour of each `UiKeyHalf` pixel, same pitch as the UI layer. */
	uint8_t *halfPlane = nullptr;
	/** @brief Maps blend results that would be read as keys (1, 2) to the nearest real colour. Identity otherwise. */
	std::array<uint8_t, 256> keyRemap;
};

extern DVL_API_FOR_TEST UiLayerState UiLayer;

/** @brief What the keyed blitters need to know about the UI layer. */
struct UiKeyedTarget {
	const uint8_t *uiPixels;
	uint8_t *halfPlane;
	const uint8_t *keyRemap;

	[[nodiscard]] uint8_t *HalfAt(const uint8_t *uiPixel) const
	{
		return halfPlane + (uiPixel - uiPixels);
	}
};

#ifdef DVL_LAYERED_RENDERER
/** @brief Whether the current frame is layered: the back buffer holds the keyed UI layer. */
inline bool PresentLayered()
{
	return UiLayer.presentLayered;
}

/** @brief Whether `out` draws onto the UI layer surface. */
inline bool IsUiLayer(const Surface &out)
{
	return out.surface != nullptr && UiLayer.uiPixels != nullptr && out.surface->pixels == UiLayer.uiPixels;
}
#else
constexpr bool PresentLayered()
{
	return false;
}

inline bool IsUiLayer(const Surface & /*out*/)
{
	return false;
}
#endif

/** @brief Whether blending onto `out` must use the keyed rule. Evaluate once per primitive call. */
inline bool IsKeyedUiLayer(const Surface &out)
{
	return PresentLayered() && IsUiLayer(out);
}

inline UiKeyedTarget GetUiKeyedTarget()
{
	return { static_cast<const uint8_t *>(UiLayer.uiPixels), UiLayer.halfPlane, UiLayer.keyRemap.data() };
}

/**
 * @brief Blends colour `src` 50 % onto the UI layer pixel `dst`.
 *
 * - `UiKeyTransparent`: becomes `UiKeyHalf`, the half plane takes `src`.
 * - `UiKeyHalf`: the half plane colour is blended with `src`.
 * - any other colour: blended as usual. Results equal to a key are remapped to a real colour.
 */
DVL_ALWAYS_INLINE void BlendKeyedPixel(uint8_t *dst, uint8_t src, const UiKeyedTarget &target)
{
	const uint8_t dstColor = *dst;
	if (dstColor == UiKeyTransparent) {
		*dst = UiKeyHalf;
		*target.HalfAt(dst) = src;
	} else if (dstColor == UiKeyHalf) {
		uint8_t &half = *target.HalfAt(dst);
		half = target.keyRemap[paletteTransparencyLookup[src][half]];
	} else {
		*dst = target.keyRemap[paletteTransparencyLookup[src][dstColor]];
	}
}

/** @brief Sets the UI layer surface pixels and the half plane (both may be null). */
void SetUiLayerTarget(const void *uiPixels, uint8_t *halfPlane);

/** @brief Starts (`true`) or ends (`false`) the layered present mode. */
void SetPresentLayered(bool presentLayered);

/**
 * @brief Rebuilds `UiLayer.keyRemap` for `palette`: the keys map to the nearest colour in 32..255,
 * every other colour maps to itself.
 */
void UpdateUiKeyRemap(const SDL_Color *palette);

/**
 * @brief Applies `trn` to every pixel of `out`.
 * @param skipCyclingColors Leave colours below 32 alone (Hell: they are colour-cycled).
 */
void ApplyTrnToSurface(const Surface &out, const uint8_t *trn, bool skipCyclingColors);

/**
 * @brief Applies `trn` to the UI layer `out`: `UiKeyTransparent` pixels are left alone, the half plane
 * colour of `UiKeyHalf` pixels is mapped, other pixels are mapped and remapped away from the keys.
 * @param skipCyclingColors Leave colours below 32 alone (Hell: they are colour-cycled).
 */
void ApplyTrnToKeyedUiLayer(const Surface &out, const uint8_t *trn, bool skipCyclingColors);

} // namespace devilution
