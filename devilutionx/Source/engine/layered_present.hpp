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

/** @brief Points of a layered frame timed by the performance log (environment variable `DEVILUTIONX_PERF_LOG=1`). */
enum class LayeredPerfEvent : uint8_t {
	FrameStart,
	WorldDrawStart,
	WorldDrawEnd,
	PresentStart,
	FrameEnd,
};

/** @brief Average and 95th percentile of a series of frame times, in milliseconds. */
struct PerfSummary {
	double average;
	double p95;
};

/** @brief Summarizes frame times. The p95 is the smallest sample that at least 95 % of the samples do not exceed. */
PerfSummary SummarizeFrameTimes(std::span<const float> milliseconds);

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

/**
 * @brief (Re)allocates the world surface and the half plane for the current view. Called by `RecalcWorldView()`.
 *
 * When the layered renderer is not active, frees them together with the textures.
 */
void UpdateLayeredBuffers();

/** @brief Whether the textures of the layered present are allocated. */
bool LayeredTexturesAllocated();

/** @brief Frees everything owned by the layered renderer. Must run before the renderer is destroyed. */
void FreeLayeredResources();

/**
 * @brief Presents the layered frame: expands both layers into their textures and composites them.
 * @return false if the layered frame cannot be presented; the layered present mode is then ended.
 */
bool LayeredPresent();

/** @brief `ComposeLayersCpu` of the current layered frame with the current system palette. */
void ComposeLayersCpu(Size output, std::span<uint32_t> out);

/**
 * @brief Records a timing point for the performance log. A no-op unless `DEVILUTIONX_PERF_LOG=1` and the frame is layered.
 *
 * Every 300 layered frames the average and the p95 of the world draw, UI draw, expand, present and whole
 * frame times are logged. The present time includes the wait for vsync and the frame limiter.
 */
void LayeredPerfMark(LayeredPerfEvent event);
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
inline void LayeredPerfMark(LayeredPerfEvent /*event*/)
{
}
#endif

} // namespace devilution
