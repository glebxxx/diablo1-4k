#include <cmath>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "cursor.h"
#include "engine/direction.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/world_view.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "player.h"
#include "utils/ui_fwd.h"

using namespace devilution;

namespace {

constexpr int MainPanelHeight = 128;
constexpr Size Output { 5760, 3240 };
constexpr Size Ui { 960, 540 };
constexpr int NumWalkFrames = 8;

struct HeroState {
	bool walking;
	Direction direction;
	int8_t frame;
};

std::vector<HeroState> HeroStates()
{
	std::vector<HeroState> states { { false, Direction::South, 0 } };
	for (int dir = 0; dir < 8; ++dir) {
		for (const int8_t frame : { 0, 3, 7 }) {
			states.push_back({ true, static_cast<Direction>(dir), frame });
		}
	}
	return states;
}

std::string Describe(const HeroState &state, int ws)
{
	std::string result = "ws=" + std::to_string(ws);
	if (state.walking)
		result += " walking dir=" + std::to_string(static_cast<int>(state.direction)) + " frame=" + std::to_string(state.frame);
	else
		result += " standing";
	return result;
}

/** @brief Centre of the tile diamond in world pixels, as defined by the renderer. */
Point DiamondCentre(Point tile)
{
	return GetScreenPosition(tile) + Displacement { TILE_WIDTH / 2, -TILE_HEIGHT / 2 };
}

class WorldPickingTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		ViewPosition = { 56, 56 };
		gnScreenWidth = Ui.width;
		gnScreenHeight = Ui.height;
		gnViewportHeight = Ui.height;
		SetHero({ false, Direction::South, 0 });
	}

	void TearDown() override
	{
		SetWorldViewForTesting(WorldView {});
		CalcViewportGeometry();
		MyPlayer->_pmode = PM_STAND;
	}

	static void SetHero(const HeroState &state)
	{
		Player &player = *MyPlayer;
		player._pmode = state.walking ? PM_WALK_SIDEWAYS : PM_STAND;
		player._pdir = state.direction;
		player.AnimInfo.setNewAnimation(std::nullopt, NumWalkFrames, 1);
		player.AnimInfo.currentFrame = state.frame;
		player.AnimInfo.tickCounterOfCurrentFrame = 0;
	}

	static WorldView SetZoom(int ws)
	{
		const WorldView view = ComputeWorldView(Output, Ui, ComputeUiPlacement(Output, Ui, true), ws, MainPanelHeight);
		SetWorldViewForTesting(view);
		CalcViewportGeometry();
		return view;
	}

	static bool InWorld(const WorldView &view, Point p)
	{
		return p.x >= 0 && p.y >= 0 && p.x < view.size.width && p.y < view.size.height;
	}

	/** @brief Tiles around the view position that may be visible. */
	static std::vector<Point> CandidateTiles(const WorldView &view)
	{
		const int radius = view.size.width / TILE_WIDTH + view.size.height / TILE_HEIGHT + 4;
		std::vector<Point> tiles;
		for (int y = ViewPosition.y - radius; y <= ViewPosition.y + radius; ++y) {
			for (int x = ViewPosition.x - radius; x <= ViewPosition.x + radius; ++x) {
				if (x >= 0 && y >= 0 && x < MAXDUNX && y < MAXDUNY)
					tiles.push_back({ x, y });
			}
		}
		return tiles;
	}
};

TEST_F(WorldPickingTest, DiamondCentresMapToTheirTile)
{
	for (const int ws : ComputeZoomLadder(Output.height, 240, 1620)) {
		const WorldView view = SetZoom(ws);
		for (const HeroState &state : HeroStates()) {
			SetHero(state);
			int checked = 0;
			for (const Point tile : CandidateTiles(view)) {
				const Point centre = DiamondCentre(tile);
				if (!InWorld(view, centre))
					continue;
				bool flipflag = false;
				ASSERT_EQ(WorldPointToTile(centre, flipflag), tile) << Describe(state, ws) << " centre=" << centre;
				++checked;
			}
			EXPECT_GT(checked, 10) << Describe(state, ws);
		}
	}
}

TEST_F(WorldPickingTest, RandomPixelsLieInTheReturnedDiamond)
{
	std::mt19937 rng(1234);
	for (const int ws : ComputeZoomLadder(Output.height, 240, 1620)) {
		const WorldView view = SetZoom(ws);
		std::uniform_int_distribution<int> distX(0, view.size.width - 1);
		std::uniform_int_distribution<int> distY(0, view.size.height - 1);
		for (const HeroState &state : HeroStates()) {
			SetHero(state);
			const Displacement prediction = WalkingInputPrediction(*MyPlayer);
			for (int i = 0; i < 20000 / 25; ++i) {
				const Point pixel { distX(rng), distY(rng) };
				bool flipflag = false;
				const Point tile = WorldPointToTile(pixel, flipflag);
				if (tile.x == 0 || tile.y == 0 || tile.x == MAXDUNX - 1 || tile.y == MAXDUNY - 1)
					continue; // clamped to the map
				// Compare pixel centres with the diamond's centre (between the two widest rows).
				const Point bottomLeft = GetScreenPosition(tile);
				const double dx = (pixel.x - prediction.deltaX + 0.5) - (bottomLeft.x + TILE_WIDTH / 2);
				const double dy = (pixel.y - prediction.deltaY + 0.5) - (bottomLeft.y - TILE_HEIGHT / 2 + 1);
				// One pixel of slack: the legacy diamond rasterisation is not exactly symmetric.
				ASSERT_LE(std::abs(dx) / (TILE_WIDTH / 2) + std::abs(dy) / (TILE_HEIGHT / 2), 1.0 + 1.5 / (TILE_HEIGHT / 2))
				    << Describe(state, ws) << " pixel=" << pixel << " tile=" << tile;
			}
		}
	}
}

TEST_F(WorldPickingTest, ThroughTheUi)
{
	for (const int ws : ComputeZoomLadder(Output.height, 240, 1620)) {
		const WorldView view = SetZoom(ws);
		for (const HeroState &state : HeroStates()) {
			SetHero(state);
			int total = 0;
			int exact = 0;
			for (const Point tile : CandidateTiles(view)) {
				const Point centre = DiamondCentre(tile);
				if (!InWorld(view, centre))
					continue;
				const Point uiPosition = WorldToUi(view, centre);
				if (uiPosition.x < 0 || uiPosition.y < 0 || uiPosition.x >= Ui.width || uiPosition.y >= Ui.height)
					continue;
				bool flipflag = false;
				const Point picked = WorldPointToTile(UiToWorld(view, uiPosition), flipflag);
				++total;
				if (picked == tile) {
					++exact;
					continue;
				}
				// A UI pixel larger than a world pixel may land on a neighbour.
				ASSERT_GT(view.uiScale / ws, 1.0F) << Describe(state, ws) << " tile=" << tile;
				ASSERT_LE(std::abs(picked.x - tile.x) + std::abs(picked.y - tile.y), 2) << Describe(state, ws) << " tile=" << tile;
			}
			ASSERT_GT(total, 0);
			if (ws >= 3) {
				EXPECT_GE(exact * 100, total * 99) << Describe(state, ws);
			}
		}
	}
}

TEST_F(WorldPickingTest, HeroStaysCentredAbovePanel)
{
	for (const int ws : ComputeZoomLadder(Output.height, 240, 1620)) {
		const WorldView view = SetZoom(ws);
		SetHero({ false, Direction::South, 0 });
		const Point hero = WorldToUi(view, DiamondCentre(ViewPosition));
		// Half a world pixel of rounding on each side, at least one UI pixel.
		const int tolerance = std::max(1, static_cast<int>(std::ceil(ws / view.uiScale)));
		EXPECT_LE(std::abs(hero.x - Ui.width / 2), tolerance) << "ws=" << ws << " hero=" << hero;
		EXPECT_LE(std::abs(hero.y - (Ui.height - MainPanelHeight) / 2), tolerance) << "ws=" << ws << " hero=" << hero;
	}
}

TEST_F(WorldPickingTest, HeroTileIsPickedAtTheAnchor)
{
	for (const int ws : ComputeZoomLadder(Output.height, 240, 1620)) {
		const WorldView view = SetZoom(ws);
		SetHero({ false, Direction::South, 0 });
		bool flipflag = false;
		EXPECT_EQ(WorldPointToTile(view.anchor, flipflag), ViewPosition) << "ws=" << ws;
	}
}

} // namespace
