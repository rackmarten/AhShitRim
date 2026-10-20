#include "player/Vitals.h"

#include "health.h"
#include "pose.h"
#include "hud/SaHud.h"
#include "wasted.h"

#include <chrono>
#include <format>

namespace AhShitRim::Vitals
{
    namespace
    {
        constexpr RE::FormID kGold = 0x0000000F;
        constexpr RE::FormID kUnbound = 0x0003372B;  // MQ101, the Helgen intro
        constexpr int        kFee = 100;

        struct State
        {
            Wasted::Sequence           sequence;
            Health::Meter              meter;
            float                      armor = 0.0f;
            bool                       essential = false;  // we made the player essential
            const Wasted::TempleInfo*  temple = &Wasted::Info(Wasted::Temple::Kynareth);  // where the player last was, for the wilds
            bool                       moveAsked = false;
            bool                       arrived = false;  // the player was re-anchored after the move
            bool                       tracked = false;
            std::chrono::steady_clock::time_point last;
            float                      scale = 1.0f;  // the game speed we set
            float                      unboundAge = 100.0f;
            bool                       unbound = false;
        } state;

        bool UnboundRunning(float a_seconds)
        {
            state.unboundAge += a_seconds;
            if (state.unboundAge < 1.0f) {
                return state.unbound;
            }
            state.unboundAge = 0.0f;
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(kUnbound);
            const char* name = quest ? quest->GetName() : nullptr;
            state.unbound = name && std::string_view(name) == "Unbound" && quest->IsRunning() && !quest->IsCompleted();
            return state.unbound;
        }

        void SetEssential(RE::PlayerCharacter* a_player, bool a_on)
        {
            auto* base = a_player ? a_player->GetActorBase() : nullptr;
            if (!base) {
                return;
            }
            auto& flags = base->actorData.actorBaseFlags;
            if (a_on && !state.essential && flags.none(RE::ACTOR_BASE_DATA::Flag::kEssential)) {
                flags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
                state.essential = true;
                SKSE::log::info("vitals: the player is essential while San Andreas has it, so it bleeds out instead of dying");
            } else if (!a_on && state.essential) {
                flags.reset(RE::ACTOR_BASE_DATA::Flag::kEssential);
                state.essential = false;
                SKSE::log::info("vitals: the player is mortal again");
            }
        }

        void SetTimeScale(float a_scale)
        {
            if (a_scale != state.scale) {
                state.scale = a_scale;
                if (auto* timer = RE::BSTimer::GetSingleton()) {
                    timer->SetGlobalTimeMultiplier(a_scale, true);
                }
            }
        }

        void EndShow()
        {
            SetTimeScale(1.0f);
            SaHud::SetDesaturation(0.0f);
            SaHud::FadeTo(0.0f, 0.0f);
        }

        // Actor::IsInBleedout crashed here; the life state it reads is the same.
        bool Bleeding(RE::PlayerCharacter* a_player)
        {
            auto* state = a_player->AsActorState();
            return state && state->GetLifeState() == RE::ACTOR_LIFE_STATE::kBleedout;
        }

        float MaxHealth(RE::PlayerCharacter* a_player) { return a_player->GetActorValueMax(RE::ActorValue::kHealth); }

        bool OutOfHealth(RE::PlayerCharacter* a_player)
        {
            return a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0.0f || Bleeding(a_player);
        }

        // Remembers the temple of the hold the player is in (its location, or the one that holds it, named
        // after a hold or a city); the wilds have no location and keep the last one.
        void TrackTemple(RE::PlayerCharacter* a_player)
        {
            for (auto* location = a_player->GetCurrentLocation(); location; location = location->parentLoc) {
                const char* name = location->GetName();
                if (!name) {
                    continue;  // an unnamed location in the chain
                }
                if (const auto* temple = Wasted::ForPlace(name)) {
                    if (temple != state.temple) {
                        SKSE::log::info("vitals: the player is in {} ({}), its temple is the {}", name, temple->cell, temple->name);
                    }
                    state.temple = temple;
                    return;
                }
            }
        }

        // `coc` to the temple's cell: the game's own console teleport, a load door's worth of loading.
        void MoveToTemple(RE::PlayerCharacter* a_player)
        {
            state.moveAsked = false;
            state.arrived = false;
            auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
            auto* script = factory ? factory->Create() : nullptr;
            if (!script) {
                SKSE::log::warn("vitals: no script to take the player to the {}, it wakes where it fell", state.temple->name);
                return;
            }
            script->SetCommand(std::format("coc {}", state.temple->cell));
            script->CompileAndRun(a_player);
            delete script;
            state.moveAsked = true;
            SKSE::log::info("vitals: the player is taken to the {} (coc {})", state.temple->name, state.temple->cell);
        }

        void Revive(RE::PlayerCharacter* a_player)
        {
            auto* values = a_player->AsActorValueOwner();
            const float max = MaxHealth(a_player);
            const float now = values->GetActorValue(RE::ActorValue::kHealth);
            if (now < max) {
                values->RestoreActorValue(RE::ActorValue::kHealth, max - now);
            }
            if (Bleeding(a_player)) {
                a_player->NotifyAnimationGraph("BleedoutStop"sv);
            }
            state.armor = 0.0f;
            const int fee = (std::min)(kFee, a_player->GetGoldAmount());
            if (fee > 0) {
                if (auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold)) {
                    a_player->RemoveItem(gold, fee, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                }
            }
            auto* cell = a_player->GetParentCell();
            SKSE::log::info("vitals: the player wakes at full health, {} gold lighter, in {:08X}{}", fee, cell ? cell->GetFormID() : 0,
                state.moveAsked && !state.arrived ? " (the temple never opened: where it fell)" : "");
            SaHud::ShowHelp(fee > 0 ? std::format("You wake up at the {}. The healers took {} gold.", state.temple->name, fee) :
                                      std::format("You wake up at the {}.", state.temple->name));
        }

        void Run(RE::PlayerCharacter* a_player, float a_seconds, bool a_cjDrawn)
        {
            const bool arrived = !state.moveAsked || (state.arrived && a_cjDrawn);
            const auto frame = state.sequence.Tick(a_seconds, arrived);
            SetTimeScale(frame.timeScale);
            SaHud::SetDesaturation(frame.desaturate);
            SaHud::FadeTo(frame.fade, 0.0f);
            switch (frame.action) {
            case Wasted::Action::Move: MoveToTemple(a_player); break;
            case Wasted::Action::Revive: Revive(a_player); break;
            case Wasted::Action::Done:
                EndShow();
                SKSE::log::info("vitals: Wasted is over");
                break;
            default: break;
            }
        }

        void Begin(RE::PlayerCharacter* a_player)
        {
            const auto frame = state.sequence.Start();
            SetTimeScale(frame.timeScale);
            SaHud::ShowBigMessage(SaHud::BigMessage::Wasted, 4.5f);
            SKSE::log::info("vitals: Wasted (health {:.0f} of {:.0f}, in {:08X}), the temple will be the {}",
                a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth), MaxHealth(a_player),
                a_player->GetParentCell() ? a_player->GetParentCell()->GetFormID() : 0, state.temple->name);
        }

        void Apply(RE::PlayerCharacter* a_player, float a_damage)
        {
            const auto split = Health::Absorb(state.armor, a_damage, MaxHealth(a_player));
            SKSE::log::info("vitals: San Andreas hit CJ for {:.1f} ({:.1f} armour left, {:.1f} health taken)", a_damage, split.armor, split.health);
            state.armor = split.armor;
            if (split.health > 0.0f) {
                a_player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kHealth, split.health);
            }
        }
    }

    void OnTakeOver(RE::PlayerCharacter* a_player)
    {
        state.meter.Reset();
        state.sequence.Cancel();
        state.tracked = false;
        if (!UnboundRunning(100.0f)) {
            SetEssential(a_player, true);
        }
    }

    void OnRelease(RE::PlayerCharacter* a_player)
    {
        if (state.sequence.Running()) {
            state.sequence.Cancel();
            SKSE::log::info("vitals: Wasted ended with the handover");
        }
        EndShow();
        SetEssential(a_player, false);
        state.meter.Reset();
        state.moveAsked = false;
    }

    void OnReanchor()
    {
        if (state.moveAsked) {
            state.arrived = true;
        }
    }

    bool Update(RE::PlayerCharacter* a_player, const Protocol::PlayerState* a_pose, bool a_cjDrawn)
    {
        const auto now = std::chrono::steady_clock::now();
        const float seconds = state.tracked ? (std::min)(std::chrono::duration<float>(now - state.last).count(), 0.25f) : 0.0f;
        state.last = now;
        state.tracked = true;

        // Unbound reloads the last save as Skyrim does: the player must be able to die then.
        SetEssential(a_player, !UnboundRunning(seconds));
        TrackTemple(a_player);

        const float damage = a_pose ? state.meter.Take(a_pose->damage) : 0.0f;
        if (state.sequence.Running()) {
            Run(a_player, seconds, a_cjDrawn);
            return state.sequence.Running();
        }
        if (damage > 0.0f) {
            Apply(a_player, damage);
        }
        if (state.essential && OutOfHealth(a_player)) {
            Begin(a_player);
            return true;
        }
        SaHud::SetArmor(state.armor / Health::kSaFullHealth);
        return false;
    }

    bool IsWasted() { return state.sequence.Running() && state.sequence.GetPhase() != Wasted::Phase::Waking; }

    float Armor() { return state.armor; }

    void SetArmor(float a_armor)
    {
        state.armor = a_armor;
        SaHud::SetArmor(a_armor / Health::kSaFullHealth);
    }
}
