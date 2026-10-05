/**
 * Acceptance test M1-T3 (issue #4): the keyed UI layer of the layered renderer.
 */
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "control/control.hpp"
#include "engine/dx.h"
#include "engine/layered_present.hpp"
#include "engine/palette.h"
#include "engine/render/blit_impl.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/ui_layer.hpp"
#include "engine/render/world_view.hpp"
#include "engine/surface.hpp"
#include "levels/dun_tile_data.hpp"
#include "lighting.h"
#include "utils/palette_blending.hpp"
#include "utils/sdl_wrap.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {

TEST(UiLayerExpandTest, ExpandIndexedIsOpaque)
{
	std::array<uint32_t, 256> lut;
	for (unsigned i = 0; i < lut.size(); ++i)
		lut[i] = (i << 16) | ((255 - i) << 8) | (i ^ 0x5A);

	// 3x2 pixels with padding in both pitches.
	const std::array<uint8_t, 8> src = { 0, 1, 2, 99, /* row 2 */ 200, 255, 7, 99 };
	std::array<uint32_t, 10> dst {};
	ExpandIndexed(src.data(), 4, dst.data(), static_cast<int>(5 * sizeof(uint32_t)), 3, 2, lut.data());
	EXPECT_EQ(dst[0], lut[0] | 0xFF000000);
	EXPECT_EQ(dst[1], lut[1] | 0xFF000000);
	EXPECT_EQ(dst[2], lut[2] | 0xFF000000);
	EXPECT_EQ(dst[3], 0U);
	EXPECT_EQ(dst[5], lut[200] | 0xFF000000);
	EXPECT_EQ(dst[6], lut[255] | 0xFF000000);
	EXPECT_EQ(dst[7], lut[7] | 0xFF000000);
	EXPECT_EQ(dst[8], 0U);
}

TEST(UiLayerExpandTest, ExpandUiKeyedAlpha)
{
	std::array<uint32_t, 256> lut;
	for (unsigned i = 0; i < lut.size(); ++i)
		lut[i] = (i << 16) | ((255 - i) << 8) | (i ^ 0x5A);

	const std::array<uint8_t, 10> src = { UiKeyTransparent, UiKeyHalf, 0, 128, 255, /* row 2 */ UiKeyHalf, UiKeyTransparent, 3, 77, 77 };
	const std::array<uint8_t, 10> half = { 9, 77, 5, 5, 5, /* row 2 */ 0, 33, 1, 2, 2 };
	std::array<uint32_t, 8> dst {};
	ExpandUiKeyed(src.data(), half.data(), 5, dst.data(), static_cast<int>(4 * sizeof(uint32_t)), 3, 2, lut.data());
	EXPECT_EQ(dst[0], 0U);
	EXPECT_EQ(dst[1], (lut[77] & 0xFFFFFF) | 0x80000000);
	EXPECT_EQ(dst[2], lut[0] | 0xFF000000);
	EXPECT_EQ(dst[3], 0U);
	EXPECT_EQ(dst[4], (lut[0] & 0xFFFFFF) | 0x80000000);
	EXPECT_EQ(dst[5], 0U);
	EXPECT_EQ(dst[6], lut[3] | 0xFF000000);
	EXPECT_EQ(dst[7], 0U);
	for (const uint32_t pixel : dst) {
		const uint32_t alpha = pixel >> 24;
		EXPECT_TRUE(alpha == 0 || alpha == 0x80 || alpha == 0xFF);
	}
}

#ifdef DVL_LAYERED_RENDERER

constexpr Size OutputSize { 512, 512 };
constexpr Size UiSize { 256, 256 };
constexpr int WorldZoom = 2;
constexpr int MainPanelHeight = 128;

/**
 * @brief A palette in which the blend of black with colour 3 is exactly colour 1 and the blend of
 * black with colour 4 is exactly colour 2, so that unmapped blending would produce the keys.
 */
std::array<SDL_Color, 256> MakeKeyTrapPalette()
{
	std::array<SDL_Color, 256> palette;
	for (unsigned i = 0; i < palette.size(); ++i) {
		palette[i].r = static_cast<uint8_t>(60 + ((i * 37) % 196));
		palette[i].g = static_cast<uint8_t>(60 + ((i * 91) % 196));
		palette[i].b = static_cast<uint8_t>(60 + ((i * 53) % 196));
		palette[i].a = SDL_ALPHA_OPAQUE;
	}
	palette[0] = { 0, 0, 0, SDL_ALPHA_OPAQUE };
	palette[UiKeyTransparent] = { 10, 10, 10, SDL_ALPHA_OPAQUE };
	palette[UiKeyHalf] = { 20, 20, 20, SDL_ALPHA_OPAQUE };
	palette[3] = { 21, 21, 21, SDL_ALPHA_OPAQUE };
	palette[4] = { 41, 41, 41, SDL_ALPHA_OPAQUE };
	return palette;
}

bool IsKey(uint8_t color)
{
	return color == UiKeyTransparent || color == UiKeyHalf;
}

class UiLayerTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		savedPalSurface_ = PalSurface;
		savedScreenWidth_ = gnScreenWidth;
		savedScreenHeight_ = gnScreenHeight;
		savedViewportHeight_ = gnViewportHeight;
		savedLogicalPalette_ = logical_palette;
		savedSystemPalette_ = system_palette;
		savedLevelType_ = leveltype;
		savedPauseTable_ = PauseTable;

		gnScreenWidth = UiSize.width;
		gnScreenHeight = UiSize.height;
		gnViewportHeight = UiSize.height;
		uiSurface_ = SDLWrap::CreateRGBSurfaceWithFormat(0, UiSize.width, UiSize.height, 8, SDL_PIXELFORMAT_INDEX8);
		PalSurface = uiSurface_.get();

		logical_palette = MakeKeyTrapPalette();
		system_palette = logical_palette;
		GenerateBlendedLookupTable(logical_palette.data());
		RebuildUiKeyRemap();

		SetWorldViewForTesting(ComputeWorldView(OutputSize, UiSize, ComputeUiPlacement(OutputSize, UiSize, true), WorldZoom, MainPanelHeight));
		UpdateLayeredBuffers();
		BeginLayeredFrame();
		ASSERT_TRUE(PresentLayered());
	}

	void TearDown() override
	{
		EndLayeredMode();
		SetWorldViewForTesting(ComputeLegacyWorldView(UiSize, MainPanelHeight));
		UpdateLayeredBuffers();
		PalSurface = savedPalSurface_;
		gnScreenWidth = savedScreenWidth_;
		gnScreenHeight = savedScreenHeight_;
		gnViewportHeight = savedViewportHeight_;
		logical_palette = savedLogicalPalette_;
		system_palette = savedSystemPalette_;
		leveltype = savedLevelType_;
		PauseTable = savedPauseTable_;
	}

	static uint8_t &Half(const Surface &out, int x, int y)
	{
		return *GetUiKeyedTarget().HalfAt(out.at(x, y));
	}

	static uint8_t Remap(uint8_t color)
	{
		return UiLayer.keyRemap[color];
	}

private:
	SDLSurfaceUniquePtr uiSurface_;
	SDL_Surface *savedPalSurface_ = nullptr;
	uint16_t savedScreenWidth_ = 0;
	uint16_t savedScreenHeight_ = 0;
	uint16_t savedViewportHeight_ = 0;
	std::array<SDL_Color, 256> savedLogicalPalette_;
	std::array<SDL_Color, 256> savedSystemPalette_;
	dungeon_type savedLevelType_ = DTYPE_TOWN;
	std::array<uint8_t, 256> savedPauseTable_;
};

TEST_F(UiLayerTest, PaletteProducesKeysWithoutRemap)
{
	// Preconditions of the exhaustive test: the plain blend table does produce the keys.
	ASSERT_EQ(paletteTransparencyLookup[0][3], UiKeyTransparent);
	ASSERT_EQ(paletteTransparencyLookup[0][4], UiKeyHalf);

	for (unsigned i = 0; i < 256; ++i) {
		if (IsKey(static_cast<uint8_t>(i))) {
			EXPECT_GE(UiLayer.keyRemap[i], 32);
		} else {
			EXPECT_EQ(UiLayer.keyRemap[i], i);
		}
	}
}

TEST_F(UiLayerTest, FrameStartsTransparent)
{
	const Surface ui = GlobalBackBuffer();
	EXPECT_TRUE(IsKeyedUiLayer(ui));
	EXPECT_TRUE(IsKeyedUiLayer(ui.subregion(10, 20, 30, 40)));
	EXPECT_FALSE(IsKeyedUiLayer(WorldBuffer()));
	EXPECT_EQ(WorldBuffer().w(), UiSize.width);
	EXPECT_EQ(WorldBuffer().h(), UiSize.height);
	for (int y = 0; y < ui.h(); ++y) {
		for (int x = 0; x < ui.w(); ++x) {
			ASSERT_EQ(*ui.at(x, y), UiKeyTransparent);
		}
	}
}

TEST_F(UiLayerTest, BlackHalfRectOverTransparent)
{
	const Surface ui = GlobalBackBuffer();
	// Unaligned position and width: on the UI layer the aligned fast path must not be used.
	DrawHalfTransparentRectTo(ui, 3, 5, 101, 7);
	for (int y = 0; y < ui.h(); ++y) {
		for (int x = 0; x < ui.w(); ++x) {
			const bool inside = x >= 3 && x < 104 && y >= 5 && y < 12;
			if (inside) {
				ASSERT_EQ(*ui.at(x, y), UiKeyHalf);
				ASSERT_EQ(Half(ui, x, y), 0);
			} else {
				ASSERT_EQ(*ui.at(x, y), UiKeyTransparent);
			}
		}
	}
}

TEST_F(UiLayerTest, BlackHalfRectOverOpaque)
{
	const Surface ui = GlobalBackBuffer();
	for (int x = 0; x < 256; ++x) {
		*ui.at(x, 0) = IsKey(static_cast<uint8_t>(x)) ? 0 : static_cast<uint8_t>(x);
	}
	DrawHalfTransparentRectTo(ui, 0, 0, 256, 1);
	for (int x = 0; x < 256; ++x) {
		const uint8_t d = IsKey(static_cast<uint8_t>(x)) ? 0 : static_cast<uint8_t>(x);
		EXPECT_EQ(*ui.at(x, 0), Remap(paletteTransparencyLookup[0][d]));
	}
	// Colours 3 and 4 blend into the keys; they are remapped to real colours.
	EXPECT_FALSE(IsKey(*ui.at(3, 0)));
	EXPECT_FALSE(IsKey(*ui.at(4, 0)));
}

TEST_F(UiLayerTest, ColoredHalfPixelOverTransparentKeepsColor)
{
	const Surface ui = GlobalBackBuffer();
	for (int color = 0; color < 256; ++color) {
		SetHalfTransparentPixel(ui, { color, 7 }, static_cast<uint8_t>(color));
		EXPECT_EQ(*ui.at(color, 7), UiKeyHalf);
		EXPECT_EQ(Half(ui, color, 7), color);
	}
}

TEST_F(UiLayerTest, OpaqueWriteOverHalfIsOpaque)
{
	const Surface ui = GlobalBackBuffer();
	DrawHalfTransparentRectTo(ui, 10, 10, 20, 20, 200);
	ASSERT_EQ(*ui.at(15, 15), UiKeyHalf);
	FillRect(ui, 12, 12, 5, 5, 150);
	EXPECT_EQ(*ui.at(15, 15), 150);
	EXPECT_EQ(*ui.at(11, 11), UiKeyHalf);
	EXPECT_EQ(Half(ui, 11, 11), 200);
}

TEST_F(UiLayerTest, NoBlendOutputIsAKey)
{
	const Surface ui = GlobalBackBuffer();
	const UiKeyedTarget target = GetUiKeyedTarget();

	std::array<uint8_t, 256> reverseMap;
	for (unsigned i = 0; i < reverseMap.size(); ++i)
		reverseMap[i] = static_cast<uint8_t>(255 - i);

	using BlendRow = std::function<void(int y, uint8_t source)>;
	const std::vector<std::pair<const char *, BlendRow>> primitives = {
		{ "DrawHalfTransparentRectTo(color)", [&](int y, uint8_t s) { DrawHalfTransparentRectTo(ui, 0, y, 256, 1, s); } },
		{ "SetHalfTransparentPixel", [&](int y, uint8_t s) { for (int x = 0; x < 256; ++x) SetHalfTransparentPixel(ui, { x, y }, s); } },
		{ "DrawHalfTransparentHorizontalLine", [&](int y, uint8_t s) { DrawHalfTransparentHorizontalLine(ui, { 0, y }, 256, s); } },
		{ "DrawHalfTransparentVerticalLine", [&](int y, uint8_t s) { for (int x = 0; x < 256; ++x) DrawHalfTransparentVerticalLine(ui, { x, y }, 1, s); } },
		{ "BlitBlendedKeyed pixels", [&](int y, uint8_t s) {
		     const std::vector<uint8_t> src(256, s);
		     BlitBlendedKeyed { target }(256, ui.at(0, y), src.data());
		 } },
		{ "BlitBlendedKeyed fill", [&](int y, uint8_t s) { BlitBlendedKeyed { target }(256, s, ui.at(0, y)); } },
		{ "BlitBlendedWithMapKeyed pixels", [&](int y, uint8_t s) {
		     const std::vector<uint8_t> src(256, static_cast<uint8_t>(255 - s));
		     BlitBlendedWithMapKeyed { reverseMap.data(), target }(256, ui.at(0, y), src.data());
		 } },
		{ "BlitBlendedWithMapKeyed fill", [&](int y, uint8_t s) { BlitBlendedWithMapKeyed { reverseMap.data(), target }(256, static_cast<uint8_t>(255 - s), ui.at(0, y)); } },
	};

	for (const auto &[name, blendRow] : primitives) {
		SCOPED_TRACE(name);
		int remapped = 0;

		// Every destination colour (1 and 2 are the keys) blended with every source colour.
		for (int s = 0; s < 256; ++s) {
			for (int x = 0; x < 256; ++x) {
				*ui.at(x, s) = static_cast<uint8_t>(x);
				Half(ui, x, s) = static_cast<uint8_t>(255 - x);
			}
		}
		for (int s = 0; s < 256; ++s)
			blendRow(s, static_cast<uint8_t>(s));
		for (int s = 0; s < 256; ++s) {
			for (int d = 0; d < 256; ++d) {
				const uint8_t result = *ui.at(d, s);
				if (d == UiKeyTransparent) {
					ASSERT_EQ(result, UiKeyHalf) << "s=" << s;
					ASSERT_EQ(Half(ui, d, s), s);
				} else if (d == UiKeyHalf) {
					ASSERT_EQ(result, UiKeyHalf) << "s=" << s;
					ASSERT_EQ(Half(ui, d, s), Remap(paletteTransparencyLookup[s][255 - d])) << "s=" << s;
				} else {
					const uint8_t blended = paletteTransparencyLookup[s][d];
					if (IsKey(blended))
						++remapped;
					ASSERT_FALSE(IsKey(result)) << "s=" << s << " d=" << d;
					ASSERT_EQ(result, Remap(blended)) << "s=" << s << " d=" << d;
				}
			}
		}
		EXPECT_GT(remapped, 0);

		// Every half plane colour blended with every source colour.
		for (int s = 0; s < 256; ++s) {
			for (int x = 0; x < 256; ++x) {
				*ui.at(x, s) = UiKeyHalf;
				Half(ui, x, s) = static_cast<uint8_t>(x);
			}
		}
		for (int s = 0; s < 256; ++s)
			blendRow(s, static_cast<uint8_t>(s));
		for (int s = 0; s < 256; ++s) {
			for (int h = 0; h < 256; ++h) {
				ASSERT_EQ(*ui.at(h, s), UiKeyHalf);
				const uint8_t half = Half(ui, h, s);
				ASSERT_FALSE(IsKey(half)) << "s=" << s << " h=" << h;
				ASSERT_EQ(half, Remap(paletteTransparencyLookup[s][h])) << "s=" << s << " h=" << h;
			}
		}
	}
}

TEST_F(UiLayerTest, LegacyBlendingOutsideOfLayeredFrames)
{
	// Not the UI layer: blending is unchanged even during a layered frame.
	OwnedSurface other(16, 1);
	for (int x = 0; x < 16; ++x)
		*other.at(x, 0) = static_cast<uint8_t>(x);
	DrawHalfTransparentRectTo(other, 0, 0, 16, 1);
	for (int x = 0; x < 16; ++x)
		EXPECT_EQ(*other.at(x, 0), paletteTransparencyLookup[0][x]);
	EXPECT_EQ(*other.at(3, 0), UiKeyTransparent);

	// The back buffer outside of a layered frame: upstream behaviour.
	EndLayeredMode();
	EXPECT_FALSE(PresentLayered());
	const Surface ui = GlobalBackBuffer();
	for (int x = 0; x < 256; ++x)
		*ui.at(x, 0) = static_cast<uint8_t>(x);
	DrawHalfTransparentRectTo(ui, 0, 0, 256, 1);
	SetHalfTransparentPixel(ui, { 0, 1 }, 7);
	for (int x = 0; x < 256; ++x)
		EXPECT_EQ(*ui.at(x, 0), paletteTransparencyLookup[0][x]);
	EXPECT_EQ(*ui.at(3, 0), UiKeyTransparent);
	EXPECT_EQ(*ui.at(4, 0), UiKeyHalf);
}

TEST_F(UiLayerTest, RedBackTintsWorldAndUi)
{
	// A pause TRN that also produces the keys: 250 -> 1, 251 -> 2.
	for (unsigned i = 0; i < PauseTable.size(); ++i)
		PauseTable[i] = static_cast<uint8_t>((i + 7) & 0xFF);
	const uint8_t *trn = PauseTable.data();

	for (const dungeon_type type : { DTYPE_CATHEDRAL, DTYPE_HELL }) {
		SCOPED_TRACE(static_cast<int>(type));
		const bool hell = type == DTYPE_HELL;
		leveltype = type;

		const Surface world = WorldBuffer();
		for (int y = 0; y < world.h(); ++y) {
			for (int x = 0; x < world.w(); ++x)
				*world.at(x, y) = static_cast<uint8_t>(x + y);
		}
		const Surface ui = GlobalBackBuffer();
		std::memset(PalSurface->pixels, UiKeyTransparent, static_cast<size_t>(PalSurface->pitch) * PalSurface->h);
		for (int x = 0; x < 256; ++x) {
			*ui.at(x, 0) = IsKey(static_cast<uint8_t>(x)) ? 200 : static_cast<uint8_t>(x);
			*ui.at(x, 1) = UiKeyHalf;
			Half(ui, x, 1) = static_cast<uint8_t>(x);
			Half(ui, x, 2) = static_cast<uint8_t>(x);
		}

		RedBack(ui);

		for (int y = 0; y < world.h(); ++y) {
			for (int x = 0; x < world.w(); ++x) {
				const auto color = static_cast<uint8_t>(x + y);
				const uint8_t expected = hell && color < 32 ? color : trn[color];
				ASSERT_EQ(*world.at(x, y), expected) << x << "," << y;
			}
		}
		for (int x = 0; x < 256; ++x) {
			const uint8_t opaque = IsKey(static_cast<uint8_t>(x)) ? 200 : static_cast<uint8_t>(x);
			const uint8_t expectedOpaque = hell && opaque < 32 ? opaque : Remap(trn[opaque]);
			EXPECT_EQ(*ui.at(x, 0), expectedOpaque) << x;
			EXPECT_FALSE(IsKey(*ui.at(x, 0))) << x;

			EXPECT_EQ(*ui.at(x, 1), UiKeyHalf);
			const auto half = static_cast<uint8_t>(x);
			EXPECT_EQ(Half(ui, x, 1), hell && half < 32 ? half : trn[half]) << x;

			EXPECT_EQ(*ui.at(x, 2), UiKeyTransparent);
			EXPECT_EQ(Half(ui, x, 2), x);
		}
	}
}

TEST_F(UiLayerTest, ExpandFrameAlpha)
{
	const Surface ui = GlobalBackBuffer();
	FillRect(ui, 0, 0, 256, 10, 160);
	DrawHalfTransparentRectTo(ui, 0, 5, 256, 10);
	DrawHalfTransparentRectTo(ui, 0, 20, 256, 10, 250);

	uint32_t lut[256];
	BuildPaletteLut32(system_palette.data(), lut);
	std::vector<uint32_t> pixels(static_cast<size_t>(UiSize.width) * UiSize.height);
	ExpandUiKeyed(static_cast<const uint8_t *>(PalSurface->pixels), GetUiKeyedTarget().HalfAt(ui.at(0, 0)), PalSurface->pitch,
	    pixels.data(), static_cast<int>(UiSize.width * sizeof(uint32_t)), UiSize.width, UiSize.height, lut);

	const auto rgb = [](const SDL_Color &c) { return (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.b; };
	const auto at = [&](int x, int y) { return pixels[static_cast<size_t>(y) * UiSize.width + x]; };
	EXPECT_EQ(at(17, 2), rgb(system_palette[160]) | 0xFF000000);
	EXPECT_EQ(at(17, 7), rgb(system_palette[Remap(paletteTransparencyLookup[0][160])]) | 0xFF000000);
	EXPECT_EQ(at(17, 12), rgb(system_palette[0]) | 0x80000000);
	EXPECT_EQ(at(17, 25), rgb(system_palette[250]) | 0x80000000);
	EXPECT_EQ(at(17, 40), 0U);
	for (const uint32_t pixel : pixels) {
		const uint32_t alpha = pixel >> 24;
		ASSERT_TRUE(alpha == 0 || alpha == 0x80 || alpha == 0xFF);
		if (alpha == 0) {
			ASSERT_EQ(pixel, 0U);
		}
	}
}

#endif // DVL_LAYERED_RENDERER

} // namespace
} // namespace devilution
