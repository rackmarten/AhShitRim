// CJ knocked down by a blow in Skyrim (v22), the game-free half. Skyrim's player is hidden while CJ has it, so a
// giant's club, a mammoth's charge or a shout that ragdolls it would leave its ragdoll fighting the position CJ's
// pose puts it at every frame, and its camera stuck. Instead the player is left to fall and get up out of sight, and
// San Andreas throws CJ away from the blow as its own explosions throw peds: a launch and its fall-and-get-up, which
// way he falls by where the blow came from. Header-only and free of game types, so the protocol tests cover it;
// skse/src/player/PlayerSync.cpp sends it (an Effect message, EffectKind::KnockCj), sa/src/cj_projectiles.cpp plays it.

#pragma once

#include <cmath>

namespace AhShitRim::Knock
{
    inline constexpr float kSpeed = 10.0f;      // metres a second CJ is thrown at, as a car's blast throws him
    inline constexpr float kRise = 0.6f;        // and upwards, this share of it
    inline constexpr float kBlowSeconds = 1.0f;  // a hit this recent is the blow that knocked the player down

    // San Andreas's falls (CTaskComplexFallAndGetUp's direction): 0 on his front (KO_skid_front), 1 spinning right,
    // 2 on his back (KO_skid_back), 3 spinning left.
    enum Fall : int
    {
        kFront = 0,
        kSpinRight = 1,
        kBack = 2,
        kSpinLeft = 3,
    };

    // Which way CJ, facing (a_faceX, a_faceY), falls when thrown along (a_pushX, a_pushY): pushed on, onto his front;
    // pushed back, onto his back; from the side, spinning.
    inline Fall FallFor(float a_faceX, float a_faceY, float a_pushX, float a_pushY)
    {
        const float face = std::hypot(a_faceX, a_faceY);
        const float push = std::hypot(a_pushX, a_pushY);
        if (face <= 0.0f || push <= 0.0f || !std::isfinite(face) || !std::isfinite(push)) {
            return kBack;
        }
        const float along = (a_faceX * a_pushX + a_faceY * a_pushY) / (face * push);
        if (along > 0.7f) {
            return kFront;
        }
        if (along < -0.7f) {
            return kBack;
        }
        const float side = a_faceX * a_pushY - a_faceY * a_pushX;  // positive: pushed to his left
        return side > 0.0f ? kSpinLeft : kSpinRight;
    }

    struct Vec
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // The launch, in metres a second, away from a blow at a_from for CJ at a_at (flat); straight back along a_faceX,
    // a_faceY when the two are on top of each other.
    inline Vec Launch(Vec a_at, Vec a_from, float a_faceX, float a_faceY, float a_speed = kSpeed)
    {
        float dx = a_at.x - a_from.x;
        float dy = a_at.y - a_from.y;
        float length = std::hypot(dx, dy);
        if (length < 0.01f || !std::isfinite(length)) {
            dx = -a_faceX;
            dy = -a_faceY;
            length = std::hypot(dx, dy);
        }
        if (length < 0.01f || !std::isfinite(length)) {
            return { 0.0f, 0.0f, a_speed * kRise };
        }
        return { dx / length * a_speed, dy / length * a_speed, a_speed * kRise };
    }

    // ---- How long Skyrim's player may stay down ----

    // Left alone, Skyrim's player may never get up (its knock state stuck, its ragdoll never settling) and the pursuers
    // keep swinging at where it lies while CJ stands elsewhere. So it stays down no longer than CJ does in San Andreas
    // (his launch and fall, then his get-up), then it is made to get up and goes back to CJ whether it did or not; the
    // fix is tried again now and then while Skyrim still says it's down, and that isn't a new blow.
    inline constexpr float kFallSeconds = 1.5f;   // CJ's launch and fall
    inline constexpr float kGetUpSeconds = 1.5f;  // and his get-up (KO_skid_front's, KO_skid_back's)
    inline constexpr float kDownMost = kFallSeconds + kGetUpSeconds;
    inline constexpr float kFixEvery = 0.5f;  // seconds between tries to make it get up
    inline constexpr int   kFixTries = 4;     // and no more than this many

    enum class Step
    {
        None,   // nothing new
        Fell,   // knocked down just now: San Andreas throws CJ, the player is left where it falls
        Force,  // down too long: make it get up (again), it goes back to CJ
        Up,     // up again (on its own or made to): back at CJ, the pursuers onto him
    };

    class Down
    {
    public:
        // A frame of a_delta seconds; a_knocked whether Skyrim says the player is down (its knock state, its ragdoll).
        Step Tick(float a_delta, bool a_knocked)
        {
            switch (_phase) {
            case Phase::Standing:
                if (!a_knocked) {
                    return Step::None;
                }
                _phase = Phase::Falling;
                _time = 0.0f;
                return Step::Fell;
            case Phase::Falling:
                if (!a_knocked) {
                    _phase = Phase::Standing;
                    return Step::Up;
                }
                _time += a_delta;
                if (_time < kDownMost) {
                    return Step::None;
                }
                _phase = Phase::Forced;
                _fixTimer = 0.0f;
                _tries = 1;
                return Step::Force;
            case Phase::Forced:
                if (!a_knocked) {
                    _phase = Phase::Standing;
                    return Step::Up;
                }
                _time += a_delta;
                _fixTimer += a_delta;
                if (_fixTimer >= kFixEvery && _tries < kFixTries) {
                    _fixTimer = 0.0f;
                    _tries++;
                    return Step::Force;
                }
                return Step::None;
            }
            return Step::None;
        }

        // Whether the player is left where it fell this frame (else it stands where CJ is).
        bool Unpinned() const { return _phase == Phase::Falling; }
        bool IsDown() const { return _phase != Phase::Standing; }
        float Seconds() const { return _phase == Phase::Standing ? 0.0f : _time; }
        int   Tries() const { return _phase == Phase::Forced ? _tries : 0; }

        void Reset() { _phase = Phase::Standing; }

    private:
        enum class Phase
        {
            Standing,
            Falling,  // down, left where it fell
            Forced,   // down too long: made to get up, back at CJ
        };

        Phase _phase = Phase::Standing;
        float _time = 0.0f;
        float _fixTimer = 0.0f;
        int   _tries = 0;
    };

    // ---- The pursuers after a knock-down ----

    // Whoever was fighting the player as it went down fights it again once it's back at CJ. Their fights are never
    // stopped for it: stopping and starting one in the same frame left Skyrim's guards out of it (stopping is what calls
    // them off), and the pursuers lost interest in CJ. And the AI may drop a target it lost sight of while it lay out of
    // sight, a while after. So for kRejoinSeconds after the player is up, every kRejoinEvery anyone of them who is in no
    // fight any more is set on it again.
    inline constexpr float kRejoinSeconds = 8.0f;
    inline constexpr float kRejoinEvery = 1.0f;

    // Whether one who fought the player as it went down is set on it again: alive, about, and in no fight at all now. One
    // fighting someone else is the leash's (Wanted::Leash), and one fighting the player is left alone: a fight started
    // over and over throws away the path being built for it.
    constexpr bool Rejoins(bool a_alive, bool a_loaded, bool a_inAFight) { return a_alive && a_loaded && !a_inAFight; }

    class Rejoin
    {
    public:
        // The player is back at CJ: a check now, then every kRejoinEvery for kRejoinSeconds.
        void Start()
        {
            _left = kRejoinSeconds;
            _timer = kRejoinEvery;
        }

        // A frame of a_delta seconds. Returns whether to check now.
        bool Tick(float a_delta)
        {
            if (_left <= 0.0f) {
                return false;
            }
            _left -= a_delta;
            _timer += a_delta;
            if (_timer >= kRejoinEvery) {
                _timer = 0.0f;
                return true;
            }
            return false;
        }

        bool Active() const { return _left > 0.0f; }
        void Reset() { _left = 0.0f; }

    private:
        float _left = 0.0f;
        float _timer = 0.0f;
    };
}
