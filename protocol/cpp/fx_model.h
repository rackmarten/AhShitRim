// San Andreas's own particles for the vehicles and the jetpack (protocol v26): the jetpack's flames, every
// vehicle's exhaust smoke and nitro flames, a helicopter's rotor dust, and the Rhino's and the Hunter's muzzle
// flashes, as the prims of their fx systems in models/effects.fxp let them out. Each prim's sprite comes from
// models/effectsPC.txd in the player's own install, and they are drawn like the damage smoke (smoke_model.h):
// camera-facing quads on bones of their own, through ModelView. Header-only and free of game types, so the
// protocol tests cover it.
//
// A prim's keys over its system's time (a level San Andreas sets: the jetpack's or the nitro's strength, the
// dust's; or the seconds since a one-off system started) say how many sprites a second it lets out, how long
// they last and how fast they go; its keys over a sprite's life say its size, colour and alpha. A sprite
// comes out of its source between angleMin and angleMax degrees off the source's way, is slowed by friction
// each 1/50 s and pushed up by force. The prims, from effects.fxp (the heat haze prims are left out: Skyrim
// has nothing to draw them with):
//  - jetpack (CTaskSimpleJetPack::DoJetPackEffect, on each jet ball 15 cm down its axis): Nitrocenter, an
//    orange flame (255, 180, 0) let out 100 a second at a level of 0, 50 from 0.2 to 0.5 and none from 0.7,
//    and gasholding, a blue one (0, 180, 255), none up to 0.5, 50 a second there and 100 at 0.7; both
//    "cloudmasked", 1.4 m/s 10 degrees off down the jet ball, 0.7 s and 0.8 s, 5 cm growing to 80 cm, fading
//    out in their first fifth. The level steps a tenth a frame towards 1 while CJ thrusts, towards 0 when not.
//  - nitro (CAutomobile::DoNitroEffect, on each exhaust, backwards): Cooler, "collisionsmoke", a pale blue
//    puff around a level of 0.4 (30 a second), the nitro cooling down; Nitro, "smoke", added onto what's
//    behind, and Nitrocenter, "cloudmasked", both from 0.5 up to 100 a second at 1, 1 m/s, 1 s, 5 cm growing
//    to 80 cm (30 cm), orange turning blue (Nitro) or an orange core fading in its first fifth.
//  - exhaust smoke (CVehicle::AddExhaustParticles, prt_smokeII_3_expand): "collisionsmoke", grey, 5 s ± 2,
//    0.5 m growing to 2 m in its first fifth and on to 10 m, fading in to 255 at a fifth and out, pushed up
//    from 0.3 of its life; San Andreas scales each sprite by 0.2 in size and life, 0.25 in alpha (less, and
//    shorter, the faster the vehicle goes) and tints it (0.9, 0.9, 1).
//  - rotor dust (CAutomobile::DoHeliDustEffect, heli_dust's "normal" prim): "smokeII_3" from the ground under
//    the rotor, straight out along it, 5 to 50 a second, 1 to 25 m/s, 0.5 to 2 s with the level, friction
//    0.985, unseen for half its life, then up to 32 m across and alpha 64 at 0.6.
//  - tank_fire (Fx_c::TriggerTankFire at the Rhino's muzzle): Smoke, "smokeII_3", 100 a second falling to 50
//    over 0.3 s, 8 m/s up to 40 degrees off the barrel, spread over its first 2 m, 3 s, friction 0.95, up at
//    2 m/s², 1.2 m growing to 4 m, white fading out; Flash, "sphere", added on, 2,200 a second for 0.1 s,
//    0.02 s each, up to 5 m, yellow turning red.
//  - gunflash (the Hunter's guns): its flash, "sphere", 1,200 a second for 0.1 s, up to 1 m. A flash's sprite
//    is a stretched rectangle in San Andreas (SPRITERECT, 0.2 by 0.5 of its size), drawn here as a square of
//    the same area.
//
// A bone can't scale, so a prim's model holds kAges sizes of sprite, kPerAge of each, every size a mesh of its
// own colour and alpha, over the part of a sprite's life it can be seen in (a flame's first fifth, the dust's
// second half): a sprite is drawn on a bone of the size its age has reached, the newest first. Unused bones go
// far below the ground.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "body.h"
#include "pose.h"
#include "skin_model.h"
#include "smoke_model.h"

namespace AhShitRim::FxModel
{
    using Pose::Vec3;

    inline constexpr float kMeter = Pose::kUnitsPerMeter;
    inline constexpr int   kAges = 8;    // sizes and shades a sprite goes through
    inline constexpr int   kPerAge = 8;  // sprites of each drawn at once
    inline constexpr int   kBones = kAges * kPerAge;
    inline constexpr int   kKept = 4 * kBones;  // sprites an emitter keeps alive at most
    inline constexpr float kHidden = -100000.0f;
    inline constexpr float kSpinMin = 30.0f * 0.0174532925f;  // radians a second
    inline constexpr float kSpinMax = 60.0f * 0.0174532925f;
    inline constexpr float kSaFrames = 30.0f;    // San Andreas's frames a second, each of which puffs exhaust smoke
    inline constexpr float kFlashRect = 0.32f;   // a flash's rectangle (0.2 by 0.5 of its size) as a square of its area

    static_assert(kBones <= static_cast<int>(Protocol::kBodyBoneMax));

    struct Key
    {
        float t;
        float v;
    };

    // Up to six keys of effects.fxp: linear between them, flat past the ends.
    struct Curve
    {
        int n = 0;
        Key k[6]{};

        constexpr Curve() = default;
        constexpr Curve(float a_value) : n(1) { k[0] = { 0.0f, a_value }; }
        constexpr Curve(std::initializer_list<Key> a_keys)
        {
            for (const Key& key : a_keys) {
                if (n < 6) {
                    k[n++] = key;
                }
            }
        }

        constexpr float At(float a_t) const
        {
            if (n == 0) {
                return 0.0f;
            }
            if (a_t <= k[0].t) {
                return k[0].v;
            }
            for (int i = 1; i < n; ++i) {
                if (a_t <= k[i].t) {
                    const float span = k[i].t - k[i - 1].t;
                    return span > 0.0f ? k[i - 1].v + (k[i].v - k[i - 1].v) * (a_t - k[i - 1].t) / span : k[i].v;
                }
            }
            return k[n - 1].v;
        }
    };

    struct Prim
    {
        const char* name;
        const char* texture;    // in models/effectsPC.txd
        bool        additive;   // added onto what's behind (SRCBLEND 4, DSTBLEND 1), else blended (4, 5)
        Curve       rate;       // sprites a second, over the system's time
        Curve       life;       // seconds, over the system's time
        Curve       lifeBias;   // and how far a sprite's life strays either way
        Curve       speed;      // metres a second, over the system's time
        Curve       speedBias;  // and how far a sprite's speed strays either way
        float       angleMin;   // degrees off the source's way
        float       angleMax;
        float       spread;     // metres along the source's way a sprite may start (EMSIZE)
        float       friction;   // of the speed kept each 1/50 s
        Curve       force;      // metres a second², up, over a sprite's life
        Curve       size;       // metres across, over a sprite's life
        Curve       red;        // 0..255, over a sprite's life
        Curve       green;
        Curve       blue;
        Curve       alpha;
    };

    // What San Andreas multiplies a prim's sprites by (FxPrtMult_c), baked into the model.
    struct Mult
    {
        float red = 1.0f;
        float green = 1.0f;
        float blue = 1.0f;
        float alpha = 1.0f;
        float size = 1.0f;
    };

    // ---- The prims (models/effects.fxp) ----

    inline constexpr Prim kJetFlame{ "JetpackFlame", "cloudmasked", false, { { 0.0f, 100.0f }, { 0.2f, 50.0f }, { 0.501f, 50.0f }, { 0.7f, 0.0f } }, 0.7f, 0.0f,
        { { 0.0f, 1.4f }, { 0.5f, 1.4f }, { 1.0f, 0.5f } }, { { 0.0f, 0.4f }, { 0.5f, 0.5f }, { 1.0f, 0.4f } }, 10.0f, 10.0f, 0.0f, 1.0f, 0.0f,
        { { 0.0f, 0.05f }, { 1.0f, 0.8f } }, 255.0f, 180.0f, 0.0f, { { 0.0f, 255.0f }, { 0.2f, 0.0f } } };
    inline constexpr Prim kJetGas{ "JetpackGas", "cloudmasked", false, { { 0.0f, 0.0f }, { 0.2f, 0.0f }, { 0.501f, 50.0f }, { 0.7f, 100.0f } }, 0.8f, 0.0f,
        { { 0.0f, 0.0f }, { 0.5f, 1.4f }, { 1.0f, 1.4f } }, { { 0.0f, 0.0f }, { 0.5f, 0.5f }, { 1.0f, 0.0f } }, 10.0f, 10.0f, 0.0f, 1.0f, 0.0f,
        { { 0.0f, 0.05f }, { 1.0f, 0.8f } }, { { 0.0f, 255.0f }, { 0.05f, 0.0f } }, { { 0.0f, 255.0f }, { 0.05f, 180.0f } }, { { 0.0f, 100.0f }, { 0.05f, 255.0f } },
        { { 0.0f, 100.0f }, { 0.05f, 100.0f }, { 0.2f, 0.0f } } };

    inline constexpr Prim kNitroCooler{ "NitroCooler", "collisionsmoke", false, { { 0.0f, 0.0f }, { 0.4f, 30.0f }, { 0.5f, 0.0f } }, 0.2f, 0.0f, 1.0f, 0.0f, 0.0f, 10.0f,
        0.0f, 1.0f, 0.0f, { { 0.0f, 0.1f }, { 1.0f, 0.8f } }, { { 0.0f, 100.0f }, { 1.0f, 128.0f } }, { { 0.0f, 100.0f }, { 1.0f, 128.0f } },
        { { 0.0f, 180.0f }, { 1.0f, 255.0f } }, { { 0.0f, 128.0f }, { 1.0f, 0.0f } } };
    inline constexpr Prim kNitroFlame{ "NitroFlame", "smoke", true, { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.501f, 10.0f }, { 1.0f, 100.0f } }, 1.0f, 0.0f, 1.0f, 0.5f, 10.0f,
        10.0f, 0.0f, 1.0f, 0.0f, { { 0.0f, 0.05f }, { 1.0f, 0.8f } }, { { 0.0f, 255.0f }, { 0.3f, 25.0f }, { 1.0f, 0.0f } },
        { { 0.0f, 128.0f }, { 0.3f, 155.0f }, { 1.0f, 0.0f } }, { { 0.0f, 0.0f }, { 0.3f, 155.0f }, { 1.0f, 255.0f } },
        { { 0.0f, 128.0f }, { 0.3f, 128.0f }, { 1.0f, 0.0f } } };
    inline constexpr Prim kNitroCore{ "NitroCore", "cloudmasked", false, { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.501f, 10.0f }, { 1.0f, 100.0f } }, 1.0f, 0.0f, 1.0f, 0.5f, 10.0f,
        10.0f, 0.0f, 1.0f, 0.0f, { { 0.0f, 0.05f }, { 1.0f, 0.3f } }, 255.0f, 180.0f, 0.0f, { { 0.0f, 255.0f }, { 0.2f, 0.0f } } };

    // Its rate and speed are AddExhaustParticles's own (ExhaustRate, ExhaustVelocity), not keys.
    inline constexpr Prim kExhaust{ "ExhaustSmoke", "collisionsmoke", false, 0.0f, 5.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        { { 0.0f, 0.0f }, { 0.299f, 0.0f }, { 0.5f, 1.0f } }, { { 0.0f, 0.5f }, { 0.2f, 2.0f }, { 1.0f, 10.0f } }, 128.0f, 128.0f, 128.0f,
        { { 0.0f, 0.0f }, { 0.2f, 255.0f }, { 1.0f, 0.0f } } };
    inline constexpr Mult kExhaustMult{ 0.9f, 0.9f, 1.0f, 0.25f, 0.2f };  // at rest: the alpha falls, and the life, with speed

    // Straight out along the ground: 90 degrees off the way up.
    inline constexpr Prim kRotorDust{ "RotorDust", "smokeII_3", false, { { 0.0f, 5.0f }, { 1.0f, 50.0f } }, { { 0.0f, 0.5f }, { 1.0f, 2.0f } }, 0.0f,
        { { 0.0f, 1.0f }, { 1.0f, 25.0f } }, 0.0f, 90.0f, 90.0f, 0.0f, 0.985f, 0.0f, { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 1.0f, 32.0f } }, 255.0f, 255.0f,
        255.0f, { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.6f, 64.0f }, { 1.0f, 0.0f } } };

    // One-off systems: their time is the seconds since they started.
    inline constexpr Prim kTankSmoke{ "TankSmoke", "smokeII_3", false, { { 0.0f, 100.0f }, { 0.3f, 50.0f }, { 0.301f, 0.0f } }, 3.0f, 0.0f, 8.0f, 0.0f, 0.0f, 40.0f,
        2.0f, 0.95f, 2.0f, { { 0.0f, 1.2f }, { 1.0f, 4.0f } }, 255.0f, 255.0f, 255.0f, { { 0.0f, 128.0f }, { 1.0f, 0.0f } } };
    inline constexpr Prim kTankFlash{ "TankFlash", "sphere", true, { { 0.0f, 2200.0f }, { 0.1f, 2200.0f }, { 0.101f, 0.0f } }, 0.02f, 0.0f, 1.0f, 0.0f, 20.0f, 20.0f,
        0.0f, 1.0f, 0.0f, { { 0.0f, 0.0f }, { 0.05f, 5.0f }, { 0.45f, 5.0f }, { 1.0f, 0.0f } }, 255.0f, { { 0.0f, 255.0f }, { 0.7f, 0.0f } },
        { { 0.0f, 128.0f }, { 0.7f, 0.0f } }, { { 0.0f, 128.0f }, { 0.7f, 0.0f } } };
    inline constexpr Prim kGunFlash{ "GunFlash", "sphere", true, { { 0.0f, 1200.0f }, { 0.1f, 1200.0f }, { 0.101f, 0.0f } }, 0.02f, 0.0f, 1.0f, 0.0f, 20.0f, 20.0f,
        0.0f, 1.0f, 0.0f, { { 0.0f, 0.0f }, { 0.05f, 1.0f }, { 0.45f, 1.0f }, { 1.0f, 0.0f } }, 255.0f, { { 0.0f, 255.0f }, { 0.7f, 0.0f } },
        { { 0.0f, 128.0f }, { 0.7f, 0.0f } }, { { 0.0f, 128.0f }, { 0.7f, 0.0f } } };
    inline constexpr Mult  kFlashMult{ 1.0f, 1.0f, 1.0f, 1.0f, kFlashRect };
    inline constexpr float kOneOffLength = 0.4f;  // seconds a one-off system lets sprites out (tank_fire's smoke stops at 0.3)

    inline constexpr Vec3 kJetballOffset{ 0.0f, 0.0f, -0.15f };  // JETBALL_OFFSET, metres in the jet ball's frame

    // ---- Seeing a sprite ----

    // The part of a sprite's life it can be seen in: from just before the alpha first rises to where it is
    // gone for good.
    inline float VisibleFrom(const Prim& a_prim)
    {
        const Curve& a = a_prim.alpha;
        for (int i = 0; i < a.n; ++i) {
            if (a.k[i].v > 0.0f) {
                return i == 0 ? 0.0f : std::clamp(a.k[i - 1].t, 0.0f, 1.0f);
            }
        }
        return 1.0f;
    }

    inline float VisibleTo(const Prim& a_prim)
    {
        const Curve& a = a_prim.alpha;
        for (int i = a.n - 1; i >= 0; --i) {
            if (a.k[i].v > 0.0f) {
                return i == a.n - 1 ? 1.0f : std::clamp(a.k[i + 1].t, 0.0f, 1.0f);
            }
        }
        return 0.0f;
    }

    // The age bucket of a sprite a_t through its life, -1 while it can't be seen yet.
    inline int AgeBucket(const Prim& a_prim, float a_t)
    {
        const float from = VisibleFrom(a_prim);
        const float to = VisibleTo(a_prim);
        if (a_t < from || !(to > from)) {
            return -1;
        }
        return std::clamp(static_cast<int>((a_t - from) / (to - from) * kAges), 0, kAges - 1);
    }

    inline float BucketMiddle(const Prim& a_prim, int a_age)
    {
        const float from = VisibleFrom(a_prim);
        return from + (VisibleTo(a_prim) - from) * (static_cast<float>(a_age) + 0.5f) / kAges;
    }

    // An age bucket's sprite: half its size in Skyrim units, its colour and alpha.
    inline float SizeOf(const Prim& a_prim, int a_age, const Mult& a_mult = {})
    {
        return 0.5f * a_prim.size.At(BucketMiddle(a_prim, a_age)) * a_mult.size * kMeter;
    }

    inline std::uint8_t Channel(float a_value) { return static_cast<std::uint8_t>(std::lround(std::clamp(a_value, 0.0f, 255.0f))); }

    inline std::array<std::uint8_t, 4> ColourOf(const Prim& a_prim, int a_age, const Mult& a_mult = {})
    {
        const float t = BucketMiddle(a_prim, a_age);
        return { Channel(a_prim.red.At(t) * a_mult.red), Channel(a_prim.green.At(t) * a_mult.green), Channel(a_prim.blue.At(t) * a_mult.blue),
            Channel(a_prim.alpha.At(t) * a_mult.alpha) };
    }

    // a_prim's kBones sprites, one quad each in its bone's XZ plane facing -Y: bone b is of age bucket
    // b / kPerAge, each age bucket a mesh of its own colour and alpha. a_sprite is the prim's texture.
    inline SkinModel::ModelIn Build(const Prim& a_prim, const SkinModel::TextureIn& a_sprite, const Mult& a_mult = {})
    {
        SkinModel::ModelIn model;
        model.name = a_prim.name;
        model.kind = Protocol::kSkinModelVehicle;  // a vehicle's wide bounds: the sprites spread round their source
        model.textures.push_back(a_sprite);
        for (int age = 0; age < kAges; ++age) {
            SkinModel::MeshIn mesh;
            mesh.texture = 0;
            mesh.flags = Protocol::kSkinMeshBlend | (a_prim.additive ? Protocol::kSkinMeshAdditive : 0u);
            const auto colour = ColourOf(a_prim, age, a_mult);
            for (int c = 0; c < 4; ++c) {
                mesh.color[c] = colour[c];
            }
            const float h = SizeOf(a_prim, age, a_mult);
            for (int k = 0; k < kPerAge; ++k) {
                const auto  bone = static_cast<std::uint32_t>(age * kPerAge + k);
                const auto  base = static_cast<std::uint16_t>(mesh.vertices.size());
                const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
                model.boneIds.push_back(bone);
                for (const auto& corner : corners) {
                    Protocol::SkinVertex v{};
                    v.position[0] = corner[0] * h;
                    v.position[2] = corner[1] * h;
                    v.normal[1] = -1.0f;
                    v.uv[0] = corner[0] * 0.5f + 0.5f;
                    v.uv[1] = 0.5f - corner[1] * 0.5f;
                    v.bones[0] = static_cast<std::uint8_t>(bone);
                    v.weights[0] = 1.0f;
                    mesh.vertices.push_back(v);
                }
                for (const int i : { 0, 1, 2, 0, 2, 3 }) {
                    mesh.indices.push_back(static_cast<std::uint16_t>(base + i));
                }
            }
            model.meshes.push_back(std::move(mesh));
        }
        return model;
    }

    // ---- Letting them out ----

    // Where sprites come out and which way, Skyrim world units and a unit vector.
    struct Source
    {
        Vec3 at;
        Vec3 along{ 0.0f, 0.0f, 1.0f };
    };

    // A unit vector between a_min and a_max degrees off a_axis (a unit vector), from two numbers in 0..1.
    inline Vec3 Spread(const Vec3& a_axis, float a_min, float a_max, float a_u, float a_v)
    {
        const Vec3  helper = std::fabs(a_axis.z) < 0.9f ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
        const Vec3  side = Body::Normalize(Body::Cross(a_axis, helper), { 1.0f, 0.0f, 0.0f });
        const Vec3  up = Body::Cross(side, a_axis);
        const float tilt = (a_min + (a_max - a_min) * a_u) * 0.0174532925f;
        const float around = a_v * 6.2831853f;
        const Vec3  off = Body::Add(Body::Scale(side, std::cos(around)), Body::Scale(up, std::sin(around)));
        return Body::Normalize(Body::Add(Body::Scale(a_axis, std::cos(tilt)), Body::Scale(off, std::sin(tilt))), a_axis);
    }

    // One prim's sprites in the world: let out from its sources, moving on their own from there (or along with
    // their source, for San Andreas's local particles) and drawn until they are gone.
    class Emitter
    {
    public:
        struct Sprite
        {
            Vec3  at;
            Vec3  speed;  // units a second
            float age = 0.0f;
            float life = 1.0f;
            float roll = 0.0f;
            float spin = 0.0f;
        };

        explicit Emitter(const Prim& a_prim = kExhaust) : _prim(&a_prim), _to(VisibleTo(a_prim)) {}

        const Prim& PrimOf() const { return *_prim; }

        // a_dt seconds on: each sprite moves, a_shift on top (how far its source moved, for particles that go
        // along with it), slows and is pushed up; the ones past what can be seen of them go.
        void Move(float a_dt, const Vec3& a_shift = {})
        {
            const float dt = std::clamp(a_dt, 0.0f, 0.25f);
            const float keep = _prim->friction < 1.0f ? std::pow(_prim->friction, dt * 50.0f) : 1.0f;
            for (auto& sprite : _sprites) {
                sprite.age += dt;
                sprite.speed = Body::Scale(sprite.speed, keep);
                sprite.speed.z += _prim->force.At(sprite.age / sprite.life) * kMeter * dt;
                sprite.at = Body::Add(sprite.at, Body::Add(Body::Scale(sprite.speed, dt), a_shift));
                sprite.roll += sprite.spin * dt;
            }
            std::erase_if(_sprites, [&](const Sprite& a_sprite) { return a_sprite.age >= a_sprite.life * _to; });
        }

        // a_dt seconds of the prim letting sprites out at system time a_level, a_scale times its rate, from
        // a_sources in turn, each carried along at a_carry units a second (the vehicle's own speed).
        void Emit(float a_dt, float a_level, std::span<const Source> a_sources, float a_scale = 1.0f, const Vec3& a_carry = {})
        {
            Every(a_dt, a_sources.empty() ? 0.0f : _prim->rate.At(a_level) * a_scale, [&](std::uint32_t a_n, float a_age) {
                const Source& source = a_sources[a_n % a_sources.size()];
                const Vec3    dir = Spread(source.along, _prim->angleMin, _prim->angleMax, SmokeModel::Hash(a_n * 6u), SmokeModel::Hash(a_n * 6u + 1u));
                const float speed = (_prim->speed.At(a_level) + _prim->speedBias.At(a_level) * (2.0f * SmokeModel::Hash(a_n * 6u + 2u) - 1.0f)) * kMeter;
                const Vec3  at = Body::Add(source.at, Body::Scale(source.along, _prim->spread * kMeter * SmokeModel::Hash(a_n * 6u + 4u)));
                Add(at, Body::Add(Body::Scale(dir, speed), a_carry), LifeOf(a_level, a_n), a_age);
            });
        }

        // a_dt seconds of a_rate sprites a second from a_sources in turn, each at a_speed units a second and
        // living a_lifeScale times the prim's life: San Andreas's AddParticle with its own speed and multipliers
        // (the exhaust smoke).
        void Puff(float a_dt, float a_rate, std::span<const Source> a_sources, const Vec3& a_speed, float a_lifeScale)
        {
            Every(a_dt, a_sources.empty() || !(a_lifeScale > 0.0f) ? 0.0f : a_rate, [&](std::uint32_t a_n, float a_age) {
                Add(a_sources[a_n % a_sources.size()].at, a_speed, LifeOf(0.0f, a_n) * a_lifeScale, a_age);
            });
        }

        // One sprite at a_at moving at a_speed units a second, for a_life seconds, a_age of it gone already.
        void Add(const Vec3& a_at, const Vec3& a_speed, float a_life, float a_age = 0.0f)
        {
            const std::uint32_t n = _count++;
            if (!(a_life > 0.0f) || _sprites.size() >= static_cast<std::size_t>(kKept)) {
                return;
            }
            Sprite sprite;
            sprite.at = Body::Add(a_at, Body::Scale(a_speed, a_age));
            sprite.speed = a_speed;
            sprite.age = a_age;
            sprite.life = a_life;
            sprite.roll = SmokeModel::Hash(n * 6u + 5u) * 6.2831853f;
            sprite.spin = kSpinMin + (kSpinMax - kSpinMin) * SmokeModel::Hash(n * 7u + 3u);
            _sprites.push_back(sprite);
        }

        bool Empty() const { return _sprites.empty(); }
        const std::vector<Sprite>& Sprites() const { return _sprites; }

        // Every bone relative to a_origin: each sprite that can be seen on a free bone of its age's size, facing
        // a_camera, the newest of an age first; the rest hidden.
        void Pose(const Vec3& a_origin, const Vec3& a_camera, Protocol::BodyState& a_out) const
        {
            std::array<int, kAges> used{};
            a_out.boneCount = kBones;
            for (int b = 0; b < kBones; ++b) {
                auto& bone = a_out.bones[b];
                bone.rotation[0] = bone.rotation[1] = bone.rotation[2] = 0.0f;
                bone.rotation[3] = 1.0f;
                bone.translation[0] = bone.translation[1] = 0.0f;
                bone.translation[2] = kHidden;
            }
            for (auto it = _sprites.rbegin(); it != _sprites.rend(); ++it) {
                const int age = AgeBucket(*_prim, it->age / it->life);
                if (age < 0 || used[age] >= kPerAge) {
                    continue;
                }
                auto&            bone = a_out.bones[age * kPerAge + used[age]++];
                const Body::Quat q = SmokeModel::Facing(it->at, a_camera, it->roll);
                bone.rotation[0] = q.x;
                bone.rotation[1] = q.y;
                bone.rotation[2] = q.z;
                bone.rotation[3] = q.w;
                bone.translation[0] = it->at.x - a_origin.x;
                bone.translation[1] = it->at.y - a_origin.y;
                bone.translation[2] = it->at.z - a_origin.z;
            }
        }

        void Clear()
        {
            _sprites.clear();
            _since = 1.0f;
        }

    private:
        float LifeOf(float a_level, std::uint32_t a_n) const
        {
            return _prim->life.At(a_level) + _prim->lifeBias.At(a_level) * (2.0f * SmokeModel::Hash(a_n * 6u + 3u) - 1.0f);
        }

        // a_spawn(n, age) for each sprite a_dt seconds at a_rate a second let out, the first at once.
        template <class F>
        void Every(float a_dt, float a_rate, F&& a_spawn)
        {
            if (!(a_rate > 0.0f)) {
                _since = 1.0f;  // the first sprite comes at once when it starts again
                return;
            }
            const float every = 1.0f / a_rate;
            _since = (std::min)(_since, every) + std::clamp(a_dt, 0.0f, 0.25f);
            while (_since >= every) {
                _since -= every;
                a_spawn(_count, _since);
            }
        }

        const Prim*         _prim;
        float               _to;  // VisibleTo
        std::vector<Sprite> _sprites;
        float               _since = 1.0f;  // seconds since the last sprite
        std::uint32_t       _count = 0;
    };

    // ---- San Andreas's rules ----

    inline std::uint8_t ToByte(float a_level) { return static_cast<std::uint8_t>(std::lround(std::clamp(a_level, 0.0f, 1.0f) * 255.0f)); }
    inline float        FromByte(std::uint8_t a_byte) { return static_cast<float>(a_byte) / 255.0f; }
    inline std::int8_t  GasToByte(float a_gas) { return static_cast<std::int8_t>(std::lround(std::clamp(a_gas, -1.0f, 1.0f) * 127.0f)); }
    inline float        GasFromByte(std::int8_t a_byte) { return static_cast<float>(a_byte) / 127.0f; }

    // The jetpack's level after one more frame (DoJetPackEffect): a tenth up while CJ thrusts (forward, back
    // or to the side), a tenth down while not.
    inline float JetpackLevel(float a_level, bool a_thrusting)
    {
        return a_thrusting ? (std::min)(a_level + 0.1f, 1.0f) : (std::max)(a_level - 0.1f, 0.0f);
    }

    // The nitro's level (CAutomobile::NitrousControl): a_value is San Andreas's m_fTireTemperature, 1 ready,
    // under 0 while the nitro burns (half to full strength with the gas pedal), 0 to 1 while it charges again
    // (the strength fading from half to none, where its cooler puffs).
    inline float NitroLevel(float a_value, float a_gas)
    {
        if (a_value < 0.0f) {
            return a_gas <= 0.0f ? 0.5f : std::fabs(a_gas) * 0.5f + 0.5f;
        }
        if (a_value < 1.0f) {
            return (1.0f - a_value) * 0.5f;
        }
        return 0.0f;
    }

    // AddExhaustParticles puffs each San Andreas frame if a random 1 to 3 times the gas pedal plus 1.1 is over
    // 2.5: the chance of that.
    inline float ExhaustChance(float a_gas)
    {
        const float pedal = a_gas + 1.1f;
        if (!(pedal > 0.0f)) {
            return 0.0f;
        }
        return std::clamp((3.0f - 2.5f / pedal) / 2.0f, 0.0f, 1.0f);
    }

    // Sprites a second from each exhaust: two a puff, and in first or second gear with the pedal over half one
    // more at one exhaust or the other.
    inline float ExhaustRate(float a_gas, bool a_lowGear)
    {
        const float extra = a_lowGear && a_gas > 0.5f ? 0.5f : 0.0f;
        return kSaFrames * ExhaustChance(a_gas) * (2.0f + extra);
    }

    // San Andreas's multipliers for a sprite at a_speed metres a second: its alpha (0.25 at rest, none at 25 m/s)
    // and its life (0.2 at rest, none at 20 m/s).
    inline float ExhaustAlpha(float a_speed) { return (std::max)(0.25f - a_speed / 100.0f, 0.0f); }
    inline float ExhaustLife(float a_speed) { return (std::max)(0.2f - a_speed / 100.0f, 0.0f); }

    // The model is baked at rest's alpha, so a fainter puff is drawn as fewer sprites.
    inline float ExhaustScale(float a_speed) { return ExhaustAlpha(a_speed) / kExhaustMult.alpha; }

    // A sprite's speed, units a second: going forward faster than 0.05 m a step (2.5 m/s), 30 times the
    // vehicle's speed a step (0.6 of it); else backwards out of the exhaust at 1.35 m/s (San Andreas's random
    // 0.9 to 1.8, picked once a game). a_velocity is the vehicle's, units a second; a_forward its front.
    inline Vec3 ExhaustVelocity(const Vec3& a_velocity, const Vec3& a_forward)
    {
        if (Body::Dot(a_forward, a_velocity) / kMeter >= 0.05f * 50.0f) {
            return Body::Scale(a_velocity, 0.6f);
        }
        return Body::Scale(a_forward, -1.35f * kMeter);
    }
}
