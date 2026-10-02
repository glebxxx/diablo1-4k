#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "utils/upscalers/upscalers.hpp"

namespace devilution {
namespace upscalers {
namespace {

constexpr uint32_t Black = 0xFF000000;
constexpr uint32_t White = 0xFFFFFFFF;
constexpr uint32_t Red = 0xFFC02020;
constexpr uint32_t Gold = 0xFFD0B040;

struct Image {
	int width;
	int height;
	ptrdiff_t pitch;
	std::vector<uint32_t> pixels;

	Image(int width, int height, ptrdiff_t padding = 0)
	    : width(width)
	    , height(height)
	    , pitch(width + padding)
	    , pixels(static_cast<size_t>(pitch * height), 0xDEADBEEF)
	{
	}

	uint32_t &At(int x, int y)
	{
		return pixels[static_cast<size_t>(y * pitch + x)];
	}

	[[nodiscard]] uint32_t At(int x, int y) const
	{
		return pixels[static_cast<size_t>(y * pitch + x)];
	}

	[[nodiscard]] ConstImageView ConstView() const
	{
		return { pixels.data(), width, height, pitch };
	}

	[[nodiscard]] ImageView View()
	{
		return { pixels.data(), width, height, pitch };
	}
};

/** @brief FNV-1a over the visible pixels (row padding excluded). */
uint64_t Hash(const Image &image)
{
	uint64_t hash = 0xCBF29CE484222325ULL;
	for (int y = 0; y < image.height; ++y) {
		for (int x = 0; x < image.width; ++x) {
			uint32_t pixel = image.At(x, y);
			for (int i = 0; i < 4; ++i) {
				hash ^= pixel & 0xFF;
				hash *= 0x100000001B3ULL;
				pixel >>= 8;
			}
		}
	}
	return hash;
}

/** @brief A deterministic image with a few colors in blocks, so that edges and flat areas both occur. */
Image MakeBlocks(int width, int height, ptrdiff_t padding = 0)
{
	const uint32_t palette[] = { Black, White, Red, Gold };
	Image image(width, height, padding);
	uint32_t state = 12345;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			state = state * 1103515245 + 12345;
			// Mostly copy the left or upper neighbour to get runs and blobs.
			const uint32_t choice = (state >> 16) % 8;
			if (choice < 3 && x > 0) {
				image.At(x, y) = image.At(x - 1, y);
			} else if (choice < 6 && y > 0) {
				image.At(x, y) = image.At(x, y - 1);
			} else {
				image.At(x, y) = palette[(state >> 24) % 4];
			}
		}
	}
	return image;
}

/** @brief An 8x8 image with a one-pixel diagonal line, a 2:1 slope and an isolated dot. */
Image MakeLines8()
{
	Image image(8, 8);
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			image.At(x, y) = Black;
		}
	}
	for (int i = 0; i < 6; ++i) {
		image.At(i, i) = White;
	}
	for (int i = 0; i < 4; ++i) {
		image.At(2 * i + 1, 7 - i) = Red;
		image.At(2 * i, 7 - i) = Red;
	}
	image.At(6, 1) = Gold;
	return image;
}

Image Apply(void (*filter)(const ConstImageView &, const ImageView &), int factor, const Image &src, ptrdiff_t dstPadding = 0)
{
	Image dst(src.width * factor, src.height * factor, dstPadding);
	filter(src.ConstView(), dst.View());
	return dst;
}

/** @brief Clamped access, as the filters see pixels beyond the edge. */
uint32_t Clamped(const Image &image, int x, int y)
{
	x = x < 0 ? 0 : (x >= image.width ? image.width - 1 : x);
	y = y < 0 ? 0 : (y >= image.height ? image.height - 1 : y);
	return image.At(x, y);
}

/** @brief Scale2x as written in its original formulation, as an independent reference. */
Image ReferenceScale2x(const Image &src)
{
	Image dst(src.width * 2, src.height * 2);
	for (int y = 0; y < src.height; ++y) {
		for (int x = 0; x < src.width; ++x) {
			const uint32_t b = Clamped(src, x, y - 1);
			const uint32_t d = Clamped(src, x - 1, y);
			const uint32_t e = Clamped(src, x, y);
			const uint32_t f = Clamped(src, x + 1, y);
			const uint32_t h = Clamped(src, x, y + 1);
			dst.At(2 * x, 2 * y) = (d == b && b != f && d != h) ? d : e;
			dst.At(2 * x + 1, 2 * y) = (b == f && b != d && f != h) ? f : e;
			dst.At(2 * x, 2 * y + 1) = (d == h && d != b && h != f) ? d : e;
			dst.At(2 * x + 1, 2 * y + 1) = (h == f && d != h && b != f) ? f : e;
		}
	}
	return dst;
}

void ExpectSamePixels(const Image &a, const Image &b)
{
	ASSERT_EQ(a.width, b.width);
	ASSERT_EQ(a.height, b.height);
	for (int y = 0; y < a.height; ++y) {
		for (int x = 0; x < a.width; ++x) {
			ASSERT_EQ(a.At(x, y), b.At(x, y)) << "at " << x << "," << y;
		}
	}
}

TEST(UpscalersTest, UniformImageStaysUniform)
{
	Image src(5, 3);
	for (uint32_t &pixel : src.pixels)
		pixel = Red;
	for (const Image &dst : { Apply(Scale2x, 2, src), Apply(Scale3x, 3, src), Apply(Mmpx2x, 2, src) }) {
		for (int y = 0; y < dst.height; ++y) {
			for (int x = 0; x < dst.width; ++x) {
				ASSERT_EQ(dst.At(x, y), Red);
			}
		}
	}
}

TEST(UpscalersTest, SinglePixelImage)
{
	Image src(1, 1);
	src.At(0, 0) = Gold;
	EXPECT_EQ(Apply(Scale2x, 2, src).pixels, std::vector<uint32_t>(4, Gold));
	EXPECT_EQ(Apply(Scale3x, 3, src).pixels, std::vector<uint32_t>(9, Gold));
	EXPECT_EQ(Apply(Mmpx2x, 2, src).pixels, std::vector<uint32_t>(4, Gold));
}

TEST(UpscalersTest, Scale2xRoundsCorner)
{
	// The black corner pixel next to two white edges gets its outer quarter filled in.
	//   W W
	//   W B
	Image src(2, 2);
	src.At(0, 0) = White;
	src.At(1, 0) = White;
	src.At(0, 1) = White;
	src.At(1, 1) = Black;
	const Image dst = Apply(Scale2x, 2, src);
	EXPECT_EQ(dst.At(2, 2), White);
	EXPECT_EQ(dst.At(3, 2), Black);
	EXPECT_EQ(dst.At(2, 3), Black);
	EXPECT_EQ(dst.At(3, 3), Black);
}

TEST(UpscalersTest, Scale2xMatchesReference)
{
	for (const Image &src : { MakeLines8(), MakeBlocks(8, 8), MakeBlocks(64, 64), MakeBlocks(37, 11) }) {
		ExpectSamePixels(Apply(Scale2x, 2, src), ReferenceScale2x(src));
	}
}

TEST(UpscalersTest, Scale3xCenterIsSource)
{
	const Image src = MakeBlocks(64, 64);
	const Image dst = Apply(Scale3x, 3, src);
	for (int y = 0; y < src.height; ++y) {
		for (int x = 0; x < src.width; ++x) {
			ASSERT_EQ(dst.At(3 * x + 1, 3 * y + 1), src.At(x, y));
		}
	}
}

TEST(UpscalersTest, PitchIsRespected)
{
	const Image packed = MakeBlocks(64, 64);
	Image padded(64, 64, /*padding=*/7);
	for (int y = 0; y < 64; ++y) {
		for (int x = 0; x < 64; ++x) {
			padded.At(x, y) = packed.At(x, y);
		}
	}
	ExpectSamePixels(Apply(Scale2x, 2, packed), Apply(Scale2x, 2, padded, /*dstPadding=*/5));
	ExpectSamePixels(Apply(Scale3x, 3, packed), Apply(Scale3x, 3, padded, /*dstPadding=*/5));
	ExpectSamePixels(Apply(Mmpx2x, 2, packed), Apply(Mmpx2x, 2, padded, /*dstPadding=*/5));

	// Padding in the destination is not written to.
	const Image dst = Apply(Mmpx2x, 2, padded, /*dstPadding=*/5);
	for (int y = 0; y < dst.height; ++y) {
		for (ptrdiff_t x = dst.width; x < dst.pitch; ++x) {
			ASSERT_EQ(dst.pixels[static_cast<size_t>(y * dst.pitch + x)], 0xDEADBEEF);
		}
	}
}

// Golden hashes. MMPX was checked against the MIT reference implementation on random images.
TEST(UpscalersTest, GoldenHashes8x8)
{
	const Image lines = MakeLines8();
	EXPECT_EQ(Hash(lines), 0x0650E820A9E2F6ACULL);
	EXPECT_EQ(Hash(Apply(Scale2x, 2, lines)), 0x1D9E8CDD89A97D34ULL);
	EXPECT_EQ(Hash(Apply(Scale3x, 3, lines)), 0xFDC03B106CED6E64ULL);
	EXPECT_EQ(Hash(Apply(Mmpx2x, 2, lines)), 0x9E101FE318B06B34ULL);
}

TEST(UpscalersTest, GoldenHashes64x64)
{
	const Image blocks = MakeBlocks(64, 64);
	EXPECT_EQ(Hash(blocks), 0xC6778B6D633F2A05ULL);
	EXPECT_EQ(Hash(Apply(Scale2x, 2, blocks)), 0xA7DF9D3D9695A88DULL);
	EXPECT_EQ(Hash(Apply(Scale3x, 3, blocks)), 0x8ECB88A878CD5FB5ULL);
	EXPECT_EQ(Hash(Apply(Mmpx2x, 2, blocks)), 0xEC31B8B7D7D76FECULL);
}

} // namespace
} // namespace upscalers
} // namespace devilution
