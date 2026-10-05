/**
 * @file layered_present.cpp
 *
 * Implementation of the layered renderer's buffers and present.
 */
#include "engine/layered_present.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "engine/backbuffer_state.hpp"
#include "engine/dx.h"
#include "engine/palette.h"
#include "engine/render/scrollrt.h"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/sdl_ptrs.h"
#include "utils/sdl_wrap.h"

namespace devilution {

namespace {

constexpr uint32_t OpaqueAlpha = 0xFF000000;
constexpr uint32_t HalfAlpha = 0x80000000;
constexpr uint32_t RgbMask = 0x00FFFFFF;

uint32_t *Row(uint32_t *base, int pitchBytes, int y)
{
	return reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(base) + static_cast<std::ptrdiff_t>(y) * pitchBytes);
}

/** @brief `src` over `dst` with alpha `alpha` (8-bit), per channel, rounded to nearest. */
uint32_t BlendOver(uint32_t src, uint32_t dst, uint32_t alpha)
{
	uint32_t result = OpaqueAlpha;
	for (const int shift : { 0, 8, 16 }) {
		const uint32_t s = (src >> shift) & 0xFF;
		const uint32_t d = (dst >> shift) & 0xFF;
		result |= (((s * alpha) + (d * (255 - alpha)) + 127) / 255) << shift;
	}
	return result;
}

/** @brief Source pixel of a layer for each output pixel along one axis, -1 outside of the layer. */
std::vector<int> WorldSamples(int outputLength, int origin, int ws, int worldLength)
{
	std::vector<int> samples(outputLength);
	for (int i = 0; i < outputLength; ++i) {
		const int q = FloorDiv(i - origin, ws);
		samples[i] = q >= 0 && q < worldLength ? q : -1;
	}
	return samples;
}

std::vector<int> UiSamples(int outputLength, int origin, int viewportLength, int uiLength)
{
	std::vector<int> samples(outputLength, -1);
	if (viewportLength <= 0)
		return samples;
	for (int i = 0; i < outputLength; ++i) {
		const int offset = i - origin;
		if (offset < 0 || offset >= viewportLength)
			continue;
		// Nearest sampling at the pixel centre: floor((offset + 0.5) * uiLength / viewportLength).
		const auto u = static_cast<int>(((2 * static_cast<int64_t>(offset) + 1) * uiLength) / (2 * static_cast<int64_t>(viewportLength)));
		samples[i] = std::clamp(u, 0, uiLength - 1);
	}
	return samples;
}

#ifdef DVL_LAYERED_RENDERER
/** @brief The world layer (W of the current view), 8-bit with the global palette. */
SDLSurfaceUniquePtr WorldSurface;
/** @brief Colours of the `UiKeyHalf` pixels of the UI layer, same pitch and height as `PalSurface`. */
std::vector<uint8_t> UiHalfPlane;

SDLTextureUniquePtr WorldTexture;
Size WorldTextureSize;
SDLTextureUniquePtr UiTexture;
Size UiTextureSize;

size_t UiLayerByteSize()
{
	return static_cast<size_t>(PalSurface->pitch) * static_cast<size_t>(PalSurface->h);
}

void FreeLayeredBuffers()
{
	EndLayeredMode();
	SetUiLayerTarget(nullptr, nullptr);
	WorldSurface = nullptr;
	UiHalfPlane = {};
}

bool LayeredBuffersMatch(const WorldView &view)
{
	return WorldSurface != nullptr && PalSurface != nullptr
	    && WorldSurface->w == view.size.width && WorldSurface->h == view.size.height
	    && PalSurface->w == view.uiSize.width && PalSurface->h == view.uiSize.height
	    && PalSurface->pixels == UiLayer.uiPixels && UiHalfPlane.size() == UiLayerByteSize();
}

void EnsureTexture(SDLTextureUniquePtr &texture, Size &textureSize, Size size, SDL_BlendMode blendMode)
{
	if (texture != nullptr && textureSize == size)
		return;
	texture = nullptr;
	texture = SDLWrap::CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, size.width, size.height);
	// Integer factors: nearest sampling keeps the pixels crisp, linear sampling would fringe the keyed edges.
	if (SDL_SetTextureScaleMode(texture.get(), SDL_ScaleModeNearest) < 0) {
		LogError("SDL_SetTextureScaleMode: {}", SDL_GetError());
		SDL_ClearError();
	}
	if (SDL_SetTextureBlendMode(texture.get(), blendMode) < 0)
		ErrSdl();
	textureSize = size;
}

template <typename ExpandFn>
void UpdateStreamingTexture(SDL_Texture *texture, ExpandFn &&expand)
{
	void *pixels;
	int pitch;
	if (SDL_LockTexture(texture, nullptr, &pixels, &pitch) < 0)
		ErrSdl();
	expand(static_cast<uint32_t *>(pixels), pitch);
	SDL_UnlockTexture(texture);
}
#endif

} // namespace

void BuildPaletteLut32(const SDL_Color *palette, uint32_t lut[256])
{
	for (unsigned i = 0; i < 256; ++i) {
		lut[i] = (static_cast<uint32_t>(palette[i].r) << 16) | (static_cast<uint32_t>(palette[i].g) << 8) | static_cast<uint32_t>(palette[i].b);
	}
}

void ExpandIndexed(const uint8_t *src, int srcPitch, uint32_t *dst, int dstPitch, int width, int height, const uint32_t lut[256])
{
	for (int y = 0; y < height; ++y) {
		const uint8_t *srcRow = src + static_cast<std::ptrdiff_t>(y) * srcPitch;
		uint32_t *dstRow = Row(dst, dstPitch, y);
		for (int x = 0; x < width; ++x) {
			dstRow[x] = lut[srcRow[x]] | OpaqueAlpha;
		}
	}
}

void ExpandUiKeyed(const uint8_t *src, const uint8_t *half, int srcPitch, uint32_t *dst, int dstPitch, int width, int height, const uint32_t lut[256])
{
	for (int y = 0; y < height; ++y) {
		const uint8_t *srcRow = src + static_cast<std::ptrdiff_t>(y) * srcPitch;
		const uint8_t *halfRow = half + static_cast<std::ptrdiff_t>(y) * srcPitch;
		uint32_t *dstRow = Row(dst, dstPitch, y);
		for (int x = 0; x < width; ++x) {
			const uint8_t color = srcRow[x];
			if (color == UiKeyTransparent) {
				dstRow[x] = 0;
			} else if (color == UiKeyHalf) {
				dstRow[x] = (lut[halfRow[x]] & RgbMask) | HalfAlpha;
			} else {
				dstRow[x] = lut[color] | OpaqueAlpha;
			}
		}
	}
}

void ComposeLayersCpu(const WorldView &view, const LayeredFrame &frame, const uint32_t lut[256], Size output, std::span<uint32_t> out)
{
	const std::vector<int> worldX = WorldSamples(output.width, view.originPx.x, view.ws, view.size.width);
	const std::vector<int> worldY = WorldSamples(output.height, view.originPx.y, view.ws, view.size.height);
	const std::vector<int> uiX = UiSamples(output.width, view.uiOriginPx.x, view.uiViewportSize.width, view.uiSize.width);
	const std::vector<int> uiY = UiSamples(output.height, view.uiOriginPx.y, view.uiViewportSize.height, view.uiSize.height);

	for (int y = 0; y < output.height; ++y) {
		uint32_t *outRow = out.data() + static_cast<std::ptrdiff_t>(y) * output.width;
		const uint8_t *worldRow = worldY[y] >= 0 ? frame.world + static_cast<std::ptrdiff_t>(worldY[y]) * frame.worldPitch : nullptr;
		const std::ptrdiff_t uiOffset = uiY[y] >= 0 ? static_cast<std::ptrdiff_t>(uiY[y]) * frame.uiPitch : -1;
		for (int x = 0; x < output.width; ++x) {
			uint32_t color = OpaqueAlpha;
			if (worldRow != nullptr && worldX[x] >= 0)
				color = lut[worldRow[worldX[x]]] | OpaqueAlpha;
			if (uiOffset >= 0 && uiX[x] >= 0) {
				const std::ptrdiff_t i = uiOffset + uiX[x];
				const uint8_t uiColor = frame.ui[i];
				if (uiColor == UiKeyHalf) {
					color = BlendOver(lut[frame.half[i]], color, HalfAlpha >> 24);
				} else if (uiColor != UiKeyTransparent) {
					color = lut[uiColor] | OpaqueAlpha;
				}
			}
			outRow[x] = color;
		}
	}
}

#ifdef DVL_LAYERED_RENDERER
Surface WorldBuffer()
{
	if (WorldSurface == nullptr)
		return {};
	return Surface(WorldSurface.get());
}

void BeginLayeredFrame()
{
	// The output size and the integer scaling can change without a hook (e.g. Alt+Enter or the
	// "Integer Scaling" option), so the view is checked every frame. Without a renderer (unit tests)
	// the view is whatever was set.
	if (renderer != nullptr && RecalcWorldView()) {
		CalcViewportGeometry();
		RedrawEverything();
	}
	if (!IsLayeredActive())
		return;
	UpdateLayeredBuffers();
	if (!LayeredBuffersMatch(GetWorldView())) {
		EndLayeredMode();
		return;
	}
	std::memset(PalSurface->pixels, UiKeyTransparent, UiLayerByteSize());
	SetPresentLayered(true);
}

void EndLayeredMode()
{
	if (!PresentLayered())
		return;
	SetPresentLayered(false);

	// The upstream present shows the renderer texture surface, which still holds whatever was blitted
	// before the layered mode began. Clear it so that it is not flashed.
	if (renderer != nullptr) {
		SDL_Surface *output = GetOutputSurface();
		if (output != nullptr)
			SDL_FillRect(output, nullptr, SDL_MapRGB(output->format, 0, 0, 0));
	}
}

void RebuildUiKeyRemap()
{
	// `paletteTransparencyLookup` is generated from `logical_palette`, so the replacements are too.
	UpdateUiKeyRemap(logical_palette.data());
}

void UpdateLayeredBuffers()
{
	if (!IsLayeredActive() || PalSurface == nullptr) {
		FreeLayeredBuffers();
		return;
	}

	const Size worldSize = GetWorldView().size;
	if (WorldSurface == nullptr || WorldSurface->w != worldSize.width || WorldSurface->h != worldSize.height) {
		WorldSurface = SDLWrap::CreateRGBSurfaceWithFormat(0, worldSize.width, worldSize.height, 8, SDL_PIXELFORMAT_INDEX8);
		if (Palette != nullptr && SDL_SetSurfacePalette(WorldSurface.get(), Palette.get()) < 0)
			ErrSdl();
	}

	const size_t halfPlaneSize = UiLayerByteSize();
	if (PresentLayered() && (PalSurface->pixels != UiLayer.uiPixels || UiHalfPlane.size() != halfPlaneSize)) {
		// The back buffer was recreated: it no longer holds the layered frame.
		EndLayeredMode();
	}
	if (UiHalfPlane.size() != halfPlaneSize)
		UiHalfPlane.assign(halfPlaneSize, 0);
	SetUiLayerTarget(PalSurface->pixels, UiHalfPlane.data());
}

void FreeLayeredResources()
{
	SetPresentLayered(false);
	SetUiLayerTarget(nullptr, nullptr);
	WorldTexture = nullptr;
	WorldTextureSize = {};
	UiTexture = nullptr;
	UiTextureSize = {};
	WorldSurface = nullptr;
	UiHalfPlane = {};
}

bool LayeredPresent()
{
	const WorldView &view = GetWorldView();
	if (renderer == nullptr || !IsLayeredActive() || !LayeredBuffersMatch(view)) {
		EndLayeredMode();
		return false;
	}

	EnsureTexture(WorldTexture, WorldTextureSize, view.size, SDL_BLENDMODE_NONE);
	EnsureTexture(UiTexture, UiTextureSize, view.uiSize, SDL_BLENDMODE_BLEND);

	// Rebuilt every present: fades, brightness and colour cycling only change `system_palette`.
	uint32_t lut[256];
	BuildPaletteLut32(system_palette.data(), lut);
	UpdateStreamingTexture(WorldTexture.get(), [&](uint32_t *pixels, int pitch) {
		ExpandIndexed(static_cast<const uint8_t *>(WorldSurface->pixels), WorldSurface->pitch, pixels, pitch, view.size.width, view.size.height, lut);
	});
	UpdateStreamingTexture(UiTexture.get(), [&](uint32_t *pixels, int pitch) {
		ExpandUiKeyed(static_cast<const uint8_t *>(PalSurface->pixels), UiHalfPlane.data(), PalSurface->pitch, pixels, pitch, view.uiSize.width, view.uiSize.height, lut);
	});

	if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) <= -1)
		ErrSdl();
	if (SDL_RenderClear(renderer) <= -1)
		ErrSdl();

	int logicalWidth;
	int logicalHeight;
	SDL_RenderGetLogicalSize(renderer, &logicalWidth, &logicalHeight);
	const bool logicalSizeIsUi = logicalWidth == view.uiSize.width && logicalHeight == view.uiSize.height;
	const bool uiViewportIsOutput = view.uiOriginPx == Point { 0, 0 } && view.uiViewportSize == view.outputSize;
	if (logicalSizeIsUi && uiViewportIsOutput) {
		// Draw the world in logical (UI) units: the renderer multiplies by the UI scale and adds the
		// UI viewport origin, which gives exactly the physical rectangle of the world.
		const float scale = view.uiScale;
		const SDL_FRect worldRect {
			static_cast<float>(view.originPx.x - view.uiOriginPx.x) / scale,
			static_cast<float>(view.originPx.y - view.uiOriginPx.y) / scale,
			static_cast<float>(view.size.width * view.ws) / scale,
			static_cast<float>(view.size.height * view.ws) / scale,
		};
		if (SDL_RenderCopyF(renderer, WorldTexture.get(), nullptr, &worldRect) <= -1)
			ErrSdl();
		if (SDL_RenderCopy(renderer, UiTexture.get(), nullptr, nullptr) <= -1)
			ErrSdl();
		return true;
	}

	// The renderer clips to the UI viewport, which does not cover the whole output here (letterbox or
	// pillarbox bars), so draw both layers at their physical rectangles without a logical size.
	if (logicalWidth != 0 && SDL_RenderSetLogicalSize(renderer, 0, 0) <= -1)
		ErrSdl();
	const SDL_Rect worldRect { view.originPx.x, view.originPx.y, view.size.width * view.ws, view.size.height * view.ws };
	const SDL_Rect uiRect { view.uiOriginPx.x, view.uiOriginPx.y, view.uiViewportSize.width, view.uiViewportSize.height };
	if (SDL_RenderCopy(renderer, WorldTexture.get(), nullptr, &worldRect) <= -1)
		ErrSdl();
	if (SDL_RenderCopy(renderer, UiTexture.get(), nullptr, &uiRect) <= -1)
		ErrSdl();
	if (logicalWidth != 0 && SDL_RenderSetLogicalSize(renderer, logicalWidth, logicalHeight) <= -1)
		ErrSdl();
	return true;
}

void ComposeLayersCpu(Size output, std::span<uint32_t> out)
{
	const WorldView &view = GetWorldView();
	if (!IsLayeredActive() || !LayeredBuffersMatch(view)) {
		std::fill(out.begin(), out.end(), OpaqueAlpha);
		return;
	}
	uint32_t lut[256];
	BuildPaletteLut32(system_palette.data(), lut);
	const LayeredFrame frame {
		static_cast<const uint8_t *>(WorldSurface->pixels),
		WorldSurface->pitch,
		static_cast<const uint8_t *>(PalSurface->pixels),
		UiHalfPlane.data(),
		PalSurface->pitch,
	};
	ComposeLayersCpu(view, frame, lut, output, out);
}
#endif

} // namespace devilution
