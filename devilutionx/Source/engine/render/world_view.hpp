/**
 * @file world_view.hpp
 *
 * Geometry of the layered renderer ("Independent Zoom"): the game world is drawn into its own surface
 * with a whole number of output pixels per world pixel, the UI is drawn on top of it at the logical
 * screen size.
 *
 * Coordinate spaces:
 * - O: renderer output, in physical pixels (`SDL_GetRendererOutputSize`).
 * - U: UI / logical screen, `gnScreenWidth` x `gnScreenHeight`. `MousePosition` is in U.
 * - W: world surface, `ceil(O / ws)` where `ws` is the world zoom (output pixels per world pixel).
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "engine/point.hpp"
#include "engine/size.hpp"

namespace devilution {

/** @brief Integer division rounding towards negative infinity. */
constexpr int FloorDiv(int a, int b)
{
	const int q = a / b;
	return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/** @brief Remainder of `FloorDiv`, always in `[0, b)` for `b > 0`. */
constexpr int PosMod(int a, int b)
{
	const int m = a % b;
	return m < 0 ? m + b : m;
}

/**
 * @brief Placement of the UI (logical size U) in the output O, exactly as SDL2 `UpdateLogicalSize` computes it.
 */
struct UiPlacement {
	/** @brief Output pixels per UI pixel (`s_u`). */
	float scale;
	/** @brief Top-left corner of the UI viewport in output pixels (`o_u`). */
	Point origin;
	/** @brief Size of the UI viewport in output pixels. */
	Size viewportSize;

	bool operator==(const UiPlacement &other) const = default;
};

struct WorldView {
	/** @brief Whether the layered renderer is active for this view. */
	bool layered;
	/** @brief World zoom: output pixels per world pixel. */
	int ws;
	/** @brief Size of the world surface (W) in world pixels. */
	Size size;
	/** @brief Top-left corner of the world surface in output pixels (`o_w`, <= 0: the overscan is cropped). */
	Point originPx;
	/** @brief Output pixels per UI pixel (`s_u`). */
	float uiScale;
	/** @brief Top-left corner of the UI viewport in output pixels (`o_u`). */
	Point uiOriginPx;
	/** @brief Size of the UI viewport in output pixels. */
	Size uiViewportSize;
	/** @brief Output size (O) the view was computed for. */
	Size outputSize;
	/** @brief UI size (U) the view was computed for. */
	Size uiSize;
	/** @brief Position of the hero in world pixels (centre of the area above the main panel). */
	Point anchor;

	bool operator==(const WorldView &other) const = default;
};

/**
 * @brief Zoom levels offered for an output of the given height: the factors from
 * {1, 2, 3, 4, 5, 6, 8, 10, 12, 16} whose world height `ceil(outputHeight / ws)` lies in `[minHeight, maxHeight]`.
 *
 * Never empty: if no factor qualifies, the result is `{ max(1, round(outputHeight / 540)) }`.
 */
std::vector<int> ComputeZoomLadder(int outputHeight, int minHeight, int maxHeight);

/** @brief The ladder entry nearest to `outputHeight / 540` (ties go to the smaller factor). */
int AutoWorldZoom(std::span<const int> ladder, int outputHeight);

/** @brief The ladder entry nearest to `ws` (ties go to the smaller factor). */
int SnapToZoomLadder(std::span<const int> ladder, int ws);

/**
 * @brief The next ladder entry after snapping `ws` to the ladder.
 * @param direction > 0: next larger factor (zoom in), < 0: next smaller factor (zoom out). Clamped to the ladder.
 */
int StepZoomLadder(std::span<const int> ladder, int ws, int direction);

/** @brief The effective world zoom for the `World Zoom` option value (0 = Auto; stale values are snapped). */
int ResolveWorldZoom(std::span<const int> ladder, int optionValue, int outputHeight);

/**
 * @brief Automatic UI scale for an output size: `floor(O.h / 480)`, decreased while the UI would be
 * at most 640 pixels wide, at least 1.
 */
int AutoUiScale(Size output);

/** @brief Replicates SDL2 `UpdateLogicalSize` (letterbox mode) for logical size `ui` on output `output`. */
UiPlacement ComputeUiPlacement(Size output, Size ui, bool integerScale);

/** @brief Computes the layered view for world zoom `ws`. The result has `layered == true`. */
WorldView ComputeWorldView(Size output, Size ui, UiPlacement placement, int ws, int mainPanelHeight);

/**
 * @brief The view used when the layered renderer is not active: the world is the UI surface itself.
 * The result has `layered == false`.
 */
WorldView ComputeLegacyWorldView(Size ui, int mainPanelHeight);

/** @brief Converts a UI pixel to the world pixel under its centre. */
Point UiToWorld(const WorldView &view, Point uiPoint);

/** @brief Converts a world pixel to the UI pixel under its centre. */
Point WorldToUi(const WorldView &view, Point worldPoint);

/** @brief The current view. Only meaningful for rendering and input while `IsLayeredActive()`. */
const WorldView &GetWorldView();

/**
 * @brief Whether the layered renderer is active: `Independent Zoom` and `Upscale` are on, a renderer exists,
 * SDL2 build, not headless, no demo is being played back or recorded.
 *
 * Cached by `RecalcWorldView()`.
 */
bool IsLayeredActive();

/**
 * @brief Recomputes the current view and `IsLayeredActive()` from the options and the renderer output size.
 * @return Whether `IsLayeredActive()` or the layered view changed.
 */
bool RecalcWorldView();

/** @brief Sets the `World Zoom` option (0 = Auto). */
void SetWorldZoom(int ws);

/** @brief Zooms the world in by one ladder step (layered mode only). Persists the option. */
void WorldZoomIn();

/** @brief Zooms the world out by one ladder step (layered mode only). Persists the option. */
void WorldZoomOut();

/** @brief Toggles between the current zoom and about twice the current zoom (layered mode only). */
void WorldZoomToggle();

/**
 * @brief Turns mouse wheel events into zoom steps.
 *
 * Trackpads and touch mice send many small wheel events per gesture (followed by momentum events),
 * which would otherwise run through the whole zoom ladder at once. A wheel event steps the zoom if the
 * wheel was idle for `IdleMs` before it, if it reverses the direction, or if the last step is at least
 * `RepeatMs` old. A notched mouse wheel turned at a normal pace steps once per notch.
 */
class WheelZoomLimiter {
public:
	static constexpr uint32_t IdleMs = 120;
	static constexpr uint32_t RepeatMs = 500;

	/**
	 * @brief Registers a wheel event.
	 * @param nowMs Time of the event in milliseconds (wrapping 32-bit counter).
	 * @param direction > 0: zoom in, < 0: zoom out.
	 * @return Whether the zoom should step.
	 */
	bool Accept(uint32_t nowMs, int direction);

private:
	std::optional<uint32_t> lastEventMs_;
	std::optional<uint32_t> lastStepMs_;
	int lastDirection_ = 0;
};

/** @brief Zooms the world by one ladder step for a Ctrl/Cmd + mouse wheel event, rate limited by `WheelZoomLimiter` (layered mode only). */
void WorldZoomWheel(int direction);

/** @brief Replaces the current view and `IsLayeredActive()` with `view` (unit tests only). */
void SetWorldViewForTesting(const WorldView &view);

} // namespace devilution
