/**
 * Acceptance test M1-T4 (issue #4): the composite of the layered renderer matches the upstream
 * single-surface result.
 */
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "engine/backbuffer_state.hpp"
#include "engine/dx.h"
#include "engine/layered_present.hpp"
#include "engine/palette.h"
#include "engine/point.hpp"
#include "engine/render/blit_impl.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/ui_layer.hpp"
#include "engine/render/world_view.hpp"
#include "engine/size.hpp"
#include "engine/surface.hpp"
#include "utils/display.h"
#include "utils/palette_blending.hpp"
#include "utils/sdl_wrap.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {

TEST(LayeredPerfLogTest, SummarizeFrameTimes)
{
	EXPECT_EQ(SummarizeFrameTimes({}).average, 0);
	EXPECT_EQ(SummarizeFrameTimes({}).p95, 0);

	const std::vector<float> single { 4.5F };
	EXPECT_FLOAT_EQ(SummarizeFrameTimes(single).average, 4.5);
	EXPECT_FLOAT_EQ(SummarizeFrameTimes(single).p95, 4.5);

	// 300 frames (one log entry) of 1..300 ms in reverse order: p95 is the 285th smallest.
	std::vector<float> frames;
	for (int i = 300; i >= 1; --i)
		frames.push_back(static_cast<float>(i));
	EXPECT_DOUBLE_EQ(SummarizeFrameTimes(frames).average, 150.5);
	EXPECT_FLOAT_EQ(SummarizeFrameTimes(frames).p95, 285);

	// Nearest rank rounds up: 95 % of 10 samples is the 10th.
	const std::vector<float> ten { 1, 1, 1, 1, 1, 1, 1, 1, 1, 9 };
	EXPECT_FLOAT_EQ(SummarizeFrameTimes(ten).p95, 9);
}

#ifdef DVL_LAYERED_RENDERER

constexpr int MainPanelHeight = 128;

/** @brief A 6x7x6 colour cube with black, white and two saturated colours on the key indices. */
std::array<SDL_Color, 256> MakeCubePalette()
{
	std::array<SDL_Color, 256> palette;
	palette[0] = { 0, 0, 0, SDL_ALPHA_OPAQUE };
	palette[UiKeyTransparent] = { 255, 0, 128, SDL_ALPHA_OPAQUE };
	palette[UiKeyHalf] = { 0, 128, 255, SDL_ALPHA_OPAQUE };
	for (unsigned i = 3; i < 255; ++i) {
		const unsigned j = i - 3;
		palette[i] = {
			static_cast<uint8_t>((j % 6) * 51),
			static_cast<uint8_t>(((j / 6) % 7) * 42),
			static_cast<uint8_t>(((j / 42) % 6) * 51),
			SDL_ALPHA_OPAQUE,
		};
	}
	palette[255] = { 255, 255, 255, SDL_ALPHA_OPAQUE };
	return palette;
}

uint32_t Rgb(const SDL_Color &color)
{
	return (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
}

int Channel(uint32_t color, int shift)
{
	return static_cast<int>((color >> shift) & 0xFF);
}

/** @brief Sets up the layered renderer state for the given geometry without a renderer. */
class LayeredFrameSetup {
public:
	LayeredFrameSetup(Size output, Size ui, int ws)
	    : savedPalSurface_(PalSurface)
	    , savedScreenWidth_(gnScreenWidth)
	    , savedScreenHeight_(gnScreenHeight)
	    , savedViewportHeight_(gnViewportHeight)
	    , savedLogicalPalette_(logical_palette)
	    , savedSystemPalette_(system_palette)
	{
		gnScreenWidth = ui.width;
		gnScreenHeight = ui.height;
		gnViewportHeight = ui.height;
		uiSurface_ = SDLWrap::CreateRGBSurfaceWithFormat(0, ui.width, ui.height, 8, SDL_PIXELFORMAT_INDEX8);
		PalSurface = uiSurface_.get();

		logical_palette = MakeCubePalette();
		system_palette = logical_palette;
		GenerateBlendedLookupTable(logical_palette.data());
		RebuildUiKeyRemap();

		view_ = ComputeWorldView(output, ui, ComputeUiPlacement(output, ui, true), ws, MainPanelHeight);
		SetWorldViewForTesting(view_);
		UpdateLayeredBuffers();
		BeginLayeredFrame();
	}

	~LayeredFrameSetup()
	{
		EndLayeredMode();
		SetWorldViewForTesting(ComputeLegacyWorldView(view_.uiSize, MainPanelHeight));
		UpdateLayeredBuffers();
		PalSurface = savedPalSurface_;
		gnScreenWidth = savedScreenWidth_;
		gnScreenHeight = savedScreenHeight_;
		gnViewportHeight = savedViewportHeight_;
		logical_palette = savedLogicalPalette_;
		system_palette = savedSystemPalette_;
	}

	[[nodiscard]] const WorldView &View() const
	{
		return view_;
	}

private:
	SDLSurfaceUniquePtr uiSurface_;
	WorldView view_ {};
	SDL_Surface *savedPalSurface_;
	uint16_t savedScreenWidth_;
	uint16_t savedScreenHeight_;
	uint16_t savedViewportHeight_;
	std::array<SDL_Color, 256> savedLogicalPalette_;
	std::array<SDL_Color, 256> savedSystemPalette_;
};

void DrawWorldPattern(const Surface &world)
{
	for (int y = 0; y < world.h(); ++y) {
		for (int x = 0; x < world.w(); ++x) {
			*world.at(x, y) = static_cast<uint8_t>((x * 7) + (y * 13) + ((x * y) >> 5));
		}
	}
}

/**
 * @brief A UI with opaque, transparent and half-transparent parts, drawn by the same primitives
 * on any 8-bit surface of at least 960x540.
 */
void DrawUiScene(const Surface &out)
{
	// Main panel and some opaque "text".
	for (int y = 412; y < 540; ++y)
		DrawHorizontalLine(out, { 0, y }, 960, static_cast<uint8_t>(128 + ((y * 3) % 120)));
	for (int i = 0; i < 400; ++i)
		out.SetPixel({ (i * 37) % 960, 160 + ((i * 11) % 90) }, static_cast<uint8_t>(130 + (i % 100)));

	// Half-transparent black and coloured rectangles over the world.
	DrawHalfTransparentRectTo(out, 101, 50, 199, 100);
	DrawHalfTransparentRectTo(out, 400, 60, 150, 80, 250);
	// Half-transparent black rectangle over the world and the panel.
	DrawHalfTransparentRectTo(out, 700, 380, 200, 100);
	// Automap-style half-transparent lines and pixels.
	DrawHalfTransparentHorizontalLine(out, { 50, 300 }, 400, 200);
	DrawHalfTransparentVerticalLine(out, { 600, 160 }, 200, 150);
	for (int x = 0; x < 100; ++x)
		SetHalfTransparentPixel(out, { 300 + x, 350 + (x / 4) }, static_cast<uint8_t>(3 + x));

	// A blended sprite run.
	std::array<uint8_t, 100> src;
	for (unsigned i = 0; i < src.size(); ++i)
		src[i] = static_cast<uint8_t>(40 + i);
	if (IsKeyedUiLayer(out)) {
		BlitBlendedKeyed { GetUiKeyedTarget() }(src.size(), out.at(620, 200), src.data());
	} else {
		BlitBlended {}(src.size(), out.at(620, 200), src.data());
	}
}

TEST(LayeredComposeTest, MatchesSingleSurfaceAt5K)
{
	const Size output { 5760, 3240 };
	const Size ui { 960, 540 };
	constexpr int Scale = 6;
	const LayeredFrameSetup setup(output, ui, Scale);
	const WorldView &view = setup.View();
	ASSERT_TRUE(PresentLayered());
	ASSERT_EQ(view.uiScale, Scale);
	ASSERT_EQ(view.uiOriginPx, Point(0, 0));
	ASSERT_EQ(view.size, ui);
	ASSERT_EQ(view.originPx, Point(0, 0));

	// Layered: world into the world surface, UI into the keyed back buffer.
	const Surface world = WorldBuffer();
	DrawWorldPattern(world);
	const Surface uiLayer = GlobalBackBuffer();
	DrawUiScene(uiLayer);

	// Upstream: the world and then the UI on one 8-bit surface.
	OwnedSurface legacy(ui.width, ui.height);
	DrawWorldPattern(legacy);
	DrawUiScene(legacy);

	std::vector<uint32_t> composite(static_cast<size_t>(output.width) * output.height);
	ComposeLayersCpu(output, composite);

	int transparent = 0;
	int opaque = 0;
	int half = 0;
	for (int y = 0; y < ui.height; ++y) {
		for (int x = 0; x < ui.width; ++x) {
			// Nearest downsample: the centre of the block, and the block must be uniform.
			const uint32_t pixel = composite[(static_cast<size_t>(y) * Scale + Scale / 2) * output.width + (x * Scale) + (Scale / 2)];
			for (int by = 0; by < Scale; ++by) {
				for (int bx = 0; bx < Scale; ++bx) {
					ASSERT_EQ(composite[(static_cast<size_t>(y) * Scale + by) * output.width + (x * Scale) + bx], pixel) << x << "," << y;
				}
			}

			const uint8_t uiColor = *uiLayer.at(x, y);
			const uint8_t legacyColor = *legacy.at(x, y);
			if (uiColor == UiKeyHalf) {
				++half;
				// The GPU blends the exact colours (upstream picks the nearest palette entry instead):
				// within 1 of the true 50 % blend, well inside the 8 the acceptance test allows.
				const uint32_t source = Rgb(system_palette[*GetUiKeyedTarget().HalfAt(uiLayer.at(x, y))]);
				const uint32_t below = Rgb(system_palette[*world.at(x, y)]);
				for (const int shift : { 0, 8, 16 }) {
					const int exact = (Channel(source, shift) + Channel(below, shift)) / 2;
					ASSERT_LE(std::abs(Channel(pixel, shift) - exact), 1) << x << "," << y;
				}
				ASSERT_EQ(pixel >> 24, 0xFFU);
				continue;
			}
			if (uiColor == UiKeyTransparent) {
				++transparent;
			} else {
				++opaque;
				// Keys are remapped on the UI layer; the scene avoids blends that produce them.
				ASSERT_NE(legacyColor, UiKeyTransparent) << x << "," << y;
				ASSERT_NE(legacyColor, UiKeyHalf) << x << "," << y;
			}
			ASSERT_EQ(pixel, Rgb(system_palette[legacyColor]) | 0xFF000000) << x << "," << y;
		}
	}
	EXPECT_GT(transparent, 0);
	EXPECT_GT(opaque, 0);
	EXPECT_GT(half, 0);
}

TEST(LayeredComposeTest, WorldFillsLetterboxBars)
{
	// 16:10 output, 16:9 UI: the UI viewport has bars at the top and the bottom, the world does not.
	const Size output { 1280, 800 };
	const Size ui { 640, 360 };
	const LayeredFrameSetup setup(output, ui, 3);
	const WorldView &view = setup.View();
	ASSERT_TRUE(PresentLayered());
	ASSERT_EQ(view.uiOriginPx, Point(0, 40));
	ASSERT_EQ(view.uiViewportSize, Size(1280, 720));
	ASSERT_EQ(view.size, Size(427, 267));

	const Surface world = WorldBuffer();
	DrawWorldPattern(world);
	const Surface uiLayer = GlobalBackBuffer();
	for (int y = 300; y < 360; ++y)
		DrawHorizontalLine(uiLayer, { 0, y }, 640, static_cast<uint8_t>(128 + (y % 100)));
	DrawHalfTransparentRectTo(uiLayer, 10, 10, 100, 50);

	std::vector<uint32_t> composite(static_cast<size_t>(output.width) * output.height);
	ComposeLayersCpu(output, composite);
	const auto at = [&](int x, int y) { return composite[static_cast<size_t>(y) * output.width + x]; };
	const auto worldColor = [&](Point q) { return Rgb(system_palette[*world.at(q.x, q.y)]) | 0xFF000000; };

	// The bars show the world.
	for (const int y : { 0, 1, 20, 39, 760, 780, 799 }) {
		for (int x = 0; x < output.width; ++x) {
			ASSERT_EQ(at(x, y), worldColor({ FloorDiv(x - view.originPx.x, view.ws), FloorDiv(y - view.originPx.y, view.ws) })) << x << "," << y;
		}
	}

	// Inside the UI viewport, transparent UI pixels show the world pixel `UiToWorld` maps them to.
	for (int y = 0; y < ui.height; ++y) {
		for (int x = 0; x < ui.width; ++x) {
			const uint8_t uiColor = *uiLayer.at(x, y);
			const uint32_t pixel = at((x * 2) + 1, 40 + (y * 2) + 1);
			if (uiColor == UiKeyTransparent) {
				ASSERT_EQ(pixel, worldColor(UiToWorld(view, { x, y }))) << x << "," << y;
			} else if (uiColor != UiKeyHalf) {
				const uint32_t expected = Rgb(system_palette[uiColor]) | 0xFF000000;
				ASSERT_EQ(at(x * 2, 40 + (y * 2)), expected) << x << "," << y;
				ASSERT_EQ(pixel, expected) << x << "," << y;
			}
		}
	}
}

TEST(LayeredComposeTest, EndingTheLayeredModeRedrawsEverything)
{
	const LayeredFrameSetup setup({ 1920, 1080 }, { 960, 540 }, 2);
	ASSERT_TRUE(PresentLayered());

	// The back buffer holds only the keyed UI layer, so the upstream path must redraw everything.
	RedrawComplete();
	EndLayeredMode();
	EXPECT_FALSE(PresentLayered());
	EXPECT_TRUE(IsRedrawEverything());

	// Without a layered frame there is nothing to end.
	RedrawComplete();
	EndLayeredMode();
	EXPECT_FALSE(IsRedrawEverything());
}

TEST(LayeredComposeTest, BeginLayeredFrameEndsTheModeWhenNoLongerLayered)
{
	const LayeredFrameSetup setup({ 1920, 1080 }, { 960, 540 }, 2);
	ASSERT_TRUE(PresentLayered());

	// E.g. the renderer output became unusable: the next frame is drawn the upstream way.
	SetWorldViewForTesting(ComputeLegacyWorldView(setup.View().uiSize, MainPanelHeight));
	BeginLayeredFrame();
	EXPECT_FALSE(PresentLayered());

	// Layered again: the frame starts with a transparent UI layer.
	SetWorldViewForTesting(setup.View());
	*GlobalBackBuffer().at(10, 10) = 200;
	BeginLayeredFrame();
	EXPECT_TRUE(PresentLayered());
	EXPECT_EQ(*GlobalBackBuffer().at(10, 10), UiKeyTransparent);
}

/** @brief A software renderer drawing into a surface, in place of the window's renderer. */
class SoftwareRendererScope {
public:
	explicit SoftwareRendererScope(Size output)
	    : output_(SDLWrap::CreateRGBSurfaceWithFormat(0, output.width, output.height, 32, SDL_PIXELFORMAT_ARGB8888))
	    , savedRenderer_(renderer)
	{
		renderer = SDL_CreateSoftwareRenderer(output_.get());
	}

	~SoftwareRendererScope()
	{
		// The layered textures belong to the renderer.
		FreeLayeredResources();
		if (renderer != nullptr)
			SDL_DestroyRenderer(renderer);
		renderer = savedRenderer_;
	}

	SoftwareRendererScope(const SoftwareRendererScope &) = delete;
	SoftwareRendererScope &operator=(const SoftwareRendererScope &) = delete;

private:
	SDLSurfaceUniquePtr output_;
	SDL_Renderer *savedRenderer_;
};

TEST(LayeredComposeTest, TexturesAreFreedWhenNoLongerLayered)
{
	const LayeredFrameSetup setup({ 1920, 1080 }, { 960, 540 }, 2);
	ASSERT_TRUE(PresentLayered());
	const SoftwareRendererScope softwareRenderer({ 1920, 1080 });
	ASSERT_NE(renderer, nullptr) << SDL_GetError();

	EXPECT_TRUE(LayeredPresent());
	EXPECT_TRUE(LayeredTexturesAllocated());

	// Leaving only the present mode (menus, movies, progress screens) keeps the textures for the next layered frame.
	EndLayeredMode();
	EXPECT_TRUE(LayeredTexturesAllocated());

	// No longer layered (e.g. Independent Zoom or Upscale turned off): the textures are released.
	SetWorldViewForTesting(ComputeLegacyWorldView(setup.View().uiSize, MainPanelHeight));
	UpdateLayeredBuffers();
	EXPECT_FALSE(LayeredTexturesAllocated());
}

TEST(LayeredComposeTest, NotLayeredWithoutAFrame)
{
	// Outside of a layered frame nothing is keyed and the composite is black.
	EXPECT_FALSE(PresentLayered());
	EXPECT_FALSE(IsLayeredActive());
	std::vector<uint32_t> composite(16 * 16, 0x12345678);
	ComposeLayersCpu({ 16, 16 }, composite);
	for (const uint32_t pixel : composite)
		EXPECT_EQ(pixel, 0xFF000000);
}

#endif // DVL_LAYERED_RENDERER

} // namespace
} // namespace devilution
