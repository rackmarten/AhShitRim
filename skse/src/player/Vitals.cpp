#include "player/Vitals.h"

#include "health.h"
#include "pose.h"
#include "hud/SaHud.h"
#include "wasted.h"

#include <chrono>
#include <format>
#include <vector>

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
            Wasted::Frame              frame;  // the sequence's latest
            std::vector<RE::TESFaction*> hunters;  // the crime factions of those fighting the player as it fell
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

        void EndShow()
        {
            SaHud::HideBigMessage();
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

        // The holds whose people fight the player as it falls: their bounty goes with Wasted.
        void NoteHunters(RE::PlayerCharacter* a_player)
        {
            state.hunters.clear();
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            lists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (a_actor && a_actor->GetActorRuntimeData().currentCombatTarget.get().get() == a_player) {
                    if (auto* faction = a_actor->GetCrimeFaction(); faction && std::ranges::find(state.hunters, faction) == state.hunters.end()) {
                        state.hunters.push_back(faction);
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        // San Andreas clears the wanted level with Wasted: the bounty those holds put on the player,
        // and everyone's fight with it.
        void Forgive(RE::PlayerCharacter* a_player)
        {
            for (auto* faction : state.hunters) {
                const auto bounty = a_player->GetCrimeGoldValue(faction);
                a_player->ClearAllCrimeGold(faction);
                SKSE::log::info("vitals: the bounty of {} ({}) is cleared", faction->GetName(), bounty);
            }
            state.hunters.clear();
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->StopCombatAndAlarmOnActor(a_player, false);
            }
            a_player->StopCombat();
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
            Forgive(a_player);
            const int fee = (std::min)(kFee, a_player->GetGoldAmount());
            if (fee > 0) {
                if (auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold)) {
                    a_player->RemoveItem(gold, fee, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                }
            }
            auto* cell = a_player->GetParentCell();
            SKSE::log::info("vitals: the player wakes at full health, {} gold lighter, in {:08X}{}", fee, cell ? cell->GetFormID() : 0,
                state.moveAsked && !state.arrived ? " (the temple never opened: where it fell)" : "");
            SaHud::HideBigMessage();
            SaHud::ShowHelp(fee > 0 ? std::format("You wake up at the {}. The healers took {} gold.", state.temple->name, fee) :
                                      std::format("You wake up at the {}.", state.temple->name));
        }

        void Run(RE::PlayerCharacter* a_player, float a_seconds, bool a_cjDrawn)
        {
            const bool arrived = !state.moveAsked || (state.arrived && a_cjDrawn);
            const auto frame = state.sequence.Tick(a_seconds, arrived);
            state.frame = frame;
            SaHud::FadeTo(frame.fade, 0.0f);
            switch (frame.action) {
            case Wasted::Action::Wasted:
                SaHud::ShowBigMessage(SaHud::BigMessage::Wasted, Wasted::TextSeconds() - 0.65f);  // less its fade in: gone as the screen is black
                SKSE::log::info("vitals: CJ lies dead, \"wasted\" and San Andreas's death camera");
                break;
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
            state.frame = state.sequence.Start();
            NoteHunters(a_player);
            SKSE::log::info("vitals: Wasted (health {:.0f} of {:.0f}, in {:08X}), CJ falls, the temple will be the {}",
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

    bool IsDown() { return state.sequence.Running() && state.frame.down; }

    bool HasDeathCamera() { return state.sequence.Running() && state.frame.camera; }

    float Armor() { return state.armor; }

    void SetArmor(float a_armor)
    {
        state.armor = a_armor;
        SaHud::SetArmor(a_armor / Health::kSaFullHealth);
    }
}
