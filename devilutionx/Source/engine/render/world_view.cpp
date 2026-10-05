#include "engine/render/world_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

#include "control/control.hpp"
#include "engine/backbuffer_state.hpp"
#include "engine/demomode.h"
#include "engine/layered_present.hpp"
#include "engine/render/scrollrt.h"
#include "headless_mode.hpp"
#include "options.h"
#include "utils/display.h"
#include "utils/ui_fwd.h"

// The world zoom options exist in SDL2 builds only. The layered renderer itself also needs
// SDL2 >= 2.0.12 (`DVL_LAYERED_RENDERER`, see ui_layer.hpp). SDL1 and SDL3 builds keep the upstream renderer.
#if !defined(USE_SDL1) && !defined(USE_SDL3)
#define DVL_WORLD_ZOOM_OPTIONS
#endif

namespace devilution {

namespace {

/** @brief Zoom factors offered by the ladder, in ascending order. */
constexpr int ZoomFactors[] = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 16 };

/** @brief Height of the world in world pixels that the automatic zoom aims for. */
constexpr int AutoWorldHeight = 540;

WorldView CurrentView {};
bool LayeredActive = false;

#ifdef DVL_WORLD_ZOOM_OPTIONS
/** @brief `World Zoom` option value restored by the next `WorldZoomToggle()`. */
std::optional<int> ZoomReturn;

WheelZoomLimiter WheelLimiter;
#endif

/** @brief Converts the centre of pixel `p` of a grid with pixel size `fromScale` and origin `fromOrigin` to the target grid. */
int ConvertPixel(int p, double fromScale, int fromOrigin, double toScale, int toOrigin)
{
	return static_cast<int>(std::floor(((p + 0.5) * fromScale + fromOrigin - toOrigin) / toScale));
}

#ifdef DVL_WORLD_ZOOM_OPTIONS
std::vector<int> CurrentZoomLadder(int outputHeight)
{
	const GraphicsOptions &graphics = GetOptions().Graphics;
	return ComputeZoomLadder(outputHeight, *graphics.worldZoomMinHeight, *graphics.worldZoomMaxHeight);
}
#endif

std::optional<WorldView> ComputeLayeredView()
{
#ifdef DVL_LAYERED_RENDERER
	const GraphicsOptions &graphics = GetOptions().Graphics;
	// Demos record the screen size and mouse positions of the single-layer renderer. The option itself is left
	// alone (not forced off by demo::OverrideOptions()): a forced value could be written back to diablo.ini once
	// the viewer takes over a demo with Escape and the options are saved.
	if (!*graphics.independentZoom || !*graphics.upscale || renderer == nullptr || HeadlessMode
	    || demo::IsRunning() || demo::IsRecording()) {
		return std::nullopt;
	}

	const Size ui { gnScreenWidth, gnScreenHeight };
	Size output;
	if (SDL_GetRendererOutputSize(renderer, &output.width, &output.height) != 0)
		return std::nullopt;
	if (ui.width <= 0 || ui.height <= 0 || output.width <= 0 || output.height <= 0)
		return std::nullopt;

	const bool integerScale = SDL_RenderGetIntegerScale(renderer) == SDL_TRUE;
	const std::vector<int> ladder = CurrentZoomLadder(output.height);
	const int ws = ResolveWorldZoom(ladder, *graphics.worldZoom, output.height);
	return ComputeWorldView(output, ui, ComputeUiPlacement(output, ui, integerScale), ws, GetMainPanel().size.height);
#else
	return std::nullopt;
#endif
}

#ifdef DVL_WORLD_ZOOM_OPTIONS
void StepWorldZoom(int direction)
{
	if (!IsLayeredActive())
		return;
	const int outputHeight = CurrentView.outputSize.height;
	const std::vector<int> ladder = CurrentZoomLadder(outputHeight);
	const int ws = ResolveWorldZoom(ladder, *GetOptions().Graphics.worldZoom, outputHeight);
	SetWorldZoom(StepZoomLadder(ladder, ws, direction));
	SaveOptions();
}

void OptionWorldViewChanged()
{
	if (RecalcWorldView()) {
		CalcViewportGeometry();
		RedrawEverything();
	}
}

void OptionWorldZoomChanged()
{
	ZoomReturn = std::nullopt;
	OptionWorldViewChanged();
}

const auto OptionChangeHandlerIndependentZoom = (GetOptions().Graphics.independentZoom.SetValueChangedCallback(OptionWorldViewChanged), true);
const auto OptionChangeHandlerWorldZoom = (GetOptions().Graphics.worldZoom.SetValueChangedCallback(OptionWorldZoomChanged), true);
const auto OptionChangeHandlerWorldZoomMinHeight = (GetOptions().Graphics.worldZoomMinHeight.SetValueChangedCallback(OptionWorldViewChanged), true);
const auto OptionChangeHandlerWorldZoomMaxHeight = (GetOptions().Graphics.worldZoomMaxHeight.SetValueChangedCallback(OptionWorldViewChanged), true);
#endif

} // namespace

std::vector<int> ComputeZoomLadder(int outputHeight, int minHeight, int maxHeight)
{
	std::vector<int> ladder;
	if (outputHeight > 0) {
		for (const int factor : ZoomFactors) {
			const int worldHeight = (outputHeight + factor - 1) / factor;
			if (worldHeight >= minHeight && worldHeight <= maxHeight)
				ladder.push_back(factor);
		}
	}
	if (ladder.empty())
		ladder.push_back(std::max(1, (outputHeight + AutoWorldHeight / 2) / AutoWorldHeight));
	return ladder;
}

int AutoWorldZoom(std::span<const int> ladder, int outputHeight)
{
	// Nearest to outputHeight / AutoWorldHeight, compared in output pixels to stay exact.
	int best = ladder.front();
	for (const int factor : ladder) {
		if (std::abs(factor * AutoWorldHeight - outputHeight) < std::abs(best * AutoWorldHeight - outputHeight))
			best = factor;
	}
	return best;
}

int SnapToZoomLadder(std::span<const int> ladder, int ws)
{
	int best = ladder.front();
	for (const int factor : ladder) {
		if (std::abs(factor - ws) < std::abs(best - ws))
			best = factor;
	}
	return best;
}

int StepZoomLadder(std::span<const int> ladder, int ws, int direction)
{
	const auto current = std::find(ladder.begin(), ladder.end(), SnapToZoomLadder(ladder, ws));
	if (direction > 0 && current + 1 != ladder.end())
		return *(current + 1);
	if (direction < 0 && current != ladder.begin())
		return *(current - 1);
	return *current;
}

int ResolveWorldZoom(std::span<const int> ladder, int optionValue, int outputHeight)
{
	if (optionValue <= 0)
		return AutoWorldZoom(ladder, outputHeight);
	return SnapToZoomLadder(ladder, optionValue);
}

int AutoUiScale(Size output)
{
	int scale = std::max(1, output.height / 480);
	while (scale > 1 && output.width / scale <= 640)
		scale--;
	return scale;
}

UiPlacement ComputeUiPlacement(Size output, Size ui, bool integerScale)
{
	if (ui.width <= 0 || ui.height <= 0)
		return { 1.0F, { 0, 0 }, output };

	// Mirrors SDL2 UpdateLogicalSize (SDL_render.c), including its float arithmetic.
	const float wantAspect = static_cast<float>(ui.width) / ui.height;
	const float realAspect = static_cast<float>(output.width) / output.height;

	UiPlacement placement {};
	Size &viewport = placement.viewportSize;
	float &scale = placement.scale;
	if (integerScale) {
		if (wantAspect > realAspect) {
			scale = static_cast<float>(output.width / ui.width);
		} else {
			scale = static_cast<float>(output.height / ui.height);
		}
		if (scale < 1.0F)
			scale = 1.0F;
		viewport.width = static_cast<int>(std::floor(ui.width * scale));
		placement.origin.x = (output.width - viewport.width) / 2;
		viewport.height = static_cast<int>(std::floor(ui.height * scale));
		placement.origin.y = (output.height - viewport.height) / 2;
	} else if (std::fabs(wantAspect - realAspect) < 0.0001) {
		scale = static_cast<float>(output.width) / ui.width;
		placement.origin = { 0, 0 };
		viewport = output;
	} else if (wantAspect > realAspect) {
		// Letterbox
		scale = static_cast<float>(output.width) / ui.width;
		placement.origin.x = 0;
		viewport.width = output.width;
		viewport.height = static_cast<int>(std::floor(ui.height * scale));
		placement.origin.y = (output.height - viewport.height) / 2;
	} else {
		// Side bars
		scale = static_cast<float>(output.height) / ui.height;
		placement.origin.y = 0;
		viewport.height = output.height;
		viewport.width = static_cast<int>(std::floor(ui.width * scale));
		placement.origin.x = (output.width - viewport.width) / 2;
	}
	return placement;
}

WorldView ComputeWorldView(Size output, Size ui, UiPlacement placement, int ws, int mainPanelHeight)
{
	ws = std::max(1, ws);

	WorldView view {};
	view.layered = true;
	view.ws = ws;
	view.size = { (output.width + ws - 1) / ws, (output.height + ws - 1) / ws };
	view.originPx = { (output.width - view.size.width * ws) / 2, (output.height - view.size.height * ws) / 2 };
	view.uiScale = placement.scale;
	view.uiOriginPx = placement.origin;
	view.uiViewportSize = placement.viewportSize;
	view.outputSize = output;
	view.uiSize = ui;
	view.anchor = UiToWorld(view, { ui.width / 2, (ui.height - mainPanelHeight) / 2 });
	return view;
}

WorldView ComputeLegacyWorldView(Size ui, int mainPanelHeight)
{
	WorldView view {};
	view.layered = false;
	view.ws = 1;
	view.size = ui;
	view.originPx = { 0, 0 };
	view.uiScale = 1.0F;
	view.uiOriginPx = { 0, 0 };
	view.uiViewportSize = ui;
	view.outputSize = ui;
	view.uiSize = ui;
	view.anchor = { ui.width / 2, (ui.height - mainPanelHeight) / 2 };
	return view;
}

Point UiToWorld(const WorldView &view, Point uiPoint)
{
	return {
		ConvertPixel(uiPoint.x, view.uiScale, view.uiOriginPx.x, view.ws, view.originPx.x),
		ConvertPixel(uiPoint.y, view.uiScale, view.uiOriginPx.y, view.ws, view.originPx.y),
	};
}

Point WorldToUi(const WorldView &view, Point worldPoint)
{
	return {
		ConvertPixel(worldPoint.x, view.ws, view.originPx.x, view.uiScale, view.uiOriginPx.x),
		ConvertPixel(worldPoint.y, view.ws, view.originPx.y, view.uiScale, view.uiOriginPx.y),
	};
}

const WorldView &GetWorldView()
{
	return CurrentView;
}

bool IsLayeredActive()
{
	return LayeredActive;
}

bool RecalcWorldView()
{
	const WorldView view = ComputeLayeredView().value_or(
	    ComputeLegacyWorldView({ gnScreenWidth, gnScreenHeight }, GetMainPanel().size.height));
	const bool changed = view.layered != CurrentView.layered || (view.layered && view != CurrentView);
	CurrentView = view;
	LayeredActive = view.layered;
	UpdateLayeredBuffers();
	return changed;
}

void SetWorldZoom([[maybe_unused]] int ws)
{
#ifdef DVL_WORLD_ZOOM_OPTIONS
	GetOptions().Graphics.worldZoom.SetValue(ws);
#endif
}

void WorldZoomIn()
{
#ifdef DVL_WORLD_ZOOM_OPTIONS
	StepWorldZoom(1);
#endif
}

void WorldZoomOut()
{
#ifdef DVL_WORLD_ZOOM_OPTIONS
	StepWorldZoom(-1);
#endif
}

void WorldZoomToggle()
{
#ifdef DVL_WORLD_ZOOM_OPTIONS
	if (!IsLayeredActive())
		return;
	if (ZoomReturn.has_value()) {
		const int returnTo = *ZoomReturn;
		SetWorldZoom(returnTo); // Clears ZoomReturn
		return;
	}
	const int outputHeight = CurrentView.outputSize.height;
	const std::vector<int> ladder = CurrentZoomLadder(outputHeight);
	const int optionValue = *GetOptions().Graphics.worldZoom;
	const int ws = ResolveWorldZoom(ladder, optionValue, outputHeight);
	SetWorldZoom(SnapToZoomLadder(ladder, 2 * ws)); // Clears ZoomReturn
	ZoomReturn = optionValue;
#endif
}

bool WheelZoomLimiter::Accept(uint32_t nowMs, int direction)
{
	direction = direction > 0 ? 1 : -1;
	const bool afterIdle = !lastEventMs_.has_value() || nowMs - *lastEventMs_ >= IdleMs;
	const bool reversed = direction != lastDirection_;
	const bool repeat = !lastStepMs_.has_value() || nowMs - *lastStepMs_ >= RepeatMs;
	lastEventMs_ = nowMs;
	lastDirection_ = direction;
	if (!afterIdle && !reversed && !repeat)
		return false;
	lastStepMs_ = nowMs;
	return true;
}

void WorldZoomWheel([[maybe_unused]] int direction)
{
#ifdef DVL_WORLD_ZOOM_OPTIONS
	if (!IsLayeredActive() || direction == 0)
		return;
	if (WheelLimiter.Accept(SDL_GetTicks(), direction))
		StepWorldZoom(direction > 0 ? 1 : -1);
#endif
}

void SetWorldViewForTesting(const WorldView &view)
{
	CurrentView = view;
	LayeredActive = view.layered;
}

} // namespace devilution
