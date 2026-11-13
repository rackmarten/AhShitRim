#include "debug/HitTest.h"

#include "link/LinkService.h"
#include "player/Police.h"
#include "player/Vitals.h"
#include "wanted.h"

#include <chrono>
#include <deque>

namespace AhShitRim::HitTest
{
    namespace
    {
        constexpr float kAhead = 840.0f;  // 12 m: where the attackers are placed
        constexpr auto  kOrphaned = std::chrono::seconds(10);  // no one has asked for the samples this long: stop

        struct State
        {
            bool                             running = false;
            bool                             bust = false;
            float                            seconds = 90.0f;
            float                            timer = 0.0f;
            float                            logTimer = 0.0f;
            float                            combatTimer = 0.0f;
            std::vector<RE::ObjectRefHandle> attackers;
            std::deque<Sample>               samples;
            Result                           result;
            std::chrono::steady_clock::time_point polled;
        } state;

        bool InVehicle()
        {
            Protocol::PlayerState pose{};
            return LinkService::Get().ReadPlayer(pose) && (pose.flags & Protocol::kPlayerValid) && (pose.flags & Protocol::kPlayerInVehicle);
        }

        float CarHealth()
        {
            for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
                Protocol::BodyState vehicle{};
                if (LinkService::Get().ReadVehicle(i, vehicle) && (vehicle.flags & Protocol::kBodyValid) && (vehicle.flags & Protocol::kBodyDriven)) {
                    return vehicle.health;
                }
            }
            return -1.0f;
        }

        // A giant and two guards 12 m ahead, a few metres apart, set on the player.
        void PlaceAttackers(RE::PlayerCharacter* a_player)
        {
            const std::uint32_t kinds[] = { Wanted::kGiant, Wanted::kHolds[0].guard, Wanted::kHolds[0].guard };
            const float         heading = a_player->GetAngleZ();
            for (int i = 0; i < 3; i++) {
                auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kinds[i]);
                auto  ref = base ? a_player->PlaceObjectAtMe(base, false) : nullptr;
                if (!ref) {
                    SKSE::log::warn("hit test: couldn't place {:08X}", kinds[i]);
                    continue;
                }
                const float side = (i - 1) * 280.0f;
                const auto  at = a_player->GetPosition() + RE::NiPoint3(std::sin(heading) * kAhead + std::cos(heading) * side,
                                                                std::cos(heading) * kAhead - std::sin(heading) * side, 0.0f);
                ref->SetPosition(at);
                state.attackers.push_back(ref->GetHandle());
                SKSE::log::info("hit test: placed {} ({:08X}) {:.0f} units ahead", ref->GetName(), ref->GetFormID(), kAhead);
            }
            state.result.attackers = static_cast<int>(state.attackers.size());
        }

        void SetOn(RE::PlayerCharacter* a_player)
        {
            for (const auto& handle : state.attackers) {
                auto  ref = handle.get();
                auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                if (actor && !actor->IsDead() && actor->Is3DLoaded() && actor->GetActorRuntimeData().currentCombatTarget.get().get() != a_player) {
                    actor->StartCombat(a_player);
                }
            }
        }

        void TakeSample(RE::PlayerCharacter* a_player)
        {
            Sample sample{ .seconds = state.timer, .carHealth = CarHealth(), .playerHealth = a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth),
                .inCar = InVehicle(), .wasted = Vitals::IsWasted(), .busted = Police::IsBusted() };
            auto& r = state.result;
            if (sample.carHealth >= 0.0f) {
                r.carMin = r.carMin < 0.0f ? sample.carHealth : (std::min)(r.carMin, sample.carHealth);
            }
            r.carEnd = sample.carHealth;
            r.playerMin = (std::min)(r.playerMin, sample.playerHealth);
            r.inCarAtEnd = sample.inCar;
            r.wasted = r.wasted || sample.wasted;
            r.busted = r.busted || sample.busted;
            r.seconds = state.timer;
            SKSE::log::info("hit test: {:.0f} s: car health {:.0f}, player health {:.0f}{}{}{}", sample.seconds, sample.carHealth, sample.playerHealth,
                sample.inCar ? ", CJ in the car" : ", CJ on foot", sample.wasted ? ", wasted" : "", sample.busted ? ", busted" : "");
            state.samples.push_back(sample);
        }
    }

    std::string Begin(bool a_bust, float a_seconds)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded() || !player->GetParentCell()) {
            return "the player isn't in the world";
        }
        if (!InVehicle()) {
            return "CJ isn't in a vehicle (asr.spawn 495 in=true puts him in a Sandking)";
        }
        Stop();
        state = State{};
        state.running = true;
        state.polled = std::chrono::steady_clock::now();
        state.bust = a_bust;
        state.seconds = a_seconds;
        state.result.carStart = state.result.carMin = state.result.carEnd = CarHealth();
        state.result.playerMin = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
        state.result.inCarAtEnd = true;
        SKSE::log::info("hit test: CJ is in his vehicle (health {:.0f}), the {} begin, for {:.0f} s", state.result.carStart, a_bust ? "stars" : "attackers",
            a_seconds);
        if (a_bust) {
            Police::CheatStars(1);
        } else {
            PlaceAttackers(player);
        }
        return {};
    }

    void Stop()
    {
        // Out of the fight and disabled; never deleted here: an actor deleted while its movement still runs on a job
        // thread crashes the game. Placed references are temporary and go with their cell.
        for (const auto& handle : state.attackers) {
            if (auto ref = handle.get()) {
                if (auto* actor = ref->As<RE::Actor>()) {
                    actor->StopCombat();
                }
                ref->Disable();
            }
        }
        if (state.running || !state.attackers.empty()) {
            SKSE::log::info("hit test: over, {} attackers taken away{}", state.attackers.size(), state.bust ? ", stars cleared" : "");
        }
        if (state.bust && state.running) {
            Police::CheatStars(0);
        }
        state.attackers.clear();
        state.running = false;
    }

    bool Running() { return state.running; }

    void OnPostLoadGame()
    {
        if (state.running) {
            state.result.stopped = "a game was loaded halfway (the player died, and Skyrim loaded the most recent save?)";
            SKSE::log::warn("hit test: {}", state.result.stopped);
        }
        // The attackers and the stars were the game that's gone: nothing of it to touch.
        state.attackers.clear();
        state.running = false;
    }

    std::optional<Sample> NextSample()
    {
        state.polled = std::chrono::steady_clock::now();
        if (state.samples.empty()) {
            return std::nullopt;
        }
        auto sample = state.samples.front();
        state.samples.pop_front();
        return sample;
    }

    const Result& Last() { return state.result; }

    void Update(RE::PlayerCharacter* a_player, float a_delta)
    {
        if (!state.running || a_delta <= 0.0f) {
            return;
        }
        if (std::chrono::steady_clock::now() - state.polled > kOrphaned) {
            state.result.stopped = "nobody asked for its samples (the request went away)";
            SKSE::log::info("hit test: {}", state.result.stopped);
            Stop();
            return;
        }
        auto* ui = RE::UI::GetSingleton();
        if ((ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) || !a_player->Is3DLoaded() || !a_player->GetParentCell()) {
            return;
        }
        state.timer += a_delta;
        state.combatTimer += a_delta;
        if (!state.bust && state.combatTimer >= 1.5f) {
            state.combatTimer = 0.0f;
            SetOn(a_player);
        }
        state.logTimer += a_delta;
        if (state.logTimer >= 2.0f) {
            state.logTimer = 0.0f;
            TakeSample(a_player);
        }
        if (state.timer >= state.seconds) {
            TakeSample(a_player);
            SKSE::log::info("hit test: done after {:.0f} s", state.timer);
            state.result.done = true;
            Stop();
        }
    }
}
