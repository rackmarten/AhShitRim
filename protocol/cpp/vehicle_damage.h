// A damaged vehicle in Skyrim (protocol v18), the game-free half: San Andreas's damage stages (engine smoke
// as its health drops, then fire, then the blow-up and a burning wreck) and what Skyrim shows for each.
// Header-only and free of game types, so the protocol tests cover it.
//
// San Andreas does the damage (its vehicles run in the hidden San Andreas); its particles are never drawn, so
// the vehicle slot carries the health, the wrecked and burning flags and the engine's place, and Skyrim puts
// San Andreas's own white and black smoke (smoke_model.h) and a vanilla fire there. The thresholds are San Andreas's own (gta-reversed):
//  - CVehicle::AddDamagedVehicleParticles: "overheat_car" smoke from the engine dummy while the health is
//    under 650 and not under 250, thicker and darker as it drops (its SetConstTime: 1 - (health - 250) / 400),
//  - CAutomobile::ProcessCarOnFireAndExplode: under 250 (not wrecked, not drowning) "fire_car" at the engine
//    dummy, and the vehicle blows up once it has burnt for 5 s (m_fBurnTimer over 5000 ms),
//  - CAutomobile::BlowUpCar: the blast, STATUS_WRECKED, the wreck drawn scorched (bRenderScorched) and set
//    burning (gFireManager.StartFire on it).
// Bikes follow the same health rules (CBike::ProcessControl and its own BlowUpCar).
//
// Units are Skyrim's (70 to the metre) and seconds unless a name says metres.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../ahshitrim_protocol.h"
#include "pose.h"
#include "vehicle.h"

namespace AhShitRim::VehicleDamage
{
    using Pose::Vec3;

    // ---- San Andreas's stages ----

    inline constexpr float kHealthFull = 1000.0f;
    inline constexpr float kSmokeBelow = 650.0f;  // overheat smoke under this
    inline constexpr float kFireBelow = 250.0f;   // the engine burns under this...
    inline constexpr float kBurnSeconds = 5.0f;   // ... and blows up after this long

    // Whether San Andreas has the engine on fire: ProcessCarOnFireAndExplode's own test.
    constexpr bool EngineBurns(float a_health, bool a_wrecked, bool a_drowning) { return a_health < kFireBelow && !a_wrecked && !a_drowning; }

    // How thick the engine smokes, 0 (none) to 1: overheat_car's strength, 0 at 650 and 1 at 250. None at or
    // over 650, under 250 (it burns instead) or under water.
    inline float SmokeLevel(float a_health, bool a_submerged = false)
    {
        if (a_submerged || !(a_health < kSmokeBelow) || a_health < kFireBelow) {
            return 0.0f;
        }
        return std::clamp(1.0f - (a_health - kFireBelow) / (kSmokeBelow - kFireBelow), 0.0f, 1.0f);
    }

    enum class Stage : std::uint8_t
    {
        Fine,
        Smoke,   // the engine smokes
        Fire,    // the engine burns: it blows up within kBurnSeconds
        Wreck,   // it blew up (burning while kBodyBurning says so)
    };

    // A vehicle slot's stage, from its flags and health.
    inline Stage StageOf(std::uint32_t a_flags, float a_health)
    {
        if (a_flags & Protocol::kBodyWrecked) {
            return Stage::Wreck;
        }
        if (a_flags & Protocol::kBodyBurning) {
            return Stage::Fire;
        }
        return SmokeLevel(a_health) > 0.0f ? Stage::Smoke : Stage::Fine;
    }

    // Where the smoke and fire come from, in the vehicle's own space (metres, +Y forward): its engine dummy,
    // or a model without one (all zero) the bonnet: a little in from the front of its box, above its middle.
    inline Vec3 EnginePoint(const Vec3& a_dummy, const Vec3& a_boxMin, const Vec3& a_boxMax)
    {
        if (a_dummy.x != 0.0f || a_dummy.y != 0.0f || a_dummy.z != 0.0f) {
            return a_dummy;
        }
        return { 0.0f, a_boxMax.y * 0.7f, a_boxMin.z + (a_boxMax.z - a_boxMin.z) * 0.6f };
    }

    // A wreck's paint and materials: San Andreas draws it with its lights swapped for a dim scorched set
    // (WorldReplaceNormalLightsWithScorched, 0.1 ambient), nearly black.
    inline constexpr float kScorched = 0.2f;

    inline Vehicle::Rgb Scorch(const Vehicle::Rgb& a_colour)
    {
        const auto dim = [](std::uint8_t a_c) { return static_cast<std::uint8_t>(std::lround(a_c * kScorched)); };
        return { dim(a_colour.r), dim(a_colour.g), dim(a_colour.b) };
    }

    // ---- What Skyrim shows ----

    // San Andreas's smoke, drawn with its own sprites (smoke_model.h): the strengths its WhiteSmoke and
    // BlackSmoke run at, negative for none. While the vehicle smokes both run at the smoke's strength, as
    // overheat_car does (the white thinning out as the black thickens); through the fire and while a wreck
    // burns, the black at full strength.
    struct Smoke
    {
        float white = -1.0f;
        float black = -1.0f;
    };

    inline Smoke SmokeOf(Stage a_stage, float a_level, bool a_burning)
    {
        if (a_stage == Stage::Smoke) {
            return { a_level, a_level };
        }
        if (a_stage == Stage::Fire || (a_stage == Stage::Wreck && a_burning)) {
            return { -1.0f, 1.0f };
        }
        return {};
    }

    // A fire: Skyrim's small fire, which only scales evenly, so `layers` of it stacked `rise` units apart,
    // each a share `taper` smaller than the one under it, burn tall and narrow. Each is spawned every `every`
    // seconds for `life` seconds.
    struct Fire
    {
        float scale = 1.0f;
        int   layers = 1;
        float rise = 0.0f;
        float taper = 0.0f;
        float every = 0.2f;
        float life = 0.5f;

        constexpr float ScaleOf(int a_layer) const { return scale * (1.0f - taper * static_cast<float>(a_layer)); }
        constexpr float HeightOf(int a_layer) const { return rise * static_cast<float>(a_layer); }
    };

    // The engine's fire, and a burning wreck's (bigger, from its middle): big enough to see from a distance.
    inline constexpr Fire kEngineFire{ 1.8f, 4, 40.0f, 0.15f, 0.2f, 0.5f };
    inline constexpr Fire kWreckFire{ 2.6f, 4, 55.0f, 0.15f, 0.2f, 0.6f };

    // ---- The blow-up ----

    // Watches one vehicle slot: says once when the vehicle in it blows up, never for a wreck it first saw
    // already wrecked (an older blow-up, or one from before a handover).
    class Watch
    {
    public:
        // A read of the slot: whether it holds a vehicle, the handover it belongs to and whether it is a wreck.
        bool BlewUp(bool a_valid, std::uint32_t a_handover, bool a_wrecked)
        {
            if (!a_valid) {
                _seen = false;
                return false;
            }
            const bool blew = _seen && _handover == a_handover && !_wrecked && a_wrecked;
            _seen = true;
            _handover = a_handover;
            _wrecked = a_wrecked;
            return blew;
        }

        void Reset() { _seen = false; }

    private:
        bool          _seen = false;
        bool          _wrecked = false;
        std::uint32_t _handover = 0;
    };
}
