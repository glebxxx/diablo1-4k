/**
 * @file post_filter.cpp
 *
 * CPU pixel-art post filter (Scale2x/EPX, MMPX) for the final frame.
 */
#ifndef USE_SDL1

#include "engine/post_filter.hpp"

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <string>
#include <string_view>

#ifdef USE_SDL3
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#else
#include <SDL.h>

#include "utils/sdl_compat.h"
#endif

#define DEVILUTIONX_SCREENSHOT_FORMAT_PCX 0
#define DEVILUTIONX_SCREENSHOT_FORMAT_PNG 1

#if DEVILUTIONX_SCREENSHOT_FORMAT == DEVILUTIONX_SCREENSHOT_FORMAT_PNG
#include "utils/surface_to_png.hpp"
#endif

#include "appfat.h"
#include "engine/surface.hpp"
#include "options.h"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/parse_int.hpp"
#include "utils/paths.h"
#include "utils/sdl_ptrs.h"
#include "utils/sdl_wrap.h"
#include "utils/str_cat.hpp"
#include "utils/upscalers/upscalers.hpp"

namespace devilution {

namespace {

/** @brief Streaming texture that receives the magnified frame (factor x the game resolution). */
SDLTextureUniquePtr FilteredTexture;

/** @brief Size of the last texture that could not be created (e.g. too large for the GPU), so that it is not retried every frame. */
int FailedWidth = 0;
int FailedHeight = 0;

/** @brief Frames presented through the renderer so far, counted only when `DVX_DUMP_FRAME` is set. */
int PresentedFrames = 0;

/** @brief Frame number (1-based) from `DVX_DUMP_FRAME` whose pre- and post-filter images are written to the pref path. */
int GetDumpFrameNumber()
{
	static const int FrameNumber = [] {
		const char *value = std::getenv("DVX_DUMP_FRAME");
		if (value == nullptr)
			return -1;
		const ParseIntResult<int> parsed = ParseInt<int>(value, 1);
		if (!parsed.has_value()) {
			LogError("DVX_DUMP_FRAME: expected a frame number >= 1, got \"{}\"", value);
			return -1;
		}
		return *parsed;
	}();
	return FrameNumber;
}

int GetFactor(PostFilter filter)
{
	if (filter == PostFilter::Mmpx)
		return 2;
	return *GetOptions().Graphics.postFilterFactor == 3 ? 3 : 2;
}

void ApplyFilter(PostFilter filter, int factor, const SDL_Surface &src, void *dstPixels, int dstPitch)
{
	const upscalers::ConstImageView srcView {
		static_cast<const uint32_t *>(src.pixels),
		src.w,
		src.h,
		src.pitch / static_cast<int>(sizeof(uint32_t)),
	};
	const upscalers::ImageView dstView {
		static_cast<uint32_t *>(dstPixels),
		src.w * factor,
		src.h * factor,
		dstPitch / static_cast<int>(sizeof(uint32_t)),
	};
	if (filter == PostFilter::Mmpx) {
		upscalers::Mmpx2x(srcView, dstView);
	} else if (factor == 3) {
		upscalers::Scale3x(srcView, dstView);
	} else {
		upscalers::Scale2x(srcView, dstView);
	}
}

void DumpSurface(SDL_Surface *surface, std::string_view name)
{
#if DEVILUTIONX_SCREENSHOT_FORMAT == DEVILUTIONX_SCREENSHOT_FORMAT_PNG
	const std::string path = StrCat(paths::PrefPath(), "frame-", PresentedFrames, "-", name, ".png");
	SDL_IOStream *out = SDL_IOFromFile(path.c_str(), "wb");
	if (out == nullptr) {
		LogError("DVX_DUMP_FRAME: failed to open {}: {}", path, SDL_GetError());
		SDL_ClearError();
		return;
	}
	const std::expected<void, std::string> result = WriteSurfaceToFilePng(Surface(surface), out);
	if (!result.has_value()) {
		LogError("DVX_DUMP_FRAME: failed to write {}: {}", path, result.error());
		return;
	}
	Log("DVX_DUMP_FRAME: wrote {}", path);
#else
	LogError("DVX_DUMP_FRAME: {} not written, PNG support is not available in this build", name);
#endif
}

void DumpFiltered(PostFilter filter, int factor, SDL_Surface *src, std::string_view name)
{
#ifdef USE_SDL3
	SDLSurfaceUniquePtr dst { SDL_CreateSurface(src->w * factor, src->h * factor, src->format) };
	if (dst == nullptr) {
		LogError("DVX_DUMP_FRAME: {}", SDL_GetError());
		SDL_ClearError();
		return;
	}
#else
	SDLSurfaceUniquePtr dst = SDLWrap::CreateRGBSurfaceWithFormat(0, src->w * factor, src->h * factor, 32, src->format->format);
#endif
	ApplyFilter(filter, factor, *src, dst->pixels, dst->pitch);
	DumpSurface(dst.get(), name);
}

bool EnsureFilteredTexture(int width, int height)
{
	if (FilteredTexture != nullptr) {
#ifdef USE_SDL3
		if (FilteredTexture->w == width && FilteredTexture->h == height)
			return true;
#else
		int w;
		int h;
		if (SDL_QueryTexture(FilteredTexture.get(), nullptr, nullptr, &w, &h) == 0 && w == width && h == height)
			return true;
#endif
		FilteredTexture = nullptr;
	}
	if (width == FailedWidth && height == FailedHeight)
		return false;
#ifdef USE_SDL3
	FilteredTexture = SDLTextureUniquePtr { SDL_CreateTexture(renderer, texture->format, SDL_TEXTUREACCESS_STREAMING, width, height) };
#else
	Uint32 format;
	if (SDL_QueryTexture(texture.get(), &format, nullptr, nullptr, nullptr) < 0) {
		LogError("Post filter: SDL_QueryTexture: {}", SDL_GetError());
		SDL_ClearError();
		return false;
	}
	FilteredTexture = SDLTextureUniquePtr { SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING, width, height) };
#endif
	if (FilteredTexture == nullptr) {
		LogError("Post filter: failed to create a {}x{} texture, showing the frame unfiltered: {}", width, height, SDL_GetError());
		SDL_ClearError();
		FailedWidth = width;
		FailedHeight = height;
		return false;
	}
	return true;
}

/**
 * @brief Whether the frame is a regular game frame: the global texture and the surface have the
 * game resolution and 32-bit pixels.
 *
 * Video playback (storm_svid.cpp) replaces the global texture with one of the video's size.
 */
bool IsRegularFrame(const SDL_Surface &surface)
{
	if (texture == nullptr || surface.w != gnScreenWidth || surface.h != gnScreenHeight)
		return false;
#ifdef USE_SDL3
	if (texture->w != gnScreenWidth || texture->h != gnScreenHeight)
		return false;
	return SDL_BYTESPERPIXEL(surface.format) == 4;
#else
	int w;
	int h;
	if (SDL_QueryTexture(texture.get(), nullptr, nullptr, &w, &h) < 0 || w != gnScreenWidth || h != gnScreenHeight)
		return false;
	return surface.format->BytesPerPixel == 4;
#endif
}

} // namespace

bool RenderPostFiltered(SDL_Surface *surface)
{
	const bool dump = GetDumpFrameNumber() > 0 && ++PresentedFrames == GetDumpFrameNumber();
	if (dump)
		DumpSurface(surface, "pre");

	const PostFilter filter = *GetOptions().Graphics.postFilter;
	if (filter != PostFilter::Scale2x && filter != PostFilter::Mmpx) {
		if (FilteredTexture != nullptr)
			ResetPostFilter();
		return false;
	}
	if (!IsRegularFrame(*surface))
		return false;

	const int factor = GetFactor(filter);
	if (dump)
		DumpFiltered(filter, factor, surface, StrCat("post-", filter == PostFilter::Mmpx ? "mmpx" : "scale", factor, "x"));

	if (!EnsureFilteredTexture(surface->w * factor, surface->h * factor))
		return false;

	void *pixels;
	int pitch;
#ifdef USE_SDL3
	if (!SDL_LockTexture(FilteredTexture.get(), nullptr, &pixels, &pitch)) {
#else
	if (SDL_LockTexture(FilteredTexture.get(), nullptr, &pixels, &pitch) < 0) {
#endif
		LogError("Post filter: SDL_LockTexture: {}", SDL_GetError());
		SDL_ClearError();
		ResetPostFilter();
		return false;
	}
	ApplyFilter(filter, factor, *surface, pixels, pitch);
	SDL_UnlockTexture(FilteredTexture.get());

#ifdef USE_SDL3
	if (!SDL_RenderTexture(renderer, FilteredTexture.get(), nullptr, nullptr)) ErrSdl();
#else
	if (SDL_RenderCopy(renderer, FilteredTexture.get(), nullptr, nullptr) <= -1) ErrSdl();
#endif
	return true;
}

void ResetPostFilter()
{
	FilteredTexture = nullptr;
	FailedWidth = 0;
	FailedHeight = 0;
}

} // namespace devilution

#endif
