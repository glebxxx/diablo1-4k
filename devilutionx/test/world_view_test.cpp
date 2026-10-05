#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "engine/demomode.h"
#include "engine/point.hpp"
#include "engine/render/world_view.hpp"
#include "engine/size.hpp"
#include "options.h"

namespace devilution {
namespace {

using ::testing::ElementsAre;

constexpr int MainPanelHeight = 128;

const Size Outputs[] = { { 5760, 3240 }, { 5120, 2880 }, { 1920, 1080 } };
const Size UiSizes[] = { { 960, 540 }, { 853, 480 }, { 3840, 2160 } };

int CeilRatio(double a, double b)
{
	return static_cast<int>(std::ceil(a / b));
}

TEST(WorldViewTest, FloorDivAndPosMod)
{
	EXPECT_EQ(FloorDiv(7, 64), 0);
	EXPECT_EQ(FloorDiv(64, 64), 1);
	EXPECT_EQ(FloorDiv(-1, 64), -1);
	EXPECT_EQ(FloorDiv(-64, 64), -1);
	EXPECT_EQ(FloorDiv(-65, 64), -2);
	EXPECT_EQ(PosMod(7, 64), 7);
	EXPECT_EQ(PosMod(-1, 64), 63);
	EXPECT_EQ(PosMod(-64, 64), 0);
	EXPECT_EQ(PosMod(-65, 32), 31);
}

TEST(WorldViewTest, ZoomLadder)
{
	EXPECT_THAT(ComputeZoomLadder(3240, 240, 1620), ElementsAre(2, 3, 4, 5, 6, 8, 10, 12));
	EXPECT_THAT(ComputeZoomLadder(2880, 240, 1620), ElementsAre(2, 3, 4, 5, 6, 8, 10, 12));
	EXPECT_THAT(ComputeZoomLadder(1080, 240, 1620), ElementsAre(1, 2, 3, 4));
	EXPECT_THAT(ComputeZoomLadder(1440, 240, 1620), ElementsAre(1, 2, 3, 4, 5, 6));
	// The limits filter the ladder.
	EXPECT_THAT(ComputeZoomLadder(3240, 540, 1080), ElementsAre(3, 4, 5, 6));
	EXPECT_THAT(ComputeZoomLadder(3240, 180, 3240), ElementsAre(1, 2, 3, 4, 5, 6, 8, 10, 12, 16));
	// Empty ladders fall back to about 540 world pixels.
	EXPECT_THAT(ComputeZoomLadder(3240, 1080, 720), ElementsAre(6));
	EXPECT_THAT(ComputeZoomLadder(200, 240, 1620), ElementsAre(1));
	EXPECT_THAT(ComputeZoomLadder(0, 240, 1620), ElementsAre(1));
}

TEST(WorldViewTest, AutoWorldZoom)
{
	EXPECT_EQ(AutoWorldZoom(ComputeZoomLadder(3240, 240, 1620), 3240), 6);
	EXPECT_EQ(AutoWorldZoom(ComputeZoomLadder(2880, 240, 1620), 2880), 5);
	EXPECT_EQ(AutoWorldZoom(ComputeZoomLadder(1440, 240, 1620), 1440), 3);
	EXPECT_EQ(AutoWorldZoom(ComputeZoomLadder(1080, 240, 1620), 1080), 2);
	// Nearest entry when the ideal factor is not on the ladder.
	EXPECT_EQ(AutoWorldZoom(ComputeZoomLadder(3240, 540, 1080), 3240), 6);
	EXPECT_EQ(AutoWorldZoom(ComputeZoomLadder(3240, 1080, 1620), 3240), 3);
}

TEST(WorldViewTest, SnapAndStep)
{
	const std::vector<int> ladder = ComputeZoomLadder(3240, 240, 1620); // 2 3 4 5 6 8 10 12
	EXPECT_EQ(SnapToZoomLadder(ladder, 6), 6);
	EXPECT_EQ(SnapToZoomLadder(ladder, 7), 6);   // tie goes down
	EXPECT_EQ(SnapToZoomLadder(ladder, 9), 8);   // tie goes down
	EXPECT_EQ(SnapToZoomLadder(ladder, 11), 10); // tie goes down
	EXPECT_EQ(SnapToZoomLadder(ladder, 1), 2);
	EXPECT_EQ(SnapToZoomLadder(ladder, 16), 12);

	EXPECT_EQ(StepZoomLadder(ladder, 6, 1), 8);
	EXPECT_EQ(StepZoomLadder(ladder, 6, -1), 5);
	EXPECT_EQ(StepZoomLadder(ladder, 12, 1), 12);
	EXPECT_EQ(StepZoomLadder(ladder, 2, -1), 2);
	EXPECT_EQ(StepZoomLadder(ladder, 7, 1), 8); // stale: snapped to 6 first
	EXPECT_EQ(StepZoomLadder(ladder, 7, -1), 5);
	EXPECT_EQ(StepZoomLadder(ladder, 1, -1), 2);
	EXPECT_EQ(StepZoomLadder(ladder, 1, 1), 3);

	EXPECT_EQ(ResolveWorldZoom(ladder, 0, 3240), 6);
	EXPECT_EQ(ResolveWorldZoom(ladder, 10, 3240), 10);
	EXPECT_EQ(ResolveWorldZoom(ladder, 7, 3240), 6);
	EXPECT_EQ(ResolveWorldZoom(ladder, 1, 3240), 2);
	EXPECT_EQ(ResolveWorldZoom(ladder, 16, 3240), 12);

	// The zoom toggle goes to the ladder entry nearest to twice the current zoom.
	EXPECT_EQ(SnapToZoomLadder(ladder, 2 * 3), 6);
	EXPECT_EQ(SnapToZoomLadder(ladder, 2 * 5), 10);
	EXPECT_EQ(SnapToZoomLadder(ladder, 2 * 8), 12);
}

TEST(WorldViewTest, AutoUiScale)
{
	EXPECT_EQ(AutoUiScale({ 5760, 3240 }), 6);
	EXPECT_EQ(AutoUiScale({ 5120, 2880 }), 6);
	EXPECT_EQ(AutoUiScale({ 3840, 2160 }), 4);
	EXPECT_EQ(AutoUiScale({ 2560, 1440 }), 3);
	EXPECT_EQ(AutoUiScale({ 1920, 1080 }), 2);
	EXPECT_EQ(AutoUiScale({ 1280, 1024 }), 1); // 1280 / 2 = 640 is too narrow
	EXPECT_EQ(AutoUiScale({ 640, 480 }), 1);
	EXPECT_EQ(AutoUiScale({ 320, 240 }), 1);
}

struct PlacementCase {
	Size output;
	Size ui;
	bool integerScale;
	float scale;
	Point origin;
	Size viewport;
};

TEST(WorldViewTest, UiPlacementMatchesSdl2UpdateLogicalSize)
{
	// Expected values follow SDL2 2.30 / 2.32 UpdateLogicalSize (letterbox mode), computed in float.
	const PlacementCase cases[] = {
		// Integer scaling
		{ { 5760, 3240 }, { 960, 540 }, true, 6.0F, { 0, 0 }, { 5760, 3240 } },
		{ { 5760, 3240 }, { 3840, 2160 }, true, 1.0F, { 960, 540 }, { 3840, 2160 } },
		{ { 5120, 2880 }, { 960, 540 }, true, 5.0F, { 160, 90 }, { 4800, 2700 } },
		{ { 5760, 3240 }, { 853, 480 }, true, 6.0F, { 321, 180 }, { 5118, 2880 } },
		{ { 5120, 2880 }, { 853, 480 }, true, 6.0F, { 1, 0 }, { 5118, 2880 } },
		{ { 1920, 1080 }, { 853, 480 }, true, 2.0F, { 107, 60 }, { 1706, 960 } },
		{ { 1920, 1080 }, { 3840, 2160 }, true, 1.0F, { -960, -540 }, { 3840, 2160 } },
		{ { 1600, 1200 }, { 1920, 1080 }, true, 1.0F, { -160, 60 }, { 1920, 1080 } },
		{ { 1920, 1080 }, { 1280, 1024 }, true, 1.0F, { 320, 28 }, { 1280, 1024 } },
		{ { 2560, 1600 }, { 960, 540 }, true, 2.0F, { 320, 260 }, { 1920, 1080 } },
		{ { 5120, 2880 }, { 3840, 2160 }, true, 1.0F, { 640, 360 }, { 3840, 2160 } },
		// Same aspect ratio: the viewport is the whole output
		{ { 5760, 3240 }, { 3840, 2160 }, false, 1.5F, { 0, 0 }, { 5760, 3240 } },
		{ { 5760, 3240 }, { 960, 540 }, false, 6.0F, { 0, 0 }, { 5760, 3240 } },
		{ { 5120, 2880 }, { 960, 540 }, false, 5120.0F / 960.0F, { 0, 0 }, { 5120, 2880 } },
		{ { 1920, 1080 }, { 3840, 2160 }, false, 0.5F, { 0, 0 }, { 1920, 1080 } },
		// UI narrower than the output: side bars
		{ { 5760, 3240 }, { 853, 480 }, false, 6.75F, { 1, 0 }, { 5757, 3240 } },
		{ { 5120, 2880 }, { 853, 480 }, false, 6.0F, { 1, 0 }, { 5118, 2880 } },
		{ { 1920, 1080 }, { 853, 480 }, false, 2.25F, { 0, 0 }, { 1919, 1080 } },
		{ { 1920, 1080 }, { 1280, 1024 }, false, 1.0546875F, { 285, 0 }, { 1350, 1080 } },
		{ { 1920, 1200 }, { 640, 480 }, false, 2.5F, { 160, 0 }, { 1600, 1200 } },
		// UI wider than the output: letterbox
		{ { 1600, 1200 }, { 1920, 1080 }, false, 1600.0F / 1920.0F, { 0, 150 }, { 1600, 900 } },
		{ { 2560, 1600 }, { 960, 540 }, false, 2560.0F / 960.0F, { 0, 80 }, { 2560, 1440 } },
	};
	for (const PlacementCase &c : cases) {
		SCOPED_TRACE(testing::Message() << "O=" << c.output.width << "x" << c.output.height
		                                << " U=" << c.ui.width << "x" << c.ui.height << " integer=" << c.integerScale);
		const UiPlacement placement = ComputeUiPlacement(c.output, c.ui, c.integerScale);
		EXPECT_EQ(placement.scale, c.scale);
		EXPECT_EQ(placement.origin, c.origin);
		EXPECT_EQ(placement.viewportSize, c.viewport);
	}
}

TEST(WorldViewTest, WorldSizeAndOrigin)
{
	const Size output { 5760, 3240 };
	const UiPlacement placement = ComputeUiPlacement(output, { 960, 540 }, true);

	const WorldView ws3 = ComputeWorldView(output, { 960, 540 }, placement, 3, MainPanelHeight);
	EXPECT_TRUE(ws3.layered);
	EXPECT_EQ(ws3.ws, 3);
	EXPECT_EQ(ws3.size, Size(1920, 1080));
	EXPECT_EQ(ws3.originPx, Point(0, 0));
	EXPECT_EQ(ws3.outputSize, output);
	EXPECT_EQ(ws3.uiSize, Size(960, 540));
	EXPECT_EQ(ws3.uiScale, 6.0F);
	EXPECT_EQ(ws3.uiOriginPx, Point(0, 0));

	EXPECT_EQ(ComputeWorldView(output, { 960, 540 }, placement, 6, MainPanelHeight).size, Size(960, 540));
	EXPECT_EQ(ComputeWorldView(output, { 960, 540 }, placement, 12, MainPanelHeight).size, Size(480, 270));

	// Hypothetical ws = 7 on 5120x2880: the world covers the output, the cropped overscan is at most ws - 1.
	const WorldView ws7 = ComputeWorldView({ 5120, 2880 }, { 960, 540 }, ComputeUiPlacement({ 5120, 2880 }, { 960, 540 }, true), 7, MainPanelHeight);
	EXPECT_EQ(ws7.size, Size(732, 412));
	EXPECT_EQ(ws7.originPx, Point(-2, -2));

	for (const Size o : { Size(5760, 3240), Size(5120, 2880), Size(1920, 1080), Size(2560, 1440), Size(1366, 768), Size(1001, 777) }) {
		for (int ws = 1; ws <= 16; ws++) {
			const WorldView view = ComputeWorldView(o, { 960, 540 }, ComputeUiPlacement(o, { 960, 540 }, false), ws, MainPanelHeight);
			const int cropX = view.size.width * ws - o.width;
			const int cropY = view.size.height * ws - o.height;
			EXPECT_GE(cropX, 0);
			EXPECT_LE(cropX, ws - 1);
			EXPECT_GE(cropY, 0);
			EXPECT_LE(cropY, ws - 1);
			EXPECT_LE(view.originPx.x, 0);
			EXPECT_LE(view.originPx.y, 0);
			EXPECT_GE(view.originPx.x + view.size.width * ws, o.width);
			EXPECT_GE(view.originPx.y + view.size.height * ws, o.height);
			EXPECT_EQ(view.originPx.x, -(cropX / 2));
			EXPECT_EQ(view.originPx.y, -(cropY / 2));
		}
	}
}

TEST(WorldViewTest, ConversionsFollowOutputPixels)
{
	// A UI pixel maps to the world pixel that covers the output position of its centre, and vice versa.
	std::mt19937 rng(4242);
	for (const Size o : Outputs) {
		for (const Size u : UiSizes) {
			for (const bool integerScale : { true, false }) {
				const UiPlacement placement = ComputeUiPlacement(o, u, integerScale);
				for (const int ws : ComputeZoomLadder(o.height, 240, 1620)) {
					const WorldView view = ComputeWorldView(o, u, placement, ws, MainPanelHeight);
					for (int i = 0; i < 1000; i++) {
						const Point p { static_cast<int>(rng() % u.width), static_cast<int>(rng() % u.height) };
						const Point q = UiToWorld(view, p);
						const double outX = view.uiOriginPx.x + (p.x + 0.5) * view.uiScale;
						const double outY = view.uiOriginPx.y + (p.y + 0.5) * view.uiScale;
						EXPECT_LE(view.originPx.x + q.x * ws, outX);
						EXPECT_GT(view.originPx.x + (q.x + 1) * ws, outX);
						EXPECT_LE(view.originPx.y + q.y * ws, outY);
						EXPECT_GT(view.originPx.y + (q.y + 1) * ws, outY);

						const Point w { static_cast<int>(rng() % view.size.width), static_cast<int>(rng() % view.size.height) };
						const Point v = WorldToUi(view, w);
						const double worldOutX = view.originPx.x + (w.x + 0.5) * ws;
						const double worldOutY = view.originPx.y + (w.y + 0.5) * ws;
						EXPECT_LE(view.uiOriginPx.x + v.x * static_cast<double>(view.uiScale), worldOutX);
						EXPECT_GT(view.uiOriginPx.x + (v.x + 1) * static_cast<double>(view.uiScale), worldOutX);
						EXPECT_LE(view.uiOriginPx.y + v.y * static_cast<double>(view.uiScale), worldOutY);
						EXPECT_GT(view.uiOriginPx.y + (v.y + 1) * static_cast<double>(view.uiScale), worldOutY);
					}
				}
			}
		}
	}
}

TEST(WorldViewTest, RoundTrips)
{
	// Note: the bounds are ceil(s_u / ws) for world -> UI -> world (a UI pixel spans s_u / ws world pixels)
	// and ceil(ws / s_u) for UI -> world -> UI (a world pixel spans ws / s_u UI pixels).
	std::mt19937 rng(20261002);
	for (const Size o : Outputs) {
		for (const Size u : UiSizes) {
			for (const bool integerScale : { true, false }) {
				const UiPlacement placement = ComputeUiPlacement(o, u, integerScale);
				std::vector<int> zoomLevels = ComputeZoomLadder(o.height, 240, 1620);
				zoomLevels.push_back(7); // not on any ladder
				for (const int ws : zoomLevels) {
					SCOPED_TRACE(testing::Message() << "O=" << o.width << "x" << o.height << " U=" << u.width << "x" << u.height
					                                << " integer=" << integerScale << " ws=" << ws);
					const WorldView view = ComputeWorldView(o, u, placement, ws, MainPanelHeight);
					const int worldBound = CeilRatio(view.uiScale, ws);
					const int uiBound = CeilRatio(ws, view.uiScale);
					int worldError = 0;
					int uiError = 0;
					for (int i = 0; i < 10000; i++) {
						const Point q { static_cast<int>(rng() % view.size.width), static_cast<int>(rng() % view.size.height) };
						const Point q2 = UiToWorld(view, WorldToUi(view, q));
						worldError = std::max({ worldError, std::abs(q2.x - q.x), std::abs(q2.y - q.y) });

						const Point p { static_cast<int>(rng() % u.width), static_cast<int>(rng() % u.height) };
						const Point p2 = WorldToUi(view, UiToWorld(view, p));
						uiError = std::max({ uiError, std::abs(p2.x - p.x), std::abs(p2.y - p.y) });
					}
					EXPECT_LE(worldError, worldBound);
					EXPECT_LE(uiError, uiBound);
				}
			}
		}
	}
}

TEST(WorldViewTest, IdentityWhenScalesMatch)
{
	// 960x540 on 5K with ws = 6: world pixels are UI pixels.
	const Size o { 5760, 3240 };
	const Size u { 960, 540 };
	const WorldView view = ComputeWorldView(o, u, ComputeUiPlacement(o, u, true), 6, MainPanelHeight);
	for (int y = 0; y < u.height; y += 7) {
		for (int x = 0; x < u.width; x += 5) {
			ASSERT_EQ(UiToWorld(view, { x, y }), Point(x, y));
			ASSERT_EQ(WorldToUi(view, { x, y }), Point(x, y));
		}
	}
}

TEST(WorldViewTest, Anchor)
{
	for (const Size o : Outputs) {
		for (const Size u : UiSizes) {
			for (const bool integerScale : { true, false }) {
				const UiPlacement placement = ComputeUiPlacement(o, u, integerScale);
				for (const int ws : ComputeZoomLadder(o.height, 240, 1620)) {
					const WorldView view = ComputeWorldView(o, u, placement, ws, MainPanelHeight);
					EXPECT_EQ(view.anchor, UiToWorld(view, { u.width / 2, (u.height - MainPanelHeight) / 2 }));
				}
			}
		}
	}

	// Equal to the legacy anchor when the world is drawn at the UI scale.
	const Size o { 5760, 3240 };
	const Size u { 960, 540 };
	const UiPlacement placement = ComputeUiPlacement(o, u, true);
	EXPECT_EQ(ComputeWorldView(o, u, placement, 6, MainPanelHeight).anchor, Point(480, 206));
	EXPECT_EQ(ComputeLegacyWorldView(u, MainPanelHeight).anchor, Point(480, 206));
	EXPECT_EQ(ComputeWorldView(o, u, placement, 3, MainPanelHeight).anchor, Point(961, 413));
	EXPECT_EQ(ComputeWorldView(o, u, placement, 12, MainPanelHeight).anchor, Point(240, 103));
}

TEST(WorldViewTest, LegacyView)
{
	const WorldView view = ComputeLegacyWorldView({ 640, 480 }, MainPanelHeight);
	EXPECT_FALSE(view.layered);
	EXPECT_EQ(view.ws, 1);
	EXPECT_EQ(view.size, Size(640, 480));
	EXPECT_EQ(view.anchor, Point(320, 176));
	EXPECT_EQ(UiToWorld(view, { 17, 33 }), Point(17, 33));
	EXPECT_EQ(WorldToUi(view, { 17, 33 }), Point(17, 33));
}

TEST(WorldViewTest, RuntimeStaysLegacyWhenHeadless)
{
	// Tests run headless without a renderer, so the layered renderer is never active.
	EXPECT_FALSE(RecalcWorldView());
	EXPECT_FALSE(IsLayeredActive());
	EXPECT_FALSE(GetWorldView().layered);

	const Size o { 5760, 3240 };
	const Size u { 960, 540 };
	const WorldView layered = ComputeWorldView(o, u, ComputeUiPlacement(o, u, true), 6, MainPanelHeight);
	SetWorldViewForTesting(layered);
	EXPECT_TRUE(IsLayeredActive());
	EXPECT_EQ(GetWorldView(), layered);

	// Recalculating without a renderer goes back to the legacy view.
	EXPECT_TRUE(RecalcWorldView());
	EXPECT_FALSE(IsLayeredActive());
	EXPECT_FALSE(GetWorldView().layered);
}

#if !defined(USE_SDL1) && !defined(USE_SDL3)
TEST(WorldViewTest, Options)
{
	GraphicsOptions &graphics = GetOptions().Graphics;
	EXPECT_TRUE(*graphics.independentZoom);
	EXPECT_EQ(*graphics.worldZoom, 0);
	EXPECT_EQ(*graphics.worldZoomMinHeight, 240);
	EXPECT_EQ(*graphics.worldZoomMaxHeight, 1620);

	// Zoom actions do nothing while the layered renderer is not active.
	WorldZoomIn();
	WorldZoomOut();
	WorldZoomToggle();
	EXPECT_EQ(*graphics.worldZoom, 0);

	// Option changes without a renderer keep the legacy path.
	graphics.worldZoom.SetValue(4);
	EXPECT_FALSE(IsLayeredActive());
	graphics.worldZoom.SetValue(0);
}

TEST(WorldViewTest, DemoOverridesIndependentZoom)
{
	GetOptions().Graphics.independentZoom.SetValue(true);
	demo::OverrideOptions();
	EXPECT_FALSE(*GetOptions().Graphics.independentZoom);
	EXPECT_FALSE(IsLayeredActive());
	GetOptions().Graphics.independentZoom.SetValue(true);
}
#endif

} // namespace
} // namespace devilution
