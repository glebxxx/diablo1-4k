/**
 * @file layered_present.hpp
 *
 * Layered renderer ("Independent Zoom"): owns the 8-bit world surface, the half plane
 * of the keyed UI layer and the textures used to composite both on the GPU.
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

/** @brief The world surface (only valid while the layered renderer is active). */
Surface WorldBuffer();

/** @brief (Re)allocates the world surface and the half plane. */
void ResizeLayers(Size worldSize, Size uiSize);

/** @brief Frees all layered resources. */
void ReleaseLayers();

/** @brief Starts a layered game frame: fills the UI layer with `UiKeyTransparent`. */
void BeginLayeredFrame();

/** @brief Returns to the single-surface (upstream) present path. */
void EndLayeredMode();

/** @brief Whether the next present composites the layers. */
bool PresentLayered();

/** @brief Rebuilds the key remap table from the current logical palette. */
void RebuildUiKeyRemap();

/** @brief ARGB8888 colours of the palette (alpha 0). */
void BuildPaletteLut(const SDL_Color *palette, uint32_t lut[256]);

/** @brief Expands an 8-bit image to opaque ARGB8888. Pitches are in bytes. */
void ExpandIndexed(const uint8_t *src, int srcPitch, uint32_t *dst, int dstPitch, int width, int height, const uint32_t lut[256]);

/**
 * @brief Expands the keyed UI layer to ARGB8888 with alpha. Pitches are in bytes,
 * `half` has the same layout as `src`.
 */
void ExpandUiKeyed(const uint8_t *src, const uint8_t *half, int srcPitch, uint32_t *dst, int dstPitch, int width, int height, const uint32_t lut[256]);

/**
 * @brief Composites the layers into the renderer. Returns false if the layered
 * resources are unavailable (the caller then uses the single-surface path).
 */
bool LayeredPresent();

/**
 * @brief CPU reference of the GPU composite: world scaled by `ws`, UI placed by `uiScale` and
 * `uiOriginPx` (nearest sampling) and alpha-blended over it.
 * @param out view.outputSize.width * view.outputSize.height ARGB8888 pixels
 */
void ComposeLayersCpu(const WorldView &view, const uint8_t *world, int worldPitch, const uint8_t *ui, const uint8_t *half, int uiPitch,
    const uint32_t lut[256], std::span<uint32_t> out);

/** @brief `ComposeLayersCpu` for the current frame (for screenshots and frame dumps). */
void ComposeLayersCpu(Size output, std::span<uint32_t> out);

enum class LayeredPerfStage : uint8_t {
	WorldDraw,
	UiDraw,
	Expand,
	Present,
};

/** @brief Whether `DEVILUTIONX_PERF_LOG=1` is set. */
bool IsLayeredPerfLogEnabled();

/** @brief Current time in milliseconds for the perf log. */
double LayeredPerfNow();

void LayeredPerfAdd(LayeredPerfStage stage, double milliseconds);

/** @brief Time accumulated for `stage` in the current frame. */
double LayeredPerfGet(LayeredPerfStage stage);

/** @brief Ends a frame of the perf log; logs averages and p95 every 300 frames. */
void LayeredPerfFrameDone();

} // namespace devilution
