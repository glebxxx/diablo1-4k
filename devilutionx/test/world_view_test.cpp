#include <cmath>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "engine/render/world_view.hpp"

using namespace devilution;

namespace {

constexpr int MainPanelHeight = 128;

TEST(WorldViewTest, ZoomLadder)
{
	EXPECT_EQ(ComputeZoomLadder(3240, 240, 1620), (std::vector<int> { 2, 3, 4, 5, 6, 8, 10, 12 }));
	EXPECT_EQ(ComputeZoomLadder(1080, 240, 1620), (std::vector<int> { 1, 2, 3, 4 }));
	EXPECT_EQ(ComputeZoomLadder(1440, 240, 1620), (std::vector<int> { 1, 2, 3, 4, 5, 6 }));
	EXPECT_EQ(ComputeZoomLadder(2880, 240, 1620), (std::vector<int> { 2, 3, 4, 5, 6, 8, 10, 12 }));
}

TEST(WorldViewTest, ZoomLadderNeverEmpty)
{
	EXPECT_EQ(ComputeZoomLadder(200, 240, 1620), (std::vector<int> { 1 }));
	EXPECT_EQ(ComputeZoomLadder(3240, 2000, 3000), (std::vector<int> { 6 }));
}

TEST(WorldViewTest, AutoWorldZoom)
{
	EXPECT_EQ(AutoWorldZoom(ComputeZoomLadder(3240, 240, 1620), 3240), 6);
	EXPECT_EQ(AutoWorldZoom(ComputeZoomLadder(1440, 240, 1620), 1440), 3);
	EXPECT_EQ(AutoWorldZoom(ComputeZoomLadder(1080, 240, 1620), 1080), 2);
}

TEST(WorldViewTest, ZoomSteps)
{
	const std::vector<int> ladder = ComputeZoomLadder(3240, 240, 1620);
	EXPECT_EQ(NextWorldZoomIn(ladder, 6), 8);
	EXPECT_EQ(NextWorldZoomOut(ladder, 6), 5);
	EXPECT_EQ(NextWorldZoomIn(ladder, 12), 12);
	EXPECT_EQ(NextWorldZoomOut(ladder, 2), 2);
	// A stale level snaps to the nearest entry first (ties go down).
	EXPECT_EQ(SnapToZoomLadder(ladder, 7), 6);
	EXPECT_EQ(SnapToZoomLadder(ladder, 9), 8);
	EXPECT_EQ(SnapToZoomLadder(ladder, 16), 12);
	EXPECT_EQ(SnapToZoomLadder(ladder, 1), 2);
	EXPECT_EQ(NextWorldZoomIn(ladder, 7), 8);
	EXPECT_EQ(NextWorldZoomOut(ladder, 7), 5);
}

TEST(WorldViewTest, AutoUiScale)
{
	EXPECT_EQ(AutoUiScale({ 5760, 3240 }), 6);
	EXPECT_EQ(AutoUiScale({ 5120, 2880 }), 6);
	EXPECT_EQ(AutoUiScale({ 1920, 1080 }), 2);
	EXPECT_EQ(AutoUiScale({ 640, 480 }), 1);
	EXPECT_EQ(AutoUiScale({ 1280, 1024 }), 1);
}

TEST(WorldViewTest, UiPlacementMatchesSdl2)
{
	// Integer scaling
	UiPlacement placement = ComputeUiPlacement({ 5760, 3240 }, { 960, 540 }, true);
	EXPECT_EQ(placement.scale, 6.0F);
	EXPECT_EQ(placement.origin, Point(0, 0));
	placement = ComputeUiPlacement({ 5760, 3240 }, { 3840, 2160 }, true);
	EXPECT_EQ(placement.scale, 1.0F);
	EXPECT_EQ(placement.origin, Point(960, 540));
	placement = ComputeUiPlacement({ 5760, 3240 }, { 853, 480 }, true);
	EXPECT_EQ(placement.scale, 6.0F);
	EXPECT_EQ(placement.origin, Point(321, 180));
	placement = ComputeUiPlacement({ 1280, 1024 }, { 640, 480 }, true);
	EXPECT_EQ(placement.scale, 2.0F);
	EXPECT_EQ(placement.origin, Point(0, 32));
	// Never smaller than 1
	placement = ComputeUiPlacement({ 1920, 1080 }, { 3840, 2160 }, true);
	EXPECT_EQ(placement.scale, 1.0F);
	EXPECT_EQ(placement.origin, Point(-960, -540));

	// Non-integer scaling
	placement = ComputeUiPlacement({ 5760, 3240 }, { 3840, 2160 }, false);
	EXPECT_EQ(placement.scale, 1.5F);
	EXPECT_EQ(placement.origin, Point(0, 0));
	placement = ComputeUiPlacement({ 5760, 3240 }, { 853, 480 }, false);
	EXPECT_EQ(placement.scale, 6.75F);
	EXPECT_EQ(placement.origin, Point(1, 0));
	placement = ComputeUiPlacement({ 1280, 1024 }, { 640, 480 }, false);
	EXPECT_EQ(placement.scale, 2.0F);
	EXPECT_EQ(placement.origin, Point(0, 32));
}

TEST(WorldViewTest, WorldGeometry)
{
	const Size output { 5760, 3240 };
	const Size ui { 960, 540 };
	const WorldView view = ComputeWorldView(output, ui, ComputeUiPlacement(output, ui, true), 3, MainPanelHeight);
	EXPECT_EQ(view.size, Size(1920, 1080));
	EXPECT_EQ(view.originPx, Point(0, 0));
	EXPECT_EQ(view.ws, 3);
}

TEST(WorldViewTest, OverscanCrop)
{
	// Hypothetical ws = 7 on 5120x2880: the world is cropped by at most ws - 1 pixels.
	const Size output { 5120, 2880 };
	const Size ui { 853, 480 };
	const WorldView view = ComputeWorldView(output, ui, ComputeUiPlacement(output, ui, true), 7, MainPanelHeight);
	EXPECT_EQ(view.size, Size(732, 412));
	EXPECT_LE(view.originPx.x, 0);
	EXPECT_LE(view.originPx.y, 0);
	EXPECT_GE(view.size.width * 7, output.width);
	EXPECT_GE(view.size.height * 7, output.height);
	EXPECT_LE(view.size.width * 7 - output.width, 7 - 1);
	EXPECT_LE(view.size.height * 7 - output.height, 7 - 1);
	EXPECT_LE(-view.originPx.x, 7 - 1);
	EXPECT_LE(-view.originPx.y, 7 - 1);
}

TEST(WorldViewTest, Anchor)
{
	for (const Size output : { Size { 5760, 3240 }, Size { 5120, 2880 }, Size { 1920, 1080 } }) {
		for (const Size ui : { Size { 960, 540 }, Size { 853, 480 }, Size { 3840, 2160 } }) {
			for (const bool integerScale : { true, false }) {
				const UiPlacement placement = ComputeUiPlacement(output, ui, integerScale);
				for (const int ws : ComputeZoomLadder(output.height, 240, 1620)) {
					const WorldView view = ComputeWorldView(output, ui, placement, ws, MainPanelHeight);
					EXPECT_EQ(view.anchor, UiToWorld(view, { ui.width / 2, (ui.height - MainPanelHeight) / 2 }));
				}
			}
		}
	}
}

TEST(WorldViewTest, AnchorMatchesLegacyAtSameScale)
{
	// With one world pixel per UI pixel, the anchor is the legacy hero position.
	const Size output { 5760, 3240 };
	const Size ui { 960, 540 };
	const WorldView view = ComputeWorldView(output, ui, ComputeUiPlacement(output, ui, true), 6, MainPanelHeight);
	EXPECT_EQ(view.anchor, Point(960 / 2, (540 - MainPanelHeight) / 2));
}

TEST(WorldViewTest, RoundTrip)
{
	std::mt19937 rng(42);
	for (const Size output : { Size { 5760, 3240 }, Size { 5120, 2880 }, Size { 1920, 1080 } }) {
		for (const Size ui : { Size { 960, 540 }, Size { 853, 480 }, Size { 3840, 2160 } }) {
			for (const bool integerScale : { true, false }) {
				const UiPlacement placement = ComputeUiPlacement(output, ui, integerScale);
				for (const int ws : ComputeZoomLadder(output.height, 240, 1620)) {
					const WorldView view = ComputeWorldView(output, ui, placement, ws, MainPanelHeight);
					// A world pixel is resolved up to the size of a UI pixel, and vice versa.
					const int worldTolerance = std::max(1, static_cast<int>(std::ceil(view.uiScale / ws)));
					const int uiTolerance = std::max(1, static_cast<int>(std::ceil(ws / view.uiScale)));
					std::uniform_int_distribution<int> worldX(0, view.size.width - 1);
					std::uniform_int_distribution<int> worldY(0, view.size.height - 1);
					std::uniform_int_distribution<int> uiX(0, ui.width - 1);
					std::uniform_int_distribution<int> uiY(0, ui.height - 1);
					for (int i = 0; i < 10000; ++i) {
						const Point q { worldX(rng), worldY(rng) };
						const Point q2 = UiToWorld(view, WorldToUi(view, q));
						ASSERT_LE(std::abs(q2.x - q.x), worldTolerance) << "ws=" << ws << " q=" << q;
						ASSERT_LE(std::abs(q2.y - q.y), worldTolerance) << "ws=" << ws << " q=" << q;

						const Point p { uiX(rng), uiY(rng) };
						const Point p2 = WorldToUi(view, UiToWorld(view, p));
						ASSERT_LE(std::abs(p2.x - p.x), uiTolerance) << "ws=" << ws << " p=" << p;
						ASSERT_LE(std::abs(p2.y - p.y), uiTolerance) << "ws=" << ws << " p=" << p;
					}
				}
			}
		}
	}
}

TEST(WorldViewTest, SameScaleIsIdentity)
{
	const Size output { 5760, 3240 };
	const Size ui { 960, 540 };
	const WorldView view = ComputeWorldView(output, ui, ComputeUiPlacement(output, ui, true), 6, MainPanelHeight);
	for (const Point p : { Point { 0, 0 }, Point { 959, 539 }, Point { 480, 206 } }) {
		EXPECT_EQ(UiToWorld(view, p), p);
		EXPECT_EQ(WorldToUi(view, p), p);
	}
}

} // namespace
