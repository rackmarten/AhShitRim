#include "player/LockOn.h"

#include "hud/SaHud.h"
#include "melee.h"

#include <vector>

namespace AhShitRim::LockOn
{
    namespace
    {
        constexpr float kCjMiddle = 0.9f * RunOver::kMeter;  // Skyrim units over his feet San Andreas measures from

        enum class Mode : std::uint8_t
        {
            None,
            MeleeLock,  // the right button with a melee weapon
            Swing,      // a blow with nobody locked on to
            Gun,        // the right button with a gun
        };

        struct State
        {
            Melee::Lock lock;
            Mode        mode = Mode::None;
            RE::FormID  logged = 0;
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

        float HealthOf(RE::Actor* a_actor)
        {
            auto*       values = a_actor->AsActorValueOwner();
            const float full = values->GetPermanentActorValue(RE::ActorValue::kHealth);
            return full > 0.0f ? values->GetActorValue(RE::ActorValue::kHealth) / full : 1.0f;
        }

        const Melee::Rules& RulesOf(Mode a_mode)
        {
            switch (a_mode) {
            case Mode::Swing: return Melee::kSwing;
            case Mode::Gun: return Melee::kGunLock;
            default: return Melee::kMeleeLock;
            }
        }

        const char* NameOf(Mode a_mode)
        {
            switch (a_mode) {
            case Mode::MeleeLock: return "locks on to";
            case Mode::Swing: return "swings at";
            case Mode::Gun: return "aims at";
            default: return "-";
            }
        }
    }

    RE::FormID Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cjFeet, const Aim& a_aim, float a_seconds)
    {
        const Mode wanted = a_aim.melee && a_aim.aim ? Mode::MeleeLock : a_aim.melee && a_aim.fire ? Mode::Swing : a_aim.gun && a_aim.aim ? Mode::Gun : Mode::None;
        if (wanted != Mode::None && wanted != state.mode) {
            state.lock.Clear();
            state.mode = wanted;
        } else if (wanted == Mode::None && (state.mode != Mode::Swing || !a_aim.melee)) {
            state.lock.Clear();  // a swing's target outlasts the button for the combo; the locks end with it
            state.mode = Mode::None;
        }
        auto* lists = RE::ProcessLists::GetSingleton();
        if (state.mode == Mode::None || !lists) {
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
        const float heading = state.mode == Mode::Gun ? a_aim.view : a_aim.heading;
        const auto  target = state.lock.Update(wanted != Mode::None, candidates, heading, [&](std::size_t i) {
            bool unused = false;
            return a_player->HasLineOfSight(actors[i], unused);
        }, RulesOf(state.mode), a_seconds);
        RE::Actor* locked = nullptr;
        for (std::size_t i = 0; i < actors.size(); i++) {
            if (candidates[i].id == target) {
                locked = actors[i];
            }
        }
        if (locked && state.mode != Mode::Swing) {
            SaHud::MarkTarget(Middle(locked), Melee::MarkerColour(HealthOf(locked)));
        }
        if (target != state.logged && locked) {
            SKSE::log::info("lock-on: CJ {} {} ({:08X}){}", NameOf(state.mode), locked->GetDisplayFullName(), target, Hostile(locked, a_player) ? ", who fights him" : "");
        }
        state.logged = target;
        if (state.mode == Mode::Swing && wanted == Mode::None && !target) {
            state.mode = Mode::None;  // the combo is over
        }
        return state.mode == Mode::Gun ? 0 : target;
    }

    RE::FormID GunTarget() { return state.mode == Mode::Gun ? state.lock.Target() : 0; }

    void Clear()
    {
        state.lock.Clear();
        state.mode = Mode::None;
    }
}
