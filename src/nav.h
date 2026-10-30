// Navigation graph for bots, built from the level collision when a map is loaded.
#pragma once

#include <vector>

#include "physics.h"

class Game;

struct NavEdge {
	enum Kind { Walk, Jump, Drop, WalkOff, Climb, Door } kind;
	int to;
	float cost;
};

struct NavNode {
	float x, y;          // standing point (feet)
	bool ledge;          // standing on a one-way platform
	bool acid = false;   // under the surface of an acid pool
	int ladderId = -1;   // climb edges: ladder object
	int doorId = -1;     // door edges: door object
	std::vector<NavEdge> edges;
};

class NavGraph {
public:
	void build(Game& g);
	bool empty() const { return nodes.empty(); }
	// nearest node a standing (or falling) body at p belongs to; -1 if none
	int nearest(Vec2 p, float maxDist = 60) const;
	// node sequence from a to b (inclusive); empty when unreachable
	std::vector<int> path(int a, int b) const;
	const NavEdge* edge(int a, int b) const;

	std::vector<NavNode> nodes;
};
