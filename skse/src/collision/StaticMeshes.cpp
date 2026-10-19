#include "collision/StaticMeshes.h"

#include "collision.h"

namespace AhShitRim::StaticMeshes
{
    namespace
    {
        using Layer = RE::COL_LAYER;

        constexpr int   kMaxDepth = 8;        // nested shape containers
        constexpr float kMaxEdge = 20000.0f;  // a triangle edge longer than this (Skyrim units) means we misread it
        constexpr std::size_t kMaxHullPoints = 64;  // a hull rebuilt from its vertices (ConvexVertices) tries every triple
        constexpr int   kCapsuleSides = 8;

        // hkpTriangleShape isn't in CommonLib: an hkpConvexShape (0x28), welding info, then its
        // three vertices from 0x30, 16 bytes apart. Checked in game: every triangle read this way
        // lies on Skyrim's own collision.
        constexpr std::size_t kTriangleVertices = 0x30;

        // Collision we keep. The landscape comes from the heightfield, but refs on the terrain or
        // ground layer (cliffs, mountain pieces) are here: they overhang paths. Transparent is what blocks the
        // player but not arrows (railings, plank fences, grates, invisible walls); clutter only
        // where it doesn't move (doors without a load door, chests, tables). Loose clutter, actors,
        // debris, water and triggers stay out.
        bool Wanted(const RE::hkpRigidBody& a_body)
        {
            switch (a_body.collidable.GetCollisionLayer()) {
            case Layer::kStatic:
            case Layer::kAnimStatic:
            case Layer::kTerrain:
            case Layer::kGround:
            case Layer::kTrees:
            case Layer::kStairHelper:
            case Layer::kTransparent:
                return true;
            case Layer::kClutter:
                return a_body.motion.type.any(RE::hkpMotion::MotionType::kFixed, RE::hkpMotion::MotionType::kKeyframed);
            default:
                return false;
            }
        }

        // A body a script or an animation moves: gates, trapdoors, drawbridges, doors.
        bool Animated(const RE::hkpRigidBody& a_body)
        {
            return a_body.collidable.GetCollisionLayer() == Layer::kAnimStatic || a_body.motion.type == RE::hkpMotion::MotionType::kKeyframed;
        }

        // Base objects whose refs have static collision once their 3D is in.
        bool HasStaticCollision(const RE::TESBoundObject* a_base)
        {
            switch (a_base->GetFormType()) {
            case RE::FormType::Static:
            case RE::FormType::MovableStatic:
            case RE::FormType::Tree:
            case RE::FormType::Door:
            case RE::FormType::Furniture:
            case RE::FormType::Activator:
            case RE::FormType::Container:
                return true;
            default:
                return false;
            }
        }

        RE::NiPoint3 Point(const RE::hkVector4& a_v)
        {
            float v[4];
            _mm_storeu_ps(v, a_v.quad);
            return { v[0], v[1], v[2] };
        }

        // A rotation (columns) and translation, in Havok units.
        struct Frame
        {
            RE::NiPoint3 c0{ 1.0f, 0.0f, 0.0f }, c1{ 0.0f, 1.0f, 0.0f }, c2{ 0.0f, 0.0f, 1.0f }, t;

            static Frame Of(const RE::hkTransform& a_xf)
            {
                Frame f;
                f.c0 = Point(a_xf.rotation.col0);
                f.c1 = Point(a_xf.rotation.col1);
                f.c2 = Point(a_xf.rotation.col2);
                f.t = Point(a_xf.translation);
                return f;
            }

            RE::NiPoint3 Apply(const RE::NiPoint3& a_p) const { return c0 * a_p.x + c1 * a_p.y + c2 * a_p.z + t; }

            // This frame, then a_inner inside it.
            Frame Then(const Frame& a_inner) const
            {
                Frame f;
                f.c0 = c0 * a_inner.c0.x + c1 * a_inner.c0.y + c2 * a_inner.c0.z;
                f.c1 = c0 * a_inner.c1.x + c1 * a_inner.c1.y + c2 * a_inner.c1.z;
                f.c2 = c0 * a_inner.c2.x + c1 * a_inner.c2.y + c2 * a_inner.c2.z;
                f.t = Apply(a_inner.t);
                return f;
            }

            // A rotation with a translation inside the Havok world, so a misread layout shows.
            bool Plausible() const
            {
                for (const auto* c : { &c0, &c1, &c2 }) {
                    const float length = c->Length();
                    if (!std::isfinite(length) || std::abs(length - 1.0f) > 0.01f) {
                        return false;
                    }
                }
                return std::isfinite(t.Length()) && t.Length() < 10000.0f && std::abs(c0.Dot(c1)) < 0.01f && std::abs(c1.Dot(c2)) < 0.01f;
            }
        };

        struct Walk
        {
            Frame           xf;  // shape space to Havok world
            float           toSkyrim = 1.0f;
            float           x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;  // the chunk
            Chunk*          out = nullptr;

            // Havok shape space to Skyrim world units.
            RE::NiPoint3 ToSkyrim(const RE::NiPoint3& a_p) const
            {
                return xf.Apply(a_p) * toSkyrim;
            }

            void Emit(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b, const RE::NiPoint3& a_c)
            {
                for (const auto& [p, q] : { std::pair{ a_a, a_b }, std::pair{ a_b, a_c }, std::pair{ a_c, a_a } }) {
                    const float edge = p.GetDistance(q);
                    if (!std::isfinite(edge) || edge > kMaxEdge) {
                        return;
                    }
                }
                const RE::NiPoint3 mid = (a_a + a_b + a_c) / 3.0f;
                if (mid.x < x0 || mid.x >= x1 || mid.y < y0 || mid.y >= y1) {
                    return;  // another chunk's
                }
                for (const auto* p : { &a_a, &a_b, &a_c }) {
                    out->triangles.insert(out->triangles.end(), { p->x, p->y, p->z });
                }
            }

            // A convex face, its corners in order around it (either way) in shape space, as a fan
            // wound counterclockwise seen from outside, which is away from the shape's center.
            void Polygon(std::vector<RE::NiPoint3> a_corners, const RE::NiPoint3& a_center)
            {
                if (a_corners.size() < 3) {
                    return;
                }
                RE::NiPoint3 normal, mid;
                for (std::size_t i = 0; i < a_corners.size(); i++) {
                    normal += a_corners[i].Cross(a_corners[(i + 1) % a_corners.size()]);
                    mid += a_corners[i];
                }
                mid /= static_cast<float>(a_corners.size());
                if (normal.Dot(mid - a_center) < 0.0f) {
                    std::ranges::reverse(a_corners);
                }
                for (auto& p : a_corners) {
                    p = ToSkyrim(p);
                }
                for (std::size_t i = 1; i + 1 < a_corners.size(); i++) {
                    Emit(a_corners[0], a_corners[i], a_corners[i + 1]);
                }
            }

            void Box(const RE::NiPoint3& a_center, const RE::NiPoint3& a_half)
            {
                // Corner i has x from bit 0, y from bit 1, z from bit 2; each face goes around once.
                static constexpr int faces[6][4] = { { 0, 2, 6, 4 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 5, 7, 6 } };
                RE::NiPoint3 corner[8];
                for (int i = 0; i < 8; i++) {
                    corner[i] = a_center + RE::NiPoint3((i & 1) ? a_half.x : -a_half.x, (i & 2) ? a_half.y : -a_half.y, (i & 4) ? a_half.z : -a_half.z);
                }
                for (const auto& face : faces) {
                    Polygon({ corner[face[0]], corner[face[1]], corner[face[2]], corner[face[3]] }, a_center);
                }
            }

            // A prism around the capsule's segment, its ends pushed out by the radius.
            void Capsule(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b, float a_radius)
            {
                RE::NiPoint3 axis = a_b - a_a;
                const float  length = axis.Length();
                axis = length > 1e-6f ? axis / length : RE::NiPoint3(0.0f, 0.0f, 1.0f);
                RE::NiPoint3 side = std::abs(axis.z) < 0.9f ? axis.Cross({ 0.0f, 0.0f, 1.0f }) : axis.Cross({ 1.0f, 0.0f, 0.0f });
                side /= side.Length();
                const RE::NiPoint3 other = axis.Cross(side);
                const RE::NiPoint3 start = a_a - axis * a_radius, end = a_b + axis * a_radius, center = (a_a + a_b) / 2.0f;

                std::vector<RE::NiPoint3> ringA, ringB;
                for (int i = 0; i < kCapsuleSides; i++) {
                    const float        angle = 2.0f * std::numbers::pi_v<float> * i / kCapsuleSides;
                    const RE::NiPoint3 offset = (side * std::cos(angle) + other * std::sin(angle)) * a_radius;
                    ringA.push_back(start + offset);
                    ringB.push_back(end + offset);
                }
                for (int i = 0; i < kCapsuleSides; i++) {
                    const int next = (i + 1) % kCapsuleSides;
                    Polygon({ ringA[i], ringA[next], ringB[next], ringB[i] }, center);
                }
                Polygon(ringA, center);
                Polygon(ringB, center);
            }

            // Each plane's face is the hull's vertices lying on it, sorted around their middle.
            void ConvexVertices(const RE::hkpConvexVerticesShape* a_shape)
            {
                std::vector<RE::NiPoint3> points;
                for (std::int32_t i = 0; i < a_shape->numVertices; i++) {
                    const auto& four = a_shape->rotatedVertices[i / 4];
                    float       x[4], y[4], z[4];
                    _mm_storeu_ps(x, four.x.quad);
                    _mm_storeu_ps(y, four.y.quad);
                    _mm_storeu_ps(z, four.z.quad);
                    points.emplace_back(x[i % 4], y[i % 4], z[i % 4]);
                }
                const RE::NiPoint3 center = Point(a_shape->aabbCenter), half = Point(a_shape->aabbHalfExtents);
                if (a_shape->planeEquations.empty() || points.size() < 4) {
                    Box(center, half);
                    return;
                }
                // Havok shrinks a hull's vertices by its convex radius while its planes stay where the
                // full hull is, so a face's vertices lie up to the radius inside its plane: matched
                // exactly, a portcullis, a floor grate trapdoor and many rubble stones gave no faces.
                const float epsilon = 1e-3f * (std::max)({ half.x, half.y, half.z, 1e-3f });
                const float inside = epsilon + (std::max)(a_shape->radius, 0.0f);
                int         faces = 0;
                const auto  before = out->triangles.size();
                for (const auto& plane : a_shape->planeEquations) {
                    float p[4];
                    _mm_storeu_ps(p, plane.quad);
                    const RE::NiPoint3        normal(p[0], p[1], p[2]);
                    std::vector<RE::NiPoint3> face;
                    for (const auto& v : points) {
                        const float d = normal.Dot(v) + p[3];
                        if (d <= epsilon && d >= -inside) {
                            face.push_back(v);
                        }
                    }
                    if (face.size() < 3) {
                        continue;
                    }
                    RE::NiPoint3 mid;
                    for (const auto& v : face) {
                        mid += v;
                    }
                    mid /= static_cast<float>(face.size());
                    RE::NiPoint3 u = face[0] - mid;
                    u /= (std::max)(u.Length(), 1e-6f);
                    const RE::NiPoint3 w = normal.Cross(u);
                    std::ranges::sort(face, {}, [&](const RE::NiPoint3& a_v) { return std::atan2((a_v - mid).Dot(w), (a_v - mid).Dot(u)); });
                    Polygon(std::move(face), center);
                    faces++;
                }
                if (faces >= 4) {
                    return;
                }
                // Still not closed: the hull again from its vertices alone, every triangle of them with
                // all the others on one side (a face of the hull; coplanar ones overlap, harmless).
                out->triangles.resize(before);
                if (points.size() > kMaxHullPoints) {
                    out->skipped++;
                    return;
                }
                RE::NiPoint3 mid;
                for (const auto& v : points) {
                    mid += v;
                }
                mid /= static_cast<float>(points.size());
                for (std::size_t i = 0; i < points.size(); i++) {
                    for (std::size_t j = i + 1; j < points.size(); j++) {
                        for (std::size_t k = j + 1; k < points.size(); k++) {
                            RE::NiPoint3 n = (points[j] - points[i]).Cross(points[k] - points[i]);
                            const float  length = n.Length();
                            if (length < 1e-9f) {
                                continue;
                            }
                            n /= length;
                            bool above = false, below = false;
                            for (const auto& v : points) {
                                const float d = n.Dot(v - points[i]);
                                above |= d > epsilon;
                                below |= d < -epsilon;
                            }
                            if (!(above && below)) {
                                Polygon({ points[i], points[j], points[k] }, mid);
                            }
                        }
                    }
                }
                out->rebuilt++;
            }

            // A child placed with a transform of its own: planks, railings, drawbridges, doors are
            // often boxes in these. Not in CommonLib; Havok 2010's layout: an hkpConvexShape (0x28)
            // or hkpShape (0x20), the child in an hkpSingleShapeContainer and its size, then the
            // translation (convex translate, 0x40), transform (convex transform, 0x40) or rotation
            // quaternion and transform (transform, 0x40 and 0x50). A transform that isn't a
            // rotation means the layout is wrong here: skipped and counted.
            void Transformed(const RE::hkpShape* a_shape, int a_depth)
            {
                const auto* raw = reinterpret_cast<const std::uint8_t*>(a_shape);
                const auto* container = a_shape->GetContainer();
                RE::hkpShapeBuffer buffer;
                const auto* child = container ? container->GetChildShape(container->GetFirstKey(), buffer) : nullptr;
                Frame inner;
                switch (a_shape->type) {
                case RE::hkpShapeType::kConvexTranslate:
                    inner.t = Point(*reinterpret_cast<const RE::hkVector4*>(raw + 0x40));
                    break;
                case RE::hkpShapeType::kConvexTransform:
                    inner = Frame::Of(*reinterpret_cast<const RE::hkTransform*>(raw + 0x40));
                    break;
                default:
                    inner = Frame::Of(*reinterpret_cast<const RE::hkTransform*>(raw + 0x50));
                    break;
                }
                if (!child || !inner.Plausible()) {
                    out->skipped++;
                    return;
                }
                const Frame outer = xf;
                xf = outer.Then(inner);
                Shape(child, a_depth + 1);
                xf = outer;
                out->transformed++;
            }

            void Shape(const RE::hkpShape* a_shape, int a_depth)
            {
                if (!a_shape || a_depth > kMaxDepth) {
                    return;
                }
                const auto* raw = reinterpret_cast<const std::uint8_t*>(a_shape);
                switch (a_shape->type) {
                case RE::hkpShapeType::kTriangle:
                    {
                        const auto* v = reinterpret_cast<const RE::hkVector4*>(raw + kTriangleVertices);
                        Emit(ToSkyrim(Point(v[0])), ToSkyrim(Point(v[1])), ToSkyrim(Point(v[2])));
                        return;
                    }
                case RE::hkpShapeType::kBox:
                    Box({}, Point(static_cast<const RE::hkpBoxShape*>(a_shape)->halfExtents));
                    return;
                case RE::hkpShapeType::kCapsule:
                    {
                        const auto* capsule = static_cast<const RE::hkpCapsuleShape*>(a_shape);
                        Capsule(Point(capsule->vertexA), Point(capsule->vertexB), capsule->radius);
                        return;
                    }
                case RE::hkpShapeType::kConvexVertices:
                    ConvexVertices(static_cast<const RE::hkpConvexVerticesShape*>(a_shape));
                    return;
                case RE::hkpShapeType::kConvexTransform:
                case RE::hkpShapeType::kConvexTranslate:
                case RE::hkpShapeType::kTransform:
                    Transformed(a_shape, a_depth);
                    return;
                default:
                    break;
                }
                if (const auto* container = a_shape->GetContainer()) {
                    RE::hkpShapeBuffer buffer;
                    for (auto key = container->GetFirstKey(); key != RE::HK_INVALID_SHAPE_KEY; key = container->GetNextKey(key)) {
                        Shape(container->GetChildShape(key, buffer), a_depth + 1);
                    }
                    return;
                }
                out->skipped++;
            }
        };
    }

    Chunk Collect(std::int32_t a_chunkX, std::int32_t a_chunkY)
    {
        Chunk chunk;
        auto* tes = RE::TES::GetSingleton();
        if (!tes) {
            return chunk;
        }
        Walk walk;
        walk.out = &chunk;
        walk.toSkyrim = 1.0f / RE::bhkWorld::GetWorldScale();
        walk.x0 = static_cast<float>(a_chunkX) * Protocol::kCollisionChunkUnits;
        walk.y0 = static_cast<float>(a_chunkY) * Protocol::kCollisionChunkUnits;
        walk.x1 = walk.x0 + Protocol::kCollisionChunkUnits;
        walk.y1 = walk.y0 + Protocol::kCollisionChunkUnits;

        tes->ForEachReference([&](RE::TESObjectREFR* a_ref) {
            const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
            if (!base || !HasStaticCollision(base) || a_ref->IsDisabled() || a_ref->IsDeleted()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            auto* root = a_ref->Get3D();
            if (!root) {
                const auto pos = a_ref->GetPosition();
                chunk.pending += pos.x >= walk.x0 && pos.x < walk.x1 && pos.y >= walk.y0 && pos.y < walk.y1;
                return RE::BSContainer::ForEachResult::kContinue;
            }
            const auto& bound = root->worldBound;
            if (bound.center.x + bound.radius < walk.x0 || bound.center.x - bound.radius >= walk.x1 ||
                bound.center.y + bound.radius < walk.y0 || bound.center.y - bound.radius >= walk.y1) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            bool       moves = false;
            const auto before = chunk.triangles.size();
            RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) {
                auto* body = a_object ? skyrim_cast<RE::bhkRigidBody*>(a_object->body.get()) : nullptr;
                auto* hk = body ? static_cast<RE::hkpRigidBody*>(body->referencedObject.get()) : nullptr;
                if (hk && hk->world && !Wanted(*hk)) {
                    chunk.otherLayers[static_cast<int>(hk->collidable.GetCollisionLayer())]++;
                }
                if (!hk || !hk->world || !Wanted(*hk)) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }
                // Havok's own transform: bhkRigidBody::GetTransform leaves out a bhkRigidBodyT's offset.
                walk.xf = Frame::Of(hk->motion.motionState.transform);
                walk.Shape(hk->GetShape(), 0);
                moves |= Animated(*hk);
                return RE::BSVisit::BSVisitControl::kContinue;
            });
            chunk.refs += chunk.triangles.size() > before;
            if (moves) {
                chunk.movers.push_back(a_ref->GetFormID());
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return chunk;
    }

    bool Pose(RE::TESObjectREFR* a_ref, RE::NiPoint3& a_pose)
    {
        auto* root = a_ref ? a_ref->Get3D() : nullptr;
        if (!root) {
            return false;
        }
        a_pose = {};
        bool any = false;
        RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) {
            auto* body = a_object ? skyrim_cast<RE::bhkRigidBody*>(a_object->body.get()) : nullptr;
            auto* hk = body ? static_cast<RE::hkpRigidBody*>(body->referencedObject.get()) : nullptr;
            if (hk && hk->world && Wanted(*hk) && Animated(*hk)) {
                const auto f = Frame::Of(hk->motion.motionState.transform);
                a_pose += f.t + f.c0 + f.c1 * 2.0f + f.c2 * 3.0f;  // moves with any turn or shift
                any = true;
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        a_pose *= 1.0f / RE::bhkWorld::GetWorldScale();
        return any;
    }
}
