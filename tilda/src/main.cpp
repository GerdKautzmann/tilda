// The Legend of Tilda
//
// A top-down parody adventure in C++17 with SDL2 (2D only). There are no asset files:
// all art is drawn in code, all sound is synthesised, and the world is read from the
// small binary file assets/world.bin (see world.h and README.md for the format).
//
// Tilda has to find and rescue her little brother Tunk from King Slime.

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "font.h"
#include "world.h"
#include "story.h"

// PNG loader (public domain, single header). Only PNG support is compiled in.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "modplay.h"
#include "sprites.h"

namespace {

constexpr int T = 16;      // tile size in pixels
constexpr int VW = 320;    // game view width in pixels (scaled up to the window)
constexpr int VH = 208;    // game view height
constexpr uint32_t GOLD = 0xe3b94e;

// ============================================================================
// Random numbers
// ============================================================================
std::mt19937 gen(std::random_device{}());
float rndf() { return std::uniform_real_distribution<float>(0.f, 1.f)(gen); }

struct Rng {  // small deterministic generator for tile art
  uint32_t a;
  explicit Rng(uint32_t seed) : a(seed) {}
  float operator()() {
    a += 0x6D2B79F5u;
    uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (float)(t ^ (t >> 14)) / 4294967296.0f;
  }
};

// ============================================================================
// Audio: a tiny synthesiser. Voices sweep from one pitch to another.
// ============================================================================
struct Voice {
  bool on = false;
  float delay = 0, f0 = 0, f1 = 0, dur = 0, t = 0, vol = 0, ph = 0;
  int wave = 0;  // 0 square, 1 saw, 2 triangle, 3 noise
};
Voice voices[16];
SDL_AudioDeviceID audioDev = 0;
int audioRate = 44100;
bool muted = false;
uint32_t noiseState = 1;
ModPlayer tracks[3];             // music1.mod (travelling tune), music2.mod (second song), music3.mod (caves only)
volatile int trackSel = 0;       // which of them the audio callback plays
int tuneChoice = 0;              // 0 / 1: the song for outside; the dancer's tip jar switches between them
float musicGain = 0.25f;
float sfxGain = 1.0f;
int musicStep = 7, sfxStep = 8;  // settings page: 0..10 (7 and 8 are the original loudness)
void applyVolumes() { musicGain = 0.25f * musicStep / 7.0f; sfxGain = sfxStep / 8.0f; }

void audioCallback(void*, Uint8* stream, int len) {
  float* out = (float*)stream;
  int n = len / (int)sizeof(float);
  const float dt = 1.0f / (float)audioRate;
  for (int i = 0; i < n; i++) out[i] = 0;
  if (!muted) {
    int t = trackSel;
    if (t < 0 || t > 2 || !tracks[t].loaded()) t = 0;
    tracks[t].render(out, n, audioRate, musicGain);
  }
  for (int i = 0; i < n; i++) {
    float s = 0;
    for (Voice& v : voices) {
      if (!v.on) continue;
      if (v.delay > 0) { v.delay -= dt; continue; }
      float u = v.t / v.dur;
      if (u >= 1) { v.on = false; continue; }
      float f = v.f0 * std::pow(v.f1 / v.f0, u);
      v.ph += f * dt;
      if (v.ph >= 1) v.ph -= 1;
      float w;
      switch (v.wave) {
        case 0: w = v.ph < 0.5f ? 1.f : -1.f; break;
        case 1: w = 2 * v.ph - 1; break;
        case 2: w = std::fabs(4 * v.ph - 2) - 1; break;
        default:
          noiseState = noiseState * 1664525u + 1013904223u;
          w = ((noiseState >> 16) & 0x7fff) / 16384.0f - 1;
      }
      s += w * v.vol * (1 - u);
      v.t += dt;
    }
    out[i] = std::max(-1.f, std::min(1.f, out[i] + s * sfxGain));
  }
}

void addVoice(float delay, float f0, float f1, float dur, float vol, int wave) {
  for (Voice& v : voices) {
    if (v.on) continue;
    v = Voice();
    v.on = true; v.delay = delay; v.f0 = f0; v.f1 = f1; v.dur = dur; v.vol = vol; v.wave = wave;
    return;
  }
}

enum Sfx { SFX_SWING, SFX_HIT, SFX_COIN, SFX_HURT, SFX_TALK, SFX_HEAL, SFX_CLUCK, SFX_JINGLE };

void sfx(Sfx k) {
  if (!audioDev || muted) return;
  SDL_LockAudioDevice(audioDev);
  switch (k) {
    case SFX_SWING: addVoice(0, 260, 110, 0.09f, 0.07f, 1); break;
    case SFX_HIT: addVoice(0, 160, 70, 0.10f, 0.09f, 0); break;
    case SFX_COIN: addVoice(0, 880, 1320, 0.09f, 0.06f, 0); break;
    case SFX_HURT: addVoice(0, 140, 50, 0.22f, 0.10f, 1); break;
    case SFX_TALK: addVoice(0, 520, 470, 0.03f, 0.025f, 2); break;
    case SFX_HEAL: addVoice(0, 400, 800, 0.20f, 0.07f, 2); break;
    case SFX_CLUCK: addVoice(0, 700, 420, 0.08f, 0.05f, 0); addVoice(0.09f, 620, 380, 0.07f, 0.05f, 0); break;
    case SFX_JINGLE: {
      const float notes[] = {523, 659, 784, 1047};
      for (int i = 0; i < 4; i++) addVoice(i * 0.09f, notes[i], notes[i], 0.14f, 0.07f, 0);
      break;
    }
  }
  SDL_UnlockAudioDevice(audioDev);
}

// ============================================================================
// Drawing helpers
// ============================================================================
SDL_Renderer* ren = nullptr;
SDL_Texture *sheetTex = nullptr, *sheetWhite = nullptr;  // assets/sprite_map.png (and a white silhouette copy for hit flashes)
SDL_Texture* tileTex = nullptr;                          // assets/tile_map.png: the tiles of the world
SDL_Texture *fontTex = nullptr, *vigTex = nullptr, *frameTex = nullptr;

inline void R(int x, int y, int w, int h, uint32_t rgb, int a = 255) {
  SDL_SetRenderDrawColor(ren, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (Uint8)a);
  SDL_Rect r = {x, y, w, h};
  SDL_RenderFillRect(ren, &r);
}

// --- text ---
int textW(const std::string& s, int sc = 1) { return (int)s.size() * 6 * sc - sc; }

void drawText(const std::string& s, int x, int y, uint32_t col, int sc = 1, int align = 0) {
  if (align == 1) x -= textW(s, sc) / 2;
  else if (align == 2) x -= textW(s, sc);
  auto pass = [&](int ox, int oy, uint32_t c) {
    SDL_SetTextureColorMod(fontTex, (c >> 16) & 255, (c >> 8) & 255, c & 255);
    for (size_t i = 0; i < s.size(); i++) {
      unsigned char ch = (unsigned char)s[i];
      if (ch < 32 || ch > 126) ch = '?';
      SDL_Rect src = {(ch - 32) * 6, 0, 5, 8};
      SDL_Rect dst = {ox + (int)i * 6 * sc, oy, 5 * sc, 8 * sc};
      SDL_RenderCopy(ren, fontTex, &src, &dst);
    }
  };
  pass(x + 1, y + 1, 0x000000);
  pass(x, y, col);
}

std::vector<std::string> wrapText(const std::string& s, size_t maxc) {
  std::vector<std::string> out;
  std::string cur;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = s.find(' ', i);
    std::string w = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
    i = (j == std::string::npos) ? s.size() : j + 1;
    if (cur.empty()) cur = w;
    else if (cur.size() + 1 + w.size() <= maxc) cur += " " + w;
    else { out.push_back(cur); cur = w; }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// --- the two PNG files (sprite map and tile map): loaded from disk so they can be repainted without touching the code ---
// Makes the textures for one image. 'white' (optional) gets a white silhouette copy that is drawn over a sprite when it is hit.
bool loadImage(const std::string& path, int minW, int minH, const char* what, SDL_Texture** color, SDL_Texture** white,
               std::string& err) {
  int w = 0, h = 0, n = 0;
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (!data) {
    err = path + ": " + stbi_failure_reason();
    return false;
  }
  if (w < minW || h < minH) {
    stbi_image_free(data);
    err = path + ": " + what + " is " + std::to_string(w) + "x" + std::to_string(h) + " pixels, it must be at least " +
          std::to_string(minW) + "x" + std::to_string(minH);
    return false;
  }
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
  SDL_Texture* c = s ? SDL_CreateTextureFromSurface(ren, s) : nullptr;
  if (s) SDL_FreeSurface(s);
  SDL_Texture* wt = nullptr;
  if (c && white) {
    for (int i = 0; i < w * h; i++)
      if (data[i * 4 + 3]) data[i * 4] = data[i * 4 + 1] = data[i * 4 + 2] = 255;
    SDL_Surface* ws = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    wt = ws ? SDL_CreateTextureFromSurface(ren, ws) : nullptr;
    if (ws) SDL_FreeSurface(ws);
  }
  stbi_image_free(data);
  if (!c || (white && !wt)) {
    if (c) SDL_DestroyTexture(c);
    if (wt) SDL_DestroyTexture(wt);
    err = path + ": could not create textures (" + SDL_GetError() + ")";
    return false;
  }
  SDL_SetTextureBlendMode(c, SDL_BLENDMODE_BLEND);
  if (wt) SDL_SetTextureBlendMode(wt, SDL_BLENDMODE_BLEND);
  if (*color) SDL_DestroyTexture(*color);
  *color = c;
  if (white) {
    if (*white) SDL_DestroyTexture(*white);
    *white = wt;
  }
  return true;
}

bool loadSprites(const std::string& path, std::string& err) {
  return loadImage(path, SHEET_W, SHEET_H, "sprite map", &sheetTex, &sheetWhite, err);
}

bool loadTiles(const std::string& path, std::string& err) {
  return loadImage(path, TILE_SHEET_W, TILE_SHEET_H, "tile map", &tileTex, nullptr, err);
}

void buildTextures() {
  // font atlas: 95 glyphs, 6x8 cells, white on transparent so it can be tinted
  SDL_Surface* fs = SDL_CreateRGBSurfaceWithFormat(0, 95 * 6, 8, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_FillRect(fs, nullptr, 0);
  Uint32* px = (Uint32*)fs->pixels;
  int pitch = fs->pitch / 4;
  for (int g = 0; g < 95; g++)
    for (int c = 0; c < 5; c++)
      for (int r = 0; r < 8; r++)
        if (FONT5X7[g][c] & (1 << r)) px[r * pitch + g * 6 + c] = 0xffffffffu;
  fontTex = SDL_CreateTextureFromSurface(ren, fs);
  SDL_SetTextureBlendMode(fontTex, SDL_BLENDMODE_BLEND);
  SDL_FreeSurface(fs);

  // vignette: darkens the corners a little
  SDL_Surface* vs = SDL_CreateRGBSurfaceWithFormat(0, VW, VH, 32, SDL_PIXELFORMAT_ARGB8888);
  Uint32* vp = (Uint32*)vs->pixels;
  int vpitch = vs->pitch / 4;
  for (int y = 0; y < VH; y++)
    for (int x = 0; x < VW; x++) {
      float dx = (x - VW / 2.f) / (VW * 0.62f), dy = (y - VH / 2.f) / (VH * 0.62f);
      float d = std::sqrt(dx * dx + dy * dy);
      float u = std::max(0.f, std::min(1.f, (d - 0.55f) / 0.7f));
      vp[y * vpitch + x] = ((Uint32)(u * u * 96) << 24);
    }
  vigTex = SDL_CreateTextureFromSurface(ren, vs);
  SDL_SetTextureBlendMode(vigTex, SDL_BLENDMODE_BLEND);
  SDL_FreeSurface(vs);

  frameTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, VW, VH);
}

// ============================================================================
// Game data
// ============================================================================
World world;
struct Fire { float x, y, vx, vy, life; };
std::vector<Fire> fires;           // the dragon's fireballs
float dragonX = 0, dragonY = 0;    // where the dragon lives (the OBJ_DRAGON marker in the world file)
bool hasDragonSpawn = false;
Story story;                       // dialogs, objectives, triggers (assets/story.txt)
std::map<std::string, int> flags;  // story flags: clue_maid, book1, ... (saved with the game)
std::vector<Obj> npcObjs;  // the characters, from npcs.txt (not part of the world file)

bool keyOwned = false;  // the cave key from Elder Maro: unlocks TILE_GATE

bool solidTile(int t) {
  return t == TILE_TREE || t == TILE_WATER || t == TILE_WALL || t == TILE_ROCK || t == TILE_ROOF ||
         t == TILE_CAVEWALL || t == TILE_BARON || (t == TILE_GATE && !keyOwned) || t == TILE_CROOF || t == TILE_CWALL ||
         t == TILE_TOWER || t == TILE_TABLE || t == TILE_BED || t == TILE_BAR || t == TILE_BOOKS || t == TILE_HILL || t >= TILE_COUNT;
}

struct Npc {
  int type;
  const char* name;
  float x, y;
  int dir;
  int sprite;  // CharIdx in sprites.h
  int id = 0;  // sheep number (0..4)
  int hitId = -1;          // the swing of Tilda that hit it last
  float slapT = 0, ramT = 0, coolT = 0;  // slapping back, ramming (sheep), pause after a fight
  float hx = 0, hy = 0;    // where a sheep lives; it walks back there after ramming
  bool back = false;
};

enum EKind { K_SLIME, K_KING, K_CHICKEN, K_GOBLIN, K_DRAGON };

struct Enemy {
  int kind = K_SLIME;
  float x = 0, y = 0, hx = 0, hy = 0;
  int hw = 5, hh = 4;
  float hp = 2, max = 2;  // health: a hit takes away damage points (a slime has 2, King Slime 10, a goblin 5)
  float hop = 0, dx = 0, dy = 0, wt = 0, flash = 0, kx = 0, ky = 0, kt = 0, step = 0;
  int hitId = 0, spawn = -1, oid = -1;  // oid: index of the world object it came from (goblins)
  bool dead = false, angry = false, faceRight = true, moving = false;
  int st = 0, rams = 0, shots = 0;      // the dragon: what it is doing (0 walk, 1 aim, 2 charge, 3 rest, 4 spit fire), rams done, fireballs spat
  float stT = 0, dvx = 0, dvy = 0;       // time in this state, the direction of the charge
  float fireT = 0, spitT = 0;            // the dragon: time until the next fireball, how long the spit animation shows
  int flockHits = 0, flockSwing = -1;   // chickens: hits taken by their flock, and the last swing counted
};

// The dragon's behaviour. Defaults here; assets/dragon.txt ("name value" lines) overrides them, F5 reloads it.
struct DragonCfg {
  float hp = 20;            // health (it turns into Dina at 30 %)
  float sight = 150;        // pixels: Tilda inside this distance AND in line of sight gets fireballs
  float near = 40;          // pixels: Tilda or Tunk this close get rammed
  float fireEvery = 2.2f;   // seconds between fireballs
  float fireSpeed = 70;     // pixels per second
  int   fireCount = 1;      // fireballs per volley (several fly in a fan)
  int   fireDmg = 1;        // hearts
  float ramWarn = 0.6f;     // seconds of flashing before a ram
  float ramSpeed = 130;     // pixels per second
  float ramTime = 0.5f;     // seconds a ram lasts
  int   ramDmg = 2;         // hearts
  float rest = 2.5f;        // seconds out of breath after a ram
  int   onlyRest = 1;       // 1: it can only be hurt while out of breath after a ram, 0: always
  float homeSpeed = 40;     // pixels per second back to its place in the middle of the cave
};
DragonCfg dragonCfg;

// assets/dragon.txt: "name value" lines, '#' starts a comment. Unknown names are reported.
void loadDragonCfg(const std::string& path) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return;   // optional: the defaults above are used
  DragonCfg c;
  char buf[256];
  int ln = 0;
  while (fgets(buf, sizeof buf, f)) {
    ln++;
    char name[64]; float v;
    std::string line = buf;
    size_t h = line.find('#'); if (h != std::string::npos) line.resize(h);
    if (sscanf(line.c_str(), "%63s %f", name, &v) != 2) { if (line.find_first_not_of(" \t\r\n") != std::string::npos) fprintf(stderr, "%s:%d: cannot read the line\n", path.c_str(), ln); continue; }
    std::string n = name;
    if (n == "hp") c.hp = v; else if (n == "sight") c.sight = v; else if (n == "near") c.near = v;
    else if (n == "fire_every") c.fireEvery = v; else if (n == "fire_speed") c.fireSpeed = v;
    else if (n == "fire_count") c.fireCount = std::max(1, (int)v); else if (n == "fire_damage") c.fireDmg = (int)v;
    else if (n == "ram_warn") c.ramWarn = v; else if (n == "ram_speed") c.ramSpeed = v; else if (n == "ram_time") c.ramTime = v;
    else if (n == "ram_damage") c.ramDmg = (int)v; else if (n == "rest") c.rest = v; else if (n == "only_hurt_when_resting") c.onlyRest = (int)v;
    else if (n == "home_speed") c.homeSpeed = v;
    else fprintf(stderr, "%s:%d: unknown setting '%s'\n", path.c_str(), ln, name);
  }
  fclose(f);
  dragonCfg = c;
}

struct Spawn {
  float x, y, timer;
  bool alive;
};
struct Pickup {
  int kind;  // 0 coin, 1 heart
  float x, y, life, ph;
};
struct Part {
  float x, y, vx, vy, life;
  uint32_t col;
};
struct Player {
  float x = 0, y = 0;
  int dir = 2, hp = 5, max = 5, swingId = 0;
  float inv = 0, walk = 0, atk = 0, cd = 0, kx = 0, ky = 0, kt = 0;
  bool moving = false;
};
struct Line {
  std::string who, text;
};
struct Dialog {
  bool active = false;
  std::vector<Line> lines;
  size_t i = 0;
  float shown = 0;
  std::function<void()> onEnd;
};

enum Mode { M_TITLE, M_PLAY, M_DEAD, M_WON };

struct State {
  Mode mode = M_TITLE;
  float t = 0, winT = 0, deadT = 0, toastT = 0, fairyT = 0;
  int quest = 0;  // 0 find Elder, 1 slimes, 2 King Slime, 3 free Tunk, 4 bring Tunk home, 5 goblin cave, 6 told by Raylin, 7 met the Baron
  int weapon = 0;  // 0 none, 1 spoon, 2 Master Spork, 3 short sword
  int raylin = 0;  // how often Tilda has spoken with Raylin the fisherman (3 = he told the story)
  int strength = 0;  // Tilda's strength: added to her weapon's damage class, +1 after every boss fight
  int spare = 0;     // a weapon Tilda no longer uses that Tunk takes when he joins
  int sheepQ = 0;    // Reg's side quest: 0 not started, 1 looking for the sheep, 2 done
  int sheepMask = 0; // bit n set = sheep n is back home
  float regenT = 0, regenTick = 0;  // onion stew: seconds of regeneration left
  int kills = 0, coins = 0, chickHits = 0, near = -1;
  bool hasSword = false, lowSaid = false, sawTunk = false;
  bool god = false;  // debug: --god makes Tilda immune to damage
  std::string toast;
} S;

Player P;
Dialog dlg;
std::vector<Npc> npcs;
std::vector<Enemy> enemies;
std::vector<Spawn> spawns;
std::vector<Pickup> pickups;
std::vector<Part> parts;
struct FloatText { float x, y, life; std::string text; uint32_t col; };  // damage numbers rising off an enemy
std::vector<FloatText> floatTexts;
float kingX = 0, kingY = 0;
bool hasKingSpawn = false;
float fairyX = 0, fairyY = 0;
struct DoorLink { int x, y; };           // tile of a door object (type 9)
std::vector<DoorLink> doorLinks;         // consecutive pairs are connected
float fadeT = 0;                         // black fade after walking through a door
float lockT = 0;                         // keeps the "gate is locked" message from repeating
bool seenRealm = false;                   // Tilda has come out of the tunnel into Skalman's realm
const int REALM_ROW = 65;                // map rows from here on belong to Baron Skalman's realm
float combatT = 0;                       // > 0 while Tilda is fighting or was just hit: Tunk may join in

void setWeapon(int w) {
  S.weapon = w; S.hasSword = w > 0;
}
// damage class of a weapon: Wooden Spoon 1, Master Spork 2, Short Sword 4
int weaponClass(int w) { return w >= 3 ? 4 : (w == 2 ? 2 : (w == 1 ? 1 : 0)); }

// One hit does (weapon damage class + strength) points, minus a random 0 to 50 percent.
float rollDamage(int weaponCls, int strength) { return (float)(weaponCls + strength) * (1.0f - 0.5f * rndf()); }

// Tunk, once rescued, walks behind Tilda like a jRPG party member and helps in fights.
struct Follower {
  bool active = false;
  float x = 0, y = 0, walk = 0, atk = 0, cd = 0;
  int dir = 2, weapon = 0, swingId = 0, chat = 0;
  int strength = 0;  // like Tilda's: added to his weapon's damage class
  bool moving = false;
  std::vector<std::pair<float, float>> trail;  // where Tilda has been, newest last
} tunk;
int camX = 0, camY = 0;

// ============================================================================
// Input (keyboard, touch pad, mouse taps)
// ============================================================================
enum Act { A_UP, A_DOWN, A_LEFT, A_RIGHT, A_TALK, A_SWING, A_MUTE, A_RESTART, A_RELOAD, A_RELOAD_SPRITES, A_MENU, A_EXAMINE, A_COUNT };
bool held[A_COUNT], press[A_COUNT], prevSyn[A_COUNT];
bool touchMode = false;

struct Finger {
  long long id;
  float x, y;
};
std::vector<Finger> fingers;  // positions in window pixels

struct Btn {
  SDL_Rect r;
  int act;
  const char* label;
  int arrow;  // 0 up, 1 right, 2 down, 3 left, -1 none
};
Btn btns[9];
const int NBTN = 7;
SDL_Rect gameRect = {0, 0, VW, VH};
int outW = 960, outH = 624;

bool inRect(const SDL_Rect& r, float x, float y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void layout() {
  SDL_GetRendererOutputSize(ren, &outW, &outH);
  bool portrait = touchMode && outW < outH * 1.1;
  int s = std::max(44, std::min(outW, outH) / 7), m = s / 2;
  int availH = outH;
  if (portrait) availH = std::max(VH / 2, outH - (s * 3 + m * 3));
  float sc = std::min((float)outW / VW, (float)availH / VH);
  if (sc >= 1.f && !touchMode) sc = std::floor(sc);  // crisp whole-number scaling on desktop, fill the screen on phones
  int gw = (int)(VW * sc), gh = (int)(VH * sc);
  gameRect = {(outW - gw) / 2, portrait ? (availH - gh) / 2 : (outH - gh) / 2, gw, gh};

  int cx = m + s * 3 / 2, cy = outH - m - s * 3 / 2;
  btns[0] = {{cx - s / 2, cy - s * 3 / 2, s, s}, A_UP, "", 0};
  btns[1] = {{cx + s / 2, cy - s / 2, s, s}, A_RIGHT, "", 1};
  btns[2] = {{cx - s / 2, cy + s / 2, s, s}, A_DOWN, "", 2};
  btns[3] = {{cx - s * 3 / 2, cy - s / 2, s, s}, A_LEFT, "", 3};
  int sw = s * 3 / 2, tw = s * 5 / 4;
  SDL_Rect swing = {outW - m - sw, outH - m - sw - s / 2, sw, sw};
  btns[4] = {swing, A_SWING, "SWING", -1};
  btns[5] = {{swing.x - tw - s / 4, outH - m - tw, tw, tw}, A_TALK, "TALK", -1};
  // the sound button sits at the right edge, clear of the coin counter in the corner of the view
  int bs = s * 2 / 3, padTop = outH - (s * 3 + m * 3);
  btns[6] = {{outW - m / 2 - bs, portrait ? padTop + m / 4 : outH / 2 - bs / 2, bs, bs}, A_MUTE, "M", -1};
  btns[7] = {{btns[6].r.x, portrait ? btns[6].r.y + bs + m / 4 : btns[6].r.y - bs - m / 4, bs, bs}, A_MENU, "SET", -1};
  btns[8] = {{swing.x - tw - s / 4, outH - m - tw * 2 - s / 4, tw, tw}, A_EXAMINE, "LOOK", -1};
}

int actOfKey(SDL_Scancode sc) {
  switch (sc) {
    case SDL_SCANCODE_W: case SDL_SCANCODE_UP: return A_UP;
    case SDL_SCANCODE_S: case SDL_SCANCODE_DOWN: return A_DOWN;
    case SDL_SCANCODE_A: case SDL_SCANCODE_LEFT: return A_LEFT;
    case SDL_SCANCODE_D: case SDL_SCANCODE_RIGHT: return A_RIGHT;
    case SDL_SCANCODE_E: case SDL_SCANCODE_RETURN: return A_TALK;
    case SDL_SCANCODE_X: return A_EXAMINE;
    case SDL_SCANCODE_SPACE: return A_SWING;
    case SDL_SCANCODE_M: return A_MUTE;
    case SDL_SCANCODE_R: return A_RESTART;
    case SDL_SCANCODE_F5: return A_RELOAD;
    case SDL_SCANCODE_F6: return A_RELOAD_SPRITES;
    case SDL_SCANCODE_ESCAPE: return A_MENU;
    default: return -1;
  }
}

bool dialogOrMenu() { return S.mode != M_PLAY || dlg.active; }

// ---- the menu: settings, status (weapons, stats) and save/load ----
bool menuOpen = false, wantQuit = false;
const int MENU_TABS = 4;
int menuTab = 0;   // 0 status, 1 save / load, 2 settings, 3 credits
int menuSel = 1;   // 0 = the tab header, then the rows of the current tab
int slotCol = 0;   // save/load page: 0 = SAVE button, 1 = LOAD button
std::string saveDir, settingsPath;  // user data folder; empty = nothing is saved (test runs)
std::string menuMsg;
const int SLOTS = 3;
const int MENU_X = 30, MENU_Y = 14, MENU_W = 260, MENU_H = 180;
const int BAR_X = MENU_X + 92, BAR_STEP = 8;  // volume bars: 10 steps of 8 pixels

struct SlotInfo { bool used = false; std::string line, when; };
SlotInfo slotInfo[SLOTS];
bool saveGame(int slot, std::string& msg);  // defined further down, next to the story code
bool loadGame(int slot, std::string& msg);

// ---- Brothilda's shop: Hot Stew (5 coins) and, once Elder Maro has sent Tilda, the Short Sword (30 coins) ----
bool shopOpen = false, shopMsgOk = true;
int shopSel = 0, shopKind = 0;  // 0 Brothilda, 1 Reg
std::string shopMsg;
const int SHOP_X = 40, SHOP_W = 240, SHOP_Y = 26;
const int STEW_PRICE = 5, SWORD_PRICE = 30, ONION_PRICE = 8;
int shopCount() { return shopKind == 1 ? 2 : (S.quest >= 5 ? 3 : 2); }  // Brothilda: Stew, Short Sword (later), Leave. Reg: Onion Stew, Leave
SDL_Rect shopRowRect(int row) { return {SHOP_X + 8, SHOP_Y + 40 + row * 30, SHOP_W - 16, 27}; }
void shopBuy(int row);  // defined next to the story code
void openShopKind(int kind) { shopKind = kind; shopOpen = true; shopSel = 0; shopMsg.clear(); }
void openShop() { openShopKind(0); }
void openRegShop() { openShopKind(1); }
void shopActivate(int row) {
  if (row == shopCount() - 1) shopOpen = false;
  else shopBuy(row);
}
void shopTap(float x, float y) {
  float lx = (x - gameRect.x) * VW / (float)gameRect.w, ly = (y - gameRect.y) * VH / (float)gameRect.h;
  for (int i = 0; i < shopCount(); i++)
    if (inRect(shopRowRect(i), lx, ly)) { shopSel = i; shopActivate(i); return; }
}

int menuRows(int tab) { return tab == 0 || tab == 3 ? 1 : (tab == 1 ? SLOTS + 1 : 4); }  // rows below the tab header

SDL_Rect menuRowRect(int tab, int row) {  // row counts from 1
  if (tab == 2) return {MENU_X + 8, 46 + (row - 1) * 22, MENU_W - 16, 19};
  if (tab == 0 || tab == 3) return {MENU_X + 8, 156, MENU_W - 16, 19};
  return {MENU_X + 8, 40 + (row - 1) * 31, MENU_W - 16, 23};  // the 3 slots and the bottom row: same height, same spacing
}
SDL_Rect menuTabRect(int t) { return {MENU_X + t * (MENU_W / MENU_TABS), MENU_Y + 3, MENU_W / MENU_TABS, 17}; }
SDL_Rect menuBtnRect(int col) {  // the two buttons in the bottom row of the Save / Load page
  SDL_Rect r = menuRowRect(1, SLOTS + 1);
  int w = (r.w - 6) / 2;
  return {r.x + col * (w + 6), r.y + 4, w, 15};
}
SDL_Rect slotBtnRect(int row, int col) {
  SDL_Rect r = menuRowRect(1, row);
  return {MENU_X + MENU_W - 8 - 104 + col * 54, r.y + 4, 50, 15};
}

std::string slotPath(int slot) { return saveDir + "save" + std::to_string(slot + 1) + ".txt"; }

void refreshSlots() {
  for (int i = 0; i < SLOTS; i++) {
    slotInfo[i] = SlotInfo();
    if (saveDir.empty()) continue;
    FILE* f = fopen(slotPath(i).c_str(), "r");
    if (!f) continue;
    char line[256], when[64] = "";
    int quest = -1, coins = 0, hp = 0;
    while (fgets(line, sizeof line, f)) {
      sscanf(line, "quest=%d", &quest);
      sscanf(line, "coins=%d", &coins);
      sscanf(line, "hp=%d", &hp);
      if (!strncmp(line, "when=", 5)) { snprintf(when, sizeof when, "%.60s", line + 5); when[strcspn(when, "\r\n")] = 0; }
    }
    fclose(f);
    if (quest < 0) continue;
    slotInfo[i].used = true;
    (void)hp;
    slotInfo[i].line = "Quest " + std::to_string(quest) + ", " + std::to_string(coins) + " coins";
    slotInfo[i].when = when;
  }
}

void saveSettings() {
  if (settingsPath.empty()) return;
  if (FILE* f = fopen(settingsPath.c_str(), "w")) {
    fprintf(f, "music=%d\nsfx=%d\nmuted=%d\n", musicStep, sfxStep, muted ? 1 : 0);
    fclose(f);
  }
}

void loadSettings() {
  applyVolumes();
  if (settingsPath.empty()) return;
  FILE* f = fopen(settingsPath.c_str(), "r");
  if (!f) return;
  char key[32];
  int v;
  while (fscanf(f, " %31[a-z]=%d", key, &v) == 2) {
    if (!strcmp(key, "music")) musicStep = std::max(0, std::min(10, v));
    else if (!strcmp(key, "sfx")) sfxStep = std::max(0, std::min(10, v));
    else if (!strcmp(key, "muted")) muted = v != 0;
  }
  fclose(f);
  applyVolumes();
}

void menuOpenNow() { menuOpen = true; menuSel = 1; menuMsg.clear(); refreshSlots(); }
void menuClose() { menuOpen = false; saveSettings(); }

void menuSetStep(int row, int v) {  // row 1 = music, 2 = effects
  v = std::max(0, std::min(10, v));
  if (row == 1) musicStep = v;
  else sfxStep = v;
  applyVolumes();
}

void menuActivate(int row) {
  if (row == 0) { menuTab = (menuTab + 1) % MENU_TABS; return; }
  if (menuTab == 2) {
    if (row == 3) muted = !muted;
    else if (row == 4) menuClose();
  } else if (menuTab == 0 || menuTab == 3) {
    if (row == 1) menuClose();
  } else {
    if (row == SLOTS + 1) {
      if (slotCol == 0) menuClose();
      else { saveSettings(); wantQuit = true; }
    } else {
      std::string m;
      if (slotCol == 0) saveGame(row - 1, m);
      else if (loadGame(row - 1, m)) { menuClose(); }
      menuMsg = m;
      refreshSlots();
    }
  }
}

void menuTap(float x, float y) {
  float lx = (x - gameRect.x) * VW / (float)gameRect.w, ly = (y - gameRect.y) * VH / (float)gameRect.h;
  for (int t = 0; t < MENU_TABS; t++)
    if (inRect(menuTabRect(t), lx, ly)) { menuTab = t; menuSel = 0; return; }
  for (int i = 1; i <= menuRows(menuTab); i++) {
    SDL_Rect r = menuRowRect(menuTab, i);
    if (!inRect(r, lx, ly)) continue;
    menuSel = i;
    if (menuTab == 2 && i <= 2) {
      if (lx >= BAR_X - 6 && lx < BAR_X + 10 * BAR_STEP + 12) menuSetStep(i, (int)std::lround((lx - BAR_X) / (float)BAR_STEP));
    } else if (menuTab == 1) {
      for (int c = 0; c < 2; c++)
        if (inRect(i <= SLOTS ? slotBtnRect(i, c) : menuBtnRect(c), lx, ly)) { slotCol = c; menuActivate(i); }
    } else {
      menuActivate(i);
    }
    return;
  }
}

void fingerDown(long long id, float x, float y) {
  fingers.push_back({id, x, y});
  bool onButton = false;
  if (touchMode)
    for (const Btn& b : btns) onButton |= inRect(b.r, x, y);
  if (menuOpen) { if (!onButton) menuTap(x, y); return; }
  if (shopOpen) { if (!onButton) shopTap(x, y); return; }
  // a tap anywhere else advances dialogs and menus
  if (!onButton && dialogOrMenu()) press[A_TALK] = true;
}
void fingerMove(long long id, float x, float y) {
  for (Finger& f : fingers)
    if (f.id == id) { f.x = x; f.y = y; }
}
void fingerUp(long long id) {
  fingers.erase(std::remove_if(fingers.begin(), fingers.end(), [&](const Finger& f) { return f.id == id; }),
                fingers.end());
}

// Combines keyboard, touch pad and scripted input into held[] and press[].
void sampleInput(uint32_t scriptMask) {
  const Uint8* ks = SDL_GetKeyboardState(nullptr);
  bool kb[A_COUNT] = {};
  kb[A_UP] = ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP];
  kb[A_DOWN] = ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN];
  kb[A_LEFT] = ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT];
  kb[A_RIGHT] = ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT];
  kb[A_TALK] = ks[SDL_SCANCODE_E] || ks[SDL_SCANCODE_RETURN];
  kb[A_SWING] = ks[SDL_SCANCODE_SPACE];
  kb[A_EXAMINE] = ks[SDL_SCANCODE_X];

  bool syn[A_COUNT] = {};
  for (int a = 0; a < A_COUNT; a++)
    if (scriptMask & (1u << a)) syn[a] = true;
  if (touchMode)
    for (const Btn& b : btns)
      for (const Finger& f : fingers)
        if (inRect(b.r, f.x, f.y)) syn[b.act] = true;

  for (int a = 0; a < A_COUNT; a++) {
    held[a] = kb[a] || syn[a];
    if (syn[a] && !prevSyn[a]) press[a] = true;
    prevSyn[a] = syn[a];
  }
}

// ============================================================================
// World helpers
// ============================================================================
bool blocked(float x, float y, int hw, int hh, bool foe) {
  const float px[4] = {x - hw, x + hw, x - hw, x + hw};
  const float py[4] = {y - hh, y - hh, y + hh, y + hh};
  for (int i = 0; i < 4; i++) {
    int t = world.at((int)std::floor(px[i] / T), (int)std::floor(py[i] / T));
    if (solidTile(t)) return true;
    if (foe && (t == TILE_DOOR || t == TILE_FLOOR || t == TILE_GATE)) return true;
  }
  return false;
}

bool npcHit(float x, float y, int hw, int hh) {
  for (const Npc& n : npcs)
    if (std::fabs(x - n.x) < hw + 5 && std::fabs(y - n.y) < hh + 4) return true;
  return false;
}

void moveEnt(float& x, float& y, float dx, float dy, int hw, int hh, bool isPlayer) {
  float nx = x + dx;
  if (!blocked(nx, y, hw, hh, !isPlayer) && !(isPlayer && npcHit(nx, y, hw, hh))) x = nx;
  float ny = y + dy;
  if (!blocked(x, ny, hw, hh, !isPlayer) && !(isPlayer && npcHit(x, ny, hw, hh))) y = ny;
}

int dirTo(float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay;
  return std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0);
}

void toast(const std::string& s, float sec = 3) {
  S.toast = s;
  S.toastT = sec;
}
void fairySay(const std::string& s) { toast("Nagi: " + s, 3.5f); }

void burst(float x, float y, uint32_t col, int n, float sp) {
  for (int i = 0; i < n; i++) {
    float a = rndf() * 6.2831853f, v = (0.4f + rndf() * 0.6f) * sp;
    parts.push_back({x, y, std::cos(a) * v, std::sin(a) * v - 10, 0.35f + rndf() * 0.3f, col});
  }
}

void drop(float x, float y, int kind) {
  pickups.push_back({kind, x + rndf() * 10 - 5, y + rndf() * 6 - 3, 20, rndf() * 6});
}

Enemy makeEnemy(int kind, float x, float y, int spawn) {
  Enemy e;
  e.kind = kind; e.x = x; e.y = y; e.hx = x; e.hy = y; e.spawn = spawn;
  e.hop = rndf() * 0.8f;
  if (kind == K_KING) { e.hw = 10; e.hh = 7; e.hp = e.max = 10; }
  else if (kind == K_DRAGON) { e.hw = 10; e.hh = 7; e.hp = e.max = dragonCfg.hp; e.stT = 0; e.fireT = 1.5f; }   // waits in the middle of the cave
  else if (kind == K_CHICKEN) { e.hw = 4; e.hh = 3; e.hp = e.max = 2; }
  else if (kind == K_GOBLIN) { e.hw = 5; e.hh = 4; e.hp = e.max = 5; }
  else { e.hw = 5; e.hh = 4; e.hp = e.max = 2; }
  return e;
}

void spawnKing() {
  if (!hasKingSpawn) { kingX = P.x; kingY = P.y + 40; }
  enemies.push_back(makeEnemy(K_KING, kingX, kingY, -1));
}

void resetGame() {
  S.quest = 0; S.kills = 0; S.coins = 0; S.raylin = 0; S.sheepQ = 0; S.sheepMask = 0; S.regenT = 0; S.regenTick = 0; seenRealm = false; setWeapon(0); S.strength = 0; S.spare = 0; floatTexts.clear(); shopOpen = false; S.chickHits = 0; keyOwned = false; tunk = Follower(); lockT = 0; flags.clear(); story.resetCounters();
  S.winT = 0; S.deadT = 0; S.lowSaid = false; S.sawTunk = false; S.near = -1; S.toastT = 0;
  dlg = Dialog();
  npcs.clear(); enemies.clear(); pickups.clear(); parts.clear(); spawns.clear(); fires.clear(); hasDragonSpawn = false;
  hasKingSpawn = false;
  doorLinks.clear(); fadeT = 0;
  bool gotStart = false;
  int oidx = -1, sheepIdx = 0;
  const int worldObjs = (int)world.objs.size();
  for (int oi = 0; oi < worldObjs + (int)npcObjs.size(); oi++) {  // the world's objects first, then the characters
    const Obj& o = oi < worldObjs ? world.objs[oi] : npcObjs[oi - worldObjs];
    oidx++;
    if (oi < worldObjs && isNpcType(o.type)) continue;  // people in a world file are ignored: see npcs.txt
    float cx = o.x * T + 8.f, cy = o.y * T + 8.f;
    switch (o.type) {
      case OBJ_PLAYER: P.x = cx; P.y = cy; gotStart = true; break;
      case OBJ_ELDER: npcs.push_back({OBJ_ELDER, "Elder Maro", cx, cy, 2, CH_ELDER}); break;
      case OBJ_MERCHANT: npcs.push_back({OBJ_MERCHANT, "Brothilda", cx, cy, 2, CH_MERCHANT}); break;
      case OBJ_PIP: npcs.push_back({OBJ_PIP, "Pip", cx, cy, 1, CH_PIP}); break;
      case OBJ_TUNK: npcs.push_back({OBJ_TUNK, "Tunk", cx, cy, 2, CH_TUNK}); break;
      case OBJ_SLIME:
        spawns.push_back({cx, cy, 0, true});
        enemies.push_back(makeEnemy(K_SLIME, cx, cy, (int)spawns.size() - 1));
        break;
      case OBJ_KING: kingX = cx; kingY = cy; hasKingSpawn = true; break;
      case OBJ_DRAGON: dragonX = cx; dragonY = cy; hasDragonSpawn = true; break;
      case OBJ_CHICKEN: enemies.push_back(makeEnemy(K_CHICKEN, cx, cy, -1)); break;
      case OBJ_DOOR: doorLinks.push_back({o.x, o.y}); break;
      case OBJ_GOBLIN: { Enemy g = makeEnemy(K_GOBLIN, cx, cy, -1); g.oid = oidx; enemies.push_back(g); break; }
      case OBJ_SIGN: npcs.push_back({OBJ_SIGN, "Iron Door", cx, cy, 2, -1}); break;
      case OBJ_RAYLIN: npcs.push_back({OBJ_RAYLIN, "Raylin", cx, cy, 3, CH_RAYLIN}); break;
      case OBJ_REG: npcs.push_back({OBJ_REG, "Reg", cx, cy, 2, CH_REG}); break;
      case OBJ_SHEEP: { Npc sh = {OBJ_SHEEP, "Sheep", cx, cy, 1, -1}; sh.id = sheepIdx++ % 5; sh.hx = cx; sh.hy = cy; npcs.push_back(sh); break; }
      case OBJ_DANCER: npcs.push_back({OBJ_DANCER, "Zahra", cx, cy, 2, CH_DANCER}); break;
      case OBJ_ALBINO: npcs.push_back({OBJ_ALBINO, "Ozzy", cx, cy, 3, CH_ALBINO}); break;
      case OBJ_BARON: npcs.push_back({OBJ_BARON, "Baron Skalman", cx, cy, 2, CH_BARON}); break;
      case OBJ_BARKEEP: npcs.push_back({OBJ_BARKEEP, "Grum", cx, cy, 0, CH_BARKEEP}); break;
      case OBJ_MAID: npcs.push_back({OBJ_MAID, "Greta", cx, cy, 2, CH_MAID}); break;
      case OBJ_KNIGHT: npcs.push_back({OBJ_KNIGHT, "Knight", cx, cy, 2, CH_KNIGHT}); break;
      case OBJ_SERVANT: npcs.push_back({OBJ_SERVANT, "Wilbur", cx, cy, 2, CH_SERVANT}); break;
      default: break;
    }
  }
  if (!gotStart) { P.x = T * 1.5f; P.y = T * 1.5f; }
  P.dir = 2; P.hp = P.max; P.inv = 0; P.atk = 0; P.cd = 0; P.kt = 0; P.moving = false;
  fairyX = P.x; fairyY = P.y - 18;
}

// ============================================================================
// Story
// ============================================================================
void openDialog(std::vector<Line> lines, std::function<void()> onEnd = nullptr) {
  dlg.active = true;
  dlg.lines = std::move(lines);
  dlg.i = 0;
  dlg.shown = 0;
  dlg.onEnd = std::move(onEnd);
}

const char* weaponName(int w);

// Tilda gets a better weapon: the old one is passed on to Tunk (or kept for him until he joins).
void giveWeapon(int w) {
  int old = S.weapon;
  setWeapon(w);
  if (old > 0 && old != w) {
    if (tunk.active) tunk.weapon = old;
    else S.spare = old;
  }
}

// Buying at Brothilda's. Row 0 = stew, row 1 = short sword.
void shopBuy(int row) {
  shopMsgOk = false;
  if (shopKind == 1) {  // Reg's onion stew: a heart back every 2 seconds for a while
    if (P.hp >= P.max) shopMsg = "You are not hurt. Keep your coins.";
    else if (S.regenT > 0) shopMsg = "You are still digesting the last bowl.";
    else if (S.coins < ONION_PRICE) shopMsg = "Not enough coins. Onion stew costs " + std::to_string(ONION_PRICE) + ".";
    else {
      S.coins -= ONION_PRICE; S.regenT = 14; S.regenTick = 0;
      sfx(SFX_HEAL);
      shopMsg = "Delicious! Your hearts will come back slowly.";
      shopMsgOk = true;
    }
    return;
  }
  if (row == 0) {
    if (P.hp >= P.max) shopMsg = "You are not hurt. Keep your coins.";
    else if (S.coins < STEW_PRICE) shopMsg = "Not enough coins. Stew costs " + std::to_string(STEW_PRICE) + ".";
    else {
      S.coins -= STEW_PRICE; P.hp = P.max;
      sfx(SFX_HEAL);
      shopMsg = "Stew eaten. All hearts restored!";
      shopMsgOk = true;
    }
  } else {
    if (S.weapon >= 3) shopMsg = "You already own the short sword.";
    else if (S.coins < SWORD_PRICE) shopMsg = "Not enough coins. The sword costs " + std::to_string(SWORD_PRICE) + ".";
    else {
      int old = S.weapon;
      S.coins -= SWORD_PRICE;
      giveWeapon(3);
      sfx(SFX_JINGLE);
      shopMsg = (tunk.active && old > 0) ? std::string("Bought! Tunk takes your ") + weaponName(old) + "." : "You bought the short sword!";
      shopMsgOk = true;
    }
  }
}

void tunkWarp(float x, float y) {  // put Tunk next to Tilda and forget the old trail
  tunk.x = x; tunk.y = y; tunk.atk = 0; tunk.moving = false;
  tunk.trail.clear();
  tunk.trail.push_back({x, y});
}

// Tunk leaves his cage and joins the party with the weapon Tilda no longer needs.
void tunkJoin() {
  for (size_t i = 0; i < npcs.size(); i++)
    if (npcs[i].type == OBJ_TUNK) {
      tunk.x = npcs[i].x; tunk.y = npcs[i].y;
      npcs.erase(npcs.begin() + i);
      break;
    }
  tunk.active = true;
  tunk.weapon = S.spare;  // the weapon Tilda passed on
  tunk.dir = 2;
  tunkWarp(tunk.x, tunk.y);
  S.near = -1;
  toast(tunk.weapon == 1 ? "Tunk joins you and takes your old spoon!" : (tunk.weapon == 2 ? "Tunk joins you and takes your old spork!" : "Tunk joins you!"), 4);
}

int sheepFound() { int n = 0; for (int i = 0; i < 5; i++) n += (S.sheepMask >> i) & 1; return n; }

void sheepHome(int id) {  // the sheep trots off to Reg's farm
  S.sheepMask |= 1 << id;
  for (size_t i = 0; i < npcs.size(); i++)
    if (npcs[i].type == OBJ_SHEEP && npcs[i].id == id) { npcs.erase(npcs.begin() + i); break; }
}

// ---- the story script (assets/story.txt): dialogs, objectives and triggers are data, not code ----
// (Re)creates what lives in the dragon's lair: the dragon once the quest has got that far, Dina once she has been found.
void refreshLair() {
  enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.kind == K_DRAGON; }), enemies.end());
  npcs.erase(std::remove_if(npcs.begin(), npcs.end(), [](const Npc& n) { return n.type == OBJ_DINA; }), npcs.end());
  fires.clear();
  S.near = -1;
  if (!hasDragonSpawn) return;
  auto it = flags.find("dina_found");
  if (it != flags.end() && it->second) npcs.push_back({OBJ_DINA, "Dina", dragonX, dragonY, 2, CH_DINA});
  else if (S.quest >= 9) enemies.push_back(makeEnemy(K_DRAGON, dragonX, dragonY, -1));
}

int storyVar(const std::string& v) {
  if (v == "quest") return S.quest;
  if (v == "kills") return S.kills;
  if (v == "coins") return S.coins;
  if (v == "weapon") return S.weapon;
  if (v == "hp") return P.hp;
  if (v == "sheepq") return S.sheepQ;
  if (v == "sheepfound") return sheepFound();
  if (v == "seenrealm") return seenRealm ? 1 : 0;
  if (v == "key") return keyOwned ? 1 : 0;
  if (v == "strength") return S.strength;
  if (v == "tunk") return tunk.active ? 1 : 0;
  if (v == "swordprice") return SWORD_PRICE;
  if (v == "px") return (int)std::floor(P.x / T);
  if (v == "py") return (int)std::floor(P.y / T);
  if (v == "clues") { int n = 0; for (const auto& kv : flags) n += kv.first.compare(0, 5, "clue_") == 0 && kv.second > 0; return n; }
  auto it = flags.find(v);
  return it == flags.end() ? 0 : it->second;
}

void setStoryVar(const std::string& v, int val) {
  if (v == "quest") S.quest = std::max(0, std::min(10, val));
  else if (v == "sheepq") S.sheepQ = std::max(0, std::min(2, val));
  else flags[v] = val;
}

void runActs(const std::vector<StAct>& acts, int npcId) {
  for (const StAct& a : acts) {
    const std::string& c = a.cmd;
    std::string arg = stExpand(a.arg, storyVar);
    if (c == "set" || c == "add") {
      size_t sp = arg.find(' ');
      std::string name = arg.substr(0, sp);
      int val = sp == std::string::npos ? 0 : atoi(arg.c_str() + sp + 1);
      if (c == "add") {
        if (name == "coins") S.coins += val;
        else flags[name] += val;
      } else {
        setStoryVar(name, val);
      }
    }
    else if (c == "giveweapon") giveWeapon(atoi(arg.c_str()));
    else if (c == "setweapon") setWeapon(atoi(arg.c_str()));
    else if (c == "heal") P.hp = P.max;
    else if (c == "spawnking") spawnKing();
    else if (c == "givekey") keyOwned = true;
    else if (c == "spawndragon") refreshLair();
    else if (c == "tunkjoin") tunkJoin();
    else if (c == "swapmusic") tuneChoice = 1 - tuneChoice;   // the dancer's tip: music1 <-> music2
    else if (c == "shop") { if (arg == "reg") openRegShop(); else openShop(); }
    else if (c == "sheephome") sheepHome(npcId);
    else if (c == "sfx") sfx(arg == "coin" ? SFX_COIN : arg == "heal" ? SFX_HEAL : arg == "hurt" ? SFX_HURT : SFX_JINGLE);
    else if (c == "toast") toast(arg, 4);
    else if (c == "fairy") fairySay(arg);
    else fprintf(stderr, "story.txt: unknown command 'do %s'\n", c.c_str());
  }
}

// Picks the first block whose conditions hold and plays it. Returns false if there is none.
bool runStory(std::vector<StBlock>& blocks, const std::string& npcName, int npcId) {
  for (StBlock& b : blocks) {
    if (b.variants.empty() || !stHolds(b.conds, storyVar)) continue;
    StVariant& v = b.variants[b.count++ % b.variants.size()];
    std::vector<Line> lines;
    for (const StSay& sy : v.say) lines.push_back({sy.who == "$" ? npcName : sy.who, stExpand(sy.text, storyVar)});
    std::vector<StAct> acts = v.acts;
    if (lines.empty()) runActs(acts, npcId);
    else openDialog(lines, [acts, npcId] { runActs(acts, npcId); });
    return true;
  }
  return false;
}

bool talkKey(const std::string& key, const std::string& npcName, int npcId) {
  auto it = story.talk.find(key);
  return it != story.talk.end() && runStory(it->second, npcName, npcId);
}

void talk(Npc& n) {
  std::string key = n.type == OBJ_SIGN ? "sign" : "";
  for (const NpcName& nn : NPC_NAMES)
    if (nn.type == n.type) key = nn.word;
  if (key.empty() || !talkKey(key, n.name, n.id)) toast(std::string(n.name) + " has nothing to say.", 2);
}

void examineTile(int tile) {
  const char* key = tile == TILE_BOOKS ? "books" : tile == TILE_BED ? "bed" : tile == TILE_BAR ? "bar" : tile == TILE_TABLE ? "table" : nullptr;
  if (!key) return;
  auto it = story.examine.find(key);
  if (it != story.examine.end()) runStory(it->second, "", -1);
}

// story triggers: things that start by themselves once their conditions are true (each only once)
void checkTriggers() {
  if (dlg.active || shopOpen) return;
  for (StBlock& b : story.trigger) {
    if (b.fired || b.variants.empty() || !stHolds(b.conds, storyVar)) continue;
    b.fired = true;
    std::vector<StBlock> one(1, b);
    runStory(one, "", -1);
    return;
  }
}

std::string dbgFlags() { std::string o; for (const auto& kv : flags) if (kv.second) o += kv.first + ":" + std::to_string(kv.second) + ","; return o; }

std::string objective() {
  for (const StBlock& b : story.objective)
    if (!b.variants.empty() && stHolds(b.conds, storyVar)) return stExpand(b.variants[0].text, storyVar);
  return "...";
}

// ---- save games: plain text files with key=value lines, one per slot ----
bool saveGame(int slot, std::string& msg) {
  if (saveDir.empty()) { msg = "Saving is not available in this run."; return false; }
  if (S.mode != M_PLAY) { msg = "You can only save during the game."; return false; }
  FILE* f = fopen(slotPath(slot).c_str(), "w");
  if (!f) { msg = "Could not write the save file."; return false; }
  char when[64] = "";
  time_t now = time(nullptr);
  if (struct tm* lt = localtime(&now)) strftime(when, sizeof when, "%d.%m. %H:%M", lt);
  fprintf(f, "tilda_save=1\nworld=%dx%d\nwhen=%s\n", world.w, world.h, when);
  fprintf(f, "quest=%d\nkills=%d\ncoins=%d\nweapon=%d\nchickhits=%d\nsawtunk=%d\nkey=%d\n", S.quest, S.kills, S.coins,
          S.weapon, S.chickHits, (int)S.sawTunk, (int)keyOwned);
  fprintf(f, "raylin=%d\nhp=%d\nx=%.2f\ny=%.2f\ndir=%d\n", S.raylin, P.hp, P.x, P.y, P.dir);
  fprintf(f, "tunk=%d\ntunkx=%.2f\ntunky=%.2f\ntunkweapon=%d\n", (int)tunk.active, tunk.x, tunk.y, tunk.weapon);
  fprintf(f, "strength=%d\ntunkstrength=%d\nspare=%d\n", S.strength, tunk.strength, S.spare);
  fprintf(f, "sheepq=%d\nsheepmask=%d\n", S.sheepQ, S.sheepMask);
  fprintf(f, "flags=");
  for (const auto& kv : flags) if (kv.second) fprintf(f, "%s:%d,", kv.first.c_str(), kv.second);
  fprintf(f, "\n");
  // goblins that are already beaten stay beaten (everything else respawns when you load)
  fprintf(f, "deadgoblins=");
  for (size_t k = 0; k < world.objs.size(); k++) {
    if (world.objs[k].type != OBJ_GOBLIN) continue;
    bool alive = false;
    for (const Enemy& e : enemies) alive |= !e.dead && e.kind == K_GOBLIN && e.oid == (int)k;
    if (!alive) fprintf(f, "%zu,", k);
  }
  fprintf(f, "\n");
  fclose(f);
  msg = "Saved to slot " + std::to_string(slot + 1) + ".";
  return true;
}

bool loadGame(int slot, std::string& msg) {
  if (saveDir.empty()) { msg = "Loading is not available in this run."; return false; }
  FILE* f = fopen(slotPath(slot).c_str(), "r");
  if (!f) { msg = "Slot " + std::to_string(slot + 1) + " is empty."; return false; }
  float v[16] = {};
  enum { QUEST, KILLS, COINS, WEAPON, CHICK, SAWTUNK, KEY, HP, X, Y, DIR, TUNK, TX, TY, TW, MAGIC };
  static const char* keys[] = {"quest", "kills", "coins", "weapon", "chickhits", "sawtunk", "key", "hp", "x", "y", "dir",
                               "tunk", "tunkx", "tunky", "tunkweapon", "tilda_save"};
  bool got[16] = {};
  std::vector<int> dead;
  std::map<std::string, int> loadedFlags;
  int ww = 0, wh = 0, raylinTalks = 0, strengthV = 0, tunkStrengthV = 0, spareV = 0, sheepQV = 0, sheepMaskV = 0;  // newer keys: older saves lack them
  char line[512];
  while (fgets(line, sizeof line, f)) {
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char* val = eq + 1;
    if (!strcmp(line, "world")) sscanf(val, "%dx%d", &ww, &wh);
    else if (!strcmp(line, "raylin")) raylinTalks = atoi(val);
    else if (!strcmp(line, "strength")) strengthV = atoi(val);
    else if (!strcmp(line, "tunkstrength")) tunkStrengthV = atoi(val);
    else if (!strcmp(line, "spare")) spareV = atoi(val);
    else if (!strcmp(line, "sheepq")) sheepQV = atoi(val);
    else if (!strcmp(line, "sheepmask")) sheepMaskV = atoi(val);
    else if (!strcmp(line, "flags")) {   // name:value,name:value,
      for (const char* q = val; *q && *q != '\n'; ) {
        const char* colon = strchr(q, ':');
        const char* comma = strchr(q, ',');
        if (!colon || !comma || colon > comma) break;
        loadedFlags[std::string(q, colon - q)] = atoi(colon + 1);
        q = comma + 1;
      }
    }
    else if (!strcmp(line, "deadgoblins")) {
      for (const char* p = val; *p && *p != '\n'; ) {
        dead.push_back(atoi(p));
        const char* c = strchr(p, ',');
        if (!c) break;
        p = c + 1;
      }
    } else {
      for (int k = 0; k < 16; k++)
        if (!strcmp(line, keys[k])) { v[k] = (float)atof(val); got[k] = true; }
    }
  }
  fclose(f);
  for (int k = 0; k < 16; k++)
    if (!got[k]) { msg = "Slot " + std::to_string(slot + 1) + " is damaged."; return false; }
  if (ww != world.w || wh != world.h) { msg = "That save belongs to a different world file."; return false; }

  resetGame();
  S.quest = std::max(0, std::min(10, (int)v[QUEST]));
  flags = loadedFlags;
  story.resetCounters();
  S.kills = std::max(0, (int)v[KILLS]);
  S.coins = std::max(0, (int)v[COINS]);
  setWeapon(std::max(0, std::min(3, (int)v[WEAPON])));
  S.chickHits = std::max(0, (int)v[CHICK]);
  S.sawTunk = v[SAWTUNK] != 0;
  S.raylin = 0;  // the dialog starts again after loading
  (void)raylinTalks;
  S.strength = std::max(0, strengthV);
  S.spare = std::max(0, std::min(3, spareV));
  keyOwned = v[KEY] != 0;
  S.sheepQ = std::max(0, std::min(2, sheepQV));
  for (int i = 0; i < 5; i++)
    if ((sheepMaskV >> i) & 1) sheepHome(i);
  S.chickHits = 0;   // the chickens have calmed down again
  if (S.quest == 2) spawnKing();
  refreshLair();
  P.hp = std::max(1, std::min(P.max, (int)v[HP]));
  P.x = v[X]; P.y = v[Y]; P.dir = std::max(0, std::min(3, (int)v[DIR]));
  if (v[TUNK] != 0) {
    for (size_t k = 0; k < npcs.size(); k++)
      if (npcs[k].type == OBJ_TUNK) { npcs.erase(npcs.begin() + k); break; }
    tunk.active = true;
    tunk.weapon = std::max(0, std::min(3, (int)v[TW]));
    tunk.strength = std::max(0, tunkStrengthV);
    tunkWarp(v[TX], v[TY]);
  }
  enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                               [&](const Enemy& e) {
                                 return e.kind == K_GOBLIN && std::find(dead.begin(), dead.end(), e.oid) != dead.end();
                               }),
                enemies.end());
  fairyX = P.x; fairyY = P.y - 18;
  seenRealm = (int)(P.y / T) >= REALM_ROW;
  S.mode = M_PLAY;
  fadeT = 0.3f;
  msg = "Loaded slot " + std::to_string(slot + 1) + ".";
  toast("Game loaded (slot " + std::to_string(slot + 1) + ")", 2.5f);
  return true;
}

// ============================================================================
// Combat
// ============================================================================
void hurtPlayer(int dmg, float fx, float fy) {
  combatT = 2.5f;
  if (S.god) return;
  P.hp -= dmg;
  P.inv = 1;
  float a = std::atan2(P.y - fy, P.x - fx);
  P.kx = std::cos(a) * 110; P.ky = std::sin(a) * 110; P.kt = 0.14f;
  sfx(SFX_HURT);
  burst(P.x, P.y, 0xe0453f, 8, 60);
  if (P.hp <= 0) {
    P.hp = 0; S.mode = M_DEAD; S.deadT = 0;
    burst(P.x, P.y, 0xe0453f, 20, 90);
  }
}

void hurtEnemy(Enemy& e, float dmg, float fx, float fy) {  // fx, fy: where the blow came from
  if (e.kind == K_DRAGON && dragonCfg.onlyRest && e.st != 3) {   // its armour holds - until it is out of breath after a ram
    sfx(SFX_HIT); burst(e.x + (fx < e.x ? -8 : 8), e.y - 6, 0xc0c8d0, 4, 40);
    floatTexts.push_back({e.x, e.y - 14, 0.6f, "clang", 0xc0c8d0});
    return;
  }
  e.flash = 0.15f;
  float a = std::atan2(e.y - fy, e.x - fx), k = e.kind == K_KING ? 50.f : (e.kind == K_DRAGON ? 35.f : 120.f);
  e.kx = std::cos(a) * k; e.ky = std::sin(a) * k; e.kt = 0.16f;

  if (e.kind == K_CHICKEN && !e.angry) {  // chickens shrug off the first few hits...
    sfx(SFX_CLUCK);
    burst(e.x, e.y - 4, 0xffffff, 6, 50);
    if (e.flockSwing != P.swingId) {  // one swing counts once, however many chickens it touches
      int hits = 0;
      for (Enemy& c : enemies)
        if (c.kind == K_CHICKEN && std::hypot(c.x - e.x, c.y - e.y) < 110) { c.flockSwing = P.swingId; hits = std::max(hits, ++c.flockHits); }
      S.chickHits = std::max(S.chickHits, hits);
      if (hits == 2) toast("The chickens look upset...", 2.5f);
      if (hits >= 3) {  // ...and then the whole flock comes for you
        for (Enemy& c : enemies)
          if (c.kind == K_CHICKEN && std::hypot(c.x - e.x, c.y - e.y) < 110) c.angry = true;
        sfx(SFX_HURT);
        toast("You hit the chickens too many times!", 4);
      }
    }
    return;
  }

  e.hp -= dmg;
  { char buf[16]; snprintf(buf, sizeof buf, "%.1f", dmg); floatTexts.push_back({e.x, e.y - 12, 0.8f, buf, 0xffe9a0}); }
  sfx(SFX_HIT);
  burst(e.x, e.y - 4, 0xffffff, 5, 50);
  if (e.kind == K_DRAGON && e.hp <= e.max * 0.3f) {   // badly hurt: the dragon turns back into Princess Dina
    e.dead = true;
    fires.clear();
    burst(e.x, e.y - 8, 0x8fe39a, 30, 90);
    burst(e.x, e.y - 8, 0xffe9a0, 16, 60);
    npcs.push_back({OBJ_DINA, "Dina", e.x, e.y, 2, CH_DINA});
    flags["dina_found"] = 1;
    sfx(SFX_JINGLE);
    toast("The dragon shrinks... and turns into Princess Dina!", 4);
    return;
  }
  if (e.hp > 0.001f) return;

  e.dead = true;
  if (e.kind == K_CHICKEN) {
    burst(e.x, e.y - 4, 0xffffff, 12, 70);
    drop(e.x, e.y, rndf() < 0.6f ? 1 : 0);
    return;
  }
  bool king = e.kind == K_KING, gob = e.kind == K_GOBLIN;
  burst(e.x, e.y - 4, king ? 0x7a7be0 : 0x5fd16a, king ? 30 : 14, 80);
  int coins = king ? 8 : (gob ? 2 + (int)(rndf() * 2) : 1 + (int)(rndf() * 2));
  for (int i = 0; i < coins; i++) drop(e.x, e.y, 0);
  if (king || rndf() < (gob ? 0.4f : 0.3f)) drop(e.x, e.y, 1);
  if (king) {
    S.winT = 1.5f;
    S.strength++;                    // a boss fight makes Tilda stronger
    if (tunk.active) tunk.strength++;
    floatTexts.push_back({P.x, P.y - 22, 1.6f, "STRENGTH +1", 0x8fe39a});
  } else if (gob) {
    return;
  } else {
    S.kills++;
    if (e.spawn >= 0) { spawns[e.spawn].alive = false; spawns[e.spawn].timer = 25; }
    if (S.quest == 1 && S.kills == 5) fairySay("Hey Tilda, go back to Elder Maro!");
  }
}

// ============================================================================
// Update
// ============================================================================
bool confirmPressed() { return press[A_TALK] || press[A_SWING]; }

// Door links: standing on one tile of a pair puts Tilda on the free tile next to the other one.
void useDoors() {
  if (!(held[A_UP] || held[A_DOWN] || held[A_LEFT] || held[A_RIGHT])) return;   // a knock-back must not push Tilda through a door
  int tx = (int)std::floor(P.x / T), ty = (int)std::floor(P.y / T);
  for (size_t i = 0; i < doorLinks.size(); i++) {
    if (doorLinks[i].x != tx || doorLinks[i].y != ty || (i ^ 1) >= doorLinks.size()) continue;
    const DoorLink& d = doorLinks[i ^ 1];
    static const int ox[4] = {0, 0, 1, -1}, oy[4] = {1, -1, 0, 0};
    for (int k = 0; k < 4; k++) {
      int nx = d.x + ox[k], ny = d.y + oy[k], t = world.at(nx, ny);
      if (solidTile(t) || t == TILE_DOOR || t == TILE_GATE || t == TILE_CGATE) continue;
      P.x = nx * T + 8.f; P.y = ny * T + 8.f;
      P.dir = k == 0 ? 2 : k == 1 ? 0 : k == 2 ? 1 : 3;
      fadeT = 0.3f;
      sfx(SFX_HEAL);
      fairyX = P.x; fairyY = P.y - 18;
      if (tunk.active) tunkWarp(P.x, P.y);
      return;
    }
  }
}

void startGame() {
  S.mode = M_PLAY;
  sfx(SFX_JINGLE);
  fairySay("Hey Tilda, ask Elder Maro about Tunk.");
}

void respawnPlayer() {
  P.x = 0; P.y = 0;
  for (const Obj& o : world.objs)
    if (o.type == OBJ_PLAYER) { P.x = o.x * T + 8.f; P.y = o.y * T + 8.f; }
  P.hp = P.max; P.inv = 2; P.kt = 0; P.atk = 0;
  if (tunk.active) tunkWarp(P.x, P.y);
  S.coins /= 2;
  S.mode = M_PLAY;
  dlg.active = false;
  toast("You wake up in the village. Half your coins are gone.", 3.5f);
}

void updateDialog(float dt) {
  const float full = (float)dlg.lines[dlg.i].text.size();
  int before = (int)dlg.shown;
  dlg.shown = std::min(full, dlg.shown + dt * 55);
  if ((int)dlg.shown / 3 > before / 3) sfx(SFX_TALK);
  if (!confirmPressed()) return;
  if (dlg.shown < full) {
    dlg.shown = full;
    return;
  }
  dlg.i++;
  dlg.shown = 0;
  if (dlg.i >= dlg.lines.size()) {
    dlg.active = false;
    auto cb = std::move(dlg.onEnd);
    dlg.onEnd = nullptr;
    if (cb) cb();
  }
}

// Tunk: follow Tilda's trail, and join any fight that happens close to her.
float dbgTunkMax = 0;  // test output: the largest distance between Tunk and Tilda seen so far

void updateTunk(float dt) {
  if (!tunk.active) return;
  if (tunk.trail.empty() || std::hypot(P.x - tunk.trail.back().first, P.y - tunk.trail.back().second) >= 2.f) {
    tunk.trail.push_back({P.x, P.y});
    if (tunk.trail.size() > 64) tunk.trail.erase(tunk.trail.begin());
  }
  tunk.atk = std::max(0.f, tunk.atk - dt);
  tunk.cd = std::max(0.f, tunk.cd - dt);
  combatT = std::max(0.f, combatT - dt);
  const float LEASH = 16.f;  // exactly one tile: Tunk is never further away from Tilda, not even in a fight
  if (std::hypot(P.x - tunk.x, P.y - tunk.y) > 40) tunkWarp(P.x, P.y);  // got stuck somewhere

  // the closest enemy that Tunk can reach from where he may stand (chickens are friends)
  Enemy* tgt = nullptr;
  float best = 1e9f;
  if (tunk.weapon > 0 && combatT > 0)  // only while Tilda attacks or is being attacked
    for (Enemy& e : enemies) {
      if (e.dead || e.kind == K_CHICKEN) continue;
      if (std::hypot(e.x - P.x, e.y - P.y) > LEASH + 12.f + e.hw) continue;  // out of reach for his leash
      float d = std::hypot(e.x - tunk.x, e.y - tunk.y);
      if (d < best) { best = d; tgt = &e; }
    }

  float vx = 0, vy = 0;
  if (tgt) {
    float dx = tgt->x - tunk.x, dy = tgt->y - tunk.y, d = std::max(0.001f, std::hypot(dx, dy));
    if (tunk.atk <= 0) tunk.dir = dirTo(tunk.x, tunk.y, tgt->x, tgt->y);
    if (d > 12.f + tgt->hw) {
      vx = dx / d * 74.f; vy = dy / d * 74.f;
    } else if (tunk.cd <= 0) {
      tunk.atk = 0.2f; tunk.cd = 0.55f; tunk.swingId++;
      sfx(SFX_SWING);
      hurtEnemy(*tgt, rollDamage(weaponClass(tunk.weapon), tunk.strength), tunk.x, tunk.y);
    }
  } else {
    size_t back = 7;  // about 14 pixels behind Tilda
    const auto& pt = tunk.trail.size() > back ? tunk.trail[tunk.trail.size() - 1 - back] : tunk.trail.front();
    float dx = pt.first - tunk.x, dy = pt.second - tunk.y, d = std::hypot(dx, dy);
    float near = std::hypot(P.x - tunk.x, P.y - tunk.y);
    if (d > 2.f && near > 12.f) {
      float sp = d > 24 ? 96.f : 66.f;
      vx = dx / d * sp; vy = dy / d * sp;
    }
  }
  tunk.moving = vx != 0 || vy != 0;
  if (tunk.moving) {
    tunk.walk += dt * 9;
    if (tunk.atk <= 0) tunk.dir = dirTo(0, 0, vx, vy);
    moveEnt(tunk.x, tunk.y, vx * dt, vy * dt, 5, 4, true);
  }
  // the leash: whatever he is doing, he is pulled back to within one tile of Tilda
  float ex = tunk.x - P.x, ey = tunk.y - P.y, ed = std::hypot(ex, ey);
  if (ed > LEASH) {
    float over = ed - LEASH;
    moveEnt(tunk.x, tunk.y, -ex / ed * over, -ey / ed * over, 5, 4, true);
    tunk.moving = true;
  }
  dbgTunkMax = std::max(dbgTunkMax, (float)std::hypot(tunk.x - P.x, tunk.y - P.y));
}

// The dragon: it charges at Tilda four times (a short warning, then a fast run in a straight line), then spits three
// fireballs, and starts again. Standing still in its way, or in the way of a fireball, hurts.
bool clearLine(float x0, float y0, float x1, float y1) {   // can a fireball fly from one point to the other?
  float d = std::max(1.f, (float)std::hypot(x1 - x0, y1 - y0));
  for (float t = 6.f; t < d; t += 6.f) {
    float x = x0 + (x1 - x0) * t / d, y = y0 + (y1 - y0) * t / d;
    if (solidTile(world.at((int)std::floor(x / T), (int)std::floor(y / T)))) return false;
  }
  return true;
}

// The dragon waits in the middle of its cave. It spits fireballs at Tilda while it can see her, rams whoever comes
// close (Tilda or Tunk), and is out of breath - and can be hurt - only after a ram. All numbers are in dragonCfg.
void updateDragon(Enemy& e, float dt) {
  const DragonCfg& c = dragonCfg;
  float dx = P.x - e.x, dy = P.y - e.y, d = std::max(0.001f, (float)std::hypot(dx, dy));
  e.stT += dt; e.fireT -= dt; e.spitT -= dt;
  if (e.kt > 0 && e.st != 2) {
    e.kt -= dt;
    moveEnt(e.x, e.y, e.kx * dt, e.ky * dt, e.hw, e.hh, false);
  }
  e.moving = false;
  // who is close enough to be rammed: Tilda or Tunk, whoever is nearer
  float tx = dx, ty = dy, td = d;
  bool near = d < c.near;
  if (tunk.active) {
    float ux = tunk.x - e.x, uy = tunk.y - e.y, ud = std::max(0.001f, (float)std::hypot(ux, uy));
    if (ud < c.near && (!near || ud < td)) { tx = ux; ty = uy; td = ud; near = true; }
  }
  switch (e.st) {
    case 0: {   // on guard
      e.faceRight = dx > 0;
      if (near) {
        e.st = 1; e.stT = 0; e.shots = 0;
        break;
      }
      float hx = e.hx - e.x, hy = e.hy - e.y, hd = (float)std::hypot(hx, hy);
      if (hd > 3.f) {   // walk back to its place
        e.moving = true; e.step += dt * 6.f;
        moveEnt(e.x, e.y, hx / hd * c.homeSpeed * dt, hy / hd * c.homeSpeed * dt, e.hw, e.hh, false);
      }
      if (d < c.sight && e.fireT <= 0 && clearLine(e.x, e.y - 8.f, P.x, P.y)) {
        float mx = e.x + (e.faceRight ? 14.f : -14.f), my = e.y - 8.f;
        float base = std::atan2(P.y - my, P.x - mx);
        for (int i = 0; i < c.fireCount; i++) {
          float a = base + (i - (c.fireCount - 1) * 0.5f) * 0.28f;
          fires.push_back({mx, my, std::cos(a) * c.fireSpeed, std::sin(a) * c.fireSpeed, 5.f});
        }
        sfx(SFX_HURT);
        e.fireT = c.fireEvery; e.spitT = 0.35f;
      }
      break;
    }
    case 1:     // it lowers its head and flashes, then locks the direction
      e.faceRight = tx > 0;
      e.flash = ((int)(e.stT * 12)) % 2 ? 0.05f : 0.f;
      if (e.stT > c.ramWarn) {
        float L = std::max(0.001f, (float)std::hypot(tx, ty));
        e.dvx = tx / L * c.ramSpeed; e.dvy = ty / L * c.ramSpeed;
        e.st = 2; e.stT = 0;
        sfx(SFX_SWING);
      }
      break;
    case 2: {   // the ram
      float ox = e.x, oy = e.y;
      moveEnt(e.x, e.y, e.dvx * dt, e.dvy * dt, e.hw, e.hh, false);
      e.faceRight = e.dvx > 0;
      e.step += dt * 16.f;
      if (tunk.active && !e.shots && std::fabs(tunk.x - e.x) < e.hw + 7 && std::fabs(tunk.y - e.y) < e.hh + 7) {   // Tunk is thrown aside
        e.shots = 1;
        float L = std::max(0.001f, (float)std::hypot(e.dvx, e.dvy));
        for (int k = 0; k < 6; k++) moveEnt(tunk.x, tunk.y, e.dvx / L * 6.f, e.dvy / L * 6.f, 5, 4, true);
        sfx(SFX_HURT); burst(tunk.x, tunk.y - 4, 0xffffff, 6, 60);
      }
      bool stuck = (std::fabs(e.x - ox) + std::fabs(e.y - oy)) < 0.2f;
      if (e.stT > c.ramTime || stuck) {
        if (stuck) { burst(e.x + (e.dvx > 0 ? 10 : -10), e.y, 0xaaaaaa, 8, 50); sfx(SFX_HIT); }
        e.st = 3; e.stT = 0;
      }
      break;
    }
    case 3:     // out of breath: now it can be hurt
      e.faceRight = dx > 0;
      if (((int)(e.stT * 6)) != ((int)((e.stT - dt) * 6))) burst(e.x, e.y - 16, 0xffe060, 1, 20);
      if (e.stT > c.rest) { e.st = 0; e.stT = 0; e.fireT = std::max(e.fireT, 0.6f); }
      break;
  }
  if (e.st == 2 && P.inv <= 0 && std::fabs(P.x - e.x) < e.hw + 5 && std::fabs(P.y - e.y) < e.hh + 4)
    hurtPlayer(c.ramDmg, e.x, e.y);
}

void updateFires(float dt) {
  for (size_t i = fires.size(); i-- > 0;) {
    Fire& f = fires[i];
    f.x += f.vx * dt; f.y += f.vy * dt; f.life -= dt;
    bool gone = f.life <= 0 || solidTile(world.at((int)std::floor(f.x / T), (int)std::floor(f.y / T)));
    if (!gone && P.inv <= 0 && std::fabs(P.x - f.x) < 7 && std::fabs(P.y - f.y) < 7) {
      hurtPlayer(dragonCfg.fireDmg, f.x, f.y);
      gone = true;
    }
    if (gone) { burst(f.x, f.y, 0xffb030, 6, 50); fires.erase(fires.begin() + i); }
  }
}

void updateEnemies(float dt) {
  updateFires(dt);
  for (Enemy& e : enemies) {
    if (e.dead) continue;
    e.flash = std::max(0.f, e.flash - dt);
    if (e.kind == K_DRAGON) { updateDragon(e, dt); continue; }
    if (e.kt > 0) {
      e.kt -= dt;
      moveEnt(e.x, e.y, e.kx * dt, e.ky * dt, e.hw, e.hh, false);
    } else {
      float dx = P.x - e.x, dy = P.y - e.y, d = std::sqrt(dx * dx + dy * dy);
      if (d < 0.001f) d = 0.001f;
      float vx = 0, vy = 0;
      bool chase = false;
      if (e.kind == K_KING) chase = d < 130;
      else if (e.kind == K_CHICKEN) chase = e.angry && d < 140;
      else if (e.kind == K_GOBLIN) chase = d < 95;
      else chase = d < 78;
      if (chase) {
        float sp = e.kind == K_KING ? 38.f : (e.kind == K_CHICKEN ? 46.f : (e.kind == K_GOBLIN ? 36.f : 30.f));
        vx = dx / d * sp; vy = dy / d * sp;
      } else {
        e.wt -= dt;
        if (e.wt <= 0) {
          e.wt = 1 + rndf() * 2;
          if (rndf() < 0.5f) { float a = rndf() * 6.2831853f; e.dx = std::cos(a); e.dy = std::sin(a); }
          else { e.dx = 0; e.dy = 0; }
        }
        float hd = std::sqrt((e.hx - e.x) * (e.hx - e.x) + (e.hy - e.y) * (e.hy - e.y));
        if (hd > 90) { e.dx = (e.hx - e.x) / hd; e.dy = (e.hy - e.y) / hd; }
        vx = e.dx * (e.kind == K_CHICKEN ? 12.f : 14.f);
        vy = e.dy * (e.kind == K_CHICKEN ? 12.f : 14.f);
      }
      if (vx > 0.1f) e.faceRight = true;
      else if (vx < -0.1f) e.faceRight = false;
      if (e.kind == K_CHICKEN || e.kind == K_GOBLIN) {
        e.moving = vx != 0 || vy != 0;
        if (e.moving) e.step += dt * 10.f;
        moveEnt(e.x, e.y, vx * dt, vy * dt, e.hw, e.hh, false);
      } else {
        e.hop = std::fmod(e.hop + dt, 0.8f);
        if (e.hop < 0.4f) moveEnt(e.x, e.y, vx * dt * 2, vy * dt * 2, e.hw, e.hh, false);
      }
    }
    bool harmful = e.kind != K_CHICKEN || e.angry;
    if (harmful && P.inv <= 0 && std::fabs(P.x - e.x) < e.hw + 5 && std::fabs(P.y - e.y) < e.hh + 4)
      hurtPlayer(e.kind == K_KING ? 2 : 1, e.x, e.y);
  }

  // slimes grow back after a while, but never in front of the player
  for (size_t i = 0; i < spawns.size(); i++) {
    Spawn& s = spawns[i];
    if (s.alive) continue;
    s.timer -= dt;
    if (s.timer > 0) continue;
    float dx = P.x - s.x, dy = P.y - s.y;
    if (std::sqrt(dx * dx + dy * dy) > 110) {
      enemies.push_back(makeEnemy(K_SLIME, s.x, s.y, (int)i));
      s.alive = true;
    } else {
      s.timer = 2;
    }
  }
  enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.dead; }),
                enemies.end());
}

void updateNpcFights(float dt) {
  for (Npc& n : npcs) {
    n.coolT = std::max(0.f, n.coolT - dt);
    float dx = P.x - n.x, dy = P.y - n.y, d = std::hypot(dx, dy);
    if (n.slapT > 0) {
      n.slapT -= dt;
      n.dir = dirTo(n.x, n.y, P.x, P.y);
      if (n.slapT <= 0) {
        n.slapT = 0; n.coolT = 1.5f;
        if (d < 30) {
          hurtPlayer(1, n.x, n.y);
          burst((n.x + P.x) / 2, P.y - 8, 0xffe9a0, 6, 50);
          toast(std::string(n.name) + " slaps you back!", 2);
        }
      }
    }
    if (n.ramT > 0 && n.type == OBJ_SHEEP) {  // the sheep charges at Tilda
      n.ramT -= dt;
      if (d > 0.1f) moveEnt(n.x, n.y, dx / d * 60.f * dt, dy / d * 60.f * dt, 4, 3, false);
      if (std::fabs(dx) < 10 && std::fabs(dy) < 9) {
        n.ramT = 0; n.coolT = 2.f; n.back = true;
        if (P.inv <= 0) { hurtPlayer(1, n.x, n.y); toast("The sheep rams you!", 2); }
        for (int k = 0; k < 4 && d > 0.1f; k++) moveEnt(n.x, n.y, -dx / d * 3.f, -dy / d * 3.f, 4, 3, false);  // bounce off, so Tilda is free
      }
      if (n.ramT < 0) { n.ramT = 0; n.back = true; }
    } else if (n.back && n.type == OBJ_SHEEP) {  // trot back home
      float hx = n.hx - n.x, hy = n.hy - n.y, hd = std::hypot(hx, hy);
      if (hd < 2.f) { n.back = false; n.x = n.hx; n.y = n.hy; }
      else moveEnt(n.x, n.y, hx / hd * 40.f * dt, hy / hd * 40.f * dt, 4, 3, false);
    }
  }
}

void updatePlay(float dt) {
  {   // music: caves have their own piece, outside it is music1 or (after a tip for the dancer) music2
    int tile = world.at((int)std::floor(P.x / T), (int)std::floor(P.y / T));
    trackSel = (tile == TILE_CAVE && tracks[2].loaded()) ? 2 : tuneChoice;
  }
  checkTriggers();
  P.cd = std::max(0.f, P.cd - dt);
  P.inv = std::max(0.f, P.inv - dt);
  if (P.atk > 0) P.atk -= dt;

  // movement
  int ix = (held[A_RIGHT] ? 1 : 0) - (held[A_LEFT] ? 1 : 0);
  int iy = (held[A_DOWN] ? 1 : 0) - (held[A_UP] ? 1 : 0);
  float vx = 0, vy = 0;
  if (P.kt > 0) {
    P.kt -= dt; vx = P.kx; vy = P.ky; P.moving = false;
  } else if (ix || iy) {
    float l = std::sqrt((float)(ix * ix + iy * iy));
    vx = ix / l * 62; vy = iy / l * 62;
    if (ix && !iy) P.dir = ix > 0 ? 1 : 3;
    else if (iy && !ix) P.dir = iy > 0 ? 2 : 0;
    else {
      bool horiz = P.dir == 1 || P.dir == 3;
      bool same = horiz ? ((ix > 0 && P.dir == 1) || (ix < 0 && P.dir == 3))
                        : ((iy > 0 && P.dir == 2) || (iy < 0 && P.dir == 0));
      if (!same) P.dir = horiz ? (iy > 0 ? 2 : 0) : (ix > 0 ? 1 : 3);
    }
    P.walk += dt * 9;
    P.moving = true;
  } else {
    P.moving = false;
  }
  moveEnt(P.x, P.y, vx * dt, vy * dt, 5, 4, true);
  useDoors();
  if (!seenRealm && (int)(P.y / T) >= REALM_ROW) {  // out of the tunnel at last
    seenRealm = true;
    toast("Baron Skalman's realm", 4);
    if (S.quest == 5) fairySay("Hey Tilda, somebody is fishing down by the lake.");
  }
  lockT = std::max(0.f, lockT - dt);
  if (!keyOwned && P.moving && lockT <= 0) {  // bumping into the locked cave gate
    static const int gx[4] = {0, 1, 0, -1}, gy[4] = {-1, 0, 1, 0};
    if (world.at((int)std::floor((P.x + gx[P.dir] * 9) / T), (int)std::floor((P.y + gy[P.dir] * 9) / T)) == TILE_GATE) {
      toast("The gate is locked. Maybe Elder Maro has a key.", 3);
      lockT = 4;
    }
  }

  // nearest person to talk to
  S.near = -1;
  float best = 26;
  for (size_t i = 0; i < npcs.size(); i++) {
    Npc& n = npcs[i];
    float d = std::hypot(n.x - P.x, n.y - P.y);
    if (d < best) { best = d; S.near = (int)i; }
    if (d < 44 && n.type != OBJ_TUNK && n.type != OBJ_DANCER && n.type != OBJ_SHEEP) n.dir = dirTo(n.x, n.y, P.x, P.y);
    if (n.type == OBJ_TUNK && S.quest < 3 && d < 50 && !S.sawTunk) {
      S.sawTunk = true;
      fairySay("Hey Tilda, that's Tunk in the cage!");
    }
  }
  if (press[A_EXAMINE]) {  // examine the furniture in front of Tilda (the texts are in story.txt)
    static const int fxs[4] = {0, 1, 0, -1}, fys[4] = {-1, 0, 1, 0};
    for (int k = 8; k <= 16; k += 4) {
      int t = world.at((int)std::floor((P.x + fxs[P.dir] * k) / T), (int)std::floor((P.y + fys[P.dir] * k) / T));
      if (t == TILE_BOOKS || t == TILE_BED || t == TILE_BAR || t == TILE_TABLE) { examineTile(t); return; }
    }
  }
  if (press[A_TALK] && S.near >= 0) { talk(npcs[S.near]); return; }
  if (press[A_TALK]) {  // a calm chicken right next to Tilda has something to say
    for (const Enemy& c : enemies) {
      if (c.dead || c.kind != K_CHICKEN || c.angry || std::hypot(c.x - P.x, c.y - P.y) > 22) continue;
      talkKey("chicken", "Chicken", -1);
      return;
    }
  }
  if (press[A_TALK] && tunk.active && std::hypot(P.x - tunk.x, P.y - tunk.y) < 26) {  // chat with Tunk
    talkKey("tunkfollower", "Tunk", -1);
    return;
  }

  if (press[A_SWING]) {
    if (!S.hasSword) toast("You need a weapon. Ask Elder Maro.", 2.5f);
    else if (P.cd <= 0) { P.atk = 0.2f; P.cd = 0.4f; P.swingId++; sfx(SFX_SWING); combatT = 2.0f; }
  }

  // sword hits
  if (P.atk > 0) {
    static const int fxs[4] = {0, 1, 0, -1}, fys[4] = {-1, 0, 1, 0};
    float cx = P.x + fxs[P.dir] * 12, cy = P.y + fys[P.dir] * 12 - 2;
    for (Enemy& e : enemies) {
      if (e.dead || e.hitId == P.swingId) continue;
      if (std::fabs(e.x - cx) < 11 + e.hw && std::fabs(e.y - cy) < 11 + e.hh) {
        e.hitId = P.swingId;
        hurtEnemy(e, rollDamage(weaponClass(S.weapon), S.strength), P.x, P.y);
      }
    }
    for (Npc& n : npcs) {  // hitting a friendly person is not nice: sheep ram you, people slap back (Tunk does nothing)
      if (n.type == OBJ_TUNK || n.type == OBJ_SIGN || n.hitId == P.swingId || n.coolT > 0 || n.ramT > 0 || n.back || n.slapT > 0) continue;
      if (std::fabs(n.x - cx) < 11 + 5 && std::fabs(n.y - cy) < 11 + 5) {
        n.hitId = P.swingId;
        burst(n.x, n.y - 6, 0xffffff, 4, 40);
        if (n.type == OBJ_SHEEP) { n.ramT = 3.f; sfx(SFX_CLUCK); toast("The sheep is angry!", 2); }
        else n.slapT = 0.35f;
      }
    }
  }

  if (S.regenT > 0) {  // onion stew: one heart back every 2 seconds
    S.regenT -= dt; S.regenTick += dt;
    if (S.regenTick >= 2) {
      S.regenTick -= 2;
      if (P.hp < P.max) { P.hp++; sfx(SFX_HEAL); }
    }
    if (S.regenT <= 0 || P.hp >= P.max) { S.regenT = 0; S.regenTick = 0; }
  }
  updateTunk(dt);
  updateEnemies(dt);
  updateNpcFights(dt);

  if (S.winT > 0) {
    S.winT -= dt;
    if (S.winT <= 0) {
      S.quest = 3;
      sfx(SFX_JINGLE);
      toast("Strength +1! The cage swings open. Go to Tunk!", 5);
    }
  }

  // pickups
  for (size_t i = pickups.size(); i-- > 0;) {
    Pickup& p = pickups[i];
    p.life -= dt; p.ph += dt * 5;
    if (p.life <= 0) { pickups.erase(pickups.begin() + i); continue; }
    if (std::fabs(p.x - P.x) < 9 && std::fabs(p.y - P.y) < 9) {
      if (p.kind == 0) { S.coins++; sfx(SFX_COIN); pickups.erase(pickups.begin() + i); }
      else if (P.hp < P.max) { P.hp++; sfx(SFX_HEAL); pickups.erase(pickups.begin() + i); }
    }
  }

  // Nagi has opinions
  if (P.hp > 2) S.lowSaid = false;
  if (P.hp == 1 && !S.lowSaid) { S.lowSaid = true; fairySay("Hey Tilda, you are low on hearts!"); }
}

void updateShop() {
  if (press[A_MENU]) { shopOpen = false; return; }
  int n = shopCount();
  if (press[A_UP]) shopSel = (shopSel + n - 1) % n;
  if (press[A_DOWN]) shopSel = (shopSel + 1) % n;
  if (press[A_TALK] || press[A_SWING]) shopActivate(shopSel);
}

void updateMenu() {
  if (press[A_MENU]) { menuClose(); return; }
  int rows = menuRows(menuTab) + 1;
  if (press[A_UP]) menuSel = (menuSel + rows - 1) % rows;
  if (press[A_DOWN]) menuSel = (menuSel + 1) % rows;
  int dir = (press[A_RIGHT] ? 1 : 0) - (press[A_LEFT] ? 1 : 0);
  if (dir) {
    if (menuSel == 0) menuTab = (menuTab + MENU_TABS + dir) % MENU_TABS;
    else if (menuTab == 2 && menuSel <= 2) {
      menuSetStep(menuSel, (menuSel == 1 ? musicStep : sfxStep) + dir);
      if (menuSel == 2) sfx(SFX_COIN);  // a sample of the new effects loudness
    } else if (menuTab == 2 && menuSel == 3) muted = !muted;
    else if (menuTab == 1 && menuSel <= SLOTS + 1) slotCol = dir > 0 ? 1 : 0;
    menuSel = std::min(menuSel, menuRows(menuTab));
  }
  if ((press[A_TALK] || press[A_SWING]) && !(menuTab == 2 && menuSel <= 2 && menuSel > 0)) menuActivate(menuSel);
  if (press[A_MUTE]) muted = !muted;
}

void update(float dt) {
  if (press[A_MENU] && !menuOpen && !shopOpen) { menuOpenNow(); return; }
  if (menuOpen) { updateMenu(); return; }
  if (shopOpen) { updateShop(); return; }
  S.t += dt;
  if (fadeT > 0) fadeT -= dt;
  if (S.toastT > 0) S.toastT -= dt;
  if (press[A_MUTE]) { muted = !muted; toast(muted ? "Sound off" : "Sound on", 1.2f); }

  for (size_t i = floatTexts.size(); i-- > 0;) {
    floatTexts[i].life -= dt;
    if (floatTexts[i].life <= 0) floatTexts.erase(floatTexts.begin() + i);
  }
  for (size_t i = parts.size(); i-- > 0;) {
    Part& p = parts[i];
    p.life -= dt; p.x += p.vx * dt; p.y += p.vy * dt; p.vy += 120 * dt;
    if (p.life <= 0) parts.erase(parts.begin() + i);
  }

  float k = std::min(1.f, 6 * dt);
  fairyX += (P.x + std::cos(S.t * 3) * 9 - fairyX) * k;
  fairyY += (P.y - 20 + std::sin(S.t * 4) * 4 - fairyY) * k;

  if (S.mode == M_TITLE) {
    if (confirmPressed()) startGame();
  } else if (S.mode == M_DEAD) {
    S.deadT += dt;
    if (S.deadT > 0.8f && (confirmPressed() || press[A_RESTART])) respawnPlayer();
  } else if (S.mode == M_WON) {
    if (confirmPressed() || press[A_RESTART]) {
      resetGame();
      S.mode = M_PLAY;
      toast("A new adventure begins.", 3);
    }
  } else if (dlg.active) {
    updateDialog(dt);
  } else {
    updatePlay(dt);
  }
}

// ============================================================================
// Drawing: sprites (everything comes from assets/sprite_map.png, see sprites.h)
// ============================================================================
void spr(int sx, int sy, int sw, int sh, int dx, int dy, bool flip = false, bool white = false) {
  SDL_Rect src = {sx, sy, sw, sh}, dst = {dx, dy, sw, sh};
  SDL_RenderCopyEx(ren, white ? sheetWhite : sheetTex, &src, &dst, 0.0, nullptr,
                   flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

void shadow(int cx, int cy, int half) {  // soft oval on the ground
  R(cx - half + 2, cy - 1, 2 * half - 4, 1, 0x000000, 50);
  R(cx - half, cy, 2 * half, 2, 0x000000, 70);
  R(cx - half + 2, cy + 2, 2 * half - 4, 1, 0x000000, 50);
}

// wx, wy is the character's position in the world; the feet are 3 pixels below it.
void drawChar(int idx, int dir, int frame, float wx, float wy) {
  int x = (int)std::lround(wx - camX), y = (int)std::lround(wy - camY);
  shadow(x, y + 2, 6);
  int ox, oy;
  if (idx < CH_RAYLIN) { ox = CHAR_ORIGIN_X + CHAR_BLOCK_W * idx; oy = 0; }
  else if (idx == CH_RAYLIN) { ox = RAYLIN_X; oy = RAYLIN_Y; }
  else if (idx < CH_BARKEEP) { ox = CHAR_ORIGIN_X + CHAR_BLOCK_W * (idx - CH_REG); oy = EXTRA_CHAR_Y; }
  else { ox = CHAR_ORIGIN_X + CHAR_BLOCK_W * (idx - CH_BARKEEP); oy = EXTRA_CHAR2_Y; }
  spr(ox + frame * CHAR_W, oy + dir * CHAR_H, CHAR_W, CHAR_H, x - 8, y - 20);
}

void drawSlime(const Enemy& e) {
  bool king = e.kind == K_KING;
  int sz = king ? KING_SIZE : SLIME_SIZE;
  int frame = std::min(3, (int)(e.hop / 0.8f * 4));  // squash, rise, stretch, land
  int x = (int)std::lround(e.x - camX), y = (int)std::lround(e.y + e.hh - camY);
  shadow(x, y - 1, king ? 15 : 8);
  spr((king ? KING_X : SLIME_X) + frame * sz, king ? KING_Y : SLIME_Y, sz, sz, x - sz / 2, y - sz + 1, false,
      e.flash > 0);
}

void drawChicken(const Enemy& e) {
  int frame = e.moving ? ((int)e.step) % 4 : 0;
  int x = (int)std::lround(e.x - camX) + (e.angry ? ((int)(S.t * 30)) % 2 : 0);
  int y = (int)std::lround(e.y + e.hh - camY);
  shadow(x, y - 1, 6);
  if (e.angry && e.flash <= 0) SDL_SetTextureColorMod(sheetTex, 255, 150, 140);
  spr(CHICKEN_X + frame * CHICKEN_SIZE, CHICKEN_Y, CHICKEN_SIZE, CHICKEN_SIZE, x - 8, y - 15, !e.faceRight, e.flash > 0);
  SDL_SetTextureColorMod(sheetTex, 255, 255, 255);
}

void drawCage(const Npc& n) {
  spr(CAGE_X, CAGE_Y, CAGE_W, CAGE_H, (int)std::lround(n.x - camX) - CAGE_W / 2, (int)std::lround(n.y - camY) + 6 - CAGE_H);
}

void drawHeart(int x, int y, bool full) { spr(full ? HEART_FULL_X : HEART_EMPTY_X, HEART_Y, 8, 8, x, y - 1); }

void drawCoin(int x, int y, int frame) { spr(COIN_X + (frame & 3) * COIN_SIZE, COIN_Y, COIN_SIZE, COIN_SIZE, x - 4, y - 4); }

// A swinging weapon (level 1 spoon, 2 Master Spork, 3 short sword) around the character at wx, wy.
void drawWeapon(float wx, float wy, int dir, float atk, int weapon) {
  if (atk <= 0 || weapon <= 0) return;
  float p = 1 - atk / 0.2f;
  static const float base[4] = {-1.5707963f, 0.f, 1.5707963f, 3.1415927f};
  float a = base[dir] + (-1 + 2 * p) * 1.0f;
  float ox = wx - camX, oy = wy - camY - 4;
  for (int k = 1; k <= 4; k++) {  // faint trail behind the swing
    float aa = a - k * 0.2f;
    R((int)std::lround(ox + std::cos(aa) * 13) - 1, (int)std::lround(oy + std::sin(aa) * 13) - 1, 3, 3, 0xffffff,
      (int)(90.f / k));
  }
  // the weapon sprite points up with its grip at (8, 14); turn it around the grip
  SDL_Rect src = {weapon >= 3 ? SWORD_X : (weapon == 2 ? SPORK_X : SPOON_X),
                  weapon >= 3 ? SWORD_Y : (weapon == 2 ? SPORK_Y : SPOON_Y), 16, 16};
  SDL_Rect dst = {(int)std::lround(ox) - 8, (int)std::lround(oy) - 14, 16, 16};
  SDL_Point pivot = {8, 14};
  SDL_RenderCopyEx(ren, sheetTex, &src, &dst, a * 57.29578 + 90.0, &pivot, SDL_FLIP_NONE);
}

// small health bar over a hurt enemy
void drawMiniBar(const Enemy& e) {  // the same bar for every enemy: just a red bar above it
  int top = e.kind == K_KING || e.kind == K_DRAGON ? 31 : 15;  // height of the sprite
  int x = (int)std::lround(e.x - camX), y = (int)std::lround(e.y + e.hh - camY) - top - 6;
  R(x - 10, y, 20, 4, 0x000000);
  R(x - 9, y + 1, 18, 2, 0x33262d);
  R(x - 9, y + 1, (int)std::lround(18 * std::max(0.f, e.hp) / e.max), 2, 0xe0453f);
}

void drawSwing() { drawWeapon(P.x, P.y, P.dir, P.atk, S.weapon); }

void drawGoblin(const Enemy& e) {
  int frame = e.moving ? ((int)e.step) % 4 : 0;
  int x = (int)std::lround(e.x - camX), y = (int)std::lround(e.y + e.hh - camY);
  shadow(x, y - 1, 6);
  spr(GOBLIN_X + frame * GOBLIN_SIZE, GOBLIN_Y, GOBLIN_SIZE, GOBLIN_SIZE, x - 8, y - 15, false, e.flash > 0);
}

void drawDragon(const Enemy& e) {
  int frame = e.st == 2 || e.st == 1 ? 4 : (e.spitT > 0 ? 5 : (e.st == 3 ? 0 : ((int)e.step) % 4));
  int x = (int)std::lround(e.x - camX), y = (int)std::lround(e.y + e.hh - camY);
  shadow(x, y - 1, 15);
  spr(DRAGON_X + frame * DRAGON_SIZE, DRAGON_Y, DRAGON_SIZE, DRAGON_SIZE, x - DRAGON_SIZE / 2, y - DRAGON_SIZE + 1, !e.faceRight, e.flash > 0);
}

void drawSheep(const Npc& n) {
  int fr = n.ramT > 0 ? ((int)(S.t * 12.f)) % 4 : n.back ? ((int)(S.t * 8.f)) % 4 : (((int)(S.t * 1.4f + n.id * 0.7f)) % 3 == 2 ? 2 : 0);  // now and then it shifts its weight
  int x = (int)std::lround(n.x - camX), y = (int)std::lround(n.y - camY) + 6;
  shadow(x, y - 1, 7);
  spr(SHEEP_X + fr * SHEEP_SIZE, SHEEP_Y, SHEEP_SIZE, SHEEP_SIZE, x - 8, y - 14, n.ramT > 0 ? P.x < n.x : (n.back ? n.hx < n.x : n.id % 2 == 0));
}

void drawFairy() {
  int x = (int)std::lround(fairyX - camX), y = (int)std::lround(fairyY - camY);
  spr(FAIRY_X + (((int)(S.t * 14)) % 3) * 8, FAIRY_Y, 8, 8, x - 4, y - 4);
}

// ============================================================================
// Drawing: world, HUD, screens
// ============================================================================
// ---- inside a building only the building is shown: everything outside its walls stays black ----
struct Indoor {
  bool on = false;
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // tile bounds of what is shown (the rooms and their walls)
  std::vector<uint8_t> vis;             // one flag per tile: part of this building
} indoor;

bool furnitureTile(int t) { return t == TILE_TABLE || t == TILE_BED || t == TILE_BAR || t == TILE_BOOKS; }
bool roomTile(int t) { return t == TILE_FLOOR || t == TILE_CFLOOR || t == TILE_CARPET; }
bool indoorVisible(float wx, float wy) {   // is this world position inside the building shown right now?
  int tx = (int)std::floor(wx / T), ty = (int)std::floor(wy / T);
  return tx >= 0 && ty >= 0 && tx < world.w && ty < world.h && indoor.vis[(size_t)ty * world.w + tx];
}

// Works out which building Tilda is in: all connected floor, carpet, table and door tiles, plus the walls around them.
void updateIndoor() {
  int tx = (int)std::floor(P.x / T), ty = (int)std::floor(P.y / T);
  int t = world.at(tx, ty);
  bool inside = tx >= 0 && ty >= 0 && tx < world.w && ty < world.h;
  if (inside && indoor.on && indoor.vis.size() == (size_t)world.w * world.h && indoor.vis[(size_t)ty * world.w + tx] &&
      (roomTile(t) || t == TILE_DOOR || furnitureTile(t)))
    return;  // still in the same building
  if (!inside || !roomTile(t)) { indoor.on = false; return; }
  const int W = world.w, H = world.h;
  std::vector<uint8_t> region((size_t)W * H, 0);
  std::vector<std::pair<int, int>> todo = {{tx, ty}};
  region[(size_t)ty * W + tx] = 1;
  while (!todo.empty()) {
    auto [x, y] = todo.back();
    todo.pop_back();
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; k++) {
      int nx = x + dx[k], ny = y + dy[k];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H || region[(size_t)ny * W + nx]) continue;
      int nt = world.at(nx, ny);
      if (!roomTile(nt) && nt != TILE_DOOR && !furnitureTile(nt)) continue;
      region[(size_t)ny * W + nx] = 1;
      todo.push_back({nx, ny});
    }
  }
  indoor.vis.assign((size_t)W * H, 0);
  indoor.x0 = W; indoor.y0 = H; indoor.x1 = 0; indoor.y1 = 0;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      if (!region[(size_t)y * W + x]) continue;
      bool door = world.at(x, y) == TILE_DOOR;
      for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
          int vx = x + i, vy = y + j;
          if (vx < 0 || vy < 0 || vx >= W || vy >= H) continue;
          if (door && j != 0 && !region[(size_t)vy * W + vx]) continue;  // no stub of wall outside the entrance
          indoor.vis[(size_t)vy * W + vx] = 1;
          indoor.x0 = std::min(indoor.x0, vx); indoor.x1 = std::max(indoor.x1, vx);
          indoor.y0 = std::min(indoor.y0, vy); indoor.y1 = std::max(indoor.y1, vy);
        }
    }
  indoor.on = true;
}

void drawWorld() {
  updateIndoor();
  int mapW = world.w * T, mapH = world.h * T;
  if (indoor.on) {  // small buildings are centred on the screen, big ones scroll but never show anything beyond their walls
    int bx0 = indoor.x0 * T, bx1 = (indoor.x1 + 1) * T, by0 = indoor.y0 * T, by1 = (indoor.y1 + 1) * T;
    camX = bx1 - bx0 <= VW ? bx0 - (VW - (bx1 - bx0)) / 2 : (int)std::lround(std::max((float)bx0, std::min(P.x - VW / 2.f, (float)(bx1 - VW))));
    camY = by1 - by0 <= VH ? by0 - (VH - (by1 - by0)) / 2 : (int)std::lround(std::max((float)by0, std::min(P.y - VH / 2.f, (float)(by1 - VH))));
    R(0, 0, VW, VH, 0x000000);
  } else {
    camX = mapW <= VW ? (mapW - VW) / 2 : (int)std::lround(std::max(0.f, std::min(P.x - VW / 2.f, (float)(mapW - VW))));
    camY = mapH <= VH ? (mapH - VH) / 2 : (int)std::lround(std::max(0.f, std::min(P.y - VH / 2.f, (float)(mapH - VH))));
    R(0, 0, VW, VH, 0x1b2a1e);
  }

  int x0 = std::max(0, camX / T), x1 = std::min(world.w - 1, (camX + VW) / T);
  int y0 = std::max(0, camY / T), y1 = std::min(world.h - 1, (camY + VH) / T);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      if (indoor.on && !indoor.vis[(size_t)y * world.w + x]) continue;  // outside the walls: nothing
      int t = world.at(x, y);
      SDL_Rect dst = {x * T - camX, y * T - camY, T, T};
      if (t >= TILE_COUNT) {  // unknown tile ID: magenta checker so mistakes in the file are easy to see
        R(dst.x, dst.y, T, T, 0xff00ff);
        R(dst.x, dst.y, 8, 8, 0x400040); R(dst.x + 8, dst.y + 8, 8, 8, 0x400040);
        continue;
      }
      int v = (int)((((unsigned)x * 73856093u) ^ ((unsigned)y * 19349663u)) >> 4) & 3;
      if (t == TILE_WATER) v = (int)(S.t * 3.f) & 3;
      if (t == TILE_GATE) v = keyOwned ? 1 : 0;  // locked door, then open cave mouth
      if (t == TILE_CROOF) {  // battlements along the top edge of the castle, slabs elsewhere
        int up = world.at(x, y - 1);
        v = (up == TILE_CROOF || up == TILE_CWALL || up == TILE_TOWER) ? 1 + (v % 3) : 0;
      }
      if (t == TILE_TOWER) v = (world.at(x - 1, y) == TILE_TOWER ? 1 : 0) | (world.at(x, y - 1) == TILE_TOWER ? 2 : 0);  // the 4 quarters of a cone
      if (t == TILE_CGATE) v = 0;
      if (t == TILE_BED) v = (world.at(x - 1, y) == TILE_BED ? 1 : 0) | (world.at(x, y - 1) == TILE_BED ? 2 : 0);  // the 4 quarters of the bed
      if (t == TILE_BOOKS) v = 0;
      SDL_Rect src = {v * T, t * T, T, T};
      SDL_RenderCopy(ren, tileTex, &src, &dst);
    }

  for (const Pickup& p : pickups) {
    if (indoor.on && !indoorVisible(p.x, p.y)) continue;
    if (p.life < 3 && ((int)(S.t * 8)) % 2) continue;
    int px = (int)std::lround(p.x - camX), py = (int)std::lround(p.y - camY + std::sin(p.ph) * 1.5f);
    R(px - 3, py + 4, 6, 1, 0x000000, 60);
    if (p.kind == 0) drawCoin(px, py, (int)(S.t * 8 + p.ph));
    else drawHeart(px - 4, py - 3, true);
  }

  struct Item { float y; std::function<void()> fn; };
  std::vector<Item> items;
  for (const Npc& n : npcs)
    if (!indoor.on || indoorVisible(n.x, n.y))
    items.push_back({n.y + 4, [&n] {
      if (n.type == OBJ_DANCER) {  // dancing: facing the room, then a full turn (left, back, right, front) now and then
        float ph = std::fmod(S.t, 4.f);
        static const int spin[4] = {3, 0, 1, 2};
        int dd = ph < 2.4f ? 2 : spin[std::min(3, (int)((ph - 2.4f) / 0.4f))];
        drawChar(n.sprite, dd, ((int)(S.t * 4.f)) & 3, n.x + std::sin(S.t * 4.f) * 1.5f, n.y);
      }
      else if (n.type == OBJ_SHEEP) drawSheep(n);
      else if (n.sprite >= 0) drawChar(n.sprite, n.dir, 0, n.x, n.y);
      if (n.type == OBJ_TUNK && S.quest < 3) drawCage(n);
    }});
  for (const Enemy& e : enemies)
    if (!e.dead && (!indoor.on || indoorVisible(e.x, e.y)))
      items.push_back({e.y + e.hh, [&e] {
        if (e.kind == K_CHICKEN) drawChicken(e);
        else if (e.kind == K_GOBLIN) drawGoblin(e);
        else if (e.kind == K_DRAGON) drawDragon(e);
        else drawSlime(e);
        if (e.hp < e.max - 0.001f) drawMiniBar(e);
      }});
  for (const Fire& f : fires)
    items.push_back({f.y + 6, [&f] {
      int x = (int)std::lround(f.x - camX), y = (int)std::lround(f.y - camY);
      spr(FIREBALL_X + (((int)(S.t * 14)) % 4) * FIREBALL_SIZE, FIREBALL_Y, FIREBALL_SIZE, FIREBALL_SIZE, x - 4, y - 4, f.vx < 0);
    }});
  if (tunk.active)
    items.push_back({tunk.y + 4, [] {
      drawChar(CH_TUNK, tunk.dir, tunk.moving ? ((int)tunk.walk) % 4 : 0, tunk.x, tunk.y);
      drawWeapon(tunk.x, tunk.y, tunk.dir, tunk.atk, tunk.weapon);
    }});
  items.push_back({P.y + 4, [] {
    if (S.mode == M_DEAD) return;
    if (P.inv > 0 && ((int)(S.t * 14)) % 2) return;
    drawChar(CH_TILDA, P.dir, P.moving ? ((int)P.walk) % 4 : 0, P.x, P.y);
    drawSwing();
  }});
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.y < b.y; });
  for (Item& it : items) it.fn();
  drawFairy();

  for (const Part& p : parts)
    R((int)std::lround(p.x - camX), (int)std::lround(p.y - camY), 2, 2, p.col,
      (int)(255 * std::max(0.f, std::min(1.f, p.life * 3))));

  for (const FloatText& f : floatTexts) {  // damage numbers float up and fade out
    int a = (int)(255 * std::min(1.f, f.life * 2.5f));
    int sx = (int)std::lround(f.x - camX), sy = (int)std::lround(f.y - camY - (1.0f - f.life) * 16.f);
    SDL_SetTextureAlphaMod(fontTex, (Uint8)a);
    drawText(f.text, sx, sy, f.col, 1, 1);
    SDL_SetTextureAlphaMod(fontTex, 255);
  }

  if (S.mode == M_PLAY && !dlg.active && S.near >= 0) {
    const Npc& n = npcs[S.near];
    int bx = (int)std::lround(n.x - camX) - 6, by = (int)std::lround(n.y - camY) - 36 + (int)std::lround(std::sin(S.t * 5));
    R(bx - 1, by - 1, 14, 13, 0x000000); R(bx, by, 12, 11, 0x141d24); R(bx, by, 12, 1, GOLD);
    drawText("E", bx + 6, by + 1, GOLD, 1, 1);
  }
  SDL_RenderCopy(ren, vigTex, nullptr, nullptr);
  if (fadeT > 0) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(std::min(1.f, fadeT / 0.3f) * 255));
    SDL_Rect all = {0, 0, VW, VH};
    SDL_RenderFillRect(ren, &all);
  }
}

void drawDialog() {
  const Line& line = dlg.lines[dlg.i];
  const int bx = 8, by = VH - 62, bw = VW - 16, bh = 54;
  R(bx - 2, by - 2, bw + 4, bh + 4, 0x000000);
  R(bx, by, bw, bh, 0x141d24);
  R(bx, by, bw, 1, GOLD); R(bx, by + bh - 1, bw, 1, GOLD); R(bx, by, 1, bh, GOLD); R(bx + bw - 1, by, 1, bh, GOLD);
  if (!line.who.empty()) {
    int nw = textW(line.who) + 12;
    R(bx + 6, by - 11, nw, 11, GOLD);
    drawText(line.who, bx + 12, by - 10, 0x141d24, 1);
  }
  auto lines = wrapText(line.text, (size_t)((bw - 20) / 6));
  int left = (int)dlg.shown;
  for (size_t i = 0; i < lines.size(); i++) {
    std::string part = lines[i].substr(0, (size_t)std::max(0, std::min(left, (int)lines[i].size())));
    left -= (int)lines[i].size() + 1;
    if (!part.empty()) drawText(part, bx + 10, by + 8 + (int)i * 12, 0xe7ecef, 1);
  }
  if (dlg.shown >= (float)line.text.size() && ((int)(S.t * 3)) % 2) {
    R(bx + bw - 14, by + bh - 12, 6, 1, GOLD); R(bx + bw - 13, by + bh - 11, 4, 1, GOLD); R(bx + bw - 12, by + bh - 10, 2, 1, GOLD);
  }
}

void drawHUD() {
  for (int i = 0; i < P.max; i++) drawHeart(6 + i * 9, 6, i < P.hp);
  if (S.regenT > 0) drawText("REGEN " + std::to_string((int)std::ceil(S.regenT)), 6 + P.max * 9 + 4, 6, 0x8fe39a);
  drawText(objective(), 6, 17, 0xe7ecef, 1);
  drawCoin(VW - 50, 9, 0);
  drawText("x " + std::to_string(S.coins), VW - 6, 5, GOLD, 1, 2);

  if (S.toastT > 0 && !dlg.active) {
    int w = textW(S.toast) + 14, x = (VW - w) / 2;
    int a = (int)(255 * std::min(1.f, S.toastT * 2));
    R(x, 34, w, 14, 0x0a1016, a * 220 / 255);
    if (S.toastT >= 0.5f) drawText(S.toast, VW / 2, 37, 0xe7ecef, 1, 1);
  }
}

void shade(uint32_t col, int a) { R(0, 0, VW, VH, col, a); }

const char* weaponName(int w) { return w >= 3 ? "Short Sword" : (w == 2 ? "Master Spork" : (w == 1 ? "Wooden Spoon" : "Bare hands")); }

void drawWeaponIcon(int w, int x, int y) {
  if (w <= 0) return;
  spr(w >= 3 ? SWORD_X : (w == 2 ? SPORK_X : SPOON_X), w >= 3 ? SWORD_Y : (w == 2 ? SPORK_Y : SPOON_Y), 16, 16, x, y);
}

// "0.5 - 1.0": what one hit can do with this weapon and strength (random 0 to 50 percent off)
std::string hitRange(int weaponCls, int strength) {
  float top = (float)(weaponCls + strength);
  char buf[32];
  snprintf(buf, sizeof buf, "%.1f - %.1f", top * 0.5f, top);
  return buf;
}

void drawFighter(int x, int weapon, int strength) {  // weapon, damage class, strength and hit range
  const uint32_t gray = 0x8fa3ad, white = 0xe7ecef;
  drawWeaponIcon(weapon, x - 2, 68);
  drawText(weaponName(weapon), x + 17, 70, white);
  drawText(weapon > 0 ? "Damage class " + std::to_string(weaponClass(weapon)) : "No weapon yet", x + 17, 80, gray);
  drawText("Strength: " + std::to_string(strength), x, 90, white);
  drawText(weapon > 0 || strength > 0 ? "Hit: " + hitRange(weaponClass(weapon), strength) : "Hit: 0", x, 100, white);
}

void drawStatusPage() {
  const uint32_t gray = 0x8fa3ad, white = 0xe7ecef;
  int lx = MENU_X + 14, rx = MENU_X + 142;
  drawText("TILDA", lx, 46, GOLD);
  for (int i = 0; i < P.max; i++) drawHeart(lx + i * 10, 58, i < P.hp);
  drawText(std::to_string(P.hp) + "/" + std::to_string(P.max), lx + P.max * 10 + 4, 57, white);
  drawFighter(lx, S.weapon, S.strength);
  drawCoin(lx + 4, 116, 0);
  drawText("Coins: " + std::to_string(S.coins), lx + 14, 112, white);
  drawText(std::string("Cave key: ") + (keyOwned ? "yes" : "no"), lx, 123, keyOwned ? 0x8fe39a : gray);

  drawText("TUNK", rx, 46, GOLD);
  if (!tunk.active) {
    drawText(S.quest >= 3 ? "Waiting in the meadow" : "Locked in a cage", rx, 59, white);
    drawText(S.quest >= 3 ? "Go and free him" : "Beat King Slime first", rx, 71, gray);
  } else {
    drawText("Walks with you", rx, 58, white);
    drawFighter(rx, tunk.weapon, tunk.strength);
  }
  drawText("Slimes beaten: " + std::to_string(S.kills), rx, 112, white);
  if (S.sheepQ == 1) drawText("Reg's sheep: " + std::to_string(sheepFound()) + "/5", rx, 123, white);
  else if (S.sheepQ == 2) drawText("Reg's sheep: all home", rx, 123, 0x8fe39a);
  R(MENU_X + 8, 134, MENU_W - 16, 1, 0x3a4650);
  drawText("QUEST", lx, 139, GOLD);
  std::vector<std::string> ln = wrapText(objective(), 31);
  for (size_t i = 0; i < ln.size() && i < 2; i++) drawText(ln[i], lx + 38, 139 + (int)i * 10, white);
}

// ---- the credits page: the text is assets/credits.txt (F5 reloads it) ----
// Lines starting with '#' are comments, "== Title" is a gold heading, an empty line is a gap. Long lines are wrapped.
// A line with just --- starts a new page. With several pages, the page turns by itself every 5 seconds.
const int CREDIT_LINES = 10;                              // lines per page
std::vector<std::pair<std::string, bool>> creditLines;   // text, is a heading
void loadCredits(const std::string& path) {
  FILE* f = fopen(path.c_str(), "r");
  creditLines.clear();
  if (!f) {
    creditLines = {{"Legend of Tilda", true}, {"by Gerd Kautzmann", false}, {"", false}, {"Freeware, MIT license", false}};
    return;
  }
  char buf[512];
  while (fgets(buf, sizeof buf, f)) {
    std::string l = buf;
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
    if (!l.empty() && l[0] == '#') continue;
    if (l == "---") { while (creditLines.size() % CREDIT_LINES) creditLines.push_back({"", false}); continue; }   // start a new page
    bool head = l.compare(0, 2, "==") == 0;
    if (head) l = l.substr(l.find_first_not_of("= ") == std::string::npos ? l.size() : l.find_first_not_of("= "));
    if (l.empty()) { creditLines.push_back({"", false}); continue; }
    for (const std::string& w : wrapText(l, 40)) creditLines.push_back({w, head});
  }
  fclose(f);
}

void drawCreditsPage() {
  const int PER = CREDIT_LINES;
  int n = (int)creditLines.size(), pages = std::max(1, (n + PER - 1) / PER);
  int page = pages > 1 ? (int)(SDL_GetTicks() / 5000) % pages : 0;   // real time: the game clock stands still in the menu
  for (int i = 0; i < PER; i++) {
    int k = page * PER + i;
    if (k >= n) break;
    const auto& L = creditLines[k];
    drawText(L.first, MENU_X + MENU_W / 2, 40 + i * 11, L.second ? GOLD : 0xe7ecef, 1, 1);
  }
  if (pages > 1) drawText(std::to_string(page + 1) + "/" + std::to_string(pages), MENU_X + MENU_W - 8, MENU_Y + 24, 0x5d6b73, 1, 2);
}

void drawMenu() {
  shade(0x0a1016, 190);
  R(MENU_X - 1, MENU_Y - 1, MENU_W + 2, MENU_H + 2, GOLD);
  R(MENU_X, MENU_Y, MENU_W, MENU_H, 0x141d24);
  static const char* tabs[MENU_TABS] = {"STATUS", "SAVE/LOAD", "SETTINGS", "CREDITS"};
  for (int t = 0; t < MENU_TABS; t++) {
    SDL_Rect r = menuTabRect(t);
    bool cur = t == menuTab;
    if (cur && menuSel == 0) R(r.x + 2, r.y, r.w - 4, r.h, 0xffffff, 28);
    drawText(tabs[t], r.x + r.w / 2, r.y + 4, cur ? GOLD : 0x8fa3ad, 1, 1);
    if (cur) R(r.x + 6, r.y + r.h - 1, r.w - 12, 2, GOLD);
  }
  R(MENU_X + 4, MENU_Y + 20, MENU_W - 8, 1, 0x3a4650);

  const uint32_t white = 0xe7ecef;
  auto rowBg = [&](int row) {
    SDL_Rect r = menuRowRect(menuTab, row);
    if (menuSel == row) {
      R(r.x, r.y, r.w, r.h, 0xffffff, 22);
      drawText(">", r.x + 4, r.y + (r.h - 8) / 2, GOLD);
    }
  };
  if (menuTab == 2) {
    static const char* labels[4] = {"Music", "Effects", "Sound", "Resume"};
    for (int i = 1; i <= 4; i++) {
      SDL_Rect r = menuRowRect(2, i);
      bool sel = menuSel == i;
      uint32_t col = sel ? GOLD : white;
      rowBg(i);
      drawText(labels[i - 1], r.x + 16, r.y + 6, col);
      if (i <= 2) {
        int v = i == 1 ? musicStep : sfxStep;
        for (int k = 0; k < 10; k++) {
          int bh = 3 + k;
          R(BAR_X + k * BAR_STEP, r.y + 14 - bh, BAR_STEP - 2, bh, k < v ? (sel ? GOLD : 0xc9a24a) : 0x3a4650);
        }
        drawText(std::to_string(v), r.x + r.w - 6, r.y + 6, col, 1, 2);
      } else if (i == 3) {
        drawText(muted ? "OFF" : "ON", BAR_X, r.y + 6, muted ? 0xff9a8f : 0x8fe39a);
      }
    }
    drawText("Up/Down choose  Left/Right change  Esc back", VW / 2, MENU_Y + MENU_H - 12, 0x8fa3ad, 1, 1);
  } else if (menuTab == 3) {
    drawCreditsPage();
    SDL_Rect r = menuRowRect(3, 1);
    rowBg(1);
    drawText("Back to the game", r.x + 16, r.y + 6, menuSel == 1 ? GOLD : white);
    drawText("Esc back   Left/Right: other pages", VW / 2, MENU_Y + MENU_H - 12, 0x8fa3ad, 1, 1);
  } else if (menuTab == 0) {
    drawStatusPage();
    SDL_Rect r = menuRowRect(0, 1);
    rowBg(1);
    drawText("Back to the game", r.x + 16, r.y + 6, menuSel == 1 ? GOLD : white);
    drawText("Esc back   Left/Right: other pages", VW / 2, MENU_Y + MENU_H - 12, 0x8fa3ad, 1, 1);
  } else {
    for (int i = 1; i <= SLOTS; i++) {
      SDL_Rect r = menuRowRect(1, i);
      rowBg(i);
      drawText("Slot " + std::to_string(i), r.x + 16, r.y + 4, menuSel == i ? GOLD : white);
      if (slotInfo[i - 1].used) drawText(slotInfo[i - 1].when, r.x + 64, r.y + 4, 0x8fa3ad);
      drawText(slotInfo[i - 1].used ? slotInfo[i - 1].line : "(empty)", r.x + 16, r.y + 13, slotInfo[i - 1].used ? 0x8fa3ad : 0x5d6b73);
      for (int c = 0; c < 2; c++) {
        SDL_Rect b = slotBtnRect(i, c);
        bool on = menuSel == i && slotCol == c;
        bool dim = c == 1 && !slotInfo[i - 1].used;
        R(b.x, b.y, b.w, b.h, on ? GOLD : 0x2a3640);
        drawText(c == 0 ? "SAVE" : "LOAD", b.x + b.w / 2, b.y + 5, on ? 0x141d24 : (dim ? 0x5d6b73 : white), 1, 1);
      }
    }
    for (int c = 0; c < 2; c++) {  // BACK TO GAME and QUIT GAME, selected like SAVE and LOAD
      SDL_Rect b = menuBtnRect(c);
      bool on = menuSel == SLOTS + 1 && slotCol == c;
      R(b.x, b.y, b.w, b.h, on ? GOLD : 0x2a3640);
      drawText(c == 0 ? "BACK TO GAME" : "QUIT GAME", b.x + b.w / 2, b.y + 5, on ? 0x141d24 : white, 1, 1);
    }
    if (!menuMsg.empty()) drawText(menuMsg, VW / 2, MENU_Y + MENU_H - 22, 0x8fe39a, 1, 1);
    drawText(saveDir.empty() ? "Saving is off in this run" : (menuSel == SLOTS + 1 ? "Left/Right: choose   E: do it" : "Left/Right: SAVE or LOAD   E: do it"), VW / 2, MENU_Y + MENU_H - 12, 0x8fa3ad, 1, 1);
  }
}

void drawStewIcon(int x, int y, bool onion = false) {
  R(x + 5, y + 1, 1, 2, 0xdddddd); R(x + 9, y, 1, 3, 0xdddddd);          // steam
  R(x + 2, y + 4, 12, 2, onion ? 0xe8d9a0 : 0xd9822b); R(x + 4, y + 4, 2, 1, onion ? 0xfff2c0 : 0xf0a850);     // the stew
  if (onion) { R(x + 8, y + 4, 2, 2, 0x9a4a9a); R(x + 11, y + 5, 1, 1, 0x9a4a9a); }
  R(x + 1, y + 6, 14, 1, 0xb07a42); R(x + 2, y + 7, 12, 2, 0x8a5a2b);    // bowl
  R(x + 4, y + 9, 8, 1, 0x6e4820);
}

void drawShop() {
  shade(0x0a1016, 190);
  int n = shopCount(), h = 40 + n * 30 + 26;
  R(SHOP_X - 1, SHOP_Y - 1, SHOP_W + 2, h + 2, GOLD);
  R(SHOP_X, SHOP_Y, SHOP_W, h, 0x141d24);
  drawText(shopKind == 1 ? "REG'S ONION STEW" : "BROTHILDA'S WARES", SHOP_X + 12, SHOP_Y + 8, GOLD);
  drawCoin(SHOP_X + SHOP_W - 44, SHOP_Y + 12, 0);
  drawText("x " + std::to_string(S.coins), SHOP_X + SHOP_W - 12, SHOP_Y + 8, GOLD, 1, 2);
  R(SHOP_X + 4, SHOP_Y + 24, SHOP_W - 8, 1, 0x3a4650);
  const uint32_t white = 0xe7ecef, gray = 0x8fa3ad;
  for (int i = 0; i < n; i++) {
    SDL_Rect r = shopRowRect(i);
    bool sel = i == shopSel, leave = i == n - 1;
    if (sel) { R(r.x, r.y, r.w, r.h, 0xffffff, 22); drawText(">", r.x + 4, r.y + (leave ? 10 : 9), GOLD); }
    uint32_t col = sel ? GOLD : white;
    if (leave) { drawText("Leave the shop", r.x + 24, r.y + 10, col); continue; }
    bool reg = shopKind == 1;
    int price = i == 0 ? (reg ? ONION_PRICE : STEW_PRICE) : SWORD_PRICE;
    bool owned = !reg && i == 1 && S.weapon >= 3;
    if (i == 0) drawStewIcon(r.x + 14, r.y + 6, reg); else drawWeaponIcon(3, r.x + 14, r.y + 5);
    drawText(i == 0 ? (reg ? "Onion Stew" : "Hot Stew") : "Short Sword", r.x + 36, r.y + 4, col);
    drawText(i == 0 ? (reg ? "A heart every 2 seconds" : "Restores all hearts") : "Damage class 4", r.x + 36, r.y + 15, gray);
    if (owned) {
      drawText("SOLD", r.x + r.w - 8, r.y + 9, 0x8fe39a, 1, 2);
    } else {
      drawText(std::to_string(price), r.x + r.w - 8, r.y + 9, S.coins >= price ? white : 0xff9a8f, 1, 2);
      drawCoin(r.x + r.w - 12 - textW(std::to_string(price)) - 4, r.y + 13, 0);
    }
  }
  if (!shopMsg.empty()) drawText(shopMsg, SHOP_X + SHOP_W / 2, SHOP_Y + h - 22, shopMsgOk ? 0x8fe39a : 0xff9a8f, 1, 1);
  drawText("Up/Down choose   E buy   Esc leave", SHOP_X + SHOP_W / 2, SHOP_Y + h - 12, gray, 1, 1);
}

void renderGame() {
  drawWorld();
  if (S.mode == M_TITLE) {
    shade(0x0a1016, 150);
    drawText("THE LEGEND OF", VW / 2, 30, 0xe7ecef, 2, 1);
    drawText("TILDA", VW / 2, 52, GOLD, 7, 1);
    drawText("A parody adventure. Rescue your brother Tunk!", VW / 2, 120, 0xe7ecef, 1, 1);
    if (((int)(S.t * 2)) % 2) drawText("Press E, Enter or tap to start", VW / 2, 142, 0xe7ecef, 1, 1);
    drawText("WASD/arrows move   E talk    M mute   Esc settings", VW / 2, 188, 0x8fa3ad, 1, 1);
    drawText("by Gerd Kautzmann - freeware, MIT license", VW / 2, 199, 0x5d6b73, 1, 1);
  } else if (S.mode != M_WON) {
    drawHUD();
    if (dlg.active) drawDialog();
  }
  if (S.mode == M_DEAD) {
    shade(0x460a0e, 140);
    drawText("YOU FELL", VW / 2, 66, 0xff9a8f, 4, 1);
    drawText("Tunk is still out there.", VW / 2, 104, 0xe7ecef, 1, 1);
    if (S.deadT > 0.8f) drawText("Press E to get back up", VW / 2, 120, 0xe7ecef, 1, 1);
  }
  if (S.mode == M_WON) {
    shade(0x0a1016, 175);
    drawText("TUNK IS SAFE!", VW / 2, 46, GOLD, 3, 1);
    drawText("Tilda rescued her brother from King Slime.", VW / 2, 86, 0xe7ecef, 1, 1);
    drawText("Slimes defeated: " + std::to_string(S.kills) + "     Coins: " + std::to_string(S.coins), VW / 2, 104,
             0xe7ecef, 1, 1);
    drawText("Press E to play again", VW / 2, 136, 0x8fa3ad, 1, 1);
    drawText("A parody. No famous green heroes were harmed.", VW / 2, 188, 0x8fa3ad, 1, 1);
  }
  if (menuOpen) drawMenu();
  if (shopOpen) drawShop();
}

// --- on-screen pad (only shown on touch devices) ---
void drawArrow(int cx, int cy, int half, int dir, uint32_t col, int a) {
  for (int k = 0; k < half; k++) {
    switch (dir) {
      case 0: R(cx - k, cy - half / 2 + k, 2 * k + 1, 1, col, a); break;
      case 2: R(cx - k, cy + half / 2 - k, 2 * k + 1, 1, col, a); break;
      case 3: R(cx - half / 2 + k, cy - k, 1, 2 * k + 1, col, a); break;
      default: R(cx + half / 2 - k, cy - k, 1, 2 * k + 1, col, a); break;
    }
  }
}

void drawPad() {
  for (const Btn& b : btns) {
    bool on = prevSyn[b.act];
    int a = on ? 150 : 70;
    R(b.r.x, b.r.y, b.r.w, b.r.h, 0x141d24, 130);
    R(b.r.x, b.r.y, b.r.w, b.r.h, 0xffffff, a / 3);
    uint32_t edge = on ? GOLD : 0xe7ecef;
    R(b.r.x, b.r.y, b.r.w, 2, edge, 200); R(b.r.x, b.r.y + b.r.h - 2, b.r.w, 2, edge, 200);
    R(b.r.x, b.r.y, 2, b.r.h, edge, 200); R(b.r.x + b.r.w - 2, b.r.y, 2, b.r.h, edge, 200);
    if (b.arrow >= 0)
      drawArrow(b.r.x + b.r.w / 2, b.r.y + b.r.h / 2, b.r.w / 3, b.arrow, on ? GOLD : 0xe7ecef, 230);
    else if (b.label[0]) {
      int sc = std::max(1, std::max(44, std::min(outW, outH) / 7) / 28);
      drawText(b.label, b.r.x + b.r.w / 2, b.r.y + b.r.h / 2 - 4 * sc, on ? GOLD : 0xe7ecef, sc, 1);
    }
  }
}

void renderFrame() {
  layout();
  SDL_SetRenderTarget(ren, frameTex);
  renderGame();
  SDL_SetRenderTarget(ren, nullptr);
  SDL_SetRenderDrawColor(ren, 0x0f, 0x16, 0x1b, 255);
  SDL_RenderClear(ren);
  SDL_RenderCopy(ren, frameTex, nullptr, &gameRect);
  if (touchMode) drawPad();
}

}  // namespace

// ============================================================================
// Program entry
// ============================================================================
static void usage(const char* exe) {
  printf(
      "The Legend of Tilda\n\n"
      "usage: %s [world.bin] [options]\n\n"
      "  world.bin      world file to load (default: assets/world.bin)\n"
      "  --sprites FILE sprite map to load (default: assets/sprite_map.png)\n"
      "  --tiles FILE   tile map to load (default: assets/tile_map.png)\n"
      "  --touch        always show the on-screen pad\n"
      "  --size WxH     initial window size (default 960x624)\n"
      "  --music FILE   background music, a ProTracker .mod (replaces assets/music1.mod; music2.mod = dancer's song, music3.mod = caves)\n"
      "  --no-music     play without the background music\n"
      "  --userdir DIR  keep settings and save games in DIR (default: your user data folder)\n"
      "  --no-audio     start without sound\n"
      "  -h, --help     this text\n\n"
      "keys: WASD/arrows move, E talk, Space swing, M sound, F5 reload world + sprites (restarts), F6 reload sprites only, Esc settings (volume, quit)\n\n"
      "test options (run without a window loop, then save a screenshot):\n"
      "  --script S     scripted input, e.g. \"1:talk,60:right,10:swing+right\" = hold the\n"
      "                 given actions for N frames each (up down left right talk swing mute, or - for none)\n"
      "  --shot FILE    save the final frame as a BMP file\n"
      "  --quest N      start at quest stage N (0..6; 4 = Tunk walks with you, 5 = sword and key)\n"
      "  --at X,Y       put Tilda on a tile\n"
      "  --coins N      start with N coins\n"
      "  --hp N         start with N hearts (1..5)\n"
      "  --weapon N     Tilda's weapon: 1 spoon, 2 spork, 3 short sword (the old one goes to Tunk)\n"
      "  --strength N   Tilda's strength\n"
      "  --sheep N      Reg's sheep quest is running and N of the 5 sheep are already home\n"
      "  --god          Tilda takes no damage (also works in normal play)\n",
      exe);
}

static uint32_t parseActs(const std::string& s) {
  static const char* names[] = {"up", "down", "left", "right", "talk", "swing", "mute", "restart", "reload", "sprites", "menu", "examine"};
  uint32_t m = 0;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find('+', i);
    std::string w = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
    for (int a = 0; a < A_COUNT; a++)
      if (w == names[a]) m |= 1u << a;
    if (j == std::string::npos) break;
    i = j + 1;
  }
  return m;
}

static bool saveShot(const std::string& file) {
  int w, h;
  SDL_GetRendererOutputSize(ren, &w, &h);
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return false;
  SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  bool ok = SDL_SaveBMP(s, file.c_str()) == 0;
  SDL_FreeSurface(s);
  return ok;
}

int main(int argc, char** argv) {
  std::string worldPath, spritePath, tilePath, musicPath, userDir, shotFile, scriptStr;
  bool noMusic = false;
  bool forceTouch = false, noAudio = false, godMode = false;
  int startQuest = -1, atX = -1, atY = -1, winW = 960, winH = 624, startCoins = -1, startHp = -1, startWeapon = -1, startStrength = -1, startSheep = -1; bool startAngry = false; std::vector<std::pair<std::string, int>> startFlags;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
    else if (a == "--touch") forceTouch = true;
    else if (a == "--no-audio") noAudio = true;
    else if (a == "--god") godMode = true;
    else if (a == "--coins" && i + 1 < argc) startCoins = atoi(argv[++i]);
    else if (a == "--hp" && i + 1 < argc) startHp = atoi(argv[++i]);
    else if (a == "--weapon" && i + 1 < argc) startWeapon = atoi(argv[++i]);
    else if (a == "--strength" && i + 1 < argc) startStrength = atoi(argv[++i]);
    else if (a == "--flag" && i + 1 < argc) {   // test: --flag name=value
      std::string fv = argv[++i];
      size_t eq = fv.find('=');
      startFlags.push_back({fv.substr(0, eq), eq == std::string::npos ? 1 : atoi(fv.c_str() + eq + 1)});
    }
    else if (a == "--angry") startAngry = true;   // test: every chicken is angry from the start
    else if (a == "--sheep" && i + 1 < argc) startSheep = atoi(argv[++i]);
    else if (a == "--shot" && i + 1 < argc) shotFile = argv[++i];
    else if (a == "--sprites" && i + 1 < argc) spritePath = argv[++i];
    else if (a == "--tiles" && i + 1 < argc) tilePath = argv[++i];
    else if (a == "--music" && i + 1 < argc) musicPath = argv[++i];
    else if (a == "--userdir" && i + 1 < argc) userDir = argv[++i];
    else if (a == "--no-music") noMusic = true;
    else if (a == "--script" && i + 1 < argc) scriptStr = argv[++i];
    else if (a == "--quest" && i + 1 < argc) startQuest = atoi(argv[++i]);
    else if (a == "--at" && i + 1 < argc) sscanf(argv[++i], "%d,%d", &atX, &atY);
    else if (a == "--size" && i + 1 < argc) sscanf(argv[++i], "%dx%d", &winW, &winH);
    else if (a[0] != '-') worldPath = a;
    else { fprintf(stderr, "unknown option %s (try --help)\n", a.c_str()); return 1; }
  }
  const bool scripted = !scriptStr.empty() || !shotFile.empty();
  if (scripted) gen.seed(1);

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  // These hints only exist in newer SDL2 versions; older ones simply do not need them.
#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
#ifdef SDL_HINT_MOUSE_TOUCH_EVENTS
  SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  // find and load the world file
  // An explicit path is used as given, so mistakes in your own file are reported instead of hidden.
  std::vector<std::string> candidates;
  if (!worldPath.empty()) {
    candidates.push_back(worldPath);
  } else {
    candidates.push_back("assets/world.bin");
    if (char* base = SDL_GetBasePath()) {
      candidates.push_back(std::string(base) + "assets/world.bin");
      candidates.push_back(std::string(base) + "../assets/world.bin");
      SDL_free(base);
    }
  }
  std::string err, loadedFrom;
  bool loaded = false;
  for (const std::string& c : candidates) {
    std::string e;
    if (loadWorld(c, world, e)) { loaded = true; loadedFrom = c; break; }
    if (err.empty()) err = e;
  }
  if (loaded) {
    std::string ne;
    if (!loadNpcs(npcFileFor(loadedFrom), npcObjs, ne)) fprintf(stderr, "Warning: %s (the game will have no characters)\n", ne.c_str());
    std::string se;
    if (!loadStory(storyFileFor(loadedFrom), story, se)) fprintf(stderr, "Warning: story.txt: %s(people will have nothing to say)\n", se.c_str());
    loadDragonCfg(storyFileFor(loadedFrom).substr(0, storyFileFor(loadedFrom).size() - 9) + "dragon.txt");
    loadCredits(storyFileFor(loadedFrom).substr(0, storyFileFor(loadedFrom).size() - 9) + "credits.txt");
  }
  if (!loaded) {
    fprintf(stderr, "Could not load a world file: %s\n", err.c_str());
    if (worldPath.empty()) fprintf(stderr, "(run `make world` to create assets/world.bin)\n");
    SDL_Quit();
    return 1;
  }

  SDL_Window* win = SDL_CreateWindow("The Legend of Tilda", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW,
                                     winH, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
  if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  SDL_SetWindowMinimumSize(win, VW, VH);
  ren = SDL_CreateRenderer(win, -1, scripted ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_RendererInfo info;
  SDL_GetRendererInfo(ren, &info);
  const bool vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
  buildTextures();

  // find and load the sprite map and the tile map (next to the world file, in assets/, or next to the executable)
  auto assetCandidates = [&](const std::string& given, const char* name) {
    std::vector<std::string> out;
    if (!given.empty()) { out.push_back(given); return out; }
    size_t sl = worldPath.find_last_of("/\\");
    if (sl != std::string::npos) out.push_back(worldPath.substr(0, sl + 1) + name);
    out.push_back(std::string("assets/") + name);
    if (char* base = SDL_GetBasePath()) {
      out.push_back(std::string(base) + "assets/" + name);
      out.push_back(std::string(base) + "../assets/" + name);
      SDL_free(base);
    }
    return out;
  };
  std::string spriteFrom, tileFrom;
  for (int pass = 0; pass < 2; pass++) {
    const bool sprites = pass == 0;
    std::string firstErr, from;
    bool ok = false;
    for (const std::string& c : assetCandidates(sprites ? spritePath : tilePath, sprites ? "sprite_map.png" : "tile_map.png")) {
      std::string e;
      if (sprites ? loadSprites(c, e) : loadTiles(c, e)) { ok = true; from = c; break; }
      if (firstErr.empty()) firstErr = e;
    }
    if (!ok) {
      fprintf(stderr, "Could not load the %s: %s\n", sprites ? "sprite map" : "tile map", firstErr.c_str());
      SDL_Quit();
      return 1;
    }
    (sprites ? spriteFrom : tileFrom) = from;
  }

  // saved settings (volumes, mute) live in the user's data folder; scripted test runs never touch them
  if (!userDir.empty()) {
    saveDir = userDir;
    if (saveDir.back() != '/' && saveDir.back() != '\\') saveDir += '/';
  } else if (!scripted) {
    if (char* pref = SDL_GetPrefPath("tilda", "legend-of-tilda")) {
      saveDir = pref;
      SDL_free(pref);
    }
  }
  if (!saveDir.empty()) settingsPath = saveDir + "settings.txt";
  loadSettings();

  // background music: an explicit --music file must load (or we say why); the default is optional
  if (!noMusic && !noAudio && !scripted) {
    // music1.mod, music2.mod and music3.mod: --music FILE replaces music1
    for (int ti = 0; ti < 3; ti++) {
      std::string nm = "music" + std::to_string(ti + 1) + ".mod";
      std::vector<std::string> mc;
      if (ti == 0 && !musicPath.empty()) {
        mc.push_back(musicPath);
      } else {
        size_t sl = worldPath.find_last_of("/\\");
        if (sl != std::string::npos) mc.push_back(worldPath.substr(0, sl + 1) + nm);
        mc.push_back("assets/" + nm);
        if (char* base = SDL_GetBasePath()) {
          mc.push_back(std::string(base) + "assets/" + nm);
          mc.push_back(std::string(base) + "../assets/" + nm);
          SDL_free(base);
        }
      }
      std::string first;
      bool ok = false;
      for (const std::string& c : mc) {
        std::string e;
        if (tracks[ti].load(c, e)) { ok = true; break; }
        if (first.empty()) first = e;
      }
      if (!ok && ti == 0 && !musicPath.empty()) fprintf(stderr, "No music: %s\n", first.c_str());
    }
  }

  // audio is optional: if it cannot start, the game is just quiet
  if (!noAudio && !scripted && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024;
    want.callback = audioCallback;
    audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDev) { audioRate = have.freq; SDL_PauseAudioDevice(audioDev, 0); }
  }

#if defined(__ANDROID__) || defined(__IPHONEOS__)
  touchMode = true;
#endif
  if (forceTouch) touchMode = true;

  resetGame();
  S.god = godMode;
  if (startCoins >= 0) S.coins = startCoins;
  if (startHp > 0) P.hp = std::min(P.max, startHp);
  if (startQuest >= 1) { setWeapon(1); S.quest = std::min(startQuest, 10); }
  if (startQuest >= 2) { giveWeapon(2); S.kills = 5; spawnKing(); }
  if (startQuest >= 3) S.strength = 1;  // King Slime has been beaten
  if (startQuest >= 3) {
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.kind == K_KING; }),
                  enemies.end());
  }
  if (startQuest >= 4) {  // Tunk is rescued and walks with Tilda
    tunkJoin();
    if (startQuest >= 5) keyOwned = true;
  }
  if (atX >= 0 && atY >= 0) { P.x = atX * T + 8.f; P.y = atY * T + 8.f; fairyX = P.x; fairyY = P.y - 18; }
  if (startWeapon >= 1) giveWeapon(std::min(3, startWeapon));
  if (startStrength >= 0) S.strength = startStrength;
  for (const auto& sf : startFlags) setStoryVar(sf.first, sf.second);
  refreshLair();
  if (startAngry) for (Enemy& c : enemies) if (c.kind == K_CHICKEN) c.angry = true;
  if (startSheep >= 0) {
    S.sheepQ = 1;
    for (int k = 0; k < std::min(5, startSheep); k++) sheepHome(k);
  }
  if (tunk.active) tunkWarp(P.x, P.y);

  auto pollEvents = [&](bool& quit) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      switch (ev.type) {
        case SDL_QUIT: quit = true; break;
        case SDL_KEYDOWN:
          if (ev.key.repeat) break;
          {
            int a = actOfKey(ev.key.keysym.scancode);
            if (a >= 0) press[a] = true;
          }
          break;
        case SDL_FINGERDOWN:
          touchMode = true;
          fingerDown((long long)ev.tfinger.fingerId, ev.tfinger.x * outW, ev.tfinger.y * outH);
          break;
        case SDL_FINGERMOTION: fingerMove((long long)ev.tfinger.fingerId, ev.tfinger.x * outW, ev.tfinger.y * outH); break;
        case SDL_FINGERUP: fingerUp((long long)ev.tfinger.fingerId); break;
        case SDL_MOUSEBUTTONDOWN:
          if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT)
            fingerDown(-1, (float)ev.button.x, (float)ev.button.y);
          break;
        case SDL_MOUSEMOTION:
          if (ev.motion.which != SDL_TOUCH_MOUSEID && (ev.motion.state & SDL_BUTTON_LMASK))
            fingerMove(-1, (float)ev.motion.x, (float)ev.motion.y);
          break;
        case SDL_MOUSEBUTTONUP:
          if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT) fingerUp(-1);
          break;
        default: break;
      }
    }
  };
  auto clearPress = [] { for (bool& p : press) p = false; };
  auto reloadSprites = [&](bool announce) {
    std::string e, e2;
    bool a = loadSprites(spriteFrom, e), b = loadTiles(tileFrom, e2);
    if (a && b) {
      if (announce) toast("Reloaded sprite map and tile map", 3);
      return true;
    }
    toast("Sprite or tile reload failed, see terminal", 3);
    if (!a) fprintf(stderr, "%s\n", e.c_str());
    if (!b) fprintf(stderr, "%s\n", e2.c_str());
    return false;
  };
  auto reloadWorld = [&] {
    World nw;
    std::string e;
    bool spritesReloaded = reloadSprites(false);
    if (loadWorld(loadedFrom, nw, e)) {
      world = std::move(nw);
      std::string ne;
      if (!loadNpcs(npcFileFor(loadedFrom), npcObjs, ne)) fprintf(stderr, "Warning: %s (keeping the old characters)\n", ne.c_str());
      std::string se;
      if (!loadStory(storyFileFor(loadedFrom), story, se)) fprintf(stderr, "Warning: story.txt: %s(keeping the old story)\n", se.c_str());
      loadDragonCfg(storyFileFor(loadedFrom).substr(0, storyFileFor(loadedFrom).size() - 9) + "dragon.txt");
      loadCredits(storyFileFor(loadedFrom).substr(0, storyFileFor(loadedFrom).size() - 9) + "credits.txt");
      Mode m = S.mode;
      resetGame();
      S.mode = m == M_TITLE ? M_TITLE : M_PLAY;
      toast(spritesReloaded ? "Reloaded world and sprites (game restarted)" : "Reloaded world (sprites failed)", 3);
    } else {
      toast("Reload failed, see terminal", 3);
      fprintf(stderr, "%s\n", e.c_str());
    }
  };

  const float step = 1.0f / 60.0f;

  if (scripted) {
    // scripted run: fixed steps with no real-time waiting, then one screenshot
    bool quit = false;
    layout();
    std::vector<std::pair<int, uint32_t>> script;
    size_t i = 0;
    while (i < scriptStr.size()) {
      size_t j = scriptStr.find(',', i);
      std::string tok = scriptStr.substr(i, j == std::string::npos ? std::string::npos : j - i);
      i = j == std::string::npos ? scriptStr.size() : j + 1;
      size_t c = tok.find(':');
      if (c == std::string::npos) continue;
      script.push_back({atoi(tok.substr(0, c).c_str()), parseActs(tok.substr(c + 1))});
    }
    for (auto& st : script)
      for (int f = 0; f < st.first && !quit; f++) {
        pollEvents(quit);
        sampleInput(st.second);
        if (press[A_RELOAD]) reloadWorld();
        if (press[A_RELOAD_SPRITES]) reloadSprites(true);
        update(step);
        clearPress();
      }
    renderFrame();
    if (!shotFile.empty()) {
      if (saveShot(shotFile)) printf("saved %s\n", shotFile.c_str());
      else fprintf(stderr, "could not save %s\n", shotFile.c_str());
    }
    int angry = 0;
    for (const Enemy& e : enemies) {
      angry += e.kind == K_CHICKEN && e.angry;
      if (getenv("TILDA_DEBUG") && e.kind == K_CHICKEN) printf("  chicken at (%.1f,%.1f) kt=%.2f\n", e.x, e.y, e.kt);
    }
    if (getenv("TILDA_DEBUG")) printf("  player at (%.1f,%.1f) dir=%d\n", P.x, P.y, P.dir);
    printf("state: mode=%d quest=%d kills=%d coins=%d hp=%d pos=(%d,%d) enemies=%zu chickenHits=%d angryChickens=%d weapon=%d key=%d tunk=%d(%d,%d,w%d) str=%d/%d sheep=%d/%d regen=%.0f tunkmax=%.1f flags=%s\n",
           (int)S.mode, S.quest, S.kills, S.coins, P.hp, (int)(P.x / T), (int)(P.y / T), enemies.size(), S.chickHits,
           angry, S.weapon, (int)keyOwned, (int)tunk.active, (int)(tunk.x / T), (int)(tunk.y / T), tunk.weapon, S.strength, tunk.strength, S.sheepQ, sheepFound(), S.regenT, dbgTunkMax, dbgFlags().c_str());
  } else {
    bool quit = false;
    Uint64 last = SDL_GetPerformanceCounter();
    float acc = 0;
    while (!quit) {
      pollEvents(quit);
      if (wantQuit) quit = true;
      Uint64 now = SDL_GetPerformanceCounter();
      acc += std::min(0.25f, (float)((double)(now - last) / (double)SDL_GetPerformanceFrequency()));
      last = now;
      layout();
      sampleInput(0);
      if (press[A_RELOAD]) { reloadWorld(); press[A_RELOAD] = false; }
      if (press[A_RELOAD_SPRITES]) { reloadSprites(true); press[A_RELOAD_SPRITES] = false; }
      while (acc >= step) {
        update(step);
        clearPress();
        acc -= step;
      }
      renderFrame();
      SDL_RenderPresent(ren);
      if (!vsync) SDL_Delay(2);
    }
  }

  if (audioDev) SDL_CloseAudioDevice(audioDev);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
