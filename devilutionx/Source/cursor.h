/**
 * @file cursor.h
 *
 * Interface of cursor tracking functionality.
 */
#pragma once

#include <memory>

#include "cursor_defs.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/point.hpp"
#include "engine/size.hpp"
#include "engine/surface.hpp"
#include "utils/attributes.h"

namespace devilution {

extern int pcursmonst;
extern int8_t pcursinvitem;
extern uint16_t pcursstashitem;
extern int8_t pcursitem;

struct Object; // Defined in objects.h
extern Object *ObjectUnderCursor;

struct Player; // Defined in player.h
extern const Player *PlayerUnderCursor;
extern Point cursPosition;
extern DVL_API_FOR_TEST int pcurs;

void InitCursor();
void FreeCursor();
void ResetCursor();

struct Item;
/**
 * @brief Use the item sprite as the cursor (or show the default hand cursor if the item isEmpty)
 */
void NewCursor(const Item &item);

void NewCursor(int cursId);

void InitLevelCursor();
void CheckRportal();
void CheckTown();
void CheckCursMove();

/**
 * @brief Create an 8-bit indexed surface containing the cursor sprite.
 *
 * For item cursors, includes a 1px outline. The surface uses system_palette
 * with color key 1 (transparent). The hotpoint is returned via outHotpoint.
 *
 * @return The created surface, or nullptr if the cursor should not be shown.
 */
std::unique_ptr<OwnedSurface> CreateSoftwareCursorSurface(int cursId, Point &outHotpoint);

void DrawSoftwareCursor(const Surface &out, Point position, int cursId);

void DrawItem(const Item &item, const Surface &out, Point position, ClxSprite clx);
void DrawItemOnScreen(const Item &item, Point position, ClxSprite clx);

/** Returns the sprite for the given inventory index. */
ClxSprite GetInvItemSprite(int cursId);

ClxSprite GetHalfSizeItemSprite(int cursId);
ClxSprite GetHalfSizeItemSpriteRed(int cursId);
void CreateHalfSizeItemSprites();
void FreeHalfSizeItemSprites();

/** Returns the width and height for an inventory index. */
Size GetInvItemSize(int cursId);

} // namespace devilution
