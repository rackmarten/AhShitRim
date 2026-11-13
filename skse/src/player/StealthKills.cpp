#include "player/StealthKills.h"

#include "player/Shots.h"
#include "weapons.h"

namespace AhShitRim::StealthKills
{
    namespace
    {
        constexpr float kKnockBehind = 60.0f;  // Skyrim units behind the victim's middle the slump's knock comes from

        struct State
        {
            RE::FormID held = 0;  // the victim grabbed, waiting for the stab
            float      heldFor = 0.0f;
            int        kills = 0;
            Debug      debug;  // the test kit's view (DebugState)
        } state;

        RE::Actor* ActorOf(RE::FormID a_id)
        {
            auto* actor = a_id ? RE::TESForm::LookupByID<RE::Actor>(a_id) : nullptr;
            return actor && actor->Is3DLoaded() && !actor->IsDisabled() ? actor : nullptr;
        }

        RE::NiPoint3 Middle(RE::Actor* a_actor)
        {
            return a_actor->GetPosition() + RE::NiPoint3(0.0f, 0.0f, 0.5f * a_actor->GetHeight());
        }

        // The way from the player (where CJ stands) to a_actor, flat; the actor's own facing when they're on top of each other.
        RE::NiPoint3 Toward(RE::PlayerCharacter* a_player, RE::Actor* a_actor)
        {
            RE::NiPoint3 dir = a_actor->GetPosition() - a_player->GetPosition();
            dir.z = 0.0f;
            if (dir.Unitize() < 1e-3f) {
                const float heading = a_actor->GetAngleZ();
                dir = RE::NiPoint3(std::sin(heading), std::cos(heading), 0.0f);
            }
            return dir;
        }

        void Grab(RE::PlayerCharacter* a_player, RE::Actor* a_actor)
        {
            const auto feet = a_player->GetPosition();
            const auto verdict = StealthKill::CheckStab({ feet.x, feet.y, feet.z }, Weapons::Knife, TargetOf(a_actor, a_player));
            if (verdict != StealthKill::Verdict::Yes) {
                SKSE::log::info("stealth kill: CJ grabs {} ({:08X}), who is {}: not held", a_actor->GetName(), a_actor->GetFormID(), StealthKill::Why(verdict));
                return;
            }
            state.held = a_actor->GetFormID();
            state.heldFor = 0.0f;
            state.debug.grabs++;
            state.debug.victim = state.held;
            state.debug.grabbedAt = a_actor->GetPosition();
            state.debug.died = false;
            // Skyrim's own stagger, from behind: San Andreas's victim struggles in KILL_KNIFE_PED_DAMAGE.
            const auto  at = a_actor->GetPosition();
            const float heading = std::atan2(feet.x - at.x, feet.y - at.y);
            a_actor->SetGraphVariableFloat("staggerDirection", Damage::StaggerDirection(a_actor->GetAngleZ(), heading));
            a_actor->SetGraphVariableFloat("staggerMagnitude", 0.25f);
            a_actor->NotifyAnimationGraph("staggerStart");
            SKSE::log::info("stealth kill: CJ grabs {} ({:08X}) {:.2f} m off", a_actor->GetName(), a_actor->GetFormID(),
                feet.GetDistance(at) * Pose::kMetersPerUnit);
        }

        void Stab(RE::PlayerCharacter* a_player, RE::Actor* a_actor, std::uint8_t a_weapon)
        {
            state.held = 0;
            const auto feet = a_player->GetPosition();
            const auto verdict = StealthKill::CheckStab({ feet.x, feet.y, feet.z }, a_weapon, TargetOf(a_actor, a_player));
            if (verdict != StealthKill::Verdict::Yes) {
                SKSE::log::info("stealth kill: CJ's stab misses {} ({:08X}), who is {}", a_actor->GetName(), a_actor->GetFormID(), StealthKill::Why(verdict));
                return;
            }
            const auto dir = Toward(a_player, a_actor);
            Shots::Kill(a_player, a_actor, a_weapon, Middle(a_actor) - dir * 10.0f, dir);
            const bool dead = a_actor->IsDead() || a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0.0f;
            if (dead) {
                if (auto* process = a_actor->GetActorRuntimeData().currentProcess) {
                    process->KnockExplosion(a_actor, Middle(a_actor) - dir * kKnockBehind, StealthKill::kSlumpKnock);
                }
            }
            state.kills++;
            state.debug.stabs++;
            state.debug.died = dead;
            SKSE::log::info("stealth kill #{}: CJ stabs {} ({:08X}){}", state.kills, a_actor->GetName(), a_actor->GetFormID(),
                dead ? ", who dies" : ", who lives (Skyrim's hit didn't kill)");
        }
    }

    std::string OffItsFeet(RE::Actor* a_actor)
    {
        if (a_actor->IsInRagdollState()) {
            return "ragdolled";
        }
        if (a_actor->IsInKillMove()) {
            return "in a kill move";
        }
        auto* actorState = a_actor->AsActorState();
        if (!actorState) {
            return {};
        }
        if (const auto knock = actorState->GetKnockState(); knock != RE::KNOCK_STATE_ENUM::kNormal) {
            return std::format("knocked down (knock state {})", static_cast<int>(knock));
        }
        if (actorState->IsBleedingOut()) {
            return "bleeding out";
        }
        if (const auto sit = actorState->GetSitSleepState(); !StealthKill::Upright(static_cast<std::uint32_t>(sit))) {
            return std::format("lying down to sleep (sit state {})", static_cast<int>(sit));
        }
        return {};
    }

    StealthKill::Target TargetOf(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
    {
        StealthKill::Target t;
        auto*               race = a_actor->GetRace();
        const char*         id = race ? race->GetFormEditorID() : nullptr;
        t.person = race && StealthKill::IsPerson(id ? id : "", race->HasKeywordString("ActorTypeNPC"), race->HasKeywordString("ActorTypeDragon"));
        t.child = a_actor->IsChild();
        t.essential = a_actor->IsEssential();
        t.dead = a_actor->IsDead();
        t.mounted = a_actor->IsOnMount();
        auto* actorState = a_actor->AsActorState();
        t.standing = OffItsFeet(a_actor).empty();
        t.seated = actorState && StealthKill::Seated(static_cast<std::uint32_t>(actorState->GetSitSleepState()));
        t.sneaking = a_actor->IsSneaking();
        t.running = actorState && (actorState->IsRunning() || actorState->IsSprinting());
        t.fighting = a_actor->GetActorRuntimeData().currentCombatTarget.get().get() == a_player || (a_actor->IsInCombat() && a_actor->IsHostileToActor(a_player));
        t.detects = a_actor->RequestDetectionLevel(a_player) > 0;
        const auto feet = a_actor->GetPosition();
        t.feet = { feet.x, feet.y, feet.z };
        t.heading = a_actor->GetAngleZ();
        return t;
    }

    void OnShot(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot)
    {
        auto* actor = ActorOf(a_shot.target);
        if (!actor || actor == a_player) {
            SKSE::log::info("stealth kill: CJ's {} names {:08X}, who isn't here", a_shot.kind == Protocol::kShotStealthStab ? "stab" : "grab", a_shot.target);
            if (a_shot.kind == Protocol::kShotStealthStab) {
                state.held = 0;
            }
            return;
        }
        if (a_shot.kind == Protocol::kShotStealthGrab) {
            Grab(a_player, actor);
        } else {
            Stab(a_player, actor, a_shot.weaponType);
        }
    }

    void Update(RE::PlayerCharacter*, float a_seconds)
    {
        if (!state.held) {
            return;
        }
        state.heldFor += std::isfinite(a_seconds) && a_seconds > 0.0f ? a_seconds : 0.0f;
        auto* actor = ActorOf(state.held);
        if (!actor || actor->IsDead() || state.heldFor > StealthKill::kGrabMax) {
            if (actor && !actor->IsDead()) {
                SKSE::log::info("stealth kill: no stab came for {} ({:08X}), let go", actor->GetName(), actor->GetFormID());
            }
            state.held = 0;
            return;
        }
        actor->StopMoving(a_seconds);
    }

    Debug DebugState()
    {
        auto out = state.debug;
        out.held = state.held;
        return out;
    }

    void Clear() { state.held = 0; }
}
