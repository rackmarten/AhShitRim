// A broken position never sticks: CJ at a position that isn't a number, or out of the world Skyrim's
// ground is built in, is put back where he last was sound, or at the handover's anchor. Game-free,
// so the protocol tests cover it; the San Andreas ASI feeds it every frame (sa/src/chunk_world.cpp).
//
// Why: San Andreas killed CJ in a vehicle that blew up and ran its own Wasted, which put him at its
// hospital (with no scripts run, its (0, 0, 0): about a metre over its sea, half a kilometre under the
// floating origin), and a frame later his position wasn't a number. The origin then moved by NaN
// every frame, which took the collision chunks, every vehicle and the camera with it, and nothing
// set them right again, not even a load. San Andreas's Wasted no longer runs (sa/src/vitals.cpp),
// the origin refuses such a move (Ground::Origin::Recenter), and this puts CJ back whatever else
// breaks his position.

#pragma once

#include <cmath>
#include <optional>

#include "ground.h"
#include "pose.h"

namespace AhShitRim::PositionGuard
{
    using Pose::Vec3;

    // San Andreas metres. The floating origin moves once CJ is kRecenterHeight under kHeight (200 m),
    // so a hundred metres under that is no fall but something putting him where Skyrim's ground can't
    // be: San Andreas's own "fell through the map" rescue drops a player's car at its sea level (0).
    // So is anything this far out.
    inline constexpr float kFloor = Ground::kHeight - Ground::kRecenterHeight - 100.0f;
    inline constexpr float kLimit = 20000.0f;
    inline constexpr float kSettle = 2.0f;  // seconds sound before the last good spot is trusted again

    inline bool Finite(const Vec3& a_v) { return std::isfinite(a_v.x) && std::isfinite(a_v.y) && std::isfinite(a_v.z); }

    inline bool Sound(const Vec3& a_v)
    {
        return Finite(a_v) && a_v.z > kFloor && a_v.z < kLimit && std::abs(a_v.x) < kLimit && std::abs(a_v.y) < kLimit;
    }

    enum class To
    {
        LastGood,  // where he was sound last
        Anchor,    // where the handover put him: nothing sound yet, or the last good spot broke again
    };

    struct Recovery
    {
        Vec3 at;
        To   to = To::LastGood;
    };

    inline const char* NameOf(To a_to) { return a_to == To::Anchor ? "the handover's anchor" : "where he last was"; }

    class Watch
    {
    public:
        // a_cj is CJ's position this frame, a_anchor the handover's spot (both San Andreas metres, after
        // any origin move). Returns where to put him, or nothing to leave him be.
        std::optional<Recovery> Update(float a_dt, const Vec3& a_cj, const Vec3& a_anchor)
        {
            if (Sound(a_cj)) {
                _good = a_cj;
                _haveGood = true;
                _sound += a_dt;
                if (_sound >= kSettle) {
                    _recovered = false;
                }
                return std::nullopt;
            }
            _sound = 0.0f;
            // Put back once and broken again before he settled: the spot itself is no good.
            const bool again = _recovered;
            _recovered = true;
            if (_haveGood && !again) {
                return Recovery{ _good, To::LastGood };
            }
            return Recovery{ Sound(a_anchor) ? a_anchor : Vec3{ 0.0f, 0.0f, Ground::kHeight }, To::Anchor };
        }

        // The floating origin moved: the remembered spot moves with it.
        void Shift(const Vec3& a_shift)
        {
            if (Finite(a_shift)) {
                _good = { _good.x - a_shift.x, _good.y - a_shift.y, _good.z - a_shift.z };
            }
        }

        // A new handover (a load, a load door, fast travel): nothing remembered.
        void Reset()
        {
            _haveGood = false;
            _recovered = false;
            _sound = 0.0f;
        }

    private:
        Vec3  _good;
        bool  _haveGood = false;
        bool  _recovered = false;
        float _sound = 0.0f;
    };
}
