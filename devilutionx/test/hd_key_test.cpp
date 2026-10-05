#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/render/gl1/gl1_hd_json.hpp"
#include "engine/render/gl1/gl1_hd_key.hpp"
#include "utils/clx_encode.hpp"
#include "utils/paths.h"

namespace devilution {
namespace {

constexpr uint16_t T = HdTransparentPixel;

HdIndexedFrame MakeFrame(uint16_t width, uint16_t height, std::vector<uint16_t> pixels)
{
	HdIndexedFrame frame;
	frame.width = width;
	frame.height = height;
	frame.pixels = std::move(pixels);
	return frame;
}

/** @brief Encodes a top-down indexed image (with `T` for transparent) as one CLX frame. */
std::vector<uint8_t> EncodeClx(const HdIndexedFrame &frame)
{
	std::vector<uint8_t> out(ClxFrameHeaderSize);
	out[0] = ClxFrameHeaderSize;
	out[2] = static_cast<uint8_t>(frame.width);
	out[3] = static_cast<uint8_t>(frame.width >> 8);
	out[4] = static_cast<uint8_t>(frame.height);
	out[5] = static_cast<uint8_t>(frame.height >> 8);
	for (int y = frame.height - 1; y >= 0; --y) {
		int x = 0;
		while (x < frame.width) {
			const int start = x;
			if (!frame.IsOpaque(x, y)) {
				while (x < frame.width && !frame.IsOpaque(x, y))
					++x;
				AppendClxTransparentRun(x - start, out);
				continue;
			}
			std::vector<uint8_t> run;
			while (x < frame.width && frame.IsOpaque(x, y))
				run.push_back(static_cast<uint8_t>(frame.pixels[static_cast<size_t>(y) * frame.width + x++]));
			AppendClxPixelsOrFillRun(run.data(), run.size(), out);
		}
	}
	return out;
}

TEST(HdKeyTest, MatchesKeyVectors)
{
	std::ifstream file(paths::BasePath() + "test/fixtures/hd_pack/key_vectors.json", std::ios::binary);
	ASSERT_TRUE(file.is_open());
	const std::string text { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	std::string error;
	const std::optional<HdJsonValue> vectors = ParseHdJson(text, &error);
	ASSERT_TRUE(vectors.has_value()) << error;
	ASSERT_TRUE(vectors->IsArray());
	ASSERT_GE(vectors->AsArray().size(), 5U);
	for (const HdJsonValue &vector : vectors->AsArray()) {
		HdIndexedFrame frame;
		frame.width = static_cast<uint16_t>(*vector.GetInt("width", 1, 65535));
		frame.height = static_cast<uint16_t>(*vector.GetInt("height", 1, 65535));
		for (const HdJsonValue &pixel : vector.Find("pixels")->AsArray())
			frame.pixels.push_back(static_cast<uint16_t>(pixel.AsNumber()));
		EXPECT_EQ(HdKeyToHex(ComputeHdKey(frame)), *vector.GetString("sha256")) << *vector.GetString("name");
	}
}

TEST(HdKeyTest, IsDeterministicAndContentSensitive)
{
	const HdIndexedFrame base = MakeFrame(3, 2, { T, 200, T, 17, 128, 31 });
	const HdKey key = ComputeHdKey(base);
	EXPECT_EQ(ComputeHdKey(base), key);

	HdIndexedFrame changedPixel = base;
	changedPixel.pixels[4] = 129;
	EXPECT_NE(ComputeHdKey(changedPixel), key);

	HdIndexedFrame transparentVsZero = base;
	transparentVsZero.pixels[0] = 0;
	EXPECT_NE(ComputeHdKey(transparentVsZero), key);

	const HdIndexedFrame transposedSize = MakeFrame(2, 3, base.pixels);
	EXPECT_NE(ComputeHdKey(transposedSize), key);
}

TEST(HdKeyTest, HexRoundTrip)
{
	const HdKey key = ComputeHdKey(MakeFrame(1, 1, { 42 }));
	const std::string hex = HdKeyToHex(key);
	ASSERT_EQ(hex.size(), 64U);
	EXPECT_EQ(HdKeyFromHex(hex), key);

	std::string upper = hex;
	for (char &c : upper)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	EXPECT_EQ(HdKeyFromHex(upper), key);

	EXPECT_FALSE(HdKeyFromHex(hex.substr(1)).has_value());
	EXPECT_FALSE(HdKeyFromHex(hex.substr(0, 63) + "g").has_value());
}

TEST(HdKeyTest, DecodesClxSprite)
{
	const HdIndexedFrame image = MakeFrame(5, 3, {
	                                                 T, 10, 10, 10, T, // top row
	                                                 1, 2, 3, 4, 5,    //
	                                                 T, T, 250, T, T,  // bottom row
	                                             });
	const std::vector<uint8_t> clxData = EncodeClx(image);
	const ClxSprite clx { clxData.data(), static_cast<uint32_t>(clxData.size()) };

	const HdIndexedFrame decoded = DecodeClxToIndexed(clx, nullptr);
	EXPECT_EQ(decoded.width, 5);
	EXPECT_EQ(decoded.height, 3);
	EXPECT_EQ(decoded.pixels, image.pixels);
	EXPECT_EQ(ComputeHdKey(decoded), ComputeHdKey(image));
}

TEST(HdKeyTest, TrnIsAppliedBeforeHashing)
{
	const HdIndexedFrame image = MakeFrame(2, 2, { 1, T, 2, 3 });
	const std::vector<uint8_t> clxData = EncodeClx(image);
	const ClxSprite clx { clxData.data(), static_cast<uint32_t>(clxData.size()) };

	std::array<uint8_t, 256> identity;
	for (size_t i = 0; i < identity.size(); ++i)
		identity[i] = static_cast<uint8_t>(i);
	EXPECT_EQ(ComputeHdKey(DecodeClxToIndexed(clx, identity.data())), ComputeHdKey(image));

	std::array<uint8_t, 256> recolour = identity;
	recolour[2] = 200;
	const HdIndexedFrame recoloured = DecodeClxToIndexed(clx, recolour.data());
	EXPECT_EQ(recoloured.pixels, (std::vector<uint16_t> { 1, T, 200, 3 }));
	EXPECT_NE(ComputeHdKey(recoloured), ComputeHdKey(image));
}

TEST(HdKeyTest, DecodesSquareTile)
{
	std::vector<uint8_t> data(32 * 32);
	for (size_t i = 0; i < data.size(); ++i)
		data[i] = static_cast<uint8_t>(i / 32); // stored bottom row first
	const HdIndexedFrame tile = DecodeDunTileToIndexed(data.data(), TileType::Square, 32);
	ASSERT_EQ(tile.width, 32);
	ASSERT_EQ(tile.height, 32);
	EXPECT_EQ(tile.pixels[0], 31);          // top-left: last stored row
	EXPECT_EQ(tile.pixels[31 * 32 + 5], 0); // bottom row: first stored row
}

TEST(HdKeyTest, DecodesLeftTriangleShape)
{
	// 16 lower rows of width 2,4,...,32, then 15 upper rows of width 30,...,2.
	std::vector<uint8_t> data;
	for (int i = 0; i < 16; ++i)
		data.insert(data.end(), 2 * (i + 1), 7);
	for (int i = 0; i < 15; ++i)
		data.insert(data.end(), 32 - 2 * (i + 1), 9);
	const HdIndexedFrame tile = DecodeDunTileToIndexed(data.data(), TileType::LeftTriangle, 31);
	ASSERT_EQ(tile.height, 31);

	// Bottom row: only the two rightmost pixels.
	EXPECT_FALSE(tile.IsOpaque(29, 30));
	EXPECT_EQ(tile.pixels[30 * 32 + 30], 7);
	EXPECT_EQ(tile.pixels[30 * 32 + 31], 7);
	// Middle row (the widest): all 32 pixels.
	EXPECT_EQ(tile.pixels[15 * 32 + 0], 7);
	// Top row: two pixels starting at x = 30.
	EXPECT_FALSE(tile.IsOpaque(29, 0));
	EXPECT_EQ(tile.pixels[30], 9);

	size_t opaque = 0;
	for (const uint16_t pixel : tile.pixels)
		opaque += pixel != T ? 1 : 0;
	EXPECT_EQ(opaque, data.size());
}

TEST(HdKeyTest, DecodesTransparentSquareRuns)
{
	// Every row: 10 transparent, 4 pixels, 18 transparent.
	std::vector<uint8_t> data;
	for (int row = 0; row < 32; ++row) {
		data.push_back(static_cast<uint8_t>(-10));
		data.push_back(4);
		data.insert(data.end(), { 1, 2, 3, 4 });
		data.push_back(static_cast<uint8_t>(-18));
	}
	const HdIndexedFrame tile = DecodeDunTileToIndexed(data.data(), TileType::TransparentSquare, 32);
	for (int y = 0; y < 32; ++y) {
		EXPECT_FALSE(tile.IsOpaque(9, y));
		EXPECT_EQ(tile.pixels[static_cast<size_t>(y) * 32 + 10], 1);
		EXPECT_EQ(tile.pixels[static_cast<size_t>(y) * 32 + 13], 4);
		EXPECT_FALSE(tile.IsOpaque(14, y));
	}
}

TEST(HdKeyTest, SpriteAndTileWithSamePixelsShareTheKey)
{
	// Keys only depend on the decoded pixels; a tile and a sprite never have the same
	// pixels in practice, and packs built from dumps carry the right key either way.
	std::vector<uint8_t> data(32 * 32, 5);
	const HdIndexedFrame tile = DecodeDunTileToIndexed(data.data(), TileType::Square, 32);
	const std::vector<uint8_t> clxData = EncodeClx(tile);
	const ClxSprite clx { clxData.data(), static_cast<uint32_t>(clxData.size()) };
	EXPECT_EQ(ComputeHdKey(DecodeClxToIndexed(clx, nullptr)), ComputeHdKey(tile));
}

TEST(HdKeyTest, AnimatedIndices)
{
	const HdIndexedFrame frame = MakeFrame(3, 1, { T, 40, 12 });
	EXPECT_TRUE(UsesAnimatedIndices(frame, { 1, 31 }));
	EXPECT_FALSE(UsesAnimatedIndices(frame, { 1, 8 }));
	EXPECT_FALSE(UsesAnimatedIndices(frame, {}));                          // town: nothing cycles
	EXPECT_FALSE(UsesAnimatedIndices(MakeFrame(1, 1, { T }), { 0, 255 })); // transparent never counts
}

} // namespace
} // namespace devilution
