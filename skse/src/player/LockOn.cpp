#include "player/LockOn.h"

#include "hud/SaHud.h"
#include "melee.h"

#include <vector>

namespace AhShitRim::LockOn
{
    namespace
    {
        constexpr float kCjMiddle = 0.9f * RunOver::kMeter;  // Skyrim units over his feet San Andreas measures from

        struct State
        {
            Melee::Lock lock;
        } state;

        RE::NiPoint3 Middle(RE::Actor* a_actor)
        {
            const auto feet = a_actor->GetPosition();
            return { feet.x, feet.y, feet.z + 0.5f * a_actor->GetHeight() };
        }

        bool Hostile(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
        {
            return a_actor->IsHostileToActor(a_player) || a_actor->GetActorRuntimeData().currentCombatTarget.get().get() == a_player;
        }
    }

    RE::FormID Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cjFeet, float a_heading, bool a_held)
    {
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!a_held || !lists) {
            state.lock.Clear();
            return 0;
        }
        const RE::NiPoint3            cj = a_cjFeet + RE::NiPoint3(0.0f, 0.0f, kCjMiddle);
        std::vector<RE::Actor*>       actors;
        std::vector<Melee::Candidate> candidates;
        for (auto& handle : lists->highActorHandles) {
            auto  ptr = handle.get();
            auto* actor = ptr.get();
            if (!actor || actor == a_player || actor->IsDead() || !actor->Is3DLoaded() || actor->IsChild() || actor->IsOnMount() ||
                actor->IsPlayerTeammate() || actor->IsCommandedActor()) {
                continue;
            }
            const auto offset = (Middle(actor) - cj) * Pose::kMetersPerUnit;
            actors.push_back(actor);
            candidates.push_back({ actor->GetFormID(), { offset.x, offset.y, offset.z }, Hostile(actor, a_player) });
        }
        const auto before = state.lock.Target();
        const auto target = state.lock.Update(true, candidates, a_heading, [&](std::size_t i) {
            bool unused = false;
            return a_player->HasLineOfSight(actors[i], unused);
        });
        RE::Actor* locked = nullptr;
        for (std::size_t i = 0; i < actors.size(); i++) {
            if (candidates[i].id == target) {
                locked = actors[i];
            }
        }
        if (locked) {
            SaHud::MarkTarget(Middle(locked), true);
        }
        if (target != before && locked) {
            SKSE::log::info("lock-on: CJ locks on to {} ({:08X}){}", locked->GetDisplayFullName(), target, Hostile(locked, a_player) ? ", who fights him" : "");
        }
        return target;
    }

    void Clear() { state.lock.Clear(); }
}
