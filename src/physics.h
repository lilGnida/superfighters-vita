// Thin Matter.js-flavoured wrapper over Box2D.
//
// The game logic (ported from the JS original) is written in Matter units:
//   positions in pixels, velocities in pixels per 1/60s step, forces such that
//   gravity = 0.001 * mass.  This layer converts those to Box2D units
//   (metres, m/s) and reproduces Matter's collision-filter rules
//   (category / mask / "above" + one-way platforms).
#pragma once

#include <box2d/box2d.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

constexpr float PTM = 16.0f;                   // pixels per metre
constexpr float STEP_MS = 1000.0f / 60.0f;     // one game step in ms
constexpr float STEP_MS2 = STEP_MS * STEP_MS;  // Matter: dv = F/m * dt^2
constexpr float PI_F = 3.14159265358979f;

struct Vec2 {
	float x = 0, y = 0;
	Vec2() = default;
	Vec2(float x_, float y_) : x(x_), y(y_) {}
	Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
	Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
	Vec2 operator*(float s) const { return {x * s, y * s}; }
	float length() const { return std::sqrt(x * x + y * y); }
	Vec2 normalised() const {
		float l = length();
		return l == 0 ? Vec2{0, 0} : Vec2{x / l, y / l};
	}
};

struct Bounds {
	Vec2 min, max;
	static Bounds fromPoints(const std::vector<Vec2>& pts) {
		Bounds b{{1e30f, 1e30f}, {-1e30f, -1e30f}};
		for (auto& p : pts) {
			if (p.x < b.min.x) b.min.x = p.x;
			if (p.y < b.min.y) b.min.y = p.y;
			if (p.x > b.max.x) b.max.x = p.x;
			if (p.y > b.max.y) b.max.y = p.y;
		}
		return b;
	}
	bool overlaps(const Bounds& o) const {
		return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y;
	}
};

struct CollisionFilter {
	uint32_t category = 1;
	uint32_t mask = 0xFFFFFFFFu;
	uint32_t above = 0;
	int group = 0;
};

enum class ShapeType { Rect, Circle, Poly };

struct BodySpec {
	ShapeType shape = ShapeType::Rect;
	float width = 0, height = 0;   // Rect
	float radius = 0;              // Circle
	std::vector<Vec2> verts;       // Poly (any origin, re-centred on centroid)
	Vec2 position, velocity;
	float angle = 0, angularVelocity = 0;
	bool isStatic = false, isSensor = false, inertiaInfinite = false;
	float friction = 0.1f, restitution = 0.0f, frictionAir = 0.01f;
	CollisionFilter filter;
};

class PhysicsWorld;

class Body {
public:
	b2Body* b2 = nullptr;
	b2Fixture* fixture = nullptr;
	PhysicsWorld* world = nullptr;
	int clientId = -1;           // id of the owning game object

	bool isStatic = false, isSensor = false, inertiaInfinite = false;
	float mass = 0;              // Matter-style mass (area * 0.001), INFINITY when static
	float friction = 0.1f, restitution = 0.0f;
	CollisionFilter filter;
	std::vector<Body*> platforms;  // one-way platforms currently being passed through
	Vec2 velPrev;                  // diagnostics

	Vec2 position() const;
	void setPosition(Vec2 p);
	Vec2 velocity() const;          // px / step
	void setVelocity(Vec2 v);
	float angle() const;
	void setAngle(float a);
	float angularVelocity() const;  // rad / step
	void setAngularVelocity(float w);
	// Matter-style force: acceleration = F / mass * dt^2 (px/step^2), applied as impulse at `point` (px)
	void applyForce(Vec2 point, Vec2 force);
	void setEnabled(bool on);       // off: out of the simulation, nothing touches it
	Bounds bounds() const;
};

struct QueryHit {
	Body* body;
	int pointCount;   // contact manifold points (>1 means an edge overlap)
	Vec2 point;       // first contact point (px)
};

class PhysicsWorld : public b2ContactFilter, public b2ContactListener {
public:
	PhysicsWorld();
	~PhysicsWorld();

	Body* createBody(const BodySpec& spec, int clientId);
	void destroyBody(Body* body);
	void step();

	// Matter.Query.region
	std::vector<Body*> queryRegion(const Bounds& bounds) const;
	// Matter.Query.collides against a temporary circle / box (filters ignored)
	std::vector<QueryHit> collideCircle(Vec2 centre, float radius) const;
	std::vector<QueryHit> collideBox(Vec2 centre, float width, float height) const;
	// First body along the segment a->b that `accept` agrees to; returns false when nothing is hit.
	bool raycast(Vec2 a, Vec2 b, const std::function<bool(Body*)>& accept, Vec2* hit = nullptr, Body** hitBody = nullptr) const;

	// All currently touching (and enabled) contacts, for the game's collision callbacks.
	struct Contact {
		Body* a;
		Body* b;
		Vec2 normal;   // from a to b (pixel space, unit)
	};
	std::vector<Contact> touchingContacts() const;

	// A distance joint between two bodies (Matter.Constraint with default stiffness).
	b2Joint* createDistanceJoint(Body* a, Body* b);
	void destroyJoint(b2Joint* j);

	const std::vector<Body*>& bodies() const { return bodies_; }

	// b2ContactFilter
	bool ShouldCollide(b2Fixture* fa, b2Fixture* fb) override;
	// b2ContactListener
	void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override;

private:
	std::unique_ptr<b2World> world_;
	std::vector<Body*> bodies_;
};
