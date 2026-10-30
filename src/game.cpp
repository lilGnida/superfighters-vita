// Port of game.js (offline core: no rollback / network yet)
#include "game.h"

#include "i18n.h"
#include "bot.h"
#include "profile.h"
#include "progress.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// The original's deterministic "random" table (values / 100)
const int RANDOM_TABLE[] = {
	33, 32, 34, 2,  35, 1,  10, 36, 38, 39, 17, 19, 9,  16, 0,  37, 8,  11, 41, 43, 5,  42, 44,
	18, 45, 46, 47, 13, 62, 14, 52, 61, 63, 21, 60, 54, 53, 56, 4,  59, 20, 51, 6,  49, 22, 23,
	57, 48, 15, 58, 50, 25, 7,  26, 24, 27, 29, 30, 28, 31, 100, 64, 97, 99, 96, 98, 70, 72, 82,
	80, 83, 75, 90, 88, 89, 66, 81, 68, 65, 67, 91, 92, 71, 95, 85, 94, 76, 74, 84, 87, 69, 77,
	86, 93, 73, 79, 78, 40, 3,  55, 12};
constexpr int RANDOM_COUNT = sizeof(RANDOM_TABLE) / sizeof(RANDOM_TABLE[0]);

float gameAngleOf(Vec2 v) {
	float deg = std::atan2(std::fabs(v.y), std::fabs(v.x)) * 180.0f / PI_F;
	if (v.x < 0) return v.y > 0 ? 180 + deg : 180 - deg;
	return v.y > 0 ? 360 - deg : deg;
}

}  // namespace

Game::Game(const GameOptions& options) : mapText_(options.mapText), localPlayers_(options.localPlayers) {
	mode = options.mode;
	suddenDeathOn = options.suddenDeath;
	botCount_ = options.bots;
	botDifficulty_ = options.botDifficulty;
	mapName_ = options.mapName;
	rewards_ = options.rewards;
	range = options.range;
	rangeWeapon_ = options.rangeWeapon;
	startPistols_ = options.startPistols;
	intro_ = options.intro;
	if (range) suddenDeathOn = false;

	// Play an ambient noise
	platform::playSound(GameSounds::get().ambient);

	for (int i = 0; i < localPlayers_; i++) {
		PlayerSlot slot;
		slot.local = true;
		slot.localId = i;
		slot.profile = {{"name", trf("Player %s", std::to_string(i + 1))}};
		if (i == 0) slot.profile = FighterProfile::local().toGameProfile();
		if (i == 1) slot.profile["torso"] = "sweater";
		players.push_back(slot);
	}

	if (mode != MODE_WAVES)
		for (int i = 0; i < options.bots; i++) players.push_back(makeBotSlot(i + 1, options.botDifficulty));
	if (mode == MODE_TEAMS)
		for (size_t i = 0; i < players.size(); i++) players[i].team = (int)(i % 2) + 1;

	restartGame();
}

// Bots, each dressed differently so they can be told apart
PlayerSlot Game::makeBotSlot(int number, int difficulty) {
	auto& apparel = Registry::instance().apparel;
	PlayerSlot slot;
	slot.local = false;
	slot.localId = -1;
	slot.bot = std::make_shared<Bot>((BotDifficulty)std::max(0, std::min(2, difficulty)), (unsigned)rand() + number * 7919u);
	slot.profile = {{"name", trf("Bot %s", std::to_string(number))}};
	for (const char* cat : {"torso", "headwear", "face"}) {
		auto it = apparel.find(cat);
		if (it == apparel.end() || it->second.empty()) continue;
		if (std::string(cat) != "torso" && rand() % 2) continue;
		auto item = it->second.begin();
		std::advance(item, (size_t)(rand() % it->second.size()));
		slot.profile[cat] = item->first;
	}
	return slot;
}

Game::~Game() {
	platform::stopAllSounds();
	objects.clear();
	byId.clear();
}

float Game::random() {
	seed++;
	if (seed >= RANDOM_COUNT) seed = 0;
	return RANDOM_TABLE[seed] / 100.0f;
}

void Game::restartGame() {
	frameCounter = 0;
	seed = (int)std::lround((double)rand() / RAND_MAX * (RANDOM_COUNT - 1));
	lastWeaponDrop = 0;
	gameOver = 0;
	firstCamera = true;
	hitStopFrames = slowFrames = shakeFrames = flashFrames = 0;
	lastSupply = 0;
	suddenDeath = false;
	introFrames = intro_ && !range ? INTRO_FRAMES : 0;
	fightBanner = 0;
	waveDelay = 0;
	rewardTokens = 0;
	stats.clear();

	if (mode == MODE_WAVES) players.resize(localPlayers_);

	loadMap(mapText_);
	createPlayers();
	fillWeaponSpawns();

	if (range) {
		lastWeaponDrop = -1000;
		Obj* me = getObjectById(players[0].objectId);
		if (me && !rangeWeapon_.empty())
			if (Obj* w = createObject(rangeWeapon_.c_str(), ObjOptions().at(me->getPosition()))) {
				Player* p = static_cast<Player*>(me);
				w->pickup(*p);
				int slot = w->type() == ObjType::Gun ? INV_GUN : (w->type() == ObjType::Melee ? INV_MELEE : INV_THROWABLE);
				p->inventory[slot] = w->id;
			}
	}

	if (mode == MODE_WAVES) {
		wave = 1;
		spawnWave();
	}
}

void Game::loadMap(const std::string& text) {
	// Reset physics world and objects
	objects.clear();
	byId.clear();
	graveyard.clear();
	world = std::make_unique<PhysicsWorld>();
	nextId = 0;

	json map = json::parse(text, nullptr, false);
	if (map.is_discarded() || !map.contains("objects")) {
		platform::log("map could not be parsed");
		return;
	}

	layers.clear();
	levelBounds.reset();
	deathY.reset();
	acid.reset();
	particles.clear();
	clearColor[0] = clearColor[1] = clearColor[2] = 0;
	if (map.contains("layers")) {
		for (auto& l : map["layers"]) {
			MapLayer layer;
			layer.tex = platform::image(l.value("image", ""));
			layer.x = l.value("x", 0.f);
			layer.y = l.value("y", 0.f);
			layer.parallax = l.value("parallax", 1.f);
			layer.front = l.value("front", false);
			if (layer.tex) layers.push_back(layer);
			else platform::log("map layer image '%s' missing", l.value("image", "").c_str());
		}
	}
	if (map.contains("bounds")) {
		const json& b = map["bounds"];
		Bounds bb;
		bb.min = {b.value("x", 0.f), b.value("y", 0.f)};
		bb.max = {bb.min.x + b.value("w", 0.f), bb.min.y + b.value("h", 0.f)};
		levelBounds = bb;
	}
	minZoom = map.value("zoom", 1.5f);
	if (map.contains("deathY")) deathY = map["deathY"].get<float>();
	else if (levelBounds) deathY = levelBounds->max.y + 80;   // fell out of the level
	if (map.contains("acid")) acid = AcidPool{map["acid"][0].get<float>(), map["acid"][1].get<float>(), map["acid"][2].get<float>()};
	if (map.contains("color")) {
		for (int i = 0; i < 3; i++) clearColor[i] = (uint8_t)map["color"][i].get<int>();
	}

	for (auto& entry : map["objects"]) {
		if (entry.is_null()) continue;
		const ObjDef* def = Registry::instance().get(entry.value("parentKey", ""));
		if (!def) {
			platform::log("map references unknown object '%s'", entry.value("parentKey", "").c_str());
			continue;
		}
		Obj* o = createObject(def, ObjOptions::fromJson(entry));
		if (o && def->key == "range_target") {
			TargetSpot spot;
			spot.pos = o->getPosition();
			spot.objectId = o->id;
			if (entry.contains("rail")) {
				spot.rail = {entry["rail"].value("dx", 0.f), entry["rail"].value("dy", 0.f)};
				spot.period = entry["rail"].value("period", 300);
			}
			targetSpots.push_back(spot);
		}
	}

	doors.clear();
	for (Obj* d : getObjectsByParent(Registry::instance().get("exit_door"))) doors.push_back(d->id);

	nav.build(*this);
}

// ------------------------------------------------------------------ objects

Obj* Game::createObject(const char* key, const ObjOptions& opts) {
	return createObject(Registry::instance().get(key), opts);
}

Obj* Game::createObject(const ObjDef* def, const ObjOptions& opts) {
	if (!def) return nullptr;

	int id = opts.id ? *opts.id : nextId;
	if (id >= nextId) nextId = id + 1;

	std::shared_ptr<Obj> obj;
	const std::string& cls = def->cls;

	// flames are the one thing that can multiply out of control
	if (cls == "Fire") {
		if (fireCount >= MAX_FIRES) return nullptr;
		fireCount++;
	}

	if (cls == "Player")
		obj = std::make_shared<Player>(*this, *def, opts, id);
	else if (cls == "Gun")
		obj = std::make_shared<Gun>(*this, *def, opts, id);
	else if (cls == "Melee")
		obj = std::make_shared<Melee>(*this, *def, opts, id);
	else if (cls == "Throwable")
		obj = std::make_shared<Throwable>(*this, *def, opts, id);
	else if (cls == "Projectile")
		obj = std::make_shared<Projectile>(*this, *def, opts, id);
	else if (cls == "Fire")
		obj = std::make_shared<Fire>(*this, *def, opts, id);
	else if (cls == "Effect")
		obj = std::make_shared<Effect>(*this, *def, opts, id);
	else if (cls == "Marker")
		obj = std::make_shared<Marker>(*this, *def, opts, id);
	else if (cls == "Ladder")
		obj = std::make_shared<Ladder>(*this, *def, opts, id);
	else if (cls == "Platform")
		obj = std::make_shared<Platform>(*this, *def, opts, id);
	else if (cls == "Door")
		obj = std::make_shared<Door>(*this, *def, opts, id);
	else if (cls == "Corpse")
		obj = std::make_shared<Corpse>(*this, *def, opts, id);
	else if (cls == "Hanging")
		obj = std::make_shared<Hanging>(*this, *def, opts, id);
	else if (cls == "Conveyor")
		obj = std::make_shared<Conveyor>(*this, *def, opts, id);
	else if (cls == "Mine")
		obj = std::make_shared<Mine>(*this, *def, opts, id);
	else
		obj = std::make_shared<Obj>(*this, *def, opts, id);

	obj->born = frameCounter;
	objects.push_back(obj);
	byId[id] = obj;
	return obj.get();
}

void Game::removeObject(Obj* o) {
	o->alive = false;
	auto it = byId.find(o->id);
	if (it != byId.end() && it->second.get() == o) {
		graveyard.push_back(it->second);   // keep the object alive until the end of the frame
		byId.erase(it);
	}
	objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const std::shared_ptr<Obj>& p) { return p.get() == o; }),
				  objects.end());
}

Obj* Game::getObjectById(int id) const {
	auto it = byId.find(id);
	return it == byId.end() ? nullptr : it->second.get();
}

std::shared_ptr<Obj> Game::getShared(int id) const {
	auto it = byId.find(id);
	return it == byId.end() ? nullptr : it->second;
}

std::vector<Obj*> Game::getObjectsByParent(const ObjDef* parent) const {
	std::vector<Obj*> out;
	for (auto& o : objects)
		if (o->parent == parent) out.push_back(o.get());
	return out;
}

std::vector<Obj*> Game::getObjectsByAABB(const Bounds& bounds) const {
	std::vector<Obj*> out;
	for (Body* b : world->queryRegion(bounds))
		if (Obj* o = getObjectByBody(b)) out.push_back(o);
	return out;
}

std::vector<Obj*> Game::getPlayers() const {
	std::vector<Obj*> out;
	for (auto& o : objects)
		if (o->type() == ObjType::Player) out.push_back(o.get());
	return out;
}

void Game::shuffle(std::vector<Obj*>& v) {
	for (int i = (int)v.size() - 1; i > 0; i--) {
		int j = std::min(i, (int)std::floor(random() * (i + 1)));
		std::swap(v[i], v[j]);
	}
}

void Game::createPlayers() {
	std::vector<Obj*> spawns = getObjectsByParent(Registry::instance().get("player_spawn"));
	if (spawns.empty()) return;

	// Randomly sort the array
	shuffle(spawns);

	// Create players within the spawns
	for (size_t i = 0; i < players.size(); i++) {
		ObjOptions opts;
		opts.at(spawns[i % spawns.size()]->getPosition());
		opts.profile = players[i].profile;
		opts.team = players[i].team;

		if (Obj* p = createObject("player", opts)) {
			players[i].objectId = p->id;
			static_cast<Player*>(p)->isBot = players[i].bot != nullptr;
			applyLoadout(static_cast<Player*>(p), players[i]);
			if (startPistols_)
				if (Obj* w = createObject("pistol", ObjOptions().at(p->getPosition()))) {
					w->pickup(*static_cast<Player*>(p));
					static_cast<Player*>(p)->inventory[INV_GUN] = w->id;
				}
		}
	}
}

// The local player's choices; bots get a random set, more of it the harder they are
void Game::applyLoadout(Player* p, const PlayerSlot& slot) {
	Progress& pr = Progress::get();
	if (slot.local) {
		if (slot.localId == 0) {
			FighterProfile& me = FighterProfile::local();
			p->setLoadout(me.loadoutPerks(), me.skill);
		}
		return;
	}
	if (botDifficulty_ == 0 || pr.perks.empty()) return;

	std::vector<std::string> perks;
	int count = botDifficulty_ == 1 ? 1 : 3;
	for (int i = 0; i < count; i++) perks.push_back(pr.perks[std::min((int)pr.perks.size() - 1, (int)(random() * pr.perks.size()))].id);
	std::string skill;
	if (botDifficulty_ == 2 && !pr.skills.empty()) skill = pr.skills[std::min((int)pr.skills.size() - 1, (int)(random() * pr.skills.size()))].id;
	p->setLoadout(perks, skill);
}

// ------------------------------------------------------------------ frame

void Game::handlePlayerInput() {
	for (auto& slot : players) {
		if (slot.local) {
			slot.input = platform::playerInput(slot.localId);
		} else if (slot.bot) {
			Obj* o = getObjectById(slot.objectId);
			slot.input = (o && o->type() == ObjType::Player) ? slot.bot->think(*this, *static_cast<Player*>(o)) : PlayerInput();
		}
	}
}

// Everything a fighter can carry and find lying about
std::vector<std::string> Game::weaponKeys() {
	static std::vector<std::string> keys;
	if (keys.empty()) {
		auto& reg = Registry::instance();
		for (auto& key : reg.order) {
			const ObjDef& d = reg.defs[key];
			bool weapon = d.cls == "Gun" || d.cls == "Throwable" || d.cls == "Melee";
			if (weapon && key != "fists" && d.durability == 0) keys.push_back(key);
		}
	}
	return keys;
}

void Game::spawnLoot(Vec2 pos) {
	std::vector<std::string> keys = weaponKeys();
	ObjOptions o;
	o.at(pos).vel({random() * 2 - 1, -3}).life(900);
	o.isStatic = false;
	o.group = "item";
	createObject(keys[std::min((int)keys.size() - 1, (int)(random() * keys.size()))].c_str(), o);
}

// Height just under whatever is above p
float Game::ceilingAbove(Vec2 p) {
	float top = levelBounds ? levelBounds->min.y + 4 : p.y - 200;
	Vec2 hit;
	bool found = world->raycast(p, {p.x, top}, [&](Body* b) {
		Obj* o = getObjectByBody(b);
		return b->isStatic && (!o || o->type() == ObjType::Base);
	}, &hit);
	return found ? hit.y + 12 : top;
}

void Game::handleWeaponDrops() {
	auto& reg = Registry::instance();

	// A crate comes down now and then; it holds a weapon and hurts whoever stands below
	if (!range && frameCounter - lastSupply > 1200) {
		lastSupply = frameCounter;
		std::vector<Obj*> points = getObjectsByParent(reg.get("weapon_spawn"));
		shuffle(points);
		if (!points.empty()) {
			Vec2 p = points[0]->getPosition();
			ObjOptions o;
			o.at({p.x, ceilingAbove(p)});
			o.customId = "supply";
			createObject("crate", o);
		}
	}

	int diff = frameCounter - lastWeaponDrop;
	if (diff <= 180) return;

	lastWeaponDrop = frameCounter;

	std::vector<Obj*> spawns = getObjectsByParent(reg.get("weapon_spawn"));
	shuffle(spawns);

	std::vector<std::string> weaponKeys = this->weaponKeys();
	if (weaponKeys.empty()) return;

	// randomly choose one
	int pick = std::min((int)weaponKeys.size() - 1, (int)std::floor(random() * weaponKeys.size()));

	// Check if the spawn doesn't already have a weapon associated with it
	for (Obj* s : spawns) {
		Marker* spawn = static_cast<Marker*>(s);

		if (!getObjectById(spawn->targetAId)) {
			ObjOptions wo;
			wo.at(spawn->getPosition()).life(1500);
			wo.group = "none";   // floats in place: must not shove the furniture around it
			Obj* weapon = createObject(weaponKeys[pick].c_str(), wo);
			if (weapon) spawn->targetAId = weapon->id;
			break;
		}
	}
}

// The round opens with a weapon on every spawn point
void Game::fillWeaponSpawns() {
	std::vector<std::string> keys = weaponKeys();
	if (keys.empty()) return;
	for (Obj* s : getObjectsByParent(Registry::instance().get("weapon_spawn"))) {
		ObjOptions wo;
		wo.at(s->getPosition()).life(1500);
		wo.group = "none";
		int pick = std::min((int)keys.size() - 1, (int)std::floor(random() * keys.size()));
		if (Obj* weapon = createObject(keys[pick].c_str(), wo)) static_cast<Marker*>(s)->targetAId = weapon->id;
	}
}

// A long round ends with bombs raining on the fighters
void Game::handleSuddenDeath() {
	if (!suddenDeathOn || mode == MODE_WAVES || frameCounter < 3600 || gameOver > 0) return;
	suddenDeath = true;
	if (frameCounter % 45 != 0) return;
	std::vector<Obj*> fighters = getPlayers();
	if (fighters.empty()) return;
	Vec2 p = fighters[std::min((int)fighters.size() - 1, (int)(random() * fighters.size()))]->getPosition();
	p.x += random() * 80 - 40;
	createObject("lobbed_grenade", ObjOptions().at({p.x, ceilingAbove(p)}));
}

void Game::spawnWave() {
	// the fallen of the last wave make room
	for (Obj* c : getObjectsByParent(Registry::instance().get("corpse"))) c->kill();
	players.resize(localPlayers_);

	std::vector<Obj*> spawns = getObjectsByParent(Registry::instance().get("player_spawn"));
	if (spawns.empty()) return;
	shuffle(spawns);

	int count = std::min(7, wave + 1);
	int difficulty = std::min(2, botDifficulty_ + (wave - 1) / 3);
	for (int i = 0; i < count; i++) {
		PlayerSlot slot = makeBotSlot(i + 1, difficulty);
		ObjOptions opts;
		opts.at(spawns[i % spawns.size()]->getPosition());
		opts.profile = slot.profile;
		if (Obj* p = createObject("player", opts)) {
			slot.objectId = p->id;
			static_cast<Player*>(p)->isBot = true;
			applyLoadout(static_cast<Player*>(p), slot);
		}
		players.push_back(slot);
	}
}

void Game::handleWaves() {
	if (gameOver > 0) {
		if (++gameOver >= 300) restartGame();
		return;
	}
	int humans = 0, bots = 0;
	for (auto& slot : players)
		if (getObjectById(slot.objectId)) (slot.bot ? bots : humans)++;

	if (humans == 0) {
		gameOver = 1;
		slowFrames = 90;
		lastWinner = -1;
		endOfRound();
	} else if (bots == 0) {
		if (waveDelay == 0) {
			waveDelay = 120;
			addStat("wave");
		} else if (--waveDelay == 0) {
			wave++;
			for (auto& slot : players)
				if (Obj* o = getObjectById(slot.objectId)) o->health = std::min(100.0f, o->health + 30);
			spawnWave();
		}
	}
}

// The local player's pay for the round
void Game::endOfRound() {
	if (lastWinner >= 0) {
		bool mine = players[lastWinner].local;
		if (mode == MODE_TEAMS)
			for (auto& slot : players) mine = mine || (slot.local && slot.team == players[lastWinner].team);
		if (mine) addStat("win");
	}
	if (rewards_) rewardTokens = Progress::get().award(stats, mapName_, mode, botDifficulty_);
	stats.clear();
}

// The shooting range never ends: targets come back, so does a fallen fighter
void Game::handleRange() {
	if (!getObjectById(players[0].objectId)) {
		restartGame();
		return;
	}
	for (TargetSpot& spot : targetSpots) {
		Obj* t = getObjectById(spot.objectId);
		if (t) {
			if (spot.period) {
				Vec2 p = targetPosition(spot);
				t->setPosition(p.x, p.y);
			}
		} else if (frameCounter % 240 == 0) {
			if (Obj* fresh = createObject("range_target", ObjOptions().at(targetPosition(spot)))) spot.objectId = fresh->id;
		}
	}
}

Vec2 Game::targetPosition(const TargetSpot& spot) const {
	if (!spot.period) return spot.pos;
	float t = (float)(frameCounter % spot.period) / spot.period * 2;
	float k = t < 1 ? t : 2 - t;
	return {spot.pos.x + spot.rail.x * k, spot.pos.y + spot.rail.y * k};
}

void Game::handleGameOver() {
	if (range) {
		handleRange();
		return;
	}
	if (mode == MODE_WAVES) {
		handleWaves();
		return;
	}

	if (gameOver == 0) {
		int count = 0, firstTeam = -1;
		bool oneTeam = mode == MODE_TEAMS;
		for (auto& slot : players)
			if (getObjectById(slot.objectId)) {
				count++;
				if (firstTeam < 0) firstTeam = slot.team;
				oneTeam = oneTeam && slot.team == firstTeam;
			}

		if ((count <= 1 || oneTeam) && players.size() > 1) {
			gameOver = 1;
			slowFrames = 90;   // the final blow in slow motion
			lastWinner = -1;
			for (size_t i = 0; i < players.size(); i++)
				if (getObjectById(players[i].objectId)) {
					if (lastWinner < 0) lastWinner = (int)i;
					players[i].wins++;
				}
			endOfRound();
		}

	} else {
		gameOver++;
		if (gameOver >= 300) restartGame();
	}
}

void Game::collisionUpdate() {
	struct Pair {
		std::shared_ptr<Obj> a, b;
		Vec2 normal;
	};
	std::vector<Pair> pairs;

	for (auto& c : world->touchingContacts()) {
		auto a = getShared(c.a->clientId);
		auto b = getShared(c.b->clientId);
		if (a && b) pairs.push_back({a, b, c.normal});
	}

	// Create temporary collision lists
	std::unordered_map<int, std::vector<int>> temp;
	for (auto& o : objects)
		if (o->body) temp[o->id];

	for (auto& p : pairs) {
		Vec2 n = p.normal;
		// angle of the contact direction as seen from each object (pointing at the other one)
		temp[p.a->id].push_back(p.b->id);
		p.a->addCollision(*p.b, gameAngleOf(n));

		temp[p.b->id].push_back(p.a->id);
		p.b->addCollision(*p.a, gameAngleOf({-n.x, -n.y}));
	}

	// Update objects collision lists
	for (auto& o : objects) {
		if (!o->body) continue;

		auto& list = temp[o->id];
		auto& cols = o->collisions;
		cols.erase(std::remove_if(cols.begin(), cols.end(),
								  [&](const CollisionRef& r) { return std::find(list.begin(), list.end(), r.objectId) == list.end(); }),
				   cols.end());
	}
}

void Game::update() {
	graveyard.clear();

	if (shakeFrames > 0) shakeFrames--;
	if (flashFrames > 0) flashFrames--;
	if (hitStopFrames > 0) {
		hitStopFrames--;
		return;
	}
	if (slowFrames > 0) {
		slowFrames--;
		if (++slowTick % 3 != 0) return;
	}

	// Handle incoming player input; the countdown holds everybody back
	if (introFrames > 0) {
		if (--introFrames == 0) fightBanner = FIGHT_BANNER_FRAMES;
		for (auto& slot : players) slot.input = PlayerInput();
	} else {
		if (fightBanner > 0) fightBanner--;
		handlePlayerInput();
	}

	// Update camera position
	updateCamera(firstCamera);
	firstCamera = false;

	handleGameOver();
	handleWeaponDrops();
	handleSuddenDeath();

	// Send input to player objects
	for (auto& slot : players) {
		Obj* obj = getObjectById(slot.objectId);
		if (obj && obj->type() == ObjType::Player) static_cast<Player*>(obj)->input = slot.input;
	}

	updateParticles();
	fireCount = 0;
	for (auto& obj : objects)
		if (obj->type() == ObjType::Fire) fireCount++;

	// Generic object update (snapshot: objects may be created / killed meanwhile)
	std::vector<std::shared_ptr<Obj>> snapshot = objects;
	for (auto& obj : snapshot)
		if (obj->alive) obj->update();

	// Physics update step
	for (auto& obj : objects)
		if (obj->body && !obj->body->isStatic) obj->prevVelocity = obj->getVelocity();
	world->step();
	collisionUpdate();

	if (deathY) {
		std::vector<std::shared_ptr<Obj>> snapshot = objects;
		for (auto& obj : snapshot)
			if (obj->alive && obj->body && !obj->body->isStatic && obj->getPosition().y > *deathY) obj->kill();
	}

	if (acid) dissolveInAcid();

	frameCounter++;
}

void Game::smashGlass(const Obj& flying) {
	Vec2 v = flying.getVelocity();
	if (v.length() < 2) return;
	Bounds b = flying.getBounds();   // where it will be next: the pane must go before it stops the flight
	b.min = {b.min.x + std::min(v.x, 0.f) - 1, b.min.y + std::min(v.y, 0.f) - 1};
	b.max = {b.max.x + std::max(v.x, 0.f) + 1, b.max.y + std::max(v.y, 0.f) + 1};
	for (Obj* o : getObjectsByAABB(b))
		if (o->alive && o->parent->key == "glass") o->dealDamage({5, "collision"});
}

void Game::dissolveInAcid() {
	std::vector<Obj*> gone;
	for (auto& obj : objects) {
		if (!obj->alive || !obj->body || obj->body->isStatic || obj->carriedBy >= 0) continue;
		Vec2 p = obj->getPosition();
		if (!inAcid({p.x, p.y - 4})) continue;
		ObjType t = obj->type();
		bool held = (t == ObjType::Gun && static_cast<Gun*>(obj.get())->holderId >= 0) ||
					(t == ObjType::Melee && static_cast<Melee*>(obj.get())->holderId >= 0) ||
					(t == ObjType::Throwable && static_cast<Throwable*>(obj.get())->holderId >= 0);
		if (!held && (t == ObjType::Base || t == ObjType::Corpse || t == ObjType::Gun || t == ObjType::Melee || t == ObjType::Throwable))
			gone.push_back(obj.get());
	}
	for (Obj* o : gone) o->kill();
}

// ------------------------------------------------------------------ particles

void Game::puff(const char* key, Vec2 pos, Vec2 vel, float life) {
	if ((int)particles.size() >= Settings::get().maxParticles()) return;
	const ObjDef* def = Registry::instance().get(key);
	if (!def || !def->image) return;
	float frames = life >= 0 ? life : (def->lifeTime ? *def->lifeTime : 30);
	if (frames < 1) return;
	particles.push_back({def, pos.x, pos.y, vel.x, vel.y, (int)frames, (int)frames});
}

void Game::updateParticles() {
	for (size_t i = 0; i < particles.size();) {
		Particle& p = particles[i];
		p.x += p.vx;
		p.y += p.vy;
		if (--p.life <= 0) {
			p = particles.back();
			particles.pop_back();
		} else {
			i++;
		}
	}
}

void Game::drawParticles() {
	for (const Particle& p : particles) {
		const ObjDef* def = p.def;
		int fw = def->image->w / def->frameCountX, fh = def->image->h / def->frameCountY;
		SpriteDraw d;
		d.tex = def->image;
		d.sx = fw * ((frameCounter / 3 + (int)p.x) % def->frameCountX);
		d.sw = fw;
		d.sh = fh;
		d.dw = (float)fw;
		d.dh = (float)fh;
		d.ox = p.x;
		d.oy = p.y;
		d.offX = -fw / 2.0f;
		d.offY = -fh / 2.0f;
		platform::setAlpha(def->fade ? (float)p.life / p.maxLife : 1.0f);
		platform::drawSprite(d);
	}
	platform::setAlpha(1.0f);
}

Vec2 Game::cameraTopLeft() const {
	return {camera.x - (SCREEN_W / 2.0f) / camera.zoom, camera.y - (SCREEN_H / 2.0f) / camera.zoom};
}

// Who the camera has to keep in the picture
std::vector<Vec2> Game::cameraSubjects() const {
	std::vector<Vec2> all, near;
	std::vector<Obj*> fighters = getPlayers();
	for (Obj* p : fighters) all.push_back(p->getPosition());
	if (Settings::get().camera != CAMERA_NEAR_ME) return all;

	// the local fighters and whoever is closest to them
	for (auto& slot : players) {
		Obj* me = getObjectById(slot.objectId);
		if (!slot.local || !me) continue;
		near.push_back(me->getPosition());
		Obj* closest = nullptr;
		float best = 1e9f;
		for (Obj* p : fighters) {
			if (p == me || (slot.team > 0 && static_cast<Player*>(p)->team == slot.team)) continue;
			float d = (p->getPosition() - me->getPosition()).length();
			if (d < best) {
				best = d;
				closest = p;
			}
		}
		if (closest) near.push_back(closest->getPosition());
	}
	return near.empty() ? all : near;
}

void Game::updateCamera(bool snap) {
	std::vector<Vec2> positions = cameraSubjects();
	if (positions.empty()) return;

	const Settings& set = Settings::get();
	bool follow = levelBounds && !range && set.camera != CAMERA_WIDE;

	Bounds bounds = Bounds::fromPoints(positions);
	bounds.min.x -= follow ? 60 : 32;
	bounds.min.y -= follow ? 64 : 64;
	bounds.max.x += follow ? 60 : 32;
	bounds.max.y += follow ? 44 : 64;

	float width = bounds.max.x - bounds.min.x;
	float height = bounds.max.y - bounds.min.y;

	float scale = width > height ? SCREEN_W / width : SCREEN_H / height;

	float rx = bounds.min.x + width / 2;
	float ry = bounds.min.y + height / 2;

	// Levels with original art: show the whole level when it fits, otherwise follow
	// the players without leaving the level
	if (levelBounds) {
		float lw = levelBounds->max.x - levelBounds->min.x;
		float lh = levelBounds->max.y - levelBounds->min.y;
		float fit = std::min(SCREEN_W / lw, SCREEN_H / lh);
		scale = std::max(fit, minZoom);
		if (follow) {
			// as close as the two furthest fighters allow; small changes are not worth the pumping
			scale = std::max(fit, std::min(std::min(SCREEN_W / width, SCREEN_H / height), set.maxZoom()));
			if (!snap && scale > camera.zoom && scale < camera.zoom * 1.08f) scale = camera.zoom;
		}
		float halfW = SCREEN_W / 2.0f / scale, halfH = SCREEN_H / 2.0f / scale;
		auto clampAxis = [](float v, float lo, float hi, float half) {
			if (hi - lo <= half * 2) return (lo + hi) / 2;
			return std::max(lo + half, std::min(hi - half, v));
		};
		rx = clampAxis(rx, levelBounds->min.x, levelBounds->max.x, halfW);
		ry = clampAxis(ry, levelBounds->min.y, levelBounds->max.y, halfH);
	}

	if (snap) {
		camera = {rx, ry, scale};
	} else {
		// fighters leaving the picture matter more than a calm camera: out fast, in slowly
		float pan = follow ? 12 : 25, zoom = follow ? (scale < camera.zoom ? 14 : 60) : 25;
		camera.x += (rx - camera.x) / pan;
		camera.y += (ry - camera.y) / pan;
		camera.zoom += (scale - camera.zoom) / zoom;
	}

	// the zoom lags behind the target: the edge of the level must stay out of the picture meanwhile
	if (levelBounds) {
		auto clampAxis = [](float v, float lo, float hi, float half) {
			if (hi - lo <= half * 2) return (lo + hi) / 2;
			return std::max(lo + half, std::min(hi - half, v));
		};
		camera.x = clampAxis(camera.x, levelBounds->min.x, levelBounds->max.x, SCREEN_W / 2.0f / camera.zoom);
		camera.y = clampAxis(camera.y, levelBounds->min.y, levelBounds->max.y, SCREEN_H / 2.0f / camera.zoom);
	}
}

void Game::draw() {
	Vec2 tl = cameraTopLeft();
	if (shakeFrames > 0 && Settings::get().shake) {   // (screen only: rand() stays out of the game state)
		tl.x += ((rand() % 200) / 100.0f - 1) * shakeStrength;
		tl.y += ((rand() % 200) / 100.0f - 1) * shakeStrength;
	}
	platform::setCamera(tl.x, tl.y, camera.zoom);

	if (clearColor[0] || clearColor[1] || clearColor[2])
		platform::drawRect(0, 0, SCREEN_W, SCREEN_H, clearColor[0], clearColor[1], clearColor[2], 255);

	auto drawLayers = [&](bool front) {
		for (auto& l : layers) {
			if (l.front != front) continue;
			SpriteDraw d;
			d.tex = l.tex;
			d.sw = d.dw = l.tex->w;
			d.sh = d.dh = l.tex->h;
			// parallax < 1 lags behind the camera (distant sky)
			d.ox = l.x + tl.x * (1 - l.parallax);
			d.oy = l.y + tl.y * (1 - l.parallax);
			platform::drawSprite(d);
		}
	};
	drawLayers(false);

	// Draw order = creation order
	std::vector<std::shared_ptr<Obj>> snapshot = objects;
	for (auto& obj : snapshot)
		if (obj->alive) obj->draw();
	drawParticles();

	drawLayers(true);

	if (acid) {   // whoever wades in sinks behind the surface
		Vec2 a = worldToScreen({acid->x0, acid->y}), b = worldToScreen({acid->x1, acid->y + 40});
		platform::drawRect(a.x, a.y, b.x - a.x, b.y - a.y, 126, 255, 64, 130);
	}

	platform::setAlpha(1.0f);
	if (flashFrames > 0) platform::drawRect(0, 0, SCREEN_W, SCREEN_H, 255, 255, 255, (uint8_t)std::min(255, flashFrames * 14));
}

// ------------------------------------------------------------------ effects & forces

void Game::createExplosion(Vec2 pos, float radius, float forceMultiplier, float damageMul, int ownerId) {
	// Explosion hitbox and collision check (by id: a barrel blowing up removes bodies from the list)
	std::vector<int> caught;
	for (auto& hit : world->collideCircle(pos, radius))
		if (Obj* o = getObjectByBody(hit.body)) caught.push_back(o->id);

	for (int id : caught) {
		Obj* obj = getObjectById(id);
		if (!obj || !obj->body) continue;

		Vec2 difference = obj->getPosition() - pos;
		Vec2 direction = difference.normalised();
		float distance = difference.length();

		float distanceMultiplier = (distance <= radius) ? ((radius - distance) / radius) : 0;

		Vec2 force = direction * (forceMultiplier * distanceMultiplier);

		if (obj->type() == ObjType::Player) {
			Player* p = static_cast<Player*>(obj);
			if (ownerId >= 0 && ownerId != p->id) p->lastHitBy = ownerId;
			p->knockback(direction * (8 * distanceMultiplier), true);
		} else {
			obj->body->applyForce(pos, force);
		}

		obj->dealDamage({distanceMultiplier * radius * damageMul, "explosion"});
	}

	// Create multiple visual explosions
	Vec2 relative[6];
	for (int i = 0; i < 6; i++) {
		float angle = random() * 2 * PI_F;
		relative[i] = {std::cos(angle) * random() * radius, std::sin(angle) * random() * radius};
	}

	for (auto& rel : relative) createObject("explosion_large", ObjOptions().at({pos.x + rel.x, pos.y + rel.y}));

	platform::playSoundRandom(GameSounds::get().explosion);
}

void Game::createFireplosion(Vec2 pos, float radius, int count) {
	for (int i = 0; i < count; i++) {
		float angle = (PI_F * 2) * ((float)i / count);
		createObject("fire", ObjOptions().at(pos).vel({std::cos(angle) * radius, std::sin(angle) * radius}));
	}
	platform::playSoundRandom(GameSounds::get().fireplosion);
}

int Game::createHitbox(Obj* source, const std::vector<Rectangle>& hitboxes, Vec2 force, const DamageSpec& damage, Vec2 push) {
	std::vector<Obj*> hitObjects;
	std::vector<std::shared_ptr<Obj>> keep;

	// Collision check body
	for (auto& hb : hitboxes) {
		for (auto& hit : world->collideBox({hb.x, hb.y}, hb.width, hb.height)) {
			Obj* obj = getObjectByBody(hit.body);

			// (effects have bodies too: a blow lasting several frames must not feed on its own sparks)
			if (obj && (obj->type() == ObjType::Effect || obj->type() == ObjType::Marker || obj->type() == ObjType::Fire)) continue;

			if (obj && source != obj && std::find(hitObjects.begin(), hitObjects.end(), obj) == hitObjects.end()) {
				hitObjects.push_back(obj);
				keep.push_back(getShared(obj->id));

				// no sparks from the floor under a passing air attack
				if (hit.pointCount > 1 && (!hit.body->isStatic || (push.x == 0 && push.y == 0)))
					createObject("hit", ObjOptions().at(hit.point));
			}
		}
	}

	int loose = 0;
	for (Obj* obj : hitObjects) {
		if (obj->body && !obj->body->isStatic && obj->parent->group != "debris") loose++;   // litter stops no blow
		if (obj->body) obj->body->applyForce(obj->getPosition(), force);

		DamageSpec d = damage;
		if (obj->type() == ObjType::Player) {
			Player* target = static_cast<Player*>(obj);
			Player* striker = source && source->type() == ObjType::Player ? static_cast<Player*>(source) : nullptr;
			if (striker && target->team > 0 && target->team == striker->team) continue;
			if (striker && target->countering()) {   // the blow comes back
				striker->lastHitBy = target->id;
				striker->knockback({target->facingDirection * 2.5f, -1.2f}, true);
				striker->dealDamage({d.hp * target->skill->amount, "melee"});
				createObject("hit", ObjOptions().at(striker->getPosition()));
				continue;
			}
			Vec2 pushed = push;
			if (striker) {
				target->lastHitBy = striker->id;
				if (striker->raging()) {
					if (pushed.x == 0 && pushed.y == 0) pushed = {striker->facingDirection * 1.6f, -1.0f};
					pushed = pushed * striker->skill->amount;
				}
			}
			if (pushed.x != 0 || pushed.y != 0) target->knockback(pushed, true);
			if (striker) striker->landedHit(*target, d);
		}
		obj->dealDamage(d);
	}
	return loose;
}
