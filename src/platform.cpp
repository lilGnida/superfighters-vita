#include "platform.h"

#include <SDL_image.h>
#include <dirent.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef __vita__
#include <sys/stat.h>
#endif
#ifdef __vita__
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#endif

namespace platform {

namespace {

SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
std::string g_assetRoot;
std::map<std::string, std::unique_ptr<Texture>> g_images;
std::map<std::string, std::unique_ptr<Sound>> g_sounds;
std::map<std::string, Mix_Chunk*> g_chunks;
Texture* g_font = nullptr;
bool g_mute = false;
bool g_quit = false;
float g_camX = 0, g_camY = 0, g_zoom = 1;
float g_alpha = 1.0f;
FILE* g_logFile = nullptr;

constexpr int FONT_CW = 8, FONT_CH = 12;
constexpr float kPi = 3.14159265358979f;

// ---- input state
bool g_prevMenu[16] = {};
SDL_GameController* g_pads[2] = {nullptr, nullptr};

enum MenuBit { M_UP, M_DOWN, M_LEFT, M_RIGHT, M_CONFIRM, M_BACK, M_START, M_LB, M_RB, M_ALT, M_COUNT };

bool fileExists(const std::string& p) {
	FILE* f = fopen(p.c_str(), "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

Texture* placeholderTexture() {
	static Texture ph;
	if (!ph.tex) {
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
		SDL_FillRect(s, nullptr, SDL_MapRGBA(s->format, 255, 0, 255, 255));
		ph.tex = SDL_CreateTextureFromSurface(g_renderer, s);
		ph.w = ph.h = 8;
		SDL_FreeSurface(s);
	}
	return &ph;
}

}  // namespace

void log(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
#ifdef __vita__
	if (!g_logFile) {
		sceIoMkdir("ux0:data/superfighters", 0777);
		g_logFile = fopen("ux0:data/superfighters/log.txt", "a");
	}
	if (g_logFile) {
		vfprintf(g_logFile, fmt, ap);
		fputc('\n', g_logFile);
		fflush(g_logFile);
	}
#else
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
#endif
	va_end(ap);
}

std::string assetPath(const std::string& rel) { return g_assetRoot + rel; }

std::string readFile(const std::string& rel) {
	std::string out;
	FILE* f = fopen(assetPath(rel).c_str(), "rb");
	if (!f) return out;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
	fclose(f);
	return out;
}

namespace {

std::string userDir() {
#ifdef __vita__
	sceIoMkdir("ux0:data/superfighters", 0777);
	return "ux0:data/superfighters/";
#else
	const char* override = getenv("SF_USER_DIR");
	if (override) return std::string(override) + "/";
	const char* home = getenv("HOME");
	std::string dir = std::string(home ? home : ".") + "/.superfighters";
	mkdir(dir.c_str(), 0755);
	return dir + "/";
#endif
}

}  // namespace

std::string readUserFile(const std::string& name) {
	std::string out;
	FILE* f = fopen((userDir() + name).c_str(), "rb");
	if (!f) return out;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
	fclose(f);
	return out;
}

bool writeUserFile(const std::string& name, const std::string& data) {
	FILE* f = fopen((userDir() + name).c_str(), "wb");
	if (!f) return false;
	bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	fclose(f);
	return ok;
}

std::vector<std::string> listFiles(const std::string& dirRel, const std::string& ext) {
	std::vector<std::string> out;
	DIR* d = opendir(assetPath(dirRel).c_str());
	if (!d) return out;
	while (dirent* e = readdir(d)) {
		std::string name = e->d_name;
		if (name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
			out.push_back(name);
	}
	closedir(d);
	std::sort(out.begin(), out.end());
	return out;
}

bool init(const char* argv0) {
#ifdef __vita__
	g_assetRoot = "app0:/assets/";
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	(void)argv0;
#else
	// look for assets/ next to the executable, then in the working directory
	std::string base;
	if (char* bp = SDL_GetBasePath()) {
		base = bp;
		SDL_free(bp);
	}
	if (fileExists(base + "assets/objects.json"))
		g_assetRoot = base + "assets/";
	else if (fileExists("assets/objects.json"))
		g_assetRoot = "assets/";
	else if (fileExists(base + "../assets/objects.json"))
		g_assetRoot = base + "../assets/";
	else {
		log("assets not found (looked next to %s and in the working directory)", argv0 ? argv0 : "exe");
		return false;
	}
#endif

	Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO;
#ifndef __vita__
	flags |= SDL_INIT_GAMECONTROLLER;
#endif
	if (SDL_Init(flags) != 0) {
		log("SDL_Init failed: %s", SDL_GetError());
		return false;
	}

	g_window = SDL_CreateWindow("Super Fighters", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W,
								SCREEN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!g_window) {
		log("SDL_CreateWindow failed: %s", SDL_GetError());
		return false;
	}

	g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!g_renderer) g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
	if (!g_renderer) {
		log("SDL_CreateRenderer failed: %s", SDL_GetError());
		return false;
	}
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest: crisp pixel art
	SDL_RenderSetLogicalSize(g_renderer, SCREEN_W, SCREEN_H);
	SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);

	IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0)
		log("Mix_OpenAudio failed: %s (continuing without audio)", Mix_GetError());
	else
		Mix_AllocateChannels(24);

#ifndef __vita__
	for (int i = 0, slot = 0; i < SDL_NumJoysticks() && slot < 2; i++)
		if (SDL_IsGameController(i)) g_pads[slot++] = SDL_GameControllerOpen(i);
#endif

	g_font = image("font.png");
	return true;
}

void shutdown() {
	for (auto& kv : g_images)
		if (kv.second->tex) SDL_DestroyTexture(kv.second->tex);
	g_images.clear();
	stopMusic();
	Mix_HaltChannel(-1);
	for (auto& kv : g_chunks) Mix_FreeChunk(kv.second);
	g_chunks.clear();
	g_sounds.clear();
	Mix_CloseAudio();
	IMG_Quit();
	if (g_renderer) SDL_DestroyRenderer(g_renderer);
	if (g_window) SDL_DestroyWindow(g_window);
	SDL_Quit();
}

// ------------------------------------------------------------------ assets

Texture* image(const std::string& path) {
	auto it = g_images.find(path);
	if (it != g_images.end()) return it->second.get();

	auto t = std::make_unique<Texture>();
	std::string full = assetPath(path);
	SDL_Surface* s = IMG_Load(full.c_str());
	if (!s) {
		log("failed to load image %s: %s", full.c_str(), IMG_GetError());
		Texture* ph = placeholderTexture();
		*t = *ph;
	} else {
		t->tex = SDL_CreateTextureFromSurface(g_renderer, s);
		t->w = s->w;
		t->h = s->h;
		SDL_FreeSurface(s);
		SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_BLEND);
	}
	Texture* raw = t.get();
	g_images[path] = std::move(t);
	return raw;
}

Sound* sound(const std::string& path, float volume, bool loop) {
	std::string key = path + "|" + std::to_string(volume) + "|" + (loop ? "1" : "0");
	auto it = g_sounds.find(key);
	if (it != g_sounds.end()) return it->second.get();

	auto s = std::make_unique<Sound>();
	s->volume = volume;
	s->loop = loop;

	auto cit = g_chunks.find(path);
	if (cit != g_chunks.end()) {
		s->chunk = cit->second;
	} else {
		std::string full = assetPath(path);
		// the original game ships mp3; the converted assets use ogg
		if (!fileExists(full) && path.size() > 4 && path.compare(path.size() - 4, 4, ".mp3") == 0)
			full = assetPath(path.substr(0, path.size() - 4) + ".ogg");
		Mix_Chunk* c = Mix_LoadWAV(full.c_str());
		if (!c) log("failed to load sound %s: %s", full.c_str(), Mix_GetError());
		g_chunks[path] = c;
		s->chunk = c;
	}
	Sound* raw = s.get();
	g_sounds[key] = std::move(s);
	return raw;
}

// ------------------------------------------------------------------ frame

bool pumpEvents() {
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) g_quit = true;
#ifndef __vita__
		if (e.type == SDL_CONTROLLERDEVICEADDED) {
			for (int slot = 0; slot < 2; slot++) {
				if (!g_pads[slot]) {
					g_pads[slot] = SDL_GameControllerOpen(e.cdevice.which);
					break;
				}
			}
		}
		if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
			for (int slot = 0; slot < 2; slot++) {
				if (g_pads[slot] &&
					SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[slot])) == e.cdevice.which) {
					SDL_GameControllerClose(g_pads[slot]);
					g_pads[slot] = nullptr;
				}
			}
		}
#endif
	}
	return !g_quit;
}

void beginFrame() {
	SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
	SDL_RenderClear(g_renderer);
}

void endFrame() { SDL_RenderPresent(g_renderer); }

// ------------------------------------------------------------------ drawing

void setCamera(float x, float y, float zoom) {
	g_camX = x;
	g_camY = y;
	g_zoom = zoom;
}

void setAlpha(float alpha) { g_alpha = std::max(0.0f, std::min(1.0f, alpha)); }

void drawSprite(const SpriteDraw& d) {
	if (!d.tex || !d.tex->tex) return;

	// centre of the quad in world space
	float lx = d.scaleX * (d.offX + d.dw * 0.5f);
	float ly = d.scaleY * (d.offY + d.dh * 0.5f);
	float c = std::cos(d.angle), s = std::sin(d.angle);
	float wx = d.ox + lx * c - ly * s;
	float wy = d.oy + lx * s + ly * c;

	float cx = (wx - g_camX) * g_zoom;
	float cy = (wy - g_camY) * g_zoom;
	float w = std::fabs(d.scaleX) * d.dw * g_zoom;
	float h = std::fabs(d.scaleY) * d.dh * g_zoom;

	if (cx + w < -SCREEN_W || cx - w > 2 * SCREEN_W || cy + h < -SCREEN_H || cy - h > 2 * SCREEN_H) return;

	SDL_FRect dst;
	if (std::fabs(d.angle) < 1e-5f) {
		// snap edges to whole pixels so tiled sprites do not show seams
		float l = std::round(cx - w * 0.5f), r = std::round(cx + w * 0.5f);
		float t = std::round(cy - h * 0.5f), b = std::round(cy + h * 0.5f);
		dst = {l, t, std::max(1.0f, r - l), std::max(1.0f, b - t)};
	} else {
		dst = {cx - w * 0.5f, cy - h * 0.5f, w, h};
	}

	SDL_Rect src = {d.sx, d.sy, d.sw, d.sh};
	int flip = SDL_FLIP_NONE;
	if (d.scaleX < 0) flip |= SDL_FLIP_HORIZONTAL;
	if (d.scaleY < 0) flip |= SDL_FLIP_VERTICAL;

	SDL_SetTextureAlphaMod(d.tex->tex, (Uint8)(g_alpha * 255.0f));
	SDL_RenderCopyExF(g_renderer, d.tex->tex, &src, &dst, d.angle * 180.0 / kPi, nullptr,
					  (SDL_RendererFlip)flip);
}

void drawImage(Texture* tex, float x, float y, float scale, int sx, int sy, int sw, int sh, bool flipX, uint8_t alpha) {
	if (!tex || !tex->tex) return;
	if (sw <= 0) sw = tex->w;
	if (sh <= 0) sh = tex->h;
	SDL_Rect src = {sx, sy, sw, sh};
	SDL_FRect dst = {x, y, sw * scale, sh * scale};
	SDL_SetTextureAlphaMod(tex->tex, alpha);
	SDL_RenderCopyExF(g_renderer, tex->tex, &src, &dst, 0, nullptr, flipX ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
	SDL_SetTextureAlphaMod(tex->tex, 255);
}

void drawRect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	SDL_SetRenderDrawColor(g_renderer, r, g, b, a);
	SDL_FRect rc = {x, y, w, h};
	SDL_RenderFillRectF(g_renderer, &rc);
}

// Both fonts: ASCII from 32, then Russian (the slots are written by tools/make_font.py and tools/make_base.py)
int glyphIndex(unsigned cp) {
	if (cp >= 32 && cp <= 127) return (int)cp - 32;
	if (cp >= 0x410 && cp <= 0x44F) return 96 + (int)cp - 0x410;
	if (cp == 0x401) return 160;
	if (cp == 0x451) return 161;
	return '?' - 32;
}

// the next letter of UTF-8 text
unsigned nextChar(const std::string& s, size_t& i) {
	unsigned char c = s[i++];
	if (c < 0x80) return c;
	int more = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : (c >= 0xC0 ? 1 : 0));
	unsigned cp = more == 3 ? c & 7 : (more == 2 ? c & 15 : c & 31);
	for (; more > 0 && i < s.size(); more--) cp = (cp << 6) | ((unsigned char)s[i++] & 63);
	return cp;
}

size_t charCount(const std::string& s) {
	size_t n = 0;
	for (unsigned char c : s) n += (c & 0xC0) != 0x80;
	return n;
}

void drawText(const std::string& text, float x, float y, float scale, uint8_t r, uint8_t g, uint8_t b) {
	if (!g_font || !g_font->tex) return;
	SDL_SetTextureColorMod(g_font->tex, r, g, b);
	SDL_SetTextureAlphaMod(g_font->tex, 255);

	float cx = x;
	for (size_t at = 0; at < text.size();) {
		unsigned ch = nextChar(text, at);
		if (ch == '\n') {
			cx = x;
			y += FONT_CH * scale;
			continue;
		}
		int i = glyphIndex(ch);
		SDL_Rect src = {(i % 16) * FONT_CW, (i / 16) * FONT_CH, FONT_CW, FONT_CH};
		SDL_FRect dst = {std::round(cx), std::round(y), FONT_CW * scale, FONT_CH * scale};
		SDL_RenderCopyF(g_renderer, g_font->tex, &src, &dst);
		cx += FONT_CW * scale;
	}
	SDL_SetTextureColorMod(g_font->tex, 255, 255, 255);
}

int textWidth(const std::string& text, float scale) { return (int)(charCount(text) * FONT_CW * scale); }

void drawTextSheet(Texture* sheet, int cw, int ch, int advance, const std::string& text, float x, float y, float scale,
				   uint8_t r, uint8_t g, uint8_t b) {
	if (!sheet || !sheet->tex) return;
	SDL_SetTextureColorMod(sheet->tex, r, g, b);
	SDL_SetTextureAlphaMod(sheet->tex, 255);
	for (size_t at = 0; at < text.size();) {
		int i = glyphIndex(nextChar(text, at));
		SDL_Rect src = {(i % 16) * cw, (i / 16) * ch, cw, ch};
		SDL_FRect dst = {std::round(x), std::round(y), cw * scale, ch * scale};
		SDL_RenderCopyF(g_renderer, sheet->tex, &src, &dst);
		x += advance * scale;
	}
	SDL_SetTextureColorMod(sheet->tex, 255, 255, 255);
}

// ------------------------------------------------------------------ audio

void setMute(bool mute) {
	g_mute = mute;
	if (mute) stopMusic();
}

namespace {
Mix_Music* g_music = nullptr;
std::string g_musicPath;
float g_musicBase = 0.6f, g_musicVolume = 1.0f, g_soundVolume = 1.0f;
}

void setVolumes(float music, float sounds) {
	g_musicVolume = music;
	g_soundVolume = sounds;
	int freq, channels;
	Uint16 format;
	if (Mix_QuerySpec(&freq, &format, &channels) > 0) Mix_VolumeMusic((int)(g_musicBase * music * MIX_MAX_VOLUME));
}

void playMusic(const std::string& path, float volume) {
	if (path == g_musicPath && g_music) return;
	stopMusic();
	if (path.empty() || g_mute) return;
	int freq, channels;
	Uint16 format;
	if (Mix_QuerySpec(&freq, &format, &channels) <= 0) return;
	std::string full = assetPath(path);
	g_music = Mix_LoadMUS(full.c_str());
	if (!g_music) {
		log("failed to load music %s: %s", full.c_str(), Mix_GetError());
		return;
	}
	g_musicPath = path;
	g_musicBase = volume;
	Mix_VolumeMusic((int)(volume * g_musicVolume * MIX_MAX_VOLUME));
	Mix_PlayMusic(g_music, -1);
}

void stopMusic() {
	if (g_music) {
		Mix_HaltMusic();
		Mix_FreeMusic(g_music);
		g_music = nullptr;
	}
	g_musicPath.clear();
}

void stopAllSounds() {
	int freq, channels;
	Uint16 format;
	if (Mix_QuerySpec(&freq, &format, &channels) > 0) Mix_HaltChannel(-1);
}

void playSound(Sound* s) {
	if (!s || !s->chunk || g_mute) return;
	int ch = Mix_PlayChannel(-1, s->chunk, s->loop ? -1 : 0);
	if (ch >= 0) Mix_Volume(ch, (int)(s->volume * g_soundVolume * MIX_MAX_VOLUME));
}

void playSoundRandom(const std::vector<Sound*>& list) {
	if (list.empty()) return;
	// Math.round(Math.random() * (n-1)) in the original
	int i = (int)std::lround((double)rand() / RAND_MAX * (list.size() - 1));
	playSound(list[i]);
}

// ------------------------------------------------------------------ input

namespace {

bool keyHeld(SDL_Scancode sc) {
	int n = 0;
	const Uint8* k = SDL_GetKeyboardState(&n);
	return sc < n && k[sc];
}

#ifdef __vita__
struct VitaPad {
	SceCtrlData d;
	bool ok = false;
};
VitaPad vitaPad() {
	VitaPad p;
	p.ok = sceCtrlPeekBufferPositive(0, &p.d, 1) > 0;
	return p;
}
#endif

// Right stick -> aim vector (normalised to -1..1), active outside a radial dead zone.
void applyStick(PlayerInput& in, float x, float y) {
	const float dead = 0.45f;
	if (x * x + y * y > dead * dead) {
		in.stickAim = true;
		in.stickX = x;
		in.stickY = y;
	}
}

// Gamepad layout (Vita and SDL game controllers alike):
//   left stick / d-pad  move, climb, crouch      Cross (A)   jump
//   Square (X)          punch                    Circle (B)  kick
//   Triangle (Y)        grenade: hold to cook, release to throw
//   L                   grab, or throw melee     R           fire (aims as well)
//   right stick         aim while moving         Up pick up, Select skill
PlayerInput padInput(int slot) {
	PlayerInput in;
#ifdef __vita__
	if (slot != 0) return in;
	VitaPad p = vitaPad();
	if (!p.ok) return in;
	unsigned b = p.d.buttons;
	in.up = (b & SCE_CTRL_UP) || p.d.ly < 64;
	in.down = (b & SCE_CTRL_DOWN) || p.d.ly > 192;
	in.left = (b & SCE_CTRL_LEFT) || p.d.lx < 64;
	in.right = (b & SCE_CTRL_RIGHT) || p.d.lx > 192;
	in.jump = b & SCE_CTRL_CROSS;
	in.punch = b & SCE_CTRL_SQUARE;
	in.kick = b & SCE_CTRL_CIRCLE;
	in.grenade = b & SCE_CTRL_TRIANGLE;
	in.fire = b & SCE_CTRL_RTRIGGER;
	in.aim = b & SCE_CTRL_RTRIGGER;
	in.grab = b & SCE_CTRL_LTRIGGER;
	in.interact = (b & SCE_CTRL_UP) || p.d.ly < 64;
	in.skill = b & SCE_CTRL_SELECT;
	applyStick(in, (p.d.rx - 128) / 127.0f, (p.d.ry - 128) / 127.0f);
	in.menuConfirm = b & SCE_CTRL_CROSS;
	in.menuBack = b & SCE_CTRL_CIRCLE;
#else
	SDL_GameController* c = g_pads[slot];
	if (!c) return in;
	auto axis = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(c, a); };
	auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b) != 0; };
	in.up = btn(SDL_CONTROLLER_BUTTON_DPAD_UP) || axis(SDL_CONTROLLER_AXIS_LEFTY) < -16000;
	in.down = btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || axis(SDL_CONTROLLER_AXIS_LEFTY) > 16000;
	in.left = btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || axis(SDL_CONTROLLER_AXIS_LEFTX) < -16000;
	in.right = btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || axis(SDL_CONTROLLER_AXIS_LEFTX) > 16000;
	in.jump = btn(SDL_CONTROLLER_BUTTON_A);
	in.punch = btn(SDL_CONTROLLER_BUTTON_X);
	in.kick = btn(SDL_CONTROLLER_BUTTON_B);
	in.grenade = btn(SDL_CONTROLLER_BUTTON_Y);
	bool rt = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) || axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
	bool lt = btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
	in.fire = rt;
	in.aim = rt;
	in.grab = lt;
	in.interact = in.up;
	in.skill = btn(SDL_CONTROLLER_BUTTON_BACK);
	applyStick(in, axis(SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f, axis(SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f);
	in.menuConfirm = btn(SDL_CONTROLLER_BUTTON_A);
	in.menuBack = btn(SDL_CONTROLLER_BUTTON_B);
#endif
	return in;
}

PlayerInput merge(const PlayerInput& a, const PlayerInput& b) {
	PlayerInput o;
	o.up = a.up || b.up;
	o.down = a.down || b.down;
	o.left = a.left || b.left;
	o.right = a.right || b.right;
	o.jump = a.jump || b.jump;
	o.punch = a.punch || b.punch;
	o.kick = a.kick || b.kick;
	o.aim = a.aim || b.aim;
	o.grab = a.grab || b.grab;
	o.skill = a.skill || b.skill;
	o.fire = a.fire || b.fire;
	o.grenade = a.grenade || b.grenade;
	o.interact = a.interact || b.interact;
	const PlayerInput& s = b.stickAim ? b : a;
	o.stickAim = s.stickAim;
	o.stickX = s.stickX;
	o.stickY = s.stickY;
	o.menuConfirm = a.menuConfirm || b.menuConfirm;
	o.menuBack = a.menuBack || b.menuBack;
	return o;
}

}  // namespace

PlayerInput keyboardInput(bool up, bool down, bool left, bool right, bool a, bool s, bool d, bool f) {
	PlayerInput in;
	in.up = up;
	in.down = down;
	in.left = left;
	in.right = right;
	in.jump = up;
	in.aim = d;
	in.punch = a && !d;
	in.fire = a && d;
	in.kick = s && !d;
	in.grenade = s && d;
	in.interact = f;
	in.menuConfirm = a;
	in.menuBack = d;
	return in;
}

int localPlayerSlots() {
#ifdef __vita__
	return 1;
#else
	return 2;
#endif
}

namespace {
PlayerInput g_override[2];
bool g_hasOverride[2] = {false, false};
}  // namespace

void setInputOverride(int localId, const PlayerInput* in) {
	if (localId < 0 || localId > 1) return;
	g_hasOverride[localId] = in != nullptr;
	if (in) g_override[localId] = *in;
}

bool saveScreenshot(const std::string& path) {
	int w = 0, h = 0;
	SDL_GetRendererOutputSize(g_renderer, &w, &h);
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return false;
	bool ok = SDL_RenderReadPixels(g_renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0;
	if (ok) ok = IMG_SavePNG(s, path.c_str()) == 0;
	SDL_FreeSurface(s);
	return ok;
}

PlayerInput playerInput(int localId) {
	if (localId >= 0 && localId < 2 && g_hasOverride[localId]) return g_override[localId];
	PlayerInput kb;
#ifndef __vita__
	if (localId == 0) {
		// same keys as the browser version
		kb = keyboardInput(keyHeld(SDL_SCANCODE_UP), keyHeld(SDL_SCANCODE_DOWN), keyHeld(SDL_SCANCODE_LEFT),
						   keyHeld(SDL_SCANCODE_RIGHT), keyHeld(SDL_SCANCODE_A), keyHeld(SDL_SCANCODE_S),
						   keyHeld(SDL_SCANCODE_D), keyHeld(SDL_SCANCODE_F));
		kb.grab = keyHeld(SDL_SCANCODE_G);
		kb.skill = keyHeld(SDL_SCANCODE_C);
	} else if (localId == 1) {
		kb = keyboardInput(keyHeld(SDL_SCANCODE_I), keyHeld(SDL_SCANCODE_K), keyHeld(SDL_SCANCODE_J),
						   keyHeld(SDL_SCANCODE_L), keyHeld(SDL_SCANCODE_U), keyHeld(SDL_SCANCODE_O),
						   keyHeld(SDL_SCANCODE_P), keyHeld(SDL_SCANCODE_H));
		kb.grab = keyHeld(SDL_SCANCODE_Y);
		kb.skill = keyHeld(SDL_SCANCODE_N);
	}
#endif
	if (localId < 2) return merge(kb, padInput(localId));
	return kb;
}

MenuInput menuInput() {
	bool now[M_COUNT] = {};
	PlayerInput p0 = playerInput(0);
	PlayerInput p1 = playerInput(1);
	now[M_UP] = p0.up || p1.up;
	now[M_DOWN] = p0.down || p1.down;
	now[M_LEFT] = p0.left || p1.left;
	now[M_RIGHT] = p0.right || p1.right;
	now[M_CONFIRM] = p0.menuConfirm || p1.menuConfirm;
	now[M_BACK] = p0.menuBack || p1.menuBack;
	now[M_LB] = p0.grab;
	now[M_RB] = p0.fire;
	now[M_ALT] = p0.grenade;

#ifdef __vita__
	VitaPad v = vitaPad();
	if (v.ok) {
		now[M_START] = (v.d.buttons & SCE_CTRL_START) != 0;
		now[M_CONFIRM] = now[M_CONFIRM] || (v.d.buttons & SCE_CTRL_START);
	}
#else
	now[M_CONFIRM] = now[M_CONFIRM] || keyHeld(SDL_SCANCODE_RETURN) || keyHeld(SDL_SCANCODE_SPACE);
	now[M_BACK] = now[M_BACK] || keyHeld(SDL_SCANCODE_BACKSPACE);
	now[M_START] = keyHeld(SDL_SCANCODE_ESCAPE);
	now[M_LB] = now[M_LB] || keyHeld(SDL_SCANCODE_Q);
	now[M_RB] = now[M_RB] || keyHeld(SDL_SCANCODE_E);
	now[M_ALT] = now[M_ALT] || keyHeld(SDL_SCANCODE_T);
	for (int i = 0; i < 2; i++)
		if (g_pads[i]) now[M_START] = now[M_START] || SDL_GameControllerGetButton(g_pads[i], SDL_CONTROLLER_BUTTON_START);
#endif

	MenuInput m;
	auto edge = [&](int bit) { return now[bit] && !g_prevMenu[bit]; };
	m.up = edge(M_UP);
	m.down = edge(M_DOWN);
	m.left = edge(M_LEFT);
	m.right = edge(M_RIGHT);
	m.confirm = edge(M_CONFIRM);
	m.back = edge(M_BACK);
	m.start = edge(M_START);
	m.lb = edge(M_LB);
	m.rb = edge(M_RB);
	m.alt = edge(M_ALT);
	for (int i = 0; i < M_COUNT; i++) g_prevMenu[i] = now[i];
	return m;
}

}  // namespace platform
