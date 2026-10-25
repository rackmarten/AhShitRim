#include "player/Vitals.h"

#include "health.h"
#include "pose.h"
#include "hud/SaHud.h"
#include "player/Hitboxes.h"
#include "player/CombatBalance.h"
#include "player/Police.h"
#include "player/Projectiles.h"
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
            bool                       invincible = false;  // BAGUVIX
            bool                       skillsFrozen = false;  // the hit-trained skills' progress when BAGUVIX went on was noted
            float                      frozenXp[3] = {};      // kHitSkills' xp then
            float                      shieldFrom = -1.0f;    // v31: the player's health after the last Update, less our own damage since
            int                        shieldLogged = 0;
            bool                       owned = false;         // San Andreas has the player
            int                        own = 0;               // our own damage being taken: not a hit of Skyrim's
            int                        magic = 0;             // a magic effect's damage being taken (EffectDamageHook)
        } state;

        // Our own damage on the player, which Police's HandleHealthDamage hook leaves alone.
        struct Own
        {
            Own() { ++state.own; }
            ~Own() { --state.own; }
        };

        // The skills being hit trains (Skyrim's own), held where they were while BAGUVIX is on: hits that do nothing
        // shouldn't level the player up.
        using Skill = RE::PlayerCharacter::PlayerSkills::Data::Skill;
        constexpr Skill kHitSkills[] = { Skill::kBlock, Skill::kLightArmor, Skill::kHeavyArmor };

        void HoldHitSkills(RE::PlayerCharacter* a_player)
        {
            auto* skills = a_player->GetInfoRuntimeData().skills;
            auto* data = skills ? skills->data : nullptr;
            if (!data) {
                static bool logged = false;
                if (!std::exchange(logged, true)) {
                    SKSE::log::warn("vitals: no player skills to hold for BAGUVIX");
                }
                return;
            }
            for (std::size_t i = 0; i < std::size(kHitSkills); ++i) {
                auto& skill = data->skills[kHitSkills[i]];
                if (!state.skillsFrozen) {
                    state.frozenXp[i] = skill.xp;
                    SKSE::log::info("vitals: BAGUVIX holds skill {} at level {:.0f}, {:.1f} of {:.1f} xp", static_cast<int>(kHitSkills[i]), skill.level, skill.xp,
                        skill.levelThreshold);
                } else if (skill.xp > state.frozenXp[i]) {
                    SKSE::log::info("vitals: BAGUVIX took back {:.2f} xp of skill {} (level {:.0f})", skill.xp - state.frozenXp[i], static_cast<int>(kHitSkills[i]), skill.level);
                    skill.xp = state.frozenXp[i];
                }
            }
            state.skillsFrozen = true;
        }

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
                Own own;
                a_player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kHealth, split.health);
                if (state.shieldFrom >= 0.0f) {
                    state.shieldFrom -= split.health;  // ours, not Skyrim's people's: the vehicle doesn't shield it
                }
            }
        }

        // In a vehicle (v31) Skyrim's hits are the vehicle's (Hitboxes): what Skyrim's people took off the player since the
        // last frame is given back, all of it in a closed one, half on a bike or a quad.
        void Shield(RE::PlayerCharacter* a_player, const Protocol::PlayerState* a_pose)
        {
            const float share = Hitboxes::CjShare(a_pose);
            auto*       values = a_player->AsActorValueOwner();
            const float now = values->GetActorValue(RE::ActorValue::kHealth);
            if (share >= 1.0f || state.shieldFrom < 0.0f || !(now < state.shieldFrom)) {
                return;
            }
            const float back = (state.shieldFrom - now) * (1.0f - share);
            values->RestoreActorValue(RE::ActorValue::kHealth, back);
            if (Bleeding(a_player) && values->GetActorValue(RE::ActorValue::kHealth) > 0.0f) {
                a_player->NotifyAnimationGraph("BleedoutStop"sv);  // one hit took it all at once, and the essential player went down
            }
            if (state.shieldLogged < 8) {
                state.shieldLogged++;
                SKSE::log::info("vitals: CJ's vehicle took a hit for him: {:.1f} of {:.1f} health given back", back, state.shieldFrom - now);
            }
        }

        // A hit reported as a_reported (either sign) times a_factor, through CJ's armour first when San Andreas's would
        // take a_kind (Health::Settle); the armour taken. Busted, the arrest takes no armour.
        Health::Settled ArmourFirst(RE::PlayerCharacter* a_player, float a_reported, float a_factor, Health::Kind a_kind)
        {
            const auto settled = Health::Settle(state.armor, a_reported, a_factor, MaxHealth(a_player), a_kind, !Police::IsBusted());
            if (settled.armor != state.armor) {
                SetArmor(settled.armor);
            }
            return settled;
        }

        // A Skyrim fire effect (a spell's, a trap's, burning oil's) is on the player.
        bool BurningInSkyrim(RE::PlayerCharacter* a_player)
        {
            auto* target = a_player->AsMagicTarget();
            auto* effects = target ? target->GetActiveEffectList() : nullptr;
            if (!effects) {
                return false;
            }
            for (auto* effect : *effects) {
                const auto* base = effect ? effect->GetBaseObject() : nullptr;
                if (base && base->data.resistVariable == RE::ActorValue::kResistFire && base->IsDetrimental() &&
                    !effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
                    return true;
                }
            }
            return false;
        }

        Health::Resist ResistOf(RE::ActiveEffect* a_effect)
        {
            const auto* base = a_effect ? a_effect->GetBaseObject() : nullptr;
            switch (base ? base->data.resistVariable : RE::ActorValue::kNone) {
            case RE::ActorValue::kResistFire: return Health::Resist::Fire;
            case RE::ActorValue::kResistFrost: return Health::Resist::Frost;
            case RE::ActorValue::kResistShock: return Health::Resist::Shock;
            case RE::ActorValue::kResistMagic: return Health::Resist::Magic;
            case RE::ActorValue::kPoisonResist: return Health::Resist::Poison;
            case RE::ActorValue::kResistDisease: return Health::Resist::Disease;
            default: return Health::Resist::None;
            }
        }

        // Skyrim's harmful magic on the player's health (spells, enchanted weapons, traps' and hazards' spells such as
        // burning oil, poisons): ValueModifierEffect::ModifyActorValue (vfunc 0x20) of each effect class that has one.
        // While CJ has the player its damage is [Balance] DamageTaken of it, times the stars' share for a guard's, through
        // CJ's armour first (not poison or disease), and fire sets CJ alight in San Andreas.
        template <int I>
        struct EffectDamageHook
        {
            static void thunk(RE::ValueModifierEffect* a_effect, RE::Actor* a_actor, float a_value, RE::ActorValue a_av)
            {
                if (!state.owned || a_av != RE::ActorValue::kHealth || !a_actor || !a_actor->IsPlayerRef() || !std::isfinite(a_value) || a_value == 0.0f) {
                    original(a_effect, a_actor, a_value, a_av);
                    return;
                }
                // Damage comes as a negative change, or as an amount on a harmful effect: either sign is handled.
                const auto* base = a_effect ? a_effect->GetBaseObject() : nullptr;
                const bool  harmful = a_value < 0.0f || (base && base->IsDetrimental());
                static int  seen = 0;
                if (seen < 8) {
                    seen++;
                    SKSE::log::info("vitals: {} ({:08X}) changes the player's health by {:.2f} ({}harmful)", base ? base->GetName() : "?", base ? base->GetFormID() : 0,
                        a_value, harmful ? "" : "not ");
                }
                if (!harmful || state.invincible || state.sequence.Running() || state.own > 0) {
                    original(a_effect, a_actor, a_value, a_av);
                    return;
                }
                auto*       player = static_cast<RE::PlayerCharacter*>(a_actor);
                const auto  resist = ResistOf(a_effect);
                const auto  kind = Health::OfEffect(resist);
                const auto  caster = a_effect->caster.get();
                const float factor = Balance::TakenFactor(Police::ShareFor(caster.get()));
                const float armor = state.armor;
                const auto  settled = ArmourFirst(player, a_value, factor, kind);
                ++state.magic;
                original(a_effect, a_actor, a_value < 0.0f ? -settled.health : settled.health, a_av);
                --state.magic;
                if (settled.back > 0.0f) {
                    Police::NoteSpared(player);
                }
                if (Health::SetsAlight(kind)) {
                    Projectiles::BurnCj(player);
                }
                static int logged = 0;
                if (logged < 40) {
                    logged++;
                    SKSE::log::info("vitals: {} ({:08X}, resist {}) from {} took {:.2f} health, x{:.2f}: armour {:.0f} -> {:.0f}, {:.2f} off health",
                        base ? base->GetName() : "?", base ? base->GetFormID() : 0, static_cast<int>(resist), caster ? caster->GetName() : "nobody",
                        Health::DamageOf(a_value), factor, armor, state.armor, settled.health);
                }
            }
            static inline REL::Relocation<decltype(thunk)> original;

            static void Install(REL::VariantID a_vtable)
            {
                REL::Relocation<std::uintptr_t> vtable{ a_vtable };
                original = vtable.write_vfunc(0x20, thunk);
            }
        };
    }

    void Install()
    {
        EffectDamageHook<0>::Install(RE::VTABLE_ValueModifierEffect[0]);
        EffectDamageHook<1>::Install(RE::VTABLE_AbsorbEffect[0]);
        EffectDamageHook<2>::Install(RE::VTABLE_AccumulatingValueModifierEffect[0]);
        EffectDamageHook<3>::Install(RE::VTABLE_DualValueModifierEffect[0]);
        EffectDamageHook<4>::Install(RE::VTABLE_PeakValueModifierEffect[0]);
        EffectDamageHook<5>::Install(RE::VTABLE_TargetValueModifierEffect[0]);
        EffectDamageHook<6>::Install(RE::VTABLE_ValueAndConditionsEffect[0]);
        SKSE::log::info("vitals: the value modifier effects' ModifyActorValue hooked: Skyrim's magic on CJ goes through his armour first");
    }

    bool OwnDamage() { return state.own > 0 || state.magic > 0; }

    void Hurt(RE::PlayerCharacter* a_player, float a_damage)
    {
        if (a_player && a_damage > 0.0f && !state.sequence.Running() && !state.invincible) {
            Apply(a_player, a_damage);
        }
    }

    float TakeHit(RE::PlayerCharacter* a_player, float a_reported, float a_factor, Health::Kind a_kind)
    {
        if (!a_player || state.sequence.Running() || state.invincible || !(Health::DamageOf(a_reported) > 0.0f)) {
            return 0.0f;
        }
        if (BurningInSkyrim(a_player)) {
            Projectiles::BurnCj(a_player);  // a fire trap's or spell's hit
        }
        const float back = ArmourFirst(a_player, a_reported, a_factor, a_kind).back;
        auto*       values = a_player->AsActorValueOwner();
        Own         own;
        if (back > 0.0f) {
            values->RestoreActorValue(RE::ActorValue::kHealth, back);
        } else if (back < 0.0f) {
            values->DamageActorValue(RE::ActorValue::kHealth, -back);
        }
        return back;
    }

    void OnTakeOver(RE::PlayerCharacter* a_player)
    {
        state.owned = true;
        state.meter.Reset();
        state.shieldFrom = -1.0f;
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
        state.owned = false;
        EndShow();
        SetEssential(a_player, false);
        state.meter.Reset();
        state.shieldFrom = -1.0f;
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
        // What Skyrim's people do to the player is measured from the health this frame ends on.
        struct NoteHealth
        {
            RE::PlayerCharacter* player;
            ~NoteHealth() { state.shieldFrom = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth); }
        } note{ a_player };
        if (!state.sequence.Running() && !state.invincible) {
            Shield(a_player, a_pose);
        }
        if (state.sequence.Running()) {
            Run(a_player, seconds, a_cjDrawn);
            return state.sequence.Running();
        }
        if (Police::IsBusted()) {
            // Busted (v22): an arrested man isn't shot dead; San Andreas's damage is dropped and Skyrim's undone, as for BAGUVIX.
            auto*       values = a_player->AsActorValueOwner();
            const float max = MaxHealth(a_player);
            if (const float now = values->GetActorValue(RE::ActorValue::kHealth); now < max) {
                values->RestoreActorValue(RE::ActorValue::kHealth, max - now);
            }
            if (Bleeding(a_player)) {
                a_player->NotifyAnimationGraph("BleedoutStop"sv);
            }
            return false;
        }
        if (state.invincible) {
            // BAGUVIX: San Andreas's damage is dropped, and whatever Skyrim's people did to the player this frame undone.
            auto*       values = a_player->AsActorValueOwner();
            const float max = MaxHealth(a_player);
            if (const float now = values->GetActorValue(RE::ActorValue::kHealth); now < max) {
                values->RestoreActorValue(RE::ActorValue::kHealth, max - now);
            }
            if (Bleeding(a_player)) {
                a_player->NotifyAnimationGraph("BleedoutStop"sv);  // one hit took it all at once, and the essential player went down
            }
            HoldHitSkills(a_player);
            SaHud::SetArmor(state.armor / Health::kSaFullHealth);
            return false;
        }
        if (damage > 0.0f) {
            Apply(a_player, damage);
        }
        if (BurningInSkyrim(a_player)) {
            Projectiles::BurnCj(a_player);  // Skyrim's fire on the player, however it came: San Andreas sets CJ alight
        }
        // A guard's hit put the essential player down, but his eased share of it (Police) left it standing: no Wasted.
        if (Police::TakeSpared() && Bleeding(a_player) && a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) > 0.0f) {
            a_player->NotifyAnimationGraph("BleedoutStop"sv);
            SKSE::log::info("vitals: a guard's eased hit left the player standing, its bleedout stopped");
            SaHud::SetArmor(state.armor / Health::kSaFullHealth);
            return false;
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

    void SetInvincible(bool a_on)
    {
        if (a_on != state.invincible) {
            state.invincible = a_on;
            state.skillsFrozen = false;
            SKSE::log::info("vitals: the player {}", a_on ? "takes no damage (BAGUVIX)" : "takes damage again");
        }
    }

    bool IsInvincible() { return state.invincible; }

    float Armor() { return state.armor; }

    void SetArmor(float a_armor)
    {
        state.armor = a_armor;
        SaHud::SetArmor(a_armor / Health::kSaFullHealth);
    }
}
