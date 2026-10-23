#include "player/CheatCodes.h"

#include "Ini.h"
#include "cheats.h"
#include "health.h"
#include "hud/SaHud.h"
#include "link/LinkService.h"
#include "mercy.h"
#include "player/Police.h"
#include "player/Vitals.h"
#include "vehicle_kinds.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

namespace AhShitRim::CheatCodes
{
    using namespace Cheats;

    namespace
    {
        constexpr RE::FormID kGold = 0x0000000F;
        constexpr int        kHesoyamGold = 250000;
        constexpr float      kSuicideDamage = 1000.0f;  // San Andreas's SuicideCheat: armour first, then ten whole bars
        constexpr auto       kScanEvery = std::chrono::seconds(1);

        struct State
        {
            Typist        typist;
            Switches      on;            // San Andreas's toggles and BAGUVIX (NOMERCY is the ledger's)
            bool          resend = false;  // San Andreas's toggles have to go to it again, each as it is (a send failed, the link came back, a load)
            bool          wasUp = false;
            Mercy::Ledger mercy;
            bool          basesCleared = false;  // NOMERCY's pass over every actor base was made (since it went on, or the game loaded)
            std::chrono::steady_clock::time_point nextScan;
        } state;

        bool Send(const Cheat& a_cheat, bool a_on)
        {
            const auto command = CommandFor(a_cheat, a_on);
            return LinkService::Get().Send(Protocol::MessageType::Cheat, &command, sizeof(command));
        }

        bool GiveWeapon(unsigned a_type, std::uint32_t a_ammo)
        {
            Protocol::WeaponCommand command{};
            command.kind = static_cast<std::uint32_t>(Protocol::WeaponCommandKind::GiveWeapon);
            command.argument = a_type;
            command.entries[0].ammo = a_ammo;
            return LinkService::Get().Send(Protocol::MessageType::WeaponCommand, &command, sizeof(command));
        }

        bool GiveSet(unsigned a_set)
        {
            Protocol::WeaponCommand command{};
            command.kind = static_cast<std::uint32_t>(Protocol::WeaponCommandKind::GiveSet);
            command.argument = a_set;
            return LinkService::Get().Send(Protocol::MessageType::WeaponCommand, &command, sizeof(command));
        }

        // HESOYAM's Skyrim half: the health bar and CJ's armour full, and San Andreas's $250,000 as gold.
        void Heal(RE::PlayerCharacter* a_player)
        {
            auto*       values = a_player->AsActorValueOwner();
            const float max = a_player->GetActorValueMax(RE::ActorValue::kHealth);
            const float now = values->GetActorValue(RE::ActorValue::kHealth);
            if (now < max) {
                values->RestoreActorValue(RE::ActorValue::kHealth, max - now);
            }
            Vitals::SetArmor(Health::kSaFullHealth);
            if (auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold)) {
                a_player->AddObjectToContainer(gold, nullptr, kHesoyamGold, nullptr);
            }
        }

        // ---- NOMERCY ----

        std::uint32_t BaseIdOf(RE::Actor* a_actor)
        {
            auto* base = a_actor->GetActorBase();
            return base ? base->GetFormID() : 0;
        }

        // Every actor base's flag, once: those loading later come without it.
        int ClearBases()
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* playerBase = player ? player->GetActorBase() : nullptr;
            int   cleared = 0;
            if (!data) {
                return 0;
            }
            for (auto* npc : data->GetFormArray<RE::TESNPC>()) {
                if (npc && npc != playerBase && Mercy::Clears(true, npc->IsEssential(), 0, npc->GetFormID())) {
                    npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kEssential);
                    state.mercy.NoteBase(npc->GetFormID());
                    cleared++;
                }
            }
            return cleared;
        }

        // Every loaded actor's own flag: its base set it as it loaded, or a quest's alias set it since.
        int ClearActors()
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            int   cleared = 0;
            if (!lists) {
                return 0;
            }
            lists->ForAllActors([&](RE::Actor* a_actor) {
                if (a_actor && !a_actor->IsPlayerRef() && Mercy::Clears(true, a_actor->IsEssential(), a_actor->GetFormID(), BaseIdOf(a_actor))) {
                    a_actor->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kEssential);
                    if (state.mercy.NoteActor(a_actor->GetFormID(), BaseIdOf(a_actor))) {
                        SKSE::log::info("cheats: NOMERCY: {} ({:08X}) isn't essential any more", a_actor->GetName(), a_actor->GetFormID());
                    }
                    cleared++;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            return cleared;
        }

        void RestoreBases(const std::vector<std::uint32_t>& a_bases)
        {
            for (const auto id : a_bases) {
                if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(id)) {
                    npc->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
                }
            }
        }

        // The actors get theirs back if they are still who they were (a temporary reference's id can be someone else's after a load).
        int RestoreActors(const std::vector<Mercy::Actor>& a_actors)
        {
            int restored = 0;
            for (const auto& noted : a_actors) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(noted.id);
                if (actor && !actor->IsPlayerRef() && BaseIdOf(actor) == noted.base) {
                    actor->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kEssential);
                    restored++;
                }
            }
            return restored;
        }

        void SetMercy(bool a_on)
        {
            if (a_on) {
                state.mercy.SetOn(true);
                const int bases = ClearBases();
                state.basesCleared = true;
                const int actors = ClearActors();
                state.nextScan = std::chrono::steady_clock::now() + kScanEvery;
                SKSE::log::info("cheats: NOMERCY on: {} actor bases and {} loaded actors aren't essential any more", bases, actors);
            } else {
                const auto cleared = state.mercy.TakeAll();
                RestoreBases(cleared.bases);
                const int actors = RestoreActors(cleared.actors);
                state.basesCleared = false;
                SKSE::log::info("cheats: NOMERCY off: {} actor bases and {} of {} actors essential again", cleared.bases.size(), actors, cleared.actors.size());
            }
        }

        // Why a cheat that works on CJ's vehicle can't now, or nothing.
        std::string VehicleRefusal(Kind a_kind, std::uint16_t a_vehicle)
        {
            if (!a_vehicle) {
                return "Get in a vehicle first";
            }
            if (a_kind == Kind::Hydraulics && VehicleKinds::KindOf(a_vehicle) != VehicleKinds::Kind::Car) {
                return "Hydraulics only fit a car";
            }
            return {};
        }

        void Apply(const Cheat& a_cheat, const World& a_world)
        {
            if (NeedsVehicle(a_cheat.kind)) {
                if (const auto refusal = VehicleRefusal(a_cheat.kind, a_world.vehicle()); !refusal.empty()) {
                    SaHud::ShowHelp(refusal, 4.0f);
                    SKSE::log::info("cheats: {} typed, but no {}: {}", a_cheat.code, a_cheat.what, refusal);
                    return;
                }
            }
            auto*      player = RE::PlayerCharacter::GetSingleton();
            const bool on = a_cheat.kind == Kind::NoMercy      ? !state.mercy.On() :
                            a_cheat.kind == Kind::Hydraulics   ? !a_world.hydraulics() :  // the car CJ is in has them: off it
                            Toggles(a_cheat.kind)              ? state.on.Flip(a_cheat) :
                                                                 true;
            bool       sent = true;
            switch (a_cheat.kind) {
            case Kind::Health:
                if (player) {
                    Heal(player);
                }
                break;
            case Kind::Invincible: Vitals::SetInvincible(on); break;
            case Kind::Weapons: sent = GiveSet(a_cheat.argument); break;
            case Kind::Weapon: sent = GiveWeapon(a_cheat.argument, kWeaponAmmo); break;
            case Kind::Spawn:
                if (const auto refusal = a_world.spawn(a_cheat.argument); !refusal.empty()) {
                    SaHud::ShowHelp(refusal, 4.0f);
                    SKSE::log::info("cheats: {} typed, but no {}: {}", a_cheat.code, a_cheat.what, refusal);
                    return;
                }
                break;
            case Kind::Suicide:
                if (player) {
                    Vitals::Hurt(player, kSuicideDamage);
                }
                break;
            case Kind::NoMercy: SetMercy(on); break;
            case Kind::Wanted: Police::CheatStars(a_cheat.argument); break;
            default: break;
            }
            if (ToSanAndreas(a_cheat.kind)) {
                sent = Send(a_cheat, on) && sent;
            }
            if (!sent && Toggles(a_cheat.kind)) {
                state.resend = true;  // San Andreas gets it once the link is back
            }
            SaHud::ShowHelp(std::string(on ? kActivated : kDeactivated), 4.0f);
            SKSE::log::info("cheats: {} typed: {} {}{}", a_cheat.code, a_cheat.what, Toggles(a_cheat.kind) || a_cheat.kind == Kind::Hydraulics ? (on ? "on" : "off") : "given",
                sent ? "" : " (the link is down: San Andreas's part is lost)");
        }
    }

    void OnDataLoaded()
    {
        const std::filesystem::path sa = Ini::Value("SanAndreas", "Path");
        const auto                  ide = sa / "data" / "vehicles.ide";
        std::ifstream               file(ide, std::ios::binary);
        if (sa.empty() || !file) {
            SKSE::log::warn("cheats: {} doesn't read, so no vehicle comes by its name", sa.empty() ? "no [SanAndreas] Path" : ide.string());
            return;
        }
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        state.typist.SetVehicleNames(ParseVehicleNames(text));
        SKSE::log::info("cheats: {} vehicles come by their name, from {}", state.typist.VehicleNameCount(), ide.string());
    }

    void Key(std::uint32_t a_dik, const World& a_world)
    {
        const auto typed = state.typist.Key(a_dik);
        if (typed.cheat) {
            Apply(*typed.cheat, a_world);
        } else if (typed.model) {
            // A vehicle typed by its name (ours): as a spawn cheat, without San Andreas's code.
            if (const auto refusal = a_world.spawn(typed.model); !refusal.empty()) {
                SaHud::ShowHelp(refusal, 4.0f);
                SKSE::log::info("cheats: vehicle {} typed by name, but: {}", typed.model, refusal);
                return;
            }
            SaHud::ShowHelp(std::string(kActivated), 4.0f);
            SKSE::log::info("cheats: vehicle {} typed by name", typed.model);
        }
    }

    void Update()
    {
        const bool up = LinkService::Get().IsUp();
        if (up && !state.wasUp) {
            state.resend = true;  // a San Andreas that started again has its cheats off, one that stayed may have missed an off
        }
        state.wasUp = up;
        if (state.resend && up) {
            bool all = true;
            for (const auto& cheat : kCheats) {
                if (Toggles(cheat.kind) && ToSanAndreas(cheat.kind)) {
                    all = Send(cheat, state.on.On(cheat)) && all;
                }
            }
            state.resend = !all;
            if (all && state.on.Any()) {
                SKSE::log::info("cheats: San Andreas's toggles sent to it again");
            }
        }
        if (state.mercy.On()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= state.nextScan) {
                state.nextScan = now + kScanEvery;
                if (!state.basesCleared) {
                    const int bases = ClearBases();
                    state.basesCleared = true;
                    SKSE::log::info("cheats: NOMERCY is on in this game: {} actor bases aren't essential any more", bases);
                }
                ClearActors();
            }
        }
    }

    bool On(SaCheat a_cheat)
    {
        const auto* cheat = Find(a_cheat);
        return cheat && state.on.On(*cheat);
    }

    void OnGameLoad()
    {
        if (state.on.Any()) {
            SKSE::log::info("cheats: a game loads, San Andreas's cheats and BAGUVIX are off");
        }
        state.on.Clear();
        state.resend = true;  // each sent off, in the next Update
        state.typist.Clear();
        Vitals::SetInvincible(false);
    }

    void Save(SKSE::SerializationInterface* a_intfc)
    {
        const auto bytes = Mercy::Write(state.mercy);
        if (!a_intfc->WriteRecord(Mercy::kRecordType, Mercy::kVersion, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
            SKSE::log::error("cheats: NOMERCY's co-save record could not be written");
            return;
        }
        SKSE::log::info("cheats: saved NOMERCY {} ({} bases, {} actors to put back)", state.mercy.On() ? "on" : "off", state.mercy.Bases().size(),
            state.mercy.Actors().size());
    }

    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size)
    {
        Mercy::Ledger ledger;
        if (!Mercy::Read(a_version, a_data, a_size, ledger)) {
            SKSE::log::warn("cheats: NOMERCY's co-save record (version {}, {} bytes) doesn't read, NOMERCY is off", a_version, a_size);
            return;
        }
        state.mercy = std::move(ledger);
        state.basesCleared = false;  // this session's bases are as the plugins have them: cleared again on the next scan
        state.nextScan = {};
        SKSE::log::info("cheats: loaded NOMERCY {} ({} actors to put back)", state.mercy.On() ? "on" : "off", state.mercy.Actors().size());
    }

    void Revert()
    {
        // The bases' flags live in this session's memory, not in any save: back as the plugins have them. The
        // actors are the loaded save's own.
        const auto bases = state.mercy.TakeBases();
        RestoreBases(bases);
        if (!bases.empty()) {
            SKSE::log::info("cheats: {} actor bases essential again for the game that loads", bases.size());
        }
        state.mercy = {};
        state.basesCleared = false;
    }
}
