/**
 * @file world_picking_test.cpp
 *
 * Mouse picking of the layered renderer ("Independent Zoom"): WorldPointToTile() must be the exact inverse of
 * the tile layout the renderer uses (GetScreenPosition()), at every world zoom level.
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/control.hpp"
#include "cursor.h"
#include "diablo.h"
#include "engine/direction.hpp"
#include "engine/displacement.hpp"
#include "engine/point.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/world_view.hpp"
#include "engine/size.hpp"
#include "inv.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "nthread.h"
#include "options.h"
#include "player.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {

constexpr Size Output5K { 5760, 3240 };
constexpr Point ViewCentre { 56, 56 };
/** @brief Offset from GetScreenPosition() to the diamond centre used by M1-T2 (the upper of the two centre pixels). */
constexpr Displacement DiamondCentre { TILE_WIDTH / 2, -TILE_HEIGHT / 2 };

struct Pose {
	bool walking;
	Direction direction;
	int8_t frame;
	uint8_t progressToNextGameTick;
};

std::string Describe(const Pose &pose)
{
	if (!pose.walking)
		return "standing";
	return "walking dir=" + std::to_string(static_cast<int>(pose.direction)) + " frame=" + std::to_string(pose.frame)
	    + " progress=" + std::to_string(pose.progressToNextGameTick);
}

PLR_MODE WalkMode(Direction direction)
{
	switch (direction) {
	case Direction::NorthWest:
	case Direction::North:
	case Direction::NorthEast:
		return PM_WALK_NORTHWARDS;
	case Direction::West:
	case Direction::East:
		return PM_WALK_SIDEWAYS;
	default:
		return PM_WALK_SOUTHWARDS;
	}
}

/** @brief Standing, and walking in all 8 directions at several animation frames. */
std::vector<Pose> AllPoses()
{
	std::vector<Pose> poses;
	poses.push_back({ false, Direction::South, 0, 0 });
	for (int dir = 0; dir < 8; dir++) {
		for (const int8_t frame : { 0, 3, 7 })
			poses.push_back({ true, static_cast<Direction>(dir), frame, 0 });
		poses.push_back({ true, static_cast<Direction>(dir), 5, 100 });
	}
	return poses;
}

void ApplyPose(const Pose &pose)
{
	Player &player = *MyPlayer;
	player._pdir = pose.direction;
	player.AnimInfo = {};
	ProgressToNextGameTick = pose.progressToNextGameTick;
	if (!pose.walking) {
		player._pmode = PM_STAND;
		return;
	}
	player._pmode = WalkMode(pose.direction);
	player.AnimInfo.numberOfFrames = 8;
	player.AnimInfo.ticksPerFrame = 1;
	player.AnimInfo.currentFrame = pose.frame;
	player.AnimInfo.tickCounterOfCurrentFrame = 0;
}

/** @brief The mouse offset WorldPointToTile() subtracts while walking. */
Displacement Prediction()
{
	return MyPlayer->isWalking() ? WalkingInputPrediction(*MyPlayer) : Displacement { 0, 0 };
}

bool Contains(Size size, Point p)
{
	return p.x >= 0 && p.y >= 0 && p.x < size.width && p.y < size.height;
}

/**
 * @brief Distance of the centre of pixel `p` from the centre of the diamond of `tile`, normalised so that the
 * diamond's edge is at 1. The diamond covers columns [S.x, S.x + 64) and rows [S.y - 31, S.y], S = GetScreenPosition(tile).
 */
double DiamondDistance(Point tile, Point p)
{
	const Point bottomLeft = GetScreenPosition(tile);
	const double dx = (p.x + 0.5) - (bottomLeft.x + TILE_WIDTH / 2.0);
	const double dy = (p.y + 0.5) - (bottomLeft.y - (TILE_HEIGHT / 2.0 - 1));
	return std::abs(dx) / (TILE_WIDTH / 2.0) + std::abs(dy) / (TILE_HEIGHT / 2.0);
}

bool IsNeighbour(Point a, Point b)
{
	return std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1;
}

class WorldPickingTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[MyPlayerId];
		*MyPlayer = {};
		MyPlayer->position.tile = ViewCentre;
		ViewPosition = ViewCentre;
		leveltype = DTYPE_TOWN;
		ProgressToNextGameTick = 0;
		GetOptions().Graphics.zoom.SetValue(false);
		ApplyPose({ false, Direction::South, 0, 0 });
	}

	void TearDown() override
	{
		invflag = false;
		ProgressToNextGameTick = 0;
		UseLegacyView();
	}

	static void SetUiSize(Size ui)
	{
		gnScreenWidth = ui.width;
		gnScreenHeight = ui.height;
		CalculatePanelAreas();
	}

	static WorldView UseLayeredView(Size output, Size ui, bool integerScale, int ws)
	{
		SetUiSize(ui);
		const WorldView view = ComputeWorldView(output, ui, ComputeUiPlacement(output, ui, integerScale), ws, GetMainPanel().size.height);
		SetWorldViewForTesting(view);
		CalcViewportGeometry();
		return view;
	}

	static void UseLegacyView()
	{
		SetWorldViewForTesting(ComputeLegacyWorldView({ gnScreenWidth, gnScreenHeight }, GetMainPanel().size.height));
		CalcViewportGeometry();
	}

	/** @brief Tiles whose diamond centre C(t) = GetScreenPosition(t) + (32, -16) lies inside the world surface. */
	static std::vector<Point> VisibleTiles(const WorldView &view)
	{
		std::vector<Point> tiles;
		for (int x = 0; x < MAXDUNX; x++) {
			for (int y = 0; y < MAXDUNY; y++) {
				if (Contains(view.size, GetScreenPosition({ x, y }) + DiamondCentre))
					tiles.push_back({ x, y });
			}
		}
		return tiles;
	}
};

struct UiCase {
	Size ui;
	bool integerScale;
};

const UiCase UiCases[] = {
	{ { 960, 540 }, true },   // s_u = 6
	{ { 853, 480 }, true },   // s_u = 6, side bars
	{ { 853, 480 }, false },  // s_u = 6.75
	{ { 3840, 2160 }, false } // s_u = 1.5
};

TEST_F(WorldPickingTest, LadderIsTheExpected5KLadder)
{
	const std::vector<int> ladder = ComputeZoomLadder(Output5K.height, 240, 1620);
	EXPECT_EQ(ladder, (std::vector<int> { 2, 3, 4, 5, 6, 8, 10, 12 }));
}

TEST_F(WorldPickingTest, TileCentresMapToTheirTile)
{
	for (const int ws : ComputeZoomLadder(Output5K.height, 240, 1620)) {
		for (const UiCase &uiCase : UiCases) {
			const WorldView view = UseLayeredView(Output5K, uiCase.ui, uiCase.integerScale, ws);
			for (const Pose &pose : AllPoses()) {
				ApplyPose(pose);
				SCOPED_TRACE(testing::Message() << "ws=" << ws << " U=" << uiCase.ui.width << "x" << uiCase.ui.height
				                                << " integer=" << uiCase.integerScale << " " << Describe(pose));
				const Displacement prediction = Prediction();
				const std::vector<Point> tiles = VisibleTiles(view);
				// The world surface is covered by about W.w * W.h / (32 * 32) diamonds
				ASSERT_GE(static_cast<int>(tiles.size()), view.size.width * view.size.height / (TILE_WIDTH * TILE_HEIGHT / 2) * 9 / 10);
				int failures = 0;
				for (const Point tile : tiles) {
					const Point centre = GetScreenPosition(tile) + DiamondCentre + prediction;
					bool flipflag = false;
					if (WorldPointToTile(centre, flipflag) != tile)
						failures++;
					// The other centre pixel
					if (WorldPointToTile(centre + Displacement { 0, 1 }, flipflag) != tile)
						failures++;
				}
				EXPECT_EQ(failures, 0);
			}
		}
	}
}

TEST_F(WorldPickingTest, RandomPixelsLieInTheReturnedDiamond)
{
	std::mt19937 rng(1234);
	double worst = 0;
	for (const int ws : ComputeZoomLadder(Output5K.height, 240, 1620)) {
		const WorldView view = UseLayeredView(Output5K, { 960, 540 }, true, ws);
		std::uniform_int_distribution<int> randomX(0, view.size.width - 1);
		std::uniform_int_distribution<int> randomY(0, view.size.height - 1);
		for (const Pose &pose : AllPoses()) {
			ApplyPose(pose);
			SCOPED_TRACE(testing::Message() << "ws=" << ws << " " << Describe(pose));
			const Displacement prediction = Prediction();
			const int samples = pose.walking ? 2000 : 20000;
			int failures = 0;
			for (int i = 0; i < samples; i++) {
				const Point pixel { randomX(rng), randomY(rng) };
				bool flipflag = false;
				const Point tile = WorldPointToTile(pixel, flipflag);
				// Edge pixels may go to either neighbour: allow 1/16 for the integer diagonals of the cell split
				const double distance = DiamondDistance(tile, pixel - prediction);
				worst = std::max(worst, distance);
				if (distance > 1 + 1.0 / 16)
					failures++;
			}
			EXPECT_EQ(failures, 0);
		}
	}
	EXPECT_GT(worst, 0.9);
}

TEST_F(WorldPickingTest, FlipflagMatchesTheHalfOfTheDiamond)
{
	const WorldView view = UseLayeredView(Output5K, { 960, 540 }, true, 3);
	std::mt19937 rng(99);
	std::uniform_int_distribution<int> randomX(0, view.size.width - 1);
	std::uniform_int_distribution<int> randomY(0, view.size.height - 1);
	int failures = 0;
	for (int i = 0; i < 20000; i++) {
		const Point pixel { randomX(rng), randomY(rng) };
		bool flipflag = false;
		const Point tile = WorldPointToTile(pixel, flipflag);
		const int centreX = GetScreenPosition(tile).x + TILE_WIDTH / 2;
		// flipflag is set in the left half of the diamond (TrySelect* then look at the tile to the south-west)
		if (flipflag != (pixel.x < centreX))
			failures++;
	}
	EXPECT_EQ(failures, 0);
}

TEST_F(WorldPickingTest, PickingThroughTheUi)
{
	for (const int ws : ComputeZoomLadder(Output5K.height, 240, 1620)) {
		for (const UiCase &uiCase : UiCases) {
			const WorldView view = UseLayeredView(Output5K, uiCase.ui, uiCase.integerScale, ws);
			for (const Pose &pose : AllPoses()) {
				ApplyPose(pose);
				SCOPED_TRACE(testing::Message() << "ws=" << ws << " U=" << uiCase.ui.width << "x" << uiCase.ui.height
				                                << " integer=" << uiCase.integerScale << " " << Describe(pose));
				const Displacement prediction = Prediction();
				int total = 0;
				int exact = 0;
				int notNeighbour = 0;
				for (const Point tile : VisibleTiles(view)) {
					const Point uiPoint = WorldToUi(view, GetScreenPosition(tile) + DiamondCentre + prediction);
					if (!Contains(view.uiSize, uiPoint))
						continue;
					bool flipflag = false;
					const Point picked = WorldPointToTile(UiToWorld(view, uiPoint), flipflag);
					total++;
					if (picked == tile)
						exact++;
					else if (!IsNeighbour(picked, tile))
						notNeighbour++;
				}
				ASSERT_GT(total, 0);
				EXPECT_EQ(notNeighbour, 0);
				if (ws >= 3) {
					EXPECT_GE(exact * 100, total * 99) << exact << " of " << total;
				}
			}
		}
	}
}

TEST_F(WorldPickingTest, HeroStaysAnchoredAboveThePanel)
{
	for (const int ws : ComputeZoomLadder(Output5K.height, 240, 1620)) {
		for (const UiCase &uiCase : UiCases) {
			const WorldView view = UseLayeredView(Output5K, uiCase.ui, uiCase.integerScale, ws);
			SCOPED_TRACE(testing::Message() << "ws=" << ws << " U=" << uiCase.ui.width << "x" << uiCase.ui.height
			                                << " integer=" << uiCase.integerScale);
			const Point heroBottomLeft = GetScreenPosition(ViewPosition);
			// The middle of the hero's diamond is exactly the anchor
			const Displacement heroCentre { TILE_WIDTH / 2, -(TILE_HEIGHT / 2 - 1) };
			EXPECT_EQ(heroBottomLeft + heroCentre, view.anchor);

			const Point uiAnchor { uiCase.ui.width / 2, (uiCase.ui.height - GetMainPanel().size.height) / 2 };
			const double worldPixelInUi = ws / view.uiScale;
			// Continuous position of the middle of the hero's diamond in UI pixels, against the centre of uiAnchor:
			// within one UI pixel, or within one world pixel when world pixels are bigger (the hero moves in whole world pixels)
			const double tolerance = std::max(1.0, worldPixelInUi);
			const double heroX = (view.originPx.x + view.anchor.x * ws - view.uiOriginPx.x) / view.uiScale;
			const double heroY = (view.originPx.y + view.anchor.y * ws - view.uiOriginPx.y) / view.uiScale;
			EXPECT_LE(std::abs(heroX - (uiAnchor.x + 0.5)), tolerance);
			EXPECT_LE(std::abs(heroY - (uiAnchor.y + 0.5)), tolerance);

			// M1-T2: the diamond centre pixel C = S + (32, -16) is within 1 UI pixel of the anchor whenever a world pixel is
			// not bigger than a UI pixel
			const Point heroCentreUi = WorldToUi(view, heroBottomLeft + DiamondCentre);
			if (worldPixelInUi <= 1) {
				EXPECT_LE(std::abs(heroCentreUi.x - uiAnchor.x), 1);
				EXPECT_LE(std::abs(heroCentreUi.y - uiAnchor.y), 1);
			} else {
				const int coarse = static_cast<int>(std::ceil(1.5 * worldPixelInUi));
				EXPECT_LE(std::abs(heroCentreUi.x - uiAnchor.x), coarse);
				EXPECT_LE(std::abs(heroCentreUi.y - uiAnchor.y), coarse);
			}
		}
	}
}

TEST_F(WorldPickingTest, CheckCursMoveUsesTheLayeredPicking)
{
	for (const int ws : { 2, 3, 6, 12 }) {
		const WorldView view = UseLayeredView(Output5K, { 960, 540 }, true, ws);
		for (const Pose &pose : { Pose { false, Direction::South, 0, 0 }, Pose { true, Direction::NorthEast, 3, 0 } }) {
			ApplyPose(pose);
			SCOPED_TRACE(testing::Message() << "ws=" << ws << " " << Describe(pose));
			int failures = 0;
			int checked = 0;
			for (int y = 0; y < view.uiSize.height; y += 3) {
				for (int x = 0; x < view.uiSize.width; x += 5) {
					MousePosition = { x, y };
					if (GetMainPanel().contains(MousePosition))
						continue;
					bool flipflag = false;
					const Point expected = WorldPointToTile(UiToWorld(view, MousePosition), flipflag);
					cursPosition = { -1, -1 };
					CheckCursMove();
					checked++;
					if (cursPosition != expected)
						failures++;
				}
			}
			EXPECT_GT(checked, 10000);
			EXPECT_EQ(failures, 0);
		}
	}
}

/**
 * With a 1:1 view (W = U, one output pixel per UI and world pixel) the layered geometry and picking must agree
 * with the legacy renderer, for UI sizes whose legacy picking is exact (at 960x540 the legacy chain picks a
 * neighbouring tile for a few percent of the pixels).
 */
TEST_F(WorldPickingTest, IdentityViewMatchesLegacy)
{
	for (const Size ui : { Size { 1920, 1080 }, Size { 1280, 720 } }) {
		for (const Pose &pose : AllPoses()) {
			SCOPED_TRACE(testing::Message() << "U=" << ui.width << "x" << ui.height << " " << Describe(pose));
			ApplyPose(pose);
			SetUiSize(ui);

			UseLegacyView();
			int legacyColumns;
			int legacyRows;
			TilesInView(&legacyColumns, &legacyRows);
			int legacyOffsetX;
			int legacyOffsetY;
			CalcTileOffset(&legacyOffsetX, &legacyOffsetY);
			const int legacyCoveredRows = RowsCoveredByPanel();
			std::vector<Point> legacyPositions;
			for (int x = 0; x < MAXDUNX; x++) {
				for (int y = 0; y < MAXDUNY; y++)
					legacyPositions.push_back(GetScreenPosition({ x, y }));
			}
			std::mt19937 rng(7);
			std::uniform_int_distribution<int> randomX(0, ui.width - 1);
			std::uniform_int_distribution<int> randomY(0, ui.height - 1);
			std::vector<Point> mousePositions;
			// Every pixel of a few tiles around the hero and in the top-left corner, and random pixels elsewhere
			for (int y = ui.height / 2 - 160; y < ui.height / 2 - 32; y++) {
				for (int x = ui.width / 2 - 128; x < ui.width / 2 + 128; x++)
					mousePositions.push_back({ x, y });
			}
			for (int y = 0; y < 2 * TILE_HEIGHT; y++) {
				for (int x = 0; x < 2 * TILE_WIDTH; x++)
					mousePositions.push_back({ x, y });
			}
			for (int i = 0; i < 5000; i++)
				mousePositions.push_back({ randomX(rng), randomY(rng) });
			std::erase_if(mousePositions, [](Point p) { return GetMainPanel().contains(p); });
			std::vector<Point> legacyTiles;
			for (const Point mouse : mousePositions) {
				MousePosition = mouse;
				CheckCursMove();
				legacyTiles.push_back(cursPosition);
			}

			const WorldView view = UseLayeredView(ui, ui, true, 1);
			ASSERT_EQ(view.size, ui);
			int columns;
			int rows;
			TilesInView(&columns, &rows);
			EXPECT_EQ(columns, legacyColumns);
			EXPECT_EQ(rows, legacyRows);
			int offsetX;
			int offsetY;
			CalcTileOffset(&offsetX, &offsetY);
			EXPECT_EQ(offsetX, legacyOffsetX);
			EXPECT_EQ(offsetY, legacyOffsetY);
			EXPECT_EQ(RowsCoveredByPanel(), legacyCoveredRows);
			int positionMismatches = 0;
			size_t i = 0;
			for (int x = 0; x < MAXDUNX; x++) {
				for (int y = 0; y < MAXDUNY; y++) {
					if (GetScreenPosition({ x, y }) != legacyPositions[i++])
						positionMismatches++;
				}
			}
			EXPECT_EQ(positionMismatches, 0);
			int pickMismatches = 0;
			for (size_t j = 0; j < mousePositions.size(); j++) {
				MousePosition = mousePositions[j];
				CheckCursMove();
				if (cursPosition == legacyTiles[j])
					continue;
				// While walking, the legacy chain truncates negative intermediate coordinates near the left and top edges
				// and may pick a tile whose diamond does not contain the pixel. The layered pick must still contain it.
				const Point pixel = mousePositions[j];
				const bool nearTopLeftEdge = pixel.x < TILE_WIDTH || pixel.y < TILE_HEIGHT;
				if (!pose.walking || !nearTopLeftEdge || DiamondDistance(cursPosition, pixel - Prediction()) > 1 + 1.0 / 16)
					pickMismatches++;
			}
			EXPECT_EQ(pickMismatches, 0) << "of " << mousePositions.size();
		}
	}
}

TEST_F(WorldPickingTest, LayeredGeometryUsesTheWorldSurface)
{
	const WorldView view = UseLayeredView(Output5K, { 960, 540 }, true, 3);
	ASSERT_EQ(view.size, (Size { 1920, 1080 }));
	ASSERT_EQ(view.anchor, (Point { 961, 413 }));
	int columns;
	int rows;
	TilesInView(&columns, &rows);
	EXPECT_EQ(columns, 30);
	EXPECT_EQ(rows, 34);
	int offsetX;
	int offsetY;
	CalcTileOffset(&offsetX, &offsetY);
	EXPECT_EQ(offsetX, 0);
	EXPECT_EQ(offsetY, 4);
	// (1080 - 2 * 413) / 32: the panel (128 UI px = 256 world px) covers about 8 rows below the hero's area
	EXPECT_EQ(RowsCoveredByPanel(), 7);
}

TEST_F(WorldPickingTest, PanelsDoNotShiftTheLayeredWorld)
{
	// Panels cover the view at 640x480; the legacy renderer shifts the world, the layered one does not (M5)
	const Size ui { 640, 480 };
	SetUiSize(ui);
	ASSERT_TRUE(CanPanelsCoverView());

	UseLegacyView();
	const Point legacyClosed = GetScreenPosition(ViewPosition);
	invflag = true;
	const Point legacyOpen = GetScreenPosition(ViewPosition);
	EXPECT_NE(legacyOpen, legacyClosed);
	invflag = false;

	const WorldView view = UseLayeredView({ 1920, 1080 }, ui, true, 2);
	const Point closed = GetScreenPosition(ViewPosition);
	invflag = true;
	EXPECT_EQ(GetScreenPosition(ViewPosition), closed);
	bool flipflag = false;
	EXPECT_EQ(WorldPointToTile(view.anchor, flipflag), ViewPosition);
	invflag = false;
}

TEST_F(WorldPickingTest, WalkingInputPredictionIsSmall)
{
	for (const Pose &pose : AllPoses()) {
		if (!pose.walking)
			continue;
		ApplyPose(pose);
		SCOPED_TRACE(Describe(pose));
		const Displacement prediction = WalkingInputPrediction(*MyPlayer);
		EXPECT_LE(std::abs(prediction.deltaX), 8);
		EXPECT_LE(std::abs(prediction.deltaY), 4);
	}
}

} // namespace
} // namespace devilution
