// The base: the building is one picture (tools/make_base.py), Up / Down picks a room and the fighter walks
// there along the floors and up the ladder. The panel on the right shows the room at once; Left / Right go
// through its tabs, Cross moves the input into the panel. The shooting range is a real level.
#include "base.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "game.h"
#include "i18n.h"
#include "profile.h"
#include "progress.h"

namespace {

constexpr int S = 2;                               // the picture is 480x272, shown at 2x
constexpr int ROOMS = 4, RANGE_ROOM = 3;
constexpr int FLOORS[ROOMS] = {64, 126, 188, 250};  // must match tools/make_base.py
constexpr int LADDER_X = 39, ROOMS_X = 72, ROOMS_END = 287;
constexpr float WALK = 2.6f, CLIMB = 3.0f;

constexpr int PX = 316, PY = 16, PW = 157;         // the panel's text area
constexpr int LIST_Y = 69, ROW_H = 13, ARMORY_ROWS = 6;
constexpr int MESSAGE_Y = 234, KEYS_Y = 246;

struct Color {
	uint8_t r, g, b;
};
const Color WHITE{255, 255, 255}, YELLOW{255, 255, 0}, GREY{150, 150, 150}, DIM{190, 190, 190}, RULE{120, 120, 120};

enum class Kind { Outfit, Contracts, Arsenal, Perks, Skills, Range };

struct Tab {
	const char* label;
	Kind kind;
	const char* arg;
	int x, facing;         // where the fighter stands for it
	const char* about;
};

struct RoomDef {
	const char* title;
	std::vector<Tab> tabs;
};

const RoomDef& roomDef(int i) {
	static const RoomDef all[ROOMS] = {
		{"WARDROBE",
		 {{"OUTFIT", Kind::Outfit, "", 118, -1, "Choose what your fighter wears in the fights."},
		  {"CONTRACTS", Kind::Contracts, "", 216, 1, "Daily jobs pay extra tokens."}}},
		{"ARMORY",
		 {{"FIREARMS", Kind::Arsenal, "Gun", 110, -1, "One perk for each group of guns."},
		  {"THROWN", Kind::Arsenal, "Throwable", 170, 1, "One perk for each group of thrown weapons."},
		  {"MELEE", Kind::Arsenal, "Melee", 226, 1, "One perk for all melee weapons."}}},
		{"GYM",
		 {{"ENDURANCE", Kind::Perks, "endurance", 176, -1, "The barbell keeps you on your feet."},
		  {"STRENGTH", Kind::Perks, "strength", 108, -1, "The punching bag makes every blow count."},
		  {"AGILITY", Kind::Perks, "agility", 238, 1, "The treadmill makes you quick."},
		  {"SKILLS", Kind::Skills, "", 134, 1, "One active skill for the fights."}}},
		{"RANGE",
		 {{"TARGETS", Kind::Range, "", 136, 1, "A real level to try everything out. Weapons lie around, nothing here costs tokens. Leave through its door."}}},
	};
	return all[i];
}

Texture* capsSheet() { return platform::image("images/base/caps.png"); }

// everything below works in picture pixels
void rect(float x, float y, float w, float h, Color c) { platform::drawRect(x * S, y * S, w * S, h * S, c.r, c.g, c.b, 255); }

void caps(std::string t, float x, float y, Color c = WHITE) {
	platform::drawTextSheet(capsSheet(), 6, 8, 6, i18n::upper(t), x * S, y * S, (float)S, c.r, c.g, c.b);
}

int capsWidth(const std::string& t) { return (int)platform::charCount(t) * 6 - 1; }

void capsRight(const std::string& t, float right, float y, Color c = WHITE) { caps(t, right - capsWidth(t), y, c); }

// the heavy titles of the original's menus
void title(const std::string& t, float x, float y) {
	std::vector<std::string> all = i18n::letters(i18n::upper(t));
	for (int i = 0; i < (int)all.size(); i++)
		for (int dx : {0, 2})
			platform::drawTextSheet(capsSheet(), 6, 8, 6, all[i], (x + i * 14 + dx) * S, y * S, 2.0f * S);
}

std::vector<std::string> wrap(const std::string& t, int perLine) {
	std::vector<std::string> lines;
	std::string line, word;
	for (size_t i = 0; i <= t.size(); i++) {
		if (i < t.size() && t[i] != ' ') {
			word += t[i];
			continue;
		}
		if (!line.empty() && (int)(platform::charCount(line) + 1 + platform::charCount(word)) > perLine) {
			lines.push_back(line);
			line.clear();
		}
		if (!word.empty()) line += (line.empty() ? "" : " ") + word;
		word.clear();
	}
	if (!line.empty()) lines.push_back(line);
	return lines;
}

float paragraph(const std::string& t, float x, float y, Color c = DIM, int perLine = 26, int gap = 9) {
	for (auto& l : wrap(t, perLine)) {
		caps(l, x, y, c);
		y += gap;
	}
	return y;
}

void rule(float y) { rect(PX, y, PW, 1, RULE); }

void bar(float x, float y, float w, float f, Color c = WHITE) {
	rect(x, y, w, 4, {45, 45, 45});
	rect(x, y, w * std::max(0.f, std::min(1.f, f)), 4, c);
}

// [X] ACCEPT, in the yellow boxes of the original
float key(const char* label, const std::string& t, float x) {
	const std::string name = tr(t);
	float w = capsWidth(label) + 6;
	rect(x, KEYS_Y, w, 1, YELLOW);
	rect(x, KEYS_Y + 9, w, 1, YELLOW);
	rect(x, KEYS_Y, 1, 10, YELLOW);
	rect(x + w - 1, KEYS_Y, 1, 10, YELLOW);
	caps(label, x + 3, KEYS_Y + 2, YELLOW);
	caps(name, x + w + 4, KEYS_Y + 2);
	return x + w + 4 + capsWidth(name) + 8;
}

#ifdef __vita__
const char* const KEY_OK = "X", * const KEY_BACK = "O", * const KEY_ALT = "^";
const char* const SKILL_KEY = "Select";
#else
const char* const KEY_OK = "ENT", * const KEY_BACK = "ESC", * const KEY_ALT = "T";
const char* const SKILL_KEY = "C";
#endif

void listRow(float y, Texture* icon, const std::string& name, const std::string& right, bool chosen, bool dim = false) {
	if (chosen) {
		rect(PX, y, PW, 12, {60, 60, 0});
		rect(PX, y, 1, 12, YELLOW);
	}
	float tx = PX + 4;
	if (icon) {
		rect(PX + 2, y + 1, 27, 10, {120, 120, 120});
		float sc = S;
		platform::drawImage(icon, std::round((PX + 15.5f - icon->w / 2.0f)) * S, std::round(y + 6 - icon->h / 2.0f) * S, sc);
		tx = PX + 32;
	}
	Color c = chosen ? YELLOW : (dim ? Color{110, 110, 110} : WHITE);
	caps(name, tx, y + 3, c);
	if (!right.empty()) capsRight(right, PX + PW - 3, y + 3, c);
}

// The fighter with everything they wear, facing left or right
void drawFighter(float x, float y, float scale, int frameX, int frameY, bool flip, uint8_t alpha = 255) {
	const FighterProfile& me = FighterProfile::local();
	const int cell = 25;
	std::vector<Texture*> layers{platform::image("images/player.png")};
	auto& reg = Registry::instance();
	for (const char* slot : {"torso", "hands", "face", "headwear"}) {
		auto chosen = me.apparel.find(slot);
		if (chosen == me.apparel.end() || chosen->second.empty()) continue;
		auto cat = reg.apparel.find(slot);
		if (cat == reg.apparel.end()) continue;
		auto item = cat->second.find(chosen->second);
		if (item != cat->second.end()) layers.push_back(platform::image(item->second));
	}
	for (Texture* tex : layers)
		if (tex) platform::drawImage(tex, x, y, scale, frameX * cell, frameY * cell, cell, cell, flip, alpha);
}

std::string itemTitle(std::string s) {
	if (s.empty()) return tr("None");
	for (char& c : s)
		if (c == '_') c = ' ';
	s[0] = (char)toupper((unsigned char)s[0]);
	return tr(s);
}

std::string number(float v) {
	char buf[32];
	snprintf(buf, sizeof buf, v == std::floor(v) ? "%.0f" : "%.1f", v);
	return buf;
}

// weapon of a kind, in the order of the definitions; improvised ones are not on display
std::vector<std::string> weaponsOf(const std::string& cls) {
	std::vector<std::string> out;
	auto& reg = Registry::instance();
	for (auto& key : reg.order) {
		const ObjDef& d = reg.defs[key];
		if (d.cls == cls && key != "fists" && d.durability == 0) out.push_back(key);
	}
	return out;
}

// a stat with the equipped perk of the weapon's group applied
float withPerk(const std::string& weapon, const char* effect, float value) {
	FighterProfile& me = FighterProfile::local();
	Progress& pr = Progress::get();
	auto eq = me.weaponPerks.find(pr.groupOf(weapon));
	if (eq == me.weaponPerks.end()) return value;
	const Perk* p = pr.perk(eq->second);
	if (!p) return value;
	auto it = p->effects.find(effect);
	return it == p->effects.end() ? value : value * it->second;
}

std::vector<const Perk*> perksOf(const std::string& group, const std::string& branch) {
	std::vector<const Perk*> out;
	for (auto& p : Progress::get().perks)
		if ((!group.empty() && p.group == group) || (!branch.empty() && p.branch == branch)) out.push_back(&p);
	return out;
}


const Tab& currentTab(int room, const int* tabs) { return roomDef(room).tabs[tabs[room]]; }

// what Cross does to a perk or a skill right now
std::string perkVerb(const std::string& id, bool on, const std::string& pending) {
	if (!FighterProfile::local().unlocked.count(id)) return pending == id ? "PAY" : "BUY";
	return on ? "TAKE OFF" : "WEAR";
}

}  // namespace

// ------------------------------------------------------------------ moving around

void BaseScreen::openRoom(int r) {
	selectRoom(r);
	x = (float)currentTab(room, tabs).x;
	y = (float)FLOORS[room];
	walking = climbing = false;
}

void BaseScreen::comeBackFromRange() {
	focused = focused && !rangeWeapon.empty();
	toRange = false;
	if (rangeWeapon.empty()) openRoom(RANGE_ROOM);   // a weapon on trial: back to the same list
	rangeWeapon.clear();
}

void BaseScreen::resetList() {
	cursor = listIndex = 0;
	perkColumn = false;
	pendingBuy.clear();
	message.clear();
}

void BaseScreen::selectRoom(int r) {
	room = r;
	toRange = false;
	resetList();
}

void BaseScreen::selectTab(int dir) {
	int n = (int)roomDef(room).tabs.size();
	int next = std::max(0, std::min(n - 1, tabs[room] + dir));
	if (next == tabs[room]) return;
	tabs[room] = next;
	resetList();
	if (currentTab(room, tabs).kind == Kind::Contracts) focused = false;   // nothing to pick there
}

// to the ladder, along it to the right floor, along the floor to the tab's place
void BaseScreen::walkFighter() {
	const Tab& tab = currentTab(room, tabs);
	float floorY = (float)FLOORS[room];
	float goal = y == floorY ? (float)tab.x : (float)LADDER_X;
	walking = climbing = false;
	if (x != goal) {
		float d = goal - x;
		if (std::fabs(d) <= WALK) {
			x = goal;
		} else {
			facing = d > 0 ? 1 : -1;
			x += facing * WALK;
			walking = true;
		}
	} else if (y != floorY) {
		float d = floorY - y;
		y = std::fabs(d) <= CLIMB ? floorY : y + (d > 0 ? CLIMB : -CLIMB);
		climbing = true;
	} else {
		facing = tab.facing;
		if (toRange) {
			toRange = false;
			rangeWeapon.clear();
			action = BaseAction::Range;
		}
	}
}

void BaseScreen::updateBuilding(const MenuInput& in) {
	if (in.up && room > 0) selectRoom(room - 1);
	if (in.down && room < ROOMS - 1) selectRoom(room + 1);
	if (in.left || in.lb) selectTab(-1);
	if (in.right || in.rb) selectTab(1);

	Kind kind = currentTab(room, tabs).kind;
	if (in.confirm) {
		if (kind == Kind::Range) {
			if (toRange) {   // already on the way: a second press does not wait for the walk
				x = (float)currentTab(room, tabs).x;
				y = (float)FLOORS[room];
			}
			toRange = true;
		} else if (kind != Kind::Contracts) {
			focused = true;
			resetList();
		}
	}
	if (in.back || in.start) action = BaseAction::Leave;
}

// ------------------------------------------------------------------ the panel

// not yet bought: a first press names the price, the second pays; bought: equip or take off
void BaseScreen::perkAction(const std::string& id, int cost) {
	FighterProfile& me = FighterProfile::local();
	if (me.unlocked.count(id)) {
		me.togglePerk(id);
		pendingBuy.clear();
		return;
	}
	if (me.tokens < cost) {
		message = trf("Need %s more tokens", std::to_string(cost - me.tokens));
		return;
	}
	if (pendingBuy != id) {
		pendingBuy = id;
		message = trf("Press again to pay %s", std::to_string(cost));
		return;
	}
	me.buy(id, cost);
	me.togglePerk(id);
	pendingBuy.clear();
	message = tr("Unlocked");
}

void BaseScreen::updatePanel(const MenuInput& in) {
	FighterProfile& me = FighterProfile::local();
	Progress& pr = Progress::get();
	const Tab& tab = currentTab(room, tabs);
	auto move = [&](int count) {
		if (in.up) cursor = (cursor + count - 1) % count;
		if (in.down) cursor = (cursor + 1) % count;
		if (in.up || in.down) {
			pendingBuy.clear();
			message.clear();
		}
	};

	if (in.back || in.start) {
		if (perkColumn) {
			perkColumn = false;
			pendingBuy.clear();
			message.clear();
			return;
		}
		if (tab.kind == Kind::Outfit) me.save();
		focused = false;
		return;
	}
	if (in.lb || in.rb) {
		selectTab(in.lb ? -1 : 1);
		return;
	}

	if (tab.kind == Kind::Outfit) {
		static const char* slots[] = {"headwear", "face", "torso", "hands"};
		move(4);
		int dir = (in.right ? 1 : 0) - (in.left ? 1 : 0);
		if (dir != 0 || in.confirm) {
			std::vector<std::string> items{""};
			auto& apparel = Registry::instance().apparel;
			auto cat = apparel.find(slots[cursor]);
			if (cat != apparel.end())
				for (auto& kv : cat->second) items.push_back(kv.first);
			std::string& current = me.apparel[slots[cursor]];
			int index = (int)(std::find(items.begin(), items.end(), current) - items.begin());
			int n = (int)items.size();
			current = items[((index % n) + (dir != 0 ? dir : 1) + n) % n];
		}
		return;
	}
	if (in.left || in.right) {
		selectTab(in.left ? -1 : 1);
		return;
	}

	if (tab.kind == Kind::Arsenal) {
		std::vector<std::string> list = weaponsOf(tab.arg);
		const std::string& weapon = list[std::min(listIndex, (int)list.size() - 1)];
		std::vector<const Perk*> perks = perksOf(pr.groupOf(weapon), "");
		if (!perkColumn) {
			if (in.up) listIndex = (listIndex + (int)list.size() - 1) % (int)list.size();
			if (in.down) listIndex = (listIndex + 1) % (int)list.size();
			if (in.confirm && !perks.empty()) {
				perkColumn = true;
				cursor = 0;
			}
		} else {
			move((int)perks.size());
			if (in.confirm) perkAction(perks[cursor]->id, perks[cursor]->cost);
		}
		if (in.alt) {   // straight to the range, with this one in the hands
			rangeWeapon = weapon;
			action = BaseAction::Range;
		}
	} else if (tab.kind == Kind::Perks) {
		std::vector<const Perk*> perks = perksOf("", tab.arg);
		if (!perks.empty()) {
			move((int)perks.size());
			if (in.confirm) perkAction(perks[cursor]->id, perks[cursor]->cost);
		}
	} else if (tab.kind == Kind::Skills) {
		if (!pr.skills.empty()) {
			move((int)pr.skills.size());
			if (in.confirm) perkAction(pr.skills[cursor].id, pr.skills[cursor].cost);
		}
	}
}

// ------------------------------------------------------------------ drawing

void BaseScreen::drawBuilding() {
	platform::drawImage(platform::image("images/base/building.png"), 0, 0, (float)S);

	int fx = 0, fy = 0;
	const std::string label = currentTab(room, tabs).label;
	bool settled = !walking && !climbing && y == FLOORS[room];
	if (climbing) {
		static const int cycle[4] = {1, 2, 3, 2};
		fx = cycle[((int)y / 6) & 3];
		fy = 6;
	} else if (walking) {
		static const int cycle[4] = {3, 4, 5, 4};
		fx = cycle[(frame / 8) % 4];
	} else if (settled && label == "STRENGTH") {
		fx = (frame / 20) % 2 ? 2 : 1;
		fy = 2;
	} else if (settled && label == "AGILITY") {
		static const int run[4] = {6, 4, 7, 4};
		fx = run[(frame / 5) % 4];
	} else if (settled && label == "ENDURANCE") {
		fx = (frame / 40) % 2 ? 0 : 1;
		fy = 8;
	} else {
		static const int idle[4] = {0, 1, 2, 1};
		fx = idle[(frame / 24) % 4];
	}
	drawFighter((x - 12) * S, (y - 22) * S, (float)S, fx, fy, facing < 0);

	for (int i = 0; i < ROOMS; i++) {
		const std::string name = tr(roomDef(i).title);
		float tx = ROOMS_X + 2, ty = FLOORS[i] - 49, tw = capsWidth(name) + 6;
		bool sel = i == room;
		rect(tx, ty, tw, 10, sel ? YELLOW : Color{120, 120, 120});
		if (!sel) rect(tx + 1, ty + 1, tw - 2, 8, {0, 0, 0});
		caps(name, tx + 3, ty + 2, sel ? Color{0, 0, 0} : DIM);
	}

	// corner brackets around the room
	float x0 = ROOMS_X - 4, x1 = ROOMS_END, y0 = FLOORS[room] - 49, y1 = FLOORS[room] - 1;
	for (float bx : {x0, x1 - 6}) {
		rect(bx, y0, 7, 1, YELLOW);
		rect(bx, y1, 7, 1, YELLOW);
	}
	for (float bx : {x0, x1}) {
		rect(bx, y0, 1, 7, YELLOW);
		rect(bx, y1 - 6, 1, 7, YELLOW);
	}
}

void BaseScreen::drawTop() {
	FighterProfile& me = FighterProfile::local();
	const RoomDef& def = roomDef(room);
	title(tr(def.title), PX, PY);

	const std::string money = std::to_string(me.tokens);
	float mx = PX + PW - capsWidth(money);
	capsRight(money, PX + PW, PY + 3, YELLOW);
	rect(mx - 9, PY + 4, 7, 5, YELLOW);
	rect(mx - 8, PY + 3, 5, 7, YELLOW);

	rule(PY + 19);
	// as many tabs as fit, from one that leaves the open tab in sight
	int open = tabs[room], first = 0;
	auto width = [&](int from, int to) {
		int w = 0;
		for (int i = from; i <= to; i++) w += capsWidth(tr(def.tabs[i].label)) + 8;
		return w - 8;
	};
	while (width(first, open) > PW) first++;
	float tx = PX;
	for (int i = first; i < (int)def.tabs.size(); i++) {
		const std::string label = tr(def.tabs[i].label);
		if (tx + capsWidth(label) > PX + PW) break;
		caps(label, tx, PY + 23, i == open ? YELLOW : GREY);
		tx += capsWidth(label) + 8;
	}
	paragraph(tr(currentTab(room, tabs).about), PX, PY + 34, GREY);
}

void BaseScreen::drawOutfit() {
	static const char* slots[] = {"headwear", "face", "torso", "hands"};
	static const char* names[] = {"Head", "Face", "Body", "Hands"};
	static const int idle[4] = {0, 1, 2, 1};
	FighterProfile& me = FighterProfile::local();
	float top = LIST_Y - 10;
	drawFighter((PX + (PW - 75) / 2.0f) * S, top * S, 3.0f * S, idle[(frame / 24) % 4], 0, false);
	float ry = top + 80;
	for (int i = 0; i < 4; i++) {
		bool sel = focused && i == cursor;
		auto chosen = me.apparel.find(slots[i]);
		caps(tr(names[i]), PX + 2, ry, sel ? YELLOW : GREY);
		caps("< " + itemTitle(chosen == me.apparel.end() ? "" : chosen->second) + " >", PX + 46, ry, sel ? YELLOW : WHITE);
		ry += 12;
	}
}

void BaseScreen::drawContracts() {
	FighterProfile& me = FighterProfile::local();
	Progress& pr = Progress::get();
	float cy = LIST_Y - 8 + 9;
	for (const DailyEvent* e : pr.todaysEvents()) {
		caps(tr(e->name), PX, cy, YELLOW);
		cy = paragraph(tr(e->desc), PX, cy + 9) + 2;
	}
	rule(cy);
	cy += 4;
	for (const Contract* c : pr.todaysContracts()) {
		bool done = me.dailyDone.count(c->id) > 0;
		auto it = me.dailyProgress.find(c->id);
		int count = done ? c->target : (it == me.dailyProgress.end() ? 0 : it->second);
		capsRight("+" + std::to_string(c->reward), PX + PW, cy, YELLOW);
		cy = paragraph(tr(c->name), PX, cy, done ? Color{120, 220, 130} : WHITE, 21);
		bar(PX, cy + 1, PW - 34, (float)count / c->target, done ? Color{0, 200, 0} : WHITE);
		capsRight(std::to_string(count) + "/" + std::to_string(c->target), PX + PW, cy, GREY);
		cy += 12;
	}
	caps(tr("Rank") + ": " + tr(pr.rank(me.earned).name), PX, cy, GREY);
}

void BaseScreen::drawArsenal() {
	FighterProfile& me = FighterProfile::local();
	Progress& pr = Progress::get();
	std::vector<std::string> list = weaponsOf(currentTab(room, tabs).arg);
	if (list.empty()) return;
	int index = std::min(listIndex, (int)list.size() - 1);
	const std::string& key = list[index];
	const ObjDef& d = Registry::instance().defs[key];
	std::string group = pr.groupOf(key);
	auto eq = me.weaponPerks.find(group);
	std::vector<const Perk*> perks = perksOf(group, "");

	if (perkColumn) {
		float ry = LIST_Y;
		listRow(ry, d.image, itemTitle(key), "", false);
		ry += ROW_H + 2;
		for (int i = 0; i < (int)perks.size(); i++) {
			const Perk* p = perks[i];
			bool on = eq != me.weaponPerks.end() && eq->second == p->id;
			bool owned = me.unlocked.count(p->id) > 0;
			listRow(ry, nullptr, tr(p->name), on ? tr("ON") : (owned ? tr("OWNED") : std::to_string(p->cost)), i == cursor);
			ry += ROW_H;
		}
		rule(ry + 2);
		if (cursor < (int)perks.size()) paragraph(tr(perks[cursor]->desc), PX, ry + 7);
		for (auto& g : pr.groups)
			if (g.id == group) paragraph(trf("For all: %s", tr(g.name)), PX, MESSAGE_Y - 21, GREY);
		return;
	}

	int top = std::max(0, std::min(index - ARMORY_ROWS / 2, (int)list.size() - ARMORY_ROWS));
	for (int i = top; i < std::min(top + ARMORY_ROWS, (int)list.size()); i++) {
		const ObjDef& item = Registry::instance().defs[list[i]];
		bool hasPerk = false;
		auto worn = me.weaponPerks.find(pr.groupOf(list[i]));
		if (worn != me.weaponPerks.end() && !worn->second.empty()) hasPerk = true;
		listRow(LIST_Y + (i - top) * ROW_H, item.image, itemTitle(list[i]), hasPerk ? tr("PERK") : "", focused && i == index);
	}
	if ((int)list.size() > ARMORY_ROWS) {   // where in the list we are
		float h = ROW_H * ARMORY_ROWS;
		rect(PX + PW + 2, LIST_Y, 2, h, {45, 45, 45});
		rect(PX + PW + 2, LIST_Y + h * top / list.size(), 2, h * ARMORY_ROWS / list.size(), GREY);
	}

	float ry = LIST_Y + ARMORY_ROWS * ROW_H + 1;
	rule(ry);
	ry += 4;
	auto stat = [&](const char* name, float value, float top) {
		caps(tr(name), PX, ry, GREY);
		bar(PX + 54, ry + 1, PW - 88, value / top);
		capsRight(number(value), PX + PW, ry);
		ry += 9;
	};
	if (d.cls == "Gun") {
		stat("DAMAGE", withPerk(key, "damageMul", d.damage.hp * (float)d.count), 70);
		stat("RATE", 60.0f / std::max(1.f, withPerk(key, "timingMul", d.timing)) * d.burst, 15);
		stat("AMMO", withPerk(key, "ammoMul", d.ammo), 60);
		stat("RECOIL", withPerk(key, "recoilMul", d.recoil), 3);
		stat("PUSH", withPerk(key, "knockbackMul", d.knockback), 5);
	} else if (d.cls == "Melee") {
		stat("DAMAGE", withPerk(key, "meleeDamageMul", d.damage.hp), 20);
		stat("THROW HIT", withPerk(key, "thrownMul", d.thrownDamage > 0 ? d.thrownDamage : 18), 50);
		stat("PUSH", d.knockback, 5);
	} else {
		stat("CARRIED", d.ammo, 6);
		if (d.damage.hp > 0) stat("DAMAGE", d.damage.hp, 20);
	}
	if (eq != me.weaponPerks.end()) {
		if (const Perk* p = pr.perk(eq->second)) caps(tr("PERK") + ": " + tr(p->name), PX, ry + 2, YELLOW);
	}
}

void BaseScreen::drawPerks() {
	FighterProfile& me = FighterProfile::local();
	Progress& pr = Progress::get();
	std::vector<const Perk*> perks = perksOf("", currentTab(room, tabs).arg);
	float ry = LIST_Y;
	for (int i = 0; i < (int)perks.size(); i++) {
		const Perk* p = perks[i];
		bool on = std::find(me.passives.begin(), me.passives.end(), p->id) != me.passives.end();
		bool owned = me.unlocked.count(p->id) > 0;
		listRow(ry, nullptr, tr(p->name), on ? tr("ON") : (owned ? tr("OWNED") : std::to_string(p->cost)), focused && i == cursor);
		ry += ROW_H;
	}
	rule(ry + 2);
	if (cursor < (int)perks.size()) {
		const Perk* p = perks[cursor];
		caps(tr(p->name), PX, ry + 7, YELLOW);
		paragraph(tr(p->desc), PX, ry + 17);
	}
	caps(tr("Worn") + ": " + std::to_string(me.passives.size()) + "/" + std::to_string(pr.passiveSlots), PX, MESSAGE_Y - 12, GREY);
}

void BaseScreen::drawSkills() {
	FighterProfile& me = FighterProfile::local();
	Progress& pr = Progress::get();
	float ry = LIST_Y;
	for (int i = 0; i < (int)pr.skills.size(); i++) {
		const Skill& s = pr.skills[i];
		bool owned = me.unlocked.count(s.id) > 0;
		listRow(ry, nullptr, tr(s.name), me.skill == s.id ? tr("ON") : (owned ? tr("OWNED") : std::to_string(s.cost)), focused && i == cursor);
		ry += ROW_H;
	}
	rule(ry + 2);
	if (cursor < (int)pr.skills.size()) {
		const Skill& s = pr.skills[cursor];
		float after = paragraph(tr(s.desc), PX, ry + 7);
		caps(trf("Ready every %s s", std::to_string(s.cooldown / 60)), PX, after + 2, GREY);
	}
	caps(trf("Used with %s", SKILL_KEY), PX, MESSAGE_Y - 12, GREY);
}

void BaseScreen::drawRange() {
	Texture* target = platform::image("images/target.png");
	if (target)
		for (int i = 0; i < 3; i++) platform::drawImage(target, (PX + 34 + i * 34) * S, (LIST_Y + 42) * S, (float)S);
}

void BaseScreen::drawKeys() {
	Kind kind = currentTab(room, tabs).kind;
	float kx = PX;
	if (!focused) {
		kx = key("+", "MOVE", kx);
		if (kind == Kind::Range)
			kx = key(KEY_OK, "ENTER", kx);
		else if (kind != Kind::Contracts)
			kx = key(KEY_OK, "OPEN", kx);
		key(KEY_BACK, "EXIT", kx);
		return;
	}
	Progress& pr = Progress::get();
	FighterProfile& me = FighterProfile::local();
	std::string verb = "ACCEPT";
	if (kind == Kind::Outfit) {
		verb = "CHANGE";
	} else if (kind == Kind::Arsenal && !perkColumn) {
		verb = "PERKS";
	} else if (kind == Kind::Arsenal) {
		std::vector<std::string> list = weaponsOf(currentTab(room, tabs).arg);
		std::string group = pr.groupOf(list[std::min(listIndex, (int)list.size() - 1)]);
		std::vector<const Perk*> perks = perksOf(group, "");
		auto eq = me.weaponPerks.find(group);
		if (cursor < (int)perks.size())
			verb = perkVerb(perks[cursor]->id, eq != me.weaponPerks.end() && eq->second == perks[cursor]->id, pendingBuy);
	} else if (kind == Kind::Perks) {
		std::vector<const Perk*> perks = perksOf("", currentTab(room, tabs).arg);
		if (cursor < (int)perks.size()) {
			const std::string& id = perks[cursor]->id;
			verb = perkVerb(id, std::find(me.passives.begin(), me.passives.end(), id) != me.passives.end(), pendingBuy);
		}
	} else if (kind == Kind::Skills && cursor < (int)pr.skills.size()) {
		verb = perkVerb(pr.skills[cursor].id, me.skill == pr.skills[cursor].id, pendingBuy);
	}
	kx = key(KEY_OK, verb, kx);
	kx = key(KEY_BACK, "BACK", kx);
	if (kind == Kind::Arsenal) key(KEY_ALT, "TRY", kx);
}

void BaseScreen::draw() {
	drawBuilding();
	drawTop();
	switch (currentTab(room, tabs).kind) {
		case Kind::Outfit: drawOutfit(); break;
		case Kind::Contracts: drawContracts(); break;
		case Kind::Arsenal: drawArsenal(); break;
		case Kind::Perks: drawPerks(); break;
		case Kind::Skills: drawSkills(); break;
		case Kind::Range: drawRange(); break;
	}
	caps(message, PX, MESSAGE_Y, YELLOW);
	drawKeys();
}

BaseAction BaseScreen::update(const MenuInput& in) {
	frame++;
	action = BaseAction::None;
	if (focused)
		updatePanel(in);
	else
		updateBuilding(in);
	walkFighter();
	draw();
	return action;
}
