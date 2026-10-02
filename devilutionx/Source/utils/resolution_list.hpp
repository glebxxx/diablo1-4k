#pragma once

#include <string>
#include <utility>
#include <vector>

#include "engine/size.hpp"

namespace devilution {

struct ResolutionListParams {
	/**
	 * @brief Display modes in landscape orientation, multiplied by the DPI scaling factor.
	 *
	 * Ignored when `hiDpiOutputSize` is set.
	 */
	std::vector<Size> displayModes;

	/** @brief The resolution from the ini file. Always included, so that it remains selected. */
	Size configuredSize { 0, 0 };

	/** @brief The platform's preferred default resolution. */
	Size defaultSize { 0, 0 };

	/**
	 * @brief The desktop display mode as reported by SDL (in window coordinates).
	 *
	 * Only used with `fitToScreen` when `hiDpiOutputSize` is empty. Empty if unknown.
	 */
	Size desktopSize { 0, 0 };

	/**
	 * @brief The desktop size in output pixels (landscape) if the display has more than one pixel
	 * per window coordinate (HiDPI, e.g. macOS Retina) and the game is upscaled. Empty otherwise.
	 *
	 * When set, the list is built from exact integer fractions of this size and common heights at
	 * its aspect ratio instead of from `displayModes`, entries larger than it are not offered, and
	 * entries that scale to it exactly are labelled with their scale factor, e.g. "1080p (x3)".
	 */
	Size hiDpiOutputSize { 0, 0 };

	/** @brief Whether the platform can display any resolution (e.g. by upscaling). */
	bool supportsAnyResolution = false;

	/** @brief "Fit to Screen": widths follow the desktop aspect ratio and entries are labelled by height. */
	bool fitToScreen = false;

	/** @brief Whether to remove resolutions smaller than 640x480. */
	bool removeSmallResolutions = true;
};

/**
 * @brief Returns the resolutions to offer in the settings menu, sorted from largest to smallest,
 * together with their labels.
 */
std::vector<std::pair<Size, std::string>> BuildResolutionList(const ResolutionListParams &params);

} // namespace devilution
