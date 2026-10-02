#include "utils/resolution_list.hpp"

#include <algorithm>

#include "utils/algorithm/container.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/**
 * @brief Adds resolutions suited to a HiDPI output of the given size (in pixels).
 */
void AddHiDpiResolutions(Size output, bool fitToScreen, std::vector<Size> &sizes)
{
	// Exact integer fractions of the output are scaled pixel-perfectly (also with "Integer Scaling").
	for (int factor = 1; output.height / factor >= 480; ++factor) {
		if (output.width % factor == 0 && output.height % factor == 0)
			sizes.emplace_back(output.width / factor, output.height / factor);
	}

	// Common heights at the output's aspect ratio.
	// Without "Fit to Screen", only if the width is a whole number, so that the aspect ratio is exact.
	constexpr int CommonHeights[] = { 480, 540, 720, 1080, 1440, 2160, 2880 };
	for (const int height : CommonHeights) {
		if (height > output.height)
			break;
		if (!fitToScreen && height * output.width % output.height != 0)
			continue;
		sizes.emplace_back(height * output.width / output.height, height);
	}
}

/**
 * @brief Returns `n` if `size` scaled by `n` is exactly `output`, or 0 otherwise.
 */
int GetExactScale(Size size, Size output)
{
	if (size.width <= 0 || size.height <= 0)
		return 0;
	if (output.width % size.width != 0 || output.height % size.height != 0)
		return 0;
	const int scale = output.width / size.width;
	return output.height / size.height == scale ? scale : 0;
}

} // namespace

std::vector<std::pair<Size, std::string>> BuildResolutionList(const ResolutionListParams &params)
{
	const Size output = params.hiDpiOutputSize;
	const bool isHiDpi = output.width > 0 && output.height > 0;

	std::vector<Size> sizes;
	if (isHiDpi) {
		// Display modes are in window coordinates and may each have a different pixel density,
		// so they do not describe what the game is scaled to. Use the output size in pixels instead.
		AddHiDpiResolutions(output, params.fitToScreen, sizes);
	} else {
		sizes = params.displayModes;
		if (params.supportsAnyResolution && sizes.size() == 1) {
			// Attempt to provide sensible options for 4:3 and the native aspect ratio
			const int width = sizes[0].width;
			const int height = sizes[0].height;
			const int commonHeights[] = { 480, 540, 720, 960, 1080, 1440, 2160 };
			for (const int commonHeight : commonHeights) {
				if (commonHeight > height)
					break;
				sizes.emplace_back(commonHeight * 4 / 3, commonHeight);
				if (commonHeight * width % height == 0)
					sizes.emplace_back(commonHeight * width / height, commonHeight);
			}
		}
	}

	const Size configuredSize = params.configuredSize;

	// Ensures that the ini specified resolution is present in resolution list even if it doesn't match a monitor resolution (for example if played in window mode)
	sizes.push_back(configuredSize);
	// Ensures that the platform's preferred default resolution is always present
	sizes.push_back(params.defaultSize);
	// Ensures that the vanilla Diablo resolution is present on systems that would support it
	if (params.supportsAnyResolution)
		sizes.emplace_back(640, 480);

	// On HiDPI displays, the output has the aspect ratio of the desktop but is always in landscape orientation,
	// like the preferred window size.
	const Size fitToScreenAspect = isHiDpi ? output : params.desktopSize;
	if (params.fitToScreen && fitToScreenAspect.height != 0) {
		for (auto &size : sizes) {
			// Ensure that the ini specified resolution remains present in the resolution list
			if (size.height == configuredSize.height)
				size.width = configuredSize.width;
			else
				size.width = size.height * fitToScreenAspect.width / fitToScreenAspect.height;
		}
	}

	if (isHiDpi) {
		// Larger resolutions than the output only make everything smaller and slower.
		// The configured resolution is kept so that it remains selected.
		std::erase_if(sizes, [&](const Size &s) {
			return s != configuredSize && (s.width > output.width || s.height > output.height);
		});
	}

	if (params.removeSmallResolutions) {
		// Only display compatible resolutions.
		std::erase_if(sizes, [](const Size &s) { return s.width < 640 || s.height < 480; });
	}

	// Sort by width then by height
	c_sort(sizes, [](const Size &x, const Size &y) -> bool {
		if (x.width == y.width)
			return x.height > y.height;
		return x.width > y.width;
	});
	// Remove duplicate entries
	sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());

	std::vector<std::pair<Size, std::string>> resolutions;
	resolutions.reserve(sizes.size());
	for (const Size &size : sizes) {
		std::string label = params.fitToScreen ? StrCat(size.height, "p") : StrCat(size.width, "x", size.height);
		if (isHiDpi) {
			const int scale = GetExactScale(size, output);
			if (scale != 0)
				StrAppend(label, " (x", scale, ")");
		}
		resolutions.emplace_back(size, std::move(label));
	}
	return resolutions;
}

} // namespace devilution
