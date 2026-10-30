// Super Fighters (C++/SDL2 port) - application shell: menus, fixed 60Hz loop, HUD, music.
#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "base.h"
#include "game.h"
#include "platform.h"
#include "profile.h"
#include "i18n.h"
#include "settings.h"
#include "progress.h"

#ifdef __vita__
// give malloc a big enough heap (textures and decoded sound effects)
extern "C" {
unsigned int _newlib_heap_size_user = 160u * 1024u * 1024u;
}
#endif

namespace {

const char* MUSIC = "sounds/superfighters_theme.ogg";

enum class State { Title, Base, Multiplayer, MapSelect, Setup, Playing, Paused, Options };

struct MapInfo {
	std::string file;    // name without extension
	std::string title;
	std::vector<Game::MapLayer> layers;   // for the preview
	float w = 0, h = 0, x = 0, y = 0;     // bounds
};

struct App {
	State state = State::Title;
	BaseScreen base;
	bool inRange = false;
	std::vector<MapInfo> maps;
	int mapIndex = 0;
	int players = 1;      // local humans
	int bots = 3;
	int difficulty = 1;   // 0 easy 1 normal 2 hard
	State optionsFrom = State::Title;
	int mode = MODE_FFA;
	bool suddenDeath = true;
	bool startPistols = false;
	int cursor = 0;
	std::unique_ptr<Game> game;
};

const char* DIFFICULTY[] = {"Easy", "Normal", "Hard"};
const char* MODES[] = {"Free for all", "Teams", "Waves"};

void text(const std::string& t, float x, float y, float scale, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255) {
	platform::drawText(t, x, y, scale, r, g, b);
}

void centered(const std::string& t, float y, float scale, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255) {
	text(t, (SCREEN_W - platform::textWidth(t, scale)) / 2.0f, y, scale, r, g, b);
}

void shadowBox(float x, float y, float w, float h, uint8_t a = 170) { platform::drawRect(x, y, w, h, 0, 0, 0, a); }

void bar(float x, float y, float w, float h, float fraction, uint8_t r, uint8_t g, uint8_t b) {
	platform::drawRect(x - 1, y - 1, w + 2, h + 2, 0, 0, 0, 200);
	platform::drawRect(x, y, w, h, 60, 60, 60, 255);
	platform::drawRect(x, y, w * std::max(0.0f, std::min(1.0f, fraction)), h, r, g, b, 255);
}

void drawMenuList(const std::vector<std::string>& items, int cursor, float y, float scale = 2.5f, float gap = 46) {
	for (size_t i = 0; i < items.size(); i++) {
		bool sel = (int)i == cursor;
		centered((sel ? "> " : "  ") + items[i] + (sel ? " <" : "  "), y + gap * i, scale, sel ? 255 : 170,
				 sel ? 220 : 170, sel ? 90 : 170);
	}
}

void moveCursor(App& app, const MenuInput& in, int count) {
	if (in.up) app.cursor = (app.cursor + count - 1) % count;
	if (in.down) app.cursor = (app.cursor + 1) % count;
}

// ------------------------------------------------------------------ maps

void loadMapList(App& app) {
	for (auto& f : platform::listFiles("maps", ".sfm")) {
		if (f == "range.sfm") continue;   // part of the base
		MapInfo m;
		m.file = f.substr(0, f.size() - 4);
		m.title = m.file;
		json j = json::parse(platform::readFile("maps/" + f), nullptr, false);
		if (!j.is_discarded()) {
			m.title = j.value("name", m.file);
			if (j.contains("layers"))
				for (auto& l : j["layers"]) {
					Game::MapLayer layer;
					layer.tex = platform::image(l.value("image", ""));
					layer.x = l.value("x", 0.f);
					layer.y = l.value("y", 0.f);
					layer.parallax = l.value("parallax", 1.f);
					if (layer.tex) m.layers.push_back(layer);
				}
			if (j.contains("bounds")) {
				m.x = j["bounds"].value("x", 0.f);
				m.y = j["bounds"].value("y", 0.f);
				m.w = j["bounds"].value("w", 0.f);
				m.h = j["bounds"].value("h", 0.f);
			}
		}
		app.maps.push_back(m);
	}
	// the order of the original game
	const char* order[] = {"warehouse", "backstreets", "factory", "sewers", "rooftops", "containers"};
	auto rank = [&](const MapInfo& m) {
		for (int i = 0; i < 6; i++)
			if (m.file == order[i]) return i;
		return 99;
	};
	std::stable_sort(app.maps.begin(), app.maps.end(), [&](auto& a, auto& b) { return rank(a) < rank(b); });
}

void drawPreview(const MapInfo& m, float bx, float by, float bw, float bh) {
	platform::drawRect(bx - 4, by - 4, bw + 8, bh + 8, 90, 90, 110, 255);
	platform::drawRect(bx, by, bw, bh, 0, 0, 0, 255);
	if (m.w <= 0 || m.h <= 0) return;
	float scale = std::min(bw / m.w, bh / m.h);
	float ox = bx + (bw - m.w * scale) / 2, oy = by + (bh - m.h * scale) / 2;
	for (auto& l : m.layers) {
		if (l.parallax < 1) {   // sky: fill the box
			float s = std::max(bw / l.tex->w, bh / l.tex->h);
			platform::drawImage(l.tex, bx, by, s, 0, 0, (int)(bw / s), (int)(bh / s));
			continue;
		}
		// crop the art to the level bounds
		int sx = (int)std::max(0.f, m.x - l.x), sy = (int)std::max(0.f, m.y - l.y);
		int sw = (int)std::min((float)l.tex->w - sx, m.w), sh = (int)std::min((float)l.tex->h - sy, m.h);
		platform::drawImage(l.tex, ox, oy, scale, sx, sy, sw, sh);
	}
}

// ------------------------------------------------------------------ HUD

void weaponIcon(Obj* o, float x, float y, float box, bool selected) {
	platform::drawRect(x, y, box, box, selected ? 120 : 30, selected ? 100 : 30, selected ? 30 : 40, 200);
	if (!o || !o->image) return;
	float s = std::min((box - 6) / o->frameW, (box - 6) / o->frameH);
	s = std::max(1.0f, std::floor(s));
	float w = o->frameW * s, h = o->frameH * s;
	platform::drawImage(o->image, x + (box - w) / 2, y + (box - h) / 2, s, 0, 0, (int)o->frameW, (int)o->frameH);
}

void drawPlayerPanel(Game& game, const PlayerSlot& slot, float x, float y) {
	Obj* o = game.getObjectById(slot.objectId);
	std::string name = tr(slot.profile.count("name") ? slot.profile.at("name") : "Player");
	shadowBox(x - 6, y - 6, 230, 74, 150);
	if (!o || o->type() != ObjType::Player) {
		text(name + "  " + tr("K.O."), x, y, 1.5f, 255, 90, 90);
		return;
	}
	Player* p = static_cast<Player*>(o);
	text(name, x, y - 1, 1.25f);
	text(tr("Wins") + ": " + std::to_string(slot.wins), x + 150, y - 1, 1.0f, 200, 200, 120);
	bar(x, y + 15, 216, 9, p->health / 100.0f, 220, 60, 50);

	Obj* melee = game.getObjectById(p->inventory[INV_MELEE]);
	Obj* gun = game.getObjectById(p->inventory[INV_GUN]);
	Obj* nade = game.getObjectById(p->inventory[INV_THROWABLE]);
	const float box = 34, gy = y + 30;
	bool fists = !melee || (melee->parent && melee->parent->key == "fists");
	weaponIcon(fists ? nullptr : melee, x, gy, box, false);
	if (fists) text("--", x + 9, gy + 11, 1.25f, 140, 140, 140);
	weaponIcon(gun, x + 40, gy, box, gun && p->equiped == INV_GUN);
	weaponIcon(nade, x + 80, gy, box, nade && p->equiped == INV_THROWABLE);
	if (gun && gun->type() == ObjType::Gun) {
		int ammo = (int)static_cast<Gun*>(gun)->ammo;
		text(ammo > 0 ? std::to_string(ammo) : tr("THROW"), x + 120, gy + 2, 1.25f, ammo > 0 ? 230 : 255,
			 ammo > 0 ? 230 : 120, ammo > 0 ? 120 : 80);
	}
	if (nade && nade->type() == ObjType::Throwable)
		text("x" + std::to_string((int)static_cast<Throwable*>(nade)->ammo), x + 120, gy + 18, 1.25f, 160, 230, 160);
}

// The start of a round: everybody stands still, your own fighter in the light under an arrow
void drawIntro(Game& game) {
	if (game.introFrames <= 0 && game.fightBanner <= 0) return;
	int locals = 0;
	for (auto& slot : game.players) locals += slot.local;

	for (auto& slot : game.players) {
		Obj* o = game.getObjectById(slot.objectId);
		if (!slot.local || !o) continue;
		Vec2 p = game.worldToScreen(o->getPosition());
		if (locals == 1 && game.introFrames > 0) {
			float r = 26 * game.camera.zoom;
			const uint8_t dim = 130;
			platform::drawRect(0, 0, SCREEN_W, std::max(0.f, p.y - r), 0, 0, 0, dim);
			platform::drawRect(0, p.y + r, SCREEN_W, SCREEN_H, 0, 0, 0, dim);
			platform::drawRect(0, p.y - r, std::max(0.f, p.x - r), r * 2, 0, 0, 0, dim);
			platform::drawRect(p.x + r, p.y - r, SCREEN_W, r * 2, 0, 0, 0, dim);
		}
		uint8_t r = slot.localId == 0 ? 255 : 90, g = slot.localId == 0 ? 230 : 220, b = slot.localId == 0 ? 60 : 255;
		float top = p.y - 16 * game.camera.zoom - 22 + std::sin(game.frameCounter * 0.18f) * 6;
		for (int i = 0; i < 9; i++) platform::drawRect(p.x - 9 + i, top + i * 2, 18 - i * 2, 2, r, g, b, 255);
		std::string name = locals == 1 ? tr("YOU") : "P" + std::to_string(slot.localId + 1);
		float w = (float)platform::textWidth(name, 2.0f);
		shadowBox(p.x - w / 2 - 5, top - 30, w + 10, 28, 190);
		text(name, p.x - w / 2, top - 28, 2.0f, r, g, b);
	}
	if (game.introFrames > 0)
		centered(std::to_string(1 + (game.introFrames - 1) / 60), 120, 8.0f, 255, 255, 255);
	else
		centered(tr("FIGHT!"), 130, 6.0f, 255, 220, 80);
}

void drawHud(Game& game) {
	drawIntro(game);
	int shown = 0;
	for (auto& slot : game.players) {
		if (!slot.local) continue;
		float x = shown == 0 ? 14.0f : SCREEN_W - 236.0f;
		drawPlayerPanel(game, slot, x, 14);
		shown++;
	}

	// the skill: name and charge
	for (auto& slot : game.players) {
		Obj* o = game.getObjectById(slot.objectId);
		if (!slot.local || !o || o->type() != ObjType::Player) continue;
		Player* p = static_cast<Player*>(o);
		if (!p->skill) continue;
		float x = slot.localId == 0 ? 20.0f : SCREEN_W - 230.0f;
		bar(x, 96, 120, 6, p->skillCharge(), 120, 200, 240);
		text(tr(p->skill->name) + (p->skillReady() ? "  " + tr("READY") : ""), x, 104, 1.0f, 170, 220, 245);
	}
	if (game.range) centered(tr("Walk through the door to leave"), SCREEN_H - 26, 1.0f, 200, 200, 200);

	// grab: the time left to throw
	for (auto& slot : game.players) {
		Obj* o = game.getObjectById(slot.objectId);
		if (!slot.local || !o || o->type() != ObjType::Player) continue;
		Player* p = static_cast<Player*>(o);
		if (p->holding < 0) continue;
		Vec2 s = game.worldToScreen({p->getPosition().x, p->getPosition().y - 44});
		bar(s.x - 26, s.y, 52, 5, p->holdFraction(), 255, 210, 80);
		text("R!", s.x - 8, s.y - 16, 1.25f, 255, 230, 120);
	}

	// small health bars over everybody's head
	for (auto& slot : game.players) {
		Obj* o = game.getObjectById(slot.objectId);
		if (!o || !Settings::get().bars) continue;
		Vec2 s = game.worldToScreen({o->getPosition().x, o->getPosition().y - 16});
		float w = 26;
		bool bot = slot.bot != nullptr;
		if (slot.team > 0)   // one colour per team
			bar(s.x - w / 2, s.y - 6, w, 3, o->health / 100.0f, slot.team == 1 ? 80 : 230, slot.team == 1 ? 170 : 90, slot.team == 1 ? 240 : 70);
		else
			bar(s.x - w / 2, s.y - 6, w, 3, o->health / 100.0f, bot ? 230 : 80, bot ? 140 : 200, bot ? 60 : 80);
	}

	if (game.mode == MODE_WAVES) {
		std::string w = trf("WAVE %s", std::to_string(game.wave));
		text(w, SCREEN_W - platform::textWidth(w, 2.0f) - 16.0f, 16, 2.0f, 255, 220, 90);
	}
	if (game.suddenDeath && game.gameOver == 0 && (game.frameCounter / 20) % 2 == 0) centered(tr("SUDDEN DEATH"), 16, 2.0f, 255, 70, 60);

	if (game.gameOver > 0) {
		std::string msg = tr("Draw!");
		if (game.mode == MODE_WAVES) {
			msg = trf("You fell on wave %s", std::to_string(game.wave));
		} else if (game.lastWinner >= 0 && game.lastWinner < (int)game.players.size()) {
			auto& w = game.players[game.lastWinner];
			msg = trf("%s wins the round!", tr(w.profile.count("name") ? w.profile.at("name") : "Player"));
			if (game.mode == MODE_TEAMS) msg = trf("Team %s wins the round!", std::to_string(w.team));
		}
		if (game.rewardTokens > 0) msg += "   +" + std::to_string(game.rewardTokens) + " " + tr("tokens");
		shadowBox(0, 96, SCREEN_W, 64, 150);
		centered(msg, 106, 2.5f, 255, 220, 80);
		std::string score;
		for (auto& slot : game.players)
			score += tr(slot.profile.count("name") ? slot.profile.at("name") : "?") + ": " + std::to_string(slot.wins) + "   ";
		centered(score, 140, 1.25f, 220, 220, 220);
	}
}

// ------------------------------------------------------------------ screens

void startGame(App& app) {
	GameOptions opts;
	opts.mapText = platform::readFile("maps/" + app.maps[app.mapIndex].file + ".sfm");
	opts.localPlayers = app.players;
	opts.bots = app.bots;
	opts.botDifficulty = app.difficulty;
	opts.mode = app.mode;
	opts.suddenDeath = app.suddenDeath;
	opts.startPistols = app.startPistols;
	opts.mapName = app.maps[app.mapIndex].file;
	opts.rewards = true;
	opts.intro = true;
	app.game = std::make_unique<Game>(opts);
	app.state = State::Playing;
	app.cursor = 0;
}

void startRange(App& app) {
	GameOptions opts;
	opts.mapText = platform::readFile("maps/range.sfm");
	opts.localPlayers = 1;
	opts.range = true;
	opts.rangeWeapon = app.base.rangeWeapon;
	app.game = std::make_unique<Game>(opts);
	app.state = State::Playing;
	app.inRange = true;
}

void controlsHint() {
#ifdef __vita__
	centered(tr("Cross jump  Square punch  Circle kick  Triangle grenade  L grab/throw  R fire  R-stick aim  Up pick up  Select skill"), SCREEN_H - 34,
			 1.0f, 140, 140, 140);
#else
	centered(tr("Arrows move/jump  A punch  S kick  D aim (+A fire, +S grenade)  G grab/throw  C skill  F pick up  |  pads: Vita layout"), SCREEN_H - 34,
			 1.0f, 140, 140, 140);
#endif
}

// ------------------------------------------------------------------ settings

void openOptions(App& app) {
	app.optionsFrom = app.state;
	app.state = State::Options;
	app.cursor = 0;
}

const int OPTION_COUNT = 9;

void optionsScreen(App& app, const MenuInput& in) {
	Settings& set = Settings::get();
	moveCursor(app, in, OPTION_COUNT);
	int dir = (in.right ? 1 : 0) - (in.left ? 1 : 0);
	if (dir) {
		switch (app.cursor) {
			case 0: set.camera = (set.camera + 3 + dir) % 3; break;
			case 1: set.zoomLimit = std::max(0, std::min(2, set.zoomLimit + dir)); break;
			case 2: set.music = std::max(0, std::min(10, set.music + dir)); break;
			case 3: set.sound = std::max(0, std::min(10, set.sound + dir)); break;
			case 4: set.shake = !set.shake; break;
			case 5: set.bars = !set.bars; break;
			case 6: set.fullEffects = !set.fullEffects; break;
			case 7: set.language = (set.language + 1) % i18n::LANGUAGE_COUNT; break;
		}
		set.apply();
	}
	if (in.back || (in.confirm && app.cursor == OPTION_COUNT - 1)) {
		set.save();
		app.state = app.optionsFrom;
		app.cursor = app.optionsFrom == State::Paused ? 2 : 3;
	}
}

void drawOptions(App& app) {
	static const char* CAMERA[] = {"All fighters", "Around me", "Wide view"};
	static const char* ZOOM[] = {"Far", "Medium", "Close"};
	static const char* HELP[] = {
		"All fighters: closes in as far as the two furthest apart allow.  Around me: you and the nearest enemy.",
		"How close the camera may get to the fighters.",
		"", "",
		"Camera shake from explosions and heavy blows.",
		"Health bars over the fighters' heads.",
		"Reduced: fewer smoke and flame sprites, for heavy moments on the console.",
		"", "",
	};
	Settings& set = Settings::get();
	auto onOff = [](bool v) { return tr(v ? "On" : "Off"); };
	centered(tr("SETTINGS"), 40, 3.5f, 255, 200, 60);
	auto option = [](const char* name, const std::string& value) { return tr(name) + ": < " + value + " >"; };
	drawMenuList({option("Camera", tr(CAMERA[set.camera])),
				  option("Zoom limit", tr(ZOOM[set.zoomLimit])),
				  option("Music", std::to_string(set.music)),
				  option("Sounds", std::to_string(set.sound)),
				  option("Screen shake", onOff(set.shake)),
				  option("Health bars", onOff(set.bars)),
				  option("Effects", tr(set.fullEffects ? "Full" : "Reduced")),
				  option("Language", i18n::languageName(set.language)),
				  tr("Back")},
				 app.cursor, 104, 2.0f, 40);
	centered(tr(HELP[app.cursor]), SCREEN_H - 40, 1.0f, 170, 170, 170);
}

void titleScreen(App& app, const MenuInput& in, bool& quit) {
	const int n = 5;
	moveCursor(app, in, n);
	if (in.confirm) {
		if (app.cursor == 0) {
			app.state = State::MapSelect;
			app.cursor = 0;
		} else if (app.cursor == 1) {
			app.state = State::Multiplayer;
		} else if (app.cursor == 2) {
			app.state = State::Base;
		} else if (app.cursor == 3) {
			openOptions(app);
		} else {
			quit = true;
		}
	}
	centered("SUPER FIGHTERS", 70, 5.0f, 255, 200, 60);
	centered(tr("PS Vita / PC port"), 128, 1.5f, 150, 150, 150);
	drawMenuList({tr("Singleplayer"), tr("Multiplayer"), tr("Base"), tr("Settings"), tr("Quit")}, app.cursor, 196);
	controlsHint();
}

void multiplayerScreen(App& app, const MenuInput& in) {
	if (in.confirm || in.back) {
		app.state = State::Title;
		app.cursor = 1;
	}
	centered(tr("MULTIPLAYER"), 90, 4.0f, 255, 200, 60);
	centered(tr("Online and LAN play are coming in the next stage."), 220, 1.5f);
	centered(tr("(dedicated server or one player hosting)"), 250, 1.25f, 170, 170, 170);
	centered("> " + tr("Back") + " <", 340, 2.5f, 255, 220, 90);
}

void mapScreen(App& app, const MenuInput& in) {
	int n = (int)app.maps.size();
	if (n == 0) {
		centered(tr("No maps found"), 250, 2.0f, 255, 90, 90);
		if (in.back || in.confirm) app.state = State::Title;
		return;
	}
	if (in.left) app.mapIndex = (app.mapIndex + n - 1) % n;
	if (in.right) app.mapIndex = (app.mapIndex + 1) % n;
	if (in.confirm) {
		app.state = State::Setup;
		app.cursor = 0;
	}
	if (in.back) {
		app.state = State::Title;
		app.cursor = 0;
	}
	centered(tr("CHOOSE A MAP"), 24, 3.0f, 255, 200, 60);
	drawPreview(app.maps[app.mapIndex], 180, 80, 600, 340);
	centered("<  " + tr(app.maps[app.mapIndex].title) + "  >", 440, 3.0f);
	centered(std::to_string(app.mapIndex + 1) + " / " + std::to_string(n), 480, 1.25f, 150, 150, 150);
#ifdef __vita__
	centered(tr("Left/Right choose   Cross next   Circle back"), SCREEN_H - 30, 1.0f, 140, 140, 140);
#else
	centered(tr("Left/Right choose   Enter next   Backspace back"), SCREEN_H - 30, 1.0f, 140, 140, 140);
#endif
}

void setupScreen(App& app, const MenuInput& in) {
	const bool multi = platform::localPlayerSlots() > 1;
	std::vector<std::string> items;
	std::vector<int> ids;   // 0 bots, 1 difficulty, 2 players, 4 start, 5 back, 6 mode, 7 sudden death, 8 start weapon
	items.push_back(tr("Mode") + ": < " + tr(MODES[app.mode]) + " >");
	ids.push_back(6);
	if (app.mode != MODE_WAVES) {
		items.push_back(tr("Bots") + ": < " + std::to_string(app.bots) + " >");
		ids.push_back(0);
	}
	items.push_back(tr("Difficulty") + ": < " + tr(DIFFICULTY[app.difficulty]) + " >");
	ids.push_back(1);
	if (multi) {
		items.push_back(tr("Players on this PC") + ": < " + std::to_string(app.players) + " >");
		ids.push_back(2);
	}
	if (app.mode != MODE_WAVES) {
		items.push_back(tr("Sudden death") + ": < " + tr(app.suddenDeath ? "On" : "Off") + " >");
		ids.push_back(7);
	}
	items.push_back(tr("Start with") + ": < " + tr(app.startPistols ? "Pistols" : "Empty hands") + " >");
	ids.push_back(8);
	items.push_back(tr("START"));
	ids.push_back(4);
	items.push_back(tr("Back"));
	ids.push_back(5);

	int n = (int)items.size();
	if (app.cursor >= n) app.cursor = n - 1;
	moveCursor(app, in, n);
	int id = ids[app.cursor];
	int dir = (in.right ? 1 : 0) - (in.left ? 1 : 0);
	if (dir) {
		if (id == 0) app.bots = std::max(0, std::min(7, app.bots + dir));
		if (id == 1) app.difficulty = std::max(0, std::min(2, app.difficulty + dir));
		if (id == 2) app.players = std::max(1, std::min(platform::localPlayerSlots(), app.players + dir));
		if (id == 6) app.mode = (app.mode + 3 + dir) % 3;
		if (id == 7) app.suddenDeath = !app.suddenDeath;
		if (id == 8) app.startPistols = !app.startPistols;
	}
	if (in.back) {
		app.state = State::MapSelect;
		return;
	}
	if (in.confirm) {
		if (id == 4) {
			if (app.mode != MODE_WAVES && app.players + app.bots < 2) app.bots = 1;   // somebody to fight
			startGame(app);
			return;
		}
		if (id == 5) {
			app.state = State::MapSelect;
			return;
		}
	}

	controlsHint();
	centered(tr("MATCH SETUP"), 24, 3.0f, 255, 200, 60);
	drawPreview(app.maps[app.mapIndex], 330, 70, 300, 170);
	centered(tr(app.maps[app.mapIndex].title), 246, 1.5f, 200, 200, 200);
	drawMenuList(items, app.cursor, 270, 1.75f, 27);
}

void pauseScreen(App& app, const MenuInput& in) {
	moveCursor(app, in, 4);
	if (in.start || in.back) {
		app.state = State::Playing;
		return;
	}
	if (in.confirm) {
		if (app.cursor == 0) {
			app.state = State::Playing;
		} else if (app.cursor == 1) {
			app.game->restartGame();
			app.state = State::Playing;
		} else if (app.cursor == 2) {
			openOptions(app);
		} else {
			bool wasRange = app.inRange;
			app.game.reset();
			app.state = wasRange ? State::Base : State::Title;
			app.cursor = 0;
			app.inRange = false;
			if (wasRange) app.base.comeBackFromRange();
		}
	}
}

void drawPause(App& app) {
	platform::drawRect(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 150);
	centered(tr("PAUSED"), 140, 4.0f);
	drawMenuList({tr("Resume"), tr("Restart round"), tr("Settings"), tr("Quit to menu")}, app.cursor, 220);
}

}  // namespace

int main(int argc, char** argv) {
	srand((unsigned)time(nullptr));

	if (!platform::init(argc > 0 ? argv[0] : nullptr)) {
		platform::log("initialisation failed");
		return 1;
	}

	std::string defs = platform::readFile("objects.json");
	if (!Registry::instance().load(defs)) {
		platform::log("could not load assets/objects.json");
		platform::shutdown();
		return 1;
	}

	App app;
	loadMapList(app);
	Settings::get().load();
	Settings::get().apply();
	bool mute = false;

	// PC test options: "--map name" / "--players n" / "--bots n" / "--frames n" / "--shot file.png" / "--mute"
	int autoFrames = -1;
	std::string shotPath;
	bool autoStart = false;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--map" && i + 1 < argc) {
			std::string name = argv[++i];
			for (size_t k = 0; k < app.maps.size(); k++)
				if (app.maps[k].file == name) app.mapIndex = (int)k;
			autoStart = true;
		} else if (a == "--players" && i + 1 < argc) {
			app.players = std::max(1, atoi(argv[++i]));
		} else if (a == "--bots" && i + 1 < argc) {
			app.bots = std::max(0, atoi(argv[++i]));
		} else if (a == "--frames" && i + 1 < argc) {
			autoFrames = atoi(argv[++i]);
		} else if (a == "--shot" && i + 1 < argc) {
			shotPath = argv[++i];
		} else if (a == "--mode" && i + 1 < argc) {
			app.mode = atoi(argv[++i]);
		} else if (a == "--mute") {
			mute = true;
		} else if (a == "--screen" && i + 1 < argc) {
			std::string s = argv[++i];
			if (s == "maps") app.state = State::MapSelect;
			if (s == "setup") app.state = State::Setup;
			if (s == "base") app.state = State::Base;
			if (s == "settings") app.state = State::Options;
			if (s == "character" || s == "armory" || s == "gym") {
				app.state = State::Base;
				app.base.openRoom(s == "character" ? 0 : (s == "armory" ? 1 : 2));
			}
			if (s == "range") startRange(app);
		}
	}
	if (autoStart) {
		if (app.players + app.bots < 2 && autoFrames < 0) app.bots = 1;
		if (!app.maps.empty()) startGame(app);
	}
	platform::setMute(mute);
	platform::playMusic(MUSIC, 0.5f);

	Uint32 last = SDL_GetTicks();
	double acc = 0;
	bool quit = false;
	int frames = 0;

	while (!quit && platform::pumpEvents()) {
		MenuInput in = platform::menuInput();

		Uint32 now = SDL_GetTicks();
		acc += now - last;
		last = now;
		if (acc > 250) acc = 250;

		platform::beginFrame();

		switch (app.state) {
			case State::Title: titleScreen(app, in, quit); break;
			case State::Base:
				switch (app.base.update(in)) {
					case BaseAction::Range: startRange(app); break;
					case BaseAction::Leave:
						app.state = State::Title;
						app.cursor = 2;
						break;
					default: break;
				}
				break;
			case State::Multiplayer: multiplayerScreen(app, in); break;
			case State::MapSelect: mapScreen(app, in); break;
			case State::Setup: setupScreen(app, in); break;
			case State::Playing:
				if (in.start) {
					app.state = State::Paused;
					app.cursor = 0;
				} else {
					// a slow frame must not pay for itself with even more work: past two steps
					// the game runs slower instead of falling further behind
					for (int steps = 0; acc >= STEP_MS && steps < 2; steps++) {
						app.game->update();
						acc -= STEP_MS;
					}
					if (acc >= STEP_MS) acc = 0;
					if (app.game->exitRequested) {
						app.game.reset();
						app.state = State::Base;
						app.inRange = false;
						app.base.comeBackFromRange();
					}
				}
				break;
			case State::Paused: pauseScreen(app, in); break;
			case State::Options: optionsScreen(app, in); break;
		}

		bool overGame = app.state == State::Options && app.optionsFrom == State::Paused;
		if (app.game && (app.state == State::Playing || app.state == State::Paused || overGame)) {
			app.game->draw();
			drawHud(*app.game);
			if (app.state == State::Paused) drawPause(app);
			if (overGame) platform::drawRect(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 200);
		} else {
			acc = 0;
		}
		if (app.state == State::Options) drawOptions(app);

		if (!shotPath.empty() && autoFrames >= 0 && frames + 1 >= autoFrames) platform::saveScreenshot(shotPath);
		platform::endFrame();

		if (autoFrames >= 0 && ++frames >= autoFrames) break;
	}

	app.game.reset();
	platform::shutdown();
	return 0;
}
