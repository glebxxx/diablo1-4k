#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "control/control.hpp"
#include "engine/dx.h"
#include "engine/layered_present.hpp"
#include "engine/render/blit_impl.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/ui_layer.hpp"
#include "engine/surface.hpp"
#include "levels/dun_tile_data.hpp"
#include "lighting.h"
#include "utils/palette_blending.hpp"
#include "utils/ui_fwd.h"

using namespace devilution;

namespace {

constexpr int Width = 64;
constexpr int Height = 32;

/**
 * @brief A palette where blending black with some colours yields the key values 1 and 2.
 */
std::array<SDL_Color, 256> MakeKeyCollidingPalette()
{
	std::array<SDL_Color, 256> palette;
	for (unsigned i = 0; i < 256; ++i) {
		palette[i] = SDL_Color { static_cast<uint8_t>(i), static_cast<uint8_t>((i * 7) % 256), static_cast<uint8_t>(255 - i), 255 };
	}
	palette[0] = SDL_Color { 0, 0, 0, 255 };
	palette[1] = SDL_Color { 30, 60, 90, 255 };
	palette[2] = SDL_Color { 90, 30, 60, 255 };
	palette[100] = SDL_Color { 60, 120, 180, 255 }; // 50% black over 100 = 1
	palette[101] = SDL_Color { 180, 60, 120, 255 }; // 50% black over 101 = 2
	return palette;
}

class UiLayerTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		palette_ = MakeKeyCollidingPalette();
		GenerateBlendedLookupTable(palette_.data());
		RebuildUiKeyRemap(palette_.data());

		ui_ = std::make_unique<OwnedSurface>(Width, Height);
		half_.assign(static_cast<size_t>(ui_->pitch()) * Height, 0);
		const uint8_t *begin = static_cast<const uint8_t *>(ui_->surface->pixels);
		UiLayer.begin = begin;
		UiLayer.end = begin + half_.size();
		UiLayer.halfPlane = half_.data();
		UiLayer.presentLayered = true;
		FillUi(UiKeyTransparent);
	}

	void TearDown() override
	{
		UiLayer = {};
	}

	void FillUi(uint8_t color)
	{
		std::memset(ui_->surface->pixels, color, half_.size());
	}

	uint8_t &Pixel(int x, int y)
	{
		return *ui_->at(x, y);
	}

	uint8_t &Half(int x, int y)
	{
		return UiLayer.halfPlane[static_cast<size_t>(y) * ui_->pitch() + x];
	}

	std::array<SDL_Color, 256> palette_;
	std::unique_ptr<OwnedSurface> ui_;
	std::vector<uint8_t> half_;
};

TEST_F(UiLayerTest, KeyRemapAvoidsKeys)
{
	EXPECT_GE(UiKeyRemap[UiKeyTransparent], 32);
	EXPECT_GE(UiKeyRemap[UiKeyHalf], 32);
	for (unsigned i = 0; i < 256; ++i) {
		if (i != UiKeyTransparent && i != UiKeyHalf)
			EXPECT_EQ(UiKeyRemap[i], i);
	}
}

TEST_F(UiLayerTest, IsUiLayer)
{
	EXPECT_TRUE(IsUiLayer(*ui_));
	EXPECT_TRUE(IsUiLayer(ui_->subregion(10, 10, 5, 5)));
	const OwnedSurface other(Width, Height);
	EXPECT_FALSE(IsUiLayer(other));
	EXPECT_TRUE(IsKeyedUiLayer(*ui_));
	UiLayer.presentLayered = false;
	EXPECT_FALSE(IsKeyedUiLayer(*ui_));
}

TEST_F(UiLayerTest, BlackRectOverTransparent)
{
	DrawHalfTransparentRectTo(*ui_, 0, 0, Width, Height);
	for (int y = 0; y < Height; ++y) {
		for (int x = 0; x < Width; ++x) {
			ASSERT_EQ(Pixel(x, y), UiKeyHalf);
			ASSERT_EQ(Half(x, y), 0);
		}
	}
}

TEST_F(UiLayerTest, BlackRectOverOpaque)
{
	for (int y = 0; y < Height; ++y) {
		for (int x = 0; x < Width; ++x) {
			Pixel(x, y) = static_cast<uint8_t>(3 + ((x + y * Width) % 253));
		}
	}
	Pixel(5, 5) = 100;
	Pixel(6, 5) = 101;
	DrawHalfTransparentRectTo(*ui_, 0, 0, Width, Height);
	for (int y = 0; y < Height; ++y) {
		for (int x = 0; x < Width; ++x) {
			uint8_t d = static_cast<uint8_t>(3 + ((x + y * Width) % 253));
			if (x == 5 && y == 5) d = 100;
			if (x == 6 && y == 5) d = 101;
			ASSERT_EQ(Pixel(x, y), UiKeyRemap[paletteTransparencyLookup[0][d]]) << x << "," << y;
		}
	}
	ASSERT_EQ(paletteTransparencyLookup[0][100], UiKeyTransparent);
	ASSERT_EQ(paletteTransparencyLookup[0][101], UiKeyHalf);
	EXPECT_NE(Pixel(5, 5), UiKeyTransparent);
	EXPECT_NE(Pixel(6, 5), UiKeyHalf);
}

TEST_F(UiLayerTest, ColoredRectOverTransparentAndHalf)
{
	DrawHalfTransparentRectTo(*ui_, 0, 0, Width, Height, 77);
	EXPECT_EQ(Pixel(3, 3), UiKeyHalf);
	EXPECT_EQ(Half(3, 3), 77);
	DrawHalfTransparentRectTo(*ui_, 0, 0, Width, Height, 150);
	EXPECT_EQ(Pixel(3, 3), UiKeyHalf);
	EXPECT_EQ(Half(3, 3), UiKeyRemap[paletteTransparencyLookup[150][77]]);
}

TEST_F(UiLayerTest, HalfTransparentPixelAndLines)
{
	SetHalfTransparentPixel(*ui_, { 4, 4 }, 99);
	EXPECT_EQ(Pixel(4, 4), UiKeyHalf);
	EXPECT_EQ(Half(4, 4), 99);

	DrawHalfTransparentHorizontalLine(*ui_, { 0, 10 }, Width, 55);
	DrawHalfTransparentVerticalLine(*ui_, { 20, 0 }, Height, 66);
	EXPECT_EQ(Pixel(0, 10), UiKeyHalf);
	EXPECT_EQ(Half(0, 10), 55);
	EXPECT_EQ(Pixel(20, 0), UiKeyHalf);
	EXPECT_EQ(Half(20, 0), 66);
	// Crossing point: the second line blends with the first one in the half plane.
	EXPECT_EQ(Pixel(20, 10), UiKeyHalf);
	EXPECT_EQ(Half(20, 10), UiKeyRemap[paletteTransparencyLookup[66][55]]);
}

TEST_F(UiLayerTest, OpaqueWritesOverHalf)
{
	DrawHalfTransparentRectTo(*ui_, 0, 0, Width, Height);
	FillRect(*ui_, 2, 2, 4, 4, 200);
	EXPECT_EQ(Pixel(3, 3), 200);
	EXPECT_EQ(Pixel(1, 1), UiKeyHalf);
}

TEST_F(UiLayerTest, LegacyPathUnchangedWhenNotLayered)
{
	UiLayer.presentLayered = false;
	FillUi(100);
	DrawHalfTransparentRectTo(*ui_, 0, 0, Width, Height);
	// Without the layered renderer, blending is the plain palette lookup (here: colour 1).
	EXPECT_EQ(Pixel(7, 7), paletteTransparencyLookup[0][100]);
	EXPECT_EQ(Pixel(7, 7), UiKeyTransparent);
}

TEST_F(UiLayerTest, NoBlendOutputIsAKey)
{
	// Exhaustive: every source colour over every opaque destination colour and over every half colour.
	OwnedSurface big(256, 256);
	std::vector<uint8_t> half(static_cast<size_t>(big.pitch()) * 256);
	const uint8_t *begin = static_cast<const uint8_t *>(big.surface->pixels);
	UiLayer.begin = begin;
	UiLayer.end = begin + half.size();
	UiLayer.halfPlane = half.data();

	bool sawKeyCollision = false;
	for (int pass = 0; pass < 2; ++pass) {
		for (int s = 0; s < 256; ++s) {
			for (int d = 0; d < 256; ++d) {
				uint8_t *pix = big.at(d, s);
				uint8_t &h = half[static_cast<size_t>(s) * big.pitch() + d];
				if (pass == 0) {
					*pix = static_cast<uint8_t>(d);
					if (d == UiKeyTransparent || d == UiKeyHalf)
						*pix = 3; // keys are not opaque colours
				} else {
					*pix = UiKeyHalf;
					h = static_cast<uint8_t>(d);
				}
				const uint8_t raw = paletteTransparencyLookup[s][pass == 0 ? *pix : h];
				if (raw == UiKeyTransparent || raw == UiKeyHalf)
					sawKeyCollision = true;
			}
		}
		for (int s = 0; s < 256; ++s)
			DrawHalfTransparentHorizontalLine(big, { 0, s }, 256, static_cast<uint8_t>(s));
		for (int s = 0; s < 256; ++s) {
			for (int d = 0; d < 256; ++d) {
				const uint8_t c = *big.at(d, s);
				if (pass == 0) {
					ASSERT_NE(c, UiKeyTransparent) << "s=" << s << " d=" << d;
					ASSERT_NE(c, UiKeyHalf) << "s=" << s << " d=" << d;
				} else {
					ASSERT_EQ(c, UiKeyHalf);
					const uint8_t h = half[static_cast<size_t>(s) * big.pitch() + d];
					ASSERT_NE(h, UiKeyTransparent) << "s=" << s << " d=" << d;
					ASSERT_NE(h, UiKeyHalf) << "s=" << s << " d=" << d;
				}
			}
		}
	}
	EXPECT_TRUE(sawKeyCollision) << "the palette must produce key values by construction";
}

TEST_F(UiLayerTest, KeyedBlitFunctors)
{
	FillUi(UiKeyTransparent);
	uint8_t *row = ui_->at(0, 0);
	row[2] = 120;
	const uint8_t src[4] = { 10, 20, 30, 40 };
	BlitPixelsBlendedKeyed(row, src, 4);
	EXPECT_EQ(row[0], UiKeyHalf);
	EXPECT_EQ(Half(0, 0), 10);
	EXPECT_EQ(row[1], UiKeyHalf);
	EXPECT_EQ(Half(1, 0), 20);
	EXPECT_EQ(row[2], UiKeyRemap[paletteTransparencyLookup[30][120]]);
	EXPECT_EQ(row[3], UiKeyHalf);

	const std::array<uint8_t, 256> trn = [] {
		std::array<uint8_t, 256> t;
		for (unsigned i = 0; i < 256; ++i)
			t[i] = static_cast<uint8_t>(255 - i);
		return t;
	}();
	BlitPixelsBlendedWithMapKeyed(row, src, 1, trn.data());
	EXPECT_EQ(row[0], UiKeyHalf);
	EXPECT_EQ(Half(0, 0), UiKeyRemap[paletteTransparencyLookup[trn[10]][10]]);

	uint8_t *row2 = ui_->at(0, 1);
	BlitFillBlendedKeyed(row2, 3, 33);
	EXPECT_EQ(row2[0], UiKeyHalf);
	EXPECT_EQ(Half(0, 1), 33);
}

TEST_F(UiLayerTest, RedBackTintsBothLayers)
{
	std::array<uint8_t, 256> savedPause = PauseTable;
	for (unsigned i = 0; i < 256; ++i)
		PauseTable[i] = static_cast<uint8_t>(i < 128 ? 200 : 201);
	PauseTable[UiKeyTransparent] = 202;

	SDL_Surface *savedPalSurface = PalSurface;
	const uint16_t savedWidth = gnScreenWidth;
	const uint16_t savedHeight = gnScreenHeight;
	const dungeon_type savedLevelType = leveltype;
	gnScreenWidth = Width;
	gnScreenHeight = Height;
	PalSurface = ui_->surface;
	leveltype = DTYPE_CATHEDRAL;

	ResizeLayers({ 40, 20 }, { Width, Height });
	BeginLayeredFrame();
	ASSERT_TRUE(PresentLayered());
	const Surface world = WorldBuffer();
	std::memset(world.begin(), 150, static_cast<size_t>(world.pitch()) * world.h());

	Pixel(0, 0) = 50;                                      // opaque
	DrawHalfTransparentRectTo(*ui_, 1, 0, 1, 1, 60);       // half
	SetHalfTransparentPixel(*ui_, { 2, 0 }, UiKeyHalf);    // half whose colour is a key value
	RedBack(GlobalBackBuffer());

	EXPECT_EQ(Pixel(0, 0), 200);
	EXPECT_EQ(Pixel(1, 0), UiKeyHalf);
	EXPECT_EQ(Half(1, 0), 200);
	EXPECT_EQ(Pixel(3, 0), UiKeyTransparent);
	EXPECT_EQ(Pixel(Width - 1, Height - 1), UiKeyTransparent);
	for (int y = 0; y < world.h(); ++y) {
		for (int x = 0; x < world.w(); ++x) {
			ASSERT_EQ(*world.at(x, y), 201);
		}
	}

	ReleaseLayers();
	PalSurface = savedPalSurface;
	gnScreenWidth = savedWidth;
	gnScreenHeight = savedHeight;
	leveltype = savedLevelType;
	PauseTable = savedPause;
}

TEST_F(UiLayerTest, RedBackKeepsHellRule)
{
	std::array<uint8_t, 256> savedPause = PauseTable;
	for (unsigned i = 0; i < 256; ++i)
		PauseTable[i] = 210;

	SDL_Surface *savedPalSurface = PalSurface;
	const uint16_t savedWidth = gnScreenWidth;
	const uint16_t savedHeight = gnScreenHeight;
	const dungeon_type savedLevelType = leveltype;
	gnScreenWidth = Width;
	gnScreenHeight = Height;
	PalSurface = ui_->surface;
	leveltype = DTYPE_HELL;

	ResizeLayers({ 8, 8 }, { Width, Height });
	BeginLayeredFrame();
	const Surface world = WorldBuffer();
	std::memset(world.begin(), 20, static_cast<size_t>(world.pitch()) * world.h());
	*world.at(0, 0) = 40;
	Pixel(0, 0) = 10;
	Pixel(1, 0) = 40;
	RedBack(GlobalBackBuffer());

	EXPECT_EQ(*world.at(0, 0), 210);
	EXPECT_EQ(*world.at(1, 0), 20);
	EXPECT_EQ(Pixel(0, 0), 10);
	EXPECT_EQ(Pixel(1, 0), 210);

	ReleaseLayers();
	PalSurface = savedPalSurface;
	gnScreenWidth = savedWidth;
	gnScreenHeight = savedHeight;
	leveltype = savedLevelType;
	PauseTable = savedPause;
}

TEST_F(UiLayerTest, ExpandUiKeyed)
{
	uint32_t lut[256];
	BuildPaletteLut(palette_.data(), lut);
	Pixel(0, 0) = UiKeyTransparent;
	Pixel(1, 0) = UiKeyHalf;
	Half(1, 0) = 77;
	Pixel(2, 0) = 200;
	Pixel(3, 0) = 0;
	std::vector<uint32_t> out(4);
	ExpandUiKeyed(ui_->at(0, 0), &Half(0, 0), ui_->pitch(), out.data(), 4 * sizeof(uint32_t), 4, 1, lut);
	auto rgb = [&](uint8_t c) {
		return (static_cast<uint32_t>(palette_[c].r) << 16) | (static_cast<uint32_t>(palette_[c].g) << 8) | palette_[c].b;
	};
	EXPECT_EQ(out[0], 0x00000000U);
	EXPECT_EQ(out[1], 0x80000000U | rgb(77));
	EXPECT_EQ(out[2], 0xFF000000U | rgb(200));
	EXPECT_EQ(out[3], 0xFF000000U);
	for (const uint32_t pixel : out) {
		const uint32_t alpha = pixel >> 24;
		EXPECT_TRUE(alpha == 0 || alpha == 0x80 || alpha == 0xFF);
	}
}

TEST_F(UiLayerTest, ExpandIndexedIsOpaque)
{
	uint32_t lut[256];
	BuildPaletteLut(palette_.data(), lut);
	const uint8_t src[4] = { 0, 1, 2, 255 };
	std::vector<uint32_t> out(4);
	ExpandIndexed(src, 4, out.data(), 16, 4, 1, lut);
	for (int i = 0; i < 4; ++i) {
		EXPECT_EQ(out[i], lut[src[i]] | 0xFF000000U);
	}
}

} // namespace
