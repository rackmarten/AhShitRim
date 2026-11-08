#include "player/StealthKills.h"

#include "Ini.h"
#include "player/Chatter.h"
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
            bool       killMoves = true;   // [Weapons] StealthKillMove: the victim plays Skyrim's own sneak kill move
            RE::FormID moving = 0;         // the victim in one now
            float      movingFor = 0.0f;
            bool       confirmed = false;  // Skyrim shows it in the kill move
            bool       loggedIdles = false;
            bool       owed = false;       // stabbed in the kill move but still alive: killed when it's over
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

        // Skyrim's own stagger, from behind: San Andreas's victim struggles in KILL_KNIFE_PED_DAMAGE.
        void Stagger(RE::PlayerCharacter* a_player, RE::Actor* a_actor)
        {
            const auto  feet = a_player->GetPosition();
            const auto  at = a_actor->GetPosition();
            const float heading = std::atan2(feet.x - at.x, feet.y - at.y);
            a_actor->SetGraphVariableFloat("staggerDirection", Damage::StaggerDirection(a_actor->GetAngleZ(), heading));
            a_actor->SetGraphVariableFloat("staggerMagnitude", 0.25f);
            a_actor->NotifyAnimationGraph("staggerStart");
        }

        // The player (hidden, where CJ stands) plays Skyrim's sneak kill move on a_actor: the first of
        // StealthKill::kKillMoves Skyrim starts. False when none does, or the ground between them isn't level.
        bool StartKillMove(RE::PlayerCharacter* a_player, RE::Actor* a_actor)
        {
            if (!state.killMoves) {
                return false;
            }
            const auto feet = a_player->GetPosition();
            const auto at = a_actor->GetPosition();
            if (!StealthKill::KillMoveFits({ feet.x, feet.y, feet.z }, { at.x, at.y, at.z })) {
                SKSE::log::info("stealth kill: {:.2f} m of height to {} ({:08X}), no kill move", std::fabs(at.z - feet.z) * Pose::kMetersPerUnit, a_actor->GetName(),
                    a_actor->GetFormID());
                return false;
            }
            auto* process = a_player->GetActorRuntimeData().currentProcess;
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!process || !data) {
                return false;
            }
            for (const auto& move : StealthKill::kKillMoves) {
                auto* idle = data->LookupForm<RE::TESIdleForm>(move.formId & 0x00FFFFFF, "Skyrim.esm");
                if (!idle) {
                    if (!state.loggedIdles) {
                        SKSE::log::warn("stealth kill: Skyrim.esm has no idle {:08X} ({})", move.formId, move.editorId);
                    }
                    continue;
                }
                // Faced as CJ faces it, so the kill move lines up behind it.
                a_player->SetHeading(std::atan2(at.x - feet.x, at.y - feet.y));
                if (process->PlayIdle(a_player, idle, a_actor)) {
                    state.moving = a_actor->GetFormID();
                    state.movingFor = 0.0f;
                    state.confirmed = false;
                    state.loggedIdles = true;
                    SKSE::log::info("stealth kill: {} plays on {} ({:08X})", move.editorId, a_actor->GetName(), a_actor->GetFormID());
                    return true;
                }
                SKSE::log::info("stealth kill: Skyrim didn't start {} on {} ({:08X})", move.editorId, a_actor->GetName(), a_actor->GetFormID());
            }
            state.loggedIdles = true;
            return false;
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
            const auto at = a_actor->GetPosition();
            SKSE::log::info("stealth kill: CJ grabs {} ({:08X}) {:.2f} m off", a_actor->GetName(), a_actor->GetFormID(),
                feet.GetDistance(at) * Pose::kMetersPerUnit);
            // Skyrim's own sneak kill move on the victim; else the stagger and, at the stab, the ragdoll.
            if (!StartKillMove(a_player, a_actor)) {
                Stagger(a_player, a_actor);
            }
        }

        void Stab(RE::PlayerCharacter* a_player, RE::Actor* a_actor, std::uint8_t a_weapon)
        {
            state.held = 0;
            const auto feet = a_player->GetPosition();
            auto       target = TargetOf(a_actor, a_player);
            if (state.moving == a_actor->GetFormID()) {
                target.standing = true;  // in our own kill move, which is what takes it off its feet
            }
            const auto verdict = StealthKill::CheckStab({ feet.x, feet.y, feet.z }, a_weapon, target);
            if (verdict != StealthKill::Verdict::Yes) {
                SKSE::log::info("stealth kill: CJ's stab misses {} ({:08X}), who is {}", a_actor->GetName(), a_actor->GetFormID(), StealthKill::Why(verdict));
                return;
            }
            const auto dir = Toward(a_player, a_actor);
            const bool killMove = state.moving == a_actor->GetFormID() && a_actor->IsInKillMove();
            Shots::Kill(a_player, a_actor, a_weapon, Middle(a_actor) - dir * 10.0f, dir);
            const bool dead = a_actor->IsDead() || a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0.0f;
            state.owed = killMove && !dead;
            if (dead && !killMove) {  // in the kill move it falls as the move has it
                if (auto* process = a_actor->GetActorRuntimeData().currentProcess) {
                    process->KnockExplosion(a_actor, Middle(a_actor) - dir * kKnockBehind, StealthKill::kSlumpKnock);
                }
            }
            state.kills++;
            SKSE::log::info("stealth kill #{}: CJ stabs {} ({:08X}){}{}", state.kills, a_actor->GetName(), a_actor->GetFormID(),
                dead ? ", who dies" : ", who lives (Skyrim's hit didn't kill)", killMove ? " in the kill move" : ", into a ragdoll");
        }
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
        t.standing = !a_actor->IsInRagdollState() && !a_actor->IsInKillMove() &&
                     (!actorState || (actorState->GetKnockState() == RE::KNOCK_STATE_ENUM::kNormal && !actorState->IsBleedingOut() &&
                                         actorState->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal));
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

    void OnDataLoaded()
    {
        state.killMoves = Ini::Value("Weapons", "StealthKillMove") != "0";
        SKSE::log::info("stealth kill: victims {}", state.killMoves ? "play Skyrim's sneak kill move, or ragdoll" : "ragdoll ([Weapons] StealthKillMove=0)");
    }

    bool HoldsPlayer(RE::PlayerCharacter* a_player)
    {
        return state.moving && a_player && (state.confirmed || state.movingFor < StealthKill::kKillMoveConfirm);
    }

    void Update(RE::PlayerCharacter* a_player, float a_seconds)
    {
        const float dt = std::isfinite(a_seconds) && a_seconds > 0.0f ? a_seconds : 0.0f;
        if (state.moving) {
            state.movingFor += dt;
            auto*      victim = ActorOf(state.moving);
            const bool in = victim && (victim->IsInKillMove() || (a_player && a_player->IsInKillMove()));
            if (in && !state.confirmed) {
                state.confirmed = true;
                SKSE::log::info("stealth kill: {} ({:08X}) is in the kill move after {:.2f} s", victim->GetName(), victim->GetFormID(), state.movingFor);
            }
            if (!state.confirmed && state.movingFor >= StealthKill::kKillMoveConfirm) {
                // Skyrim took the idle but never played it: the stagger, and the ragdoll at the stab.
                SKSE::log::info("stealth kill: the kill move never showed on {:08X}, falling back to the ragdoll", state.moving);
                if (victim && a_player && !victim->IsDead()) {
                    Stagger(a_player, victim);
                }
                state.moving = 0;
            } else if (state.confirmed && (!in || state.movingFor >= StealthKill::kKillMoveMax)) {
                SKSE::log::info("stealth kill: the kill move on {:08X} is over after {:.2f} s, the victim {}", state.moving, state.movingFor,
                    victim && victim->IsDead() ? "dead" : "alive");
                if (state.owed && victim && victim->IsDead()) {
                    Chatter::StealthKill();  // the kill move killed it, holding off the stab's hit
                } else if (state.owed && victim && a_player && !victim->IsDead()) {
                    const auto dir = Toward(a_player, victim);
                    Shots::Kill(a_player, victim, Weapons::Knife, Middle(victim) - dir * 10.0f, dir);
                    SKSE::log::info("stealth kill: the stab Skyrim held off in the kill move lands now, {:08X} {}", state.moving, victim->IsDead() ? "dies" : "still lives");
                }
                state.owed = false;
                state.moving = 0;
            }
        }
        if (!state.held) {
            return;
        }
        state.heldFor += dt;
        auto* actor = ActorOf(state.held);
        if (!actor || actor->IsDead() || state.heldFor > StealthKill::kGrabMax) {
            if (actor && !actor->IsDead()) {
                SKSE::log::info("stealth kill: no stab came for {} ({:08X}), let go", actor->GetName(), actor->GetFormID());
            }
            state.held = 0;
            return;
        }
        if (state.moving != state.held) {
            actor->StopMoving(a_seconds);  // the kill move holds its own
        }
    }

    void Clear()
    {
        state.held = 0;
        state.moving = 0;
    }
}
