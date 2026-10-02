#include "engine/layered_present.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "engine/dx.h"
#include "engine/palette.h"
#include "engine/render/scrollrt.h"
#include "utils/log.hpp"
#include "utils/sdl_ptrs.h"
#include "utils/sdl_wrap.h"

namespace devilution {

#ifndef USE_SDL1
extern SDL_Renderer *renderer;
#endif

namespace {

SDLSurfaceUniquePtr WorldSurface;
std::vector<uint8_t> UiHalfPlane;

#if !defined(USE_SDL1) && !defined(USE_SDL3)
SDLTextureUniquePtr WorldTexture;
Size WorldTextureSize;
SDLTextureUniquePtr UiTexture;
Size UiTextureSize;

bool EnsureTexture(SDLTextureUniquePtr &texture, Size &textureSize, Size size, bool blend)
{
	if (texture != nullptr && textureSize == size)
		return true;
	texture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, size.width, size.height));
	if (texture == nullptr) {
		LogError("Layered renderer: SDL_CreateTexture failed: {}", SDL_GetError());
		SDL_ClearError();
		return false;
	}
	textureSize = size;
	// Integer factors: nearest sampling keeps pixels crisp, linear would add dark fringes at alpha-keyed edges.
	SDL_SetTextureScaleMode(texture.get(), SDL_ScaleModeNearest);
	SDL_SetTextureBlendMode(texture.get(), blend ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
	return true;
}
#endif

void UpdateUiLayerRange()
{
	if (PalSurface == nullptr || UiHalfPlane.empty()) {
		UiLayer.begin = nullptr;
		UiLayer.end = nullptr;
		UiLayer.halfPlane = nullptr;
		return;
	}
	const auto *pixels = static_cast<const uint8_t *>(PalSurface->pixels);
	const size_t size = static_cast<size_t>(PalSurface->pitch) * PalSurface->h;
	if (UiHalfPlane.size() < size)
		UiHalfPlane.resize(size);
	UiLayer.begin = pixels;
	UiLayer.end = pixels + size;
	UiLayer.halfPlane = UiHalfPlane.data();
}

uint32_t BlendHalf(uint32_t ui, uint32_t world)
{
	// SDL_BLENDMODE_BLEND with alpha 0x80: dst = src * a + dst * (1 - a).
	uint32_t result = 0xFF000000;
	for (int shift = 0; shift < 24; shift += 8) {
		const uint32_t s = (ui >> shift) & 0xFF;
		const uint32_t d = (world >> shift) & 0xFF;
		result |= ((s * 0x80 + d * (0xFF - 0x80) + 127) / 0xFF) << shift;
	}
	return result;
}

// Perf log
constexpr int PerfLogInterval = 300;
constexpr size_t PerfStageCount = 4;
std::array<double, PerfStageCount> PerfFrame {};
std::array<std::vector<double>, PerfStageCount> PerfSamples;

} // namespace

Surface WorldBuffer()
{
	return Surface(WorldSurface.get());
}

void ResizeLayers(Size worldSize, Size uiSize)
{
	if (WorldSurface == nullptr || WorldSurface->w != worldSize.width || WorldSurface->h != worldSize.height) {
		WorldSurface = SDLWrap::CreateRGBSurfaceWithFormat(0, worldSize.width, worldSize.height, 8, SDL_PIXELFORMAT_INDEX8);
		std::memset(WorldSurface->pixels, 0, static_cast<size_t>(WorldSurface->pitch) * WorldSurface->h);
	}
	const size_t halfSize = PalSurface != nullptr
	    ? static_cast<size_t>(PalSurface->pitch) * PalSurface->h
	    : static_cast<size_t>(uiSize.width) * uiSize.height;
	UiHalfPlane.resize(halfSize);
	UpdateUiLayerRange();
}

void ReleaseLayers()
{
	UiLayer.presentLayered = false;
	WorldSurface = nullptr;
	UiHalfPlane = {};
	UpdateUiLayerRange();
#if !defined(USE_SDL1) && !defined(USE_SDL3)
	WorldTexture = nullptr;
	WorldTextureSize = {};
	UiTexture = nullptr;
	UiTextureSize = {};
#endif
}

void BeginLayeredFrame()
{
	UpdateUiLayerRange();
	if (PalSurface == nullptr || WorldSurface == nullptr)
		return;
	std::memset(PalSurface->pixels, UiKeyTransparent, static_cast<size_t>(PalSurface->pitch) * PalSurface->h);
	std::memset(WorldSurface->pixels, 0, static_cast<size_t>(WorldSurface->pitch) * WorldSurface->h);
	UiLayer.presentLayered = true;
}

void EndLayeredMode()
{
	UiLayer.presentLayered = false;
}

bool PresentLayered()
{
	return UiLayer.presentLayered;
}

void RebuildUiKeyRemap()
{
	RebuildUiKeyRemap(logical_palette.data());
}

void BuildPaletteLut(const SDL_Color *palette, uint32_t lut[256])
{
	for (unsigned i = 0; i < 256; ++i) {
		lut[i] = (static_cast<uint32_t>(palette[i].r) << 16) | (static_cast<uint32_t>(palette[i].g) << 8) | palette[i].b;
	}
}

void ExpandIndexed(const uint8_t *src, int srcPitch, uint32_t *dst, int dstPitch, int width, int height, const uint32_t lut[256])
{
	std::array<uint32_t, 256> opaque;
	for (unsigned i = 0; i < 256; ++i)
		opaque[i] = lut[i] | 0xFF000000;
	for (int y = 0; y < height; ++y) {
		const uint8_t *s = src + static_cast<ptrdiff_t>(y) * srcPitch;
		auto *d = reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(y) * dstPitch);
		for (int x = 0; x < width; ++x)
			d[x] = opaque[s[x]];
	}
}

void ExpandUiKeyed(const uint8_t *src, const uint8_t *half, int srcPitch, uint32_t *dst, int dstPitch, int width, int height, const uint32_t lut[256])
{
	std::array<uint32_t, 256> opaque;
	for (unsigned i = 0; i < 256; ++i)
		opaque[i] = lut[i] | 0xFF000000;
	opaque[UiKeyTransparent] = 0;
	for (int y = 0; y < height; ++y) {
		const uint8_t *s = src + static_cast<ptrdiff_t>(y) * srcPitch;
		const uint8_t *h = half + static_cast<ptrdiff_t>(y) * srcPitch;
		auto *d = reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(dst) + static_cast<ptrdiff_t>(y) * dstPitch);
		for (int x = 0; x < width; ++x) {
			const uint8_t c = s[x];
			d[x] = c == UiKeyHalf ? ((lut[h[x]] & 0xFFFFFF) | 0x80000000) : opaque[c];
		}
	}
}

bool LayeredPresent()
{
#if defined(USE_SDL1) || defined(USE_SDL3)
	return false;
#else
	if (!UiLayer.presentLayered || renderer == nullptr || WorldSurface == nullptr || PalSurface == nullptr || UiHalfPlane.empty())
		return false;

	// The output size can change without a call to `CreateBackBuffer` (e.g. window resize, display change).
	{
		Size output;
		if (SDL_GetRendererOutputSize(renderer, &output.width, &output.height) == 0 && output != GetWorldView().outputSize) {
			RecalcWorldView();
			CalcViewportGeometry();
			if (!UiLayer.presentLayered || WorldSurface == nullptr)
				return false;
		}
	}

	const WorldView &view = GetWorldView();
	const Size uiSize { PalSurface->w, PalSurface->h };
	if (!EnsureTexture(WorldTexture, WorldTextureSize, view.size, /*blend=*/false)
	    || !EnsureTexture(UiTexture, UiTextureSize, uiSize, /*blend=*/true))
		return false;

	const bool perf = IsLayeredPerfLogEnabled();
	double start = perf ? LayeredPerfNow() : 0;

	uint32_t lut[256];
	BuildPaletteLut(system_palette.data(), lut);

	void *pixels;
	int pitch;
	if (SDL_LockTexture(WorldTexture.get(), nullptr, &pixels, &pitch) != 0) {
		LogError("Layered renderer: SDL_LockTexture failed: {}", SDL_GetError());
		SDL_ClearError();
		return false;
	}
	ExpandIndexed(static_cast<const uint8_t *>(WorldSurface->pixels), WorldSurface->pitch, static_cast<uint32_t *>(pixels), pitch,
	    WorldSurface->w, WorldSurface->h, lut);
	SDL_UnlockTexture(WorldTexture.get());

	if (SDL_LockTexture(UiTexture.get(), nullptr, &pixels, &pitch) != 0) {
		LogError("Layered renderer: SDL_LockTexture failed: {}", SDL_GetError());
		SDL_ClearError();
		return false;
	}
	ExpandUiKeyed(static_cast<const uint8_t *>(PalSurface->pixels), UiHalfPlane.data(), PalSurface->pitch, static_cast<uint32_t *>(pixels), pitch,
	    PalSurface->w, PalSurface->h, lut);
	SDL_UnlockTexture(UiTexture.get());

	if (perf) {
		const double now = LayeredPerfNow();
		LayeredPerfAdd(LayeredPerfStage::Expand, now - start);
		start = now;
	}

	if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) <= -1) ErrSdl();
	if (SDL_RenderClear(renderer) <= -1) ErrSdl();

	// In logical (UI) units: SDL multiplies by the UI scale and adds the viewport origin,
	// which yields exactly the physical rectangle of the world.
	const float uiScale = view.uiScale;
	const SDL_FRect worldRect {
		static_cast<float>(view.originPx.x - view.uiOriginPx.x) / uiScale,
		static_cast<float>(view.originPx.y - view.uiOriginPx.y) / uiScale,
		static_cast<float>(view.size.width * view.ws) / uiScale,
		static_cast<float>(view.size.height * view.ws) / uiScale,
	};
	if (SDL_RenderCopyF(renderer, WorldTexture.get(), nullptr, &worldRect) <= -1) ErrSdl();
	if (SDL_RenderCopy(renderer, UiTexture.get(), nullptr, nullptr) <= -1) ErrSdl();

	if (perf)
		LayeredPerfAdd(LayeredPerfStage::Present, LayeredPerfNow() - start);
	return true;
#endif
}

void ComposeLayersCpu(const WorldView &view, const uint8_t *world, int worldPitch, const uint8_t *ui, const uint8_t *half, int uiPitch,
    const uint32_t lut[256], std::span<uint32_t> out)
{
	const Size output = view.outputSize;
	const double uiScale = view.uiScale;
	std::vector<int> uiColumns(output.width);
	for (int x = 0; x < output.width; ++x)
		uiColumns[x] = static_cast<int>(std::floor((x + 0.5 - view.uiOriginPx.x) / uiScale));
	for (int y = 0; y < output.height; ++y) {
		const int wy = std::clamp((y - view.originPx.y) / view.ws, 0, view.size.height - 1);
		const int uy = static_cast<int>(std::floor((y + 0.5 - view.uiOriginPx.y) / uiScale));
		const bool uiRow = uy >= 0 && uy < view.uiSize.height;
		uint32_t *dst = &out[static_cast<size_t>(y) * output.width];
		for (int x = 0; x < output.width; ++x) {
			const int wx = std::clamp((x - view.originPx.x) / view.ws, 0, view.size.width - 1);
			uint32_t color = lut[world[static_cast<ptrdiff_t>(wy) * worldPitch + wx]] | 0xFF000000;
			const int ux = uiColumns[x];
			if (uiRow && ux >= 0 && ux < view.uiSize.width) {
				const ptrdiff_t i = static_cast<ptrdiff_t>(uy) * uiPitch + ux;
				const uint8_t c = ui[i];
				if (c == UiKeyHalf) {
					color = BlendHalf(lut[half[i]], color);
				} else if (c != UiKeyTransparent) {
					color = lut[c] | 0xFF000000;
				}
			}
			dst[x] = color;
		}
	}
}

void ComposeLayersCpu(Size output, std::span<uint32_t> out)
{
	if (WorldSurface == nullptr || PalSurface == nullptr || UiHalfPlane.empty())
		return;
	WorldView view = GetWorldView();
	view.outputSize = output;
	uint32_t lut[256];
	BuildPaletteLut(system_palette.data(), lut);
	ComposeLayersCpu(view, static_cast<const uint8_t *>(WorldSurface->pixels), WorldSurface->pitch,
	    static_cast<const uint8_t *>(PalSurface->pixels), UiHalfPlane.data(), PalSurface->pitch, lut, out);
}

bool IsLayeredPerfLogEnabled()
{
	static const bool Enabled = [] {
		const char *value = std::getenv("DEVILUTIONX_PERF_LOG");
		return value != nullptr && std::string_view(value) == "1";
	}();
	return Enabled;
}

double LayeredPerfNow()
{
	return static_cast<double>(SDL_GetPerformanceCounter()) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}

void LayeredPerfAdd(LayeredPerfStage stage, double milliseconds)
{
	PerfFrame[static_cast<size_t>(stage)] += milliseconds;
}

double LayeredPerfGet(LayeredPerfStage stage)
{
	return PerfFrame[static_cast<size_t>(stage)];
}

void LayeredPerfFrameDone()
{
	if (!IsLayeredPerfLogEnabled())
		return;
	double total = 0;
	for (size_t i = 0; i < PerfStageCount; ++i) {
		PerfSamples[i].push_back(PerfFrame[i]);
		total += PerfFrame[i];
		PerfFrame[i] = 0;
	}
	static std::vector<double> totals;
	totals.push_back(total);
	if (totals.size() < PerfLogInterval)
		return;

	auto stats = [](std::vector<double> &samples) {
		double sum = 0;
		for (const double sample : samples)
			sum += sample;
		const double avg = sum / static_cast<double>(samples.size());
		const size_t p95Index = std::min(samples.size() - 1, (samples.size() * 95) / 100);
		std::nth_element(samples.begin(), samples.begin() + static_cast<ptrdiff_t>(p95Index), samples.end());
		const double p95 = samples[p95Index];
		samples.clear();
		return std::pair { avg, p95 };
	};
	const WorldView &view = GetWorldView();
	const auto [worldAvg, worldP95] = stats(PerfSamples[static_cast<size_t>(LayeredPerfStage::WorldDraw)]);
	const auto [uiAvg, uiP95] = stats(PerfSamples[static_cast<size_t>(LayeredPerfStage::UiDraw)]);
	const auto [expandAvg, expandP95] = stats(PerfSamples[static_cast<size_t>(LayeredPerfStage::Expand)]);
	const auto [presentAvg, presentP95] = stats(PerfSamples[static_cast<size_t>(LayeredPerfStage::Present)]);
	const auto [totalAvg, totalP95] = stats(totals);
	Log("perf ws={} world={}x{} ui={}x{} out={}x{}: world {:.2f}/{:.2f} ms, ui {:.2f}/{:.2f} ms, expand {:.2f}/{:.2f} ms, present {:.2f}/{:.2f} ms, total {:.2f}/{:.2f} ms (avg/p95)",
	    view.ws, view.size.width, view.size.height, view.uiSize.width, view.uiSize.height, view.outputSize.width, view.outputSize.height,
	    worldAvg, worldP95, uiAvg, uiP95, expandAvg, expandP95, presentAvg, presentP95, totalAvg, totalP95);
}

} // namespace devilution
