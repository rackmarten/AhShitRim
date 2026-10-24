// Fall rescue: CJ below Skyrim's ground in San Andreas is put back where he last stood. Game-free,
// so the protocol tests cover it; the San Andreas ASI feeds it every frame (sa/src/ground.cpp).
//
// PerkeleRim's rule (msc/src/Link/FallRescue.cs and the player's CatchFall): with no surface under
// his feet but one above (he slipped through the land or into a rock, whose inside isn't solid), CJ
// goes back where he last stood within kReach, and with no such spot only onto a surface at most
// kMaxAbove over his feet, never a ceiling. With no surface below or above at all (a hole in the
// collision, or ground that hasn't arrived) he goes back where he last stood, however far.
//
// v23: below the land Skyrim sampled (Ground::LandMap) by more than kUnderLand, he goes back on top of it, right
// above where he is, whatever is under him: a hard crash pushed a car through the land's thin triangles and the
// buried part of a rock under it passed for ground, so the spot he "last stood" was under the land too and every
// rescue put him back there. A spot under the land is never remembered. On top of the land only where San Andreas
// has that ground (the lowest surface over him is the land's): where its chunk was missing, a car put on the land's
// height fell straight through again, every 0.7 s for minutes; there he goes back where he last stood instead.

#pragma once

#include <cmath>
#include <optional>

#include "pose.h"

namespace AhShitRim::FallRescue
{
    using Pose::Vec3;

    inline constexpr float kReach = 25.0f;     // metres from the last standing spot
    inline constexpr float kMaxAbove = 1.0f;   // metres over the feet a surface may be with nothing remembered
    inline constexpr float kGrace = 0.25f;     // seconds below the ground before the rescue
    inline constexpr float kBelowReach = 500.0f;  // how far down San Andreas looks for a surface
    inline constexpr float kAboveReach = 200.0f;  // ... and up
    inline constexpr float kUnderLand = 0.5f;     // metres below the sampled land that count as under it
    inline constexpr float kLandMatch = 0.5f;     // the lowest surface over him this close to the land's height is the land

    // What San Andreas sees around CJ this frame. Positions are his feet, San Andreas metres.
    struct Around
    {
        Vec3  feet;
        bool  standing = false;      // on something (CPed::bIsStanding)
        bool  surfaceBelow = false;  // within kBelowReach under the feet
        bool  surfaceAbove = false;  // within kAboveReach over them
        float aboveZ = 0.0f;         // the lowest surface above, with surfaceAbove
        std::optional<float> landZ;  // v23: the sampled land's height right over or under the feet, where known
    };

    // Whether the feet are under the sampled land.
    inline bool UnderLand(const Around& a_around)
    {
        return a_around.landZ && a_around.feet.z < *a_around.landZ - kUnderLand;
    }

    // Whether San Andreas has the sampled land over him: something to put him on.
    inline bool LandBuilt(const Around& a_around)
    {
        return a_around.landZ && a_around.surfaceAbove && std::abs(a_around.aboveZ - *a_around.landZ) <= kLandMatch;
    }

    class Watch
    {
    public:
        // Returns where to put CJ's feet, or nothing to leave him be.
        std::optional<Vec3> Update(float a_dt, const Around& a_around)
        {
            if (UnderLand(a_around)) {
                _below += a_dt;
                if (_below < kGrace) {
                    return std::nullopt;
                }
                _below = 0.0f;
                if (LandBuilt(a_around) || !_haveStood) {
                    return Vec3{ a_around.feet.x, a_around.feet.y, *a_around.landZ };
                }
                return _stood;
            }
            if (a_around.standing && a_around.surfaceBelow) {
                _stood = a_around.feet;
                _haveStood = true;
                _below = 0.0f;
                return std::nullopt;
            }
            const bool underground = !a_around.surfaceBelow && a_around.surfaceAbove;
            const bool hole = !a_around.surfaceBelow && !a_around.surfaceAbove;
            if (!underground && !hole) {
                _below = 0.0f;
                return std::nullopt;
            }
            _below += a_dt;
            if (_below < kGrace) {
                return std::nullopt;
            }
            _below = 0.0f;
            if (_haveStood && (hole || Distance(_stood, a_around.feet) <= kReach)) {
                return _stood;
            }
            if (underground && a_around.aboveZ - a_around.feet.z <= kMaxAbove) {
                return Vec3{ a_around.feet.x, a_around.feet.y, a_around.aboveZ };
            }
            return std::nullopt;
        }

        // The floating origin moved: the remembered spot moves with it.
        void Shift(const Vec3& a_shift)
        {
            _stood = { _stood.x - a_shift.x, _stood.y - a_shift.y, _stood.z - a_shift.z };
        }

        // A new handover: nothing remembered.
        void Reset()
        {
            _haveStood = false;
            _below = 0.0f;
        }

        bool HaveStood() const { return _haveStood; }

    private:
        static float Distance(const Vec3& a_a, const Vec3& a_b)
        {
            return std::sqrt((a_a.x - a_b.x) * (a_a.x - a_b.x) + (a_a.y - a_b.y) * (a_a.y - a_b.y) + (a_a.z - a_b.z) * (a_a.z - a_b.z));
        }

        Vec3  _stood;
        bool  _haveStood = false;
        float _below = 0.0f;
    };
}
