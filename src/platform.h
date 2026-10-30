// Platform layer: window / renderer / audio / input / assets (SDL2, PC + PS Vita).
#pragma once

#include <SDL.h>
#include <SDL_mixer.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

constexpr int SCREEN_W = 960;   // PS Vita native resolution
constexpr int SCREEN_H = 544;

struct Texture {
	SDL_Texture* tex = nullptr;
	int w = 0, h = 0;
};

struct Sound {
	Mix_Chunk* chunk = nullptr;
	float volume = 1.0f;
	bool loop = false;
};

// Inputs of one local player, as the game objects read them (one field per action).
struct PlayerInput {
	bool up = false, down = false, left = false, right = false;   // move / climb / crouch
	bool jump = false;
	bool punch = false, kick = false;
	bool aim = false;        // hold to aim (R on Vita; D on keyboard)
	bool grab = false;       // L: grab a fighter next to you, otherwise throw the melee weapon
	bool fire = false;       // shoot the gun
	bool grenade = false;    // hold to cook, release to throw
	bool interact = false;   // pick up
	bool skill = false;      // active skill (Select)
	bool run = false;        // bots: run without the double tap
	bool hop = false;        // a plain jump even on the run (bots clearing an obstacle)
	// right analog stick: aims while still allowing movement
	bool stickAim = false;
	float stickX = 0, stickY = 0;   // -1..1, y down
	// menus
	bool menuConfirm = false, menuBack = false;
};

namespace platform {
// Keyboard-style mapping used by the PC keyboard and the tests:
// up = jump/climb, a = punch (fire while aiming), s = kick (grenade while aiming), d = aim, f = pick up.
PlayerInput keyboardInput(bool up, bool down, bool left, bool right, bool a, bool s, bool d, bool f);
}

// Edge-triggered events for the menus.
struct MenuInput {
	bool up = false, down = false, left = false, right = false;
	bool confirm = false, back = false, start = false;
	bool lb = false, rb = false, alt = false;   // bumpers, Triangle
};

struct SpriteDraw {
	Texture* tex = nullptr;
	int sx = 0, sy = 0, sw = 0, sh = 0;   // source rectangle
	float ox = 0, oy = 0;                 // world-space origin (translate)
	float angle = 0;                      // radians, rotation about origin
	float scaleX = 1, scaleY = 1;         // negative = mirrored
	float offX = 0, offY = 0;             // local offset of the top-left corner
	float dw = 0, dh = 0;                 // local destination size
};

namespace platform {

struct TileCell {
	int sx, sy, x, y;      // 8x8 from the sheet, placed at x, y
};

bool init(const char* argv0);
void shutdown();

// assets
Texture* image(const std::string& path);
Sound* sound(const std::string& path, float volume = 1.0f, bool loop = false);
std::string assetPath(const std::string& rel);
std::string readFile(const std::string& rel);          // contents of an asset text file
std::vector<std::string> listFiles(const std::string& dirRel, const std::string& ext);
std::vector<std::string> listUserFiles(const std::string& dirRel, const std::string& ext);   // in the data folder
void makeUserFolder(const std::string& dirRel);
// a picture put together from tiles in memory; there is one at a time, the next call replaces it
Texture* composeTiles(const std::string& sheet, int w, int h, const std::vector<TileCell>& cells);
// player data (settings, profile): ux0:data/superfighters on the Vita, ~/.superfighters on PC
std::string readUserFile(const std::string& name);
bool writeUserFile(const std::string& name, const std::string& data);

// frame
bool pumpEvents();                  // returns false when the user closed the app
void beginFrame();
void endFrame();

// drawing
void setCamera(float x, float y, float zoom);   // x,y = top-left of the view in world px
void drawSprite(const SpriteDraw& d);
void drawRect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);  // screen space
void drawText(const std::string& text, float x, float y, float scale = 1.0f,
			  uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
int textWidth(const std::string& text, float scale = 1.0f);
size_t charCount(const std::string& utf8);          // letters, not bytes
// the same from another font: a 16 column sheet of cw x ch cells, a letter every `advance` pixels
void drawTextSheet(Texture* sheet, int cw, int ch, int advance, const std::string& text, float x, float y, float scale,
				   uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
void setAlpha(float alpha);          // applies to following sprites (0..1)
// screen-space picture (HUD, menus); source rect sw/sh <= 0 means the whole texture
void drawImage(Texture* tex, float x, float y, float scale = 1.0f, int sx = 0, int sy = 0, int sw = 0, int sh = 0,
			   bool flipX = false, uint8_t alpha = 255);

// audio
void playSound(Sound* s);
void playSoundRandom(const std::vector<Sound*>& list);
void setMute(bool mute);
void setVolumes(float music, float sounds);   // 0..1, on top of each sound's own volume
void stopAllSounds();                // safe to call after shutdown
void playMusic(const std::string& path, float volume = 0.6f);   // loops; "" stops
void stopMusic();

// input
PlayerInput playerInput(int localId);
MenuInput menuInput();
int localPlayerSlots();              // how many players can be controlled locally

// testing helpers (PC): scripted input and screenshots
void setInputOverride(int localId, const PlayerInput* in);   // nullptr = back to real devices
bool saveScreenshot(const std::string& path);                // PNG of the current frame

// log (stdout on PC, file on Vita)
void log(const char* fmt, ...);

}  // namespace platform
