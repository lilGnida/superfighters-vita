// Core game classes: a C++ port of the Super Fighters JS game objects.
#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "json.hpp"
#include "physics.h"
#include "nav.h"
#include "platform.h"

using json = nlohmann::json;

class Game;
class Obj;

// ------------------------------------------------------------------ data definitions

struct Frame {
	int x = 0, y = 0, delay = 0;
};

struct DamageSpec {
	float hp = 0;
	std::string type;     // "" = default
	std::string sound;    // "" = same as type
	float threshold = 0;
};

struct Rectangle {
	float x = 0, y = 0, width = 0, height = 0;
};

using SoundList = std::vector<Sound*>;

struct ObjDef {
	std::string key, cls, group, material;
	std::string imagePath;
	Texture* image = nullptr;
	int frameCountX = 1, frameCountY = 1;
	int frameIndexX = 0, frameIndexY = 0;
	float health = -1;
	bool flammable = false;
	std::string shape;                 // "", "circle", "tl-br", "tr-bl"
	float gravityScale = 1;
	bool disableGravity = false;
	std::optional<float> lifeTime;
	bool fade = false, animateRealTime = false;

	// "matter" defaults
	bool isStatic = false, isSensor = false, inertiaInfinite = false;
	std::optional<float> friction, restitution, frictionAir;

	std::map<std::string, float> damageModifier;
	std::map<std::string, SoundList> sounds;
	std::vector<Frame> animation;

	// weapons
	float ammo = 0, speed = 0, timing = 0, spread = 0, detonationTime = 0;
	float recoil = 0, knockback = 0;         // guns, melee: push on the shooter / on whoever is hit
	float thrownDamage = 0;                  // melee weapon thrown at someone (0 = default)
	int durability = 0;                      // improvised weapons: hits until they break (0 = forever)
	float stun = 0;                          // projectile: frames the target is stunned
	bool plain = false;                      // projectile drawn as it is, not as a stretched tracer
	int count = 1, burst = 1, hands = 0;
	DamageSpec damage;                       // guns: hp only; melee: full spec
	std::string projectile, expel, flash, effect;   // keys of other definitions
	std::vector<std::vector<Rectangle>> hitbox;     // melee, [combo][n]

	CollisionFilter filter;
	bool hasFilter = false;
	float matFriction = -1, matRestitution = -1;    // from material (-1 = none)

	std::function<void(Game&, Obj&)> onkill;
};

struct Registry {
	std::map<std::string, ObjDef> defs;
	std::vector<std::string> order;
	std::map<std::string, std::map<std::string, std::string>> apparel;   // slot -> name -> image path
	std::map<std::string, CollisionFilter> groups;

	const ObjDef* get(const std::string& key) const {
		auto it = defs.find(key);
		return it == defs.end() ? nullptr : &it->second;
	}
	bool load(const std::string& jsonText);
	static Registry& instance();
};

void installCallbacks(Registry& r);   // onkill handlers (defs_callbacks.cpp)

// ------------------------------------------------------------------ per-instance options

struct Callback {
	float delay = 0;
	std::string action;
};

struct StateData {
	std::string name = "none", lastName;
	float entropy = 0;
	float delay = 0, delayMax = 0;
	std::vector<Callback> callbacks;
	int serial = 0;
};

struct CollisionRef {
	int objectId = -1;
	float angle = 0;
};

struct ObjOptions {
	std::optional<int> id;
	std::optional<std::string> customId;
	std::optional<std::string> link;           // doors: customId of the linked door
	std::optional<float> health, fire, lifeTime;
	std::optional<int> facingDirection;
	std::optional<int> frameIndexX, frameIndexY;
	std::optional<int> tilingW, tilingH;
	std::optional<StateData> state;

	std::optional<Vec2> position, velocity;
	std::optional<float> angle, angularVelocity;
	std::optional<bool> isStatic;
	std::optional<bool> disableGravity;
	// level collision: explicit size (px), convex polygon (local px, around position), invisible
	std::optional<Vec2> size;
	std::vector<Vec2> verts;
	bool hidden = false;

	std::optional<float> damage;             // projectile damage
	std::optional<float> ammo;
	std::optional<int> holderId, projectileId, targetAId, targetBId;
	std::map<std::string, float> damageModifier;
	std::optional<float> knockback;          // projectile push
	std::optional<std::string> group;        // collision group instead of the definition's
	std::optional<int> ownerId;              // projectile: the one who fired it
	std::optional<float> speed;              // conveyor
	std::optional<float> blastRadiusMul, blastDamageMul, rideMul;   // perks of whoever launched it
	int pierce = 0;
	bool directExplode = false;

	// player
	std::optional<std::map<std::string, std::string>> profile;
	std::optional<int> team;

	ObjOptions& at(Vec2 p) {
		position = p;
		return *this;
	}
	ObjOptions& vel(Vec2 v) {
		velocity = v;
		return *this;
	}
	ObjOptions& rot(float a) {
		angle = a;
		return *this;
	}
	ObjOptions& life(float l) {
		lifeTime = l;
		return *this;
	}
	static ObjOptions fromJson(const json& j);
};

// ------------------------------------------------------------------ objects

// Direction of a contact as seen from an object (degrees, screen up = 90). Steps and ramps
// count as ground up to 50 degrees: the art has stairs steeper than 45.
inline bool angleIsGround(float a) { return a >= 220 && a <= 320; }
inline bool angleIsLeft(float a) { return a >= 135 && a < 220; }
inline bool angleIsRight(float a) { return (a >= -45 && a <= 45) || (a > 320 && a <= 405); }

enum class ObjType { Base, Player, Gun, Melee, Throwable, Projectile, Fire, Effect, Marker, Ladder, Platform, Door, Corpse, Conveyor };

const char* typeName(ObjType t);

// Constructor-time overrides that subclasses (Player, Projectile) pass to the base class,
// like the extra parameter objects of `super(...params, {...})` in the JS version.
struct BodyOverrides {
	std::optional<float> width, height;
	std::vector<Vec2> verts;      // convex polygon instead of the default rectangle
	bool sensor = false, inertiaInfinite = false, disableGravity = false;
	std::optional<float> friction;
};

class Obj {
public:
	Obj(Game& game, const ObjDef& parent, const ObjOptions& opts, int id, const BodyOverrides& ov = {});
	virtual ~Obj();

	virtual ObjType type() const { return ObjType::Base; }
	virtual void update();
	virtual void draw() { drawSprite(); }
	virtual bool dealDamage(const DamageSpec& damage);
	virtual void delayCallback(const std::string& action);
	virtual void addCollision(Obj& source, float angleForThis);
	virtual void pickup(Obj& /*equiper*/) {}

	// generic helpers (names follow the JS version)
	Vec2 getPosition() const { return body ? body->position() : Vec2{0, 0}; }
	void movePosition(float x, float y);
	void setPosition(float x, float y);
	Vec2 getVelocity() const { return body ? body->velocity() : Vec2{0, 0}; }
	void setVelocity(float x, float y);
	void moveVelocity(float x, float y);
	float getAngle() const { return body ? body->angle() : 0; }
	void setAngle(float a) {
		if (body) body->setAngle(a);
	}
	float getAngularVelocity() const { return body ? body->angularVelocity() : 0; }
	void setAngularVelocity(float w) {
		if (body) body->setAngularVelocity(w);
	}
	Bounds getBounds() const;
	bool getStatic() const { return body ? body->isStatic : false; }

	bool onLeft() const;
	bool onRight() const;
	std::optional<float> onGround() const;

	// state machine
	bool strictState(const char* s) const { return state.name == s; }
	bool checkState(const char* s) const { return state.name.find(s) != std::string::npos; }
	bool checkLastState(const char* s) const { return state.lastName.find(s) != std::string::npos; }
	void setState(const std::string& name, float delay = 0, std::vector<Callback> callbacks = {});
	float getStateEntropy() const { return state.entropy; }
	bool delayDone() const { return delayTimestamp() == state.delayMax; }
	float delayTimestamp() const { return state.delayMax - state.delay; }
	float delayPercentDone() const;
	float delayPercentNotDone() const { return clamp01(1.0f - delayPercentDone()); }
	void delayStep();
	void setAnimationFrame(const std::vector<Frame>& frames, std::optional<float> timestamp = std::nullopt);

	void updateFire();
	void updateThrown();
	bool liftable() const;              // a fighter can pick it up and throw it
	void setCarried(int holderId);      // -1: back in the world
	void putoutFire();
	void kill();
	void killBody();
	void playSound(const char* name);   // plays sounds[name] if present

	// drawing
	struct DrawOptions {
		std::optional<float> angle;
		std::optional<Vec2> scale;
		std::optional<Vec2> offset;
	};
	void drawSprite(const DrawOptions& o = {});

	static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

	Game& game;
	const ObjDef* parent;
	int id = -1;
	std::string customId;
	bool alive = true;
	int ownerId = -1;             // who fired or threw it
	float blastRadiusMul = 1, blastDamageMul = 1;

	Texture* image = nullptr;
	int frameCountX = 1, frameCountY = 1;
	int frameIndexX = 0, frameIndexY = 0;
	float frameW = 0, frameH = 0;
	int tilingW = 1, tilingH = 1;
	float width = 0, height = 0;

	StateData state;
	float health = -1;
	float fire = 0;
	int carriedBy = -1;                 // lifted overhead by this fighter
	int thrownBy = -1, thrownUntil = 0; // flying from a fighter's hands
	int born = 0;                       // frame it came into being
	bool flammable = false;
	int facingDirection = 1;
	std::map<std::string, float> damageModifier;

	Body* body = nullptr;
	Vec2 prevVelocity{0, 0};      // before the last physics step: contacts have already been resolved
	float restY = 0;              // height it came to rest at
	bool atRest = false;
	int noRestUntil = 0;          // sank while "resting": it is on its way down after all
	std::vector<CollisionRef> collisions;
	float gravityScale = 1;
	bool disableGravity = false;
	bool disableDrawing = false;
	bool noBody = false;

	const std::map<std::string, SoundList>* sounds;

protected:
	static const std::map<std::string, SoundList>& noSounds();
};

class Ladder : public Obj {
public:
	using Obj::Obj;
	ObjType type() const override { return ObjType::Ladder; }
};

class Platform : public Obj {
public:
	using Obj::Obj;
	ObjType type() const override { return ObjType::Platform; }
};

// Emergency exit: running into it moves the player to the linked door (another floor)
class Door : public Obj {
public:
	Door(Game& g, const ObjDef& p, const ObjOptions& o, int id) : Obj(g, p, o, id), link(o.link.value_or("")) {}
	ObjType type() const override { return ObjType::Door; }
	int into();         // direction that walks into the door: towards its wall (0 = either way)
	std::string link;   // customId of the other door

private:
	int intoDir = -2;   // -2 = not worked out yet
};

// What is left of a fighter: falls down and stays for the rest of the round
class Corpse : public Obj {
public:
	Corpse(Game& g, const ObjDef& p, const ObjOptions& o, int id);
	ObjType type() const override { return ObjType::Corpse; }
	void update() override;
	void draw() override;
	std::map<std::string, std::string> profile;
	bool charred = false;
	int born = 0;
};

class Effect : public Obj {
public:
	Effect(Game& g, const ObjDef& p, const ObjOptions& o, int id);
	ObjType type() const override { return ObjType::Effect; }
	void draw() override;
	std::vector<Frame> animation;
	bool animateRealTime = false, fade = false;
};

class Fire : public Obj {
public:
	using Obj::Obj;
	ObjType type() const override { return ObjType::Fire; }
	void update() override;
	void draw() override;
};

// On a chain from the ceiling; swings when bumped, comes down when the chain goes
class Hanging : public Obj {
public:
	using Obj::Obj;
	bool dealDamage(const DamageSpec& damage) override;
	void draw() override;
	void cut();
	int ropeId = -1;     // the distance marker holding it
	Vec2 anchor{0, 0};
	int hits = 0;
};

// Moves whatever stands on it
class Conveyor : public Obj {
public:
	Conveyor(Game& g, const ObjDef& p, const ObjOptions& o, int id) : Obj(g, p, o, id), speed(o.speed.value_or(0)) {}
	ObjType type() const override { return ObjType::Conveyor; }
	void update() override;
	void draw() override;
	float speed;
};

// Lies on the floor and goes off when someone steps near
class Mine : public Obj {
public:
	using Obj::Obj;
	void update() override;
	int armed = 0;
};

class Marker : public Obj {
public:
	Marker(Game& g, const ObjDef& p, const ObjOptions& o, int id);
	~Marker() override;
	ObjType type() const override { return ObjType::Marker; }
	void update() override;
	void cut();          // the joint goes, the targets are free
	int targetAId = -1, targetBId = -1;
	bool hasTargets = false;     // distance_marker carries targets
	b2Joint* joint = nullptr;
};

class Projectile : public Obj {
public:
	Projectile(Game& g, const ObjDef& p, const ObjOptions& o, int id);
	ObjType type() const override { return ObjType::Projectile; }
	void update() override;
	void draw() override;
	void addCollision(Obj& source, float angle) override;
	float damage = 0, knockback = 0;
	int team = 0;
	int pierce = 0, lastPierced = -1;
	float rideMul = 1;
	bool directExplode = false;

private:
	void carry();                  // rocket with a fighter on it: wanders about, then explodes
	bool clearAhead(float heading, float dist);
	int carrying = -1;             // the fighter stuck to this rocket
	float heading = 0, turn = 0;
	int nextTurn = 0;
};

class Melee : public Obj {
public:
	Melee(Game& g, const ObjDef& p, const ObjOptions& o, int id);
	ObjType type() const override { return ObjType::Melee; }
	void pickup(Obj& equiper) override;
	void update() override;
	void swing(int combo);
	int hands = 0;
	int holderId = -1;
	int thrownBy = -1, thrownFrames = 0;   // thrown weapon still flying
	int uses = 0;                          // improvised weapons wear out
	float thrownMul = 1;

	// transient animation offset used by the player's draw routine
	struct Offset {
		float x = 0, y = 0, angle = 0;
		bool flip = false, onTop = false;
	} offset;
	bool hasOffset = false;
};

class Gun : public Obj {
public:
	Gun(Game& g, const ObjDef& p, const ObjOptions& o, int id);
	ObjType type() const override { return ObjType::Gun; }
	void pickup(Obj& equiper) override;
	float pullout();
	float shoot();
	void delayCallback(const std::string& action) override;
	float getSpread();
	void update() override;
	float ammo = 0;
	int holderId = -1;
	int thrownBy = -1, thrownFrames = 0;   // thrown empty gun still flying
};

class Throwable : public Obj {
public:
	Throwable(Game& g, const ObjDef& p, const ObjOptions& o, int id);
	ObjType type() const override { return ObjType::Throwable; }
	void pickup(Obj& equiper) override;
	float pullout();
	void update() override;
	void cook();
	float throwIt();
	float ammo = 0;
	int holderId = -1, projectileId = -1;
};

enum Inventory { INV_MELEE = 0, INV_GUN = 1, INV_THROWABLE = 2, INV_POWERUP = 3 };

class Player : public Obj {
public:
	Player(Game& g, const ObjDef& p, const ObjOptions& o, int id);
	ObjType type() const override { return ObjType::Player; }
	void update() override;
	void draw() override;
	bool dealDamage(const DamageSpec& damage) override;
	void delayCallback(const std::string& action) override;
	void addCollision(Obj& source, float angle) override;

	// Pushed by a hit or a recoil. `launch`: a strong push knocks the fighter off their feet
	void knockback(Vec2 v, bool launch);
	void grabbedByRocket(int rocketId);
	void stun(int frames);

	// perks and the active skill
	void setLoadout(const std::vector<std::string>& perks, const std::string& skillId);
	float mod(const char* key, float def) const;                                  // fighter perks
	float weaponMod(const std::string& weapon, const char* key, float def) const;  // perks of that weapon's group
	void credit(const char* stat);        // the local player's deeds pay tokens
	bool skillReady() const;
	float skillCharge() const;            // 0..1
	bool countering() const;
	bool raging() const;
	const struct Skill* skill = nullptr;
	int lastHitBy = -1;                   // who gets the kill
	int hiddenUntil = 0;                  // smoke bomb
	void landedHit(Player& target, DamageSpec& damage);   // melee hit connected: counts towards the combo
	bool disarm(int dir);                                 // weapon flies out of the hands
	void dropWeapons();                                   // everything falls to the floor
	float holdFraction() const;                           // grab: time left to throw, 1..0

	int heldBy = -1, holding = -1;   // grab: the fighter holding this one / what this one holds (a fighter or an object)
	int tackledBy = -1;              // swept along by this fighter's dive

	float getCrosshairAngle() const;
	Vec2 getCrosshairPosition() const;
	Vec2 getThrowablePosition() const;

	PlayerInput input;
	int team = 0;
	int inventory[4] = {-1, -1, -1, -1};
	int equiped = INV_GUN;
	float crosshairAngle = 0;     // degrees
	float walkingSpeed = 1;
	bool isBot = false;
	std::map<std::string, std::string> profile;

private:
	struct Button {
		bool pressed, released, held;
	};
	Button getButtonStatus(int key);
	bool lastButtons[16] = {};
	int lastRun = -1000, lastCrouch = -1000;
	int doorLock = -1;            // door just came out of: inactive until stepped away from it
	int mirrorDir = 0;            // after a door: this held direction walks the other way until released
	int rocketId = -1;            // rocket carrying this fighter
	int airborneUntil = 0;        // just launched: still touching the floor is not a landing
	float shove = 0;              // push being worked off along the ground
	int grabUntil = 0;            // holding someone: they break free at this frame
	int thrownBy2 = -1, flyUntil = 0;   // thrown by a fighter: flying into others hurts
	bool dazeOnLanding = false;   // combo finisher: stays down for a while
	int comboTarget = -1, comboHits = 0, comboFrame = -1000;
	int grabTotal = 72;
	std::map<std::string, float> mods;
	std::map<std::string, std::map<std::string, float>> weaponMods;
	int skillReadyAt = 0, dashUntil = 0, counterUntil = 0, rageUntil = 0;
	int airAttack = 0, airAttackUntil = 0;   // jump punch / kick / dive: reaches for a few frames
	bool fireQueued = false;      // fire pressed: shoot as soon as the gun is out
	bool grenadeQueued = false;   // grenade pressed: cook (or throw) as soon as it is out
	float fallPeak = 0;           // fastest downward speed since leaving the ground
	int diveStart = -1000, aimWalkFrame = -1000;
	int tackleId = -1, tackleStart = 0;   // carrying someone along in a dive
	bool tackleLanding = false;           // no fall damage until back on the ground
	int acidBurn = 0;                     // frames of burning left after the acid
	void throwGun(Gun* g);

	bool isAiming() const;        // drawing, aiming, shooting, cooking or throwing
	bool sideBlocked(int dir);
	void moveHorizontal(float scale);
	void moveRight(Button b);
	void moveLeft(Button b);
	void moveUp(Button b);
	void moveDown(Button b);
	void jump(Button b);
	void punch(Button b);
	void kick(Button b);
	void weaponActions(Button fire, Button grenade);
	void stickAim();
	void interact(Button b);
	void checkDoors();
	void workOffShove(bool grounded);
	void grabAction(Button b);
	void releaseHeld();
	void throwHeld();
	void releaseHolding();
	void startTackle(Player& victim);
	void endTackle();
	void updateAcid();
	void throwMelee();
	void updateAirAttack();
	void updateFlight();
	void useSkill();
	bool dropWeapon(int slot, Vec2 v);
	void aim(Button b);
	void moveSide(Button b, int dir);
};

// ------------------------------------------------------------------ game

class Bot;

struct PlayerSlot {
	bool local = true;
	std::shared_ptr<Bot> bot;         // computer controlled when set
	int wins = 0;
	int team = 0;                     // 0 = everyone for themselves
	int objectId = -1;
	int localId = 0;
	int netId = -1;
	PlayerInput input;
	std::map<std::string, std::string> profile;
};

struct GameOptions {
	std::string mapText;
	int localPlayers = 1;
	int bots = 0;
	int botDifficulty = 1;   // 0 easy, 1 normal, 2 hard
	int mode = 0;            // see GameMode
	bool suddenDeath = true;
	std::string mapName;     // for the daily events
	bool rewards = false;    // rounds pay tokens to the local player
	bool range = false;      // shooting range: no bots, no rounds
	bool intro = false;      // every round opens with a countdown, the fighters held back
	std::string rangeWeapon; // starts with this in the hands
	bool startPistols = false;   // everybody starts with a pistol instead of empty hands
};

enum GameMode { MODE_FFA = 0, MODE_TEAMS = 1, MODE_WAVES = 2 };

constexpr int INTRO_FRAMES = 180, FIGHT_BANNER_FRAMES = 45;

class Game {
public:
	explicit Game(const GameOptions& options);
	~Game();

	void restartGame();
	void loadMap(const std::string& text);
	void update();
	void draw();

	Obj* createObject(const ObjDef* def, const ObjOptions& opts = {});
	Obj* createObject(const char* key, const ObjOptions& opts = {});
	void removeObject(Obj* o);   // used by Obj::kill

	Obj* getObjectById(int id) const;
	std::shared_ptr<Obj> getShared(int id) const;
	Obj* getObjectByBody(const Body* b) const { return b ? getObjectById(b->clientId) : nullptr; }
	std::vector<Obj*> getObjectsByParent(const ObjDef* parent) const;
	std::vector<Obj*> getObjectsByAABB(const Bounds& bounds) const;
	std::vector<Obj*> getPlayers() const;
	const std::vector<int>& getDoors() const { return doors; }

	void createExplosion(Vec2 pos, float radius, float forceMultiplier, float damageMul = 1, int ownerId = -1);
	Vec2 cameraTopLeft() const;
	std::vector<Vec2> cameraSubjects() const;
	void createFireplosion(Vec2 pos, float radius, int count);
	// a bodiless sprite that drifts and fades: fire smoke and flames, far too many for real objects
	void puff(const char* key, Vec2 pos, Vec2 vel, float life = -1);
	void smashGlass(const Obj& flying);     // what flies this fast goes through a window pane
	bool inAcid(Vec2 p) const { return acid && p.x > acid->x0 && p.x < acid->x1 && p.y > acid->y; }
	static constexpr int MAX_FIRES = 20;
	int particleCount() const { return (int)particles.size(); }
	int fireCount = 0;
	// returns how many loose things were hit; `push` knocks fighters back
	int createHitbox(Obj* source, const std::vector<Rectangle>& hitboxes, Vec2 force, const DamageSpec& damage, Vec2 push = {0, 0});

	void spawnLoot(Vec2 pos);            // a random weapon pops out
	void addStat(const char* stat) { stats[stat]++; }
	void screenFlash(int frames) { flashFrames = frames; }
	void hitStop(int frames) { hitStopFrames = std::max(hitStopFrames, frames); }
	void shake(int frames, float strength) {
		shakeFrames = frames;
		shakeStrength = strength;
	}

	float random();   // deterministic table-based RNG of the original

	PhysicsWorld& physics() { return *world; }
	const std::vector<std::shared_ptr<Obj>>& allObjects() const { return objects; }
	Vec2 worldToScreen(Vec2 p) const {
		Vec2 tl = cameraTopLeft();
		return {(p.x - tl.x) * camera.zoom, (p.y - tl.y) * camera.zoom};
	}
	int lastWinner = -1;   // slot index of the last round's winner, -1 = nobody
	int mode = MODE_FFA;
	int wave = 0;          // waves mode
	bool range = false;
	int introFrames = 0;     // the countdown before a round
	int fightBanner = 0;     // FIGHT! after it
	bool exitRequested = false;           // walked through the door of the range
	int rewardTokens = 0;                 // paid at the end of the last round
	bool suddenDeathOn = true, suddenDeath = false;   // bombs start falling in a long round

	int frameCounter = 0;
	int seed = 0;
	int lastWeaponDrop = 0;
	int gameOver = 0;
	Vec2 gravity{0.0f, 0.001f};
	std::vector<PlayerSlot> players;
	bool paused = false;

	struct Camera {
		float x = 0, y = 0, zoom = 1;
	} camera;

	// Pictures drawn behind (or in front of) the objects: original level art, sky...
	struct MapLayer {
		Texture* tex = nullptr;
		float x = 0, y = 0;       // world position of the top-left corner
		float parallax = 1;       // 1 = moves with the world, 0 = fixed to the screen
		bool front = false;
	};
	std::vector<MapLayer> layers;
	std::optional<Bounds> levelBounds;   // camera stays inside; whole level shown when it fits
	uint8_t clearColor[3] = {0, 0, 0};
	std::optional<float> deathY;          // anything falling below this line dies (pits)
	struct AcidPool { float x0, x1, y; };
	std::optional<AcidPool> acid;         // burns fighters, dissolves everything else
	float minZoom = 1.5f;                 // keep fighters readable on the Vita screen
	NavGraph nav;                         // bot navigation, rebuilt with the map

private:
	void handlePlayerInput();
	void handleWeaponDrops();
	void fillWeaponSpawns();
	void dissolveInAcid();
	void updateParticles();
	void drawParticles();
	struct Particle {
		const ObjDef* def;
		float x, y, vx, vy;
		int life, maxLife;
	};
	std::vector<Particle> particles;
	void handleGameOver();
	void collisionUpdate();
	void updateCamera(bool snap);
	void createPlayers();
	void handleSuddenDeath();
	void handleRange();
	void endOfRound();
	void applyLoadout(Player* p, const PlayerSlot& slot);
	void handleWaves();
	void spawnWave();
	PlayerSlot makeBotSlot(int number, int difficulty);
	float ceilingAbove(Vec2 p);
	std::vector<std::string> weaponKeys();
	void shuffle(std::vector<Obj*>& v);

	std::string mapText_;
	int localPlayers_ = 1;
	std::unique_ptr<PhysicsWorld> world;
	std::vector<std::shared_ptr<Obj>> objects;
	std::unordered_map<int, std::shared_ptr<Obj>> byId;
	std::vector<std::shared_ptr<Obj>> graveyard;   // killed objects, freed at the start of the next update
	std::vector<int> doors;
	int nextId = 0;
	bool firstCamera = true;
	int hitStopFrames = 0, slowFrames = 0, slowTick = 0;   // frozen / slow-motion game time
	int shakeFrames = 0;
	float shakeStrength = 0;
	int flashFrames = 0;
	int lastSupply = 0, waveDelay = 0;
	int botCount_ = 0, botDifficulty_ = 1;
	std::string mapName_, rangeWeapon_;
	bool startPistols_ = false, intro_ = false;
	bool rewards_ = false;
	std::map<std::string, int> stats;
	struct TargetSpot {
		Vec2 pos, rail;      // a moving target shuttles between pos and pos + rail
		int period = 0;      // frames there and back, 0: stands still
		int objectId = -1;
	};
	std::vector<TargetSpot> targetSpots;
	Vec2 targetPosition(const TargetSpot& spot) const;
	friend class Obj;
};

// shared sound sets used by the game itself
struct GameSounds {
	SoundList explosion, fireplosion;
	Sound* ambient = nullptr;
	SoundList damageMelee;   // default hit sound
	static GameSounds& get();
};
