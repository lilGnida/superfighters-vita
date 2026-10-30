// Computer-controlled fighters: produce the same PlayerInput a human would.
#pragma once

#include <random>
#include <vector>

#include "physics.h"
#include "platform.h"

class Game;
class Player;
class Obj;

enum class BotDifficulty { Easy = 0, Normal = 1, Hard = 2 };

class Bot {
public:
	Bot(BotDifficulty difficulty, unsigned seed);
	PlayerInput think(Game& game, Player& self);
	char debug[160] = "";   // last decision, for diagnostics

private:
	struct Params {
		int reaction;        // frames a target must be visible before shooting
		float aimError;      // radians
		int fireInterval;    // frames between shots
		int meleeInterval;   // frames between punches / kicks
		float grenadeRate;   // chance per second to throw a grenade at a visible target
		float grabRate;      // chance of a grab instead of a blow
		int struggle;        // frames between struggling presses when held
		float runRate;       // share of the time spent running
		float diveRate;      // chance per frame of diving at someone within reach
	} p;
	std::mt19937 rng;

	// All timers below count game frames, and the frame counter restarts with every round
	void newRound();
	int lastFrame = -1;

	int targetId = -1;
	int seenFrames = 0;
	int lastFire = -1000, lastMelee = -1000, nextJump = 0;
	int cookUntil = -1;
	int tossAt = -1;      // holding a fighter: throw at this frame
	float aimOffset = 0;
	int aimOffsetUntil = 0;
	float lastX = 0, stuckX = 0;
	int stuckFrames = 0, reverseUntil = -1, reverseDir = 0;
	int dropStage = 0, dropFrame = 0;
	int climbLadder = -1;
	// something loose in the way: what to do about it
	enum class Plan { Hop, Break, Lift };
	int obstacleId = -1, obstacleSince = 0;
	Plan obstaclePlan = Plan::Hop;
	bool wantRun = false, dive = false;   // dive: this jump is meant as one
	int runRollAt = 0, lastDive = -1000;
	// nothing has happened for a while (no move, nobody hurt): go somewhere else
	Vec2 calmAt{0, 0};
	float calmHp = 0, calmTargetHp = 0;
	int calmSince = 0, wanderUntil = -1, wanderNode = -1;
	std::vector<int> route;
	int routeGoal = -1, curNode = -1, repathAt = 0;
	float airTargetX = -1e9f;
	bool lastJump = false, lastPunch = false, lastKick = false, lastFireBtn = false, lastInteract = false, lastGrab = false;

	float rnd() { return std::uniform_real_distribution<float>(0, 1)(rng); }
	bool tap(bool& last, bool want) {   // a press: down for one frame, then released
		bool out = want && !last;
		last = out;
		return out;
	}
	Player* pickTarget(Game& g, Player& self);
	Obj* pickItem(Game& g, Player& self);
	bool lineOfSight(Game& g, float ax, float ay, float bx, float by);
	bool safeAhead(Game& g, Player& self, int dir);
	void moveTowards(Game& g, Player& self, float gx, float gy, PlayerInput& in);
	void unstick(Game& g, Player& self, PlayerInput& in);
	bool obstacle(Game& g, Player& self, int dir, PlayerInput& in);
	PlayerInput decide(Game& g, Player& self);
	void aimAt(Game& g, Player& self, float tx, float ty, float lob, PlayerInput& in);
	bool wander(Game& g, Player& self, Player* target, PlayerInput& in);
	bool tryDive(Game& g, Player& self, int dir, float chance, PlayerInput& in);
};
