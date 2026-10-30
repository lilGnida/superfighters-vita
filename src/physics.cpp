#include "physics.h"

#include <algorithm>

namespace {

constexpr float PX_STEP_TO_MS = 60.0f / PTM;  // px/step -> m/s

inline Body* bodyOf(b2Fixture* f) {
	return reinterpret_cast<Body*>(f->GetBody()->GetUserData().pointer);
}

inline Vec2 toPx(const b2Vec2& v) { return {v.x * PTM, v.y * PTM}; }
inline b2Vec2 toM(const Vec2& v) { return {v.x / PTM, v.y / PTM}; }

float polyArea(const std::vector<Vec2>& v) {
	float a = 0;
	for (size_t i = 0; i < v.size(); i++) {
		const Vec2& p = v[i];
		const Vec2& q = v[(i + 1) % v.size()];
		a += p.x * q.y - q.x * p.y;
	}
	return std::fabs(a) * 0.5f;
}

Vec2 polyCentroid(const std::vector<Vec2>& v) {
	float cx = 0, cy = 0, a = 0;
	for (size_t i = 0; i < v.size(); i++) {
		const Vec2& p = v[i];
		const Vec2& q = v[(i + 1) % v.size()];
		float cross = p.x * q.y - q.x * p.y;
		a += cross;
		cx += (p.x + q.x) * cross;
		cy += (p.y + q.y) * cross;
	}
	if (a == 0) return {0, 0};
	return {cx / (3 * a), cy / (3 * a)};
}

float dampingFor(float frictionAir) {
	if (frictionAir <= 0) return 0;
	if (frictionAir >= 0.99f) frictionAir = 0.99f;
	return frictionAir / (1.0f - frictionAir) * 60.0f;
}

}  // namespace

// ---------------------------------------------------------------- Body

Vec2 Body::position() const { return toPx(b2->GetPosition()); }

void Body::setPosition(Vec2 p) { b2->SetTransform(toM(p), b2->GetAngle()); }

Vec2 Body::velocity() const {
	if (isStatic) return {0, 0};
	b2Vec2 v = b2->GetLinearVelocity();
	return {v.x / PX_STEP_TO_MS, v.y / PX_STEP_TO_MS};
}

void Body::setVelocity(Vec2 v) {
	if (isStatic) return;
	b2->SetLinearVelocity({v.x * PX_STEP_TO_MS, v.y * PX_STEP_TO_MS});
}

float Body::angle() const { return b2->GetAngle(); }

void Body::setAngle(float a) { b2->SetTransform(b2->GetPosition(), a); }

float Body::angularVelocity() const {
	if (isStatic) return 0;
	return b2->GetAngularVelocity() / 60.0f;
}

void Body::setAngularVelocity(float w) {
	if (isStatic || inertiaInfinite) return;
	b2->SetAngularVelocity(w * 60.0f);
}

void Body::applyForce(Vec2 point, Vec2 force) {
	if (isStatic || mass <= 0 || std::isinf(mass)) return;
	// Matter: delta velocity (px/step) = F / m * dt^2
	float k = STEP_MS2 / mass * PX_STEP_TO_MS * b2->GetMass();
	b2->ApplyLinearImpulse({force.x * k, force.y * k}, toM(point), true);
}

void Body::setEnabled(bool on) { b2->SetEnabled(on); }

Bounds Body::bounds() const {
	b2AABB aabb;
	fixture->GetShape()->ComputeAABB(&aabb, b2->GetTransform(), 0);
	return {toPx(aabb.lowerBound), toPx(aabb.upperBound)};
}

// ---------------------------------------------------------------- World

PhysicsWorld::PhysicsWorld() {
	world_ = std::make_unique<b2World>(b2Vec2(0, 0));
	world_->SetAllowSleeping(false);
	world_->SetContactFilter(this);
	world_->SetContactListener(this);
}

PhysicsWorld::~PhysicsWorld() {
	for (Body* b : bodies_) delete b;
	bodies_.clear();
}

Body* PhysicsWorld::createBody(const BodySpec& spec, int clientId) {
	Body* body = new Body();
	body->world = this;
	body->clientId = clientId;
	body->isStatic = spec.isStatic;
	body->isSensor = spec.isSensor;
	body->inertiaInfinite = spec.inertiaInfinite;
	body->friction = spec.friction;
	body->restitution = spec.restitution;
	body->filter = spec.filter;

	b2BodyDef bd;
	bd.type = spec.isStatic ? b2_staticBody : b2_dynamicBody;
	bd.position = toM(spec.position);
	bd.angle = spec.angle;
	bd.fixedRotation = spec.inertiaInfinite;
	bd.allowSleep = false;
	bd.linearDamping = dampingFor(spec.frictionAir);
	bd.angularDamping = dampingFor(spec.frictionAir);
	bd.userData.pointer = reinterpret_cast<uintptr_t>(body);
	body->b2 = world_->CreateBody(&bd);

	b2FixtureDef fd;
	fd.density = 1.0f;
	fd.friction = spec.friction;
	fd.restitution = spec.restitution;
	fd.isSensor = spec.isSensor;
	fd.userData.pointer = reinterpret_cast<uintptr_t>(body);

	float area = 0;
	b2PolygonShape poly;
	b2CircleShape circle;

	switch (spec.shape) {
		case ShapeType::Circle:
			circle.m_radius = spec.radius / PTM;
			fd.shape = &circle;
			area = PI_F * spec.radius * spec.radius;
			break;

		case ShapeType::Poly: {
			Vec2 c = polyCentroid(spec.verts);
			b2Vec2 pts[b2_maxPolygonVertices];
			int n = std::min<int>((int)spec.verts.size(), b2_maxPolygonVertices);
			for (int i = 0; i < n; i++) pts[i] = toM(spec.verts[i] - c);
			poly.Set(pts, n);
			fd.shape = &poly;
			area = polyArea(spec.verts);
			break;
		}

		case ShapeType::Rect:
		default:
			poly.SetAsBox(spec.width * 0.5f / PTM, spec.height * 0.5f / PTM);
			fd.shape = &poly;
			area = spec.width * spec.height;
			break;
	}

	body->fixture = body->b2->CreateFixture(&fd);
	body->mass = spec.isStatic ? INFINITY : area * 0.001f;

	bodies_.push_back(body);

	if (!spec.isStatic) {
		body->setVelocity(spec.velocity);
		body->setAngularVelocity(spec.angularVelocity);
	}
	return body;
}

void PhysicsWorld::destroyBody(Body* body) {
	if (!body) return;

	bodies_.erase(std::remove(bodies_.begin(), bodies_.end(), body), bodies_.end());
	for (Body* other : bodies_) {
		auto& pl = other->platforms;
		pl.erase(std::remove(pl.begin(), pl.end(), body), pl.end());
	}
	world_->DestroyBody(body->b2);
	delete body;
}

void PhysicsWorld::step() {
	// Matter.Body.update patch: forget one-way platforms we no longer overlap
	for (Body* body : bodies_) {
		auto& pl = body->platforms;
		for (size_t i = 0; i < pl.size();) {
			Body* p = pl[i];
			bool overlap = b2TestOverlap(body->fixture->GetShape(), 0, p->fixture->GetShape(), 0,
										 body->b2->GetTransform(), p->b2->GetTransform());
			if (!overlap)
				pl.erase(pl.begin() + i);
			else
				i++;
		}
	}

	world_->Step(1.0f / 60.0f, 8, 3);

	// Box2D refreshes contacts at the *start* of a step, but the game (like Matter's detector)
	// wants them for the positions reached at the end of it. A zero-length step re-runs the
	// contact update without integrating anything.
	world_->Step(0.0f, 0, 0);
}

// Matter.Detector.canCollide with the SuperFighters "above" patch.
bool PhysicsWorld::ShouldCollide(b2Fixture* fa, b2Fixture* fb) {
	const CollisionFilter& A = bodyOf(fa)->filter;
	const CollisionFilter& B = bodyOf(fb)->filter;

	if ((A.above & B.category) != 0 && (B.mask & A.category) != 0) return true;
	if ((B.above & A.category) != 0 && (A.mask & B.category) != 0) return true;

	if (A.group == B.group && A.group != 0) return A.group > 0;

	return (A.mask & B.category) != 0 && (B.mask & A.category) != 0;
}

// One-way platform handling (Matter.Collision.collides patch) + Matter friction mixing.
void PhysicsWorld::PreSolve(b2Contact* contact, const b2Manifold*) {
	Body* A = bodyOf(contact->GetFixtureA());
	Body* B = bodyOf(contact->GetFixtureB());

	contact->SetFriction(std::min(A->friction, B->friction));
	contact->SetRestitution(std::max(A->restitution, B->restitution));

	const CollisionFilter& fA = A->filter;
	const CollisionFilter& fB = B->filter;

	Body* platform = nullptr;
	Body* above = nullptr;

	if ((fA.above & fB.category) != 0 && (fB.mask & fA.category) != 0) {
		platform = B;
		above = A;
	}
	if ((fB.above & fA.category) != 0 && (fA.mask & fB.category) != 0) {
		platform = A;
		above = B;
	}

	if (!above || !platform) return;

	auto it = std::find(above->platforms.begin(), above->platforms.end(), platform);
	bool listed = it != above->platforms.end();

	// Going up: pass through, remember the platform if we are below it
	if (above->velocity().y - platform->velocity().y < 0) {
		if (!listed && above->position().y > platform->position().y) above->platforms.push_back(platform);
		contact->SetEnabled(false);

	// Going down: keep passing through while still overlapping a remembered platform
	} else if (listed) {
		contact->SetEnabled(false);
	}
}

bool PhysicsWorld::raycast(Vec2 a, Vec2 b, const std::function<bool(Body*)>& accept, Vec2* hit, Body** hitBody) const {
	struct CB : b2RayCastCallback {
		const std::function<bool(Body*)>* accept;
		bool found = false;
		b2Vec2 point;
		Body* body = nullptr;
		float ReportFixture(b2Fixture* f, const b2Vec2& p, const b2Vec2&, float fraction) override {
			Body* bd = reinterpret_cast<Body*>(f->GetUserData().pointer);
			if (!bd || f->IsSensor() || !(*accept)(bd)) return -1;   // ignore this fixture
			found = true;
			point = p;
			body = bd;
			return fraction;   // clip to the closest so far
		}
	} cb;
	cb.accept = &accept;
	if ((b - a).length() < 1e-3f) return false;
	world_->RayCast(&cb, toM(a), toM(b));
	if (cb.found) {
		if (hit) *hit = toPx(cb.point);
		if (hitBody) *hitBody = cb.body;
	}
	return cb.found;
}

std::vector<Body*> PhysicsWorld::queryRegion(const Bounds& bounds) const {
	std::vector<Body*> out;
	for (Body* body : bodies_)
		if (body->bounds().overlaps(bounds)) out.push_back(body);
	return out;
}

std::vector<QueryHit> PhysicsWorld::collideCircle(Vec2 centre, float radius) const {
	std::vector<QueryHit> out;

	b2CircleShape circle;
	circle.m_radius = radius / PTM;
	b2Transform xfC(toM(centre), b2Rot(0));

	for (Body* body : bodies_) {
		b2Manifold m;
		const b2Shape* shape = body->fixture->GetShape();
		const b2Transform& xf = body->b2->GetTransform();
		float radiusA;

		if (shape->GetType() == b2Shape::e_polygon) {
			b2CollidePolygonAndCircle(&m, static_cast<const b2PolygonShape*>(shape), xf, &circle, xfC);
			radiusA = static_cast<const b2PolygonShape*>(shape)->m_radius;
		} else {
			b2CollideCircles(&m, static_cast<const b2CircleShape*>(shape), xf, &circle, xfC);
			radiusA = shape->m_radius;
		}

		if (m.pointCount > 0) {
			b2WorldManifold wm;
			wm.Initialize(&m, xf, radiusA, xfC, circle.m_radius);
			out.push_back({body, m.pointCount, toPx(wm.points[0])});
		}
	}
	return out;
}

std::vector<QueryHit> PhysicsWorld::collideBox(Vec2 centre, float width, float height) const {
	std::vector<QueryHit> out;

	b2PolygonShape box;
	box.SetAsBox(width * 0.5f / PTM, height * 0.5f / PTM);
	b2Transform xfB(toM(centre), b2Rot(0));

	for (Body* body : bodies_) {
		b2Manifold m;
		const b2Shape* shape = body->fixture->GetShape();
		const b2Transform& xf = body->b2->GetTransform();
		float radiusA;

		if (shape->GetType() == b2Shape::e_polygon) {
			b2CollidePolygons(&m, static_cast<const b2PolygonShape*>(shape), xf, &box, xfB);
			radiusA = static_cast<const b2PolygonShape*>(shape)->m_radius;
		} else {
			// circle (A) vs polygon (B): collide polygon-vs-circle with swapped roles
			b2CollidePolygonAndCircle(&m, &box, xfB, static_cast<const b2CircleShape*>(shape), xf);
			if (m.pointCount > 0) {
				b2WorldManifold wm;
				wm.Initialize(&m, xfB, box.m_radius, xf, shape->m_radius);
				out.push_back({body, m.pointCount, toPx(wm.points[0])});
			}
			continue;
		}

		if (m.pointCount > 0) {
			b2WorldManifold wm;
			wm.Initialize(&m, xf, radiusA, xfB, box.m_radius);
			out.push_back({body, m.pointCount, toPx(wm.points[0])});
		}
	}
	return out;
}

std::vector<PhysicsWorld::Contact> PhysicsWorld::touchingContacts() const {
	std::vector<Contact> out;

	for (b2Contact* c = world_->GetContactList(); c; c = c->GetNext()) {
		if (!c->IsTouching() || !c->IsEnabled()) continue;

		Body* a = bodyOf(c->GetFixtureA());
		Body* b = bodyOf(c->GetFixtureB());

		Vec2 normal;
		if (c->GetFixtureA()->IsSensor() || c->GetFixtureB()->IsSensor()) {
			normal = (b->position() - a->position()).normalised();
		} else {
			b2WorldManifold wm;
			c->GetWorldManifold(&wm);
			normal = {wm.normal.x, wm.normal.y};
		}
		out.push_back({a, b, normal});
	}
	return out;
}

b2Joint* PhysicsWorld::createDistanceJoint(Body* a, Body* b) {
	b2DistanceJointDef jd;
	jd.Initialize(a->b2, b->b2, a->b2->GetPosition(), b->b2->GetPosition());
	jd.collideConnected = true;
	b2LinearStiffness(jd.stiffness, jd.damping, 10.0f, 0.7f, a->b2, b->b2);
	return world_->CreateJoint(&jd);
}

void PhysicsWorld::destroyJoint(b2Joint* j) {
	if (j) world_->DestroyJoint(j);
}
