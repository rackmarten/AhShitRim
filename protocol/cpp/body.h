// CJ's skeleton between the games (BodyState, protocol v5): San Andreas's skinning matrices to
// BodyBones, skinning a vertex with them in Skyrim, and Skyrim's interpolation between San Andreas's
// 30 frames a second. Header-only and free of game types, so the protocol tests cover it.
//
// RenderWare matrices are rows (right, up, at, pos) and map a point p to
// p.x * right + p.y * up + p.z * at + pos; RwMatrixMultiply(out, a, b) applies a first, then b.
// Both games put X east, Y north and Z up, so only the scale changes (pose.h).

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "../ahshitrim_protocol.h"
#include "pose.h"

namespace AhShitRim::Body
{
    using Pose::Vec3;

    inline Vec3  Add(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Vec3  Sub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Vec3  Scale(const Vec3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
    inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3  Cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

    inline Vec3 Normalize(const Vec3& a, const Vec3& a_fallback = { 0.0f, 0.0f, 1.0f })
    {
        const float length = std::sqrt(Dot(a, a));
        return length > 1e-12f ? Scale(a, 1.0f / length) : a_fallback;
    }

    // ---- San Andreas side ----

    struct RwRows
    {
        Vec3 right{ 1.0f, 0.0f, 0.0f };
        Vec3 up{ 0.0f, 1.0f, 0.0f };
        Vec3 at{ 0.0f, 0.0f, 1.0f };
        Vec3 pos;
    };

    inline Vec3 RotateBy(const RwRows& m, const Vec3& v) { return Add(Add(Scale(m.right, v.x), Scale(m.up, v.y)), Scale(m.at, v.z)); }
    inline Vec3 Apply(const RwRows& m, const Vec3& v) { return Add(RotateBy(m, v), m.pos); }

    // a, then b (RwMatrixMultiply(out, a, b)).
    inline RwRows Then(const RwRows& a, const RwRows& b)
    {
        return { RotateBy(b, a.right), RotateBy(b, a.up), RotateBy(b, a.at), Apply(b, a.pos) };
    }

    // Where RenderWare's skin pipeline takes a vertex of the skinned geometry for one bone, in world
    // space: the skin's skin-to-bone matrix, then the hierarchy's matrix for the bone, then the
    // atomic's frame when the hierarchy works in local space (rpHANIMHIERARCHYLOCALSPACEMATRICES).
    inline RwRows SkinToWorld(const RwRows& a_skinToBone, const RwRows& a_bone, const RwRows& a_atomic, bool a_localSpace)
    {
        const RwRows boned = Then(a_skinToBone, a_bone);
        return a_localSpace ? Then(boned, a_atomic) : boned;
    }

    struct Quat
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    // The rotation taking X, Y, Z to a_right, a_up, a_at, made orthonormal first (an animation
    // matrix carries a little drift).
    inline Quat FromAxes(const Vec3& a_right, const Vec3& a_up)
    {
        const Vec3 r = Normalize(a_right, { 1.0f, 0.0f, 0.0f });
        const Vec3 u = Normalize(Sub(a_up, Scale(r, Dot(a_up, r))), std::abs(r.z) < 0.9f ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ 1.0f, 0.0f, 0.0f });
        const Vec3 a = Cross(r, u);
        // Columns r, u, a: m[row][col].
        const float m00 = r.x, m01 = u.x, m02 = a.x;
        const float m10 = r.y, m11 = u.y, m12 = a.y;
        const float m20 = r.z, m21 = u.z, m22 = a.z;
        Quat        q;
        const float trace = m00 + m11 + m22;
        if (trace > 0.0f) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            q = { (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s };
        } else if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            q = { 0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s };
        } else if (m11 > m22) {
            const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            q = { (m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s };
        } else {
            const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            q = { (m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s };
        }
        return q;
    }

    // One bone for BodyState: the world skin matrix (metres) relative to CJ's feet (metres), in
    // Skyrim units. Model vertices are in Skyrim units too, so only the translation scales.
    inline Protocol::BodyBone ToBone(const RwRows& a_world, const Vec3& a_feet)
    {
        const Quat         q = FromAxes(a_world.right, a_world.up);
        const Vec3         t = Scale(Sub(a_world.pos, a_feet), Pose::kUnitsPerMeter);
        Protocol::BodyBone bone{};
        bone.rotation[0] = q.x;
        bone.rotation[1] = q.y;
        bone.rotation[2] = q.z;
        bone.rotation[3] = q.w;
        bone.translation[0] = t.x;
        bone.translation[1] = t.y;
        bone.translation[2] = t.z;
        return bone;
    }

    // ---- Skyrim side: skinning ----

    // A bone as a 3x4 matrix, m[row][col], world = m * (x, y, z, 1).
    struct Matrix34
    {
        float m[3][4]{};
    };

    inline Matrix34 ToMatrix(const Protocol::BodyBone& a_bone)
    {
        const float x = a_bone.rotation[0], y = a_bone.rotation[1], z = a_bone.rotation[2], w = a_bone.rotation[3];
        Matrix34    out;
        out.m[0][0] = 1.0f - 2.0f * (y * y + z * z);
        out.m[0][1] = 2.0f * (x * y - z * w);
        out.m[0][2] = 2.0f * (x * z + y * w);
        out.m[1][0] = 2.0f * (x * y + z * w);
        out.m[1][1] = 1.0f - 2.0f * (x * x + z * z);
        out.m[1][2] = 2.0f * (y * z - x * w);
        out.m[2][0] = 2.0f * (x * z - y * w);
        out.m[2][1] = 2.0f * (y * z + x * w);
        out.m[2][2] = 1.0f - 2.0f * (x * x + y * y);
        out.m[0][3] = a_bone.translation[0];
        out.m[1][3] = a_bone.translation[1];
        out.m[2][3] = a_bone.translation[2];
        return out;
    }

    // Whether bone a_bone is hidden by a_mask (BodyState.hiddenParts, v19). Bones past 63 always show.
    constexpr bool Hidden(std::uint64_t a_mask, std::uint32_t a_bone) { return a_bone < 64 && ((a_mask >> a_bone) & 1) != 0; }

    // A hidden bone's matrix: every vertex on it goes to its joint, so its triangles have no area and nothing
    // of it is drawn (a vehicle's parts each hang on one bone alone).
    inline Matrix34 Collapsed(const Protocol::BodyBone& a_bone)
    {
        Matrix34 out;
        out.m[0][3] = a_bone.translation[0];
        out.m[1][3] = a_bone.translation[1];
        out.m[2][3] = a_bone.translation[2];
        return out;
    }

    // A model vertex posed by its bones (a_bones indexed by SkinVertex.bones): the weighted sum of
    // where each bone takes it, normal turned the same way. A vertex without weight stays put.
    inline void Skin(const Matrix34* a_bones, std::uint32_t a_boneCount, const Protocol::SkinVertex& a_vertex, Vec3& a_position, Vec3& a_normal)
    {
        float m[3][4]{};
        float total = 0.0f;
        for (int k = 0; k < 4; k++) {
            const float weight = a_vertex.weights[k];
            if (!(weight > 0.0f) || a_vertex.bones[k] >= a_boneCount) {  // also skips a NaN
                continue;
            }
            const auto& b = a_bones[a_vertex.bones[k]].m;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 4; c++) {
                    m[r][c] += b[r][c] * weight;
                }
            }
            total += weight;
        }
        const Vec3 p{ a_vertex.position[0], a_vertex.position[1], a_vertex.position[2] };
        const Vec3 n{ a_vertex.normal[0], a_vertex.normal[1], a_vertex.normal[2] };
        if (total <= 0.0f) {
            a_position = p;
            a_normal = n;
            return;
        }
        const float inv = 1.0f / total;
        a_position = { (m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3]) * inv, (m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3]) * inv,
            (m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]) * inv };
        a_normal = Normalize({ m[0][0] * n.x + m[0][1] * n.y + m[0][2] * n.z, m[1][0] * n.x + m[1][1] * n.y + m[1][2] * n.z,
            m[2][0] * n.x + m[2][1] * n.y + m[2][2] * n.z }, n);
    }

    // ---- Skyrim side: interpolation ----

    inline Quat Nlerp(const Quat& a, Quat b, float t)
    {
        if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) {
            b = { -b.x, -b.y, -b.z, -b.w };  // the short way round
        }
        Quat        q{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
        const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length > 1e-12f) {
            q = { q.x / length, q.y / length, q.z / length, q.w / length };
        }
        return q;
    }

    inline void Lerp3(const float (&a)[3], const float (&b)[3], float t, float (&out)[3])
    {
        for (int i = 0; i < 3; i++) {
            out[i] = a[i] + (b[i] - a[i]) * t;
        }
    }

    inline Protocol::BodyBone BlendBone(const Protocol::BodyBone& a, const Protocol::BodyBone& b, float t)
    {
        const Quat q = Nlerp({ a.rotation[0], a.rotation[1], a.rotation[2], a.rotation[3] }, { b.rotation[0], b.rotation[1], b.rotation[2], b.rotation[3] }, t);
        Protocol::BodyBone out{};
        out.rotation[0] = q.x;
        out.rotation[1] = q.y;
        out.rotation[2] = q.z;
        out.rotation[3] = q.w;
        Lerp3(a.translation, b.translation, t, out.translation);
        return out;
    }

    // a at t = 0, b at t = 1; everything else (frame, handover, model) is b's.
    inline Protocol::BodyState Blend(const Protocol::BodyState& a, const Protocol::BodyState& b, float t)
    {
        Protocol::BodyState out = b;
        Lerp3(a.feet, b.feet, t, out.feet);
        Lerp3(a.cameraPosition, b.cameraPosition, t, out.cameraPosition);
        float forward[3];
        Lerp3(a.cameraForward, b.cameraForward, t, forward);
        const Vec3 f = Normalize({ forward[0], forward[1], forward[2] }, { b.cameraForward[0], b.cameraForward[1], b.cameraForward[2] });
        out.cameraForward[0] = f.x;
        out.cameraForward[1] = f.y;
        out.cameraForward[2] = f.z;
        float up[3];
        Lerp3(a.cameraUp, b.cameraUp, t, up);
        const Vec3 u = Normalize({ up[0], up[1], up[2] }, { b.cameraUp[0], b.cameraUp[1], b.cameraUp[2] });
        out.cameraUp[0] = u.x;
        out.cameraUp[1] = u.y;
        out.cameraUp[2] = u.z;
        out.cameraFov = a.cameraFov + (b.cameraFov - a.cameraFov) * t;
        const std::uint32_t count = (std::min)(b.boneCount, Protocol::kBodyBoneMax);
        for (std::uint32_t i = 0; i < count; i++) {
            out.bones[i] = BlendBone(a.bones[i], b.bones[i], t);
        }
        // A vehicle's passenger seats (v23) go with it.
        const std::uint32_t seats = (std::min)((std::min)(a.seatCount, b.seatCount), Protocol::kSeatMax);
        for (std::uint32_t i = 0; i < seats; i++) {
            out.seats[i] = BlendBone(a.seats[i], b.seats[i], t);
        }
        out.vehicleFrame = BlendBone(a.vehicleFrame, b.vehicleFrame, t);  // v24: how a vehicle is turned, for its box
        return out;
    }

    inline bool Finite(const Protocol::BodyState& a_body)
    {
        auto finite = [](const float* a_values, int a_count) {
            for (int i = 0; i < a_count; i++) {
                if (!std::isfinite(a_values[i])) {
                    return false;
                }
            }
            return true;
        };
        if (!finite(a_body.feet, 3) || !finite(a_body.cameraPosition, 3) || !finite(a_body.cameraForward, 3) || !finite(a_body.cameraUp, 3) || !std::isfinite(a_body.cameraFov)) {
            return false;
        }
        const std::uint32_t count = (std::min)(a_body.boneCount, Protocol::kBodyBoneMax);
        for (std::uint32_t i = 0; i < count; i++) {
            if (!finite(a_body.bones[i].rotation, 4) || !finite(a_body.bones[i].translation, 3)) {
                return false;
            }
        }
        const std::uint32_t seats = (std::min)(a_body.seatCount, Protocol::kSeatMax);
        for (std::uint32_t i = 0; i < seats; i++) {
            if (!finite(a_body.seats[i].rotation, 4) || !finite(a_body.seats[i].translation, 3)) {
                return false;
            }
        }
        return finite(a_body.vehicleFrame.rotation, 4) && finite(a_body.vehicleFrame.translation, 3) && finite(a_body.boxMin, 3) && finite(a_body.boxMax, 3);
    }

    // Plays San Andreas's 30 frames a second back smoothly at Skyrim's rate, kDelayMs behind.
    // San Andreas stamps each frame with its own clock (BodyState.timeMs); the gap to Skyrim's clock
    // is the smallest seen (a frame read the moment it was written), let drift up slowly so a hitch
    // can't hold it low forever. A new handover or model starts over.
    class Interpolator
    {
    public:
        static constexpr double kDelayMs = 50.0;        // one San Andreas frame plus a Skyrim frame of slack
        static constexpr double kGapDriftPerMs = 0.02;  // how fast the gap estimate may grow

        // A frame read from the slot at a_nowMs (Skyrim's clock). The same frame again is ignored.
        void Push(const Protocol::BodyState& a_body, double a_nowMs)
        {
            if (_count > 0) {
                const auto& newest = _frames[(_next + kCapacity - 1) % kCapacity];
                if (newest.handover != a_body.handover || newest.model != a_body.model || newest.boneCount != a_body.boneCount) {
                    Reset();
                } else if (newest.frame == a_body.frame) {
                    return;
                }
            }
            const double gap = a_nowMs - static_cast<double>(a_body.timeMs);
            _gap = _haveGap ? (std::min)(gap, _gap + (a_nowMs - _lastPushMs) * kGapDriftPerMs) : gap;
            _haveGap = true;
            _lastPushMs = a_nowMs;
            _frames[_next] = a_body;
            _next = (_next + 1) % kCapacity;
            _count = (std::min)(_count + 1, kCapacity);
        }

        // The pose to draw at a_nowMs; false before any frame arrived.
        bool Sample(double a_nowMs, Protocol::BodyState& a_out) const
        {
            if (_count == 0) {
                return false;
            }
            const double t = a_nowMs - _gap - kDelayMs;  // on San Andreas's clock
            const auto&  oldest = At(0);
            const auto&  newest = At(_count - 1);
            if (_count == 1 || t >= newest.timeMs) {
                a_out = newest;
                return true;
            }
            if (t <= oldest.timeMs) {
                a_out = oldest;
                return true;
            }
            for (int i = 1; i < _count; i++) {
                const auto& b = At(i);
                if (t <= b.timeMs) {
                    const auto&  a = At(i - 1);
                    const double span = static_cast<double>(b.timeMs) - a.timeMs;
                    a_out = span > 0.0 ? Blend(a, b, static_cast<float>((t - a.timeMs) / span)) : b;
                    return true;
                }
            }
            a_out = newest;
            return true;
        }

        void Reset()
        {
            _count = 0;
            _next = 0;
            _haveGap = false;
        }

    private:
        static constexpr int kCapacity = 4;

        const Protocol::BodyState& At(int a_age) const { return _frames[(_next + kCapacity - _count + a_age) % kCapacity]; }  // 0 = oldest

        std::array<Protocol::BodyState, kCapacity> _frames{};
        int                                        _next = 0;
        int                                        _count = 0;
        double                                     _gap = 0.0;
        bool                                       _haveGap = false;
        double                                     _lastPushMs = 0.0;
    };
}
