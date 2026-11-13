#include "debug/StreamTest.h"

#include "player/PlayerSync.h"
#include "pose.h"

#include <chrono>
#include <deque>

namespace AhShitRim::StreamTest
{
    void Second::Add(const Second& a_other)
    {
        frames += a_other.frames;
        worst = (std::max)(worst, a_other.worst);
        over50 += a_other.over50;
        over100 += a_other.over100;
        samples += a_other.samples;
        underAttached += a_other.underAttached;
        underLand += a_other.underLand;
        ahead1Attached += a_other.ahead1Attached;
        ahead1Land += a_other.ahead1Land;
        ahead2Attached += a_other.ahead2Attached;
        ahead2Land += a_other.ahead2Land;
    }

    namespace
    {
        struct State
        {
            bool                  flying = false;
            float                 speed = 0.0f;    // metres a second
            float                 heading = 0.0f;  // radians from north, clockwise
            float                 seconds = 60.0f;
            float                 time = 0.0f;
            RE::NiPoint3          from;
            Second                second, total;
            float                 secondTime = 0.0f;
            std::deque<Second>    done;
            std::optional<Second> result;
            std::chrono::steady_clock::time_point polled;
        } state;

        void End(const char* a_why)
        {
            if (state.flying) {
                SKSE::log::info("stream test: stopped, {}", a_why);
            }
            state.flying = false;
        }

        // Whether the land at a_at is in an attached cell (Havok has it, so the sampler can read it), and has a height.
        void Look(const RE::NiPoint3& a_at, int& a_attached, int& a_land)
        {
            auto* tes = RE::TES::GetSingleton();
            auto* cell = tes ? tes->GetCell(a_at) : nullptr;
            a_attached += cell && cell->IsAttached();
            float z = 0.0f;
            a_land += tes && tes->GetLandHeight(a_at, z);
        }

        int Percent(int a_part, int a_whole) { return a_whole ? a_part * 100 / a_whole : 0; }

        void Log(const char* a_what, float a_at, float a_window, const Second& a_s)
        {
            SKSE::log::info(
                "stream test: {} {:.0f} s, {:.0f} m: {} frames ({:.0f} a second), worst {:.0f} ms, {} over 50 ms, {} over 100 ms; land under the player "
                "attached {}% / height {}%, 1 s ahead {}% / {}%, 2 s ahead {}% / {}%",
                a_what, a_at, a_at * state.speed, a_s.frames, a_s.frames / (std::max)(a_window, 0.001f), a_s.worst * 1000.0f, a_s.over50, a_s.over100,
                Percent(a_s.underAttached, a_s.samples), Percent(a_s.underLand, a_s.samples), Percent(a_s.ahead1Attached, a_s.samples),
                Percent(a_s.ahead1Land, a_s.samples), Percent(a_s.ahead2Attached, a_s.samples), Percent(a_s.ahead2Land, a_s.samples));
        }
    }

    std::string Begin(float a_speed, float a_headingDegrees, float a_heightMetres, float a_seconds)
    {
        if (!(a_speed > 0.0f && a_speed <= 1000.0f) || !(a_seconds > 0.0f && a_seconds <= 3600.0f) || !(a_heightMetres >= 0.0f && a_heightMetres <= 2000.0f)) {
            return "speed is 0 to 1000 m/s, seconds 0 to 3600, height 0 to 2000 m";
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        if (!cell || !player->Is3DLoaded()) {
            return "the player isn't in the world";
        }
        if (cell->IsInteriorCell()) {
            return "the player is indoors (coc to an outdoor cell first)";
        }
        if (PlayerSync::DebugOwned()) {
            return "San Andreas has the player (asr.cj on=false first)";
        }
        state = State{};
        state.speed = a_speed;
        state.heading = a_headingDegrees * 0.0174532925f;
        state.seconds = a_seconds;
        state.from = player->GetPosition() + RE::NiPoint3(0.0f, 0.0f, a_heightMetres * Pose::kUnitsPerMeter);
        state.flying = true;
        state.polled = std::chrono::steady_clock::now();
        SKSE::log::info("stream test: flying from ({:.0f},{:.0f},{:.0f}) in {:08X} at {} m/s, heading {:.0f}, {} m up, for {} s", state.from.x, state.from.y,
            state.from.z, cell->GetFormID(), a_speed, a_headingDegrees, a_heightMetres, a_seconds);
        return {};
    }

    void Stop() { End("asked to"); }

    void OnPostLoadGame() { End("a game was loaded"); }

    // Every frame (PlayerSync's frame.busy), whoever has the player: a flight nobody watches, or one San Andreas took the
    // player from, ends here (Update only runs while Skyrim has the player).
    bool Running()
    {
        if (state.flying && PlayerSync::DebugOwned()) {
            End("San Andreas took the player");
        } else if (state.flying && std::chrono::steady_clock::now() - state.polled > std::chrono::seconds(10)) {
            End("nobody asked for its seconds (the request went away)");
        }
        return state.flying;
    }

    std::optional<Second> NextSecond()
    {
        state.polled = std::chrono::steady_clock::now();
        if (state.done.empty()) {
            return std::nullopt;
        }
        auto s = state.done.front();
        state.done.pop_front();
        return s;
    }

    std::optional<Second> Total() { return state.result; }

    void Update(RE::PlayerCharacter* a_player, float a_delta)
    {
        if (!state.flying || a_delta <= 0.0f) {
            return;
        }
        state.time += a_delta;
        const RE::NiPoint3 dir(std::sin(state.heading), std::cos(state.heading), 0.0f);
        const float        units = state.speed * Pose::kUnitsPerMeter;
        const RE::NiPoint3 at = state.from + dir * (units * state.time);
        a_player->SetPosition(at, true);
        if (auto* controller = a_player->GetCharController()) {
            controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f));
            controller->fallStartHeight = at.z;
            controller->fallTime = 0.0f;
        }
        auto& s = state.second;
        s.frames++;
        s.worst = (std::max)(s.worst, a_delta);
        s.over50 += a_delta > 0.05f;
        s.over100 += a_delta > 0.1f;
        s.samples++;
        Look(at, s.underAttached, s.underLand);
        Look(at + dir * units, s.ahead1Attached, s.ahead1Land);
        Look(at + dir * (2.0f * units), s.ahead2Attached, s.ahead2Land);
        state.secondTime += a_delta;
        if (state.secondTime >= 1.0f) {
            Log("at", state.time, state.secondTime, s);
            s.at = state.time;
            s.window = state.secondTime;
            state.done.push_back(s);
            state.total.Add(s);
            s = {};
            state.secondTime = 0.0f;
        }
        if (state.time >= state.seconds) {
            if (s.frames) {
                s.at = state.time;
                s.window = state.secondTime;
                state.done.push_back(s);
            }
            state.total.Add(s);
            state.total.at = state.total.window = state.time;
            Log("in all", state.time, state.time, state.total);
            state.result = state.total;
            state.flying = false;
        }
    }
}
