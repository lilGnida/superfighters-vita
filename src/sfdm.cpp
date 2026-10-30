#include "sfdm.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "json.hpp"
#include "physics.h"

using json = nlohmann::json;

namespace {

constexpr int TILE = 8;
constexpr float PI_F = 3.14159265f;

// slots of images/sfd_tiles.png, 8 to a row (tools/make_sfd_tiles.py)
enum Tile { CONCRETE, METAL, STONE, WOOD, DIRT, ASPHALT, BRICK, ROOF, BG_TEAL = 16, BG_BLUE, BG_WOOD, BG_STONE, BG_GREEN, BG_GREY, BG_WINDOW, LEDGE, LADDER };

struct Raw {
	std::string name;
	float x, y, rot;
	int w, h;
};

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool startsWithAny(const std::string& s, std::initializer_list<const char*> list) {
	for (const char* p : list)
		if (startsWith(s, p)) return true;
	return false;
}

bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

int solidTile(const std::string& n) {
	if (startsWith(n, "CONCRETE")) return CONCRETE;
	if (startsWithAny(n, {"METAL0", "METAL1", "METALTRAIN", "BLASTDOOR", "METALHATCH", "CARGOCONTAINER", "DUCTWALL", "TINROOF", "BULLETPROOFGLASS", "REINFORCEDGLASS", "CONVEYORBELT", "PLACEHOLDERTILERAMP"})) return METAL;
	if (startsWithAny(n, {"STONEWEAK", "STONE0", "STONE1", "ROCK", "ADOBE"})) return STONE;
	if (startsWithAny(n, {"WOOD0", "WOOD1"})) return WOOD;
	if (startsWithAny(n, {"DIRT", "SAND", "SNOW", "GRASS"})) return DIRT;
	if (startsWith(n, "ASPHALT")) return ASPHALT;
	if (startsWithAny(n, {"BRICK", "TILE"})) return BRICK;
	if (startsWith(n, "ROOFTILE")) return ROOF;
	if (startsWith(n, "INVISIBLEBLOCK") && n != "INVISIBLEBLOCKNOCOLLISION") return -2;
	return -1;
}

int wallTile(const std::string& n) {
	if (startsWith(n, "BGSHADOW") || startsWith(n, "BGTEXT") || startsWith(n, "BGSYMBOL") || startsWith(n, "BGSIGN")) return -1;
	if (contains(n, "WINDOW")) return BG_WINDOW;
	if (contains(n, "WOOD")) return BG_WOOD;
	if (contains(n, "STONE") || contains(n, "DIRT") || contains(n, "ADOBE")) return BG_STONE;
	if (contains(n, "MILITARY") || contains(n, "TREE")) return BG_GREEN;
	if (contains(n, "METAL") || contains(n, "PIPE") || contains(n, "MACHINE") || contains(n, "ELECTRIC")) return BG_BLUE;
	if (contains(n, "BEAM") || contains(n, "VENT")) return BG_GREY;
	return BG_TEAL;
}

const char* propKey(const std::string& n) {
	if (startsWith(n, "BARRELEXPLOSIVE")) return "explosive_barrel";
	if (startsWith(n, "BARREL00") || startsWith(n, "WOODBARREL")) return "barrel";
	if (startsWith(n, "CRATE")) return "crate";
	if (n == "TABLE00") return "table";
	if (n == "CHAIR00" || startsWith(n, "SWIVELCHAIR")) return "chair";
	if (startsWith(n, "COMPUTER00") || startsWith(n, "MONITOR")) return "computer_monitor";
	if (startsWith(n, "FILECAB")) return "filecab";
	if (n == "DESK00" || startsWith(n, "METALDESK")) return "desk";
	return nullptr;
}

uint32_t u32(const std::string& d, size_t i) {
	uint32_t v;
	memcpy(&v, d.data() + i, 4);
	return v;
}

float f32(const std::string& d, size_t i) {
	float v;
	memcpy(&v, d.data() + i, 4);
	return v;
}

// An object: length, NAME, 1, 0, [10, 0,] 2, x, y, 3, rotation, then its other properties
std::vector<Raw> readObjects(const std::string& d) {
	std::vector<Raw> out;
	std::vector<size_t> ends;
	const size_t n = d.size();
	for (size_t i = 0; i + 40 < n; i++) {
		size_t len = (unsigned char)d[i];
		if (len < 3 || len > 48 || i + 1 + len + 32 >= n) continue;
		if (!isupper((unsigned char)d[i + 1])) continue;
		bool name = true;
		for (size_t k = 1; k <= len && name; k++) {
			unsigned char c = d[i + k];
			name = isalnum(c) || c == '_';
		}
		if (!name) continue;
		size_t p = i + 1 + len;
		if (u32(d, p) != 1 || u32(d, p + 4) != 0) continue;
		p += 8;
		if (u32(d, p) == 10 && d[p + 4] == 0 && d[p + 5] == 0) p += 6;
		if (u32(d, p) != 2 || p + 20 >= n || u32(d, p + 12) != 3) continue;
		out.push_back({d.substr(i + 1, len), f32(d, p + 4), f32(d, p + 8), f32(d, p + 16), 1, 1});
		ends.push_back(i);
		i = p + 19;
	}
	ends.push_back(n);
	for (size_t k = 0; k < out.size(); k++)
		for (size_t p = ends[k]; p + 12 <= ends[k + 1]; p++) {
			if (u32(d, p + 4) != 2) continue;
			uint32_t key = u32(d, p);
			if (key != 0x13 && key != 0x14) continue;
			int v = (int)u32(d, p + 8);
			if (v >= 1 && v <= 400) (key == 0x13 ? out[k].w : out[k].h) = v;
		}
	return out;
}

// top,left,bottom,right of the camera area, y upwards
bool cameraArea(const std::string& d, float out[4]) {
	size_t limit = std::min<size_t>(d.size(), 2000);
	for (size_t i = 0; i < limit; i++) {
		if (!isdigit((unsigned char)d[i]) && d[i] != '-') continue;
		if (i && (isdigit((unsigned char)d[i - 1]) || d[i - 1] == ',' || d[i - 1] == '-')) continue;
		int a, b, c, e, used = 0;
		if (sscanf(d.c_str() + i, "%d,%d,%d,%d%n", &a, &b, &c, &e, &used) == 4 && used > 6) {
			out[0] = (float)a;
			out[1] = (float)b;
			out[2] = (float)c;
			out[3] = (float)e;
			return true;
		}
	}
	return false;
}

struct Rect {
	int x, y, w, h;
};

// neighbours become one body: thousands of 8 pixel walls are slow and fighters catch on their seams
void merge(std::vector<Rect>& r) {
	auto pass = [&](bool across) {
		std::sort(r.begin(), r.end(), [&](const Rect& a, const Rect& b) {
			if (across) return std::tie(a.y, a.h, a.x) < std::tie(b.y, b.h, b.x);
			return std::tie(a.x, a.w, a.y) < std::tie(b.x, b.w, b.y);
		});
		std::vector<Rect> out;
		for (const Rect& c : r) {
			if (!out.empty()) {
				Rect& l = out.back();
				if (across && l.y == c.y && l.h == c.h && l.x + l.w >= c.x) {
					l.w = std::max(l.w, c.x + c.w - l.x);
					continue;
				}
				if (!across && l.x == c.x && l.w == c.w && l.y + l.h >= c.y) {
					l.h = std::max(l.h, c.y + c.h - l.y);
					continue;
				}
			}
			out.push_back(c);
		}
		r.swap(out);
	};
	pass(true);
	pass(false);
	pass(true);
}

struct Result {
	sfdm::Loaded info;
	std::vector<platform::TileCell> cells;
};

Result build(const std::string& d, const std::string& title, bool cells) {
	Result res;
	std::vector<Raw> all = readObjects(d);

	float area[4] = {240, -320, -240, 320};
	cameraArea(d, area);
	float top = area[0], left = area[1], bottom = area[2], right = area[3];
	bool ownSpawns = false;   // some maps place their fighters from a script: these marks are all there is then
	for (const Raw& o : all) ownSpawns = ownSpawns || o.name == "SPAWNPLAYER";
	const char* spawnMark = ownSpawns ? "SPAWNPLAYER" : "SPAWNUNKNOWN";
	for (const Raw& o : all)
		if (o.name == spawnMark) {
			top = std::max(top, o.y + 40);
			bottom = std::min(bottom, o.y - 40);
			left = std::min(left, o.x - 40);
			right = std::max(right, o.x + 40);
		}
	const int width = (int)(right - left), height = (int)(top - bottom);

	json objects = json::array();
	int id = 0;
	auto add = [&](const char* key, float x, float y, json extra = json::object()) {
		json o = extra;
		o["parentKey"] = key;
		o["id"] = id++;
		o["matter"] = {{"position", {{"x", x}, {"y", y}}}};
		objects.push_back(o);
	};
	auto box = [&](const char* key, int x, int y, int w, int h) {
		add(key, x + w / 2.0f, y + h / 2.0f, {{"size", {{"w", w}, {"h", h}}}, {"hidden", true}});
	};

	std::vector<Rect> walls;
	std::vector<platform::TileCell> back, front;
	std::set<std::pair<int, int>> filled;
	std::vector<std::pair<int, int>> solidCells;
	std::vector<int> solidKinds;
	auto put = [&](std::vector<platform::TileCell>& to, int slot, int x, int y) {
		if (cells) to.push_back({(slot % 8) * TILE, (slot / 8) * TILE, x, y});
	};

	for (const Raw& o : all) {
		// to the map's own coordinates, y downwards
		float px = o.x - left, py = top - o.y;
		float c = std::cos(o.rot), s = std::sin(o.rot);
		float spanW = (float)o.w * TILE, spanH = (float)o.h * TILE;
		auto corner = [&](float lx, float ly) { return Vec2{px + lx * c + ly * s, py - lx * s + ly * c}; };
		bool straight = std::fabs(s) < 0.01f || std::fabs(c) < 0.01f;
		Vec2 a = corner(0, 0), b = corner(spanW, spanH);
		int rx = (int)std::lround(std::min(a.x, b.x)), ry = (int)std::lround(std::min(a.y, b.y));
		int rw = (int)std::lround(std::fabs(a.x - b.x)), rh = (int)std::lround(std::fabs(a.y - b.y));

		const std::string& n = o.name;
		int kind = solidTile(n);
		if (kind != -1) {
			res.info.solids++;
			if (straight) {
				walls.push_back({rx, ry, rw, rh});
			} else {
				json verts = json::array();
				Vec2 mid = corner(spanW / 2, spanH / 2);
				for (Vec2 v : {corner(0, 0), corner(0, spanH), corner(spanW, spanH), corner(spanW, 0)})
					verts.push_back({v.x - mid.x, v.y - mid.y});
				add("wall", mid.x, mid.y, {{"verts", verts}, {"hidden", true}});
			}
			if (kind >= 0)
				for (int i = 0; i < o.w; i++)
					for (int j = 0; j < o.h; j++) {
						Vec2 at = corner(i * (float)TILE, j * (float)TILE);
						if (!straight) at = corner((i + .5f) * TILE, (j + .5f) * TILE);
						int qx = (int)std::lround(straight ? std::min(at.x, corner((i + 1) * (float)TILE, (j + 1) * (float)TILE).x) : at.x - 4);
						int qy = (int)std::lround(straight ? std::min(at.y, corner((i + 1) * (float)TILE, (j + 1) * (float)TILE).y) : at.y - 4);
						solidCells.push_back({qx, qy});
						solidKinds.push_back(kind);
						filled.insert({qx, qy});
					}
		} else if (startsWithAny(n, {"METALPLAT", "WOODPLAT", "STONEPLAT", "BRIDGEPLANK", "PLANK", "INVISIBLEPLATFORM"})) {
			box("ledge", rx, ry, rw, 3);
			if (n != "INVISIBLEPLATFORM")
				for (int i = 0; i < rw / TILE; i++) put(front, LEDGE, rx + i * TILE, ry);
		} else if (startsWithAny(n, {"METALLADDER", "INVISIBLELADDER", "WOODLADDER"})) {
			box("climb", rx, ry, rw, rh);
			if (n != "INVISIBLELADDER")
				for (int j = 0; j < rh / TILE; j++)
					for (int i = 0; i < rw / TILE; i++) put(front, LADDER, rx + i * TILE, ry + j * TILE);
		} else if (startsWith(n, "GLASS") && !startsWith(n, "GLASSSHEET")) {
			add("glass", rx + 4.0f, ry + rh / 2.0f, {{"tiling", {{"width", 1}, {"height", std::max(1, rh / 10)}}}});
		} else if (n == spawnMark) {
			add("player_spawn", px + 8, py - 2);
			res.info.players++;
		} else if (n == "SPAWNWEAPON" || n == "SPAWNWEAPONAREA") {
			add("weapon_spawn", px + spanW / 2, py);
		} else if (const char* prop = propKey(n)) {
			add(prop, px + 8, py + 8);
		} else if (startsWith(n, "BG") && !startsWith(n, "BGSHADOW")) {
			int tile = wallTile(n);
			if (tile >= 0)
				for (int i = 0; i < o.w; i++)
					for (int j = 0; j < o.h; j++) put(back, tile, rx + i * TILE, ry + j * TILE);
		}
	}

	merge(walls);
	for (const Rect& r : walls) box("wall", r.x, r.y, r.w, r.h);

	if (cells) {
		res.cells = back;
		for (size_t i = 0; i < solidCells.size(); i++) {
			bool open = !filled.count({solidCells[i].first, solidCells[i].second - TILE});
			put(res.cells, solidKinds[i] + (open ? 8 : 0), solidCells[i].first, solidCells[i].second);
		}
		res.cells.insert(res.cells.end(), front.begin(), front.end());
	}

	json map;
	map["name"] = title;
	map["bounds"] = {{"x", 0}, {"y", 0}, {"w", width}, {"h", height}};
	map["objects"] = objects;
	res.info.mapText = map.dump();
	res.info.width = (float)width;
	res.info.height = (float)height;
	return res;
}

}  // namespace

namespace sfdm {

Loaded parse(const std::string& bytes, const std::string& title) { return build(bytes, title, false).info; }

Loaded load(const std::string& bytes, const std::string& title) {
	Result r = build(bytes, title, true);
	r.info.art = platform::composeTiles("images/sfd_tiles.png", (int)r.info.width, (int)r.info.height, r.cells);
	return r.info;
}

std::string titleOf(const std::string& fileName) {
	std::string s = fileName.substr(0, fileName.rfind('.'));
	size_t digits = 0;
	while (digits < s.size() && isdigit((unsigned char)s[digits])) digits++;
	if (digits && digits < s.size() && s[digits] == '_') s = s.substr(digits + 1);
	for (char& c : s)
		if (c == '_') c = ' ';
	return s;
}

}  // namespace sfdm
