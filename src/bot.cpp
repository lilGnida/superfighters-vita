// Bot brain: target selection, weapon pickup, shooting with reaction time and aim error,
// melee, grenades, ladders, platform drops, edge (acid) avoidance and unsticking.
#include "bot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game.h"
#include "progress.h"

namespace {

bool isWall(Game& g, Body* b) {
	if (!b->isStatic) return false;
	Obj* o = g.getObjectByBody(b);
	if (!o) return true;
	// glass is see-through (and gets shot to pieces), ledges and ladders do not block shots
	if (o->type() == ObjType::Platform || o->type() == ObjType::Ladder || o->type() == ObjType::Door) return false;
	if (o->parent && o->parent->key == "glass") return false;
	return o->type() == ObjType::Base;
}

bool isFloor(Game& g, Body* b) {
	if (!b->isStatic) return false;
	Obj* o = g.getObjectByBody(b);
	if (!o) return true;
	return o->type() == ObjType::Base || o->type() == ObjType::Platform;
}

template <class T>
T* held(Game& g, Player& self, int slot, ObjType t) {
	Obj* o = g.getObjectById(self.inventory[slot]);
	return (o && o->type() == t) ? static_cast<T*>(o) : nullptr;
}

}  // namespace

Bot::Bot(BotDifficulty d, unsigned seed) : rng(seed) {
	switch (d) {
		case BotDifficulty::Easy: p = {45, 0.24f, 40, 26, 0.15f, 0.08f, 24, 0.35f, 0.01f}; break;
		case BotDifficulty::Normal: p = {24, 0.12f, 22, 14, 0.35f, 0.15f, 14, 0.75f, 0.05f}; break;
		case BotDifficulty::Hard: p = {9, 0.045f, 12, 9, 0.6f, 0.22f, 8, 1.0f, 0.12f}; break;
	}
}

bool Bot::lineOfSight(Game& g, float ax, float ay, float bx, float by) {
	return !g.physics().raycast({ax, ay}, {bx, by}, [&](Body* b) { return isWall(g, b); });
}

// Is there ground (not acid / not the void) in front of us?
bool Bot::safeAhead(Game& g, Player& self, int dir) {
	Vec2 me = self.getPosition();
	float x = me.x + dir * 10;
	float maxDrop = 160;
	Vec2 hit;
	bool found = g.physics().raycast({x, me.y}, {x, me.y + maxDrop}, [&](Body* b) { return isFloor(g, b); }, &hit);
	if (!found) return false;
	if (g.deathY && hit.y > *g.deathY - 4) return false;
	if (g.inAcid({x, hit.y - 2})) return false;
	return true;
}

Player* Bot::pickTarget(Game& g, Player& self) {
	Vec2 me = self.getPosition();
	Player* best = nullptr;
	float bestScore = 1e9f;
	for (Obj* o : g.getPlayers()) {
		if (o == &self || !o->alive) continue;
		if (self.team > 0 && static_cast<Player*>(o)->team == self.team) continue;
		Vec2 q = o->getPosition();
		float d = (q - me).length();
		if (static_cast<Player*>(o)->hiddenUntil > g.frameCounter && d > 40) continue;   // lost in the smoke
		bool see = lineOfSight(g, me.x, me.y - 4, q.x, q.y - 4);
		float score = d * (see ? 0.6f : 1.0f) + std::fabs(q.y - me.y) * 0.8f;
		if (o->id == targetId) score *= 0.7f;   // stick with the current one
		if (score < bestScore) {
			bestScore = score;
			best = static_cast<Player*>(o);
		}
	}
	return best;
}

// Weapon lying around worth walking to
Obj* Bot::pickItem(Game& g, Player& self) {
	Gun* gun = held<Gun>(g, self, INV_GUN, ObjType::Gun);
	bool wantGun = !gun || gun->ammo <= 0;
	Vec2 me = self.getPosition();
	Obj* best = nullptr;
	float bestD = 160;
	for (auto& sp : g.allObjects()) {
		Obj* o = sp.get();
		if (!o->alive) continue;
		bool ok = false;
		if (o->type() == ObjType::Gun) ok = wantGun && static_cast<Gun*>(o)->holderId < 0 && static_cast<Gun*>(o)->ammo > 0;
		else if (o->type() == ObjType::Throwable)
			ok = static_cast<Throwable*>(o)->holderId < 0 && !held<Throwable>(g, self, INV_THROWABLE, ObjType::Throwable);
		else if (o->type() == ObjType::Melee)
			ok = static_cast<Melee*>(o)->holderId < 0 && self.inventory[INV_MELEE] >= 0 &&
				 g.getObjectById(self.inventory[INV_MELEE]) &&
				 g.getObjectById(self.inventory[INV_MELEE])->parent->key == "fists";
		if (!ok) continue;
		Vec2 q = o->getPosition();
		if (std::fabs(q.y - me.y) > 24) continue;   // same floor only
		float d = std::fabs(q.x - me.x);
		if (d < bestD) {
			bestD = d;
			best = o;
		}
	}
	return best;
}

void Bot::aimAt(Game& g, Player& self, float tx, float ty, float lob, PlayerInput& in) {
	Vec2 me = self.getPosition();
	if (g.frameCounter >= aimOffsetUntil) {
		aimOffset = (rnd() * 2 - 1) * p.aimError;
		aimOffsetUntil = g.frameCounter + 30 + (int)(rnd() * 30);
	}
	float a = std::atan2(ty - (me.y - 2), tx - me.x) + aimOffset;
	if (lob != 0) {
		// throw upwards a bit, on the side of the target
		float side = std::cos(a) >= 0 ? 1.f : -1.f;
		a = std::atan2(std::sin(a) - lob, std::cos(a));
		if ((std::cos(a) >= 0 ? 1.f : -1.f) != side) a = side > 0 ? -1.2f : PI_F + 1.2f;
	}
	in.stickAim = true;
	in.stickX = std::cos(a);
	in.stickY = std::sin(a);
}

// Follow the navigation graph towards (gx, gy); falls back to simple steering
void Bot::moveTowards(Game& g, Player& self, float gx, float gy, PlayerInput& in) {
	Vec2 me = self.getPosition();
	Vec2 feet{me.x, me.y + 9.5f};
	const NavGraph& nav = g.nav;
	bool grounded = self.onGround().has_value();
	bool climbing = self.checkState("climbing");

	if (g.frameCounter >= runRollAt) {
		wantRun = rnd() < p.runRate;
		runRollAt = g.frameCounter + 50 + (int)(rnd() * 70);
	}

	auto hold = [&](int dir) {
		if (dir > 0) in.right = true;
		if (dir < 0) in.left = true;
	};
	auto towards = [&](float x, float tol = 2.5f) {
		float dx = x - me.x;
		int dir = std::fabs(dx) <= tol ? 0 : (dx > 0 ? 1 : -1);
		hold(dir);
		// two pixels a frame would never settle on a point: the last stretch is walked
		if (wantRun && std::fabs(dx) > 22) in.run = true;
		return dir == 0;
	};

	// Mid-air: steer to where the jump / drop was meant to land
	// (resting on the corner of something counts as air too, but must not stop the bot for good)
	if (!grounded && !climbing && std::fabs(self.getVelocity().y) > 0.05f) {
		if (airTargetX > -1e8f) towards(airTargetX, 1.5f);
		return;
	}

	if (!nav.empty()) {
		int cur = climbing && curNode >= 0 ? curNode : nav.nearest(feet, 40);
		int goal = nav.nearest({gx, gy + 9.5f}, 80);
		if (cur >= 0 && goal >= 0) {
			curNode = cur;
			bool onPath = std::find(route.begin(), route.end(), cur) != route.end();
			if (goal != routeGoal || !onPath || g.frameCounter >= repathAt) {
				route = nav.path(cur, goal);
				routeGoal = goal;
				repathAt = g.frameCounter + 45;
			}
			auto it = std::find(route.begin(), route.end(), cur);
			if (it != route.end() && it + 1 != route.end()) {
				int next = *(it + 1);
				const NavEdge* e = nav.edge(cur, next);
				const NavNode& A = nav.nodes[cur];
				const NavNode& B = nav.nodes[next];
				airTargetX = -1e9f;
				snprintf(debug, sizeof debug, "route %d->%d (%.0f,%.0f)->(%.0f,%.0f) kind=%d len=%d", cur, next, A.x, A.y, B.x, B.y,
						 e ? (int)e->kind : -1, (int)route.size());
				if (e) switch (e->kind) {
					case NavEdge::Walk: {
						// run along consecutive walk steps without stopping at every point
						float x = B.x;
						for (auto j = it + 1; j + 1 != route.end(); ++j) {
							const NavEdge* w = nav.edge(*j, *(j + 1));
							if (!w || w->kind != NavEdge::Walk) break;
							x = nav.nodes[*(j + 1)].x;
						}
						towards(x, 0.5f);
						break;
					}
					case NavEdge::Jump:
						if (towards(A.x, 2.0f) && grounded) {
							in.jump = tap(lastJump, true);
							airTargetX = B.x;
						}
						break;
					case NavEdge::WalkOff:
						airTargetX = B.x;
						towards(B.x + (B.x > A.x ? 4 : -4), 0.5f);
						break;
					case NavEdge::Drop: {
						if (!towards(A.x, 2.5f)) break;
						int t = g.frameCounter - dropFrame;
						if (t > 30) {
							dropFrame = g.frameCounter;
							t = 0;
						}
						in.down = (t == 0) || (t >= 3 && t < 14);
						airTargetX = B.x;
						break;
					}
					case NavEdge::Climb: {
						Obj* ladder = g.getObjectById(A.ladderId);
						float lx = ladder ? (ladder->getBounds().min.x + ladder->getBounds().max.x) / 2 : A.x;
						bool up = B.y < A.y;
						if (!climbing) {
							if (towards(lx, 1.5f)) {
								if (up) in.up = true;
								else in.jump = tap(lastJump, true);   // hop onto the ladder, then climb down
							}
						} else {
							if (up) in.up = true;
							else in.down = true;
							// at the top: step off towards the next point
							if (up && me.y + 9.5f <= B.y + 2) towards(B.x + (B.x >= lx ? 6 : -6));
							if (!up && grounded) curNode = next;
						}
						if (climbing && std::fabs(me.y + 9.5f - B.y) < 3) curNode = next;
						break;
					}
					case NavEdge::Door:
						if (towards(A.x, 3.0f)) {
							// doors work on the move: push on towards the wall behind them
							Obj* d = g.getObjectById(A.doorId);
							int into = d ? static_cast<Door*>(d)->into() : 0;
							hold(into != 0 ? into : self.facingDirection);
						}
						break;
				}
				unstick(g, self, in);
				return;
			}
			snprintf(debug, sizeof debug, "cur=%d goal=%d route=%d", cur, goal, (int)route.size());
			// on the goal's surface: walk straight at it
			if (cur == goal) {
				towards(gx, 3.0f);
				unstick(g, self, in);
				return;
			}
		}
	}

	snprintf(debug, sizeof debug, "steer (no route)");
	// No graph / no route: plain steering
	float dx = gx - me.x;
	int dir = std::fabs(dx) < 4 ? 0 : (dx > 0 ? 1 : -1);
	if (dir != 0 && !safeAhead(g, self, dir)) dir = 0;
	hold(dir);
	if (gy < me.y - 24 && g.frameCounter >= nextJump && grounded) {
		in.jump = tap(lastJump, true);
		nextJump = g.frameCounter + 40 + (int)(rnd() * 40);
	}
	unstick(g, self, in);
}

// Walking into something for a while: break glass, hop, kick, then try the other way
void Bot::unstick(Game& g, Player& self, PlayerInput& in) {
	Vec2 me = self.getPosition();
	int dir = in.right ? 1 : (in.left ? -1 : 0);
	if (g.frameCounter < reverseUntil) {
		in.left = reverseDir < 0;
		in.right = reverseDir > 0;
	}

	// a window pane right in front: punch through it
	if (dir != 0 && self.onGround()) {
		Bounds ahead{{me.x + dir * 4 - 3, me.y - 8}, {me.x + dir * 4 + 9 * dir + 3, me.y + 8}};
		if (dir < 0) std::swap(ahead.min.x, ahead.max.x);
		for (Obj* o : g.getObjectsByAABB(ahead)) {
			if (o->parent && o->parent->key == "glass" && o->alive) {
				in.left = in.right = false;
				if (self.facingDirection != dir) {
					in.right = dir > 0;
					in.left = dir < 0;
				} else {
					in.punch = tap(lastPunch, true);
				}
				return;
			}
		}
	}

	if (dir != 0 && self.onGround() && obstacle(g, self, dir, in)) return;

	if (g.frameCounter % 30 == 0) {
		bool moving = dir != 0;
		bool stuck = moving && std::fabs(me.x - stuckX) < 3 && self.onGround();
		stuckX = me.x;
		stuckFrames = stuck ? stuckFrames + 1 : 0;
	}
	if (stuckFrames > 0 && dir != 0) {
		int phase = g.frameCounter % 30;
		if (stuckFrames <= 2 && phase == 0) in.jump = tap(lastJump, true);
		if (stuckFrames == 2 && phase == 15) in.kick = tap(lastKick, true);
		if (stuckFrames >= 3) {
			reverseDir = -dir;
			reverseUntil = g.frameCounter + 45;
			stuckFrames = 0;
			repathAt = 0;
		}
	}
	lastX = me.x;
}

// Something loose right ahead: walking on only shoves it along (together with whoever is
// coming from the other side, so that does not even look stuck). Break it, lift it or hop over.
bool Bot::obstacle(Game& g, Player& self, int dir, PlayerInput& in) {
	Vec2 me = self.getPosition();
	int f = g.frameCounter;
	Bounds ahead{{std::min(me.x + dir * 4, me.x + dir * 13), me.y - 7}, {std::max(me.x + dir * 4, me.x + dir * 13), me.y + 8}};
	Obj* block = nullptr;
	for (Obj* o : g.getObjectsByAABB(ahead)) {
		bool loose = o->alive && o->body && !o->body->isStatic && o->type() == ObjType::Base && o->carriedBy < 0;
		if (loose && o->parent->group == "dynamic_active") block = o;
	}
	if (!block) {
		obstacleId = -1;
		return false;
	}

	if (block->id != obstacleId) {
		obstacleId = block->id;
		obstacleSince = f;
		float r = rnd();
		auto melee = block->damageModifier.find("melee");
		bool breakable = block->health > 0 && block->health < 80 && (melee == block->damageModifier.end() || melee->second > 0);
		obstaclePlan = r < 0.35f && block->liftable() ? Plan::Lift : (r < 0.75f && breakable ? Plan::Break : Plan::Hop);
	}
	if (f - obstacleSince > 100) obstaclePlan = Plan::Hop;

	if (obstaclePlan == Plan::Hop) {
		float height = me.y + 9.5f - block->getBounds().min.y;
		if (height > 26) return false;   // too tall: the stuck logic turns around
		in.jump = tap(lastJump, true);
		airTargetX = me.x + dir * 30.0f;
		return true;
	}

	in.left = in.right = false;
	if (obstaclePlan == Plan::Lift)
		in.grab = tap(lastGrab, true);
	else
		in.punch = tap(lastPunch, f % 14 == 0);
	return true;
}

// A running jump, if they are on the run already and the landing is safe
bool Bot::tryDive(Game& g, Player& self, int dir, float chance, PlayerInput& in) {
	if (!self.checkState("running") || self.facingDirection != dir || !self.onGround()) return false;
	if (g.frameCounter - lastDive < 80 || rnd() >= chance || !safeAhead(g, self, dir)) return false;
	lastDive = g.frameCounter;
	dive = true;
	lastJump = false;
	in.jump = tap(lastJump, true);
	return true;
}

// Two fighters can face each other without getting anywhere (blocked shots, different floors,
// out of reach): when nothing changes for a while, move to another spot and come back from there
bool Bot::wander(Game& g, Player& self, Player* target, PlayerInput& in) {
	Vec2 me = self.getPosition();
	int f = g.frameCounter;
	if ((me - calmAt).length() > 10 || self.health != calmHp || target->health != calmTargetHp) {
		calmAt = me;
		calmHp = self.health;
		calmTargetHp = target->health;
		calmSince = f;
	}

	if (f - calmSince > 150 && f >= wanderUntil && !g.nav.empty()) {
		wanderNode = -1;
		for (int i = 0; i < 16 && wanderNode < 0; i++) {
			int n = (int)(rnd() * g.nav.nodes.size()) % (int)g.nav.nodes.size();
			const NavNode& node = g.nav.nodes[n];
			float d = std::fabs(node.x - me.x) + std::fabs(node.y - me.y);
			if (d > 60 && d < 260) wanderNode = n;
		}
		wanderUntil = f + 100;
		calmSince = f;
	}
	if (f >= wanderUntil || wanderNode < 0) return false;

	const NavNode& node = g.nav.nodes[wanderNode];
	moveTowards(g, self, node.x, node.y - 9.5f, in);
	return true;
}

void Bot::newRound() {
	targetId = -1;
	seenFrames = 0;
	lastFire = lastMelee = -1000;
	nextJump = 0;
	cookUntil = -1;
	aimOffsetUntil = 0;
	stuckFrames = 0;
	reverseUntil = -1;
	dropStage = dropFrame = 0;
	route.clear();
	routeGoal = curNode = -1;
	repathAt = 0;
	airTargetX = -1e9f;
	obstacleId = -1;
	dive = wantRun = false;
	runRollAt = 0;
	lastDive = -1000;
	lastJump = lastPunch = lastKick = lastFireBtn = lastInteract = lastGrab = false;
	tossAt = -1;
	calmSince = 0;
	wanderUntil = wanderNode = -1;
}

PlayerInput Bot::think(Game& g, Player& self) {
	if (g.frameCounter < lastFrame) newRound();
	lastFrame = g.frameCounter;

	dive = false;
	PlayerInput in = decide(g, self);
	in.hop = !dive;   // on the run a jump turns into a dive: too low for clearing anything
	return in;
}

PlayerInput Bot::decide(Game& g, Player& self) {
	PlayerInput in;
	Vec2 me = self.getPosition();

	// Struggle when held; throw whoever we are holding
	if (self.heldBy >= 0) {
		in.punch = g.frameCounter % p.struggle == 0;
		return in;
	}
	if (self.holding >= 0) {
		if (tossAt < 0) tossAt = g.frameCounter + 18 + (int)(rnd() * 60);   // slow ones lose their grip
		in.fire = tap(lastFireBtn, g.frameCounter >= tossAt);
		if (in.fire)
			if (Player* t = pickTarget(g, self)) aimAt(g, self, t->getPosition().x, t->getPosition().y, 0.25f, in);
		return in;
	}
	tossAt = -1;

	// Keep the grenade cooking until it is time to let go
	Throwable* nade = held<Throwable>(g, self, INV_THROWABLE, ObjType::Throwable);
	Player* target = pickTarget(g, self);
	targetId = target ? target->id : -1;

	if (cookUntil > g.frameCounter) {
		in.grenade = true;
		if (target) aimAt(g, self, target->getPosition().x, target->getPosition().y, 0.6f, in);
		return in;
	}
	cookUntil = -1;

	// On fire: roll it out
	if (self.fire > 0 && self.onGround()) {
		in.down = true;
		in.right = self.facingDirection > 0;
		in.left = !in.right;
		return in;
	}

	// Get away from live grenades
	for (const char* bomb : {"grenade", "lobbed_grenade"})
		for (Obj* o : g.getObjectsByParent(Registry::instance().get(bomb))) {
			Vec2 q = o->getPosition();
			if ((q - me).length() < 48) {
				int away = q.x > me.x ? -1 : 1;
				if (safeAhead(g, self, away)) {
					in.right = away > 0;
					in.left = away < 0;
					in.run = true;
					tryDive(g, self, away, p.diveRate * 2, in);
				}
				return in;
			}
		}

	// In the acid: out by the nearest way, nothing else matters
	if (g.inAcid(me) && !g.nav.empty()) {
		int exit = -1;
		float best = 1e9f;
		for (int i = 0; i < (int)g.nav.nodes.size(); i++) {
			const NavNode& n = g.nav.nodes[i];
			float d = std::fabs(n.x - me.x) + std::fabs(n.y - me.y);
			if (!n.acid && d < best) {
				best = d;
				exit = i;
			}
		}
		if (exit >= 0) {
			moveTowards(g, self, g.nav.nodes[exit].x, g.nav.nodes[exit].y - 9.5f, in);
			return in;
		}
	}

	if (!target) return in;

	if (wander(g, self, target, in)) return in;

	Vec2 t = target->getPosition();
	float dx = t.x - me.x, dy = t.y - me.y;
	bool see = lineOfSight(g, me.x, me.y - 4, t.x, t.y - 4);
	seenFrames = see ? seenFrames + 1 : 0;

	Gun* gun = held<Gun>(g, self, INV_GUN, ObjType::Gun);
	bool hasAmmo = gun && gun->ammo > 0;
	bool grounded = self.onGround().has_value();

	// The skill, when it makes sense
	if (self.skillReady() && rnd() < 0.05f) {
		const std::string& type = self.skill->type;
		bool close = std::fabs(dx) < 30 && std::fabs(dy) < 20;
		if ((type == "heal" && self.health < 55) || (type == "dash" && std::fabs(dx) > 90) || (type == "smoke" && self.health < 40) ||
			((type == "counter" || type == "shout" || type == "rage") && close))
			in.skill = true;
	}

	// Grenade at a visible target at medium range
	if (nade && nade->ammo > 0 && see && grounded && std::fabs(dx) > 50 && std::fabs(dx) < 200 &&
		rnd() < p.grenadeRate / 60.0f) {
		cookUntil = g.frameCounter + 30 + (int)(rnd() * 25);
		in.grenade = true;
		aimAt(g, self, t.x, t.y, 0.6f, in);
		return in;
	}

	// Shoot (or throw the empty gun when close enough)
	bool ranged = gun && (hasAmmo || std::fabs(dx) < 110);
	snprintf(debug, sizeof debug, "target %d dx=%.0f dy=%.0f see=%d", targetId, dx, dy, (int)see);
	if (ranged && see && grounded && std::fabs(dx) > 18 && seenFrames >= p.reaction) {
		snprintf(debug, sizeof debug, "shoot %d", targetId);
		aimAt(g, self, t.x, t.y, 0, in);
		bool ready = self.checkState("aiming") && !self.checkState("drawing");
		if (ready && g.frameCounter - lastFire >= p.fireInterval) {
			in.fire = tap(lastFireBtn, true);
			if (in.fire) lastFire = g.frameCounter;
		} else {
			lastFireBtn = false;
		}
		return in;
	}

	// Close enough to brawl
	if (std::fabs(dx) < 12 && std::fabs(dy) < 14 && grounded) {
		int face = dx > 0 ? 1 : -1;
		snprintf(debug, sizeof debug, "melee %d dx=%.0f", targetId, dx);
		if (self.facingDirection != face) {
			if (std::fabs(dx) < 5) {
				// standing inside each other (fighters do not collide): walk out first
				int away = -face;
				if (safeAhead(g, self, away)) {
					in.right = away > 0;
					in.left = away < 0;
				}
			} else if (g.frameCounter % 8 == 0) {
				// a single short step turns around (too rare to start running)
				in.right = face > 0;
				in.left = face < 0;
			}
			return in;
		}
		// stay on them: step in between the blows
		if (std::fabs(dx) > 9 && !self.checkState("attacking")) {
			in.right = face > 0;
			in.left = face < 0;
		}
		if (g.frameCounter - lastMelee >= p.meleeInterval) {
			lastMelee = g.frameCounter;
			if (rnd() < p.grabRate)
				in.grab = tap(lastGrab, true);
			else if (rnd() < 0.65f)
				in.punch = tap(lastPunch, true);
			else
				in.kick = tap(lastKick, true);
		} else {
			lastPunch = lastKick = false;
		}
		return in;
	}
	lastPunch = lastKick = lastGrab = false;

	// A melee weapon is thrown from a distance now and then
	if (see && grounded && !gun && std::fabs(dx) > 40 && std::fabs(dx) < 120 && std::fabs(dy) < 20 && rnd() < 0.01f) {
		Obj* m = g.getObjectById(self.inventory[INV_MELEE]);
		if (m && m->parent->key != "fists") {
			self.facingDirection = dx > 0 ? 1 : -1;
			in.grab = tap(lastGrab, true);
			return in;
		}
	}

	// Grab a weapon first if one is close
	if (Obj* item = pickItem(g, self)) {
		Vec2 q = item->getPosition();
		if (std::fabs(q.x - me.x) < 5 && std::fabs(q.y - me.y) < 14) {
			in.interact = tap(lastInteract, true);
			return in;
		}
		lastInteract = false;
		if (std::fabs(q.x - me.x) < std::fabs(dx) + 40) {
			moveTowards(g, self, q.x, q.y, in);
			char tmp[160];
			snprintf(tmp, sizeof tmp, "item %s | %s", item->parent->key.c_str(), debug);
			snprintf(debug, sizeof debug, "%s", tmp);
			return in;
		}
	}

	if (g.inAcid(t)) return in;   // let the acid do it

	moveTowards(g, self, t.x, t.y, in);
	// close enough on the same floor: dive in and take them along
	if (std::fabs(dy) < 8 && std::fabs(dx) > 22 && std::fabs(dx) < 44) tryDive(g, self, dx > 0 ? 1 : -1, p.diveRate, in);
	return in;
}
