/**
 * @file utils/upscalers/upscalers.hpp
 *
 * Pixel-art magnification filters for 32-bit pixels (e.g. RGB888 / XRGB8888).
 *
 * The filters only compare pixels for equality and (MMPX) by the brightness of their low 24 bits,
 * so they work with any 32-bit pixel format whose color channels occupy the low 24 bits.
 * Pixels outside the source image are treated as copies of the nearest edge pixel.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace devilution {
namespace upscalers {

/** @brief A read-only view of a 32-bit image. `pitch` is in pixels, not bytes. */
struct ConstImageView {
	const uint32_t *pixels;
	int width;
	int height;
	ptrdiff_t pitch;

	[[nodiscard]] const uint32_t *Row(int y) const
	{
		return pixels + y * pitch;
	}
};

/** @brief A writable view of a 32-bit image. `pitch` is in pixels, not bytes. */
struct ImageView {
	uint32_t *pixels;
	int width;
	int height;
	ptrdiff_t pitch;

	[[nodiscard]] uint32_t *Row(int y) const
	{
		return pixels + y * pitch;
	}
};

/**
 * @brief Scale2x (EPX / AdvMAME2x) magnification.
 *
 * @param dst Must be exactly 2x the size of `src`.
 */
void Scale2x(const ConstImageView &src, const ImageView &dst);

/**
 * @brief Scale3x (AdvMAME3x) magnification, the 3x variant of Scale2x / EPX.
 *
 * @param dst Must be exactly 3x the size of `src`.
 */
void Scale3x(const ConstImageView &src, const ImageView &dst);

/**
 * @brief MMPX style-preserving pixel-art magnification (McGuire & Gagiu, 2021). 2x only.
 *
 * @param dst Must be exactly 2x the size of `src`.
 */
void Mmpx2x(const ConstImageView &src, const ImageView &dst);

} // namespace upscalers
} // namespace devilution
