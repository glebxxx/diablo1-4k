/**
 * @file gl1_hd_key.hpp
 *
 * Content keys for HD texture packs.
 *
 * An HD texture replaces one 1x sprite frame or dungeon tile frame. It is found by a key
 * computed from the frame's palette indices, so it does not depend on the asset path
 * (a `ClxSprite` does not know it) and recoloured (TRN) variants get their own keys.
 *
 * Key = SHA-256 over: width (u16 LE), height (u16 LE), then every pixel in row-major
 * order from the top-left corner as u16 LE: the palette index after TRN, or 0x0100 for a
 * transparent pixel. See docs/hd-pack-format.md.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/clx_sprite.hpp"
#include "levels/dun_tile.hpp"

namespace devilution {

/** @brief Pixel value of a transparent pixel in an indexed HD frame. */
constexpr uint16_t HdTransparentPixel = 0x0100;

/** @brief A frame decoded to palette indices (or `HdTransparentPixel`), top-down. */
struct HdIndexedFrame {
	uint16_t width = 0;
	uint16_t height = 0;
	std::vector<uint16_t> pixels;

	[[nodiscard]] bool IsOpaque(int x, int y) const
	{
		return pixels[static_cast<size_t>(y) * width + x] != HdTransparentPixel;
	}
};

using HdKey = std::array<uint8_t, 32>;

/** @brief Decodes a CLX sprite frame, applying `trn` (may be null) to every opaque pixel. */
HdIndexedFrame DecodeClxToIndexed(ClxSprite clx, const uint8_t *trn);

/**
 * @brief Decodes a dungeon tile frame (level CEL micro-tile) as the GL1 renderer draws it:
 * 32 pixels wide, `height` rows, pixels outside the tile's shape are transparent.
 */
HdIndexedFrame DecodeDunTileToIndexed(const uint8_t *frameData, TileType tileType, int height);

HdKey ComputeHdKey(const HdIndexedFrame &frame);

/** @brief 64 lowercase hex digits. */
std::string HdKeyToHex(const HdKey &key);
std::optional<HdKey> HdKeyFromHex(std::string_view hex);

/**
 * @brief Palette indices that colour cycling animates on the current level, as an inclusive
 * range. `first > last` means none.
 */
struct HdAnimatedRange {
	uint8_t first = 1;
	uint8_t last = 0;

	[[nodiscard]] bool Contains(uint16_t pixel) const
	{
		return pixel >= first && pixel <= last;
	}
};

/** @brief Whether any opaque pixel of the frame uses an animated palette index. */
bool UsesAnimatedIndices(const HdIndexedFrame &frame, HdAnimatedRange range);

} // namespace devilution
