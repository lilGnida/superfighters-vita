// BaseObject and the simple object classes (port of base_object.js, effect.js, fire.js,
// marker.js, projectile.js, melee.js, gun.js, throwable.js).
#include <algorithm>
#include <cmath>

#include "game.h"
#include "progress.h"

namespace {

// Matter.Vector.getAngle patched by sf.js: 0..360 degrees, y axis flipped (screen down = 270)
float gameAngle(Vec2 v) {
	float deg = std::atan2(std::fabs(v.y), std::fabs(v.x)) * 180.0f / PI_F;
	if (v.x < 0) return v.y > 0 ? 180 + deg : 180 - deg;
	return v.y > 0 ? 360 - deg : deg;
}

}  // namespace

GameSounds& GameSounds::get() {
	static GameSounds g;
	static bool init = false;
	if (!init) {
		init = true;
		for (const char* n : {"explosion00", "explosion01", "explosion02", "explosion03"})
			g.explosion.push_back(platform::sound(std::string("sounds/") + n + ".mp3"));
		g.fireplosion.push_back(platform::sound("sounds/fireplosion.mp3"));
		g.ambient = platform::sound("sounds/ambient_loop_1.ogg", 0.05f, true);
		g.damageMelee.push_back(platform::sound("sounds/hit_generic.mp3"));
	}
	return g;
}

const char* typeName(ObjType t) {
	switch (t) {
		case ObjType::Player: return "Player";
		case ObjType::Gun: return "Gun";
		case ObjType::Melee: return "Melee";
		case ObjType::Throwable: return "Throwable";
		case ObjType::Projectile: return "Projectile";
		case ObjType::Fire: return "Fire";
		case ObjType::Effect: return "Effect";
		case ObjType::Marker: return "Marker";
		case ObjType::Ladder: return "Ladder";
		case ObjType::Platform: return "Platform";
		default: return "BaseObject";
	}
}

// ------------------------------------------------------------------ options from map JSON

ObjOptions ObjOptions::fromJson(const json& j) {
	ObjOptions o;
	auto has = [&](const char* k) { return j.contains(k) && !j[k].is_null(); };

	if (has("id")) o.id = j["id"].get<int>();
	if (has("customId")) o.customId = j["customId"].get<std::string>();
	if (has("link")) o.link = j["link"].get<std::string>();
	if (has("health")) o.health = j["health"].get<float>();
	if (has("fire")) o.fire = j["fire"].get<float>();
	if (has("facingDirection")) o.facingDirection = j["facingDirection"].get<int>();
	if (has("frameIndex")) {
		o.frameIndexX = j["frameIndex"].value("x", 0);
		o.frameIndexY = j["frameIndex"].value("y", 0);
	}
	if (has("tiling")) {
		o.tilingW = j["tiling"].value("width", 1);
		o.tilingH = j["tiling"].value("height", 1);
	}
	if (has("matter")) {
		const json& m = j["matter"];
		if (m.contains("position")) o.position = Vec2{m["position"].value("x", 0.f), m["position"].value("y", 0.f)};
		if (m.contains("velocity")) o.velocity = Vec2{m["velocity"].value("x", 0.f), m["velocity"].value("y", 0.f)};
		if (m.contains("angle")) o.angle = m["angle"].get<float>();
		if (m.contains("angularVelocity")) o.angularVelocity = m["angularVelocity"].get<float>();
		if (m.contains("isStatic")) o.isStatic = m["isStatic"].get<bool>();
	}
	if (has("disableGravity")) o.disableGravity = j["disableGravity"].get<bool>();
	if (has("size")) o.size = Vec2{j["size"].value("w", 1.f), j["size"].value("h", 1.f)};
	if (has("verts"))
		for (auto& v : j["verts"]) o.verts.push_back(Vec2{v[0].get<float>(), v[1].get<float>()});
	if (has("hidden")) o.hidden = j["hidden"].get<bool>();
	if (has("ammo")) o.ammo = j["ammo"].get<float>();
	if (has("targetAId")) o.targetAId = j["targetAId"].get<int>();
	if (has("speed")) o.speed = j["speed"].get<float>();
	if (has("targetBId")) o.targetBId = j["targetBId"].get<int>();
	if (has("holderId")) o.holderId = j["holderId"].get<int>();
	if (has("damage") && j["damage"].is_number()) o.damage = j["damage"].get<float>();
	return o;
}

// ------------------------------------------------------------------ Obj

const std::map<std::string, SoundList>& Obj::noSounds() {
	static std::map<std::string, SoundList> s;
	return s;
}

Obj::Obj(Game& g, const ObjDef& p, const ObjOptions& o, int id_, const BodyOverrides& ov) : game(g), parent(&p), id(id_) {
	image = p.image;
	frameCountX = std::max(1, p.frameCountX);
	frameCountY = std::max(1, p.frameCountY);
	frameIndexX = o.frameIndexX.value_or(p.frameIndexX);
	frameIndexY = o.frameIndexY.value_or(p.frameIndexY);
	frameW = (float)image->w / frameCountX;
	frameH = (float)image->h / frameCountY;
	tilingW = o.tilingW.value_or(1);
	tilingH = o.tilingH.value_or(1);

	width = ov.width.value_or(frameW * tilingW);
	height = ov.height.value_or(frameH * tilingH);
	if (o.size) {
		width = o.size->x;
		height = o.size->y;
	}
	disableDrawing = o.hidden;

	sounds = &p.sounds;
	customId = o.customId.value_or("");
	facingDirection = o.facingDirection.value_or(1);

	// `(options.health) ? options.health : -1`: a health of 0 means "no health"
	float h = o.health.value_or(p.health);
	health = h != 0 ? h : -1;
	fire = o.fire.value_or(0);
	flammable = p.flammable;

	damageModifier = {{"default", 1}, {"collision", 1}, {"melee", 1}, {"projectile", 1}, {"explosion", 1}, {"fire", 1}};
	for (auto& kv : p.damageModifier) damageModifier[kv.first] = kv.second;

	if (o.state) state = *o.state;

	ownerId = o.ownerId.value_or(-1);
	blastRadiusMul = o.blastRadiusMul.value_or(1);
	blastDamageMul = o.blastDamageMul.value_or(1);

	// Shortcut for objects to self destruct
	std::optional<float> life = o.lifeTime ? o.lifeTime : p.lifeTime;
	if (life && state.name == "none")
		setState("__life_span__", *life, {{*life, "__kill_this__"}});

	// ---- physics
	noBody = false;
	if (!noBody) {
		BodySpec spec;
		spec.position = o.position.value_or(Vec2{0, 0});
		if (ov.verts.size() >= 3) {
			spec.shape = ShapeType::Poly;
			spec.verts = ov.verts;
		} else if (o.verts.size() >= 3) {
			spec.shape = ShapeType::Poly;
			spec.verts = o.verts;
		} else if (p.shape == "circle") {
			spec.shape = ShapeType::Circle;
			spec.radius = height / 2;
		} else if (p.shape == "tl-br") {
			spec.shape = ShapeType::Poly;
			spec.verts = {{frameW, frameH / 2}, {frameW, frameH}, {0, frameH / 2}, {0, 0}};
		} else if (p.shape == "tr-bl") {
			spec.shape = ShapeType::Poly;
			spec.verts = {{frameW, 0}, {frameW, frameH / 2}, {0, frameH}, {0, frameH / 2}};
		} else {
			spec.shape = ShapeType::Rect;
			spec.width = width;
			spec.height = height;
		}
		spec.isStatic = o.isStatic.value_or(p.isStatic);
		spec.isSensor = ov.sensor || p.isSensor;
		spec.inertiaInfinite = ov.inertiaInfinite || p.inertiaInfinite;
		spec.velocity = o.velocity.value_or(Vec2{0, 0});
		spec.angle = o.angle.value_or(0);
		spec.angularVelocity = o.angularVelocity.value_or(0);

		spec.friction = p.friction.value_or(0.1f);
		spec.restitution = p.restitution.value_or(0.0f);
		spec.frictionAir = p.frictionAir.value_or(0.01f);
		if (ov.friction) spec.friction = *ov.friction;
		// Object.assign(options.matter, options.material)
		if (p.matFriction >= 0) spec.friction = p.matFriction;
		if (p.matRestitution >= 0) spec.restitution = p.matRestitution;

		if (p.hasFilter) spec.filter = p.filter;
		if (o.group) spec.filter = Registry::instance().groups[*o.group];

		body = g.physics().createBody(spec, id);

		gravityScale = p.gravityScale;
		disableGravity = o.disableGravity.value_or(p.disableGravity) || ov.disableGravity;
	}
}

Obj::~Obj() = default;

void Obj::movePosition(float x, float y) {
	if (body) body->setPosition({body->position().x + x, body->position().y + y});
}
void Obj::setPosition(float x, float y) {
	if (body) body->setPosition({x, y});
}
void Obj::setVelocity(float x, float y) {
	if (body) body->setVelocity({x, y});
}
void Obj::moveVelocity(float x, float y) {
	if (body) body->setVelocity({body->velocity().x + x, body->velocity().y + y});
}

Bounds Obj::getBounds() const {
	if (body) return body->bounds();
	return {{0, 0}, {0, 0}};
}

bool Obj::onLeft() const {
	for (auto& c : collisions)
		if (angleIsLeft(c.angle)) return true;
	return false;
}

bool Obj::onRight() const {
	for (auto& c : collisions)
		if (angleIsRight(c.angle)) return true;
	return false;
}

std::optional<float> Obj::onGround() const {
	for (auto& c : collisions)
		if (angleIsGround(c.angle)) return c.angle;
	return std::nullopt;
}

// ---- state machine

void Obj::setState(const std::string& name, float delay, std::vector<Callback> callbacks) {
	state.lastName = state.name;
	state.name = name;
	state.entropy = game.random();
	state.delay = delay;
	state.delayMax = state.delay;
	state.callbacks = std::move(callbacks);
	state.serial++;
}

float Obj::delayPercentDone() const {
	float percent = delayTimestamp() / state.delayMax;
	if (std::isnan(percent)) return 1.0f;   // 0/0 or inf/inf: treat as finished
	return clamp01(percent);
}

void Obj::delayStep() {
	if (state.delay > 0) {
		state.delay -= 1;
		if (state.delay < 0) state.delay = 0;
	}

	if (!state.callbacks.empty()) {
		float timestamp = delayTimestamp();
		std::vector<Callback> callbacks = state.callbacks;   // like the JS local reference
		int serial = state.serial;

		for (size_t i = 0; i < callbacks.size(); i++) {
			// Once fulfilled run and delete the callback
			// (like the original, no step back after the splice: the next entry is skipped)
			if (timestamp >= callbacks[i].delay) {
				std::string action = callbacks[i].action;
				delayCallback(action);
				callbacks.erase(callbacks.begin() + i);
			}
		}
		// if an action replaced the state, its callbacks are the new state's, not ours
		if (state.serial == serial) state.callbacks = callbacks;
	}
}

void Obj::delayCallback(const std::string& action) {
	if (action == "__kill_this__") kill();
}

void Obj::setAnimationFrame(const std::vector<Frame>& frames, std::optional<float> timestamp) {
	float loop = 0;
	for (auto& f : frames) loop += f.delay;
	if (loop <= 0) return;

	float t = timestamp ? *timestamp : (float)game.frameCounter;
	float time = std::isnan(t) ? 0 : std::fmod(t, loop);

	float accum = 0;
	for (auto& f : frames) {
		if (accum <= time && time < accum + f.delay) {
			frameIndexX = f.x;
			frameIndexY = f.y;
		}
		accum += f.delay;
	}
}

// ---- update / fire / damage

void Obj::update() {
	delayStep();
	if (carriedBy >= 0) {
		if (game.getObjectById(carriedBy)) return;
		setCarried(-1);
	}
	updateFire();
	if (game.frameCounter < thrownUntil) updateThrown();
	if (!alive) return;

	if (body) {
		// Loose things lying flat stay put: gravity would press them into the floor every frame
		// and the solver would bounce them straight back out
		bool resting = false;
		if (!getStatic() && !body->inertiaInfinite) {
			Vec2 v = getVelocity();
			float tilt = std::fabs(std::remainder(getAngle(), PI_F / 2));
			bool flat = false;
			for (auto& c : collisions) flat = flat || (c.angle >= 255 && c.angle <= 285);
			resting = flat && tilt < 0.04f && v.length() < 0.4f && std::fabs(getAngularVelocity()) < 0.06f &&
					  game.frameCounter >= noRestUntil;
			if (resting && !atRest) restY = getPosition().y;
			if (resting && getPosition().y - restY > 0.5f) {
				resting = false;
				noRestUntil = game.frameCounter + 30;
			}
			atRest = resting;
			if (resting) {
				setVelocity(0, 0);
				setAngularVelocity(0);
			}
		}

		// Apply gravity
		if (!resting && !disableGravity && !getStatic() && !(body->inertiaInfinite && onGround())) {
			Vec2 g = game.gravity;
			float m = body->mass;
			body->applyForce(body->position(), {g.x * m * gravityScale, g.y * m * gravityScale});
		}
	}
}

void Obj::updateFire() {
	if (!flammable) return;

	if (fire < 0) fire = 0;
	if (fire == 0 && game.fireCount == 0) return;

	bool touchingFire = false;
	if (game.fireCount > 0)
		for (Obj* obj : game.getObjectsByAABB(getBounds())) {
			if (obj->type() == ObjType::Fire && !touchingFire) {
				fire += 1;
				touchingFire = true;
			}
		}

	if (fire > 0) {
		if (!touchingFire) fire += 1;

		if (((int)fire) % 10 == 0) game.puff("smoke", getPosition(), {game.random() - 0.5f, -1});

		if (fire > 100) {
			dealDamage({1, "fire"});
			game.puff("burn", getPosition(), getVelocity());

			// burning things set their surroundings alight
			if (alive && (game.frameCounter + id) % 50 == 0 && type() != ObjType::Player)
				game.createObject("fire", ObjOptions().at(getPosition()).vel({game.random() * 3 - 1.5f, -1.5f}));
		}
	}
}

// Thrown things knock down the fighter they reach and take the blow themselves
void Obj::updateThrown() {
	game.smashGlass(*this);
	float was = prevVelocity.length();
	float size = std::max(0.6f, std::min(2.0f, width * height / 256.0f));
	Player* thrower = static_cast<Player*>(game.getObjectById(thrownBy));
	Bounds b = getBounds();
	b.min = b.min - Vec2{2, 2};
	b.max = b.max + Vec2{2, 2};
	for (Obj* o : game.getObjectsByAABB(b)) {
		if (o->type() != ObjType::Player || o->id == thrownBy || !o->alive) continue;
		Player* t = static_cast<Player*>(o);
		if (t->heldBy >= 0 || t->tackledBy >= 0 || (thrower && thrower->team > 0 && t->team == thrower->team)) continue;
		t->lastHitBy = thrownBy;
		t->knockback({(getVelocity().x > 0 ? 1 : -1) * 3.0f, -1.5f}, true);
		t->dealDamage({14 * size, "thrown"});
		game.hitStop(3);
		thrownUntil = 0;
		dealDamage({15, "collision"});
		return;
	}
	if (was > 3 && getVelocity().length() < was * 0.5f) {
		thrownUntil = 0;
		dealDamage({was * 3, "collision"});
	}
}

bool Obj::liftable() const {
	if (!alive || !body || body->isStatic || carriedBy >= 0) return false;
	if (type() == ObjType::Corpse) return true;
	if (type() != ObjType::Base || parent->cls != "BaseObject") return false;
	if (parent->group != "dynamic_active" && parent->group != "dynamic_inactive") return false;
	for (const char* live : {"grenade", "lobbed_grenade", "flashbang", "molotov", "explosive_barrel_debris", "range_target"})
		if (parent->key == live) return false;
	return width <= 36 && height <= 36;
}

void Obj::setCarried(int holderId) {
	carriedBy = holderId;
	if (body) body->setEnabled(holderId < 0);
}

void Obj::putoutFire() {
	// Set from burning to smoking
	if (fire > 100)
		fire = 1;
	else
		fire = 0;
}

void Obj::playSound(const char* name) {
	auto it = sounds->find(name);
	if (it != sounds->end()) platform::playSoundRandom(it->second);
}

bool Obj::dealDamage(const DamageSpec& damage) {
	if (!alive) return false;

	std::string type = damage.type.empty() ? "default" : damage.type;
	std::string sound = damage.sound.empty() ? damage.type : damage.sound;

	auto mod = damageModifier.find(type);
	float hp = damage.hp * (mod != damageModifier.end() ? mod->second : 1.0f);

	// Deal damage if above threshold
	if (health != -1 && hp > damage.threshold) {
		auto specific = sounds->find("damage_" + sound);
		auto typed = sounds->find("damage_" + type);

		if (!sound.empty() && specific != sounds->end())
			platform::playSoundRandom(specific->second);
		else if (typed != sounds->end())
			platform::playSoundRandom(typed->second);
		else if (type == "melee")
			platform::playSoundRandom(GameSounds::get().damageMelee);

		health -= std::floor(hp + 0.5f);

		if (health < 0) {
			playSound("kill");
			kill();
		}
		return true;
	}
	return false;
}

void Obj::addCollision(Obj& source, float angle) {
	if (!body || !source.body) return;

	// Check if collision already exists
	for (auto& c : collisions)
		if (c.objectId == source.id) return;

	collisions.push_back({source.id, angle});

	// All force went back into this
	if (std::isinf(source.body->mass)) return;

	Vec2 forceVector;
	if (std::isinf(body->mass)) {
		// This is an unmoving object, force returns to source
		forceVector = {source.getVelocity().x * source.body->mass, source.getVelocity().y * source.body->mass};
	} else {
		forceVector = {getVelocity().x * body->mass - source.getVelocity().x * source.body->mass,
					   getVelocity().y * body->mass - source.getVelocity().y * source.body->mass};
	}

	float hp = std::floor(6 * std::sqrt(forceVector.x * forceVector.x + forceVector.y * forceVector.y) + 0.5f);

	// Deal hp damage within a threshold of 6 (ie: time to hit freefall)
	DamageSpec d;
	d.hp = hp;
	d.type = "collision";
	d.threshold = 6;
	source.dealDamage(d);
}

void Obj::kill() {
	if (!alive) return;

	game.removeObject(this);

	if (parent->onkill) parent->onkill(game, *this);

	// Remove world body
	killBody();
}

void Obj::killBody() {
	if (body) {
		game.physics().destroyBody(body);
		body = nullptr;
	}

	// Remove collisions to this object
	for (auto& c : collisions)
		if (Obj* other = game.getObjectById(c.objectId))
			other->collisions.erase(std::remove_if(other->collisions.begin(), other->collisions.end(),
												   [&](const CollisionRef& r) { return r.objectId == id; }),
									other->collisions.end());
	collisions.clear();

	playSound("killed");
}

// ---- drawing

void Obj::drawSprite(const DrawOptions& o) {
	if (disableDrawing || !body || !image) return;

	Vec2 pos = getPosition();
	float angle = o.angle ? *o.angle : body->angle();
	Vec2 scale = o.scale ? *o.scale : Vec2{1, 1};
	Vec2 offset = o.offset ? *o.offset : Vec2{-width / 2, -height / 2};

	SpriteDraw d;
	d.tex = image;
	d.sx = (int)(frameIndexX * frameW);
	d.sy = (int)(frameIndexY * frameH);
	d.sw = (int)frameW;
	d.sh = (int)frameH;
	d.ox = pos.x;
	d.oy = pos.y;
	d.angle = angle;
	d.scaleX = facingDirection * scale.x;
	d.scaleY = scale.y;
	d.dw = frameW;
	d.dh = frameH;

	for (int w = 0; w < tilingW; w++) {
		for (int h = 0; h < tilingH; h++) {
			d.offX = offset.x + w * frameW;
			d.offY = offset.y + h * frameH;
			platform::drawSprite(d);
		}
	}
}

// ------------------------------------------------------------------ Effect

Effect::Effect(Game& g, const ObjDef& p, const ObjOptions& o, int id_) : Obj(g, p, o, id_) {
	animation = p.animation;
	animateRealTime = p.animateRealTime;
	fade = p.fade;
}

void Effect::draw() {
	if (!animation.empty()) {
		if (animateRealTime)
			setAnimationFrame(animation);
		else
			setAnimationFrame(animation, delayTimestamp());
	}

	platform::setAlpha(fade ? delayPercentNotDone() : 1.0f);
	drawSprite();
	platform::setAlpha(1.0f);
}

// ------------------------------------------------------------------ Hanging, Conveyor, Mine

bool Hanging::dealDamage(const DamageSpec& damage) {
	bool hurt = Obj::dealDamage(damage);
	if (alive && damage.hp >= 1 && (++hits >= 2 || damage.hp >= 20)) cut();
	return hurt;
}

void Hanging::cut() {
	if (Obj* o = game.getObjectById(ropeId)) static_cast<Marker*>(o)->cut();
	ropeId = -1;
}

void Hanging::draw() {
	if (!body || !image) return;

	if (parent->key == "lamp") {
		static Texture* glow = platform::image("images/lamp_glow.png");
		if (glow) {
			SpriteDraw d;
			d.tex = glow;
			d.sw = d.dw = glow->w;
			d.sh = d.dh = glow->h;
			d.ox = getPosition().x - glow->w / 2.0f;
			d.oy = getPosition().y + 2;
			platform::drawSprite(d);
		}
	}

	if (ropeId >= 0) {
		static Texture* rope = platform::image("images/rope.png");
		Vec2 p = getPosition();
		Vec2 to = p - anchor;
		if (rope) {
			SpriteDraw d;
			d.tex = rope;
			d.sw = d.sh = 1;
			d.dw = to.length();
			d.dh = 1;
			d.ox = anchor.x;
			d.oy = anchor.y;
			d.angle = std::atan2(to.y, to.x);
			platform::drawSprite(d);
		}
	}
	drawSprite();
}

void Conveyor::update() {
	Bounds zone = getBounds();
	zone.min.y -= 14;
	for (Obj* o : game.getObjectsByAABB(zone))
		if (o->body && !o->body->isStatic && o->type() != ObjType::Effect && o->onGround()) o->movePosition(speed, 0);
}

void Conveyor::draw() {
	if (!image) return;
	Bounds b = getBounds();
	const int period = 8;
	int step = (game.frameCounter / 3) % period;
	int shift = speed > 0 ? period - 1 - step : step;
	for (float x = b.min.x; x < b.max.x; x += period) {
		SpriteDraw d;
		d.tex = image;
		d.sx = shift;
		d.sw = d.dw = (int)std::min((float)period, b.max.x - x);
		d.sh = d.dh = 3;
		d.ox = x;
		d.oy = b.min.y;
		platform::drawSprite(d);
	}
}

void Mine::update() {
	Obj::update();
	if (!alive || !onGround()) return;
	if (armed < 40) {
		armed++;
		return;
	}
	Vec2 p = getPosition();
	for (Obj* t : game.getPlayers())
		if ((t->getPosition() - p).length() < 15) {
			kill();
			return;
		}
}

// ------------------------------------------------------------------ Corpse

Corpse::Corpse(Game& g, const ObjDef& p, const ObjOptions& o, int id_)
	: Obj(g, p, o, id_, [] {
		  BodyOverrides ov;
		  ov.width = 12;
		  ov.height = 6;
		  ov.inertiaInfinite = true;
		  return ov;
	  }()),
	  born(g.frameCounter) {}

void Corpse::update() {
	Obj::update();
	if (onGround()) {
		Vec2 v = getVelocity();
		setVelocity(v.x * 0.85f, v.y);
	}
}

void Corpse::draw() {
	if (!body || !image) return;

	// collapses onto the floor: the last frame stays
	int frame = std::min((game.frameCounter - born) / 6, 4);
	frameIndexX = charred ? 5 + frame : 2 + frame;
	frameIndexY = charred ? 5 : 3;
	bool tumbling = !onGround() && getVelocity().length() > 3;

	std::vector<Texture*> drawOrder{image};
	if (!charred) {
		auto& reg = Registry::instance();
		for (auto& [slot, name] : profile) {
			auto cat = reg.apparel.find(slot);
			if (cat == reg.apparel.end()) continue;
			auto item = cat->second.find(name);
			if (item != cat->second.end()) drawOrder.push_back(platform::image(item->second));
		}
	}

	Vec2 pos = getPosition();
	for (Texture* tex : drawOrder) {
		SpriteDraw d;
		d.tex = tex;
		d.sx = (int)(frameIndexX * frameW);
		d.sy = (int)(frameIndexY * frameH);
		d.sw = d.dw = (int)frameW;
		d.sh = d.dh = (int)frameH;
		d.offX = -frameW / 2;
		d.offY = height / 2 - frameH;
		d.ox = pos.x;
		d.oy = pos.y;
		d.scaleX = (float)facingDirection;
		d.angle = tumbling ? facingDirection * (game.frameCounter - born) * 0.35f : 0;
		platform::drawSprite(d);
	}
}

// ------------------------------------------------------------------ Fire

void Fire::update() {
	if ((game.frameCounter + id) % 15 == 0) game.puff("smoke", getPosition(), {game.random() - 0.5f, -1}, 30 * delayPercentNotDone());
	Obj::update();
}

void Fire::draw() {
	platform::setAlpha(delayPercentNotDone());
	DrawOptions o;
	o.angle = 0;
	drawSprite(o);
	platform::setAlpha(1.0f);
}

// ------------------------------------------------------------------ Marker

Marker::Marker(Game& g, const ObjDef& p, const ObjOptions& o, int id_) : Obj(g, p, o, id_) {
	targetAId = o.targetAId.value_or(-1);
	targetBId = o.targetBId.value_or(-1);

	// The browser version draws these editor markers during play; hide them in the game.
	disableDrawing = true;
}

Marker::~Marker() = default;   // joints die together with their bodies

void Marker::update() {
	Obj* targetA = game.getObjectById(targetAId);
	Obj* targetB = game.getObjectById(targetBId);

	// Check if the targets are still valid
	if (!targetA) targetAId = -1;
	if (!targetB) targetBId = -1;

	// Create constraint and add to world on first game update
	if (!joint && targetA && targetB && targetA->body && targetB->body) {
		joint = game.physics().createDistanceJoint(targetA->body, targetB->body);
		if (targetB->parent->cls == "Hanging") {
			Hanging* h = static_cast<Hanging*>(targetB);
			h->ropeId = id;
			h->anchor = targetA->getPosition();
		}
	}
}

void Marker::cut() {
	game.physics().destroyJoint(joint);
	joint = nullptr;
	targetAId = targetBId = -1;
}

// ------------------------------------------------------------------ Projectile

Projectile::Projectile(Game& g, const ObjDef& p, const ObjOptions& o, int id_)
	: Obj(g, p, o, id_, [] {
		  BodyOverrides ov;
		  ov.disableGravity = true;
		  ov.width = 8;
		  ov.height = 1;
		  ov.sensor = true;
		  return ov;
	  }()) {
	damage = o.damage.value_or(0);
	knockback = o.knockback.value_or(0);
	team = o.team.value_or(0);
	pierce = o.pierce;
	rideMul = o.rideMul.value_or(1);
	directExplode = o.directExplode;
}

void Projectile::update() {
	Obj::update();
	frameIndexX = game.frameCounter % frameCountX;
	if (alive && carrying >= 0) carry();
}

bool Projectile::clearAhead(float a, float dist) {
	Vec2 p = getPosition();
	Vec2 to{p.x + std::cos(a) * dist, p.y + std::sin(a) * dist};
	if (game.levelBounds) {
		const Bounds& b = *game.levelBounds;
		if (to.x < b.min.x || to.x > b.max.x || to.y < b.min.y || to.y > b.max.y) return false;
	}
	return !game.physics().raycast(p, to, [&](Body* bd) {
		if (!bd->isStatic) return false;
		Obj* o = game.getObjectByBody(bd);
		return !o || (o->type() == ObjType::Base && o->parent->key != "glass");
	});
}

// The rocket wanders with its passenger, turning away from walls, until its timer runs out
void Projectile::carry() {
	Obj* o = game.getObjectById(carrying);
	if (!o || o->type() != ObjType::Player) {
		kill();
		return;
	}
	if (game.frameCounter >= nextTurn) {
		turn = (game.random() - 0.5f) * 0.5f;
		nextTurn = game.frameCounter + 6 + (int)(game.random() * 14);
	}
	heading += turn;
	if (!clearAhead(heading, 24)) {
		for (int i = 0; i < 12; i++) {
			float a = game.random() * 2 * PI_F;
			if (clearAhead(a, 32)) {
				heading = a;
				break;
			}
		}
	}

	const float speed = 4.5f;
	Vec2 dir{std::cos(heading), std::sin(heading)};
	setVelocity(dir.x * speed, dir.y * speed);
	setAngle(heading);

	Vec2 p = getPosition();
	o->setPosition(p.x - dir.x * 6, p.y - dir.y * 6);
	o->setVelocity(dir.x * speed, dir.y * speed);

	if (game.frameCounter % 3 == 0) game.createObject("smoke", ObjOptions().at(p).vel({game.random() - 0.5f, -0.5f}));
}

void Projectile::draw() {
	if (parent->plain) {
		DrawOptions spin;
		spin.angle = game.frameCounter * 0.6f;
		spin.offset = Vec2{-frameW / 2, -frameH / 2};
		drawSprite(spin);
		return;
	}

	Vec2 v = getVelocity();
	float speed = std::sqrt(v.x * v.x + v.y * v.y);

	DrawOptions o;
	o.offset = Vec2{-frameW / 2, -frameH / 2};
	o.scale = Vec2{speed * 2 / frameW, 0.5f};
	drawSprite(o);
}

void Projectile::addCollision(Obj& source, float /*angle*/) {
	if (!alive || carrying >= 0 || source.id == ownerId) return;
	if (source.type() == ObjType::Player && team > 0 && static_cast<Player&>(source).team == team) return;

	Player* owner = nullptr;
	if (Obj* o = game.getObjectById(ownerId))
		if (o->type() == ObjType::Player) owner = static_cast<Player*>(o);
	if (source.type() == ObjType::Player && owner) static_cast<Player&>(source).lastHitBy = ownerId;

	// A rocket does not go off on a fighter: it takes them for a ride first
	if (source.type() == ObjType::Player && parent->key == "rocket" && !directExplode) {
		Vec2 v = getVelocity();
		heading = std::atan2(v.y, v.x);
		carrying = source.id;
		float ride = (40 + game.random() * 50) * rideMul;
		if (owner) owner->credit("ride");
		setState("carrying", ride, {{ride, "__kill_this__"}});
		static_cast<Player&>(source).grabbedByRocket(id);
		source.dealDamage({10, "projectile"});
		return;
	}

	// a crouching fighter is under the line of fire
	if (source.type() == ObjType::Player && source.checkState("crouching") && getPosition().y < source.getPosition().y) return;

	if (source.id == lastPierced) return;

	Vec2 dir = getVelocity().normalised();
	if (source.type() == ObjType::Player) {
		float push = knockback * (owner && owner->raging() ? owner->skill->amount : 1);
		if (push >= 2) game.hitStop(2);
		static_cast<Player&>(source).knockback(dir * push, true);
	} else if (source.body && !source.body->isStatic) {
		source.moveVelocity(dir.x * knockback * 0.5f, dir.y * knockback * 0.5f);
	}
	source.dealDamage({damage, "projectile"});
	if (parent->stun > 0 && source.type() == ObjType::Player && source.alive) static_cast<Player&>(source).stun((int)parent->stun);
	game.createObject("hit", ObjOptions().at(getPosition()));
	if (pierce > 0 && source.type() == ObjType::Player) {   // on to the next one
		pierce--;
		lastPierced = source.id;
		return;
	}
	kill();
}

// ------------------------------------------------------------------ Melee

Melee::Melee(Game& g, const ObjDef& p, const ObjOptions& o, int id_) : Obj(g, p, o, id_) {
	hands = p.hands;
	holderId = o.holderId.value_or(-1);
}

void Melee::pickup(Obj& equiper) {
	holderId = equiper.id;
	setState("held");   // no more time limit
	killBody();   // remove physical body from world
	playSound("draw");
}

namespace {

// A thrown weapon hurts the first fighter it reaches while it is still fast
void thrownWeaponHit(Obj& self, int& thrownBy, int& frames, float hp, float push) {
	if (thrownBy < 0 || !self.body) return;
	Vec2 v = self.getVelocity();
	if (--frames <= 0 || v.length() < 1.5f) {
		thrownBy = -1;
		return;
	}
	self.game.smashGlass(self);
	for (Obj* o : self.game.getObjectsByAABB(self.getBounds())) {
		if (o->type() != ObjType::Player || o->id == thrownBy || !o->alive) continue;
		static_cast<Player*>(o)->knockback({(v.x > 0 ? 1 : -1) * push, -1.5f}, true);
		static_cast<Player*>(o)->lastHitBy = thrownBy;
		if (Obj* t = self.game.getObjectById(thrownBy)) static_cast<Player*>(t)->credit("thrown_weapon");
		o->dealDamage({hp, "thrown"});
		self.setVelocity(-v.x * 0.3f, -1.5f);
		self.playSound("draw");
		thrownBy = -1;
		break;
	}
}

}  // namespace

void Melee::update() {
	Obj::update();
	thrownWeaponHit(*this, thrownBy, thrownFrames, (parent->thrownDamage > 0 ? parent->thrownDamage : 18) * thrownMul, 2.6f);
}

void Melee::swing(int combo) {
	Obj* holder = game.getObjectById(holderId);
	if (!holder) return;

	Vec2 position = holder->getPosition();
	std::vector<Rectangle> rects;
	if (combo < (int)parent->hitbox.size()) {
		for (auto& hb : parent->hitbox[combo])
			rects.push_back({position.x + hb.x * holder->facingDirection, position.y + hb.y, hb.width, hb.height});
	}

	Vec2 push{holder->facingDirection * parent->knockback, -1.2f};
	if (parent->knockback == 0) push = {0, 0};
	Player* player = holder->type() == ObjType::Player ? static_cast<Player*>(holder) : nullptr;
	DamageSpec blow = parent->damage;
	if (player) blow.hp *= player->weaponMod(parent->key, "meleeDamageMul", 1);
	int hit = game.createHitbox(holder, rects, {0.001f * holder->facingDirection, combo == 2 ? -0.001f : 0.0f}, blow, push);

	// Swing effect
	if (!parent->effect.empty()) {
		ObjOptions eo;
		eo.at(position);
		eo.facingDirection = holder->facingDirection;
		eo.frameIndexX = combo;
		eo.frameIndexY = 0;
		game.createObject(parent->effect.c_str(), eo);
	}

	// Improvised weapons do not last
	if (hit > 0 && parent->durability > 0 && player &&
		++uses >= parent->durability * player->weaponMod(parent->key, "durabilityMul", 1)) {
		player->inventory[INV_MELEE] = -1;
		game.createObject("hit", ObjOptions().at(position));
		kill();
	}
}

// ------------------------------------------------------------------ Gun

Gun::Gun(Game& g, const ObjDef& p, const ObjOptions& o, int id_) : Obj(g, p, o, id_) {
	ammo = o.ammo.value_or(p.ammo);
	holderId = o.holderId.value_or(-1);
}

void Gun::pickup(Obj& equiper) {
	holderId = equiper.id;
	if (equiper.type() == ObjType::Player && ammo >= parent->ammo) {   // a fresh magazine
		float m = static_cast<Player&>(equiper).weaponMod(parent->key, "ammoMul", 1);
		float burst = (float)std::max(1, parent->burst);
		ammo = std::max(burst, std::round(ammo * m / burst) * burst);
	}
	setState("held");   // no more time limit
	killBody();   // remove physical body from world
	playSound("draw");
}

// A thrown (empty) gun knocks down the first other player it hits while still fast
void Gun::update() {
	Obj::update();
	thrownWeaponHit(*this, thrownBy, thrownFrames, 8, 2.2f);
}

float Gun::pullout() {
	setState("picked_up");
	playSound("draw");
	Obj* holder = game.getObjectById(holderId);
	return holder && holder->type() == ObjType::Player ? static_cast<Player*>(holder)->weaponMod(parent->key, "drawFrames", 6) : 6;
}

float Gun::getSpread() {
	float spread = parent->spread;
	Obj* holder = game.getObjectById(holderId);
	if (holder && holder->type() == ObjType::Player) spread *= static_cast<Player*>(holder)->weaponMod(parent->key, "spreadMul", 1);
	return std::floor(game.random() * spread - spread / 2 + 0.5f) * PI_F / 180.0f;
}

void Gun::delayCallback(const std::string& action) {
	Obj::delayCallback(action);

	if (action != "shoot") return;

	Obj* holderObj = game.getObjectById(holderId);
	if (!holderObj || holderObj->type() != ObjType::Player) return;
	Player* holder = static_cast<Player*>(holderObj);

	Vec2 position = holder->getCrosshairPosition();
	float angle = holder->getCrosshairAngle();

	// Create the count of projectiles
	const ObjDef* projectileDef = Registry::instance().get(parent->projectile);
	int count = (int)holder->weaponMod(parent->key, "countSet", (float)parent->count + holder->weaponMod(parent->key, "countAdd", 0));
	for (int j = 0; j < count; j++) {
		float projectileAngle = angle + getSpread();

		if (projectileDef) {
			ObjOptions po;
			po.at(position).rot(projectileAngle).vel({std::cos(projectileAngle) * parent->speed,
													  std::sin(projectileAngle) * parent->speed});
			po.damage = parent->damage.hp * holder->weaponMod(parent->key, "damageMul", 1);
			po.knockback = parent->knockback * holder->weaponMod(parent->key, "knockbackMul", 1);
			po.ownerId = holder->id;
			po.team = holder->team;
			po.pierce = (int)holder->weaponMod(parent->key, "pierce", 0);
			po.rideMul = holder->weaponMod(parent->key, "rideMul", 1);
			po.directExplode = holder->weaponMod(parent->key, "directExplode", 0) > 0;
			po.blastRadiusMul = holder->weaponMod(parent->key, "blastRadiusMul", 1);
			po.blastDamageMul = holder->weaponMod(parent->key, "blastDamageMul", 1);
			game.createObject(projectileDef, po);
		}
	}

	// The shot pushes the shooter back
	float recoil = parent->recoil * holder->weaponMod(parent->key, "recoilMul", 1);
	holder->knockback({-std::cos(angle) * recoil, -std::sin(angle) * recoil}, false);

	// Create muzzle flash
	if (!parent->flash.empty())
		game.createObject(parent->flash.c_str(), ObjOptions()
													 .at({position.x + std::cos(angle) * (frameW / 4),
														  position.y + std::sin(angle) * (frameW / 4)})
													 .rot(angle));

	// Effect, typically the casing / shell of the gun
	if (!parent->expel.empty()) {
		ObjOptions eo;
		eo.at(position).rot(game.random() * PI_F * 2).vel({std::cos(angle) * -3, std::sin(angle) * -3 - 1});
		game.createObject(parent->expel.c_str(), eo);
	}

	playSound("fire");
}

float Gun::shoot() {
	if (ammo > 0) {
		ammo -= parent->burst;

		Obj* holder = game.getObjectById(holderId);
		float timing = parent->timing;
		if (holder && holder->type() == ObjType::Player) timing *= static_cast<Player*>(holder)->weaponMod(parent->key, "timingMul", 1);

		std::vector<Callback> shots;
		float delay = 0;

		// Spread burst fire over the timing delay
		for (int i = 0; i < parent->burst; i++) {
			shots.push_back({delay, "shoot"});
			delay += timing / parent->burst;
		}

		// Play audio once (spare ears)
		playSound("fireOnce");

		setState("firing", timing, shots);
		return timing;
	}

	playSound("empty");
	return 9;
}

// ------------------------------------------------------------------ Throwable

Throwable::Throwable(Game& g, const ObjDef& p, const ObjOptions& o, int id_) : Obj(g, p, o, id_) {
	ammo = o.ammo.value_or(p.ammo);
	holderId = o.holderId.value_or(-1);
	projectileId = -1;
}

void Throwable::pickup(Obj& equiper) {
	holderId = equiper.id;
	setState("held");   // no more time limit
	killBody();
	playSound("draw");
}

float Throwable::pullout() {
	playSound("draw");
	return 6;
}

void Throwable::update() {
	Obj* holder = game.getObjectById(holderId);
	Obj* projectile = game.getObjectById(projectileId);

	if (!projectile) projectileId = -1;

	// Reset cooking projectile to the holders defined position
	if (holder && projectile) {
		// Check if the holder is still preparing the throw
		if (holder->checkState("prepare_throw")) {
			Vec2 p = static_cast<Player*>(holder)->getThrowablePosition();
			projectile->setPosition(p.x, p.y);
			projectile->setVelocity(0, 0);
			projectile->setAngle(0);
			projectile->setAngularVelocity(0);
		} else {
			projectileId = -1;   // no longer need to keep track of
		}
	}
}

void Throwable::cook() {
	// Create projectile item and prepare to throw
	if (ammo > 0) {
		ammo--;
		playSound("draw");

		Obj* holder = game.getObjectById(holderId);
		if (!holder) return;

		ObjOptions po;
		po.at(static_cast<Player*>(holder)->getThrowablePosition());
		Player* player = static_cast<Player*>(holder);
		if (parent->detonationTime > 0) po.life(parent->detonationTime * player->weaponMod(parent->key, "fuseMul", 1));
		po.ownerId = holder->id;
		po.team = player->team;
		po.damage = parent->damage.hp;
		po.blastRadiusMul = player->weaponMod(parent->key, "blastRadiusMul", 1);
		po.blastDamageMul = player->weaponMod(parent->key, "blastDamageMul", 1);

		if (Obj* projectile = game.createObject(parent->projectile.c_str(), po)) projectileId = projectile->id;
	}
}

float Throwable::throwIt() {
	Obj* holderObj = game.getObjectById(holderId);
	Obj* projectile = game.getObjectById(projectileId);

	// Throw to the holders crosshair
	if (holderObj && projectile) {
		Player* holder = static_cast<Player*>(holderObj);
		Vec2 pos = holder->getCrosshairPosition();
		float a = holder->getCrosshairAngle();
		projectile->setPosition(pos.x, pos.y);
		float speed = (parent->speed > 0 ? parent->speed : 5) * holder->weaponMod(parent->key, "throwSpeedMul", 1);
		projectile->setVelocity(std::cos(a) * speed, std::sin(a) * speed);
		projectileId = -1;
	}
	return 9;
}

// helper exported for game.cpp
float objGameAngle(Vec2 v) { return gameAngle(v); }
