// world.h - the binary world file format, shared by the game and the mkworld tool.
//
// File layout (all values are single bytes, so any hex editor works):
//
//   offset 0..3   magic        'T' 'L' 'D' 'A'
//   offset 4      version      1
//   offset 5      width        map width in tiles   (1..255)
//   offset 6      height       map height in tiles  (1..255)
//   offset 7      objectCount  number of objects    (0..255)
//   offset 8      tiles        width*height bytes, row by row, top row first
//   then          objects      objectCount records of 3 bytes: type, x, y (tile coordinates)
//
// The characters (Elder Maro, Brothilda, Reg, ...) are not objects of this file; see npcs.txt below.
//
// Tile and object IDs are listed below and in README.md.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

enum Tile : uint8_t {
  TILE_GRASS = 0,   // walkable
  TILE_TREE = 1,    // solid
  TILE_WATER = 2,   // solid
  TILE_PATH = 3,    // walkable
  TILE_WALL = 4,    // solid (brick)
  TILE_FLOOR = 5,   // walkable, inside houses (slimes will not enter)
  TILE_DOOR = 6,    // walkable (slimes will not enter)
  TILE_FLOWER = 7,  // walkable
  TILE_ROCK = 8,    // solid
  TILE_SAND = 9,    // walkable
  TILE_ROOF = 10,   // solid
  TILE_GATE = 11,   // cave gate: solid and locked until Tilda has the key, then walkable
  TILE_CAVE = 12,   // cave floor, walkable
  TILE_CAVEWALL = 13,  // cave wall, solid
  TILE_BARON = 14,  // Baron Skalman's iron door, solid
  TILE_CROOF = 15,  // castle battlement roof, solid
  TILE_CWALL = 16,  // castle wall, solid
  TILE_TOWER = 17,  // conical tower roof; towers are 2 x 2 tiles, solid
  TILE_CFLOOR = 18, // castle stone floor, walkable
  TILE_CARPET = 19, // red carpet, walkable
  TILE_TABLE = 20,  // tavern table, solid
  TILE_CGATE = 21,  // the castle gate (arch), walkable
  TILE_BED = 22,    // Princess Dina's bed, solid
  TILE_BAR = 23,    // the bar counter in the tavern, solid
  TILE_BOOKS = 24,  // a table with three open books, solid
  TILE_HILL = 25,   // a grassy hill, solid
  TILE_RFLOOR = 26, // ruin floor: looks like the castle floor, but is outdoors (no 'inside a room' view), walkable
  TILE_COUNT = 27   // any ID >= 27 is drawn as a magenta checker and is solid
};

enum ObjType : uint8_t {
  OBJ_PLAYER = 1,    // Tilda's start position
  OBJ_ELDER = 2,     // Elder Maro
  OBJ_MERCHANT = 3,  // Brothilda, sells stew
  OBJ_PIP = 4,       // Pip, the villager
  OBJ_SLIME = 5,     // slime spawn point (respawns after a while)
  OBJ_KING = 6,      // where the King Slime appears once quest 2 starts
  OBJ_CHICKEN = 7,   // a chicken. Do not hit it more than twice.
  OBJ_TUNK = 8,      // Tunk, in his cage
  OBJ_DOOR = 9,      // door link: objects of this type come in pairs (1st+2nd, 3rd+4th, ...).
                     // Walking onto one of the two tiles moves Tilda in front of the other one.
  OBJ_GOBLIN = 10,   // a goblin in the cave
  OBJ_SIGN = 11,     // invisible: talk to it (E) next to Baron Skalman's door (not used in the default world)
  OBJ_RAYLIN = 12,   // Raylin the fisherman, by the lake in Baron Skalman's realm
  OBJ_REG = 13,      // Reg, the onion farmer: sells onion stew, asks for his 5 lost sheep
  OBJ_SHEEP = 14,    // one of Reg's lost sheep (put 5 of them in the realm)
  OBJ_DANCER = 15,   // the belly dancer in the tavern
  OBJ_ALBINO = 16,   // the red-eyed albino at a tavern table
  OBJ_BARON = 17,    // Baron Skalman in the great hall of his castle
  OBJ_BARKEEP = 18,  // the barkeeper behind the bar
  OBJ_MAID = 19,     // the kitchen maid in the castle
  OBJ_KNIGHT = 20,   // a knight in armour in the throne room
  OBJ_SERVANT = 21,  // a servant in the throne room
  OBJ_DRAGON = 22,   // where the dragon waits in its lair (world object: appears once the quest reaches it)
  OBJ_DINA = 23      // Princess Dina (appears where the dragon was; created by the game, not read from npcs.txt)
};

struct Obj {
  int type, x, y;
};

struct World {
  int w = 0, h = 0;
  std::vector<uint8_t> tiles;
  std::vector<Obj> objs;

  // Anything outside the map counts as a tree, so nothing can walk off the edge.
  int at(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return TILE_TREE;
    return tiles[(size_t)y * w + x];
  }
};

inline bool loadWorld(const std::string& path, World& out, std::string& err) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::vector<uint8_t> b;
  uint8_t buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
  fclose(f);

  if (b.size() < 8 || memcmp(b.data(), "TLDA", 4) != 0) {
    err = path + ": not a world file (header must start with TLDA)";
    return false;
  }
  if (b[4] != 1) {
    err = path + ": unsupported world version " + std::to_string(b[4]);
    return false;
  }
  int w = b[5], h = b[6], count = b[7];
  if (w < 1 || h < 1) {
    err = path + ": width and height must be at least 1";
    return false;
  }
  size_t need = 8 + (size_t)w * h + (size_t)count * 3;
  if (b.size() < need) {
    err = path + ": file is too short (" + std::to_string(b.size()) + " bytes, need " +
          std::to_string(need) + ")";
    return false;
  }
  World nw;
  nw.w = w;
  nw.h = h;
  nw.tiles.assign(b.begin() + 8, b.begin() + 8 + (size_t)w * h);
  size_t p = 8 + (size_t)w * h;
  for (int i = 0; i < count; i++, p += 3) nw.objs.push_back({b[p], b[p + 1], b[p + 2]});
  out = std::move(nw);
  return true;
}

inline bool saveWorld(const std::string& path, const World& wd) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  uint8_t head[8] = {'T', 'L', 'D', 'A', 1, (uint8_t)wd.w, (uint8_t)wd.h, (uint8_t)wd.objs.size()};
  fwrite(head, 1, 8, f);
  fwrite(wd.tiles.data(), 1, wd.tiles.size(), f);
  for (const Obj& o : wd.objs) {
    uint8_t r[3] = {(uint8_t)o.type, (uint8_t)o.x, (uint8_t)o.y};
    fwrite(r, 1, 3, f);
  }
  fclose(f);
  return true;
}

// ---- the characters (npcs.txt) -----------------------------------------------------------------
// The people of the game are NOT stored in the world file: their positions live in a small text file
// next to it, one character per line:   name  x  y     (tile coordinates; # starts a comment)
// names: elder brothilda pip tunk raylin reg sheep dancer albino baron
// (NPC object types in a world file are ignored.)
inline bool isNpcType(int t) {
  return t == OBJ_ELDER || t == OBJ_MERCHANT || t == OBJ_PIP || t == OBJ_TUNK || t == OBJ_RAYLIN || t == OBJ_REG ||
         t == OBJ_SHEEP || t == OBJ_DANCER || t == OBJ_ALBINO || t == OBJ_BARON ||
         t == OBJ_BARKEEP || t == OBJ_MAID || t == OBJ_KNIGHT || t == OBJ_SERVANT || t == OBJ_DINA;
}

struct NpcName { const char* word; int type; };
static const NpcName NPC_NAMES[] = {{"elder", OBJ_ELDER}, {"brothilda", OBJ_MERCHANT}, {"pip", OBJ_PIP}, {"tunk", OBJ_TUNK},
                                    {"raylin", OBJ_RAYLIN}, {"reg", OBJ_REG}, {"sheep", OBJ_SHEEP}, {"dancer", OBJ_DANCER},
                                    {"albino", OBJ_ALBINO}, {"baron", OBJ_BARON}, {"barkeeper", OBJ_BARKEEP},
                                    {"maid", OBJ_MAID}, {"knight", OBJ_KNIGHT}, {"servant", OBJ_SERVANT}, {"dina", OBJ_DINA}};

inline std::string npcFileFor(const std::string& worldPath) {
  size_t sl = worldPath.find_last_of("/\\");
  return (sl == std::string::npos ? std::string() : worldPath.substr(0, sl + 1)) + "npcs.txt";
}

inline bool loadNpcs(const std::string& path, std::vector<Obj>& out, std::string& err) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::vector<Obj> list;
  char line[256];
  int ln = 0;
  while (fgets(line, sizeof line, f)) {
    ln++;
    char* hash = strchr(line, '#');
    if (hash) *hash = 0;
    char word[64];
    int x, y;
    int got = sscanf(line, " %63s %d %d", word, &x, &y);
    if (got <= 0) continue;  // blank or comment line
    int type = 0;
    for (const NpcName& n : NPC_NAMES)
      if (!strcmp(word, n.word)) type = n.type;
    if (got != 3 || !type || x < 0 || y < 0 || x > 255 || y > 255) {
      err = path + ": line " + std::to_string(ln) + ": expected 'name x y' with a known name";
      fclose(f);
      return false;
    }
    list.push_back({type, x, y});
  }
  fclose(f);
  out = std::move(list);
  return true;
}

inline bool saveNpcs(const std::string& path, const std::vector<Obj>& npcs) {
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return false;
  fprintf(f, "# The characters of The Legend of Tilda: one per line, 'name x y' (tile coordinates, as in world.bin).\n");
  fprintf(f, "# Names: elder brothilda pip tunk raylin reg sheep dancer albino baron.   Press F5 in the game to reload.\n");
  for (const Obj& o : npcs)
    for (const NpcName& n : NPC_NAMES)
      if (n.type == o.type) fprintf(f, "%-10s %3d %3d\n", n.word, o.x, o.y);
  fclose(f);
  return true;
}
