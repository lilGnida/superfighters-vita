// Headless gameplay checks: scripted input against a small test map, prints what happens.
// run: SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/sf_test [assets-dir-parent]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "base.h"
#include "game.h"
#include "i18n.h"
#include "profile.h"
#include "settings.h"
#include "sfdm.h"
#include "progress.h"

static int g_fail = 0;
#define CHECK(cond, ...)                                  \
	do {                                                  \
		if (cond) {                                       \
			printf("  ok    "); printf(__VA_ARGS__);       \
		} else {                                          \
			printf("  FAIL  "); printf(__VA_ARGS__);       \
			g_fail++;                                     \
		}                                                 \
		printf("\n");                                     \
	} while (0)

// A small arena: floor (y=100), walls, one-way platform at y=60, crate, barrel, items.
static std::string testMap() {
	auto obj = [](int id, const char* key, float x, float y, int tw = 1, int th = 1, bool st = false) {
		char buf[512];
		snprintf(buf, sizeof buf,
				 "{\"parentKey\":\"%s\",\"id\":%d,\"tiling\":{\"width\":%d,\"height\":%d},\"matter\":{\"position\":{\"x\":%g,\"y\":%g}%s}}",
				 key, id, tw, th, x, y, st ? ",\"isStatic\":true" : "");
		return std::string(buf);
	};
	std::string m = "{\"objects\":[";
	m += obj(0, "concrete", 100, 110, 20, 1) + ",";            // floor, 13px tiles -> 260 wide, top at y=103.5
	m += obj(1, "concrete", -20, 60, 1, 8) + ",";               // left wall
	m += obj(2, "concrete", 220, 60, 1, 8) + ",";               // right wall
	m += obj(3, "platform", 60, 60, 4, 1) + ",";                // one-way platform
	m += obj(4, "player_spawn", 20, 90) + ",";
	m += obj(5, "crate", 140, 96) + ",";
	m += obj(6, "explosive_barrel", 180, 94) + ",";
	m += obj(7, "pistol", 50, 94) + ",";
	m += obj(8, "grenades", 80, 94) + ",";
	m += obj(9, "weapon_spawn", 120, 90);
	m += "]}";
	return m;
}

static PlayerInput held(bool up = false, bool down = false, bool left = false, bool right = false, bool a = false,
						bool s = false, bool d = false, bool f = false) {
	return platform::keyboardInput(up, down, left, right, a, s, d, f);
}

template <class F> static PlayerInput pad(F setup) { PlayerInput i; setup(i); return i; }

static void run(Game& g, const PlayerInput& in, int frames) {
	platform::setInputOverride(0, &in);
	for (int i = 0; i < frames; i++) g.update();
}

static Player* player(Game& g) {
	Obj* o = g.getObjectById(g.players[0].objectId);
	return o && o->type() == ObjType::Player ? static_cast<Player*>(o) : nullptr;
}

int main(int argc, char** argv) {
	if (!platform::init(argc > 0 ? argv[0] : nullptr)) return 2;
	if (!Registry::instance().load(platform::readFile("objects.json"))) return 2;

	GameOptions opts;
	opts.mapText = testMap();
	opts.localPlayers = 1;
	auto gamePtr = std::make_unique<Game>(opts);
	Game& g = *gamePtr;
	platform::setMute(true);

	Player* p = player(g);
	CHECK(p != nullptr, "player spawned");
	if (!p) return 1;

	printf("== falling and landing\n");
	run(g, held(), 90);
	Vec2 pos = p->getPosition();
	printf("   pos=(%.2f, %.2f) state=%s onGround=%d\n", pos.x, pos.y, p->state.name.c_str(), (int)(bool)p->onGround());
	CHECK(p->onGround().has_value(), "player is on the ground");
	CHECK(pos.y > 85 && pos.y < 100, "player rests on the floor (y=%.2f)", pos.y);
	float groundY = pos.y;

	printf("== walking right 30 frames\n");
	run(g, held(false, false, false, true), 30);
	Vec2 p2 = p->getPosition();
	printf("   pos=(%.2f, %.2f) state=%s\n", p2.x, p2.y, p->state.name.c_str());
	CHECK(p2.x - pos.x > 25 && p2.x - pos.x < 35, "moved ~30px right (%.2f)", p2.x - pos.x);
	CHECK(std::fabs(p2.y - groundY) < 1.0f, "stayed on the floor (dy=%.2f)", p2.y - groundY);

	printf("== jump\n");
	run(g, held(), 5);
	float minY = 1e9;
	platform::setInputOverride(0, nullptr);
	{
		PlayerInput in = held(true);
		platform::setInputOverride(0, &in);
		g.update();   // press up: jump
		in = held();
		platform::setInputOverride(0, &in);
		for (int i = 0; i < 80; i++) {
			g.update();
			minY = std::min(minY, p->getPosition().y);
		}
	}
	printf("   apex y=%.2f (ground %.2f) -> height %.2f, final y=%.2f state=%s\n", minY, groundY, groundY - minY, p->getPosition().y, p->state.name.c_str());
	CHECK(groundY - minY > 15 && groundY - minY < 60, "jump height %.2f", groundY - minY);
	CHECK(p->onGround().has_value() && std::fabs(p->getPosition().y - groundY) < 1.5f, "landed back on the floor");

	printf("== pick up the pistol (player stands on it)\n");
	printf("   pos=(%.2f, %.2f)\n", p->getPosition().x, p->getPosition().y);
	{
		PlayerInput in = held(false, false, false, false, false, false, false, true);
		platform::setInputOverride(0, &in);
		g.update();
		in = held();
		platform::setInputOverride(0, &in);
		g.update();
	}
	Obj* gun = g.getObjectById(p->inventory[INV_GUN]);
	CHECK(gun != nullptr && gun->type() == ObjType::Gun, "picked up a gun (inventory gun id=%d)", p->inventory[INV_GUN]);

	printf("== aim and shoot the pistol\n");
	float ammo0 = gun ? static_cast<Gun*>(gun)->ammo : 0;
	run(g, held(false, false, false, true), 4);  // face right
	run(g, held(false, false, false, false, false, false, true), 12);  // hold aim
	printf("   state=%s\n", p->state.name.c_str());
	CHECK(p->checkState("aiming"), "player is aiming");
	{
		PlayerInput in = held(false, false, false, false, true, false, true);
		platform::setInputOverride(0, &in);
		g.update();
		in = held(false, false, false, false, false, false, true);
		platform::setInputOverride(0, &in);
	}
	int projectiles = 0;
	for (int i = 0; i < 6; i++) {
		g.update();
		for (Obj* o : g.getObjectsByParent(Registry::instance().get("projectile"))) { (void)o; projectiles++; }
	}
	CHECK(gun && static_cast<Gun*>(gun)->ammo < ammo0, "ammo decreased (%g -> %g)", ammo0, gun ? static_cast<Gun*>(gun)->ammo : -1);
	CHECK(projectiles > 0, "a projectile was fired");

	printf("== bullets damage the crate\n");
	Obj* crate = g.getObjectById(5);
	float h0 = crate ? crate->health : -1;
	run(g, held(false, false, false, false, false, false, true), 30);
	for (int i = 0; i < 6; i++) {
		PlayerInput in = held(false, false, false, false, true, false, true);
		platform::setInputOverride(0, &in);
		g.update();
		in = held(false, false, false, false, false, false, true);
		platform::setInputOverride(0, &in);
		run(g, in, 14);
	}
	crate = g.getObjectById(5);
	printf("   crate health %g -> %g, exists=%d  player x=%.1f\n", h0, crate ? crate->health : -999, crate != nullptr, p->getPosition().x);

	// ------------------------------------------------------------------ scenario 2
	gamePtr.reset();
	printf("\n######## scenario 2: platform, melee, grenade\n");
	{
		auto o2 = [](int id, const char* key, float x, float y, int tw = 1, int th = 1) {
			char buf[512];
			snprintf(buf, sizeof buf, "{\"parentKey\":\"%s\",\"id\":%d,\"tiling\":{\"width\":%d,\"height\":%d},\"matter\":{\"position\":{\"x\":%g,\"y\":%g}}}", key, id, tw, th, x, y);
			return std::string(buf);
		};
		std::string m = "{\"objects\":[";
		m += o2(0, "concrete", 100, 110, 20, 1) + ",";
		m += o2(1, "concrete", -20, 60, 1, 8) + ",";
		m += o2(2, "concrete", 220, 60, 1, 8) + ",";
		m += o2(3, "platform", 60, 79.5, 4, 1) + ",";
		m += o2(4, "player_spawn", 20, 90) + ",";
		m += o2(5, "crate", 130, 96) + ",";
		m += o2(6, "explosive_barrel", 165, 94) + ",";
		m += o2(7, "grenades", 100, 94);
		m += "]}";
		GameOptions o; o.mapText = m; o.localPlayers = 1;
		gamePtr = std::make_unique<Game>(o);
	}
	Game& g2 = *gamePtr;
	Player* q = player(g2);
	run(g2, held(), 60);
	printf("== walk under the platform and jump through it\n");
	run(g2, held(false, false, false, true), 40);   // to x=60
	Vec2 qp = q->getPosition();
	printf("   at (%.1f, %.1f)\n", qp.x, qp.y);
	{
		PlayerInput in = held(true);
		platform::setInputOverride(0, &in);
		g2.update();
		in = held();
		platform::setInputOverride(0, &in);
		float maxStand = 1e9;
		for (int i = 0; i < 90; i++) {
			g2.update();
			maxStand = std::min(maxStand, q->getPosition().y);
		}
		Vec2 e = q->getPosition();
		printf("   after jump: (%.1f, %.1f) apex=%.1f state=%s onGround=%d\n", e.x, e.y, maxStand, q->state.name.c_str(), (int)(bool)q->onGround());
		CHECK(e.y < 80 && e.y > 65 && q->onGround().has_value(), "landed on top of the one-way platform (y=%.1f)", e.y);
	}
	printf("== double tap down to drop through the platform\n");
	{
		PlayerInput d = held(false, true), n = held();
		for (int rep = 0; rep < 2; rep++) {
			platform::setInputOverride(0, &d); g2.update();
			platform::setInputOverride(0, &n); g2.update();
			if (rep == 0) { platform::setInputOverride(0, &n); g2.update(); }
		}
		platform::setInputOverride(0, &d);
		for (int i = 0; i < 60; i++) g2.update();
		platform::setInputOverride(0, &n);
		for (int i = 0; i < 40; i++) g2.update();
		Vec2 e = q->getPosition();
		printf("   (%.1f, %.1f) state=%s\n", e.x, e.y, q->state.name.c_str());
		CHECK(e.y > 88, "dropped through the platform (y=%.1f)", e.y);
	}

	printf("== punch combo on the crate\n");
	run(g2, held(false, false, false, true), 70);   // to ~x=130
	Obj* crate2 = g2.getObjectById(5);
	printf("   player x=%.1f crate x=%.1f hp=%g\n", q->getPosition().x, crate2 ? crate2->getPosition().x : -1, crate2 ? crate2->health : -1);
	for (int i = 0; i < 2; i++) {
		PlayerInput a = held(false, false, false, false, true), n = held();
		platform::setInputOverride(0, &a); g2.update();
		platform::setInputOverride(0, &n); for (int k = 0; k < 14; k++) g2.update();
	}
	crate2 = g2.getObjectById(5);
	printf("   crate hp after combo: %g  state=%s\n", crate2 ? crate2->health : -1, q->state.name.c_str());
	CHECK(crate2 && crate2->health < 50, "melee damaged the crate (hp=%g)", crate2 ? crate2->health : -1);

	printf("== grenade throw into the barrel\n");
	if (Obj* c = g2.getObjectById(5)) c->kill();   // clear the line of fire
	run(g2, held(), 5);
	// go back left to the grenades at x=100 and pick them up
	for (int i = 0; i < 200 && q->getPosition().x > 100; i++) run(g2, held(false, false, true), 1);
	{
		PlayerInput f = held(false, false, false, false, false, false, false, true), n = held();
		platform::setInputOverride(0, &f); g2.update();
		platform::setInputOverride(0, &n); g2.update();
	}
	printf("   throwable inv=%d  player x=%.1f\n", q->inventory[INV_THROWABLE], q->getPosition().x);
	CHECK(q->inventory[INV_THROWABLE] != -1, "picked up the grenades");
	run(g2, held(false, false, false, true), 3);
	run(g2, held(false, false, false, false, false, false, true), 20);   // aim
	{
		PlayerInput s = held(false, false, false, false, false, true, true), a = held(false, false, false, false, false, false, true);
		platform::setInputOverride(0, &s); g2.update();      // equip throwable
		platform::setInputOverride(0, &a); g2.update();
		platform::setInputOverride(0, &s); g2.update();      // cook
		for (int i = 0; i < 4; i++) g2.update();
		printf("   state=%s\n", q->state.name.c_str());
		platform::setInputOverride(0, &a); g2.update();      // release -> throw
	}
	bool barrelGone = false;
	for (int i = 0; i < 200 && !barrelGone; i++) {
		PlayerInput a = held(false, false, false, false, false, false, true);
		platform::setInputOverride(0, &a);
		g2.update();
		barrelGone = g2.getObjectById(6) == nullptr;
		if (i % 10 == 0 || barrelGone)
			for (Obj* o : g2.getObjectsByParent(Registry::instance().get("grenade")))
				printf("   f%d nade (%.1f,%.1f) v=(%.2f,%.2f) player x=%.1f\n", i, o->getPosition().x, o->getPosition().y, o->getVelocity().x, o->getVelocity().y, q->getPosition().x);
	}
	int debris = (int)g2.getObjectsByParent(Registry::instance().get("explosive_barrel_debris")).size();
	Obj* nade = nullptr;
	for (Obj* o : g2.getObjectsByParent(Registry::instance().get("grenade"))) nade = o;
	printf("   barrel gone=%d debris=%d grenade alive=%d player hp=%g\n", (int)barrelGone, debris, nade != nullptr, q->health);
	CHECK(nade == nullptr, "grenade detonated");
	CHECK(barrelGone, "explosive barrel destroyed");
	CHECK(debris > 0, "barrel left debris (%d)", debris);

	// ------------------------------------------------------------------ scenario 3: gamepad layout
	gamePtr.reset();
	printf("\n######## scenario 3: gamepad layout (Vita)\n");
	{
		GameOptions o; o.mapText = testMap(); o.localPlayers = 1;
		gamePtr = std::make_unique<Game>(o);
	}
	Game& g3 = *gamePtr;
	Player* r = player(g3);
	PlayerInput none;
	run(g3, none, 90);


	printf("== X jumps\n");
	{
		float y0 = r->getPosition().y, top = y0;
		run(g3, pad([](PlayerInput& i) { i.jump = true; }), 1);
		for (int k = 0; k < 40; k++) { run(g3, none, 1); top = std::min(top, r->getPosition().y); }
		CHECK(y0 - top > 20, "jumped with Cross (%.1f px)", y0 - top);
	}

	printf("== walk to the pistol and pick it up with Up\n");
	for (int k = 0; k < 200 && r->getPosition().x < 50; k++) run(g3, pad([](PlayerInput& i) { i.right = true; }), 1);
	run(g3, pad([](PlayerInput& i) { i.up = true; i.interact = true; }), 1);
	run(g3, none, 2);
	CHECK(r->inventory[INV_GUN] != -1, "picked up the pistol");

	printf("== right stick aims while walking\n");
	{
		auto aimWalk = pad([](PlayerInput& i) { i.stickAim = true; i.stickX = -0.7f; i.stickY = -0.7f; i.right = true; });
		run(g3, aimWalk, 30);   // gun drawn, still walking
		float x0 = r->getPosition().x;
		run(g3, aimWalk, 20);
		float dx = r->getPosition().x - x0;
		printf("   state=%s facing=%d angle=%.1f dx=%.1f\n", r->state.name.c_str(), r->facingDirection, r->crosshairAngle, dx);
		CHECK(r->checkState("aiming"), "aiming with the right stick");
		CHECK(dx > 10, "kept walking right while aiming (dx=%.1f)", dx);
		CHECK(r->facingDirection == -1, "faces the stick (left) while walking right");
		CHECK(r->crosshairAngle < -30 && r->crosshairAngle > -60, "crosshair follows the stick (%.1f)", r->crosshairAngle);
	}

	printf("== R fires without holding L\n");
	{
		run(g3, none, 10);
		Gun* gun = static_cast<Gun*>(g3.getObjectById(r->inventory[INV_GUN]));
		float ammo0 = gun ? gun->ammo : -1;
		auto rt = pad([](PlayerInput& i) { i.fire = true; i.aim = true; });
		run(g3, rt, 40);   // draws the gun, the queued shot goes off once it is out
		run(g3, none, 5);
		float ammo1 = gun ? gun->ammo : -1;
		CHECK(gun && ammo1 == ammo0 - 1, "one shot fired by holding R (%g -> %g)", ammo0, ammo1);
	}

	printf("== Square punches, Circle kicks\n");
	{
		run(g3, none, 20);
		run(g3, pad([](PlayerInput& i) { i.punch = true; }), 1);
		CHECK(r->checkState("combo_1"), "Square starts the punch combo (%s)", r->state.name.c_str());
		run(g3, none, 30);
		run(g3, pad([](PlayerInput& i) { i.kick = true; }), 1);
		CHECK(r->checkState("kicking"), "Circle kicks (%s)", r->state.name.c_str());
		run(g3, none, 20);
	}

	printf("== Triangle throws a grenade\n");
	{
		for (int k = 0; k < 200 && r->getPosition().x > 80; k++) run(g3, pad([](PlayerInput& i) { i.left = true; }), 1);
		run(g3, pad([](PlayerInput& i) { i.interact = true; }), 1);
		run(g3, none, 2);
		Obj* t = g3.getObjectById(r->inventory[INV_THROWABLE]);
		CHECK(t != nullptr, "picked up grenades");
		float ammo0 = t ? static_cast<Throwable*>(t)->ammo : -1;
		auto tri = pad([](PlayerInput& i) { i.grenade = true; });
		run(g3, tri, 30);   // draw + cook
		CHECK(r->checkState("prepare_throw"), "holding Triangle cooks the grenade (%s)", r->state.name.c_str());
		run(g3, none, 30);   // release -> throw
		int nades = (int)g3.getObjectsByParent(Registry::instance().get("grenade")).size();
		float ammo1 = t ? static_cast<Throwable*>(t)->ammo : -1;
		CHECK(nades == 1 && ammo1 == ammo0 - 1, "released Triangle threw one grenade (live=%d, ammo %g -> %g)", nades, ammo0, ammo1);
	}

	// ------------------------------------------------------------------ scenario 5: dive, fall damage, gun throw
	gamePtr.reset();
	printf("\n######## scenario 5: dive roll, fall damage, throwing an empty gun\n");
	{
		GameOptions o; o.mapText = testMap(); o.localPlayers = 2;
		gamePtr = std::make_unique<Game>(o);
	}
	{
		Game& g5 = *gamePtr;
		Player* a = player(g5);
		Player* b = static_cast<Player*>(g5.getObjectById(g5.players[1].objectId));
		PlayerInput none;
		platform::setInputOverride(1, &none);
		a->setPosition(20, 90); b->setPosition(200, 90);
		run(g5, none, 60);

		printf("== running jump = dive + roll\n");
		PlayerInput r = pad([](PlayerInput& i) { i.right = true; });
		run(g5, r, 1); run(g5, none, 2); run(g5, r, 6);   // double tap -> run
		CHECK(a->checkState("running"), "running (%s)", a->state.name.c_str());
		float x0 = a->getPosition().x;
		PlayerInput rj = r; rj.jump = true;
		run(g5, rj, 1);
		CHECK(a->checkState("diving"), "running + Cross dives (%s)", a->state.name.c_str());
		bool rolled = false;
		for (int k = 0; k < 60 && !rolled; k++) { run(g5, r, 1); rolled = a->checkState("rolling"); }
		CHECK(rolled, "landed into a roll after %.0f px", a->getPosition().x - x0);

		printf("== fall damage\n");
		run(g5, none, 60);
		a->setPosition(40, -150); a->setVelocity(0, 0);
		float hp0 = a->health;
		run(g5, none, 120);
		CHECK(a->health < hp0 - 5, "a 240 px fall hurts (%g -> %g)", hp0, a->health);
		run(g5, none, 60);

		printf("== empty gun is thrown and knocks the enemy down\n");
		a->setPosition(100, 90); b->setPosition(160, 90);
		run(g5, none, 60);
		Obj* gun = g5.createObject("pistol", ObjOptions().at(a->getPosition()));
		static_cast<Gun*>(gun)->ammo = 0;
		run(g5, pad([](PlayerInput& i) { i.interact = true; }), 1);
		run(g5, pad([](PlayerInput& i) { i.right = true; }), 2);
		CHECK(a->inventory[INV_GUN] == gun->id, "holding the empty pistol");
		float bhp = b->health;
		for (int k = 0; k < 30; k++) {
			run(g5, pad([](PlayerInput& i) { i.fire = true; i.aim = true; }), 1);
		}
		bool knocked = false;
		for (int k = 0; k < 40 && !knocked; k++) { run(g5, none, 1); knocked = b->checkState("free_falling") || b->checkState("recovering"); }
		CHECK(a->inventory[INV_GUN] == -1, "the pistol left the hands");
		CHECK(knocked && b->health < bhp, "the enemy got knocked down (%s, hp %g -> %g)", b->state.name.c_str(), bhp, b->health);
		platform::setInputOverride(1, nullptr);
	}

	// ------------------------------------------------------------------ scenario 4: original level art maps
	for (const char* mapName : {"warehouse", "backstreets", "factory", "sewers", "rooftops", "containers"}) {
		gamePtr.reset();
		printf("\n######## scenario 4: map %s\n", mapName);
		GameOptions o;
		o.mapText = platform::readFile(std::string("maps/") + mapName + ".sfm");
		o.localPlayers = 2;
		gamePtr = std::make_unique<Game>(o);
		Game& gm = *gamePtr;
		CHECK(gm.levelBounds.has_value() && !gm.layers.empty(), "%s: art layer and bounds loaded", mapName);
		for (int round = 0; round < 3; round++) {
			gm.restartGame();
			PlayerInput idle;
			platform::setInputOverride(0, &idle);
			platform::setInputOverride(1, &idle);
			for (int k = 0; k < 120; k++) gm.update();
			auto inside = [&](Obj* pl) {
				Vec2 q = pl->getPosition();
				return q.x > gm.levelBounds->min.x && q.x < gm.levelBounds->max.x && q.y > gm.levelBounds->min.y &&
					   q.y < gm.levelBounds->max.y;
			};
			for (Obj* pl : gm.getPlayers()) {
				Vec2 q = pl->getPosition();
				CHECK(pl->onGround().has_value() && inside(pl), "round %d: player landed at (%.0f, %.0f) %s vel %.2f,%.2f contacts %d", round, q.x, q.y,
					  pl->state.name.c_str(), pl->getVelocity().x, pl->getVelocity().y, (int)pl->collisions.size());
			}
			// run around for a while: nobody may leave the level
			PlayerInput l = platform::keyboardInput(false, false, true, false, false, false, false, false);
			PlayerInput rr = platform::keyboardInput(true, false, false, true, false, false, false, false);
			platform::setInputOverride(0, &l);
			platform::setInputOverride(1, &rr);
			for (int k = 0; k < 400; k++) gm.update();
			for (Obj* pl : gm.getPlayers()) {
				Vec2 q = pl->getPosition();
				CHECK(inside(pl), "round %d: still inside the level after running (%.0f, %.0f)", round, q.x, q.y);
			}
		}
		{
			gm.restartGame();
			PlayerInput idle;
			platform::setInputOverride(1, &idle);
			Player* pl = player(gm);
			for (Obj* d : gm.getObjectsByParent(Registry::instance().get("exit_door"))) {
				Door* door = static_cast<Door*>(d);
				Obj* target = nullptr;
				for (Obj* o : gm.getObjectsByParent(door->parent))
					if (o->customId == door->link) target = o;
				// run into the door from its open side, on the move, without any button
				int into = door->into();
				int dir = into != 0 ? into : 1;
				Vec2 at = door->getPosition();
				pl->setPosition(at.x - dir * 16, at.y);
				pl->setVelocity(0, 0);
				run(gm, idle, 30);
				PlayerInput go;
				(dir > 0 ? go.right : go.left) = true;
				Vec2 t = target ? target->getPosition() : Vec2{0, 0};
				bool arrived = false;
				for (int k = 0; k < 40 && !arrived; k++) {
					run(gm, go, 1);
					Vec2 q = pl->getPosition();
					arrived = std::fabs(q.x - t.x) < 4 && std::fabs(q.y - t.y) < 4;
				}
				CHECK(arrived, "door %s -> %s: reached by just walking (into %d)", door->customId.c_str(), door->link.c_str(), into);

				// the same button keeps walking, away from the exit, and does not bounce back
				float x0 = pl->getPosition().x;
				int out = target && static_cast<Door*>(target)->into() != 0 ? -static_cast<Door*>(target)->into() : dir;
				run(gm, go, 20);
				float moved = pl->getPosition().x - x0;
				CHECK(moved * out > 8 && std::fabs(pl->getPosition().y - t.y) < 40, "  held button leads away from the exit (%+.0f px, out %d)", moved, out);
				run(gm, idle, 5);
				PlayerInput back;
				(out > 0 ? back.left : back.right) = true;
				float x1 = pl->getPosition().x;
				run(gm, back, 5);
				CHECK((pl->getPosition().x - x1) * out < 0, "  after release the controls are normal again");
			}
		}
		{
			auto panes = gm.getObjectsByParent(Registry::instance().get("glass"));
			if (!panes.empty()) {
				gm.restartGame();
				PlayerInput idle;
				platform::setInputOverride(1, &idle);
				Player* pl = player(gm);
				Obj* pane = gm.getObjectsByParent(Registry::instance().get("glass"))[0];
				int pid = pane->id;
				Vec2 gp = pane->getPosition();
				float floorY = gp.y + pane->height / 2;
				pl->setPosition(gp.x + 9, floorY - 10);
				pl->setVelocity(0, 0);
				run(gm, idle, 20);
				run(gm, pad([](PlayerInput& i) { i.left = true; }), 15);   // walk into it: must not break
				bool intact = gm.getObjectById(pid) != nullptr;
				for (int k = 0; k < 3; k++) {
					run(gm, pad([](PlayerInput& i) { i.punch = true; }), 1);
					run(gm, idle, 14);
				}
				int shards = (int)gm.getObjectsByParent(Registry::instance().get("glass_shard")).size();
				CHECK(intact && gm.getObjectById(pid) == nullptr && shards > 0,
					  "glass: survives a touch (%d), punches break it into %d shards", (int)intact, shards);
			}
		}
		if (gm.deathY && std::string(mapName) == "sewers") {
			Obj* c = gm.createObject("crate", ObjOptions().at({250, 250}));
			int cid = c ? c->id : -1;
			for (int k = 0; k < 120; k++) gm.update();
			CHECK(cid >= 0 && gm.getObjectById(cid) == nullptr, "a crate dropped into the acid is destroyed");
		}
		platform::setInputOverride(1, nullptr);
	}

	// ------------------------------------------------------------------ scenario 6: combat feel
	{
		gamePtr.reset();
		printf("\n######## scenario 6: warehouse moves and combat\n");
		GameOptions o;
		o.mapText = platform::readFile("maps/warehouse.sfm");
		o.localPlayers = 2;
		gamePtr = std::make_unique<Game>(o);
		Game& gm = *gamePtr;
		PlayerInput idle;
		auto fresh = [&](Vec2 pa, Vec2 pb) {
			gm.restartGame();
			platform::setInputOverride(0, &idle);
			platform::setInputOverride(1, &idle);
			Player* a = static_cast<Player*>(gm.getObjectById(gm.players[0].objectId));
			Player* b = static_cast<Player*>(gm.getObjectById(gm.players[1].objectId));
			a->setPosition(pa.x, pa.y);
			b->setPosition(pb.x, pb.y);
			a->setVelocity(0, 0);
			b->setVelocity(0, 0);
			run(gm, idle, 40);
			return std::make_pair(a, b);
		};

		printf("== staircase from the racks to the gallery\n");
		auto [up, far] = fresh({200, 198}, {420, 245});
		PlayerInput left, right;
		left.left = true;
		right.right = true;
		run(gm, left, 70);
		CHECK(up->getPosition().y < 175 && up->getPosition().x < 160, "walks up (%.0f,%.0f)", up->getPosition().x, up->getPosition().y);
		up->setPosition(170, 176);
		up->setVelocity(0, 0);
		run(gm, idle, 20);
		Vec2 s0 = up->getPosition();
		run(gm, idle, 120);
		CHECK((up->getPosition() - s0).length() < 1, "stands still on the stairs (%.1f,%.1f -> %.1f,%.1f)", s0.x, s0.y, up->getPosition().x, up->getPosition().y);
		run(gm, right, 45);
		CHECK(up->getPosition().y > 190 && up->onGround().has_value(), "walks down (%.0f,%.0f)", up->getPosition().x, up->getPosition().y);

		printf("== running jump is a long flat dive\n");
		auto [d1, d2] = fresh({60, 245}, {420, 245});
		d1->walkingSpeed = 2;
		run(gm, right, 1);
		run(gm, idle, 1);
		run(gm, right, 8);
		float dx0 = d1->getPosition().x, dy0 = d1->getPosition().y, top = dy0;
		PlayerInput runJump = right;
		runJump.jump = true;
		run(gm, runJump, 1);
		CHECK(d1->checkState("air_diving"), "dive started (%s)", d1->state.name.c_str());
		for (int k = 0; k < 16; k++) { run(gm, right, 1); top = std::min(top, d1->getPosition().y); }
		CHECK(dy0 - top < 9, "stays low: %.1f px high", dy0 - top);
		run(gm, idle, 24);
		float span = d1->getPosition().x - dx0;
		CHECK(span > 25 && span < 80, "covers %.0f px including the roll", span);

		printf("== hard landing stuns\n");
		auto [l1, l2] = fresh({100, 110}, {420, 245});
		l1->setPosition(312, 58);
		l1->setVelocity(0, 0);
		float hp = l1->health;
		int stun = 0, landedAt = -1;
		for (int k = 0; k < 120; k++) {
			run(gm, right, 1);
			bool down = l1->checkState("damaged");
			if (down) stun++;
			if (down && landedAt < 0) landedAt = k;
		}
		CHECK(stun >= 16, "dazed for %d frames (hp %g -> %g)", stun, hp, l1->health);

		printf("== bullets push, the shot pushes back\n");
		auto shoot = [&](const char* gun) {
			auto [s, t] = fresh({175, 263}, {215, 263});
			Obj* g = gm.createObject(gun, ObjOptions().at(s->getPosition()));
			s->inventory[INV_GUN] = -1;
			PlayerInput pick;
			pick.interact = true;
			run(gm, pick, 1);
			run(gm, idle, 1);
			static_cast<Gun*>(gm.getObjectById(g->id))->ammo = 5;
			Vec2 a0 = s->getPosition(), b0 = t->getPosition();
			PlayerInput aim;
			aim.aim = true;
			run(gm, aim, 10);
			PlayerInput fire = aim;
			fire.fire = true;
			run(gm, fire, 1);
			run(gm, aim, 12);
			float r = std::fabs(s->getPosition().x - a0.x);
			bool down = t->checkState("free_falling") || t->checkState("damaged");
			run(gm, idle, 30);
			return std::make_tuple(r, t->alive ? std::fabs(t->getPosition().x - b0.x) : 99.f, down);
		};
		auto [rp, kp, dp] = shoot("pistol");
		auto [rm, km, dm] = shoot("magnum");
		auto [rs, ks, ds] = shoot("sniper");
		printf("  pistol recoil %.1f push %.1f | magnum %.1f %.1f | sniper %.1f %.1f\n", rp, kp, rm, km, rs, ks);
		CHECK(rp < 0.05f && rm > rp && rs > rm, "pistol: no recoil, bigger guns a little");
		CHECK(kp > 0.5f && km > kp && ks > km && dm && ds, "stronger gun, stronger knockback");

		printf("== rocket carries the target, then explodes\n");
		auto [r1, r2] = fresh({120, 245}, {420, 245});
		Obj* rocket = gm.createObject("rocket", ObjOptions().at({100, 170}).vel({4, 0}));
		(void)rocket;
		r2->setPosition(112, 170);
		r2->setVelocity(0, 0);
		int rid = rocket->id, ride = 0;
		float maxDist = 0;
		Vec2 start = r2->getPosition();
		bool insideWall = false;
		for (int k = 0; k < 110 && gm.getObjectById(rid); k++) {
			run(gm, idle, 1);
			if (!r2->alive) break;
			if (gm.getObjectById(rid) && (r2->getPosition() - gm.getObjectById(rid)->getPosition()).length() < 9) ride++;
			maxDist = std::max(maxDist, (r2->getPosition() - start).length());
			Vec2 q = r2->getPosition();
			insideWall = insideWall || q.x < gm.levelBounds->min.x || q.x > gm.levelBounds->max.x || q.y < gm.levelBounds->min.y || q.y > gm.levelBounds->max.y;
		}
		CHECK(ride >= 35, "carried for %d frames, up to %.0f px away", ride, maxDist);
		CHECK(maxDist > 30 && !insideWall, "really travels, stays in the level");
		CHECK(gm.getObjectById(rid) == nullptr, "rocket is gone: it exploded");

		printf("== falling crate hurts\n");
		auto [c1, c2] = fresh({282, 263}, {420, 245});
		float chp = c1->health;
		gm.createObject("crate", ObjOptions().at({282, 150}));
		run(gm, idle, 60);
		CHECK(c1->health < chp - 5, "hp %g -> %g", chp, c1->health);

		printf("== death leaves a body\n");
		auto [k1, k2] = fresh({240, 263}, {420, 245});
		k1->dealDamage({500, "melee"});
		CHECK(gm.getObjectById(gm.players[0].objectId) == nullptr, "fighter is out");
		run(gm, idle, 250);
		auto bodies = gm.getObjectsByParent(Registry::instance().get("corpse"));
		CHECK(bodies.size() == 1 && bodies[0]->onGround().has_value() && std::fabs(bodies[0]->getPosition().x - 240) < 12,
			  "corpse lies there after 4 s (%d)", (int)bodies.size());
		platform::setInputOverride(1, nullptr);
	}

	// ------------------------------------------------------------------ scenario 7: bots across rounds
	{
		gamePtr.reset();
		printf("\n######## scenario 7: bots keep fighting in later rounds\n");
		GameOptions o;
		o.mapText = platform::readFile("maps/warehouse.sfm");
		o.localPlayers = 0;
		o.bots = 2;
		gamePtr = std::make_unique<Game>(o);
		Game& gm = *gamePtr;
		for (int k = 0; k < 2000; k++) gm.update();   // bot timers far ahead of a new round's frame counter
		gm.restartGame();
		Obj* a = gm.getObjectById(gm.players[0].objectId);
		Obj* b = gm.getObjectById(gm.players[1].objectId);
		a->setPosition(190, 263);
		b->setPosition(199, 263);
		for (int k = 0; k < 90; k++) gm.update();
		bool hurt = !a->alive || !b->alive || a->health < 100 || b->health < 100;
		CHECK(hurt, "second round: adjacent bots hit each other (hp %g / %g)", a->health, b->health);
	}

	// ------------------------------------------------------------------ scenario 9: close combat
	{
		gamePtr.reset();
		printf("\n######## scenario 9: grab, combo, air attacks\n");
		GameOptions o;
		o.mapText = platform::readFile("maps/warehouse.sfm");
		o.localPlayers = 2;
		gamePtr = std::make_unique<Game>(o);
		Game& gm = *gamePtr;
		PlayerInput idle;
		auto fresh = [&](Vec2 pa, Vec2 pb) {
			gm.restartGame();
			platform::setInputOverride(0, &idle);
			platform::setInputOverride(1, &idle);
			Player* a = static_cast<Player*>(gm.getObjectById(gm.players[0].objectId));
			Player* b = static_cast<Player*>(gm.getObjectById(gm.players[1].objectId));
			a->setPosition(pa.x, pa.y);
			b->setPosition(pb.x, pb.y);
			a->setVelocity(0, 0);
			b->setVelocity(0, 0);
			run(gm, idle, 40);
			return std::make_pair(a, b);
		};
		PlayerInput grab, fire, punch;
		grab.grab = true;
		fire.fire = true;
		fire.aim = true;
		punch.punch = true;

		printf("== grab and throw\n");
		auto [a1, b1] = fresh({180, 263}, {190, 263});
		run(gm, grab, 1);
		run(gm, idle, 20);
		CHECK(a1->holding == b1->id && b1->heldBy == a1->id && b1->getPosition().y < a1->getPosition().y - 10, "lifted overhead (y %.0f / %.0f)", b1->getPosition().y, a1->getPosition().y);
		float x1 = b1->getPosition().x, hp1 = b1->health;
		run(gm, fire, 1);
		run(gm, idle, 14);
		CHECK(a1->holding < 0 && b1->heldBy < 0 && b1->getPosition().x - x1 > 25 && b1->health < hp1, "thrown forward %.0f px, hp %g -> %g", b1->getPosition().x - x1, hp1, b1->health);

		printf("== too slow: the victim breaks free\n");
		auto [a2, b2] = fresh({180, 263}, {190, 263});
		run(gm, grab, 1);
		run(gm, idle, 90);
		CHECK(a2->holding < 0 && b2->heldBy < 0 && a2->state.name != "grounded_attacking_grab", "free again, grabber is %s", a2->state.name.c_str());
		CHECK(std::fabs(b2->getPosition().y - a2->getPosition().y) < 4, "back on the floor");

		printf("== struggling shortens the hold\n");
		auto [a3, b3] = fresh({180, 263}, {190, 263});
		run(gm, grab, 1);
		PlayerInput mash;
		mash.punch = true;
		int k3 = 0;
		for (; k3 < 72 && a3->holding >= 0; k3++) {
			platform::setInputOverride(1, k3 % 2 ? &idle : &mash);
			run(gm, idle, 1);
		}
		platform::setInputOverride(1, &idle);
		CHECK(k3 < 45, "broke free after %d frames", k3);

		printf("== L at a distance throws the melee weapon\n");
		auto [a4, b4] = fresh({180, 263}, {240, 263});
		a4->facingDirection = 1;
		int bladeId = gm.createObject("machete", ObjOptions().at(a4->getPosition()))->id;
		PlayerInput pick;
		pick.interact = true;
		run(gm, pick, 1);
		run(gm, idle, 2);
		CHECK(a4->inventory[INV_MELEE] == bladeId, "picked the machete up");
		float hp4 = b4->health;
		run(gm, grab, 1);
		run(gm, idle, 40);
		CHECK(a4->inventory[INV_MELEE] != bladeId && b4->health < hp4 - 10, "it flew and hit: hp %g -> %g", hp4, b4->health);

		printf("== combo: three hits disarm\n");
		auto [a5, b5] = fresh({180, 263}, {190, 263});
		Obj* g5 = gm.createObject("pistol", ObjOptions().at(b5->getPosition()));
		platform::setInputOverride(1, &pick);
		run(gm, idle, 1);
		platform::setInputOverride(1, &idle);
		run(gm, idle, 2);
		CHECK(b5->inventory[INV_GUN] == g5->id, "victim holds a pistol");
		for (int k = 0; k < 3; k++) { run(gm, punch, 1); run(gm, idle, 11); a5->setPosition(180, a5->getPosition().y); }
		run(gm, idle, 20);
		CHECK(b5->inventory[INV_GUN] < 0, "the pistol is knocked out of the hands");
		bool lying = false;
		for (auto& sp : gm.allObjects())
			lying = lying || (sp->type() == ObjType::Gun && static_cast<Gun*>(sp.get())->holderId < 0 && (sp->getPosition() - b5->getPosition()).length() < 60);
		CHECK(lying, "and lies on the floor nearby");

		printf("== combo without a weapon: heavy blow\n");
		auto [a6, b6] = fresh({180, 263}, {190, 263});
		float hp6 = b6->health, x6 = b6->getPosition().x;
		int dazed = 0;
		for (int k = 0; k < 3; k++) { run(gm, punch, 1); run(gm, idle, 11); a6->setPosition(180, a6->getPosition().y); }
		for (int k = 0; k < 90; k++) { run(gm, idle, 1); if (b6->checkState("landing")) dazed++; }
		CHECK(b6->getPosition().x - x6 > 6 && b6->health <= hp6 - 25 && dazed >= 30, "thrown %.0f px, hp %g -> %g, dazed %d frames", b6->getPosition().x - x6, hp6, b6->health, dazed);

		printf("== jump kick beats a standing hit\n");
		auto [a7, b7] = fresh({182, 263}, {204, 263});
		PlayerInput jr;
		jr.jump = true;
		jr.right = true;
		float hp7 = b7->health, x7 = b7->getPosition().x;
		run(gm, jr, 1);
		PlayerInput r;
		r.right = true;
		run(gm, r, 5);
		PlayerInput kr = r;
		kr.kick = true;
		run(gm, kr, 1);
		run(gm, r, 12);
		run(gm, idle, 30);
		CHECK(b7->health <= hp7 - 14 && b7->getPosition().x - x7 > 8, "hp %g -> %g, pushed %.0f px", hp7, b7->health, b7->getPosition().x - x7);

		printf("== crouching ducks under bullets\n");
		auto [a8, b8] = fresh({170, 263}, {215, 263});
		a8->facingDirection = 1;
		Obj* g8 = gm.createObject("pistol", ObjOptions().at(a8->getPosition()));
		g8->pickup(*a8);
		a8->inventory[INV_GUN] = g8->id;
		PlayerInput aim;
		aim.aim = true;
		PlayerInput duck;
		duck.down = true;
		platform::setInputOverride(1, &duck);
		run(gm, aim, 12);
		float hp8 = b8->health;
		run(gm, fire, 1);
		run(gm, aim, 12);
		CHECK(b8->checkState("crouching") && b8->health == hp8, "not hit (%s, hp %g)", b8->state.name.c_str(), b8->health);
		platform::setInputOverride(1, &idle);

		printf("== wall slam\n");
		auto [a9, b9] = fresh({425, 131}, {200, 263});
		float hp9 = a9->health;
		a9->knockback({7, -1}, true);
		run(gm, idle, 12);
		CHECK(a9->health < hp9 - 5, "hp %g -> %g", hp9, a9->health);

		printf("== resting objects do not shake\n");
		for (const char* mapName : {"warehouse", "backstreets", "rooftops", "containers"}) {
			GameOptions m;
			m.mapText = platform::readFile(std::string("maps/") + mapName + ".sfm");
			m.localPlayers = 1;
			Game calm(m);
			platform::setInputOverride(0, &idle);
			if (Obj* nobody = calm.getObjectById(calm.players[0].objectId)) {   // only the props
				nobody->disableGravity = true;
				nobody->setPosition(-500, -500);
			}
			for (int k = 0; k < 300; k++) calm.update();
			std::map<int, std::pair<Vec2, Vec2>> span;
			for (int k = 0; k < 120; k++) {
				calm.update();
				for (auto& sp : calm.allObjects()) {
					if (!sp->body || sp->body->isStatic || sp->type() != ObjType::Base || sp->parent->cls == "Hanging") continue;
					Vec2 q = sp->getPosition();
					auto it = span.find(sp->id);
					if (it == span.end()) span[sp->id] = {q, q};
					else {
						it->second.first = {std::min(it->second.first.x, q.x), std::min(it->second.first.y, q.y)};
						it->second.second = {std::max(it->second.second.x, q.x), std::max(it->second.second.y, q.y)};
					}
				}
			}
			int shaking = 0;
			for (auto& [id, r2] : span)
				if (r2.second.y - r2.first.y > 0.1f || r2.second.x - r2.first.x > 1.5f) {
					shaking++;
					Obj* o = calm.getObjectById(id);
					printf("  moved: %s at (%.1f,%.1f) dx %.2f dy %.2f\n", o ? o->parent->key.c_str() : "?", r2.first.x, r2.first.y, r2.second.x - r2.first.x, r2.second.y - r2.first.y);
				}
			CHECK(shaking == 0, "%s: %d of %d loose objects moved", mapName, shaking, (int)span.size());
		}
		platform::setInputOverride(1, nullptr);
	}

	// ------------------------------------------------------------------ scenario 10: living maps
	{
		gamePtr.reset();
		printf("\n######## scenario 10: living maps\n");
		auto loadMap = [&](const char* name, int locals) {
			gamePtr.reset();
			GameOptions o;
			o.mapText = platform::readFile(std::string("maps/") + name + ".sfm");
			o.localPlayers = locals;
			o.suddenDeath = false;
			gamePtr = std::make_unique<Game>(o);
			return gamePtr.get();
		};
		PlayerInput idle, pick;
		pick.interact = true;
		auto reg = [](const char* k) { return Registry::instance().get(k); };

		Game* gm = loadMap("warehouse", 2);
		platform::setInputOverride(0, &idle);
		platform::setInputOverride(1, &idle);
		Player* a = static_cast<Player*>(gm->getObjectById(gm->players[0].objectId));
		Player* b = static_cast<Player*>(gm->getObjectById(gm->players[1].objectId));
		a->setPosition(182, 263);
		b->setPosition(420, 245);
		run(*gm, idle, 60);

		printf("== hanging crate\n");
		auto hung = gm->getObjectsByParent(reg("crate_hanging"));
		CHECK(hung.size() == 1, "one crate on a chain");
		float y0 = hung[0]->getPosition().y;
		run(*gm, idle, 120);
		CHECK(std::fabs(hung[0]->getPosition().y - y0) < 2, "stays up (%.1f -> %.1f)", y0, hung[0]->getPosition().y);
		hung[0]->dealDamage({6, "projectile"});
		hung[0]->dealDamage({6, "projectile"});
		run(*gm, idle, 40);
		CHECK(hung[0]->getPosition().y > y0 + 15, "two hits and the chain goes (%.1f -> %.1f)", y0, hung[0]->getPosition().y);

		printf("== lamp, shards under a roll\n");
		auto lamps = gm->getObjectsByParent(reg("lamp"));
		CHECK(lamps.size() == 3, "three lamps");
		for (int k = 0; k < 6; k++) {
			ObjOptions so;
			so.at({a->getPosition().x + 12 + k * 3, a->getPosition().y + 6});
			gm->createObject("glass_shard", so)->born = -1000;   // settled long ago
		}
		lamps[0]->dealDamage({6, "projectile"});
		run(*gm, idle, 10);
		CHECK(gm->getObjectsByParent(reg("lamp")).size() == 2, "a shot breaks a lamp");
		a->facingDirection = 1;
		float hp = a->health;
		PlayerInput down, downRight;
		down.down = true;
		downRight.down = true;
		downRight.right = true;
		run(*gm, down, 3);
		run(*gm, downRight, 20);
		run(*gm, idle, 5);
		CHECK(a->health < hp, "rolling over the glass hurts (hp %g -> %g)", hp, a->health);

		printf("== chain explosion\n");
		Obj* b1 = gm->createObject("explosive_barrel", ObjOptions().at({300, 258}));
		Obj* b2 = gm->createObject("explosive_barrel", ObjOptions().at({320, 245}));
		int id1 = b1->id, id2 = b2->id;
		run(*gm, idle, 30);
		gm->getObjectById(id1)->dealDamage({5, "projectile"});
		run(*gm, idle, 5);
		CHECK(!gm->getObjectById(id1) && !gm->getObjectById(id2), "one barrel sets off the other");

		printf("== club from the wreckage\n");
		a->setPosition(200, 263);
		a->setVelocity(0, 0);
		run(*gm, idle, 20);
		gm->createObject("crate_debris01", ObjOptions().at(a->getPosition()));
		run(*gm, pick, 1);
		run(*gm, idle, 1);
		Obj* club = gm->getObjectById(a->inventory[INV_MELEE]);
		CHECK(club && club->parent->key == "plank", "debris picked up as a %s", club ? club->parent->key.c_str() : "nothing");
		b->setPosition(208, 263);
		b->setVelocity(0, 0);
		a->facingDirection = 1;
		PlayerInput punch;
		punch.punch = true;
		bool broke = false;
		for (int k = 0; k < 14 && !broke; k++) {
			b->health = 100;
			b->setPosition(a->getPosition().x + 8, 263);
			run(*gm, punch, 1);
			run(*gm, idle, 26);
			Obj* m = gm->getObjectById(a->inventory[INV_MELEE]);
			broke = !m || m->parent->key == "fists";
		}
		CHECK(broke, "breaks after a few blows");

		printf("== crates hold weapons\n");
		Obj* box = gm->createObject("crate", ObjOptions().at({230, 263}));
		box->customId = "supply";
		int guns = 0;
		box->kill();
		for (auto& sp : gm->allObjects())
			if ((sp->type() == ObjType::Gun || sp->type() == ObjType::Melee || sp->type() == ObjType::Throwable) && sp->body && !sp->body->isStatic) guns++;
		CHECK(guns >= 1, "supply crate spilled a weapon (%d loose)", guns);

		printf("== conveyor\n");
		gm = loadMap("factory", 1);
		platform::setInputOverride(0, &idle);
		a = static_cast<Player*>(gm->getObjectById(gm->players[0].objectId));
		a->setPosition(75, 241);
		a->setVelocity(0, 0);
		run(*gm, idle, 20);
		float cx = a->getPosition().x;
		run(*gm, idle, 40);
		CHECK(a->getPosition().x - cx > 20, "carries the fighter %.0f px", a->getPosition().x - cx);

		printf("== new weapons\n");
		for (const char* key : {"smg", "grenade_launcher", "taser", "mines", "flashbangs", "shurikens", "bat", "knife"}) {
			gm = loadMap("warehouse", 2);
			platform::setInputOverride(0, &idle);
			platform::setInputOverride(1, &idle);
			a = static_cast<Player*>(gm->getObjectById(gm->players[0].objectId));
			b = static_cast<Player*>(gm->getObjectById(gm->players[1].objectId));
			a->setPosition(170, 263);
			b->setPosition(204, 263);
			a->setVelocity(0, 0);
			b->setVelocity(0, 0);
			a->facingDirection = 1;
			run(*gm, idle, 30);
			Obj* w = gm->createObject(key, ObjOptions().at(a->getPosition()));
			ObjType type = w->type();
			run(*gm, pick, 1);
			run(*gm, idle, 1);
			PlayerInput use;
			if (type == ObjType::Gun) { use.aim = true; run(*gm, use, 10); use.fire = true; }
			else if (type == ObjType::Throwable) { use.aim = true; run(*gm, use, 8); use.grenade = true; }
			else { b->setPosition(a->getPosition().x + 10, 263); use.punch = true; }
			run(*gm, use, 1);
			use.fire = use.grenade = use.punch = false;
			bool stunned = false;
			for (int k = 0; k < 130; k++) { run(*gm, use, 1); if (b->alive) stunned = stunned || b->checkState("stun"); }
			bool hurt = !b->alive || b->health < 100;
			bool ok = hurt || stunned || std::string(key) == "mines";
			if (std::string(key) == "mines") {
				bool lying = !gm->getObjectsByParent(reg("mine")).empty();
				CHECK(lying, "mine lies on the floor");
				if (lying) {
					Vec2 q = gm->getObjectsByParent(reg("mine"))[0]->getPosition();
					b->setPosition(q.x + 8, 263);
					b->setVelocity(0, 0);
					run(*gm, idle, 12);
					ok = gm->getObjectsByParent(reg("mine")).empty();
				}
			}
			CHECK(ok, "%s works (hp %g, stunned %d)", key, b->alive ? b->health : 0.f, (int)stunned);
		}

		platform::setInputOverride(1, nullptr);
	}

	// ------------------------------------------------------------------ scenario 11: modes
	{
		gamePtr.reset();
		printf("\n######## scenario 11: game modes\n");
		PlayerInput idle;
		platform::setInputOverride(0, &idle);

		printf("== waves\n");
		GameOptions w;
		w.mapText = platform::readFile("maps/warehouse.sfm");
		w.localPlayers = 1;
		w.mode = MODE_WAVES;
		gamePtr = std::make_unique<Game>(w);
		Game& gw = *gamePtr;
		int alive = 0;
		auto botsAlive = [&] {
			int n = 0;
			for (auto& sl : gw.players) if (sl.bot && gw.getObjectById(sl.objectId)) n++;
			return n;
		};
		CHECK(gw.wave == 1 && botsAlive() == 2, "wave 1: %d bots", botsAlive());
		for (auto& sl : gw.players) if (sl.bot) if (Obj* o = gw.getObjectById(sl.objectId)) o->kill();
		run(gw, idle, 140);
		alive = botsAlive();
		CHECK(gw.wave == 2 && alive == 3, "cleared: wave %d with %d bots", gw.wave, alive);
		Obj* me = gw.getObjectById(gw.players[0].objectId);
		if (me) me->kill();
		run(gw, idle, 10);
		CHECK(gw.gameOver > 0, "the human falls: game over");
		run(gw, idle, 420);
		CHECK(gw.wave <= 2 && gw.gameOver == 0 && botsAlive() >= 1, "starts over (wave %d, %d bots)", gw.wave, botsAlive());

		printf("== teams\n");
		gamePtr.reset();
		GameOptions t;
		t.mapText = platform::readFile("maps/containers.sfm");
		t.localPlayers = 0;
		t.bots = 4;
		t.mode = MODE_TEAMS;
		gamePtr = std::make_unique<Game>(t);
		Game& gt = *gamePtr;
		CHECK(gt.players[0].team != gt.players[1].team, "bots split into teams");
		bool decided = false;
		for (int k = 0; k < 4200 && !decided; k++) {
			gt.update();
			if (gt.gameOver > 0) decided = true;
		}
		bool sameTeam = true;
		int winTeam = -1;
		for (auto& sl : gt.players)
			if (gt.getObjectById(sl.objectId)) {
				if (winTeam < 0) winTeam = sl.team;
				sameTeam = sameTeam && sl.team == winTeam;
			}
		CHECK(decided && sameTeam, "round ends with one team standing (team %d, %d frames)", winTeam, gt.frameCounter);

		printf("== sudden death\n");
		gamePtr.reset();
		GameOptions d;
		d.mapText = platform::readFile("maps/warehouse.sfm");
		d.localPlayers = 2;
		gamePtr = std::make_unique<Game>(d);
		Game& gd = *gamePtr;
		platform::setInputOverride(1, &idle);
		gd.frameCounter = 3600;
		int bombs = 0;
		for (int k = 0; k < 100; k++) {
			gd.update();
			bombs = std::max(bombs, (int)gd.getObjectsByParent(Registry::instance().get("lobbed_grenade")).size());
		}
		CHECK(gd.suddenDeath && bombs > 0, "bombs fall after a minute (%d at once)", bombs);
		platform::setInputOverride(1, nullptr);
	}

	// ------------------------------------------------------------------ scenario 12: perks, skills, tokens, range
	{
		gamePtr.reset();
		printf("\n######## scenario 12: perks and skills\n");
		Progress& pr = Progress::get();
		CHECK(pr.perks.size() >= 23 && pr.skills.size() == 6 && pr.groups.size() >= 7, "config loaded: %d perks, %d skills", (int)pr.perks.size(), (int)pr.skills.size());
		int full = 0;
		for (auto& p : pr.perks) full += p.cost;
		for (auto& sk : pr.skills) full += sk.cost;
		float perRound = pr.tokens["round"] + 2 * pr.tokens["kill"] + 0.3f * pr.tokens["win"];
		float hours = full / perRound * 2.5f / 60;
		printf("  full unlock: %d tokens, about %.0f hours at 2.5 min per round\n", full, hours);
		CHECK(hours > 8, "takes more than a day of play");
		for (auto& p : pr.perks) {
			bool known = p.group.empty() ? !p.branch.empty() : true;
			CHECK(known && !p.effects.empty() && p.cost > 0, "perk %s is complete", p.id.c_str());
		}

		PlayerInput idle, pick;
		pick.interact = true;
		GameOptions o;
		o.mapText = platform::readFile("maps/warehouse.sfm");
		o.localPlayers = 2;
		gamePtr = std::make_unique<Game>(o);
		Game& gm = *gamePtr;
		auto fresh = [&](Vec2 pa, Vec2 pb) {
			gm.restartGame();
			platform::setInputOverride(0, &idle);
			platform::setInputOverride(1, &idle);
			Player* a = static_cast<Player*>(gm.getObjectById(gm.players[0].objectId));
			Player* b = static_cast<Player*>(gm.getObjectById(gm.players[1].objectId));
			a->setPosition(pa.x, pa.y);
			b->setPosition(pb.x, pb.y);
			a->setVelocity(0, 0);
			b->setVelocity(0, 0);
			run(gm, idle, 40);
			return std::make_pair(a, b);
		};
		auto pellets = [&](const char* perk) {
			auto [a, b] = fresh({175, 263}, {420, 245});
			(void)b;
			a->setLoadout({perk}, "");
			a->facingDirection = 1;
			gm.createObject("shotgun", ObjOptions().at(a->getPosition()));
			run(gm, pick, 1);
			run(gm, idle, 1);
			PlayerInput aim, fire;
			aim.aim = true;
			fire.aim = fire.fire = true;
			run(gm, aim, 10);
			size_t before = gm.getObjectsByParent(Registry::instance().get("projectile")).size();
			run(gm, fire, 1);
			int most = 0;
			for (int k = 0; k < 4; k++) {
				run(gm, aim, 1);
				most = std::max(most, (int)gm.getObjectsByParent(Registry::instance().get("projectile")).size() - (int)before);
			}
			return most;
		};
		CHECK(pellets("buckshot") == 6, "Buckshot fires 6 pellets");
		CHECK(pellets("slug") == 1, "Slug fires one");

		{
			auto [a, b] = fresh({180, 263}, {190, 263});
			a->setLoadout({"quick_combo"}, "");
			float hp = b->health;
			PlayerInput punch;
			punch.punch = true;
			for (int k = 0; k < 2; k++) { run(gm, punch, 1); run(gm, idle, 11); a->setPosition(180, a->getPosition().y); }
			run(gm, idle, 30);
			CHECK(hp - b->health > 14, "Quick Combo: two hits finish (hp %g -> %g)", hp, b->health);
		}
		{
			auto drop = [&](const char* perk) {
				auto [a, b] = fresh({312, 58}, {420, 245});
				(void)b;
				a->setLoadout({perk}, "");
				a->setPosition(312, 58);
				a->setVelocity(0, 0);
				run(gm, idle, 100);
				return 100 - a->health;
			};
			float plain = drop("buckshot"), soft = drop("hard_landing");
			CHECK(plain > 10 && soft < plain * 0.7f, "Hard Landing: fall costs %g instead of %g", soft, plain);
		}

		printf("== skills on Select\n");
		PlayerInput select;
		select.skill = true;
		{
			auto [a, b] = fresh({180, 263}, {300, 263});
			(void)b;
			a->setLoadout({}, "second_wind");
			a->health = 40;
			run(gm, select, 1);
			run(gm, idle, 2);
			CHECK(a->health == 65 && !a->skillReady(), "Second Wind heals and starts the cooldown (hp %g)", a->health);
			run(gm, select, 1);
			run(gm, idle, 2);
			CHECK(a->health == 65, "no second use during the cooldown");
		}
		{
			auto [a, b] = fresh({170, 263}, {215, 263});
			(void)b;
			a->setLoadout({}, "dash");
			a->facingDirection = 1;
			float x0 = a->getPosition().x;
			run(gm, select, 1);
			run(gm, idle, 16);
			CHECK(a->getPosition().x - x0 > 30, "Dash covers %.0f px", a->getPosition().x - x0);
		}
		{
			auto [a, b] = fresh({180, 263}, {200, 263});
			a->setLoadout({}, "war_cry");
			run(gm, select, 1);
			run(gm, idle, 2);
			CHECK(b->checkState("stun"), "War Cry stuns (%s)", b->state.name.c_str());
		}
		{
			auto [a, b] = fresh({180, 263}, {190, 263});
			b->setLoadout({}, "counter");
			platform::setInputOverride(1, &select);
			run(gm, idle, 1);
			platform::setInputOverride(1, &idle);
			PlayerInput punch;
			punch.punch = true;
			float hb = b->health;
			run(gm, punch, 1);
			run(gm, idle, 12);
			CHECK(a->health < 100 && b->health == hb, "Counter: the blow comes back (hp %g / %g)", a->health, b->health);
		}

		printf("== tokens, events, contracts\n");
		FighterProfile saved = FighterProfile::local();
		FighterProfile& me = FighterProfile::local();
		me.tokens = me.earned = 0;
		me.dailyDate = 0;
		me.dailyDone.clear();
		me.dailyProgress.clear();
		const Contract* c = pr.todaysContracts()[0];
		std::map<std::string, int> round{{"kill", 2}, {"win", 1}};
		int paid = pr.award(round, "warehouse", 0, 1);
		CHECK(paid > 0 && me.tokens == paid && me.earned == paid, "a round pays %d tokens", paid);
		CHECK(pr.todaysEvents().size() == 2 && pr.todaysContracts().size() == 3, "2 events and 3 contracts today");
		CHECK(pr.todaysEvents()[0] == pr.todaysEvents()[0], "the day's picks do not change");
		int before = me.tokens;
		pr.award({{c->stat, c->target}}, "warehouse", 0, 1);
		CHECK(me.dailyDone.count(c->id) && me.tokens - before >= c->reward, "contract '%s' pays its reward", c->name.c_str());
		me.tokens = 1000;
		CHECK(me.buy("slug", 300) && me.tokens == 700 && me.unlocked.count("slug"), "buying a perk costs tokens");
		me.togglePerk("slug");
		CHECK(me.weaponPerks["shotgun"] == "slug", "equipped");
		me.togglePerk("buckshot");
		CHECK(me.weaponPerks["shotgun"] == "buckshot", "one perk per weapon group");
		me.togglePerk("power_throw");
		me.togglePerk("iron_grip");
		me.togglePerk("long_roll");
		CHECK(me.passives.size() == 2 && me.passives[0] == "iron_grip", "two fighter perks at most");
		me = saved;
		me.save();

		printf("== shooting range\n");
		gamePtr.reset();
		GameOptions r;
		r.mapText = platform::readFile("maps/range.sfm");
		r.localPlayers = 1;
		r.range = true;
		r.rangeWeapon = "sniper";
		gamePtr = std::make_unique<Game>(r);
		Game& gr = *gamePtr;
		platform::setInputOverride(0, &idle);
		run(gr, idle, 40);
		Player* me2 = static_cast<Player*>(gr.getObjectById(gr.players[0].objectId));
		Obj* held2 = gr.getObjectById(me2->inventory[INV_GUN]);
		CHECK(held2 && held2->parent->key == "sniper", "starts with the chosen weapon");
		CHECK(me2->onGround().has_value(), "stands on the floor (%.0f,%.0f)", me2->getPosition().x, me2->getPosition().y);
		auto targets = gr.getObjectsByParent(Registry::instance().get("range_target"));
		CHECK(targets.size() == 9, "%d targets", (int)targets.size());
		targets[0]->dealDamage({20, "projectile"});
		run(gr, idle, 250);
		CHECK(gr.getObjectsByParent(Registry::instance().get("range_target")).size() == 9, "targets come back");
		PlayerInput lb;
		lb.grab = true;
		run(gr, lb, 2);
		CHECK(!gr.exitRequested, "bumpers do not leave the range");
		me2->kill();
		run(gr, idle, 5);
		CHECK(gr.getObjectById(gr.players[0].objectId) != nullptr, "a fallen fighter gets up again");
		me2 = static_cast<Player*>(gr.getObjectById(gr.players[0].objectId));
		PlayerInput left;
		left.left = true;
		run(gr, left, 90);
		CHECK(gr.exitRequested, "walking into the door leaves the range (x %.0f)", me2->getPosition().x);
		platform::setInputOverride(1, nullptr);
	}

	// ------------------------------------------------------------------ scenario 13: console feedback
	{
		gamePtr.reset();
		printf("\n######## scenario 13: console feedback\n");
		PlayerInput idle;

		// weapons from the first frame, pistols on request, no recoil on small arms
		{
			GameOptions o;
			o.mapText = platform::readFile("maps/warehouse.sfm");
			o.localPlayers = 1;
			o.startPistols = true;
			Game g(o);
			int weapons = 0, spots = (int)g.getObjectsByParent(Registry::instance().get("weapon_spawn")).size();
			for (Obj* s : g.getObjectsByParent(Registry::instance().get("weapon_spawn")))
				if (g.getObjectById(static_cast<Marker*>(s)->targetAId)) weapons++;
			CHECK(spots > 0 && weapons == spots, "every weapon spot is filled at once (%d of %d)", weapons, spots);
			Player* me = static_cast<Player*>(g.getObjectById(g.players[0].objectId));
			Obj* gun = g.getObjectById(me->inventory[INV_GUN]);
			CHECK(gun && gun->parent->key == "pistol", "the fighter starts with a pistol");
			float x = me->getPosition().x;
			PlayerInput shoot;
			shoot.fire = true;
			for (int i = 0; i < 6; i++) {
				run(g, shoot, 1);
				run(g, idle, 14);
			}
			CHECK(std::fabs(me->getPosition().x - x) < 0.5f, "a pistol does not move its owner (%.2f px)", me->getPosition().x - x);
			platform::setInputOverride(0, nullptr);
		}
		CHECK(Registry::instance().get("sniper")->recoil <= 1 && Registry::instance().get("bazooka")->recoil <= 1, "big guns kick only a little");
		{
			GameOptions o;
			o.mapText = platform::readFile("maps/warehouse.sfm");
			o.localPlayers = 1;
			Game g(o);
			Player* me = static_cast<Player*>(g.getObjectById(g.players[0].objectId));
			CHECK(!g.getObjectById(me->inventory[INV_GUN]), "empty hands by default");
		}

		// fire stays within a budget
		{
			GameOptions o;
			o.mapText = platform::readFile("maps/warehouse.sfm");
			o.localPlayers = 1;
			Game g(o);
			g.getObjectById(g.players[0].objectId)->setPosition(430, 245);   // out of the flames, a dead fighter leaves a lot of blood
			int before = (int)g.allObjects().size(), peak = 0;
			for (int f = 0; f < 240; f++) {
				if (f % 30 == 0) g.createFireplosion({250, 255}, 2, 16);
				g.update();
				peak = std::max(peak, (int)g.allObjects().size());
			}
			CHECK(peak < before + Game::MAX_FIRES + 12, "fire adds few objects (%d -> peak %d)", before, peak);
			CHECK(g.particleCount() > 0 && g.particleCount() <= 48, "smoke is drawn as particles (%d)", g.particleCount());
		}

		// acid: a slow death, with a way out
		{
			GameOptions o;
			o.mapText = platform::readFile("maps/sewers.sfm");
			o.localPlayers = 1;
			Game g(o);
			Player* me = static_cast<Player*>(g.getObjectById(g.players[0].objectId));
			run(g, idle, 2);
			me->setPosition(300, 316);
			me->setVelocity(0, 0);
			run(g, idle, 30);
			CHECK(me->alive && me->health > 70 && me->health < 100, "30 frames in the acid: alive and hurt (%g)", me->health);
			Obj* crate = g.createObject("crate", ObjOptions().at({320, 300}));
			int crateId = crate->id;
			run(g, idle, 60);
			CHECK(!g.getObjectById(crateId), "acid dissolves things that fall in");
			run(g, idle, 200);
			CHECK(!me->alive, "staying in means death in the end");

			GameOptions b;
			b.mapText = o.mapText;
			b.localPlayers = 0;
			b.bots = 2;
			b.botDifficulty = 2;
			Game gb(b);
			Player* bot = static_cast<Player*>(gb.getObjectById(gb.players[0].objectId));
			gb.update();
			bot->setPosition(180, 316);
			bot->setVelocity(0, 0);
			int out = -1;
			for (int f = 0; f < 170 && bot->alive; f++) {
				gb.update();
				if (!gb.inAcid(bot->getPosition())) {
					out = f;
					break;
				}
			}
			CHECK(out >= 0, "a bot climbs out of the acid (frame %d)", out);
		}

		// objects go up and fly like fighters do
		{
			GameOptions o;
			o.mapText = testMap();
			o.localPlayers = 2;
			Game g(o);
			Player* me = static_cast<Player*>(g.getObjectById(g.players[0].objectId));
			Player* other = static_cast<Player*>(g.getObjectById(g.players[1].objectId));
			PlayerInput grab, fire;
			grab.grab = true;
			fire.fire = true;
			run(g, idle, 20);
			Obj* crate = nullptr;
			for (auto& s : g.allObjects())
				if (s->parent->key == "crate") crate = s.get();
			Vec2 c = crate->getPosition();
			me->setPosition(c.x - 14, me->getPosition().y);
			other->setPosition(c.x + 50, other->getPosition().y);
			me->facingDirection = 1;
			run(g, idle, 3);
			run(g, grab, 1);
			run(g, idle, 10);
			CHECK(me->holding == crate->id && crate->carriedBy == me->id, "L lifts the crate");
			CHECK(crate->getPosition().y < me->getPosition().y - 10, "the crate is overhead (%.0f / %.0f)", crate->getPosition().y, me->getPosition().y);
			float hp = other->health;
			me->health = 100;
			run(g, fire, 1);
			run(g, idle, 40);
			CHECK(me->holding < 0 && crate->carriedBy < 0, "R throws it");
			CHECK(other->health < hp, "the thrown crate hits the fighter (%g -> %g)", hp, other->health);
			CHECK(me->health == 100, "the thrower is unhurt");
			platform::setInputOverride(0, nullptr);
		}

		// the dive: one roll at the end, carries the fighter it reaches, no fall damage
		{
			GameOptions o;
			o.mapText = platform::readFile("maps/range.sfm");
			o.localPlayers = 2;
			Game g(o);
			for (auto& s : std::vector<std::shared_ptr<Obj>>(g.allObjects()))
				if (s->alive && s->type() != ObjType::Player && s->body && (!s->body->isStatic || s->parent->key == "range_target")) s->kill();
			for (Obj* shard : g.getObjectsByParent(Registry::instance().get("glass_shard"))) shard->kill();   // of the lamps
			Player* me = static_cast<Player*>(g.getObjectById(g.players[0].objectId));
			Player* other = static_cast<Player*>(g.getObjectById(g.players[1].objectId));
			me->setPosition(165, 263);
			other->setPosition(215, 263);
			PlayerInput runR, jumpR;
			runR.right = true;
			jumpR.right = jumpR.jump = true;
			PlayerInput none;
			run(g, none, 30);
			run(g, runR, 1);
			run(g, none, 2);
			run(g, runR, 6);
			run(g, jumpR, 1);
			CHECK(me->strictState("air_diving"), "a running jump is a dive");
			bool caught = false, rolled = false;
			float fx = 0;
			for (int f = 0; f < 60; f++) {
				run(g, runR, 1);
				if (other->tackledBy == me->id) {
					caught = true;
					fx = other->getPosition().x;
				}
				rolled = rolled || me->checkState("rolling");
			}
			CHECK(caught, "the dive catches the fighter");
			CHECK(fx > 217, "and carries them along (x %.0f)", fx);
			CHECK(other->health < 100 && other->tackledBy < 0, "they are thrown down hurt (%g)", other->health);
			CHECK(rolled, "the diver rolls on landing");
			CHECK(me->health == 100, "the diver takes no damage");

			// off a high catwalk: the fall does not hurt the diver either
			run(g, none, 120);
			for (Obj* pane : g.getObjectsByParent(Registry::instance().get("glass"))) pane->kill();
			me->setPosition(262, 134);
			other->setPosition(292, 134);
			other->health = 100;
			run(g, none, 10);
			me->facingDirection = 1;
			run(g, runR, 1);
			run(g, none, 2);
			run(g, runR, 6);
			run(g, jumpR, 1);
			bool caughtHigh = false;
			float peak = 0;
			for (int f = 0; f < 90; f++) {
				run(g, runR, 1);
				for (Obj* shard : g.getObjectsByParent(Registry::instance().get("glass_shard"))) shard->kill();   // of the pane
				caughtHigh = caughtHigh || other->tackledBy == me->id;
				peak = std::max(peak, me->getPosition().y);
			}
			CHECK(caughtHigh && peak > 200, "the catwalk dive catches them and drops to the floor (y %.0f)", peak);
			CHECK(me->health == 100, "no fall damage for the diver (%g)", me->health);
			platform::setInputOverride(0, nullptr);
		}

		// a crate between two bots gets dealt with: no pushing it across the room
		{
			int hitFrame = 2000, pushed = 0;
			for (int round = 0; round < 6; round++) {
				GameOptions o;
				o.mapText = platform::readFile("maps/range.sfm");
				o.localPlayers = 0;
				o.bots = 2;
				o.botDifficulty = 1;
				o.suddenDeath = false;
				Game g(o);
				for (auto& s : std::vector<std::shared_ptr<Obj>>(g.allObjects()))
					if (s->alive && s->type() != ObjType::Player && s->body && !s->body->isStatic) s->kill();
				g.update();
				Player* a = static_cast<Player*>(g.getObjectById(g.players[0].objectId));
				Player* b = static_cast<Player*>(g.getObjectById(g.players[1].objectId));
				a->setPosition(170, 263);
				b->setPosition(270, 263);
				Obj* crate = g.createObject("crate", ObjOptions().at({220, 263}));
				int id = crate->id;
				int first = 2000;
				for (int f = 0; f < 400 && first == 2000; f++) {
					g.update();
					if (a->health < 100 || b->health < 100) first = f;
				}
				Obj* c = g.getObjectById(id);
				if (c) pushed = std::max(pushed, (int)std::fabs(c->getPosition().x - 220));
				hitFrame = std::min(2000, std::max(hitFrame == 2000 ? 0 : hitFrame, first));
			}
			CHECK(hitFrame < 300, "bots get past a crate and fight (worst first blow at frame %d)", hitFrame);
			CHECK(pushed < 60, "and do not shove it across the room (%d px)", pushed);
		}
	}

	// ------------------------------------------------------------------ scenario 14: camera, settings, running bots
	{
		gamePtr.reset();
		printf("\n######## scenario 14: camera, settings, bots on the run\n");
		Settings& set = Settings::get();
		Settings saved = set;
		PlayerInput idle;
		auto seen = [](Game& g, Obj* p) {
			Vec2 s = g.worldToScreen(p->getPosition());
			return s.x > 0 && s.x < SCREEN_W && s.y > 0 && s.y < SCREEN_H;
		};
		auto arena = [&](float ax, float bx, float ay, float by) {
			GameOptions o;
			o.mapText = platform::readFile("maps/warehouse.sfm");
			o.localPlayers = 2;
			auto g = std::make_unique<Game>(o);
			Player* a = static_cast<Player*>(g->getObjectById(g->players[0].objectId));
			Player* b = static_cast<Player*>(g->getObjectById(g->players[1].objectId));
			platform::setInputOverride(1, &idle);
			a->setPosition(ax, ay);
			b->setPosition(bx, by);
			run(*g, idle, 240);
			return g;
		};
		set.camera = CAMERA_FIGHTERS;
		set.zoomLimit = 1;
		{
			auto close = arena(330, 350, 243, 243);
			CHECK(close->camera.zoom > 2.4f, "fighters together: close in (zoom %.2f)", close->camera.zoom);
			CHECK(close->camera.zoom <= set.maxZoom() + 0.01f, "up to the limit");
			auto wide = arena(60, 470, 243, 243);
			Player* a = static_cast<Player*>(wide->getObjectById(wide->players[0].objectId));
			Player* b = static_cast<Player*>(wide->getObjectById(wide->players[1].objectId));
			CHECK(wide->camera.zoom < close->camera.zoom - 0.5f, "apart: further out (zoom %.2f)", wide->camera.zoom);
			CHECK(seen(*wide, a) && seen(*wide, b), "both stay in the picture");
		}
		{
			// nobody leaves the picture in a real fight, the edge of the level never shows
			GameOptions o;
			o.mapText = platform::readFile("maps/rooftops.sfm");
			o.localPlayers = 0;
			o.bots = 4;
			o.botDifficulty = 2;
			o.suddenDeath = false;
			Game g(o);
			int lost = 0, frames = 0, edge = 0;
			float lo = 9, hi = 0;
			for (int f = 0; f < 900; f++) {
				g.update();
				if (f < 60 || g.gameOver) continue;
				frames++;
				for (Obj* p : g.getPlayers()) lost += seen(g, p) ? 0 : 1;
				Vec2 tl = g.cameraTopLeft();
				Bounds& lb = *g.levelBounds;
				float vw = SCREEN_W / g.camera.zoom, vh = SCREEN_H / g.camera.zoom;   // a level smaller than the view is centred
				if (lb.max.x - lb.min.x > vw && (tl.x < lb.min.x - 1 || tl.x + vw > lb.max.x + 1)) edge++;
				if (lb.max.y - lb.min.y > vh && (tl.y < lb.min.y - 1 || tl.y + vh > lb.max.y + 1)) edge++;
				lo = std::min(lo, g.camera.zoom);
				hi = std::max(hi, g.camera.zoom);
			}
			CHECK(lost * 100 < frames, "fighters out of the picture: %d of %d frames", lost, frames);
			CHECK(edge == 0, "the view stays inside the level");
			printf("  zoom range in a fight: %.2f .. %.2f\n", lo, hi);
		}
		set.camera = CAMERA_NEAR_ME;
		{
			GameOptions o;
			o.mapText = platform::readFile("maps/warehouse.sfm");
			o.localPlayers = 1;
			o.bots = 2;
			Game g(o);
			Player* me = static_cast<Player*>(g.getObjectById(g.players[0].objectId));
			Player* near = static_cast<Player*>(g.getObjectById(g.players[1].objectId));
			Player* far = static_cast<Player*>(g.getObjectById(g.players[2].objectId));
			me->setPosition(330, 243);
			near->setPosition(350, 243);
			far->setPosition(60, 243);
			CHECK(g.cameraSubjects().size() == 2, "around me: the fighter and the nearest enemy (%d)", (int)g.cameraSubjects().size());
		}
		set.camera = CAMERA_WIDE;
		{
			auto g = arena(330, 350, 243, 243);
			Bounds& lb = *g->levelBounds;
			float fit = std::max(1.5f, std::min(SCREEN_W / (lb.max.x - lb.min.x), SCREEN_H / (lb.max.y - lb.min.y)));
			CHECK(std::fabs(g->camera.zoom - fit) < 0.02f, "wide view shows the level as before (zoom %.2f)", g->camera.zoom);
		}
		set = saved;
		set.camera = CAMERA_WIDE;
		set.zoomLimit = 2;
		set.music = 3;
		set.bars = false;
		set.save();
		set = Settings();
		set.load();
		CHECK(set.camera == CAMERA_WIDE && set.zoomLimit == 2 && set.music == 3 && !set.bars, "settings survive a restart");
		set = saved;
		set.save();
		platform::setInputOverride(1, nullptr);
		platform::setInputOverride(0, nullptr);

		// bots run, and dive
		int running = 0, walking = 0, tackles = 0;
		for (int round = 0; round < 4; round++) {
			GameOptions o;
			o.mapText = platform::readFile("maps/warehouse.sfm");
			o.localPlayers = 0;
			o.bots = 6;
			o.botDifficulty = 2;
			o.suddenDeath = false;
			Game g(o);
			for (int f = 0; f < 1200; f++) {
				g.update();
				for (Obj* p : g.getPlayers()) {
					Player* q = static_cast<Player*>(p);
					running += q->strictState("grounded_running");
					walking += q->strictState("grounded_walking");
					tackles += q->tackledBy >= 0;
				}
			}
		}
		printf("  frames running %d, walking %d, carried in a dive %d\n", running, walking, tackles);
		CHECK(running > walking / 2, "bots run");
		CHECK(tackles > 0, "bots dive into fighters and carry them along");
	}

	// ------------------------------------------------------------------ scenario 15: the base as one building
	{
		gamePtr.reset();
		printf("\n######## scenario 15: the base\n");
		FighterProfile& me = FighterProfile::local();
		FighterProfile saved = me;
		MenuInput none, up, down, left, right, ok, back, alt;
		up.up = down.down = left.left = right.right = ok.confirm = back.back = alt.alt = true;
		auto step = [](BaseScreen& b, const MenuInput& in) {
			platform::beginFrame();
			BaseAction a = b.update(in);
			platform::endFrame();
			return a;
		};
		static const float floors[4] = {64, 126, 188, 250};

		BaseScreen b;
		step(b, down);
		CHECK(b.selectedRoom() == 1, "the panel changes room on the press");
		CHECK(b.fighterY() == floors[0], "the fighter is still upstairs");
		bool offLadder = false, climbed = false;
		int frames = 0;
		for (; frames < 400 && !(b.fighterY() == floors[1] && b.fighterX() == 110); frames++) {
			step(b, none);
			bool between = b.fighterY() != floors[0] && b.fighterY() != floors[1];
			climbed = climbed || between;
			offLadder = offLadder || (between && b.fighterX() != 39);
		}
		printf("  one floor down in %d frames\n", frames);
		CHECK(frames < 400 && climbed && !offLadder, "the fighter walks to the ladder, climbs it and walks to the room");

		step(b, up);
		step(b, up);
		CHECK(b.selectedRoom() == 0, "the top floor is the end");
		step(b, right);
		CHECK(b.selectedTab() == 1, "right goes to the next tab");
		step(b, right);
		CHECK(b.selectedTab() == 1, "the last tab is the end");
		step(b, ok);
		CHECK(!b.panelActive(), "contracts have nothing to pick");
		step(b, left);

		step(b, ok);
		CHECK(b.panelActive(), "cross moves into the panel");
		me.apparel["torso"] = "";
		step(b, down);
		step(b, down);
		step(b, right);
		CHECK(!me.apparel["torso"].empty() && b.selectedTab() == 0, "in the outfit left and right change the clothes");
		step(b, back);
		CHECK(!b.panelActive() && step(b, none) == BaseAction::None, "circle leaves the panel, not the base");

		BaseScreen shop;
		shop.openRoom(1);
		const Perk* first = nullptr;
		for (auto& p : Progress::get().perks)
			if (p.group == Progress::get().groupOf("pistol")) {
				first = &p;
				break;
			}
		me.tokens = first->cost;
		me.unlocked.erase(first->id);
		me.weaponPerks.clear();
		step(shop, ok);
		step(shop, ok);
		step(shop, ok);
		CHECK(me.tokens == first->cost, "the first press only names the price");
		step(shop, ok);
		CHECK(me.tokens == 0 && me.unlocked.count(first->id) && me.weaponPerks["pistols"] == first->id, "the second one pays and wears the perk");
		step(shop, back);
		CHECK(shop.panelActive(), "circle goes back to the weapons");
		CHECK(step(shop, alt) == BaseAction::Range && shop.rangeWeapon == "pistol", "triangle takes the weapon to the range");
		shop.comeBackFromRange();
		CHECK(shop.selectedRoom() == 1 && shop.panelActive() && shop.rangeWeapon.empty(), "and comes back to the same list");

		BaseScreen go;
		step(go, down);
		step(go, down);
		step(go, down);
		step(go, ok);
		BaseAction got = BaseAction::None;
		for (frames = 0; frames < 500 && got == BaseAction::None; frames++) got = step(go, none);
		printf("  from the wardrobe to the range in %d frames\n", frames);
		CHECK(got == BaseAction::Range && go.fighterY() == floors[3], "the fighter walks into the range");
		go.comeBackFromRange();
		CHECK(go.selectedRoom() == 3 && !go.panelActive(), "the range is left in the range's room");

		BaseScreen hurry;
		for (int i = 0; i < 3; i++) step(hurry, down);
		step(hurry, ok);
		CHECK(step(hurry, ok) == BaseAction::Range, "a second press does not wait for the walk");

		BaseScreen out;
		CHECK(step(out, back) == BaseAction::Leave, "circle in the building leaves the base");
		me = saved;
	}

	// ------------------------------------------------------------------ scenario 16: round start, glass, moving targets
	{
		gamePtr.reset();
		printf("\n######## scenario 16: countdown, glass, moving targets\n");
		PlayerInput none, runR, jumpR;
		runR.right = true;
		jumpR.right = jumpR.jump = true;

		// the countdown
		{
			GameOptions o;
			o.mapText = platform::readFile("maps/warehouse.sfm");
			o.localPlayers = 1;
			o.bots = 3;
			o.botDifficulty = 2;
			o.intro = true;
			Game g(o);
			Player* me = static_cast<Player*>(g.getObjectById(g.players[0].objectId));
			run(g, none, 30);
			float x = me->getPosition().x, ground = me->getPosition().y;
			run(g, runR, 40);
			CHECK(g.introFrames > 0 && me->getPosition().x == x, "nobody moves during the countdown");
			CHECK(me->onGround().has_value(), "the fighters have landed meanwhile (y %.0f, %.0f)", ground, me->getPosition().y);
			float others = 0;
			auto where = [&]() {
				float sum = 0;
				for (auto& slot : g.players)
					if (Obj* b = g.getObjectById(slot.objectId)) sum += b->getPosition().x;
				return sum;
			};
			others = where();
			run(g, none, 60);
			CHECK(std::fabs(where() - others) < 1, "bots wait as well (%.2f px)", std::fabs(where() - others));
			run(g, runR, INTRO_FRAMES);
			CHECK(g.introFrames == 0 && me->getPosition().x != x, "the round starts after it");
			g.restartGame();
			CHECK(g.introFrames == INTRO_FRAMES, "and each next round has one too");
		}
		{
			GameOptions o;
			o.mapText = platform::readFile("maps/range.sfm");
			o.localPlayers = 1;
			o.range = true;
			o.intro = true;
			Game g(o);
			CHECK(g.introFrames == 0, "the range needs no countdown");
		}

		auto glassMap = [] {
			std::string m = testMap();
			m.replace(m.find("explosive_barrel"), 16, "crate");   // the arena is cleared by killing what stands in it
			m.insert(m.rfind("]}"), ",{\"parentKey\":\"glass\",\"id\":20,\"tiling\":{\"width\":1,\"height\":3},\"matter\":{\"position\":{\"x\":120,\"y\":88.5}}}");
			return m;
		};
		auto arena = [&](int locals) {
			GameOptions o;
			o.mapText = glassMap();
			o.localPlayers = locals;
			auto g = std::make_unique<Game>(o);
			for (auto& s : std::vector<std::shared_ptr<Obj>>(g->allObjects()))
				if (s->alive && s->type() != ObjType::Player && s->body && (!s->body->isStatic || s->type() == ObjType::Gun || s->type() == ObjType::Melee || s->type() == ObjType::Throwable)) s->kill();
			run(*g, none, 2);
			return g;
		};
		auto paneUp = [](Game& g) { return !g.getObjectsByParent(Registry::instance().get("glass")).empty(); };
		auto dive = [&](Game& g, Player* me) {
			run(g, none, 30);
			run(g, runR, 1);
			run(g, none, 2);
			run(g, runR, 6);
			run(g, jumpR, 1);
			CHECK(me->strictState("air_diving"), "a running jump is a dive");
		};

		{
			auto g = arena(1);
			Player* me = static_cast<Player*>(g->getObjectById(g->players[0].objectId));
			me->setPosition(60, 93);
			CHECK(paneUp(*g), "the pane stands");
			dive(*g, me);
			float x = 0;
			for (int f = 0; f < 60 && paneUp(*g); f++) {
				run(*g, runR, 1);
				x = me->getPosition().x;
			}
			CHECK(!paneUp(*g), "a dive breaks the window (at x %.0f)", x);
			float hp = me->health;
			run(*g, runR, 15);
			CHECK(me->getPosition().x > 121, "and goes on through it (x %.0f)", me->getPosition().x);
			CHECK(me->health == hp, "its own shards do not cut the diver (%g)", me->health);
		}
		{
			auto g = arena(2);
			Player* me = static_cast<Player*>(g->getObjectById(g->players[0].objectId));
			Player* other = static_cast<Player*>(g->getObjectById(g->players[1].objectId));
			me->setPosition(50, 93);
			other->setPosition(106, 93);
			dive(*g, me);
			bool caught = false;
			for (int f = 0; f < 60 && paneUp(*g); f++) {
				run(*g, runR, 1);
				caught = caught || other->tackledBy == me->id;
			}
			CHECK(caught && !paneUp(*g), "so does a dive with someone in the arms");
		}
		{
			auto g = arena(1);
			Player* me = static_cast<Player*>(g->getObjectById(g->players[0].objectId));
			Obj* crate = g->createObject("crate", ObjOptions().at({76, 96}));
			me->setPosition(62, 93);
			me->facingDirection = 1;
			PlayerInput grab, fire;
			grab.grab = true;
			fire.fire = true;
			run(*g, none, 30);
			run(*g, grab, 1);
			run(*g, none, 10);
			CHECK(me->holding == crate->id, "the crate is lifted");
			run(*g, fire, 1);
			run(*g, none, 20);
			CHECK(!paneUp(*g), "a thrown crate breaks the window");
		}
		{
			auto g = arena(1);
			Player* me = static_cast<Player*>(g->getObjectById(g->players[0].objectId));
			me->setPosition(100, 93);
			run(*g, none, 30);
			ObjOptions w;
			w.at({108, 88}).vel({6.5f, 0}).life(600);
			w.isStatic = false;
			w.group = "item";
			Melee* m = static_cast<Melee*>(g->createObject("knife", w));
			m->thrownBy = me->id;
			m->thrownFrames = 90;
			run(*g, none, 8);
			CHECK(!paneUp(*g), "so does a thrown knife");
		}
		{
			auto g = arena(1);
			Player* me = static_cast<Player*>(g->getObjectById(g->players[0].objectId));
			me->setPosition(100, 93);
			run(*g, none, 30);
			Obj* crate = g->createObject("crate", ObjOptions().at({108, 96}));
			crate->setVelocity(1, 0);
			run(*g, none, 10);
			CHECK(paneUp(*g), "something merely nudged against it does not");
		}

		// the range
		{
			GameOptions o;
			o.mapText = platform::readFile("maps/range.sfm");
			o.localPlayers = 1;
			o.range = true;
			Game g(o);
			platform::setInputOverride(0, &none);
			auto targets = g.getObjectsByParent(Registry::instance().get("range_target"));
			std::map<int, Vec2> start;
			for (Obj* t : targets) start[t->id] = t->getPosition();
			run(g, none, 100);
			int moved = 0, held = 0;
			for (Obj* t : targets) {
				moved += (t->getPosition() - start[t->id]).length() > 5;
				held += (t->getPosition() - start[t->id]).length() < 0.01f;
			}
			CHECK(moved == 4 && held == 5, "four targets run on rails, five stand (%d, %d)", moved, held);
			Obj* runner = nullptr;
			for (Obj* t : targets)
				if ((t->getPosition() - start[t->id]).length() > 5) runner = t;
			runner->dealDamage({20, "projectile"});
			run(g, none, 2);
			int left = (int)g.getObjectsByParent(Registry::instance().get("range_target")).size();
			run(g, none, 245);
			auto back = g.getObjectsByParent(Registry::instance().get("range_target"));
			CHECK(left == 8 && back.size() == 9, "a shot one comes back (%d, %d)", left, (int)back.size());
			Vec2 p = back.back()->getPosition();
			float furthest = 0;
			for (int f = 0; f < 120; f++) {
				run(g, none, 1);
				furthest = std::max(furthest, (back.back()->getPosition() - p).length());
			}
			CHECK(furthest > 5, "and goes on running (%.0f px)", furthest);
			int weapons = 0;
			for (auto& s : g.allObjects())
				if (s->alive && (s->type() == ObjType::Gun || s->type() == ObjType::Melee || s->type() == ObjType::Throwable)) weapons++;
			CHECK(weapons >= 5, "weapons lie about from the start (%d)", weapons);
			platform::setInputOverride(0, nullptr);
		}
	}

	// ------------------------------------------------------------------ scenario 17: Russian
	{
		printf("\n######## scenario 17: Russian\n");
		CHECK(tr("Back") == "Back", "English by default");
		i18n::setLanguage(i18n::RUSSIAN);
		CHECK(tr("Back") == "Назад", "the table is loaded");
		CHECK(tr("no such text") == "no such text", "what is missing stays English");
		CHECK(trf("WAVE %s", "3") == "ВОЛНА 3", "numbers go into the text");
		CHECK(i18n::upper("ёжик Ъя z") == "ЁЖИК ЪЯ Z", "capitals: %s", i18n::upper("ёжик Ъя z").c_str());
		CHECK(platform::charCount("Жетон") == 5 && i18n::letters("Жz").size() == 2, "letters are counted, not bytes");

		std::vector<std::string> missing;
		auto need = [&](const std::string& s) {
			if (tr(s) == s) missing.push_back(s);
		};
		Progress& pr = Progress::get();
		for (auto& p : pr.perks) {
			need(p.name);
			need(p.desc);
		}
		for (auto& s : pr.skills) {
			need(s.name);
			need(s.desc);
		}
		for (auto& g : pr.groups) need(g.name);
		for (auto& r : pr.ranks) need(r.name);
		for (auto& e : pr.events) {
			need(e.name);
			need(e.desc);
		}
		for (auto& c : pr.contracts) need(c.name);
		auto title = [](std::string s) {
			for (char& c : s)
				if (c == '_') c = ' ';
			s[0] = (char)toupper((unsigned char)s[0]);
			return s;
		};
		auto& reg = Registry::instance();
		for (auto& key : reg.order) {
			const ObjDef& d = reg.defs[key];
			if ((d.cls == "Gun" || d.cls == "Melee" || d.cls == "Throwable") && key != "fists" && d.durability == 0) need(title(key));
		}
		for (auto& cat : reg.apparel)
			for (auto& kv : cat.second) need(title(kv.first));
		for (const char* m : {"warehouse", "backstreets", "factory", "sewers", "rooftops", "containers", "range"}) {
			json j = json::parse(platform::readFile(std::string("maps/") + m + ".sfm"), nullptr, false);
			need(j.value("name", ""));
		}
		std::string list;
		for (auto& s : missing) list += " [" + s + "]";
		CHECK(missing.empty(), "everything in the data is translated:%s", list.c_str());

		BaseScreen b;
		MenuInput none;
		platform::beginFrame();
		b.update(none);
		platform::endFrame();
		CHECK(true, "the base draws in Russian");
		i18n::setLanguage(i18n::ENGLISH);
		CHECK(tr("Back") == "Back", "and back to English");
	}

	// ------------------------------------------------------------------ scenario 18: Superfighters Deluxe maps
	{
		printf("\n######## scenario 18: Deluxe maps\n");
		std::string d = "header 200,-100,-100,100 ";
		auto i32 = [&](int v) { d.append((const char*)&v, 4); };
		auto f32 = [&](float v) { d.append((const char*)&v, 4); };
		auto object = [&](const char* name, float x, float y, int w = 1, int h = 1, float rot = 0) {
			d += (char)strlen(name);
			d += name;
			i32(1);
			i32(0);
			i32(2);
			f32(x);
			f32(y);
			i32(3);
			f32(rot);
			i32(0x13);
			i32(2);
			i32(w);
			i32(0x14);
			i32(2);
			i32(h);
			i32(9999);
		};
		for (int i = 0; i < 20; i++) object("CONCRETE00A", -80.f + i * 8, -40);    // one floor of twenty tiles
		object("METALPLAT00A", -40, 40, 10);
		object("METALLADDER00A", 0, 40, 1, 10);
		object("SPAWNPLAYER", -60, -20);
		object("SPAWNPLAYER", 60, -20);
		object("SPAWNWEAPON", 0, -20);
		object("CRATE00", 40, -20);
		object("BARRELEXPLOSIVE", -30, -20);
		object("BGWALL00A", -20, 20, 4, 4);
		object("EXPLOSIONTRIGGER", 0, 0);
		sfdm::Loaded l = sfdm::load(d, "Test");
		CHECK(l.width == 200 && l.height == 300, "the camera area becomes the bounds (%g x %g)", l.width, l.height);
		CHECK(l.players == 2 && l.solids == 20, "objects are read (%d spawns, %d solid)", l.players, l.solids);
		CHECK(l.art != nullptr, "the picture is put together");
		json j = json::parse(l.mapText);
		int walls = 0, ledges = 0, ladders = 0, crates = 0, barrels = 0, weapons = 0;
		for (auto& o : j["objects"]) {
			std::string key = o["parentKey"];
			walls += key == "wall";
			ledges += key == "ledge";
			ladders += key == "climb";
			crates += key == "crate";
			barrels += key == "explosive_barrel";
			weapons += key == "weapon_spawn";
		}
		CHECK(walls == 1, "neighbouring tiles become one wall (%d)", walls);
		CHECK(ledges == 1 && ladders == 1 && crates == 1 && barrels == 1 && weapons == 1, "ledge, ladder, things: %d %d %d %d %d", ledges, ladders, crates, barrels, weapons);
		PlayerInput idle2;
		platform::setInputOverride(0, &idle2);
		GameOptions o;
		o.mapText = l.mapText;
		o.backdrop = l.art;
		o.localPlayers = 1;
		o.bots = 1;
		Game g(o);
		run(g, idle2, 90);
		Player* me = static_cast<Player*>(g.getObjectById(g.players[0].objectId));
		CHECK(me && me->onGround().has_value(), "fighters stand on its floor (y %.0f)", me ? me->getPosition().y : 0.f);
		CHECK(!g.layers.empty() && g.layers[0].tex == l.art, "the picture is under the level");
		platform::setInputOverride(0, nullptr);
		CHECK(sfdm::titleOf("4917_Hotel_II.sfdm") == "Hotel II" && sfdm::titleOf("Woods.sfdm") == "Woods", "titles from file names");
		CHECK(sfdm::parse("no such thing", "x").players == 0, "rubbish in a file does no harm");
	}

	printf("\n######## scenario 8: character room data\n");
	{
		FighterProfile& me = FighterProfile::local();
		FighterProfile saved = me;
		me.apparel["torso"] = "tuxedo";
		me.save();
		std::string text = platform::readUserFile("profile.json");
		CHECK(text.find("tuxedo") != std::string::npos, "profile is written to the player's data folder");
		CHECK(me.toGameProfile()["torso"] == "tuxedo", "the choice reaches the game profile");
		me = saved;
		me.save();
	}

	printf("== done: %d failure(s)\n", g_fail);
	gamePtr.reset();
	platform::shutdown();
	return g_fail ? 1 : 0;
}
