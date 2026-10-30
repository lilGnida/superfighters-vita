#include "nav.h"

#include <algorithm>
#include <cmath>
#include <queue>

#include "game.h"

namespace {

constexpr float STEP = 6;           // horizontal sampling
constexpr float HEAD = 18;          // headroom a fighter needs
constexpr float JUMP_UP = 27;       // highest reachable step with a jump
constexpr float JUMP_SIDE = 30;
constexpr float DROP_SIDE = 26;

enum class Hit { None, Wall, Ledge };

Hit classify(Game& g, Body* b) {
	if (!b || !b->isStatic) return Hit::None;
	Obj* o = g.getObjectByBody(b);
	if (!o) return Hit::Wall;
	if (o->type() == ObjType::Platform) return Hit::Ledge;
	if (o->type() == ObjType::Base && o->parent && o->parent->key != "glass") return Hit::Wall;
	return Hit::None;
}

bool wallBetween(Game& g, Vec2 a, Vec2 b) {
	return g.physics().raycast(a, b, [&](Body* bd) { return classify(g, bd) == Hit::Wall; });
}

}  // namespace

void NavGraph::build(Game& g) {
	nodes.clear();
	if (!g.levelBounds) return;
	Bounds lb = *g.levelBounds;
	float deathY = g.deathY ? *g.deathY : lb.max.y + 1000;

	// 1. standing points: walk each column from the top down, collecting surfaces
	for (float x = lb.min.x + 3; x < lb.max.x - 3; x += STEP) {
		float y = lb.min.y - 40;
		for (int guard = 0; guard < 24 && y < lb.max.y; guard++) {
			Vec2 hit;
			Body* body = nullptr;
			bool any = g.physics().raycast({x, y}, {x, lb.max.y + 40},
										   [&](Body* bd) { return classify(g, bd) != Hit::None; }, &hit, &body);
			if (!any) break;
			Hit kind = classify(g, body);
			bool headroom = !g.physics().raycast({x, hit.y - 1}, {x, hit.y - HEAD},
												 [&](Body* bd) { return classify(g, bd) == Hit::Wall; });
			if (headroom && hit.y < deathY - 2) {
				NavNode n;
				n.x = x;
				n.y = hit.y;
				n.ledge = kind == Hit::Ledge;
				n.acid = g.inAcid({x, hit.y - 2});
				nodes.push_back(n);
			}
			// continue below this surface
			y = (kind == Hit::Ledge) ? hit.y + 4 : body->bounds().max.y + 1;
		}
	}

	auto addEdge = [&](int a, int b, NavEdge::Kind k, float cost) {
		// never a short cut through the acid, but the way out of it stays open
		if (nodes[b].acid && !nodes[a].acid) cost += 2000;
		nodes[a].edges.push_back({k, b, cost});
	};

	// 2. walking, jumping and dropping between nearby points
	for (int i = 0; i < (int)nodes.size(); i++) {
		NavNode& a = nodes[i];
		bool hasLeft = false, hasRight = false;
		for (int j = 0; j < (int)nodes.size(); j++) {
			if (i == j) continue;
			const NavNode& b = nodes[j];
			float dx = b.x - a.x, dy = b.y - a.y;
			if (std::fabs(dx) > JUMP_SIDE + 1) continue;

			if (std::fabs(std::fabs(dx) - STEP) < 0.5f && std::fabs(dy) <= 7) {
				if (!wallBetween(g, {a.x, a.y - 8}, {b.x, b.y - 8})) {
					addEdge(i, j, NavEdge::Walk, STEP);
					(dx > 0 ? hasRight : hasLeft) = true;
				}
			}
		}
		for (int j = 0; j < (int)nodes.size(); j++) {
			if (i == j) continue;
			const NavNode& b = nodes[j];
			float dx = b.x - a.x, dy = b.y - a.y;
			// jump up onto a higher surface
			if (dy < -7 && dy >= -JUMP_UP && std::fabs(dx) <= JUMP_SIDE) {
				bool clear = !wallBetween(g, {a.x, a.y - 2}, {a.x, b.y - HEAD}) &&
							 !wallBetween(g, {a.x, b.y - HEAD}, {b.x, b.y - HEAD + 2}) &&
							 !wallBetween(g, {b.x, b.y - HEAD}, {b.x, b.y - 2});
				if (clear) addEdge(i, j, NavEdge::Jump, 18 + std::fabs(dx) + -dy);
			}
			// jump across a gap on the same level
			if (std::fabs(dy) <= 7 && std::fabs(dx) >= 12 && ((dx > 0 && !hasRight) || (dx < 0 && !hasLeft))) {
				bool clear = !wallBetween(g, {a.x, a.y - 8}, {b.x, b.y - 8}) && !wallBetween(g, {a.x, a.y - HEAD}, {b.x, b.y - HEAD});
				if (clear) addEdge(i, j, NavEdge::Jump, 18 + std::fabs(dx));
			}
			// fall down: through a ledge, or off the end of a floor
			if (dy > 7 && std::fabs(dx) <= DROP_SIDE) {
				bool clearPath = !wallBetween(g, {b.x, a.y - 6}, {b.x, b.y - 2}) &&
								 !wallBetween(g, {a.x, a.y - 6}, {b.x, a.y - 6});
				if (!clearPath) continue;
				if (a.ledge && std::fabs(dx) < STEP * 0.6f)
					addEdge(i, j, NavEdge::Drop, 10 + dy * 0.2f);
				else if ((dx > 0 && !hasRight) || (dx < 0 && !hasLeft))
					addEdge(i, j, NavEdge::WalkOff, 8 + std::fabs(dx) + dy * 0.2f);
			}
		}
	}

	// 3. ladders: bottom <-> top
	for (Obj* l : g.getObjectsByParent(Registry::instance().get("climb"))) {
		Bounds b = l->getBounds();
		float lx = (b.min.x + b.max.x) / 2;
		int bottom = -1, top = -1;
		float bestB = 1e9f, bestT = 1e9f;
		for (int i = 0; i < (int)nodes.size(); i++) {
			const NavNode& n = nodes[i];
			float ax = std::fabs(n.x - lx);
			if (ax > 20) continue;
			float db = std::fabs(n.y - b.max.y) + ax;
			if (n.y >= b.max.y - 14 && n.y <= b.max.y + 14 && db < bestB) {
				bestB = db;
				bottom = i;
			}
			float dt = std::fabs(n.y - b.min.y) + ax;
			if (n.y <= b.min.y + 24 && n.y >= b.min.y - 10 && dt < bestT) {
				bestT = dt;
				top = i;
			}
		}
		if (bottom >= 0 && top >= 0 && bottom != top) {
			float c = (nodes[bottom].y - nodes[top].y) * 1.5f + 10;
			nodes[bottom].ladderId = l->id;
			nodes[top].ladderId = l->id;
			addEdge(bottom, top, NavEdge::Climb, c);
			addEdge(top, bottom, NavEdge::Climb, c);
		}
	}

	// 4. emergency exits
	for (Obj* d : g.getObjectsByParent(Registry::instance().get("exit_door"))) {
		Door* door = static_cast<Door*>(d);
		Obj* other = nullptr;
		for (Obj* o : g.getObjectsByParent(door->parent))
			if (o->customId == door->link) other = o;
		if (!other) continue;
		int a = nearest({d->getPosition().x, d->getPosition().y + 9.5f}, 14);
		int b = nearest({other->getPosition().x, other->getPosition().y + 9.5f}, 14);
		if (a >= 0 && b >= 0 && a != b) {
			nodes[a].doorId = d->id;
			addEdge(a, b, NavEdge::Door, 30);
		}
	}
}

int NavGraph::nearest(Vec2 p, float maxDist) const {
	int best = -1;
	float bestD = maxDist;
	for (int i = 0; i < (int)nodes.size(); i++) {
		const NavNode& n = nodes[i];
		float dy = n.y - p.y;
		// prefer the surface under the feet: points above count three times as far
		float d = std::fabs(n.x - p.x) + (dy >= -2 ? dy : -dy * 3);
		if (d < bestD) {
			bestD = d;
			best = i;
		}
	}
	return best;
}

std::vector<int> NavGraph::path(int a, int b) const {
	std::vector<int> out;
	if (a < 0 || b < 0) return out;
	std::vector<float> dist(nodes.size(), 1e18f);
	std::vector<int> prev(nodes.size(), -1);
	using Q = std::pair<float, int>;
	std::priority_queue<Q, std::vector<Q>, std::greater<Q>> q;
	dist[a] = 0;
	q.push({0, a});
	while (!q.empty()) {
		auto [d, u] = q.top();
		q.pop();
		if (d > dist[u]) continue;
		if (u == b) break;
		for (const NavEdge& e : nodes[u].edges) {
			float nd = d + e.cost;
			if (nd < dist[e.to]) {
				dist[e.to] = nd;
				prev[e.to] = u;
				q.push({nd, e.to});
			}
		}
	}
	if (dist[b] >= 1e18f) return out;
	for (int v = b; v >= 0; v = prev[v]) out.push_back(v);
	std::reverse(out.begin(), out.end());
	return out;
}

const NavEdge* NavGraph::edge(int a, int b) const {
	if (a < 0 || a >= (int)nodes.size()) return nullptr;
	for (const NavEdge& e : nodes[a].edges)
		if (e.to == b) return &e;
	return nullptr;
}
