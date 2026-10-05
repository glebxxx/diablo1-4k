#include "engine/render/gl1/gl1_hd_dump.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_iostream.h>
#endif

#include "engine/surface.hpp"
#include "utils/file_util.h"
#include "utils/sdl_compat.h"
#include "utils/sdl_wrap.h"
#include "utils/str_cat.hpp"
#include "utils/surface_to_png.hpp"

namespace devilution {

HdDumper::HdDumper(std::string dir)
    : dir_(std::move(dir))
{
	while (!dir_.empty() && (dir_.back() == '/' || dir_.back() == '\\'))
		dir_.pop_back();
}

std::string HdDumper::DirFromEnvironment()
{
	const char *dir = std::getenv("DVX_HD_DUMP");
	return dir != nullptr ? std::string(dir) : std::string();
}

bool HdDumper::Dump(const HdKey &key, const HdIndexedFrame &frame, const std::array<SDL_Color, 256> &palette, const HdDumpInfo &info)
{
	if (!enabled() || frame.width == 0 || frame.height == 0)
		return false;
	const std::string hex = HdKeyToHex(key);
	if (!dumped_.insert(hex).second)
		return false;

	const std::string subdir = StrCat(dir_, DIRECTORY_SEPARATOR_STR "1x" DIRECTORY_SEPARATOR_STR, hex.substr(0, 2));
	const std::string path = StrCat(subdir, DIRECTORY_SEPARATOR_STR, hex, ".png");
	if (FileExists(path))
		return false;
	RecursivelyCreateDir(subdir.c_str());

	std::vector<uint8_t> rgba(frame.pixels.size() * 4, 0);
	for (size_t i = 0; i < frame.pixels.size(); ++i) {
		const uint16_t pixel = frame.pixels[i];
		if (pixel == HdTransparentPixel)
			continue;
		const SDL_Color &color = palette[pixel];
		rgba[i * 4 + 0] = color.r;
		rgba[i * 4 + 1] = color.g;
		rgba[i * 4 + 2] = color.b;
		rgba[i * 4 + 3] = 255;
	}
	const SDLSurfaceUniquePtr surface = SDLWrap::CreateRGBSurfaceWithFormatFrom(
	    rgba.data(), frame.width, frame.height, 32, frame.width * 4, SDL_PIXELFORMAT_RGBA32);
	if (!WriteSurfaceToFilePng(Surface(surface.get()), SDL_IOFromFile(path.c_str(), "wb")).has_value())
		return false;

	const std::string manifestPath = StrCat(dir_, DIRECTORY_SEPARATOR_STR "manifest.tsv");
	const bool newManifest = !FileExists(manifestPath);
	FILE *manifest = OpenFile(manifestPath.c_str(), "ab");
	if (manifest != nullptr) {
		if (newManifest) {
			constexpr std::string_view Header = "key\tkind\twidth\theight\ttile_type\ttrn\tlevel_type\n";
			std::fwrite(Header.data(), 1, Header.size(), manifest);
		}
		const std::string line = StrCat(hex, "\t", info.kind, "\t", frame.width, "\t", frame.height, "\t",
		    info.tileType, "\t", info.trn ? "trn" : "none", "\t", info.levelType, "\n");
		std::fwrite(line.data(), 1, line.size(), manifest);
		std::fclose(manifest);
	}
	return true;
}

} // namespace devilution
