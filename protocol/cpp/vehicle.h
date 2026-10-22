// San Andreas's vehicles in Skyrim (protocol v6): where a spawned vehicle goes, and how its model is
// put together. Header-only and free of game types, so the protocol tests cover it; the ASI feeds
// it from San Andreas's vehicles.
//
// A vehicle goes to Skyrim as a SkinModel of kind kSkinModelVehicle: every drawn part (an atomic:
// chassis, wheels, doors, a bike's forks and handlebars) is a bone, and its vertices hang on it
// alone, in the part's own space. Each San Andreas frame the vehicle slot carries every part's
// transform (its frame's world matrix relative to the vehicle), so Skyrim poses it with the same
// skinning as CJ.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "body.h"
#include "skin_model.h"

namespace AhShitRim::Vehicle
{
    using Pose::Vec3;

    // ---- Where a spawned vehicle goes ----
    //
    // San Andreas's vehicle cheat parks a vehicle across CJ's path, its side to him, a few metres
    // ahead. AhShitRim does the same, but on Skyrim's ground, which is rarely flat: spots further
    // out and turned away from straight ahead are tried in turn, and the first one whose ground is
    // flat enough, near CJ's height and clear of anything in the way wins.

    inline constexpr float kNearGap = 2.0f;      // metres between CJ and the vehicle's near side
    inline constexpr float kFurtherStep = 2.0f;  // each ring of spots this much further out
    inline constexpr int   kRings = 3;
    inline constexpr float kTurns[] = { 0.0f, 0.5236f, -0.5236f, 1.0472f, -1.0472f };  // 0, +-30, +-60 degrees
    inline constexpr float kMaxUneven = 0.8f;    // highest minus lowest ground under the footprint, metres
    inline constexpr float kMaxStep = 1.5f;      // the ground's middle height at most this far from CJ's feet
    inline constexpr float kSampleInset = 0.9f;  // samples sit this far towards the middle of the footprint

    // Half the vehicle's size, metres, from its collision box: San Andreas models face +Y.
    struct Footprint
    {
        float halfWidth = 1.0f;   // along the model's X
        float halfLength = 2.5f;  // along its Y
    };

    struct Spot
    {
        Vec3  center;   // on the ground plane through CJ's feet (z is CJ's); the samples give the height
        Vec3  forward;  // the vehicle's facing, horizontal unit vector
        float heading;  // San Andreas heading of forward (radians, counter-clockwise, 0 = north)
    };

    // San Andreas heading of a horizontal facing: a heading h faces (-sin h, cos h).
    inline float HeadingOf(const Vec3& a_forward) { return std::atan2(-a_forward.x, a_forward.y); }

    // The spots to try, best first: rings further out, and in each straight ahead first, then
    // turned either way. a_cjForward is where CJ faces (only x and y count).
    inline std::vector<Spot> Candidates(const Vec3& a_feet, const Vec3& a_cjForward, const Footprint& a_size)
    {
        const Vec3 ahead = Body::Normalize({ a_cjForward.x, a_cjForward.y, 0.0f }, { 0.0f, 1.0f, 0.0f });
        std::vector<Spot> spots;
        for (int ring = 0; ring < kRings; ring++) {
            const float distance = a_size.halfWidth + kNearGap + ring * kFurtherStep;
            for (const float turn : kTurns) {
                const float c = std::cos(turn), s = std::sin(turn);
                const Vec3  dir{ ahead.x * c - ahead.y * s, ahead.x * s + ahead.y * c, 0.0f };
                Spot        spot;
                spot.center = { a_feet.x + dir.x * distance, a_feet.y + dir.y * distance, a_feet.z };
                spot.forward = { -dir.y, dir.x, 0.0f };  // across CJ's path: its side faces him
                spot.heading = HeadingOf(spot.forward);
                spots.push_back(spot);
            }
        }
        return spots;
    }

    inline constexpr int kSamples = 9;

    // Where a spot's ground is sampled (x, y; z is the spot's): a 3 x 3 grid over the footprint,
    // corners first, then the middles of the sides, then the middle.
    inline std::array<Vec3, kSamples> SamplePoints(const Spot& a_spot, const Footprint& a_size)
    {
        static constexpr float kAt[kSamples][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 }, { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 }, { 0, 0 } };
        const Vec3             right{ a_spot.forward.y, -a_spot.forward.x, 0.0f };
        std::array<Vec3, kSamples> out{};
        for (int i = 0; i < kSamples; i++) {
            const float x = kAt[i][0] * a_size.halfWidth * kSampleInset;
            const float y = kAt[i][1] * a_size.halfLength * kSampleInset;
            out[i] = { a_spot.center.x + right.x * x + a_spot.forward.x * y, a_spot.center.y + right.y * x + a_spot.forward.y * y, a_spot.center.z };
        }
        return out;
    }

    // What was found at a spot's sample points: the ground's height under each, if any.
    struct Ground
    {
        std::array<bool, kSamples>  hit{};
        std::array<float, kSamples> z{};
    };

    // The height to put the vehicle at when the spot's ground will do: every sample on ground, the
    // highest at most a_maxUneven (kMaxUneven, or a kind's own: VehicleKinds::Traits) over the lowest, and the
    // middle of them at most kMaxStep from CJ's feet. Whether anything stands in the way is checked after (it
    // needs that height).
    inline std::optional<float> GroundHeight(const Ground& a_ground, float a_feetZ, float a_maxUneven = kMaxUneven)
    {
        float low = 1e30f, high = -1e30f;
        for (int i = 0; i < kSamples; i++) {
            if (!a_ground.hit[i] || !std::isfinite(a_ground.z[i])) {
                return std::nullopt;
            }
            low = (std::min)(low, a_ground.z[i]);
            high = (std::max)(high, a_ground.z[i]);
        }
        std::array<float, kSamples> sorted = a_ground.z;
        std::nth_element(sorted.begin(), sorted.begin() + kSamples / 2, sorted.end());
        const float middle = sorted[kSamples / 2];
        if (high - low > a_maxUneven || std::abs(middle - a_feetZ) > kMaxStep) {
            return std::nullopt;
        }
        return middle;
    }

    // ---- A vehicle's model ----

    // Paint and lights: San Andreas draws a vehicle's materials in placeholder colours and swaps
    // them right before drawing it (CVehicleModelInfo::SetEditableMaterialsCB): four placeholders
    // take the vehicle's own colours from the colour table, and materials of the lights texture turn
    // white. a_colours are the vehicle's four colours (RGB) already looked up in the table.
    struct Rgb
    {
        std::uint8_t r = 255, g = 255, b = 255;
        bool         operator==(const Rgb&) const = default;
    };

    inline Rgb MaterialColour(const Rgb& a_material, bool a_lightsTexture, const std::array<Rgb, 4>& a_colours)
    {
        if (a_lightsTexture) {
            return {};
        }
        static constexpr Rgb kPlaceholders[4] = { { 60, 255, 0 }, { 255, 0, 175 }, { 0, 255, 255 }, { 255, 0, 255 } };
        for (int i = 0; i < 4; i++) {
            if (a_material == kPlaceholders[i]) {
                return a_colours[i];
            }
        }
        return a_material;
    }

    // A part's frame can carry more than a rotation (San Andreas scales wheels to the vehicle's wheel
    // size, and may mirror one side), but a bone is a rotation and a translation. What isn't
    // rotation is constant for a part, so it is baked into the part's vertices once:
    // frame = R * B, with R the rotation Body::ToBone takes from the frame every frame.
    struct Matrix33
    {
        float m[3][3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

        Vec3 operator*(const Vec3& v) const
        {
            return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
        }

        float Determinant() const
        {
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                   m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        }

        // Turns normals as this turns positions: the inverse transpose (up to scale, normalized after).
        Matrix33 NormalMatrix() const
        {
            Matrix33 c;
            c.m[0][0] = m[1][1] * m[2][2] - m[1][2] * m[2][1];
            c.m[0][1] = m[1][2] * m[2][0] - m[1][0] * m[2][2];
            c.m[0][2] = m[1][0] * m[2][1] - m[1][1] * m[2][0];
            c.m[1][0] = m[0][2] * m[2][1] - m[0][1] * m[2][2];
            c.m[1][1] = m[0][0] * m[2][2] - m[0][2] * m[2][0];
            c.m[1][2] = m[0][1] * m[2][0] - m[0][0] * m[2][1];
            c.m[2][0] = m[0][1] * m[1][2] - m[0][2] * m[1][1];
            c.m[2][1] = m[0][2] * m[1][0] - m[0][0] * m[1][2];
            c.m[2][2] = m[0][0] * m[1][1] - m[0][1] * m[1][0];
            return Determinant() < 0.0f ? c.Scaled(-1.0f) : c;  // cofactors are det * inverse transpose
        }

        Matrix33 Scaled(float s) const
        {
            Matrix33 out = *this;
            for (auto& row : out.m) {
                for (float& v : row) {
                    v *= s;
                }
            }
            return out;
        }
    };

    // B for a part whose frame is a_frame: R^T * L, L the frame's linear part (columns right, up,
    // at) and R the rotation Body::FromAxes reads from it.
    inline Matrix33 Bake(const Body::RwRows& a_frame)
    {
        const Body::Quat q = Body::FromAxes(a_frame.right, a_frame.up);
        Protocol::BodyBone bone{};
        bone.rotation[0] = q.x;
        bone.rotation[1] = q.y;
        bone.rotation[2] = q.z;
        bone.rotation[3] = q.w;
        const auto r = Body::ToMatrix(bone);
        const Vec3 columns[3] = { a_frame.right, a_frame.up, a_frame.at };
        Matrix33   b;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                const Vec3& l = columns[col];
                b.m[row][col] = r.m[0][row] * l.x + r.m[1][row] * l.y + r.m[2][row] * l.z;
            }
        }
        return b;
    }

    inline bool IsIdentity(const Matrix33& a_b, float a_epsilon = 1e-4f)
    {
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                if (std::abs(a_b.m[r][c] - (r == c ? 1.0f : 0.0f)) > a_epsilon) {
                    return false;
                }
            }
        }
        return true;
    }

    // Bakes B into a part's meshes (SkinBuild::Meshes of its geometry): positions and normals turned,
    // and triangles turned round when B mirrors.
    inline void BakeMeshes(std::vector<SkinModel::MeshIn>& a_meshes, const Matrix33& a_b)
    {
        if (IsIdentity(a_b)) {
            return;
        }
        const Matrix33 normals = a_b.NormalMatrix();
        const bool     mirrored = a_b.Determinant() < 0.0f;
        for (auto& mesh : a_meshes) {
            for (auto& v : mesh.vertices) {
                const Vec3 p = a_b * Vec3{ v.position[0], v.position[1], v.position[2] };
                const Vec3 n = Body::Normalize(normals * Vec3{ v.normal[0], v.normal[1], v.normal[2] }, { v.normal[0], v.normal[1], v.normal[2] });
                v.position[0] = p.x;
                v.position[1] = p.y;
                v.position[2] = p.z;
                v.normal[0] = n.x;
                v.normal[1] = n.y;
                v.normal[2] = n.z;
            }
            if (mirrored) {
                for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                    std::swap(mesh.indices[t + 1], mesh.indices[t + 2]);
                }
            }
        }
    }

    // Meshes that look the same (texture, flags, colour) put together, so a vehicle of a few dozen
    // parts is drawn as a handful of shapes. A mesh that would take one past kModelMeshVertexMax
    // vertices or triangles (Skyrim counts both in 16 bits) starts a new one.
    inline std::vector<SkinModel::MeshIn> Merge(std::vector<SkinModel::MeshIn> a_meshes)
    {
        std::vector<SkinModel::MeshIn> out;
        for (auto& mesh : a_meshes) {
            auto same = std::find_if(out.begin(), out.end(), [&](const SkinModel::MeshIn& a_other) {
                return a_other.texture == mesh.texture && a_other.flags == mesh.flags && std::memcmp(a_other.color, mesh.color, sizeof(mesh.color)) == 0 &&
                       a_other.vertices.size() + mesh.vertices.size() <= Protocol::kModelMeshVertexMax &&
                       (a_other.indices.size() + mesh.indices.size()) / 3 <= Protocol::kModelMeshVertexMax;
            });
            if (same == out.end()) {
                out.push_back(std::move(mesh));
                continue;
            }
            const auto base = static_cast<std::uint16_t>(same->vertices.size());
            same->vertices.insert(same->vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            for (const auto index : mesh.indices) {
                same->indices.push_back(static_cast<std::uint16_t>(base + index));
            }
        }
        return out;
    }

    // ---- Steering a car in the air ----
    //
    // San Andreas turns a car with no wheel on the ground (CAutomobile::ProcessAI, 0x6B4F69): the
    // handbrake and left/right yaw it, left/right alone roll it, up/down pitch it, roll and pitch
    // only off the gas. Each push goes on only while the car turns slower than kSaAirSpin that way,
    // so it barely moves a car that left a slope already turning. AhShitRim pushes the player's car
    // kAirTurnGain times as hard, up to kAirSpin, and rolls and pitches it on the gas too (people
    // keep W held over a jump): the ASI adds what San Andreas's own push didn't. Turn speeds are
    // San Andreas's, radians per 1/50 s.

    inline constexpr float kSaAirSpin = 0.02f;  // San Andreas's cap: 1 rad/s, 57 degrees a second
    inline constexpr float kAirTurnGain = 1.5f;
    inline constexpr float kAirSpin = 0.03f;  // 86 degrees a second

    // Whether a push of a_steer (negative or positive) goes on about an axis the car turns about at
    // a_spin: only while it turns slower than a_cap the way it pushes.
    constexpr bool AirPushes(float a_spin, float a_steer, float a_cap)
    {
        return (a_steer < 0.0f && a_spin < a_cap) || (a_steer > 0.0f && a_spin > -a_cap);
    }

    // The push AhShitRim adds about one axis, in San Andreas's own pushes: kAirTurnGain of them up to
    // kAirSpin, less the one San Andreas gave below kSaAirSpin (none when a_saPushes is false: the gas
    // stopped it).
    constexpr float ExtraAirPush(float a_spin, float a_steer, bool a_saPushes = true)
    {
        return (AirPushes(a_spin, a_steer, kAirSpin) ? kAirTurnGain : 0.0f) - (a_saPushes && AirPushes(a_spin, a_steer, kSaAirSpin) ? 1.0f : 0.0f);
    }

    // San Andreas's steering in the air (-1..1 each): the keys, or with the mouse steering in the
    // third person (m_bUseMouse3rdPerson) and both key axes idle, the mouse's movement / 50, -1.5..1.5.
    struct AirSteer
    {
        float leftRight = 0.0f;
        float upDown = 0.0f;
    };
    constexpr AirSteer AirSteering(short a_stickX, short a_stickY, bool a_mouseSteers, float a_mouseX, float a_mouseY)
    {
        AirSteer s{ a_stickX / 128.0f, a_stickY / 128.0f };
        const auto idle = [](float a_v) { return a_v > -0.05f && a_v < 0.05f; };
        if (a_mouseSteers && idle(s.leftRight) && idle(s.upDown)) {
            s.leftRight = std::clamp(a_mouseX / 50.0f, -1.5f, 1.5f);
            s.upDown = std::clamp(a_mouseY / 50.0f, -1.5f, 1.5f);
        }
        return s;
    }

    // The extra pushes about the car's up (yaw), forward (roll) and right (pitch) axes, given how
    // fast it turns about each. a_gas: San Andreas pushed no roll or pitch, so all of it is added.
    struct AirPush
    {
        float yaw = 0.0f;
        float roll = 0.0f;
        float pitch = 0.0f;
    };
    constexpr AirPush ExtraAirPushes(float a_spinUp, float a_spinForward, float a_spinRight, const AirSteer& a_steer, bool a_handbrake, bool a_gas)
    {
        AirPush p;
        if (a_handbrake) {
            p.yaw = ExtraAirPush(a_spinUp, a_steer.leftRight);
        } else {
            p.roll = ExtraAirPush(a_spinForward, a_steer.leftRight, !a_gas);
        }
        p.pitch = ExtraAirPush(a_spinRight, a_steer.upDown, !a_gas);
        return p;
    }

    // A bike in the air: San Andreas only leans it (left/right, and forward and back with up/down),
    // nothing turns it flat (a bicycle has its own yaw). AhShitRim yaws the player's motorbike with
    // left/right, handbrake or not, as hard as a car's yaw (kAirTurnGain pushes, up to kAirSpin);
    // a_steer from the keys alone.
    constexpr float BikeAirYaw(float a_spinUp, float a_steer)
    {
        return ExtraAirPush(a_spinUp, a_steer, false);
    }

    // One San Andreas push's force per 1/50 s: a_perMass (0.0007 times the driving skill's modifier,
    // a monster truck 0.0025) times the turn mass, counted up to 3000.
    constexpr float AirPushForce(float a_perMass, float a_turnMass)
    {
        return a_perMass * (std::min)(a_turnMass, 3000.0f);
    }
}
