/**
 * @file post_filter.hpp
 *
 * CPU pixel-art post filter (Scale2x/EPX, MMPX) for the final frame.
 */
#pragma once

#ifndef USE_SDL1

#ifdef USE_SDL3
#include <SDL3/SDL_surface.h>
#else
#include <SDL.h>
#endif

namespace devilution {

/**
 * @brief Renders the frame through the post filter selected in the options.
 *
 * Magnifies `surface` (the 32-bit renderer texture surface) into a separate streaming texture and
 * copies it to the renderer. The logical render size is not changed.
 *
 * @return false if nothing was rendered: the filter is off, or the frame is not a regular game
 * frame (e.g. during video playback). The caller then renders the frame the normal way.
 */
bool RenderPostFiltered(SDL_Surface *surface);

/** @brief Releases the post filter texture, e.g. before the renderer is destroyed. */
void ResetPostFilter();

} // namespace devilution

#endif
