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
        };

        struct State
        {
            Melee::Lock lock;
            Mode        mode = Mode::None;
            RE::FormID  logged = 0;
            RE::FormID  marked = 0;  // the gun's indicator
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

        // San Andreas's triangle over its head, coloured by its health.
        void Mark(RE::Actor* a_actor)
        {
            auto*       values = a_actor->AsActorValueOwner();
            const float full = values->GetPermanentActorValue(RE::ActorValue::kHealth);
            const float health = full > 0.0f ? values->GetActorValue(RE::ActorValue::kHealth) / full : 1.0f;
            SaHud::MarkHead(a_actor->GetPosition() + RE::NiPoint3(0.0f, 0.0f, a_actor->GetHeight()), Melee::MarkerColour(health));
        }

        // Who could be locked on to or marked: alive, loaded, not the player's followers, children or riders.
        void Gather(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cj, std::vector<RE::Actor*>& a_actors, std::vector<Melee::Candidate>& a_candidates)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            for (auto& handle : lists->highActorHandles) {
                auto  ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == a_player || actor->IsDead() || !actor->Is3DLoaded() || actor->IsChild() || actor->IsOnMount() ||
                    actor->IsPlayerTeammate() || actor->IsCommandedActor()) {
                    continue;
                }
                const auto offset = (Middle(actor) - a_cj) * Pose::kMetersPerUnit;
                a_actors.push_back(actor);
                a_candidates.push_back({ actor->GetFormID(), { offset.x, offset.y, offset.z }, Hostile(actor, a_player) });
            }
        }

        // While a gun is aimed: the enemy nearest the crosshair, in sight, marked. Nothing else happens with it.
        void Indicate(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cj)
        {
            auto* camera = RE::Main::WorldRootCamera();
            float x = 0.0f, y = 0.0f, aspect = 0.0f;
            if (!camera || !SaHud::AimCrosshair(x, y, aspect)) {
                state.marked = 0;
                return;
            }
            std::vector<RE::Actor*>       actors;
            std::vector<Melee::Candidate> candidates;
            Gather(a_player, a_cj, actors, candidates);
            std::vector<RE::Actor*>      enemies;
            std::vector<Melee::OnScreen> shown;
            for (std::size_t i = 0; i < actors.size(); i++) {
                if (!candidates[i].hostile) {
                    continue;
                }
                Melee::OnScreen on;
                float           depth = 0.0f;
                on.visible = camera->WorldPtToScreenPt3(Middle(actors[i]), on.x, on.y, depth, 1e-5f);
                on.y = 1.0f - on.y;  // Skyrim's screen points go up
                on.distance = RunOver::Length(candidates[i].offset);
                enemies.push_back(actors[i]);
                shown.push_back(on);
            }
            RE::Actor* marked = nullptr;
            for (const auto i : Melee::NearCrosshair(shown, x, y, aspect)) {
                bool unused = false;
                if (enemies[i]->GetFormID() == state.marked || a_player->HasLineOfSight(enemies[i], unused)) {
                    marked = enemies[i];
                    break;
                }
            }
            state.marked = marked ? marked->GetFormID() : 0;
            if (marked) {
                Mark(marked);
            }
        }
    }

    RE::FormID Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cjFeet, const Aim& a_aim, float a_seconds)
    {
        const RE::NiPoint3 cj = a_cjFeet + RE::NiPoint3(0.0f, 0.0f, kCjMiddle);
        if (a_aim.gun && a_aim.aim) {
            Indicate(a_player, cj);
        } else {
            state.marked = 0;
        }

        const Mode wanted = a_aim.melee && a_aim.aim ? Mode::MeleeLock : a_aim.melee && a_aim.fire ? Mode::Swing : Mode::None;
        if (wanted != Mode::None && wanted != state.mode) {
            state.lock.Clear();
            state.mode = wanted;
        } else if (wanted == Mode::None && (state.mode != Mode::Swing || !a_aim.melee)) {
            state.lock.Clear();  // a swing's target outlasts the button for the combo; the lock ends with it
            state.mode = Mode::None;
        }
        if (state.mode == Mode::None) {
            state.logged = 0;
            return 0;
        }
        std::vector<RE::Actor*>       actors;
        std::vector<Melee::Candidate> candidates;
        Gather(a_player, cj, actors, candidates);
        const auto& rules = state.mode == Mode::Swing ? Melee::kSwing : Melee::kMeleeLock;
        const auto  target = state.lock.Update(wanted != Mode::None, candidates, a_aim.heading, [&](std::size_t i) {
            bool unused = false;
            return a_player->HasLineOfSight(actors[i], unused);
        }, rules, a_seconds);
        RE::Actor* locked = nullptr;
        for (std::size_t i = 0; i < actors.size(); i++) {
            if (candidates[i].id == target) {
                locked = actors[i];
            }
        }
        if (locked && state.mode == Mode::MeleeLock) {
            Mark(locked);
        }
        if (target != state.logged && locked) {
            SKSE::log::info("lock-on: CJ {} {} ({:08X}){}", state.mode == Mode::Swing ? "swings at" : "locks on to", locked->GetDisplayFullName(), target,
                Hostile(locked, a_player) ? ", who fights him" : "");
        }
        state.logged = target;
        if (state.mode == Mode::Swing && wanted == Mode::None && !target) {
            state.mode = Mode::None;  // the combo is over
        }
        return target;
    }

    void Clear()
    {
        state.lock.Clear();
        state.mode = Mode::None;
        state.marked = 0;
    }
}
