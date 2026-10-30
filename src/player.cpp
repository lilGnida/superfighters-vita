// Port of player.js
#include <algorithm>
#include <cmath>

#include "game.h"
#include "progress.h"

namespace {

// State names
const char* S_NONE = "none";
const char* S_JUMPING = "jumping";
const char* S_GROUNDED = "grounded";
const char* S_STUN = "grounded_damaged_stun";
const char* S_RECOVERING = "grounded_damaged_recovering";
const char* S_LANDED = "grounded_damaged_stun_landing";   // dazed after a fall
const char* S_DAMAGED = "damaged";                        // any of the states above
const char* S_FREEFALL = "free_falling";
const char* S_WALKING = "grounded_walking";
const char* S_RUNNING = "grounded_running";
const char* S_CROUCHING = "grounded_crouching";
const char* S_ROLLING = "grounded_rolling";
const char* S_ATTACKING = "attacking";
const char* S_MELEE = "grounded_attacking_melee";
const char* S_COMBO1 = "grounded_attacking_melee_combo_1";
const char* S_COMBO2 = "grounded_attacking_melee_combo_2";
const char* S_COMBO3 = "grounded_attacking_melee_combo_3";
const char* S_JUMPPUNCH = "jumping_attacking_punching";
const char* S_KICKING = "grounded_attacking_kicking";
const char* S_JUMPKICK = "jumping_attacking_kicking";
const char* S_DRAWING = "grounded_drawing";
const char* S_AIMING = "grounded_aiming";
const char* S_PREPARE = "grounded_aiming_prepare_throw";
const char* S_THROWING = "grounded_aiming_attacking_throwing";
const char* S_SHOOTING = "grounded_aiming_attacking_shooting";
const char* S_CLIMBING = "climbing";
const char* S_GRAB = "grounded_attacking_grab";       // holding a fighter overhead
const char* S_HELD = "held_overhead";                 // being held
const char* S_TOSS = "grounded_attacking_toss";       // arm swing of a throw
const char* S_DIVE = "air_diving";        // running jump: flat, ends in a single roll

enum ButtonKey { K_AIM, K_DOWN, K_UP, K_RIGHT, K_LEFT, K_JUMP, K_PUNCH, K_KICK, K_FIRE, K_GRENADE, K_INTERACT, K_GRAB, K_SKILL };

constexpr float DEG = PI_F / 180.0f;

}  // namespace

Player::Player(Game& g, const ObjDef& p, const ObjOptions& o, int id_)
	: Obj(g, p, o, id_, [] {
		  BodyOverrides ov;
		  ov.width = 8;
		  ov.height = 19;
		  // bevelled corners: the body slides over the seams between level colliders
		  const float hw = 4, hh = 9.5f, c = 2;
		  ov.verts = {{-hw + c, -hh}, {hw - c, -hh}, {hw, -hh + c}, {hw, hh - c},
					  {hw - c, hh}, {-hw + c, hh}, {-hw, hh - c}, {-hw, -hh + c}};
		  ov.inertiaInfinite = true;
		  ov.friction = 0;
		  return ov;
	  }()) {
	if (o.profile) profile = *o.profile;
	team = o.team.value_or(0);

	// Collision group for platforms
	if (body) body->filter.group = id;
}

// A door's wall is on the side you run towards to go through it
int Door::into() {
	if (intoDir != -2) return intoDir;
	Vec2 p = getPosition();
	auto wall = [&](int dir) {
		return game.physics().raycast(p, {p.x + dir * 20, p.y}, [&](Body* b) {
			if (!b->isStatic) return false;
			Obj* o = game.getObjectByBody(b);
			return !o || o->type() == ObjType::Base;
		});
	};
	bool left = wall(-1), right = wall(1);
	intoDir = left == right ? 0 : (right ? 1 : -1);
	return intoDir;
}

float Player::getCrosshairAngle() const {
	if (facingDirection == 1) return crosshairAngle * DEG;
	return (180 - crosshairAngle) * DEG;
}

Vec2 Player::getCrosshairPosition() const {
	float angle = getCrosshairAngle();
	Vec2 p = getPosition();
	Vec2 pos{p.x - facingDirection * 2.0f, p.y - 2.0f};

	pos.x += std::cos(angle) * (frameW / 2 + 4);
	pos.y += std::sin(angle) * (frameW / 2 + 4);

	pos.x += std::cos(angle - PI_F / 2) * (facingDirection * 2.0f);
	pos.y += std::sin(angle - PI_F / 2) * (facingDirection * 2.0f);
	return pos;
}

Vec2 Player::getThrowablePosition() const {
	Vec2 p = getPosition();
	return {p.x - 8.0f * facingDirection, p.y - 1.0f};
}

Player::Button Player::getButtonStatus(int key) {
	bool now = false;
	switch (key) {
		case K_AIM: now = input.aim; break;
		case K_DOWN: now = input.down; break;
		case K_UP: now = input.up; break;
		case K_RIGHT: now = input.right; break;
		case K_LEFT: now = input.left; break;
		case K_JUMP: now = input.jump; break;
		case K_PUNCH: now = input.punch; break;
		case K_KICK: now = input.kick; break;
		case K_FIRE: now = input.fire; break;
		case K_GRENADE: now = input.grenade; break;
		case K_INTERACT: now = input.interact; break;
		case K_GRAB: now = input.grab; break;
		case K_SKILL: now = input.skill; break;
	}
	bool prev = lastButtons[key];
	Button b{!prev && now, prev && !now, now};
	lastButtons[key] = now;
	return b;
}

// ------------------------------------------------------------------ update

void Player::update() {
	Obj::update();
	if (!alive) return;

	// Out of a door the other way round: the held direction keeps working, mirrored
	if (mirrorDir != 0) {
		if (mirrorDir > 0 ? input.right : input.left)
			std::swap(input.left, input.right);
		else
			mirrorDir = 0;
	}

	// Check inventory is still valid
	for (int i = 0; i < 4; i++) {
		Obj* item = game.getObjectById(inventory[i]);

		if (!item) {
			inventory[i] = -1;

			// Default items when nothing held
			if (i == INV_MELEE) {
				if (Obj* fists = game.createObject("fists")) {
					fists->pickup(*this);
					inventory[i] = fists->id;
				}
			}
		}
	}

	// Whatever knocked the fighter out of the lift also knocks the object out of their hands
	if (holding >= 0 && !strictState(S_GRAB)) {
		Obj* t = game.getObjectById(holding);
		if (t && t->type() != ObjType::Player) releaseHolding();
	}

	// Riding a rocket: helpless until it goes off
	if (rocketId >= 0) {
		if (game.getObjectById(rocketId)) {
			if (!strictState(S_FREEFALL)) setState(S_FREEFALL);
			fallPeak = 0;
			return;
		}
		rocketId = -1;
		disableGravity = false;
	}

	// Held overhead: can only struggle
	if (heldBy >= 0) {
		Player* h = static_cast<Player*>(game.getObjectById(heldBy));
		if (h && h->holding == id) {
			Vec2 p = h->getPosition();
			setPosition(p.x, p.y - 19);
			setVelocity(0, 0);
			facingDirection = -h->facingDirection;
			if (!strictState(S_HELD)) setState(S_HELD);
			fallPeak = 0;
			for (int key : {K_PUNCH, K_KICK, K_JUMP})
				if (getButtonStatus(key).pressed) h->grabUntil -= (int)(5 * mod("breakFreeMul", 1));
			return;
		}
		releaseHeld();
	}

	// Swept along by a dive
	if (tackledBy >= 0) {
		Player* h = static_cast<Player*>(game.getObjectById(tackledBy));
		if (h && h->tackleId == id) {
			Vec2 p = h->getPosition();
			setPosition(p.x + h->facingDirection * 5, p.y + 2);
			Vec2 v = h->getVelocity();
			setVelocity(v.x, v.y);
			facingDirection = -h->facingDirection;
			fallPeak = 0;
			return;
		}
		tackledBy = -1;
		disableGravity = false;
	}

	// Fall damage: remember the fastest fall, get hurt when landing
	bool grounded = onGround().has_value() && game.frameCounter >= airborneUntil;
	updateFlight();
	updateAirAttack();
	if (tackleId >= 0) {
		bool down = grounded && game.frameCounter - tackleStart > 3;
		bool stopped = game.frameCounter - tackleStart > 3 && std::fabs(getVelocity().x) < 0.4f;
		if (!game.getObjectById(tackleId))
			tackleId = -1;
		else if (!strictState(S_DIVE) || down || stopped || game.frameCounter - tackleStart > 60)
			endTackle();
	}
	updateAcid();
	if (!alive) return;

	// Flying into something solid hurts (the bounce is over within one physics step)
	float slam = std::fabs(prevVelocity.x);
	if (checkState(S_FREEFALL) && slam > 4 && std::fabs(getVelocity().x) < slam * 0.5f) {
		dealDamage({(slam - 3) * 4, "collision"});
		game.createObject("hit", ObjOptions().at(getPosition()));
		game.hitStop(3);
		if (!alive) return;
	}

	// A diver goes through a window pane, with or without someone in their arms
	if (strictState(S_DIVE)) game.smashGlass(*this);

	// Broken glass cuts whoever rolls or dives through it, once it has settled:
	// the shards of a pane one has just gone through do not count
	if ((checkState(S_ROLLING) || strictState(S_DIVE)) && mod("shardImmune", 0) == 0) {
		for (Obj* o : game.getObjectsByAABB(getBounds()))
			if (o->parent->key == "glass_shard" && game.frameCounter - o->born > 90) {
				dealDamage({3, ""});
				o->kill();
			}
		if (!alive) return;
	}
	if (checkState(S_CLIMBING)) {
		fallPeak = 0;
	} else if (!grounded) {
		fallPeak = tackleLanding ? 0 : std::max(fallPeak, getVelocity().y);
	} else {
		float peak = fallPeak;
		fallPeak = 0;
		tackleLanding = false;
		if (peak > 6.5f) {
			dealDamage({(peak - 6.5f) * 12.0f * mod("fallDamageMul", 1), "fall"});
			if (!alive) return;
		}
		// A hard landing leaves the fighter dazed; a dive rolls it off
		if (peak >= 4.5f && !strictState(S_DIVE) && !checkState(S_ROLLING))
			setState(S_LANDED, (peak >= 6.0f ? std::min(70.0f, 40 + (peak - 6.0f) * 10) : 16) * mod("stunMul", 1));
	}

	// Check player is on ground
	if (grounded) {
		// Check if previously free falling
		if (checkState(S_FREEFALL)) {
			if (dazeOnLanding)
				setState(S_LANDED, 60 * mod("stunMul", 1));
			else
				setState(S_RECOVERING, 30);
			dazeOnLanding = false;
		}

		// Landing from a dive turns into a roll
		if (strictState(S_DIVE) && game.frameCounter - diveStart > 3) {
			setState(S_ROLLING, 18);
			playSound("roll");
		}

		// Check Rolling
		if (checkState(S_ROLLING)) moveHorizontal((float)facingDirection);

		// Check if any actions are done then reset to grounded
		if (delayDone()) setState(S_GROUNDED);

	} else {
		// If reached high enough velocity enable freefall
		if (std::fabs(getVelocity().y) > 6 || checkState(S_STUN)) setState(S_FREEFALL);

		// Displaced from off ground then set to jumping
		if (checkState(S_GROUNDED)) setState(S_JUMPING);
	}

	// A dive never lasts forever (e.g. landing on top of a loose object)
	if (strictState(S_DIVE) && game.frameCounter - diveStart > 70) setState(S_JUMPING);

	// Stop velocity changes
	if (checkState(S_GROUNDED) || checkState(S_CLIMBING)) setVelocity(0, 0);

	// Check if still climbing
	if (disableGravity) {
		bool onLadder = false;
		for (Obj* t : game.getObjectsByAABB(getBounds()))
			if (t->type() == ObjType::Ladder) onLadder = true;

		if (!onLadder) {
			disableGravity = false;
			setState(S_JUMPING);
		}
	}

	workOffShove(grounded);
	if (game.frameCounter < dashUntil && !sideBlocked(facingDirection)) moveHorizontal(facingDirection * skill->amount);

	// Check inputs
	Button fire = getButtonStatus(K_FIRE);
	Button grenade = getButtonStatus(K_GRENADE);
	Button aimBtn = getButtonStatus(K_AIM);

	auto heldItem = [&](int slot, ObjType t) {
		Obj* o = game.getObjectById(inventory[slot]);
		return (o && o->type() == t) ? o : nullptr;
	};

	// Holding a fighter: R throws them, too late and they break free
	if (holding >= 0) {
		Obj* t = game.getObjectById(holding);
		if (!t || !strictState(S_GRAB)) {
			holding = -1;
		} else if (fire.pressed) {
			throwHeld();
		} else if (game.frameCounter >= grabUntil) {
			if (t->type() == ObjType::Player) {
				Player* p = static_cast<Player*>(t);
				holding = -1;
				p->releaseHeld();
				p->setPosition(getPosition().x + facingDirection * 8, getPosition().y);
				p->setState(S_JUMPING);
				setState(S_STUN, 30);
			} else {
				releaseHolding();
				setState(S_GROUNDED);
			}
		} else if (t->type() != ObjType::Player) {
			t->setPosition(getPosition().x, getPosition().y - 13 - t->height / 2);
			t->setVelocity(0, 0);
			t->setAngle(0);
		}
	}

	// Fire / grenade buttons pick the weapon and are remembered until it is out
	if (fire.pressed && heldItem(INV_GUN, ObjType::Gun)) {
		equiped = INV_GUN;
		fireQueued = true;
	}
	if (grenade.pressed) {
		Obj* t = heldItem(INV_THROWABLE, ObjType::Throwable);
		if (t && static_cast<Throwable*>(t)->ammo > 0) {
			equiped = INV_THROWABLE;
			grenadeQueued = true;
		}
	}

	// Aim is held by L / R / the right stick, or while a grenade is queued or cooking
	bool wantAim = aimBtn.held || input.stickAim || grenadeQueued || strictState(S_PREPARE);
	aim(Button{false, false, wantAim});
	stickAim();   // after aim(): drawing a weapon resets the crosshair

	moveDown(getButtonStatus(K_DOWN));
	moveUp(getButtonStatus(K_UP));
	jump(getButtonStatus(K_JUMP));
	moveRight(getButtonStatus(K_RIGHT));
	moveLeft(getButtonStatus(K_LEFT));

	weaponActions(fire, grenade);
	punch(getButtonStatus(K_PUNCH));
	kick(getButtonStatus(K_KICK));
	interact(getButtonStatus(K_INTERACT));
	grabAction(getButtonStatus(K_GRAB));
	if (getButtonStatus(K_SKILL).pressed) useSkill();
	checkDoors();

	// Queued actions are dropped when the weapon could not be drawn (jumping, stunned...)
	if (!isAiming()) {
		fireQueued = false;
		grenadeQueued = false;
	}
}

bool Player::isAiming() const { return checkState(S_AIMING) || checkState(S_DRAWING); }

// Right stick: crosshair follows the stick, facing follows its horizontal side
void Player::stickAim() {
	if (!input.stickAim) return;
	float x = input.stickX, y = input.stickY;
	if (x > 0.15f) facingDirection = 1;
	else if (x < -0.15f) facingDirection = -1;
	float a = std::atan2(y, std::fabs(x)) / DEG;
	crosshairAngle = std::max(-90.0f, std::min(90.0f, a));
}

// ------------------------------------------------------------------ damage

bool Player::dealDamage(const DamageSpec& damage) {
	// If still trying to get up prevent damage
	if (checkState(S_RECOVERING)) return false;

	// Check if damage is actually dealt
	if (!Obj::dealDamage(damage)) return false;

	// Create blood effects
	for (int i = 0; i < 2; i++) {
		const char* effect = i == 0 ? "blood_large" : "blood_small";
		Vec2 velocity{game.random() * 2 - 1, game.random() * 2 - 1};
		game.createObject(effect, ObjOptions().at(getPosition()).vel(velocity));
	}

	// Melee cause slight stun
	const std::string& type = damage.type;

	if (type == "melee") {
		if (!checkState(S_FREEFALL)) setState(S_STUN, 9 * mod("stunMul", 1));

	} else if (type == "projectile") {
		if (damage.hp > 10) setState(S_FREEFALL);

	} else if (type == "explosion") {
		setState(S_FREEFALL);

	} else if (type == "thrown") {
		setState(S_FREEFALL);

	} else if (type == "collision") {
		if (damage.hp < 6 && !checkState(S_FREEFALL))
			setState(S_STUN, 9);
		else
			setState(S_FREEFALL, 9);
	}
	return true;
}

// Something heavy coming down on a fighter hurts: speed towards them, scaled by size
void Player::addCollision(Obj& source, float angle) {
	bool known = false;
	for (auto& c : collisions) known = known || c.objectId == source.id;
	Obj::addCollision(source, angle);
	if (known || !alive || !source.alive || !source.body || source.type() != ObjType::Base) return;
	if (game.frameCounter < source.thrownUntil) return;   // it has its own reckoning

	if (source.body->isStatic) return;

	Vec2 towards = (getPosition() - source.getPosition()).normalised();
	float speed = source.prevVelocity.x * towards.x + source.prevVelocity.y * towards.y;
	if (speed < 2.5f) return;
	float size = std::max(0.3f, std::min(2.5f, source.width * source.height / 256.0f));
	dealDamage({(speed - 1.5f) * 2.5f * size, "collision"});
}

void Player::knockback(Vec2 v, bool launch) {
	if (checkState(S_RECOVERING) || game.getObjectById(rocketId) || heldBy >= 0 || tackledBy >= 0) return;

	if (launch && v.length() >= 2.0f) {
		setVelocity(v.x, std::min(v.y, -1.2f));
		setState(S_FREEFALL);
		disableGravity = false;
		airborneUntil = game.frameCounter + 4;
		shove = 0;
	} else if (onGround()) {
		shove += v.x;
	} else {
		moveVelocity(v.x, v.y);
	}
}

// Three hits in a row on the same fighter: the last one disarms them, or if they have
// nothing to lose, floors them. Only the fighter who landed it feels the camera shake.
void Player::landedHit(Player& target, DamageSpec& damage) {
	if (target.id == comboTarget && game.frameCounter - comboFrame <= 50)
		comboHits++;
	else
		comboHits = 1;
	comboTarget = target.id;
	comboFrame = game.frameCounter;
	if (comboHits < (int)mod("comboHits", 3)) return;

	comboHits = 0;
	if (target.disarm(facingDirection)) {
		credit("disarm");
		game.hitStop(4);
	} else {
		credit("finisher");
		damage.hp += 14 * mod("finisherMul", 1);
		target.knockback({facingDirection * 3.4f, -1.5f}, true);
		target.dazeOnLanding = true;
		game.hitStop(6);
		if (!isBot) game.shake(14, 3);
	}
}

bool Player::dropWeapon(int slot, Vec2 v) {
	Obj* item = game.getObjectById(inventory[slot]);
	if (!item || item->parent->key == "fists") return false;

	ObjOptions o;
	o.at({getPosition().x, getPosition().y - 4}).vel(v).life(900);
	o.isStatic = false;
	o.group = "item";
	o.angularVelocity = 0.3f * facingDirection;
	if (item->type() == ObjType::Gun) o.ammo = static_cast<Gun*>(item)->ammo;
	if (item->type() == ObjType::Throwable) {
		o.ammo = static_cast<Throwable*>(item)->ammo;
		if (*o.ammo <= 0) return false;
	}
	game.createObject(item->parent, o);
	inventory[slot] = -1;
	item->kill();
	return true;
}

bool Player::disarm(int dir) {
	Vec2 v{dir * 2.5f, -3.5f};
	return dropWeapon(INV_GUN, v) || dropWeapon(INV_MELEE, v);
}

void Player::dropWeapons() {
	dropWeapon(INV_GUN, {game.random() * 3 - 1.5f, -2.5f});
	dropWeapon(INV_MELEE, {game.random() * 3 - 1.5f, -2.5f});
	dropWeapon(INV_THROWABLE, {game.random() * 3 - 1.5f, -2.5f});
}

float Player::holdFraction() const {
	return holding >= 0 ? clamp01((grabUntil - game.frameCounter) / (float)grabTotal) : 0;
}

// L: grab whoever is within reach; with nobody near, throw the melee weapon
void Player::grabAction(Button button) {
	if (!button.pressed || holding >= 0 || isAiming()) return;
	if (!checkState(S_GROUNDED) || checkState(S_ATTACKING) || checkState(S_DAMAGED) || checkState(S_ROLLING)) return;

	Vec2 me = getPosition();
	Player* best = nullptr;
	float bestD = 1e9f;
	for (Obj* o : game.getPlayers()) {
		Player* p = static_cast<Player*>(o);
		if (p == this || (team > 0 && p->team == team) || p->heldBy >= 0 || p->tackledBy >= 0 || p->rocketId >= 0) continue;
		if (p->checkState(S_RECOVERING) || p->checkState(S_CLIMBING)) continue;
		Vec2 q = p->getPosition();
		float dx = std::fabs(q.x - me.x);
		if (dx <= 14 && std::fabs(q.y - me.y) <= 12 && dx < bestD) {
			bestD = dx;
			best = p;
		}
	}

	// nobody near: anything loose within reach goes up instead, the side we face first
	Obj* prop = nullptr;
	bestD = 1e9f;
	if (!best)
		for (Obj* o : game.getObjectsByAABB({{me.x - 20, me.y - 12}, {me.x + 20, me.y + 12}})) {
			if (!o->liftable()) continue;
			float dx = o->getPosition().x - me.x;
			float rank = std::fabs(dx) + (dx * facingDirection < 0 ? 12 : 0);
			if (rank < bestD) {
				bestD = rank;
				prop = o;
			}
		}

	if (best) {
		facingDirection = best->getPosition().x >= me.x ? 1 : -1;
		holding = best->id;
		grabTotal = (int)(72 * mod("grabMul", 1));
		grabUntil = game.frameCounter + grabTotal;
		setState(S_GRAB, INFINITY);
		best->heldBy = id;
		best->disableGravity = true;
		best->shove = 0;
		best->setState(S_HELD);
		playSound("punch");
	} else if (prop) {
		facingDirection = prop->getPosition().x >= me.x ? 1 : -1;
		holding = prop->id;
		grabTotal = (int)(150 * mod("grabMul", 1));
		grabUntil = game.frameCounter + grabTotal;
		setState(S_GRAB, INFINITY);
		prop->setCarried(id);
		prop->setVelocity(0, 0);
		prop->setAngularVelocity(0);
		playSound("punch");
	} else {
		throwMelee();
	}
}

void Player::releaseHolding() {
	Obj* t = game.getObjectById(holding);
	holding = -1;
	if (!t) return;
	if (t->type() == ObjType::Player) {
		static_cast<Player*>(t)->releaseHeld();
	} else {
		t->setCarried(-1);
		t->setVelocity(0, 0);
	}
}

void Player::releaseHeld() {
	heldBy = -1;
	disableGravity = false;
}

void Player::throwHeld() {
	Obj* held = game.getObjectById(holding);
	holding = -1;
	setState(S_TOSS, 14);
	playSound("punch");
	if (!held) return;

	float a = input.stickAim ? std::atan2(input.stickY, input.stickX) : (facingDirection > 0 ? -0.35f : PI_F + 0.35f);
	Vec2 p = getPosition();

	if (held->type() != ObjType::Player) {
		float power = 6.5f * mod("throwMul", 1);
		held->setCarried(-1);
		held->setPosition(p.x + std::cos(a) * (7 + held->width / 2), p.y - 12);
		held->setVelocity(std::cos(a) * power, std::sin(a) * power);
		held->setAngularVelocity(0.2f * facingDirection);
		held->thrownBy = id;
		held->thrownUntil = game.frameCounter + 70;
		credit("throw");
		return;
	}

	Player* t = static_cast<Player*>(held);
	t->releaseHeld();
	t->setPosition(p.x + facingDirection * 8, p.y - 14);
	float power = 6.5f * mod("throwMul", 1);
	t->setVelocity(std::cos(a) * power, std::sin(a) * power);
	t->setState(S_FREEFALL);
	t->lastHitBy = id;
	credit("throw");
	t->airborneUntil = game.frameCounter + 4;
	t->thrownBy2 = id;
	t->flyUntil = game.frameCounter + 45;
	t->dealDamage({8, "thrown"});
}

// Out of reach: the melee weapon flies towards the crosshair direction
void Player::throwMelee() {
	Obj* item = game.getObjectById(inventory[INV_MELEE]);
	if (!item || item->parent->key == "fists") return;

	float a = input.stickAim ? std::atan2(input.stickY, input.stickX) : (facingDirection > 0 ? -0.1f : PI_F + 0.1f);
	Vec2 p = getPosition();
	ObjOptions o;
	o.at({p.x + std::cos(a) * 10, p.y - 4 + std::sin(a) * 10}).vel({std::cos(a) * 6.5f, std::sin(a) * 6.5f}).rot(a).life(600);
	o.isStatic = false;
	o.group = "item";
	o.angularVelocity = 0.4f * facingDirection;
	if (Obj* t = game.createObject(item->parent, o)) {
		Melee* thrown = static_cast<Melee*>(t);
		thrown->thrownBy = id;
		thrown->thrownFrames = 90;
		thrown->thrownMul = weaponMod(item->parent->key, "thrownMul", 1);
	}
	inventory[INV_MELEE] = -1;
	item->kill();
	setState(S_TOSS, 14);
	playSound("punch");
}

// Thrown fighters take down whoever they fly into
void Player::updateFlight() {
	if (game.frameCounter >= flyUntil) return;
	if (onGround() && game.frameCounter >= airborneUntil) {
		flyUntil = 0;
		return;
	}
	game.smashGlass(*this);
	for (Obj* o : game.getObjectsByAABB(getBounds())) {
		if (o->type() != ObjType::Player || o == this || o->id == thrownBy2 || !o->alive) continue;
		Vec2 v = getVelocity();
		static_cast<Player*>(o)->knockback({(v.x > 0 ? 1 : -1) * 3.0f, -1.5f}, true);
		o->dealDamage({12, "thrown"});
		game.hitStop(3);
		flyUntil = 0;
		dealDamage({6, "thrown"});
		break;
	}
}

// The dive catches a fighter and carries them along to the landing
void Player::startTackle(Player& v) {
	airAttack = 0;
	tackleId = v.id;
	tackleStart = game.frameCounter;
	tackleLanding = true;
	Vec2 speed = getVelocity();
	setVelocity(speed.x, std::min(speed.y, -1.0f));
	v.releaseHolding();
	v.tackledBy = id;
	v.disableGravity = true;
	v.shove = 0;
	v.setState(S_FREEFALL);
	v.lastHitBy = id;
	game.createObject("hit", ObjOptions().at(v.getPosition()));
	game.hitStop(3);
	playSound("punch");
}

// Thrown down at the end of the flight
void Player::endTackle() {
	Player* v = static_cast<Player*>(game.getObjectById(tackleId));
	tackleId = -1;
	if (!v) return;
	v->tackledBy = -1;
	v->disableGravity = false;
	v->setVelocity(facingDirection * 1.5f, -1.2f);
	v->airborneUntil = game.frameCounter + 4;
	v->dazeOnLanding = true;
	v->setState(S_FREEFALL);
	v->dealDamage({18, "melee"});
	credit("tackle");
	game.createObject("hit", ObjOptions().at(v->getPosition()));
	if (!isBot) game.shake(10, 2);
}

// Acid eats away at a fighter like fire does, and a little longer after climbing out
void Player::updateAcid() {
	if (game.inAcid(getPosition())) {
		acidBurn = 40;
		fallPeak = 0;
	}
	if (acidBurn <= 0) return;
	acidBurn--;
	if (game.frameCounter % 2 == 0) dealDamage({1, "acid"});
	if (game.frameCounter % 8 == 0) game.puff("smoke", getPosition(), {game.random() - 0.5f, -1});
}

// Jump punch, jump kick and the dive stay dangerous for a moment, and hit harder the faster you go
void Player::updateAirAttack() {
	if (airAttack == 0) return;
	if (game.frameCounter >= airAttackUntil || (airAttack != 3 && onGround())) {
		airAttack = 0;
		return;
	}
	Vec2 p = getPosition();
	if (airAttack == 3) {
		// (reaches as far as the blow below, or it would only ever knock them over)
		for (Obj* o : game.getObjectsByAABB({{p.x + facingDirection * 8 - 14, p.y - 9}, {p.x + facingDirection * 8 + 14, p.y + 12}})) {
			if (o->type() != ObjType::Player || o == this) continue;
			Player* v = static_cast<Player*>(o);
			if ((team > 0 && v->team == team) || v->heldBy >= 0 || v->tackledBy >= 0 || v->rocketId >= 0) continue;
			if (v->checkState(S_RECOVERING) || v->countering()) continue;
			startTackle(*v);
			return;
		}
	}
	float speed = std::fabs(getVelocity().x);
	Rectangle box{p.x + (width / 2 + 3) * facingDirection, airAttack == 1 ? p.y - 2 : p.y + 6, 12, 12};
	float hp = (airAttack == 1 ? 14.0f : (airAttack == 2 ? 16.0f : 10.0f)) + speed * 2;
	Vec2 push{facingDirection * (airAttack == 1 ? 2.2f : 3.0f), -1.5f};
	if (game.createHitbox(this, {box}, {0.001f * facingDirection, 0}, {hp, "melee"}, push) > 0) {
		airAttack = 0;
		game.hitStop(3);
	}
}

void Player::stun(int frames) {
	if (heldBy >= 0 || tackledBy >= 0 || game.getObjectById(rocketId)) return;
	shove = 0;
	setState(S_STUN, frames * mod("stunMul", 1));
}

// ------------------------------------------------------------------ perks and skills

void Player::setLoadout(const std::vector<std::string>& perks, const std::string& skillId) {
	Progress& pr = Progress::get();
	auto merge = [](std::map<std::string, float>& into, const std::map<std::string, float>& effects) {
		for (auto& [key, value] : effects) {
			bool mul = key.size() > 3 && key.compare(key.size() - 3, 3, "Mul") == 0;
			if (mul && into.count(key)) into[key] *= value;
			else into[key] = value;
		}
	};
	for (auto& id : perks) {
		const Perk* p = pr.perk(id);
		if (!p) continue;
		merge(p->group.empty() ? mods : weaponMods[p->group], p->effects);
	}
	skill = pr.skill(skillId);
}

float Player::mod(const char* key, float def) const {
	auto it = mods.find(key);
	return it == mods.end() ? def : it->second;
}

float Player::weaponMod(const std::string& weapon, const char* key, float def) const {
	if (weaponMods.empty()) return def;
	auto group = weaponMods.find(Progress::get().groupOf(weapon));
	if (group == weaponMods.end()) return def;
	auto it = group->second.find(key);
	return it == group->second.end() ? def : it->second;
}

void Player::credit(const char* stat) {
	if (!isBot) game.addStat(stat);
}

bool Player::skillReady() const { return skill && game.frameCounter >= skillReadyAt; }

float Player::skillCharge() const {
	if (!skill || skill->cooldown <= 0) return 0;
	return clamp01(1.0f - (skillReadyAt - game.frameCounter) / (float)skill->cooldown);
}

bool Player::countering() const { return skill && game.frameCounter < counterUntil; }
bool Player::raging() const { return skill && game.frameCounter < rageUntil; }

void Player::useSkill() {
	if (!skillReady() || holding >= 0 || checkState(S_DAMAGED)) return;
	int now = game.frameCounter;
	skillReadyAt = now + skill->cooldown;
	const std::string& type = skill->type;

	if (type == "dash") {
		dashUntil = now + skill->duration;
		putoutFire();
	} else if (type == "counter") {
		counterUntil = now + skill->duration;
	} else if (type == "heal") {
		health = std::min(100.0f, health + skill->amount);
	} else if (type == "rage") {
		rageUntil = now + skill->duration;
	} else if (type == "smoke") {
		hiddenUntil = now + skill->duration;
		for (int i = 0; i < 10; i++)
			game.createObject("smoke", ObjOptions().at(getPosition()).vel({game.random() * 2 - 1, game.random() * -1.5f}));
	} else if (type == "shout") {
		for (Obj* o : game.getPlayers()) {
			Player* p = static_cast<Player*>(o);
			if (p == this || (team > 0 && p->team == team)) continue;
			if ((p->getPosition() - getPosition()).length() <= skill->radius) p->stun(skill->duration);
		}
		game.shake(8, 2);
	}
	playSound("punch");
}

void Player::grabbedByRocket(int id_) {
	rocketId = id_;
	disableGravity = true;
	shove = 0;
	setState(S_FREEFALL);
}

// Pushes on the ground slide the fighter along it (grounded states hold the velocity at zero)
void Player::workOffShove(bool grounded) {
	if (shove == 0) return;
	if (grounded) {
		if (!sideBlocked(shove > 0 ? 1 : -1)) moveHorizontal(shove);
		shove *= 0.8f;
		if (std::fabs(shove) < 0.05f) shove = 0;
	} else {
		moveVelocity(shove, 0);
		shove = 0;
	}
}

void Player::delayCallback(const std::string& action) {
	Obj::delayCallback(action);

	auto melee = [&]() -> Melee* {
		Obj* o = game.getObjectById(inventory[INV_MELEE]);
		return (o && o->type() == ObjType::Melee) ? static_cast<Melee*>(o) : nullptr;
	};

	if (action == "melee_1" || action == "melee_2" || action == "melee_3") {
		int combo = action[6] - '1';
		if (Melee* m = melee()) m->swing(combo);
		moveHorizontal(facingDirection * 1.5f);
		playSound("punch");

	} else if (action == "reset-aim") {
		setState(S_AIMING);
	}
}

// ------------------------------------------------------------------ input handling

void Player::moveHorizontal(float scale) {
	// Calculate based on the ground angle; at the foot of a slope the floor is touched too,
	// and the slope is what the step has to follow
	if (auto ground = onGround()) {
		float best = *ground;
		for (auto& c : collisions)
			if (angleIsGround(c.angle) && (c.angle - 270) * scale < 0 && std::fabs(c.angle - 270) > std::fabs(best - 270)) best = c.angle;
		float angle = (best + 90) * DEG;
		movePosition(std::cos(angle) * scale, -std::sin(angle) * scale);
	} else {
		movePosition(scale, 0);
	}
}

void Player::moveSide(Button button, int dir) {
	if (!button.held) return;

	// Carrying someone: slowly, but they can be taken anywhere
	if (strictState(S_GRAB)) {
		facingDirection = dir;
		if (!sideBlocked(dir)) moveHorizontal(dir * 0.6f);
		return;
	}

	// Walking while aiming: the weapon stays up, the right stick keeps the facing
	if (isAiming()) {
		if (!input.stickAim) facingDirection = dir;
		bool blocked = sideBlocked(dir);
		if (!blocked) {
			moveHorizontal((float)dir);
			aimWalkFrame = game.frameCounter;
		}
		return;
	}

	facingDirection = dir;

	bool blocked = sideBlocked(dir);

	if (!blocked && (checkState(S_JUMPING) || strictState(S_GROUNDED) || strictState(S_CROUCHING) || checkState(S_CLIMBING))) {
		if (checkState(S_CROUCHING)) {
			setState(S_ROLLING, 18);
			putoutFire();
			playSound("roll");

		} else {
			// Check if double tap for run
			if (button.pressed) {
				if (game.frameCounter - lastRun < 16)
					walkingSpeed = 2;
				else
					walkingSpeed = 1;

				lastRun = game.frameCounter;
			}
			if (isBot) walkingSpeed = input.run ? 2 : 1;
			moveHorizontal(dir * walkingSpeed);

			if (checkState(S_GROUNDED)) {
				if (walkingSpeed <= 1)
					setState(S_WALKING);
				else
					setState(S_RUNNING);
			}
		}
	}
}

// Something solid on that side? Loose objects (crates, barrels...) get pushed along instead.
bool Player::sideBlocked(int dir) {
	bool blocked = false;
	for (auto& c : collisions) {
		if (!(dir > 0 ? angleIsRight(c.angle) : angleIsLeft(c.angle))) continue;
		Obj* o = game.getObjectById(c.objectId);
		if (o && (o->type() == ObjType::Platform || o->type() == ObjType::Ladder)) continue;   // never walls
		if (o && o->body && !o->body->isStatic && o->type() != ObjType::Player) {
			Vec2 v = o->getVelocity();
			float push = dir * walkingSpeed * 1.1f;
			if (std::fabs(v.x) < std::fabs(push)) o->setVelocity(push, v.y);
			continue;
		}
		blocked = true;
	}
	return blocked;
}

void Player::moveRight(Button b) { moveSide(b, 1); }
void Player::moveLeft(Button b) { moveSide(b, -1); }

void Player::moveUp(Button button) {
	if (!button.held) return;

	// Aiming up (L aim without the right stick)
	if (strictState(S_AIMING) || strictState(S_DRAWING)) {
		if (!input.stickAim && crosshairAngle > -90) crosshairAngle -= 5;
		return;
	}

	if (!delayDone() && !checkState(S_ROLLING)) return;

	// Climb up a ladder
	if (strictState(S_GROUNDED) || strictState(S_WALKING) || strictState(S_RUNNING) || checkState(S_JUMPING) ||
		checkState(S_CLIMBING)) {
		for (Obj* t : game.getObjectsByAABB(getBounds())) {
			if (t->type() == ObjType::Ladder) {
				disableGravity = true;
				setState(S_CLIMBING);
				movePosition(0, -mod("climbMul", 1));
				break;
			}
		}
	}
}

void Player::jump(Button button) {
	if (!button.pressed) return;

	// Jump off a ladder (not when the same key also climbs, as on the keyboard)
	if (checkState(S_CLIMBING)) {
		if (input.up) return;
		disableGravity = false;
		setVelocity(getVelocity().x, -3);
		setState(S_JUMPING);
		playSound("jump");
		return;
	}

	// Jump from the ground; aiming does not prevent it (it puts the weapon away)
	if (checkState(S_ROLLING) || checkState(S_STUN) || checkState(S_RECOVERING) || (checkState(S_ATTACKING) && !isAiming()))
		return;
	if (holding >= 0) return;
	if (strictState(S_PREPARE)) return;   // keep the cooked grenade in hand

	// Running jump: a flat dive, rolled off on landing
	bool running = strictState(S_RUNNING) || (checkLastState(S_RUNNING) && walkingSpeed > 1 && (input.left || input.right));
	if (running && checkState(S_GROUNDED) && !isAiming() && !input.hop) {
		setVelocity(facingDirection * 2.8f * mod("diveMul", 1), -1.9f);
		setState(S_DIVE, INFINITY);
		diveStart = game.frameCounter;
		airAttack = 3;
		airAttackUntil = game.frameCounter + 40;
		playSound("jump");
		return;
	}

	if (checkState(S_GROUNDED)) {
		setVelocity(getVelocity().x, -4);
		setState(S_JUMPING);
		playSound("jump");
	}
}

void Player::moveDown(Button button) {
	if (!button.held) return;

	// Aiming down (L aim without the right stick)
	if (strictState(S_AIMING) || strictState(S_DRAWING)) {
		if (!input.stickAim && crosshairAngle < 90) crosshairAngle += 5;
		return;
	}
	if (isAiming()) return;

	if (!delayDone()) return;

	// Crouching
	if (checkState(S_GROUNDED) && !checkState(S_ROLLING)) {
		// Not previously crouching
		if (!checkLastState(S_CROUCHING)) {
			if (game.frameCounter - lastCrouch < 16) {
				// Drop through the platforms we are standing on
				if (body) {
					for (auto& c : collisions) {
						Obj* obj = game.getObjectById(c.objectId);
						if (!obj || obj->type() != ObjType::Platform || !obj->body) continue;

						auto& pl = body->platforms;
						bool listed = false;
						for (Body* b : pl) listed = listed || b == obj->body;
						if (!listed) pl.push_back(obj->body);
					}
				}
			}
			lastCrouch = game.frameCounter;
		}
		setState(S_CROUCHING);

	// Climb down ladder
	} else if (checkState(S_JUMPING) || checkState(S_CLIMBING)) {
		for (Obj* t : game.getObjectsByAABB(getBounds())) {
			if (t->type() == ObjType::Ladder) {
				disableGravity = true;
				setState(S_CLIMBING);
				movePosition(0, mod("climbMul", 1));
				break;
			}
		}
	}
}

// Shooting and grenades, once the weapon is drawn
void Player::weaponActions(Button fire, Button grenade) {
	auto gun = [&]() -> Gun* {
		Obj* o = game.getObjectById(inventory[INV_GUN]);
		return (o && o->type() == ObjType::Gun) ? static_cast<Gun*>(o) : nullptr;
	};
	auto throwable = [&]() -> Throwable* {
		Obj* o = game.getObjectById(inventory[INV_THROWABLE]);
		return (o && o->type() == ObjType::Throwable) ? static_cast<Throwable*>(o) : nullptr;
	};

	// Release a cooking grenade
	if (strictState(S_PREPARE)) {
		if (!grenade.held) {
			if (Throwable* t = throwable()) {
				float throwDelay = t->throwIt();
				setState(S_THROWING, throwDelay, {{throwDelay, "reset-aim"}});
			} else {
				setState(S_AIMING);
			}
		}
		return;
	}

	if (!strictState(S_AIMING)) return;

	if (grenadeQueued) {
		grenadeQueued = false;
		Throwable* t = throwable();
		if (t && t->ammo > 0) {
			equiped = INV_THROWABLE;
			t->cook();
			setState(S_PREPARE, INFINITY);
			// Tapped and already released: throw straight away
			if (!grenade.held) {
				float throwDelay = t->throwIt();
				setState(S_THROWING, throwDelay, {{throwDelay, "reset-aim"}});
			}
		}
		return;
	}

	if (fireQueued) {
		fireQueued = false;
		if (Gun* g = gun()) {
			equiped = INV_GUN;
			if (g->ammo <= 0) {
				throwGun(g);
				return;
			}
			float recoilTime = g->shoot();
			setState(S_SHOOTING, recoilTime, {{recoilTime, "reset-aim"}});
		}
	}
}

// Out of ammo: the gun itself is thrown at the crosshair; it knocks down whoever it hits
void Player::throwGun(Gun* g) {
	float angle = getCrosshairAngle();
	Vec2 pos = getCrosshairPosition();
	ObjOptions o;
	o.at(pos).vel({std::cos(angle) * 5.5f, std::sin(angle) * 5.5f - 1.0f}).rot(angle).life(600);
	o.ammo = 0;
	o.isStatic = false;   // weapon pickups float in place; a thrown one flies
	o.angularVelocity = 0.35f * facingDirection;
	if (Obj* t = game.createObject(g->parent, o)) {
		Gun* thrown = static_cast<Gun*>(t);
		thrown->thrownBy = id;
		thrown->thrownFrames = 90;
	}
	inventory[INV_GUN] = -1;
	g->kill();
	equiped = INV_THROWABLE;
	setState(S_THROWING, 12, {{12, "reset-aim"}});
	playSound("punch");
}

void Player::punch(Button button) {
	if (!button.pressed || isAiming() || checkState(S_DAMAGED)) return;

	// Melee Combo 1
	if (checkState(S_GROUNDED) && !checkState(S_ATTACKING) && !checkState(S_ROLLING)) {
		setState(S_COMBO1, 21, {{9, "melee_1"}});

	// Melee Combo 2
	} else if (checkState(S_COMBO1) && delayTimestamp() > 9) {
		setState(S_COMBO2, 21, {{9, "melee_2"}});

	// Melee Combo 3
	} else if (checkState(S_COMBO2) && delayTimestamp() > 9) {
		setState(S_COMBO3, 24, {{9, "melee_3"}});

	// Jump Punching
	} else if (checkState(S_JUMPING) && !checkState(S_ATTACKING)) {
		setState(S_JUMPPUNCH);
		airAttack = 1;
		airAttackUntil = game.frameCounter + 10;
		playSound("punch");
	}
}

void Player::kick(Button button) {
	if (!button.pressed || isAiming() || checkState(S_DAMAGED)) return;

	// Kicking
	if (checkState(S_GROUNDED) && !checkState(S_ATTACKING) && !checkState(S_ROLLING)) {
		setState(S_KICKING, 6);

		Vec2 p = getPosition();
		game.createHitbox(this, {{p.x + width / 2 * facingDirection, p.y + height / 2, 5, 5}}, {0.001f * facingDirection, 0}, {1, "melee"});
		playSound("punch");

	// Jump kicking
	} else if (checkState(S_JUMPING) && !checkState(S_ATTACKING)) {
		setState(S_JUMPKICK);
		airAttack = 2;
		airAttackUntil = game.frameCounter + 10;
		playSound("punch");
	}
}

void Player::interact(Button button) {
	// Pickup weapons
	if (!button.pressed) return;

	bool tookClub = false;
	for (Obj* obj : game.getObjectsByAABB(getBounds())) {
		switch (obj->type()) {
			case ObjType::Base: {
				// pieces of broken furniture make a club, for a few blows
				const std::string& k = obj->parent->key;
				const char* club = nullptr;
				if (k.rfind("crate_debris", 0) == 0 || k.rfind("table_debris", 0) == 0) club = "plank";
				if (k == "pipe") club = "pipe_club";
				if (!club || tookClub) break;
				dropWeapon(INV_MELEE, {facingDirection * -1.0f, -2.0f});
				if (Obj* c = game.createObject(club, ObjOptions().at(obj->getPosition()))) {
					c->pickup(*this);
					inventory[INV_MELEE] = c->id;
					tookClub = true;
					obj->kill();
				}
				break;
			}
			case ObjType::Melee:
				if (obj->id == inventory[INV_MELEE]) break;
				dropWeapon(INV_MELEE, {facingDirection * -1.0f, -2.0f});
				obj->pickup(*this);
				inventory[INV_MELEE] = obj->id;
				break;
			case ObjType::Gun:
				if (obj->id == inventory[INV_GUN]) break;
				dropWeapon(INV_GUN, {facingDirection * -1.0f, -2.0f});
				obj->pickup(*this);
				inventory[INV_GUN] = obj->id;
				break;
			case ObjType::Throwable:
				if (obj->id == inventory[INV_THROWABLE]) break;
				dropWeapon(INV_THROWABLE, {facingDirection * -1.0f, -2.0f});
				obj->pickup(*this);
				inventory[INV_THROWABLE] = obj->id;
				break;
			default:
				break;
		}
	}
}

// Emergency exits work on the move: run into one, come out of the other
void Player::checkDoors() {
	Vec2 p = getPosition();
	auto near = [&](Obj* d) {
		Vec2 q = d->getPosition();
		return std::fabs(q.x - p.x) <= 9 && std::fabs(q.y - p.y) <= 10;
	};

	if (doorLock >= 0) {
		Obj* d = game.getObjectById(doorLock);
		if (!d || !near(d)) doorLock = -1;
	}

	int dir = (input.right ? 1 : 0) - (input.left ? 1 : 0);
	if (dir == 0 || holding >= 0 || checkState(S_CLIMBING) || checkState(S_DAMAGED)) return;

	for (int id_ : game.getDoors()) {
		if (id_ == doorLock) continue;
		Door* door = static_cast<Door*>(game.getObjectById(id_));
		if (!door || !near(door) || (door->into() != 0 && door->into() != dir)) continue;

		if (door->link == "__exit") {   // the way out of the shooting range
			game.exitRequested = true;
			return;
		}

		for (int other : game.getDoors()) {
			Door* exit = static_cast<Door*>(game.getObjectById(other));
			if (!exit || exit->customId != door->link) continue;

			// door objects are as tall as a player: keep the feet on the floor
			Vec2 to = exit->getPosition();
			Vec2 v = getVelocity();
			int outDir = exit->into() != 0 ? -exit->into() : dir;
			setPosition(to.x, to.y);
			setVelocity(outDir * std::fabs(v.x), v.y);
			facingDirection = outDir;
			fallPeak = 0;
			doorLock = exit->id;
			if (outDir != dir) {
				mirrorDir = mirrorDir != 0 ? 0 : dir;   // (already mirrored: back to normal)
				std::swap(lastButtons[K_LEFT], lastButtons[K_RIGHT]);
			}
			playSound("roll");
			return;
		}
	}
}

void Player::aim(Button button) {
	if (!delayDone()) return;

	Obj* g = game.getObjectById(inventory[INV_GUN]);
	Obj* t = game.getObjectById(inventory[INV_THROWABLE]);

	// Choose which inventory item to use when not holding one or the other
	Obj* weapon = nullptr;

	if (equiped == INV_THROWABLE) {
		if (t)
			weapon = t;
		else
			equiped = INV_GUN;
	}

	if (equiped == INV_GUN) {
		if (g)
			weapon = g;
		else
			equiped = INV_THROWABLE;
	}

	if (button.held && checkState(S_GROUNDED) && !checkState(S_AIMING) && weapon) {
		// Not previously aiming then reset crosshair angle
		if (!checkLastState(S_AIMING) && !checkState(S_DRAWING) && !checkLastState(S_DRAWING)) {
			crosshairAngle = 0;
			float delay = weapon->type() == ObjType::Gun ? static_cast<Gun*>(weapon)->pullout()
														 : static_cast<Throwable*>(weapon)->pullout();
			setState(S_DRAWING, delay);

		} else if (checkLastState(S_AIMING) || checkLastState(S_DRAWING)) {
			setState(S_AIMING);
		}
	}
}

// ------------------------------------------------------------------ drawing

namespace {

struct Anim {
	std::vector<Frame> frames;
	std::optional<float> timestamp;
};

}  // namespace

void Player::draw() {
	if (!body || !image) return;

	Obj* mw = game.getObjectById(inventory[INV_MELEE]);
	Obj* gw = game.getObjectById(inventory[INV_GUN]);
	Obj* tw = game.getObjectById(inventory[INV_THROWABLE]);
	Melee* meleeWeapon = (mw && mw->type() == ObjType::Melee) ? static_cast<Melee*>(mw) : nullptr;

	float angle = 0;
	const std::string& sn = state.name;
	auto is = [&](const char* s) { return sn == s; };
	float ts = delayTimestamp();

	auto frames = [&](std::vector<Frame> f, std::optional<float> t = std::nullopt) { setAnimationFrame(f, t); };
	auto setFrame = [&](int x, int y) {
		frameIndexX = x;
		frameIndexY = y;
	};
	auto offset = [&](float x, float y, float a, bool flip = false, bool onTop = false) {
		if (meleeWeapon) {
			meleeWeapon->offset = {x, y, a, flip, onTop};
			meleeWeapon->hasOffset = true;
		}
	};

	// Get animation from state
	if (is(S_GROUNDED)) {
		frames({{0, 0, 12}, {1, 0, 12}, {2, 0, 12}});

	} else if (is(S_WALKING)) {
		frames({{3, 0, 9}, {4, 0, 9}, {5, 0, 9}, {4, 0, 9}});

	} else if (is(S_RUNNING)) {
		frames({{6, 0, 4}, {4, 0, 4}, {7, 0, 4}, {4, 0, 4}});

	} else if (is(S_CROUCHING)) {
		setFrame(0, 1);

	} else if (is(S_ROLLING)) {
		frames({{1, 1, 9}, {2, 1, 9}}, ts);
		angle = facingDirection * (1 - delayPercentNotDone()) * 2 * PI_F;   // one somersault

	} else if (is(S_STUN)) {
		if (getStateEntropy() < 0.5f)
			setFrame(0, 3);
		else
			setFrame(1, 3);

	} else if (is(S_LANDED)) {
		// short: a crouch; long: flat on the floor, then getting up
		if (state.delayMax <= 16)
			setFrame(0, 1);
		else if (state.delay > 14)
			setFrame(6, 3);
		else
			setFrame(8, 1);

	} else if (is(S_GRAB)) {
		setFrame(0, 8);

	} else if (is(S_TOSS)) {
		setFrame(2, 4);

	} else if (is(S_HELD)) {
		setFrame(3, 1);

	} else if (is(S_JUMPING)) {
		setFrame(4, 1);

	} else if (is(S_RECOVERING)) {
		frames({{6, 3, 15}, {8, 1, 15}}, ts);

	} else if (is(S_KICKING)) {
		setFrame(0, 2);

	} else if (is(S_COMBO1) || is(S_COMBO2) || is(S_COMBO3)) {
		int combo = is(S_COMBO1) ? 0 : (is(S_COMBO2) ? 1 : 2);
		int hands = meleeWeapon ? meleeWeapon->hands : 0;

		if (combo == 0) {
			if (hands == 0) {
				frames({{1, 2, 9}, {2, 2, 12}}, ts);
			} else if (hands == 1) {
				frames({{0, 7, 9}, {1, 7, 12}}, ts);
				if (frameIndexX == 0) offset(-2, -9, 180);
				if (frameIndexX == 1) offset(-1, 1, 225, true, true);
			} else {
				frames({{0, 8, 9}, {1, 8, 12}}, ts);
				if (frameIndexX == 0) offset(6, -4, 225);
				if (frameIndexX == 1) offset(-4, 1, 180);
			}
		} else if (combo == 1) {
			if (hands == 0) {
				frames({{3, 2, 9}, {4, 2, 12}}, ts);
			} else if (hands == 1) {
				frames({{1, 7, 9}, {2, 7, 12}}, ts);
				if (frameIndexX == 1) offset(-1, 1, 225, true, true);
				if (frameIndexX == 2) offset(-5, -4, 225);
			} else {
				frames({{2, 8, 9}, {3, 8, 12}}, ts);
				if (frameIndexX == 2) offset(-4, 0, 225, true, true);
				if (frameIndexX == 3) offset(6, -4, 225, true);
			}
		} else {
			if (hands == 0) {
				frames({{5, 2, 9}, {6, 2, 18}}, ts);
			} else if (hands == 1) {
				frames({{2, 7, 9}, {3, 7, 18}}, ts);
				if (frameIndexX == 2) offset(-5, -4, 225);
				if (frameIndexX == 3) offset(5, 4, 0);
			} else {
				frames({{4, 8, 9}, {5, 8, 18}}, ts);
				if (frameIndexX == 4) offset(0, -8, 135);
				if (frameIndexX == 5) offset(0, 4, 0);
			}
		}

	} else if (is(S_JUMPPUNCH)) {
		setFrame(8, 2);

	} else if (is(S_JUMPKICK)) {
		setFrame(7, 2);

	} else if (is(S_FREEFALL)) {
		if (getStateEntropy() < 0.5f)
			frames({{5, 1, 6}, {6, 1, 6}});
		else
			setFrame(7, 1);

		angle = std::fmod((float)(SDL_GetTicks() / 2), 360.0f) * DEG;

	} else if (checkState(S_DRAWING) || checkState(S_AIMING)) {
		// (the JS switch only matches the exact drawing / aiming states)
		if (is(S_DRAWING) || is(S_AIMING)) {
			if (equiped == INV_GUN)
				setFrame(0, 4);
			else
				setFrame(1, 4);
		} else if (is(S_THROWING)) {
			setFrame(2, 4);
		}

	} else if (is(S_DIVE)) {
		setFrame(3, 1);   // stretched out flat

	} else if (is(S_CLIMBING)) {
		frames({{1, 6, 4}, {2, 6, 4}, {3, 6, 4}, {2, 6, 4}}, std::fabs(getPosition().y));
	}

	// Set draw order of cosmetics
	std::vector<Texture*> drawOrder{image};
	{
		auto& reg = Registry::instance();
		for (auto& [slot, name] : profile) {
			auto cat = reg.apparel.find(slot);
			if (cat == reg.apparel.end()) continue;
			auto item = cat->second.find(name);
			if (item != cat->second.end()) drawOrder.push_back(platform::image(item->second));
		}
	}

	Vec2 pos = getPosition();

	// Draw weapon when performing melee combo
	auto drawMelee = [&]() {
		if (!meleeWeapon || !meleeWeapon->image) return;
		SpriteDraw d;
		d.tex = meleeWeapon->image;
		d.sx = 0;
		d.sy = 0;
		d.sw = d.dw = (int)meleeWeapon->frameW;
		d.sh = d.dh = (int)meleeWeapon->frameH;
		d.offX = -2;
		d.offY = -2;
		d.ox = pos.x + meleeWeapon->offset.x * facingDirection;
		d.oy = pos.y + meleeWeapon->offset.y;
		d.angle = meleeWeapon->offset.angle * DEG * facingDirection;
		d.scaleX = (float)facingDirection;
		d.scaleY = meleeWeapon->offset.flip ? -1.0f : 1.0f;
		platform::drawSprite(d);
	};

	bool melee = checkState(S_MELEE) && meleeWeapon && meleeWeapon->hasOffset;

	// Draw melee over player
	if (melee && !meleeWeapon->offset.onTop) drawMelee();

	// Walking while aiming: the aiming frame on top, walking legs below
	bool aimWalk = (checkState(S_AIMING) || checkState(S_DRAWING)) && game.frameCounter - aimWalkFrame <= 1;
	const int legY = 17;   // first sprite row of the legs
	int walkX = 4;
	if (aimWalk) {
		static const int cycle[4] = {3, 4, 5, 4};
		walkX = cycle[(game.frameCounter / 9) % 4];
	}

	// Draw main body
	for (Texture* tex : drawOrder) {
		if (aimWalk) {
			for (int part = 0; part < 2; part++) {
				SpriteDraw d;
				d.tex = tex;
				int top = part == 0 ? 0 : legY;
				int rows = part == 0 ? legY : (int)frameH - legY;
				d.sx = (int)((part == 0 ? frameIndexX : walkX) * frameW);
				d.sy = (int)((part == 0 ? frameIndexY : 0) * frameH) + top;
				d.sw = d.dw = (int)frameW;
				d.sh = d.dh = rows;
				d.offX = -frameW / 2;
				d.offY = height / 2 - frameH + top;
				d.ox = pos.x;
				d.oy = pos.y;
				d.scaleX = (float)facingDirection;
				platform::drawSprite(d);
			}
			continue;
		}
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
		d.angle = angle;
		platform::drawSprite(d);
	}

	// Draw melee under player
	if (melee && meleeWeapon->offset.onTop) drawMelee();

	// Draw upper torso when aiming
	if ((checkState(S_AIMING) || checkState(S_DRAWING)) && !checkState(S_THROWING)) {
		Obj* weapon = equiped == INV_GUN ? gw : tw;

		// Get the recoil
		float recoil = 0;
		if (checkState(S_SHOOTING)) recoil = std::sin(delayTimestamp() / state.delayMax * PI_F);

		bool drawing = checkState(S_DRAWING);
		int aimX = drawing ? 0 : 1, aimY = 5;

		float cAngle = getCrosshairAngle();
		Vec2 origin{pos.x - facingDirection * 2.0f, pos.y - 2.0f};

		// Draw weapon
		if (!drawing && weapon && weapon->image) {
			SpriteDraw d;
			d.tex = weapon->image;
			d.sx = d.sy = 0;
			d.sw = d.dw = (int)weapon->frameW;
			d.sh = d.dh = (int)weapon->frameH;
			d.offX = (frameW / 2 - weapon->frameW / 2) - recoil;
			d.offY = -(weapon->frameH * 3 / 4);
			d.ox = origin.x;
			d.oy = origin.y;
			d.scaleY = (float)facingDirection;
			d.angle = cAngle;
			platform::drawSprite(d);
		}

		// Draw upper body
		for (Texture* tex : drawOrder) {
			SpriteDraw d;
			d.tex = tex;
			d.sx = (int)(frameW * aimX);
			d.sy = (int)(frameH * aimY);
			d.sw = d.dw = (int)frameW;
			d.sh = d.dh = (int)frameH;
			d.offX = -frameW / 2 - recoil + 2;
			d.offY = height / 2 - frameH + 2;
			d.ox = origin.x;
			d.oy = origin.y;
			d.scaleY = (float)facingDirection;
			d.angle = cAngle;
			platform::drawSprite(d);
		}
	}

	// Crosshair for human players
	if (!isBot && (strictState(S_AIMING) || checkState(S_SHOOTING) || strictState(S_PREPARE))) {
		static Texture* cross = platform::image("images/crosshair.png");
		if (cross) {
			float cAngle = getCrosshairAngle();
			Vec2 origin{pos.x - facingDirection * 2.0f, pos.y - 2.0f};
			SpriteDraw d;
			d.tex = cross;
			d.sw = d.dw = cross->w;
			d.sh = d.dh = cross->h;
			d.ox = origin.x + std::cos(cAngle) * 46;
			d.oy = origin.y + std::sin(cAngle) * 46;
			d.offX = -cross->w / 2.0f;
			d.offY = -cross->h / 2.0f;
			platform::drawSprite(d);
		}
	}
}
