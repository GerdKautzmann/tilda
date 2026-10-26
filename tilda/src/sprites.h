// sprites.h - where everything lives in the two PNG files in assets/.
//
//   sprite_map.png (320 x 432 pixels): people, animals and items
//   tile_map.png   (64 x 432 pixels):  architecture and surroundings (the tiles of the world)
//
// The font, the shadows and the screen effects are drawn by the game itself. You can repaint any
// of it in a pixel editor, keep the same grid, and press F6 in the game to see the change without
// restarting. Colours may use transparency (alpha).
//
// ---- tile_map.png: x = variant * 16, y = tileId * 16; 4 variants per tile (see world.h for the IDs) ----
//   water uses its 4 variants as animation frames; the cave gate (11) uses them as: locked, open,
//   Baron's door, open; the tower cone (17) uses them as the 4 quarters of a 2 x 2 tower.
//
// ---- sprite_map.png ----
//   characters   one 64 x 96 block per person: Tilda, Tunk, Elder, Brothilda and Pip at x = 64 * index, y = 0
//                inside a block: frame f at x + 16*f, direction d at y + 24*d
//                frames: 0 standing, 1 step, 2 passing pose, 3 other step (cycled while walking)
//                directions: 0 up, 1 right, 2 down, 3 left       (each sprite is 16 x 24)
//   Raylin       a character block at (128, 112)
//   Reg, dancer, albino, Baron   character blocks at (0, 208), (64, 208), (128, 208), (192, 208)
//   barkeeper, kitchen maid, knight, servant, Dina   character blocks at (0, 304), (64, 304), (128, 304), (192, 304), (256, 304)
//   dragon       6 frames of 32 x 32 at (0, 400): walk x4, charge, spit (drawn facing right); fireball 4 frames of 8 x 8 at (192, 400)
//   slime        4 frames of 16 x 16 at (0, 96)         hop cycle
//   chicken      4 frames of 16 x 16 at (64, 96)        walk cycle, drawn facing right (flipped in code)
//   goblin       4 frames of 16 x 16 at (128, 96)       walk cycle (the cave goblins)
//   King Slime   4 frames of 32 x 32 at (0, 112)        hop cycle
//   coin         4 frames of  8 x  8 at (0, 144)        spinning
//   heart        full at (32, 144), empty at (40, 144)  8 x 8
//   fairy        3 frames of  8 x  8 at (48, 144)       wing flap
//   spoon        16 x 16 at (80, 144), spork 16 x 16 at (96, 144), short sword 16 x 16 at (112, 144)
//                drawn pointing up, the grip is at (8, 14); the game rotates them when swinging
//   cage         20 x 30 at (0, 160)
//   sheep        4 frames of 16 x 16 at (256, 208)      walk cycle, drawn facing right
#pragma once

constexpr int SHEET_W = 320;   // sprite_map.png must be at least this big
constexpr int SHEET_H = 432;
constexpr int TILE_SHEET_W = 64;   // tile_map.png must be at least this big
constexpr int TILE_SHEET_H = 432;

constexpr int CHAR_ORIGIN_X = 0;
constexpr int CHAR_BLOCK_W = 64;
constexpr int CHAR_W = 16;
constexpr int CHAR_H = 24;
enum CharIdx { CH_TILDA = 0, CH_TUNK, CH_ELDER, CH_MERCHANT, CH_PIP, CH_RAYLIN, CH_REG, CH_DANCER, CH_ALBINO, CH_BARON, CH_BARKEEP, CH_MAID, CH_KNIGHT, CH_SERVANT, CH_DINA };
constexpr int RAYLIN_X = 128, RAYLIN_Y = 112;  // Raylin's 64 x 96 block (same layout as the others)
constexpr int EXTRA_CHAR2_Y = 304;             // barkeeper, kitchen maid, knight, servant: blocks at x = 64 * (index - CH_BARKEEP)
constexpr int EXTRA_CHAR_Y = 208;              // Reg, dancer, albino and Baron: blocks at x = 64 * (index - CH_REG)

constexpr int SLIME_X = 0, SLIME_Y = 96, SLIME_SIZE = 16;
constexpr int CHICKEN_X = 64, CHICKEN_Y = 96, CHICKEN_SIZE = 16;
constexpr int GOBLIN_X = 128, GOBLIN_Y = 96, GOBLIN_SIZE = 16;
constexpr int KING_X = 0, KING_Y = 112, KING_SIZE = 32;
constexpr int COIN_X = 0, COIN_Y = 144, COIN_SIZE = 8;
constexpr int HEART_FULL_X = 32, HEART_EMPTY_X = 40, HEART_Y = 144;
constexpr int FAIRY_X = 48, FAIRY_Y = 144;
constexpr int SPOON_X = 80, SPOON_Y = 144;
constexpr int SPORK_X = 96, SPORK_Y = 144;
constexpr int SWORD_X = 112, SWORD_Y = 144;
constexpr int CAGE_X = 0, CAGE_Y = 160, CAGE_W = 20, CAGE_H = 30;
constexpr int SHEEP_X = 256, SHEEP_Y = 208, SHEEP_SIZE = 16;
constexpr int DRAGON_X = 0, DRAGON_Y = 400, DRAGON_SIZE = 32;   // 6 frames: 4 walking, 4 = charging, 5 = spitting fire
constexpr int FIREBALL_X = 192, FIREBALL_Y = 400, FIREBALL_SIZE = 8;
