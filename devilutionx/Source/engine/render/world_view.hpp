/**
 * @file world_view.hpp
 *
 * Geometry of the layered renderer ("Independent Zoom"): the world is drawn into
 * its own 8-bit surface with a whole number of output pixels per world pixel,
 * the UI is drawn into the regular back buffer and both are composited at present time.
 *
 * Coordinate spaces:
 * - O (output): renderer output size in physical pixels;
 * - U (UI): gnScreenWidth x gnScreenHeight, placed into O the same way SDL2 places a logical size;
 * - W (world): ceil(O / ws), centred on O (the overscan is cropped).
 */
#pragma once

#include <span>
#include <vector>

#include "engine/point.hpp"
#include "engine/size.hpp"

namespace devilution {

/** @brief The world zoom levels that are considered, in output pixels per world pixel. */
inline constexpr int WorldZoomCandidates[] = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 16 };

struct UiPlacement {
	/** @brief Output pixels per UI pixel. */
	float scale;
	/** @brief Position of the UI origin in output pixels. */
	Point origin;
};

struct WorldView {
	/** @brief Whether the layered renderer is active. */
	bool layered = false;
	/** @brief Output pixels per world pixel. */
	int ws = 1;
	/** @brief Size of the world surface (W). */
	Size size;
	/** @brief Position of the world origin in output pixels (<= 0). */
	Point originPx;
	/** @brief Output pixels per UI pixel. */
	float uiScale = 1;
	/** @brief Position of the UI origin in output pixels. */
	Point uiOriginPx;
	/** @brief World position the hero is centred on. */
	Point anchor;
	/** @brief Renderer output size (O). */
	Size outputSize;
	/** @brief UI size (U). */
	Size uiSize;
};

/**
 * @brief The zoom levels whose world height lies within [minHeight, maxHeight].
 * Never empty.
 */
std::vector<int> ComputeZoomLadder(int outputHeight, int minHeight, int maxHeight);

/** @brief The ladder entry closest to a framing of about 540 world pixels in height. */
int AutoWorldZoom(std::span<const int> ladder, int outputHeight);

/** @brief The ladder entry nearest to `ws` (ties go to the smaller entry). */
int SnapToZoomLadder(std::span<const int> ladder, int ws);

/** @brief The next larger zoom level (clamped). */
int NextWorldZoomIn(std::span<const int> ladder, int ws);

/** @brief The next smaller zoom level (clamped). */
int NextWorldZoomOut(std::span<const int> ladder, int ws);

/** @brief Largest integer UI scale that keeps the UI at least 480 pixels high and wider than 640 pixels. */
int AutoUiScale(Size output);

/** @brief Replicates SDL2's placement of a logical size on the renderer output (`UpdateLogicalSize`). */
UiPlacement ComputeUiPlacement(Size output, Size ui, bool integerScale);

WorldView ComputeWorldView(Size output, Size ui, UiPlacement placement, int ws, int mainPanelHeight);

/** @brief Converts a UI position to the world pixel under its centre. */
Point UiToWorld(const WorldView &view, Point uiPosition);

/** @brief Converts a world position to the UI pixel under its centre. */
Point WorldToUi(const WorldView &view, Point worldPosition);

/** @brief The current geometry. Only meaningful when `IsLayeredActive()`. */
const WorldView &GetWorldView();

/** @brief Recomputes the geometry from the renderer and options, and (re)allocates the layers. */
void RecalcWorldView();

/** @brief Whether the layered renderer is used for game frames. */
bool IsLayeredActive();

/** @brief Replaces the runtime geometry without a renderer (tests only). */
void SetWorldViewForTesting(const WorldView &view);

void SetWorldZoom(int ws);
void WorldZoomIn();
void WorldZoomOut();
/** @brief Toggles between the current zoom level and twice that, or back. */
void WorldZoomToggle();

} // namespace devilution
