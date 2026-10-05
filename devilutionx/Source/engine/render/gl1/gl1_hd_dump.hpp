/**
 * @file gl1_hd_dump.hpp
 *
 * Dump mode for building HD packs: with `DVX_HD_DUMP=<dir>` set, every sprite and tile frame
 * the GL1 renderer uploads is written once as `<dir>/1x/<kk>/<key>.png`, and a line is added
 * to `<dir>/manifest.tsv`. These are exactly the frames, and keys, the HD loader looks up.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

#ifdef USE_SDL3
#include <SDL3/SDL_pixels.h>
#else
#include <SDL.h>
#endif

#include "engine/render/gl1/gl1_hd_key.hpp"

namespace devilution {

/** @brief What a dumped frame is, for `manifest.tsv`. */
struct HdDumpInfo {
	/** "sprite" or "tile". */
	std::string_view kind;
	/** Tile type number (`TileType`), or -1 for sprites. */
	int tileType = -1;
	/** Whether a TRN (colour translation) was applied. */
	bool trn = false;
	/** `dungeon_type` of the current level. */
	int levelType = 0;
};

class HdDumper {
public:
	/** @brief Dumps into `dir`; an empty `dir` disables dumping. */
	explicit HdDumper(std::string dir);

	/** @brief Directory from the `DVX_HD_DUMP` environment variable (empty if unset). */
	static std::string DirFromEnvironment();

	[[nodiscard]] bool enabled() const { return !dir_.empty(); }

	/**
	 * @brief Writes the frame (once per key) as straight-alpha RGBA PNG, colours from `palette`.
	 * @return true if a new file was written.
	 */
	bool Dump(const HdKey &key, const HdIndexedFrame &frame, const std::array<SDL_Color, 256> &palette, const HdDumpInfo &info);

private:
	std::string dir_;
	std::unordered_set<std::string> dumped_;
};

} // namespace devilution
