#include "engine/render/world_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

#ifdef USE_SDL3
#include <SDL3/SDL_render.h>
#else
#include <SDL.h>
#endif

#include "control/control.hpp"
#include "engine/backbuffer_state.hpp"
#include "engine/demomode.h"
#include "engine/dx.h"
#include "engine/layered_present.hpp"
#include "engine/render/scrollrt.h"
#include "headless_mode.hpp"
#include "options.h"
#include "utils/display.h"

namespace devilution {

std::vector<int> ComputeZoomLadder(int outputHeight, int minHeight, int maxHeight)
{
	std::vector<int> ladder;
	for (const int ws : WorldZoomCandidates) {
		const int worldHeight = (outputHeight + ws - 1) / ws;
		if (worldHeight >= minHeight && worldHeight <= maxHeight)
			ladder.push_back(ws);
	}
	if (ladder.empty())
		ladder.push_back(std::max(1, static_cast<int>(std::lround(outputHeight / 540.0))));
	return ladder;
}

namespace {

/** @brief The ladder entry nearest to `target` (ties go to the smaller entry). */
int NearestLadderEntry(std::span<const int> ladder, double target)
{
	int best = ladder.front();
	double bestDistance = std::abs(best - target);
	for (const int ws : ladder) {
		const double distance = std::abs(ws - target);
		if (distance < bestDistance || (distance == bestDistance && ws < best)) {
			best = ws;
			bestDistance = distance;
		}
	}
	return best;
}

} // namespace

int AutoWorldZoom(std::span<const int> ladder, int outputHeight)
{
	return NearestLadderEntry(ladder, outputHeight / 540.0);
}

int SnapToZoomLadder(std::span<const int> ladder, int ws)
{
	return NearestLadderEntry(ladder, ws);
}

int NextWorldZoomIn(std::span<const int> ladder, int ws)
{
	ws = SnapToZoomLadder(ladder, ws);
	for (const int entry : ladder) {
		if (entry > ws)
			return entry;
	}
	return ws;
}

int NextWorldZoomOut(std::span<const int> ladder, int ws)
{
	ws = SnapToZoomLadder(ladder, ws);
	int result = ws;
	for (const int entry : ladder) {
		if (entry >= ws)
			break;
		result = entry;
	}
	return result;
}

int AutoUiScale(Size output)
{
	int us = output.height / 480;
	while (us > 1 && output.width / us <= 640)
		us--;
	return std::max(us, 1);
}

UiPlacement ComputeUiPlacement(Size output, Size ui, bool integerScale)
{
	// Mirrors SDL2's `UpdateLogicalSize` (letterbox policy).
	UiPlacement placement { 1.0F, { 0, 0 } };
	if (ui.width <= 0 || ui.height <= 0 || output.width <= 0 || output.height <= 0)
		return placement;

	const float wantAspect = static_cast<float>(ui.width) / static_cast<float>(ui.height);
	const float realAspect = static_cast<float>(output.width) / static_cast<float>(output.height);
	int viewportWidth;
	int viewportHeight;
	if (integerScale) {
		float scale = wantAspect > realAspect
		    ? static_cast<float>(output.width / ui.width)
		    : static_cast<float>(output.height / ui.height);
		if (scale < 1.0F)
			scale = 1.0F;
		placement.scale = scale;
		viewportWidth = static_cast<int>(std::floor(ui.width * scale));
		viewportHeight = static_cast<int>(std::floor(ui.height * scale));
	} else if (std::fabs(wantAspect - realAspect) < 0.0001) {
		placement.scale = static_cast<float>(output.width) / static_cast<float>(ui.width);
		viewportWidth = output.width;
		viewportHeight = output.height;
	} else if (wantAspect > realAspect) {
		placement.scale = static_cast<float>(output.width) / static_cast<float>(ui.width);
		viewportWidth = output.width;
		viewportHeight = static_cast<int>(std::floor(ui.height * placement.scale));
	} else {
		placement.scale = static_cast<float>(output.height) / static_cast<float>(ui.height);
		viewportHeight = output.height;
		viewportWidth = static_cast<int>(std::floor(ui.width * placement.scale));
	}
	placement.origin = { (output.width - viewportWidth) / 2, (output.height - viewportHeight) / 2 };
	return placement;
}

Point UiToWorld(const WorldView &view, Point uiPosition)
{
	const double scale = view.uiScale;
	return {
		static_cast<int>(std::floor(((uiPosition.x + 0.5) * scale + view.uiOriginPx.x - view.originPx.x) / view.ws)),
		static_cast<int>(std::floor(((uiPosition.y + 0.5) * scale + view.uiOriginPx.y - view.originPx.y) / view.ws)),
	};
}

Point WorldToUi(const WorldView &view, Point worldPosition)
{
	const double scale = view.uiScale;
	return {
		static_cast<int>(std::floor(((worldPosition.x + 0.5) * view.ws + view.originPx.x - view.uiOriginPx.x) / scale)),
		static_cast<int>(std::floor(((worldPosition.y + 0.5) * view.ws + view.originPx.y - view.uiOriginPx.y) / scale)),
	};
}

WorldView ComputeWorldView(Size output, Size ui, UiPlacement placement, int ws, int mainPanelHeight)
{
	WorldView view;
	view.layered = true;
	view.ws = std::max(ws, 1);
	view.outputSize = output;
	view.uiSize = ui;
	view.size = { (output.width + view.ws - 1) / view.ws, (output.height + view.ws - 1) / view.ws };
	view.originPx = { (output.width - view.size.width * view.ws) / 2, (output.height - view.size.height * view.ws) / 2 };
	view.uiScale = placement.scale;
	view.uiOriginPx = placement.origin;
	view.anchor = UiToWorld(view, { ui.width / 2, (ui.height - mainPanelHeight) / 2 });
	return view;
}

namespace {

WorldView CurrentView;
bool LayeredActive;
/** @brief Zoom level to return to with the zoom toggle. */
std::optional<int> ZoomReturn;

std::vector<int> CurrentLadder()
{
	const GraphicsOptions &options = GetOptions().Graphics;
	return ComputeZoomLadder(CurrentView.outputSize.height, *options.worldZoomMinHeight, *options.worldZoomMaxHeight);
}

bool CanUseLayeredRenderer()
{
#if defined(USE_SDL1) || defined(USE_SDL3)
	return false;
#else
	return *GetOptions().Graphics.independentZoom
	    && *GetOptions().Graphics.upscale
	    && renderer != nullptr
	    && !HeadlessMode
	    && !demo::IsRunning()
	    && !demo::IsRecording();
#endif
}

void OnWorldZoomOptionChanged()
{
	RecalcWorldView();
	CalcViewportGeometry();
	RedrawEverything();
}

const auto OptionChangeHandlerIndependentZoom = (GetOptions().Graphics.independentZoom.SetValueChangedCallback(OnWorldZoomOptionChanged), true);
const auto OptionChangeHandlerWorldZoom = (GetOptions().Graphics.worldZoom.SetValueChangedCallback(OnWorldZoomOptionChanged), true);
const auto OptionChangeHandlerWorldZoomMinHeight = (GetOptions().Graphics.worldZoomMinHeight.SetValueChangedCallback(OnWorldZoomOptionChanged), true);
const auto OptionChangeHandlerWorldZoomMaxHeight = (GetOptions().Graphics.worldZoomMaxHeight.SetValueChangedCallback(OnWorldZoomOptionChanged), true);

} // namespace

const WorldView &GetWorldView()
{
	return CurrentView;
}

bool IsLayeredActive()
{
	return LayeredActive && !demo::IsRunning() && !demo::IsRecording();
}

void RecalcWorldView()
{
#if !defined(USE_SDL1) && !defined(USE_SDL3)
	if (CanUseLayeredRenderer()) {
		Size output;
		if (SDL_GetRendererOutputSize(renderer, &output.width, &output.height) == 0 && output.width > 0 && output.height > 0) {
			const Size ui { gnScreenWidth, gnScreenHeight };
			const UiPlacement placement = ComputeUiPlacement(output, ui, SDL_RenderGetIntegerScale(renderer) == SDL_TRUE);
			const GraphicsOptions &options = GetOptions().Graphics;
			const std::vector<int> ladder = ComputeZoomLadder(output.height, *options.worldZoomMinHeight, *options.worldZoomMaxHeight);
			const int ws = *options.worldZoom == 0 ? AutoWorldZoom(ladder, output.height) : SnapToZoomLadder(ladder, *options.worldZoom);
			CurrentView = ComputeWorldView(output, ui, placement, ws, GetMainPanel().size.height);
			LayeredActive = true;
			ResizeLayers(CurrentView.size, ui);
			return;
		}
	}
#endif
	LayeredActive = false;
	CurrentView.layered = false;
	EndLayeredMode();
	ReleaseLayers();
}

void SetWorldViewForTesting(const WorldView &view)
{
	CurrentView = view;
	LayeredActive = view.layered;
}

void SetWorldZoom(int ws)
{
	if (!IsLayeredActive())
		return;
	ws = SnapToZoomLadder(CurrentLadder(), ws);
	if (ws == CurrentView.ws && *GetOptions().Graphics.worldZoom == ws)
		return;
	// Persists the level and triggers `OnWorldZoomOptionChanged`.
	GetOptions().Graphics.worldZoom.SetValue(ws);
}

void WorldZoomIn()
{
	if (!IsLayeredActive())
		return;
	ZoomReturn = std::nullopt;
	SetWorldZoom(NextWorldZoomIn(CurrentLadder(), CurrentView.ws));
}

void WorldZoomOut()
{
	if (!IsLayeredActive())
		return;
	ZoomReturn = std::nullopt;
	SetWorldZoom(NextWorldZoomOut(CurrentLadder(), CurrentView.ws));
}

void WorldZoomToggle()
{
	if (!IsLayeredActive())
		return;
	if (!ZoomReturn) {
		ZoomReturn = CurrentView.ws;
		SetWorldZoom(SnapToZoomLadder(CurrentLadder(), 2 * CurrentView.ws));
	} else {
		const int ws = *ZoomReturn;
		ZoomReturn = std::nullopt;
		SetWorldZoom(ws);
	}
}

} // namespace devilution
