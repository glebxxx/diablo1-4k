#include <array>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "engine/layered_present.hpp"
#include "engine/render/ui_layer.hpp"
#include "engine/render/world_view.hpp"
#include "utils/palette_blending.hpp"

using namespace devilution;

namespace {

std::array<SDL_Color, 256> MakePalette()
{
	std::array<SDL_Color, 256> palette;
	std::mt19937 rng(7);
	std::uniform_int_distribution<int> dist(0, 255);
	for (SDL_Color &color : palette) {
		color = SDL_Color { static_cast<uint8_t>(dist(rng)), static_cast<uint8_t>(dist(rng)), static_cast<uint8_t>(dist(rng)), 255 };
	}
	return palette;
}

int Channel(uint32_t color, int shift)
{
	return static_cast<int>((color >> shift) & 0xFF);
}

TEST(LayeredComposeTest, MatchesLegacySingleSurface)
{
	// us = ws = 6 on the target 5K output: world and UI have the same size.
	const Size output { 5760, 3240 };
	const Size ui { 960, 540 };
	const WorldView view = ComputeWorldView(output, ui, ComputeUiPlacement(output, ui, true), 6, 128);
	ASSERT_EQ(view.size, ui);

	const std::array<SDL_Color, 256> palette = MakePalette();
	GenerateBlendedLookupTable(palette.data());
	uint32_t lut[256];
	BuildPaletteLut(palette.data(), lut);

	std::mt19937 rng(99);
	std::uniform_int_distribution<int> colorDist(3, 255);
	std::uniform_int_distribution<int> kindDist(0, 2);
	std::vector<uint8_t> world(static_cast<size_t>(ui.width) * ui.height);
	std::vector<uint8_t> uiLayer(world.size());
	std::vector<uint8_t> half(world.size());
	std::vector<uint8_t> legacy(world.size());
	for (size_t i = 0; i < world.size(); ++i) {
		world[i] = static_cast<uint8_t>(colorDist(rng));
		switch (kindDist(rng)) {
		case 0: // transparent
			uiLayer[i] = UiKeyTransparent;
			half[i] = static_cast<uint8_t>(colorDist(rng)); // stale, never read
			legacy[i] = world[i];
			break;
		case 1: // half-transparent
			uiLayer[i] = UiKeyHalf;
			half[i] = static_cast<uint8_t>(colorDist(rng));
			legacy[i] = paletteTransparencyLookup[half[i]][world[i]];
			break;
		default: // opaque
			uiLayer[i] = static_cast<uint8_t>(colorDist(rng));
			legacy[i] = uiLayer[i];
			break;
		}
	}

	std::vector<uint32_t> composed(static_cast<size_t>(output.width) * output.height);
	ComposeLayersCpu(view, world.data(), ui.width, uiLayer.data(), half.data(), ui.width, lut, composed);

	int halfPixels = 0;
	for (int y = 0; y < ui.height; ++y) {
		for (int x = 0; x < ui.width; ++x) {
			const size_t i = static_cast<size_t>(y) * ui.width + x;
			// Every output pixel of the 6x6 block must be the same (nearest).
			const uint32_t block = composed[static_cast<size_t>(y * 6) * output.width + x * 6];
			const uint32_t sample = composed[static_cast<size_t>(y * 6 + 3) * output.width + x * 6 + 5];
			ASSERT_EQ(block, sample) << x << "," << y;
			if (uiLayer[i] != UiKeyHalf) {
				ASSERT_EQ(sample, lut[legacy[i]] | 0xFF000000U) << x << "," << y;
			} else {
				++halfPixels;
				const uint32_t a = lut[half[i]];
				const uint32_t b = lut[world[i]];
				for (const int shift : { 0, 8, 16 }) {
					const int exact = (Channel(a, shift) + Channel(b, shift)) / 2;
					ASSERT_LE(std::abs(Channel(sample, shift) - exact), 8) << x << "," << y;
				}
				ASSERT_EQ(sample >> 24, 0xFFU);
			}
		}
	}
	EXPECT_GT(halfPixels, 0);
}

TEST(LayeredComposeTest, WorldAndUiAtDifferentScales)
{
	// ws = 3, UI scale 6 with an offset (853x480 on 5760x3240).
	const Size output { 5760, 3240 };
	const Size ui { 853, 480 };
	const UiPlacement placement = ComputeUiPlacement(output, ui, true);
	ASSERT_EQ(placement.scale, 6.0F);
	ASSERT_EQ(placement.origin, Point(321, 180));
	const WorldView view = ComputeWorldView(output, ui, placement, 3, 128);
	ASSERT_EQ(view.size, Size(1920, 1080));

	uint32_t lut[256];
	for (unsigned i = 0; i < 256; ++i)
		lut[i] = i * 0x010101U;

	std::vector<uint8_t> world(static_cast<size_t>(view.size.width) * view.size.height);
	for (int y = 0; y < view.size.height; ++y) {
		for (int x = 0; x < view.size.width; ++x)
			world[static_cast<size_t>(y) * view.size.width + x] = static_cast<uint8_t>(3 + (x + y) % 200);
	}
	std::vector<uint8_t> uiLayer(static_cast<size_t>(ui.width) * ui.height, UiKeyTransparent);
	std::vector<uint8_t> half(uiLayer.size(), 0);
	uiLayer[10 * ui.width + 20] = 250;

	std::vector<uint32_t> composed(static_cast<size_t>(output.width) * output.height);
	ComposeLayersCpu(view, world.data(), view.size.width, uiLayer.data(), half.data(), ui.width, lut, composed);

	// Letterbox and transparent UI: world pixel (x / 3, y / 3).
	for (const Point p : { Point { 0, 0 }, Point { 100, 3000 }, Point { 5759, 3239 }, Point { 2000, 1000 } }) {
		const uint8_t expected = world[static_cast<size_t>(p.y / 3) * view.size.width + p.x / 3];
		EXPECT_EQ(composed[static_cast<size_t>(p.y) * output.width + p.x], lut[expected] | 0xFF000000U) << p;
	}
	// Opaque UI pixel (20, 10) covers output [321 + 120, 321 + 126) x [180 + 60, 180 + 66).
	EXPECT_EQ(composed[static_cast<size_t>(180 + 60) * output.width + 321 + 120], lut[250] | 0xFF000000U);
	EXPECT_EQ(composed[static_cast<size_t>(180 + 65) * output.width + 321 + 125], lut[250] | 0xFF000000U);
	EXPECT_NE(composed[static_cast<size_t>(180 + 66) * output.width + 321 + 125], lut[250] | 0xFF000000U);
}

} // namespace
