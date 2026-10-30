#include <algorithm>
// Loads assets/objects.json (generated from the JS sources by tools/dump_defs.mjs)
// and installs the hand-ported onkill callbacks.
#include "game.h"

Registry& Registry::instance() {
	static Registry r;
	return r;
}

namespace {

float num(const json& j, const char* key, float def) {
	auto it = j.find(key);
	return (it != j.end() && it->is_number()) ? it->get<float>() : def;
}

bool flag(const json& j, const char* key, bool def = false) {
	auto it = j.find(key);
	return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
}

std::string str(const json& j, const char* key) {
	auto it = j.find(key);
	return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

std::string refKey(const json& j, const char* key) {
	auto it = j.find(key);
	if (it == j.end() || !it->is_object()) return "";
	return str(*it, "ref");
}

Sound* soundFrom(const json& s) {
	return platform::sound(s.value("sound", ""), s.value("volume", 1.0f), s.value("loop", false));
}

SoundList soundList(const json& v) {
	SoundList out;
	if (v.is_array()) {
		for (auto& s : v) out.push_back(soundFrom(s));
	} else if (v.is_object()) {
		out.push_back(soundFrom(v));
	}
	return out;
}

CollisionFilter filterFrom(const json& g) {
	CollisionFilter f;
	f.category = g.value("category", 0ull) & 0xFFFFFFFFull;
	f.mask = g.value("mask", 0ull) & 0xFFFFFFFFull;
	f.above = g.value("above", 0ull) & 0xFFFFFFFFull;
	return f;
}

}  // namespace

bool Registry::load(const std::string& text) {
	json root = json::parse(text, nullptr, false);
	if (root.is_discarded()) return false;

	for (auto& [name, g] : root["groups"].items()) groups[name] = filterFrom(g);

	std::map<std::string, std::pair<float, float>> materials;
	for (auto& [name, m] : root["materials"].items())
		materials[name] = {m.value("friction", 0.1f), m.value("restitution", 0.0f)};

	for (auto& [slot, items] : root["apparel"].items())
		for (auto& [name, path] : items.items()) apparel[slot][name] = path.get<std::string>();

	for (auto& key : root["order"]) {
		const json& d = root["objects"][key.get<std::string>()];
		ObjDef def;
		def.key = key.get<std::string>();
		def.cls = str(d, "cls");

		if (d.contains("image") && d["image"].is_object()) def.imagePath = d["image"].value("image", "");
		def.image = platform::image(def.imagePath);

		if (d.contains("frameCount")) {
			def.frameCountX = d["frameCount"].value("x", 1);
			def.frameCountY = d["frameCount"].value("y", 1);
		}
		if (d.contains("frameIndex")) {
			def.frameIndexX = d["frameIndex"].value("x", 0);
			def.frameIndexY = d["frameIndex"].value("y", 0);
		}
		def.health = num(d, "health", -1);
		def.flammable = flag(d, "flammable");
		def.shape = str(d, "shape");
		def.gravityScale = num(d, "gravityScale", 1);
		def.disableGravity = flag(d, "disableGravity");
		if (d.contains("lifeTime") && d["lifeTime"].is_number()) def.lifeTime = d["lifeTime"].get<float>();
		def.fade = flag(d, "fade");
		def.animateRealTime = flag(d, "animateRealTime");

		if (d.contains("matter") && d["matter"].is_object()) {
			const json& m = d["matter"];
			def.isStatic = flag(m, "isStatic");
			def.isSensor = flag(m, "isSensor");
			def.inertiaInfinite = m.contains("inertia");   // Infinity is serialised as null
			if (m.contains("friction")) def.friction = m["friction"].get<float>();
			if (m.contains("restitution")) def.restitution = m["restitution"].get<float>();
			if (m.contains("frictionAir")) def.frictionAir = m["frictionAir"].get<float>();
		}

		if (d.contains("damageModifier"))
			for (auto& [k, v] : d["damageModifier"].items()) def.damageModifier[k] = v.get<float>();

		if (d.contains("sounds"))
			for (auto& [k, v] : d["sounds"].items()) def.sounds[k] = soundList(v);

		if (d.contains("animation"))
			for (auto& f : d["animation"]) def.animation.push_back({f.value("x", 0), f.value("y", 0), f.value("delay", 1)});

		def.ammo = num(d, "ammo", 0);
		def.speed = num(d, "speed", 0);
		def.timing = num(d, "timing", 0);
		def.spread = num(d, "spread", 0);
		def.detonationTime = num(d, "detonationTime", 0);
		def.recoil = num(d, "recoil", 0);
		def.knockback = num(d, "knockback", 0);
		def.thrownDamage = num(d, "thrownDamage", 0);
		def.durability = d.value("durability", 0);
		def.stun = num(d, "stun", 0);
		def.plain = flag(d, "plain");
		def.count = d.value("count", 1);
		def.burst = d.value("burst", 1);
		def.hands = d.value("hands", 0);

		if (d.contains("damage")) {
			const json& dm = d["damage"];
			if (dm.is_number()) {
				def.damage.hp = dm.get<float>();
			} else if (dm.is_object()) {
				def.damage.hp = num(dm, "hp", 0);
				def.damage.type = str(dm, "type");
				def.damage.sound = str(dm, "sound");
			}
		}

		def.projectile = refKey(d, "projectile");
		def.expel = refKey(d, "expel");
		def.flash = refKey(d, "flash");
		def.effect = refKey(d, "effect");

		if (d.contains("hitbox"))
			for (auto& combo : d["hitbox"]) {
				std::vector<Rectangle> rects;
				for (auto& r : combo)
					rects.push_back({r.value("x", 0.f), r.value("y", 0.f), r.value("width", 0.f), r.value("height", 0.f)});
				def.hitbox.push_back(rects);
			}

		def.group = str(d, "group");
		if (!def.group.empty() && groups.count(def.group)) {
			def.filter = groups[def.group];
			def.hasFilter = true;
		}
		def.material = str(d, "material");
		if (!def.material.empty() && materials.count(def.material)) {
			def.matFriction = materials[def.material].first;
			def.matRestitution = materials[def.material].second;
		}

		order.push_back(def.key);
		defs[def.key] = std::move(def);
	}

	installCallbacks(*this);
	return true;
}

// ------------------------------------------------------------------ onkill callbacks
// Ported by hand from the `onkill:` closures in the JS object definitions.

namespace {

void spawnDebris(Game& g, Obj& o, const char* key, float dx, float dy) {
	g.createObject(key, ObjOptions().at({o.getPosition().x + dx, o.getPosition().y + dy}).vel(o.getVelocity()));
}

void explode(Game& g, Obj& o) { g.createExplosion(o.getPosition(), 32 * o.blastRadiusMul, 0.008f, o.blastDamageMul, o.ownerId); }

}  // namespace

void installCallbacks(Registry& r) {
	auto set = [&](const char* key, std::function<void(Game&, Obj&)> fn) {
		auto it = r.defs.find(key);
		if (it != r.defs.end()) it->second.onkill = std::move(fn);
	};

	// a dead fighter stays on the map (those lost to acid or a pit die below the line again)
	set("player", [](Game& g, Obj& o) {
		Player& p = static_cast<Player&>(o);
		Vec2 pos = p.getPosition();
		p.dropWeapons();
		Obj* killer = g.getObjectById(p.lastHitBy);
		if (killer && killer != &o && killer->type() == ObjType::Player) static_cast<Player*>(killer)->credit("kill");
		ObjOptions co;
		co.at({pos.x, pos.y + 6}).vel(p.getVelocity());
		co.facingDirection = p.facingDirection;
		if (Obj* c = g.createObject("corpse", co)) {
			Corpse* corpse = static_cast<Corpse*>(c);
			corpse->profile = p.profile;
			corpse->charred = p.fire > 100;
		}
		for (int i = 0; i < 4; i++)
			g.createObject(i % 2 ? "blood_small" : "blood_large",
						   ObjOptions().at(pos).vel({g.random() * 3 - 1.5f, g.random() * -2}));
	});

	set("crate", [](Game& g, Obj& o) {
		spawnDebris(g, o, "crate_debris02", 0, -5);
		spawnDebris(g, o, "crate_debris01", 0, 0);
		spawnDebris(g, o, "crate_debris00", 0, 5);
		// supply drops always hold something, other crates only now and then
		if (o.customId == "supply" || g.random() < 0.2f) g.spawnLoot(o.getPosition());
	});
	r.defs["crate_hanging"].onkill = r.defs["crate"].onkill;

	set("explosive_barrel", [](Game& g, Obj& o) {
		explode(g, o);
		Vec2 v = o.getVelocity();
		Vec2 velocity{v.x + g.random() * 5 - 2.5f, v.y + -5 + g.random() * 2};
		ObjOptions opts;
		opts.at(o.getPosition()).vel(velocity).rot(o.getAngle());
		opts.angularVelocity = g.random() - 0.5f;
		g.createObject("explosive_barrel_debris", opts);
	});
	set("explosive_barrel_debris", [](Game& g, Obj& o) { explode(g, o); });
	set("grenade", [](Game& g, Obj& o) { explode(g, o); });
	set("rocket", [](Game& g, Obj& o) { explode(g, o); });

	set("lobbed_grenade", [](Game& g, Obj& o) { explode(g, o); });
	set("mine", [](Game& g, Obj& o) { g.createExplosion(o.getPosition(), 36 * o.blastRadiusMul, 0.008f, o.blastDamageMul, o.ownerId); });

	set("flashbang", [](Game& g, Obj& o) {
		Vec2 p = o.getPosition();
		g.createObject("explosion_small", ObjOptions().at(p));
		platform::playSoundRandom(GameSounds::get().fireplosion);
		for (Obj* t : g.getPlayers()) {
			Vec2 q = t->getPosition();
			if ((q - p).length() > 100) continue;
			bool wall = g.physics().raycast(p, q, [&](Body* b) {
				Obj* w = g.getObjectByBody(b);
				return b->isStatic && (!w || w->type() == ObjType::Base);
			});
			if (wall) continue;
			Player* victim = static_cast<Player*>(t);
			victim->stun(90);
			if (!victim->isBot) g.screenFlash(30);
		}
	});

	set("molotov", [](Game& g, Obj& o) { g.createFireplosion(o.getPosition(), 2, 16); });

	set("table", [](Game& g, Obj& o) {
		spawnDebris(g, o, "table_debris00", -5, 0);
		spawnDebris(g, o, "table_debris01", 0, 0);
		spawnDebris(g, o, "table_debris02", 5, 0);
	});
	set("small_table", [](Game& g, Obj& o) { spawnDebris(g, o, "table_debris01", 0, 0); });

	// window pane: a shower of shards
	auto shards = [](Game& g, Obj& o) {
		Vec2 p = o.getPosition();
		int n = std::max(4, (int)(o.height / 4));
		for (int i = 0; i < n; i++) {
			ObjOptions so;
			so.at({p.x + g.random() * 3 - 1.5f, p.y - o.height / 2 + o.height * i / n})
				.vel({g.random() * 4 - 2, g.random() * -2});
			so.angularVelocity = g.random() - 0.5f;
			g.createObject("glass_shard", so);
		}
	};
	set("glass", shards);
	set("lamp", shards);

	auto computer = [](Game& g, Obj& o) { g.createObject("explosion_small", ObjOptions().at(o.getPosition())); };
	set("computer_monitor", computer);
	set("computer_desktop", computer);

	auto targetDebris = [](Game& g, Obj& o) {
		spawnDebris(g, o, "target_debris00", 0, -5);
		spawnDebris(g, o, "target_debris01", 0, 0);
		spawnDebris(g, o, "target_debris02", 0, 5);
	};
	set("target", targetDebris);
	set("range_target", targetDebris);

	set("beachball", [](Game& g, Obj& o) { spawnDebris(g, o, "beachball_debris", 0, 0); });
}
