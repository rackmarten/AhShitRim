#include "player/Police.h"

#include "hud/SaHud.h"
#include "player/CombatBalance.h"
#include "player/CheatCodes.h"
#include "player/Vitals.h"
#include "knock.h"
#include "health.h"
#include "wasted.h"
#include "vehicle_hits.h"
#include "vehicle_kinds.h"
#include "wanted.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AhShitRim::Police
{
    namespace
    {
        constexpr RE::FormID kGold = 0x0000000F;
        constexpr float      kLookEvery = 0.25f;  // seconds between looks for who sees CJ
        constexpr float      kJoinEvery = 1.0f;   // seconds between calls for the hold's guards to join
        constexpr float      kCalmEvery = 0.5f;   // seconds between calling the fight off, while busted or never wanted
        constexpr float      kFightAfter = 0.5f;  // one of ours goes for CJ this long after it's in
        constexpr float      kSnapFar = 600.0f;     // a spawn spot's navmesh further than this from it is somewhere else: not used
        constexpr float      kGoneRange = 4000.0f;   // a pursuer leaving goes once this far, or out of the player's sight
        constexpr float      kLostRange = 10000.0f;  // one of ours this far off is lost: he leaves
        constexpr float      kSpawnRise = 300.0f;    // a spawn spot more than this above or below the player is a tower or a ledge: not used
        constexpr float      kOnSpot = 150.0f;       // after the move, the player this far from where it should be is moved there again
        constexpr float      kSettle = 2.0f;         // seconds after a game loads before the crimes and the guards are looked at
        constexpr float      kBarracksRange = 6000.0f;  // the city's barracks this close: half the reinforcements come out of there
        constexpr float      kDragonHeat = 400.0f;   // bringing down the guards' dragon, San Andreas's helicopter
        constexpr float      kChasedAfterRise = 6.0f;  // seconds at least between CJ's chased line on a star gained and the last one
        constexpr int        kSpawnTries = 6;        // spots tried per spawn
        constexpr RE::FormID kXMarker = 0x0000003B;  // XMarker: probes the ground for a spawn spot
        constexpr float      kSteerEvery = 0.25f;    // seconds between new spots for the door runner (the car settling, the player turning)
        constexpr float      kSteerMoved = 20.0f;    // or sooner, once the spot moved this far from the last one

        // MoveTo_Impl, which CommonLib keeps private: TESObjectREFR::MoveTo with a position of our own.
        void MoveToSpot(RE::TESObjectREFR* a_ref, const RE::ObjectRefHandle& a_target, RE::TESObjectCELL* a_cell, RE::TESWorldSpace* a_world,
            const RE::NiPoint3& a_position, const RE::NiPoint3& a_rotation)
        {
            using func_t = void (*)(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
            static REL::Relocation<func_t> func{ RELOCATION_ID(56227, 56626) };
            func(a_ref, a_target, a_cell, a_world, a_position, a_rotation);
        }

        struct Spawned
        {
            RE::ObjectRefHandle actor;
            Wanted::Unit        unit = Wanted::Unit::Guard;
            float               age = 0.0f;  // seconds since it was in (3D loaded)
            float               speedMult = 100.0f;  // a wolf's SpeedMult as last set
            Wanted::Leash       leash;              // when it's set on CJ again, fighting someone else
            bool                fighting = false;
        };

        // A pursuer spawned for the pursuit that is no longer needed: taken out a step a frame once the player can't
        // see it. Actors are only disabled: deleting them (SetDelete) left dangling ones in Skyrim's AI that crashed a
        // load, and all at once in a fight crashed too. The ground probes are deleted.
        struct Leaving
        {
            RE::ObjectRefHandle actor;
            int                 step = 0;  // 0 disable it, 1 delete it if it's no actor
            bool                purge = false;  // a copy left in a save: deleted too, actor or not, so no save keeps it
        };

        // Where walking out of a barracks door puts the player: the door's own teleport.
        struct Spot
        {
            RE::ObjectRefHandle door;  // the door outside, the move's target
            RE::TESObjectCELL*  cell = nullptr;
            RE::TESWorldSpace*  world = nullptr;
            RE::NiPoint3        position;
            RE::NiPoint3        rotation;
        };

        std::atomic<int> g_cheat{ -1 };  // a wanted cheat typed, for the next Update: 0 none, 1 two more, 2 six

        struct State
        {
            bool                     owned = false;
            Wanted::Pursuit          pursuit;
            const Wanted::Hold*      hold = nullptr;  // the hold the force comes from: where the player is, else the last crime's
            struct Bounty  // a crime faction's bounty on the player last frame, its two halves
            {
                RE::FormID    faction = 0;
                std::uint32_t violent = 0;
                std::uint32_t nonViolent = 0;
            };
            std::vector<Bounty>      bounties;
            int                      owed = 0;        // every faction's bounty together, last frame
            bool                     seeded = false;  // the bounties above are this game's
            bool                     seen = false;
            Wanted::Amnesty          amnesty;         // a bounty with no stars, forgotten unseen
            Wanted::Grudges          grudges;         // people fighting CJ, forgotten unseen
            Wanted::Gaps             gaps;            // bystanders and guards turned in a crowd
            float                    lookTimer = 0.0f;
            float                    joinTimer = 0.0f;
            float                    calmTimer = 0.0f;
            Wanted::Director         director;
            std::vector<Spawned>     spawned;
            std::vector<Leaving>     leaving;
            bool                     clearOut = false;  // the leaving go whether the player sees them or not (Skyrim has it back)
            int                      spawnTries = 0;    // turns the next spawn spot round CJ
            bool                     neverWanted = false;  // AEZAKMI was on last frame
            Wanted::StopWatch        stop;
            Wanted::Sequence         sequence;
            Wanted::Frame            frame;
            RE::TESFaction*          bustFaction = nullptr;
            RE::TESFaction*          jailAsked = nullptr;  // GoToPrison came while CJ has the player: Busted next frame
            bool                     moveAsked = false;
            bool                     arrived = false;
            bool                     movedAgain = false;  // the player was moved to the spot a second time, its ground in
            RE::NiPoint3             destination;        // where the move should put the player
            float                    settle = 0.0f;  // seconds left before the loaded world is looked at
            int                      loggedStars = 0;
            int                      shownStars = -1;  // what the HUD was last told (so the HUD demo's stars stay up)
            bool                     shownFlashing = false;
            std::uint8_t             line = 0;        // a line for CJ to say (Wanted::kLine*), taken by PlayerSync
            bool                     spared = false;  // a guard's hit was eased and left the player standing, since Vitals last asked
            RE::NiPoint3             blowFrom;        // where the last hit on the player came from (anyone's), and when
            std::chrono::steady_clock::time_point blowAt;
            float                    chasedTimer = 0.0f;  // seconds since his last chased line
            VehicleHits::DoorRun     doorRun;             // v31: the pursuer running to the door of CJ's stopped car
            std::uint32_t            recalled = 0;        // the last runner called back, kept from leaving for VehicleHits::kRecallHold
            std::chrono::steady_clock::time_point recalledAt;
            struct Bound  // an Actor script object CallActor bound itself, to unbind once the runner is done with it
            {
                RE::FormID                                id = 0;
                RE::BSTSmartPointer<RE::BSScript::Object> object;
                bool                                      releasing = false;
                std::chrono::steady_clock::time_point     at;  // when it may go
            };
            std::vector<Bound>       bound;
            float                    steerTimer = 0.0f;
            Wanted::Point            steeredTo{ 1e9f, 1e9f };  // the offset from the player he was last told
            float                    cjSpeed = 0.0f;      // how fast CJ goes across, units a second (the pose's)
            std::vector<RE::ActorHandle> downFighters;  // who fought the player as it was knocked down
            bool                     lookForLeftovers = false;  // a game loaded: copies left in it by earlier builds are looked for
            float                    leftoverSettle = 0.0f;     // seconds the loaded game has run settled
            std::chrono::steady_clock::time_point goneAt;  // when the last of ours leaving was taken out
            Knock::Rejoin            rejoin;           // and for how long they're set on it again
        } state;

        void Show(int a_stars, bool a_flashing = false)
        {
            if (a_stars != state.shownStars || a_flashing != state.shownFlashing) {
                state.shownStars = a_stars;
                state.shownFlashing = a_flashing;
                SaHud::SetWantedLevel(a_stars);
                SaHud::SetWantedFlashing(a_flashing);
            }
        }

        RE::TESFaction* FactionOf(const Wanted::Hold& a_hold) { return RE::TESForm::LookupByID<RE::TESFaction>(a_hold.faction); }

        int BountyOf(RE::PlayerCharacter* a_player, const Wanted::Hold* a_hold)
        {
            auto* faction = a_hold ? FactionOf(*a_hold) : nullptr;
            return faction ? static_cast<int>(a_player->GetViolentCrimeGoldValue(faction) + a_player->GetNonViolentCrimeGoldValue(faction)) : 0;
        }

        bool NeverWanted() { return CheatCodes::On(Cheats::SaCheat::NeverWanted); }

        const Spawned* SpawnedOf(RE::Actor* a_actor)
        {
            for (const auto& s : state.spawned) {
                if (s.actor.get().get() == a_actor) {
                    return &s;
                }
            }
            return nullptr;
        }

        bool IsSpawned(RE::Actor* a_actor) { return SpawnedOf(a_actor) != nullptr; }

        // Whether a_actor is one of the pursuit's: any hold's guard (one wanted level for all of Skyrim), or one we spawned.
        bool Pursues(RE::Actor* a_actor)
        {
            if (!a_actor || a_actor->IsDead() || a_actor->IsPlayerRef() || !a_actor->Is3DLoaded()) {
                return false;
            }
            return IsSpawned(a_actor) || a_actor->IsGuard();
        }

        // Whether a_actor forgives CJ (Wanted::Forgives): a guard, townsfolk, or unaggressive by its base.
        bool ForgivesOf(RE::Actor* a_actor)
        {
            const auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
            const bool  unaggressive = base && !base->aiData.aggression1 && !base->aiData.aggression2;
            return a_actor && Wanted::Forgives(a_actor->IsGuard(), IsSpawned(a_actor), a_actor->GetCrimeFaction() != nullptr, unaggressive);
        }

        double NowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        // The hold the player stands in: its location, or one that holds it, named after it. nullptr out of every hold.
        const Wanted::Hold* HoldHere(RE::PlayerCharacter* a_player)
        {
            for (auto* location = a_player ? a_player->GetCurrentLocation() : nullptr; location; location = location->parentLoc) {
                const char* name = location->GetName();
                for (const auto& hold : Wanted::kHolds) {
                    std::string_view word = hold.name;
                    if (word.starts_with("the ")) {
                        word.remove_prefix(4);
                    }
                    if (name && Wasted::detail::HasWord(name, word)) {
                        return &hold;
                    }
                }
            }
            return nullptr;
        }

        // Where the force comes from: the hold the player is in, else the last one's, else Whiterun.
        void PickHold(RE::PlayerCharacter* a_player)
        {
            if (const auto* here = HoldHere(a_player)) {
                if (here != state.hold) {
                    SKSE::log::info("police: the pursuit is now {}'s{}", here->name, state.hold ? std::format(" (was {}'s)", state.hold->name) : std::string());
                }
                state.hold = here;
            } else if (!state.hold) {
                state.hold = &Wanted::kHolds[0];  // Whiterun's
            }
        }

        bool Fights(RE::Actor* a_actor, RE::PlayerCharacter* a_player) { return a_actor->GetActorRuntimeData().currentCombatTarget.get().get() == a_player; }

        // One of Actor's Papyrus methods on a_actor (KeepOffsetFromActor and ClearKeepOffsetFromActor have no CommonLib
        // function), through an Actor script object bound to it first if it has none (most guards carry no script).
        template <class... Args>
        bool CallActor(RE::Actor* a_actor, const char* a_function, Args... a_args)
        {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            auto* binder = vm ? vm->GetObjectBindPolicy() : nullptr;
            if (!policy || !binder) {
                return false;
            }
            const auto handle = policy->GetHandleForObject(a_actor->GetFormType(), a_actor);
            if (handle == policy->EmptyHandle()) {
                return false;
            }
            RE::BSTSmartPointer<RE::BSScript::Object> object;
            if (!vm->FindBoundObject(handle, "Actor", object) || !object) {
                if (!vm->CreateObject("Actor", object) || !object) {
                    return false;
                }
                binder->BindObject(object, handle);
                state.bound.push_back({ a_actor->GetFormID(), object });  // ours: unbound again when his run is over
                SKSE::log::info("police: bound an Actor script object to {:08X} for {}", a_actor->GetFormID(), a_function);
            }
            auto*                                                    args = RE::MakeFunctionArguments(std::move(a_args)...);
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            return vm->DispatchMethodCall(object, a_function, args, callback);
        }

        // ---- The door runner (v31) ----

        RunOver::Build BuildOf(RE::Actor* a_actor)
        {
            auto*       race = a_actor->GetRace();
            const char* id = race ? race->GetFormEditorID() : nullptr;
            return race ? RunOver::BuildOf(id ? id : "", race->HasKeywordString("ActorTypeNPC"), race->HasKeywordString("ActorTypeDragon")) : RunOver::Build::Beast;
        }

        RE::Actor* RunnerOf(std::uint32_t a_id) { return a_id ? RE::TESForm::LookupByID<RE::Actor>(a_id) : nullptr; }

        // He puts his weapon away and runs for the door; Steer tells him where it is.
        void SendRunner(std::uint32_t a_id)
        {
            auto* actor = RunnerOf(a_id);
            if (!actor) {
                return;
            }
            actor->StopCombat();
            actor->DrawWeaponMagicHands(false);
            state.steerTimer = kSteerEvery;
            state.steeredTo = { 1e9f, 1e9f };
            SKSE::log::info("police: {} ({:08X}) puts his weapon away and runs for CJ's door", actor->GetName(), a_id);
        }

        // His run is over: back to fighting CJ while the stars last (a_fight).
        void RecallRunner(RE::PlayerCharacter* a_player, std::uint32_t a_id, bool a_gaveUp, bool a_fight)
        {
            auto* actor = RunnerOf(a_id);
            if (!actor) {
                return;
            }
            const bool cleared = CallActor(actor, "ClearKeepOffsetFromActor");
            state.recalled = a_id;  // Papyrus clears his offset later: he doesn't leave (disabled, a load's garbage) before that
            state.recalledAt = std::chrono::steady_clock::now();
            for (auto& b : state.bound) {
                if (b.id == a_id) {
                    b.releasing = true;
                    b.at = state.recalledAt + std::chrono::milliseconds(static_cast<int>(VehicleHits::kRecallHold * 1000.0f));
                }
            }
            if (a_fight && !actor->IsDead()) {
                actor->StartCombat(a_player);
            }
            SKSE::log::info("police: {} ({:08X}) {}{}", actor->GetName(), a_id, a_gaveUp ? "can't get to the door and gives up" : "stops running for the door",
                cleared ? "" : " (his offset couldn't be cleared)");
        }

        // The script objects CallActor bound whose run is over (all of them, a_all, before a load): unbound, so no object of
        // ours is left on an actor the pursuit sends away or a load clears.
        void Unbind(bool a_all)
        {
            auto*      vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(state.bound, [&](const State::Bound& a_bound) {
                if (!a_all && (!a_bound.releasing || now < a_bound.at)) {
                    return false;
                }
                if (vm && a_bound.object) {
                    vm->UnbindObject(a_bound.object);
                }
                SKSE::log::info("police: unbound the Actor script object of {:08X}{}", a_bound.id, a_all ? " (a load or a new game)" : "");
                return true;
            });
        }

        bool Bound(RE::FormID a_id)
        {
            return std::any_of(state.bound.begin(), state.bound.end(), [&](const State::Bound& a_bound) { return a_bound.id == a_id; });
        }

        void EndRun(RE::PlayerCharacter* a_player, bool a_fight)
        {
            if (const auto id = state.doorRun.Reset()) {
                RecallRunner(a_player, id, false, a_fight);
            }
        }

        // Keeps him going for a_to (world) as the player turns and the car settles, and out of the fight.
        void Steer(RE::PlayerCharacter* a_player, std::uint32_t a_id, Wanted::Point a_to, float a_delta)
        {
            auto* actor = RunnerOf(a_id);
            if (!actor) {
                return;
            }
            state.steerTimer += a_delta;
            const auto at = a_player->GetPosition();
            const auto offset = VehicleHits::LocalOffset({ a_to.x - at.x, a_to.y - at.y }, a_player->GetAngleZ());
            const bool moved = std::hypot(offset.x - state.steeredTo.x, offset.y - state.steeredTo.y) > kSteerMoved;
            if (state.steerTimer < kSteerEvery && !moved) {
                return;
            }
            state.steerTimer = 0.0f;
            if (actor->IsInCombat()) {
                actor->StopCombat();  // a hold's own guard sees CJ again and goes for him
                actor->DrawWeaponMagicHands(false);
            }
            const bool first = state.steeredTo.x > 1e8f;
            state.steeredTo = offset;
            const bool told = CallActor(actor, "KeepOffsetFromActor", static_cast<RE::Actor*>(a_player), offset.x, offset.y, 0.0f, 0.0f, 0.0f, 0.0f, 20.0f, 5.0f);
            if (first || !told) {
                const auto from = actor->GetPosition();
                SKSE::log::info("police: {:08X} runs for ({:.0f},{:.0f}), {:.0f} units off, offset ({:.0f},{:.0f}) from the player{}", a_id, a_to.x, a_to.y,
                    std::hypot(from.x - a_to.x, from.y - a_to.y), offset.x, offset.y, told ? "" : ": KeepOffsetFromActor wasn't called");
            }
        }

        void Say(std::uint8_t a_line)
        {
            state.line = a_line;
            if (a_line == Wanted::kLineChased) {
                state.chasedTimer = 0.0f;
            }
        }

        // ---- After a knock-down ----

        // Whoever fought the player as it went down and isn't fighting it now is set on it again (Knock::Rejoin). Nobody's
        // fight is stopped: stopping and starting one in a frame left Skyrim's guards out of it.
        void Rejoin(RE::PlayerCharacter* a_player)
        {
            if (!a_player || state.sequence.Running() || NeverWanted()) {
                state.rejoin.Reset();
                return;
            }
            std::vector<RE::NiPointer<RE::Actor>> back;  // not while looking: a fight started may change who fights whom
            for (const auto& handle : state.downFighters) {
                auto actor = handle.get();
                if (actor && Knock::Rejoins(!actor->IsDead(), actor->Is3DLoaded(), static_cast<bool>(actor->GetActorRuntimeData().currentCombatTarget.get()))) {
                    back.push_back(actor);
                }
            }
            int again = 0;
            for (const auto& actor : back) {
                again += actor->StartCombat(a_player) ? 1 : 0;
            }
            if (!back.empty()) {
                SKSE::log::info("police: the player is back at CJ: {} of the {} who fought it had dropped it, {} back on it", back.size(), state.downFighters.size(),
                    again);
            }
            if (!state.rejoin.Active()) {
                state.downFighters.clear();
            }
        }

        // ---- Calling the fight off ----

        // Every guard and everyone of ours fighting the player stops; a_alarm also calls off everyone's alarm
        // (Skyrim's own, which brings bandits round too, so only once at a time).
        void Calm(RE::PlayerCharacter* a_player, bool a_alarm)
        {
            int calmed = 0;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                if (a_alarm) {
                    lists->StopCombatAndAlarmOnActor(a_player, false);
                }
                lists->ForEachHighActor([&](RE::Actor* a_actor) {
                    if (a_actor && !a_actor->IsDead() && Fights(a_actor, a_player) && (a_actor->IsGuard() || IsSpawned(a_actor))) {
                        a_actor->StopCombat();
                        calmed++;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            if (a_alarm) {
                a_player->StopCombat();
            }
            if (calmed > 0) {
                SKSE::log::info("police: {} pursuer(s) stop fighting the player", calmed);
            }
        }

        // ---- The bounties ----

        // Every crime faction the player has a bounty with, or had (the nine holds', the Legion's, the Stormcloaks', a
        // stronghold's...): Skyrim's own list of them on the player.
        template <class F>
        void ForEachCrimeFaction(RE::PlayerCharacter* a_player, F&& a_do)
        {
            std::vector<RE::TESFaction*> factions;  // not while walking Skyrim's map: clearing a bounty may change it
            for (const auto& [faction, gold] : a_player->GetPlayerRuntimeData().crimeGoldMap) {
                if (faction) {
                    factions.push_back(const_cast<RE::TESFaction*>(faction));
                }
            }
            for (auto* faction : factions) {
                a_do(faction);
            }
        }

        // Every bounty cleared and the guards' and ours' fight with the player called off: nobody remembers (the stars lost,
        // Busted, Wasted, the fine paid, AEZAKMI, a bounty forgotten unseen). a_alarm also calls off everyone's alarm, for
        // the ends that are a clean slate (Busted, Wasted, the fine, the cheats); not when the stars or a bounty just
        // wear off, or a bandit CJ is fighting would stop too. Returns the gold cleared.
        int ClearBounties(RE::PlayerCharacter* a_player, const char* a_why, bool a_alarm = true)
        {
            int cleared = 0;
            int factions = 0;
            ForEachCrimeFaction(a_player, [&](RE::TESFaction* a_faction) {
                if (const auto gold = a_player->GetCrimeGoldValue(a_faction); gold > 0) {
                    cleared += static_cast<int>(gold);
                    factions++;
                    a_player->ClearAllCrimeGold(a_faction);
                }
            });
            Calm(a_player, a_alarm);
            state.seeded = false;  // the cleared bounties are no crime the other way either
            state.owed = 0;
            state.amnesty.Reset();
            state.grudges.Reset();
            SKSE::log::info("police: {}: every bounty cleared ({} gold in {} faction(s)), nobody remembers", a_why, cleared, factions);
            return cleared;
        }

        // Reads every crime faction's bounty: one that went up is a crime, which adds to the one pursuit wherever it was.
        // AEZAKMI puts it back and calls the fight off.
        void WatchCrimes(RE::PlayerCharacter* a_player)
        {
            const bool          never = NeverWanted();
            std::vector<State::Bounty> now;
            int                 owed = 0;
            ForEachCrimeFaction(a_player, [&](RE::TESFaction* a_faction) {
                State::Bounty b{ a_faction->GetFormID(), a_player->GetViolentCrimeGoldValue(a_faction), a_player->GetNonViolentCrimeGoldValue(a_faction) };
                State::Bounty last{ b.faction, 0, 0 };
                for (const auto& old : state.bounties) {
                    if (old.faction == b.faction) {
                        last = old;
                        break;
                    }
                }
                const int before = static_cast<int>(last.violent + last.nonViolent);
                const int total = static_cast<int>(b.violent + b.nonViolent);
                if (state.seeded && total > before) {
                    const auto* hold = Wanted::HoldOf(b.faction);
                    if (never) {
                        a_player->SetCrimeGoldValue(a_faction, true, last.violent);
                        a_player->SetCrimeGoldValue(a_faction, false, last.nonViolent);
                        Calm(a_player, true);
                        SKSE::log::info("police: AEZAKMI: a crime against {} ({} gold) undone, the bounty stays {}", a_faction->GetName(), total - before, before);
                        b = last;
                    } else {
                        if (hold) {
                            state.hold = hold;
                        }
                        const int stars = state.pursuit.Crime(total - before, false);
                        SKSE::log::info("police: a crime against {}{}: its bounty is {} ({} more), heat {:.0f}, {} star(s)", a_faction->GetName(),
                            hold ? std::format(" ({})", hold->name) : std::string(), total, total - before, state.pursuit.Heat(), stars);
                    }
                }
                owed += static_cast<int>(b.violent + b.nonViolent);
                now.push_back(b);
            });
            state.bounties = std::move(now);
            state.owed = owed;
            state.seeded = true;
        }

        // ---- Who sees CJ ----

        // Whether any pursuer (any hold's guard, or ours) sees the player; while no stars show, a guard of a hold with a
        // bounty on the player fighting it brings a star or two back. Also who holds a grudge against CJ (Wanted::Grudges),
        // and, while stars show, bystanders fighting the pursuit and guards fighting bystanders (Wanted::TurnOf).
        void Look(RE::PlayerCharacter* a_player)
        {
            state.seen = false;
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            const auto   feet = a_player->GetPosition();
            const bool   never = NeverWanted();
            const int    stars = state.pursuit.Stars();
            const double now = NowSeconds();
            std::vector<Wanted::Grudges::Holder>                         holders;
            std::vector<RE::NiPointer<RE::Actor>>                        holderActors;
            std::vector<std::pair<RE::NiPointer<RE::Actor>, Wanted::Turn>> turns;  // not while looking: a fight changed may change who fights whom
            lists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (!a_actor || a_actor->IsDead() || !a_actor->Is3DLoaded() || a_actor->GetPosition().GetDistance(feet) > Wanted::kSightRange) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                const auto target = a_actor->GetActorRuntimeData().currentCombatTarget.get();
                const bool pursues = Pursues(a_actor);
                bool       unused = false;
                if (stars == 0 && a_actor->IsGuard() && target.get() == a_player) {
                    auto*       faction = a_actor->GetCrimeFaction();
                    const auto* hold = faction ? Wanted::HoldOf(faction->GetFormID()) : nullptr;
                    const int   bounty = BountyOf(a_player, hold);
                    if (hold && bounty > 0 && !never) {
                        state.hold = hold;
                        state.pursuit.Recognised(bounty, false);
                        SKSE::log::info("police: {} ({:08X}) of {} recognises CJ: the bounty is {}, {} star(s)", a_actor->GetName(), a_actor->GetFormID(), hold->name,
                            bounty, state.pursuit.Stars());
                    }
                }
                if (!state.seen && pursues && a_actor->HasLineOfSight(a_player, unused)) {
                    state.seen = true;
                }
                if (!target) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (target.get() == a_player) {
                    if (!(stars > 0 && pursues) && ForgivesOf(a_actor)) {
                        holders.push_back({ a_actor->GetFormID(), a_actor->HasLineOfSight(a_player, unused) });
                        holderActors.emplace_back(a_actor);
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (stars > 0 && a_actor->GetFormID() != state.doorRun.Runner()) {
                    auto*      other = target.get();
                    const auto turn = Wanted::TurnOf(pursues, ForgivesOf(a_actor), Pursues(other), ForgivesOf(other), false);
                    // Ours fighting someone else are the Leash's (Lead), gently.
                    if (turn != Wanted::Turn::None && !(turn == Wanted::Turn::OnCj && IsSpawned(a_actor)) && state.gaps.Take(a_actor->GetFormID(), now)) {
                        turns.emplace_back(a_actor, turn);
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            int calledOff = 0;
            int onCj = 0;
            for (const auto& [actor, turn] : turns) {
                if (turn == Wanted::Turn::CallOff) {
                    actor->StopCombat();
                    calledOff++;
                } else if (actor->StartCombat(a_player)) {
                    onCj++;
                }
            }
            if (!turns.empty()) {
                SKSE::log::info("police: in the crowd: {} bystander(s) stop fighting the pursuit, {} guard(s) back on CJ", calledOff, onCj);
            }
            for (const auto id : state.grudges.Tick(kLookEvery, holders)) {
                for (std::size_t i = 0; i < holders.size(); i++) {
                    if (holders[i].id == id) {
                        holderActors[i]->StopCombat();
                        SKSE::log::info("police: {} ({:08X}) hasn't seen CJ for {:.0f} s and forgets the fight", holderActors[i]->GetName(), id, Wanted::kForgetAfter);
                    }
                }
            }
        }

        // ---- Ours leaving ----

        // Everyone spawned stops fighting and leaves: SweepLeaving takes them out once the player can't see them.
        void Dismiss(const char* a_why, bool a_clearOut = false)
        {
            state.clearOut = state.clearOut || a_clearOut;
            state.director.Reset();
            if (state.spawned.empty()) {
                return;
            }
            state.rejoin.Reset();  // the next pursuit starts clean: nobody of this one is set on the player again
            state.downFighters.clear();
            for (auto& s : state.spawned) {
                if (auto ref = s.actor.get()) {
                    if (auto* actor = ref->As<RE::Actor>(); actor && !actor->IsDead()) {
                        if (s.unit == Wanted::Unit::Wolf) {
                            actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, 0.0f);  // or it goes for CJ again
                        }
                        actor->StopCombat();
                    }
                }
                state.leaving.push_back({ s.actor, 0 });
            }
            SKSE::log::info("police: {} spawned pursuer(s) leave ({})", state.spawned.size(), a_why);
            state.spawned.clear();
        }

        // Whether a leaving pursuer can go now: dead, far off, out of the player's sight, or everyone has to.
        bool CanGo(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref)
        {
            if (!a_ref || state.clearOut || !a_player || !a_ref->Is3DLoaded()) {
                return true;
            }
            if (a_ref->GetPosition().GetDistance(a_player->GetPosition()) > kGoneRange) {
                return true;
            }
            bool unused = false;
            return !a_player->HasLineOfSight(a_ref, unused);
        }

        // Before one of ours goes: nobody is left fighting it (Skyrim drops a disabled actor later, and a fight with it
        // kept in someone's AI is a freed actor then), and it fights nobody.
        void Unlink(RE::Actor* a_gone)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!a_gone || !lists) {
                return;
            }
            std::vector<RE::NiPointer<RE::Actor>> fighting;
            lists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (a_actor && a_actor != a_gone && a_actor->GetActorRuntimeData().currentCombatTarget.get().get() == a_gone) {
                    fighting.emplace_back(a_actor);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            for (const auto& actor : fighting) {
                actor->StopCombat();
            }
            if (!a_gone->IsDead()) {
                a_gone->StopCombat();
            }
            if (!fighting.empty()) {
                SKSE::log::info("police: {} stop fighting {:08X} before it goes", fighting.size(), a_gone->GetFormID());
            }
        }

        // One step a frame of the first leaving pursuer that can go (from RunFrame, on the main thread).
        void SweepLeaving()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            for (auto it = state.leaving.begin(); it != state.leaving.end(); ++it) {
                auto ref = it->actor.get();
                if (it->step == 0 && !CanGo(player, ref.get())) {
                    continue;
                }
                if (it->step == 0 && ref && (Bound(ref->GetFormID()) || (ref->GetFormID() == state.recalled &&
                    std::chrono::duration<float>(std::chrono::steady_clock::now() - state.recalledAt).count() < VehicleHits::kRecallHold))) {
                    continue;  // the door runner, whose offset Papyrus hasn't cleared yet, or who still has our script object
                }
                if (ref) {
                    if (it->step == 0) {
                        Unlink(ref->As<RE::Actor>());
                        ref->Disable();
                    } else if (!ref->As<RE::Actor>() || it->purge) {
                        ref->SetDelete(true);  // a ground probe, or a copy left in a save
                    }
                }
                if (++it->step >= 2) {
                    state.leaving.erase(it);
                    if (state.leaving.empty()) {
                        state.goneAt = std::chrono::steady_clock::now();
                    }
                }
                break;
            }
            if (state.leaving.empty()) {
                state.clearOut = false;
            }
        }

        // Ours that died or got lost leave (out of sight); the guards' dragon brought down is a lot of heat.
        void Prune(RE::PlayerCharacter* a_player)
        {
            const auto feet = a_player->GetPosition();
            for (auto it = state.spawned.begin(); it != state.spawned.end();) {
                auto  ref = it->actor.get();
                auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                const bool dead = !actor || actor->IsDead();
                const bool lost = actor && actor->GetPosition().GetDistance(feet) > kLostRange;
                if (!dead && !lost) {
                    ++it;
                    continue;
                }
                if (dead && actor && it->unit == Wanted::Unit::Dragon) {
                    state.pursuit.AddHeat(kDragonHeat);
                    SKSE::log::info("police: the guards' dragon is down: heat {:.0f}, {} star(s)", state.pursuit.Heat(), state.pursuit.Stars());
                }
                state.leaving.push_back({ it->actor, 0 });
                it = state.spawned.erase(it);
            }
        }

        // ---- The response ----

        // Every hold's guards round about who aren't fighting yet join: one wanted level for all of Skyrim.
        void CallGuards(RE::PlayerCharacter* a_player, float a_radius)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists || a_radius <= 0.0f) {
                return;
            }
            const auto feet = a_player->GetPosition();
            int        joined = 0;
            lists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (Pursues(a_actor) && !IsSpawned(a_actor) && !Fights(a_actor, a_player) && a_actor->GetFormID() != state.doorRun.Runner() &&
                    a_actor->GetPosition().GetDistance(feet) <= a_radius) {  // not the one running for the door
                    joined += a_actor->StartCombat(a_player) ? 1 : 0;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            if (joined > 0) {
                SKSE::log::info("police: {} guard(s) within {:.0f} units join the chase", joined, a_radius);
            }
        }

        // A guard of the hold about, whose base ours copy (the hold's guards as the war left them), else the table's.
        bool Unique(RE::TESBoundObject* a_base)
        {
            auto* npc = a_base ? a_base->As<RE::TESNPC>() : nullptr;
            return npc && npc->IsUnique();
        }

        // The hold's guard record (Wanted::Spawnable: the game's own, never a guard about's runtime base or a unique one).
        RE::TESBoundObject* GuardBase(RE::PlayerCharacter*) { return RE::TESForm::LookupByID<RE::TESBoundObject>(state.hold->guard); }

        // A levelled list (the soldiers' LCharSoldier*) as one of its NPCs (Wanted::PickLeveled, a few lists deep): a reference
        // made straight from a list is no actor. Anything else as it is.
        RE::TESBoundObject* Resolve(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_base)
        {
            static unsigned roll = 0;
            for (int depth = 0; depth < 4 && a_base; depth++) {
                auto* list = a_base->As<RE::TESLevCharacter>();
                if (!list) {
                    return a_base->Is(RE::FormType::NPC) ? a_base : nullptr;
                }
                std::vector<std::uint16_t> levels;
                for (const auto& entry : list->entries) {
                    levels.push_back(entry.level);
                }
                const int i = Wanted::PickLeveled(levels.data(), static_cast<int>(levels.size()), a_player->GetLevel(), roll++);
                a_base = i >= 0 && list->entries[static_cast<std::size_t>(i)].form ? list->entries[static_cast<std::size_t>(i)].form->As<RE::TESBoundObject>() : nullptr;
            }
            return a_base && a_base->Is(RE::FormType::NPC) ? a_base : nullptr;
        }

        RE::TESBoundObject* BaseFor(RE::PlayerCharacter* a_player, Wanted::Unit a_unit)
        {
            const bool imperial = state.hold->side == Wanted::Side::Imperial;
            std::uint32_t id = 0;
            switch (a_unit) {
            case Wanted::Unit::Wolf: id = Wanted::WolfFor(state.pursuit.Stars()); break;
            case Wanted::Unit::Captain: id = imperial ? Wanted::kCaptainImperial : Wanted::kCaptainSons; break;
            case Wanted::Unit::Wizard: id = imperial ? Wanted::kWizardImperial : Wanted::kWizardSons; break;
            case Wanted::Unit::Soldier: id = imperial ? Wanted::kSoldiersImperial : Wanted::kSoldiersSons; break;
            case Wanted::Unit::Dragon: id = Wanted::kDragon; break;
            case Wanted::Unit::Giant: id = Wanted::kGiant; break;
            case Wanted::Unit::Mammoth: id = Wanted::kMammoth; break;
            default: return GuardBase(a_player);
            }
            return Resolve(a_player, RE::TESForm::LookupByID<RE::TESBoundObject>(id));
        }

        // The hold's barracks door's spot outside.
        bool BarracksSpot(const Wanted::Hold& a_hold, Spot& a_out)
        {
            auto* door = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_hold.barracks);
            auto* teleport = door ? door->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
            auto* data = teleport ? teleport->teleportData : nullptr;
            auto  outside = data ? data->linkedDoor.get() : RE::NiPointer<RE::TESObjectREFR>();
            if (!outside) {
                return false;
            }
            a_out.door = outside->CreateRefHandle();
            a_out.cell = outside->GetParentCell();
            a_out.world = outside->GetWorldspace();
            a_out.position = data->position;
            a_out.rotation = data->rotation;
            return true;
        }

        Wanted::Point Flat(const RE::NiPoint3& a_at) { return { a_at.x, a_at.y }; }

        // A new reference of a_base at a_at, as it is.
        RE::NiPointer<RE::TESObjectREFR> Create(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_base, const RE::NiPoint3& a_at)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* cell = a_player->GetParentCell();
            if (!data || !a_base || !cell) {
                return {};
            }
            return data
                ->CreateReferenceAtLocation(a_base, a_at, RE::NiPoint3(0.0f, 0.0f, a_player->GetAngleZ()), cell, a_player->GetWorldspace(), nullptr, nullptr,
                    RE::ObjectRefHandle(), false, true)
                .get();
        }

        // The nearest navmesh to a_at, found with a marker (it has no AI, so it can go at once; an actor created only to be
        // taken out again left a dangling one in Skyrim's AI that crashed the next load).
        bool GroundAt(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, RE::NiPoint3& a_out)
        {
            auto marker = Create(a_player, RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker), a_at);
            if (!marker) {
                return false;
            }
            const bool found = marker->MoveToNearestNavmesh();
            a_out = marker->GetPosition();
            state.leaving.push_back({ marker->CreateRefHandle(), 0 });
            // The nearest navmesh may be far off (another island of it) or in a cell that isn't loaded: Skyrim's path
            // search to and from such a spot is what crashed. Only a spot on loaded navmesh close to the one asked for.
            auto*       cell = marker->GetParentCell();
            const float off = std::hypot(a_out.x - a_at.x, a_out.y - a_at.y);
            if (found && (off > kSnapFar || !cell || !cell->IsAttached())) {
                SKSE::log::info("police: a spawn spot given up (its navmesh {:.0f} units off{})", off, cell && cell->IsAttached() ? "" : ", in a cell not loaded");
                return false;
            }
            return found;
        }

        // One of a_base at a_at (on the nearest navmesh when a_snap), or nothing when the spot turns out in view or on a
        // tower or a ledge: those are found out before anything is made there.
        RE::TESObjectREFR* Place(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_base, const RE::NiPoint3& a_at, bool a_snap, bool a_anyHeight)
        {
            const auto   feet = a_player->GetPosition();
            RE::NiPoint3 at = a_at;
            if (a_snap && !GroundAt(a_player, a_at, at)) {
                SKSE::log::info("police: a spawn spot given up (no navmesh)");
                return nullptr;
            }
            const bool tooHigh = !a_anyHeight && std::abs(at.z - feet.z) > kSpawnRise;
            const bool seen = Wanted::InView(Wanted::CameraOf(Flat(feet), a_player->GetAngleZ()), a_player->GetAngleZ(), Flat(at));
            if (tooHigh || seen) {
                SKSE::log::info("police: a spawn spot given up ({})", tooHigh ? "a tower or a ledge" : "in view");
                return nullptr;
            }
            return Create(a_player, a_base, at).get();
        }

        // Where the next of ours comes from: half of them out of the city's barracks when it's close and out of view,
        // else round CJ, behind him first.
        RE::TESObjectREFR* PlaceSomewhere(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_base, Wanted::Unit a_unit, const char*& a_from)
        {
            const auto  feet = a_player->GetPosition();
            const float heading = a_player->GetAngleZ();
            if (a_unit == Wanted::Unit::Dragon) {
                a_from = "far behind";
                const auto spot = Wanted::SpawnSpot(Flat(feet), heading, 0, Wanted::kDragonDistance);
                return Place(a_player, a_base, RE::NiPoint3(spot.x, spot.y, feet.z + 1500.0f), false, true);
            }
            Spot barracks;
            if (!Wanted::OpenWorldOnly(a_unit) && state.spawnTries % 2 == 0 && BarracksSpot(*state.hold, barracks) && barracks.world == a_player->GetWorldspace() &&
                barracks.position.GetDistance(feet) < kBarracksRange &&
                !Wanted::InView(Wanted::CameraOf(Flat(feet), heading), heading, Flat(barracks.position))) {
                state.spawnTries++;
                a_from = "the barracks";
                return Place(a_player, a_base, barracks.position, false, true);
            }
            for (int i = 0; i < kSpawnTries; ++i) {
                const auto spot = Wanted::SpawnSpot(Flat(feet), heading, state.spawnTries++);
                if (Wanted::InView(Wanted::CameraOf(Flat(feet), heading), heading, spot)) {
                    continue;
                }
                if (auto* ref = Place(a_player, a_base, RE::NiPoint3(spot.x, spot.y, feet.z + 50.0f), true, false)) {
                    a_from = "round CJ";
                    return ref;
                }
            }
            return nullptr;
        }

        // One of ours joins the hold's guards' side (Wanted::Enlist): their factions and the hold's crime faction, its own
        // left, aggressive only to enemies. Then no giant, dragon or guard of ours turns on another, or on the hold's guards.
        void Enlist(RE::Actor* a_actor)
        {
            auto* guard = RE::TESForm::LookupByID<RE::TESNPC>(state.hold->guard);
            std::vector<std::uint32_t> guards;
            if (guard) {
                for (const auto& rank : guard->factions) {
                    if (rank.faction && rank.rank >= 0) {
                        guards.push_back(rank.faction->GetFormID());
                    }
                }
            }
            std::vector<std::uint32_t> own;
            a_actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t a_rank) {
                if (a_faction && a_rank >= 0) {
                    own.push_back(a_faction->GetFormID());
                }
                return false;
            });
            const auto plan = Wanted::Enlist(own, guards, state.hold->faction);
            for (const auto id : plan.leave) {
                if (auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(id)) {
                    a_actor->RemoveFromFaction(faction);
                }
            }
            for (const auto id : plan.join) {
                if (auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(id)) {
                    a_actor->AddToFaction(faction, 0);
                }
            }
            a_actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, Wanted::kOursAggression);
            SKSE::log::info("police: {:08X} enlisted: {} faction(s) of {}'s guards joined, {} of its own left", a_actor->GetFormID(), plan.join.size(),
                state.hold->name, plan.leave.size());
        }

        bool Spawn(RE::PlayerCharacter* a_player, Wanted::Unit a_unit)
        {
            auto* base = BaseFor(a_player, a_unit);
            if (base && !Wanted::Spawnable(base->GetFormID(), Unique(base))) {
                SKSE::log::warn("police: {:08X} for a {} is a runtime or unique base: never spawned, the hold's guard instead", base->GetFormID(),
                    Wanted::NameOf(a_unit));
                base = GuardBase(a_player);
                a_unit = Wanted::Unit::Guard;
            }
            if (!base && a_unit != Wanted::Unit::Guard) {
                SKSE::log::info("police: no {} to send for {}, a guard instead", Wanted::NameOf(a_unit), state.hold->name);
                a_unit = Wanted::Unit::Guard;
                base = BaseFor(a_player, a_unit);
            }
            const char* from = "?";
            auto*       ref = base ? PlaceSomewhere(a_player, base, a_unit, from) : nullptr;
            auto*       actor = ref ? ref->As<RE::Actor>() : nullptr;
            if (!actor) {
                if (ref) {
                    state.leaving.push_back({ ref->CreateRefHandle(), 0 });  // made, but no actor: it goes again
                }
                SKSE::log::info("police: no spot for a {} of {} (base {:08X}{})", Wanted::NameOf(a_unit), state.hold->name, base ? base->GetFormID() : 0,
                    ref ? ", and it made no actor" : "");
                return false;
            }
            Enlist(actor);
            Spawned s;
            s.actor = actor->CreateRefHandle();
            s.unit = a_unit;
            if (a_unit == Wanted::Unit::Wolf) {
                // The hold's tamed wolves: out of their own factions (predators, enemies of the guards), and
                // aggressive to enemies only (a wild wolf goes for anyone about).
                if (auto* npc = actor->GetActorBase()) {
                    for (const auto& rank : npc->factions) {
                        if (rank.faction) {
                            actor->AddToFaction(rank.faction, -1);  // rank -1 is out, as Papyrus's RemoveFromFaction
                        }
                    }
                }
                actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, 1.0f);
            }
            if (!Wanted::OpenWorldOnly(a_unit) && a_unit != Wanted::Unit::Guard) {
                // The hold's own (wolves, the captain, wizards, soldiers) take the factions of the guards ours copy, so
                // they're all allies through the guards' civil war and guard factions, which are allied to themselves.
                // The crime faction isn't, and the captain isn't in his side's civil war faction: a stray blow between
                // him and a wolf, only neutral to each other, started a fight the guards joined on the wolf's side.
                int   took = 0;
                auto* guard = GuardBase(a_player);
                if (auto* guardNpc = guard ? guard->As<RE::TESNPC>() : nullptr) {
                    for (const auto& rank : guardNpc->factions) {
                        if (rank.faction && rank.rank >= 0) {
                            actor->AddToFaction(rank.faction, rank.rank);
                            took++;
                        }
                    }
                }
                SKSE::log::info("police: {} {:08X} takes the {} faction(s) of guard base {:08X}", Wanted::NameOf(a_unit), actor->GetFormID(), took,
                    guard ? guard->GetFormID() : 0);
            }
            const auto at = actor->GetPosition();
            SKSE::log::info("police: a {} for {} ({:08X} of base {:08X}) comes from {} at ({:.0f},{:.0f},{:.0f}), {:.0f} units from CJ", Wanted::NameOf(a_unit),
                state.hold->name, actor->GetFormID(), base->GetFormID(), from, at.x, at.y, at.z, at.GetDistance(a_player->GetPosition()));
            state.spawned.push_back(s);
            return true;
        }

        // A wolf's speed by how far behind CJ it is and how fast he goes (Wanted::ChaseSpeedMult). Skyrim only takes a new
        // SpeedMult up once something else about its movement changes, so its carry weight is nudged after.
        void Pace(RE::Actor* a_wolf, Spawned& a_s, const RE::NiPoint3& a_feet)
        {
            const float mult = Wanted::ChaseSpeedMult(a_wolf->GetPosition().GetDistance(a_feet), state.cjSpeed);
            if (mult == a_s.speedMult) {
                return;
            }
            auto* values = a_wolf->AsActorValueOwner();
            values->SetActorValue(RE::ActorValue::kSpeedMult, mult);
            values->ModActorValue(RE::ACTOR_VALUE_MODIFIERS::kPermanent, RE::ActorValue::kCarryWeight, 0.1f);
            values->ModActorValue(RE::ACTOR_VALUE_MODIFIERS::kPermanent, RE::ActorValue::kCarryWeight, -0.1f);
            if ((mult > 100.0f) != (a_s.speedMult > 100.0f)) {
                SKSE::log::info("police: wolf {:08X} at {:.0f} units, CJ at {:.0f} a second: speed {:.0f}%", a_wolf->GetFormID(), a_wolf->GetPosition().GetDistance(a_feet),
                    state.cjSpeed, mult);
            }
            a_s.speedMult = mult;
        }

        // Everyone of ours goes for CJ once in, and wolves keep his pace.
        void Lead(RE::PlayerCharacter* a_player, float a_delta)
        {
            const auto feet = a_player->GetPosition();
            for (auto& s : state.spawned) {
                auto  actorRef = s.actor.get();
                auto* actor = actorRef ? actorRef->As<RE::Actor>() : nullptr;
                if (!actor || !actor->Is3DLoaded()) {
                    continue;
                }
                s.age += a_delta;
                if (!s.fighting && s.age >= kFightAfter) {
                    s.fighting = true;
                    Enlist(actor);  // again now it's in: a levelled or templated one has its factions only once its base is built
                    const bool fights = actor->StartCombat(a_player);
                    SKSE::log::info("police: {} {:08X} goes for CJ ({})", Wanted::NameOf(s.unit), actor->GetFormID(), fights ? "fighting" : "won't fight");
                }
                if (s.unit == Wanted::Unit::Wolf) {
                    Pace(actor, s, feet);
                }
                // Ours are after CJ: a giant swatting at the dragon, or a dragon burning guards, is set on him again, gently
                // (Wanted::Leash: after a while of it, never more often than every few seconds, no fight stopped).
                if (s.fighting && actor->GetFormID() != state.doorRun.Runner()) {  // not the one running for the door
                    const auto target = actor->GetActorRuntimeData().currentCombatTarget.get();
                    const auto on = target ? (target.get() == a_player ? Wanted::Target::Cj : Wanted::Target::Other) : Wanted::Target::None;
                    if (s.leash.Tick(a_delta, on)) {
                        const bool fights = actor->StartCombat(a_player);
                        SKSE::log::info("police: {} {:08X} {}, set on CJ again{}", Wanted::NameOf(s.unit), actor->GetFormID(),
                            target ? std::format("fights {:08X}", target->GetFormID()) : std::string("fights nobody"), fights ? "" : " (refused)");
                    }
                }
            }
        }

        void Respond(RE::PlayerCharacter* a_player, float a_delta, bool a_scene)
        {
            const int stars = state.pursuit.Stars();
            if (stars == 0 || !state.hold) {
                Dismiss("the stars are gone");
                return;
            }
            state.joinTimer += a_delta;
            if (state.joinTimer >= kJoinEvery) {
                state.joinTimer = 0.0f;
                CallGuards(a_player, Wanted::JoinRadius(stars));
            }
            Prune(a_player);
            Lead(a_player, a_delta);
            auto* cell = a_player->GetParentCell();
            if (a_scene || !cell || cell->IsInteriorCell()) {
                return;
            }
            Wanted::Counts counts{};
            for (const auto& s : state.spawned) {
                counts[static_cast<std::size_t>(s.unit)]++;
            }
            if (state.director.Tick(a_delta, stars, Wanted::TotalOf(counts))) {
                const auto* world = a_player->GetWorldspace();
                const bool  open = world && world->GetFormID() == Wanted::kTamriel;
                const auto  force = Wanted::ForceFor(stars);
                if (!Spawn(a_player, Wanted::NextUnit(force, counts, open))) {
                    state.director.Retry();
                }
            }
        }

        // AEZAKMI typed: every bounty cleared, the fight called off, the stars gone.
        void ForgiveAll(RE::PlayerCharacter* a_player)
        {
            ClearBounties(a_player, "AEZAKMI");
            state.pursuit.Clear();
            Dismiss("AEZAKMI", true);  // all of ours go at once, seen or not
        }

        // ---- Busted ----

        void EndShow()
        {
            SaHud::HideBigMessage();
            SaHud::FadeTo(0.0f, 0.0f);
        }

        void Begin(RE::PlayerCharacter* a_player, Wanted::Arrest a_arrest, RE::TESFaction* a_faction)
        {
            state.bustFaction = a_faction;
            state.frame = state.sequence.Start(a_arrest);
            state.moveAsked = state.arrived = state.movedAgain = false;
            state.stop.Reset();
            state.calmTimer = 0.0f;
            EndRun(a_player, false);
            Calm(a_player, true);  // nobody shoots an arrested man
            Dismiss("Busted");
            Say(Wanted::kLineArrested);
            static constexpr const char* kHow[] = { "on foot, his hands up", "dragged out of the car", "knocked off his bike" };
            SKSE::log::info("police: BUSTED by {} ({}), bounty {}, {} gold", a_faction ? a_faction->GetName() : "?", kHow[static_cast<int>(a_arrest)],
                a_faction ? a_player->GetCrimeGoldValue(a_faction) : 0, a_player->GetGoldAmount());
        }

        // The hold Busted takes CJ to: the arresting guard's, else (a Legion soldier, a Stormcloak) the pursuit's.
        const Wanted::Hold* BustHold()
        {
            const auto* hold = state.bustFaction ? Wanted::HoldOf(state.bustFaction->GetFormID()) : nullptr;
            return hold ? hold : state.hold;
        }

        // Out of the hold's barracks door (its teleport's spot outside), else to its crime faction's exterior jail marker.
        void MoveToBarracks(RE::PlayerCharacter* a_player)
        {
            state.moveAsked = state.arrived = false;
            const auto* hold = BustHold();
            Spot        spot;
            if (hold && BarracksSpot(*hold, spot)) {
                state.destination = spot.position;
                MoveToSpot(a_player, spot.door, spot.cell, spot.world, spot.position, spot.rotation);
                state.moveAsked = true;
                SKSE::log::info("police: the player is taken out of {} (door {:08X}) at ({:.0f},{:.0f},{:.0f})", Wanted::BarracksName(*hold), hold->barracks,
                    spot.position.x, spot.position.y, spot.position.z);
                return;
            }
            auto* marker = state.bustFaction ? state.bustFaction->crimeData.factionJailMarker : nullptr;
            if (!marker) {
                SKSE::log::warn("police: no barracks to take the player to, it wakes where it was");
                return;
            }
            state.destination = marker->GetPosition();
            a_player->MoveTo(marker);
            state.moveAsked = true;
            SKSE::log::info("police: the player is taken to {}'s jail marker {:08X}", state.bustFaction->GetName(), marker->GetFormID());
        }

        // The fine from the gold (all of it if short): every faction's bounty, which no pursuit outlasts. Every bounty
        // cleared, the stars gone, everyone's fight over.
        void Release(RE::PlayerCharacter* a_player)
        {
            const auto* hold = BustHold();
            int         bounty = 0;
            ForEachCrimeFaction(a_player, [&](RE::TESFaction* a_faction) { bounty += static_cast<int>(a_player->GetCrimeGoldValue(a_faction)); });
            const int fine = Wanted::FineFor(bounty, a_player->GetGoldAmount());
            if (fine > 0) {
                if (auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold)) {
                    a_player->RemoveItem(gold, fine, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                }
            }
            ClearBounties(a_player, "Busted");
            state.pursuit.Clear();
            Dismiss("Busted", true);
            auto* cell = a_player->GetParentCell();
            SKSE::log::info("police: Busted is paid: a {} gold fine of a {} bounty, in {:08X}{}", fine, bounty, cell ? cell->GetFormID() : 0,
                state.moveAsked && !state.arrived ? " (the barracks never opened: where it was)" : "");
            SaHud::HideBigMessage();
            const std::string where = hold ? std::format("You were let go outside {}.", Wanted::BarracksName(*hold)) : std::string("You were let go.");
            SaHud::ShowHelp(fine > 0 ? std::format("{} The guards took {} gold for the bounty.", where, fine) : where);
        }

        void Run(RE::PlayerCharacter* a_player, float a_delta, bool a_cjDrawn)
        {
            // Nobody keeps fighting an arrested man (the guards Skyrim sends on, a dragon coming in).
            state.calmTimer += a_delta;
            if (state.frame.active && state.calmTimer >= kCalmEvery) {
                state.calmTimer = 0.0f;
                Calm(a_player, false);
            }
            // A move can drop the player under the ground while the cell loads: once there, it is moved onto its spot
            // again, the ground in.
            if (state.moveAsked && state.arrived && a_cjDrawn && !state.movedAgain && a_player->GetPosition().GetDistance(state.destination) > kOnSpot) {
                state.movedAgain = true;
                SKSE::log::info("police: the player came out {:.0f} units from its spot, moved there again", a_player->GetPosition().GetDistance(state.destination));
                MoveToBarracks(a_player);
            }
            const bool arrived = !state.moveAsked || (state.arrived && a_cjDrawn);
            state.frame = state.sequence.Tick(a_delta, arrived);
            SaHud::FadeTo(state.frame.fade, 0.0f);
            switch (state.frame.action) {
            case Wanted::Action::Busted:
                SaHud::ShowBigMessage(SaHud::BigMessage::Busted, Wanted::TextSeconds() - 0.65f);  // less its fade in: gone as the screen is black
                SKSE::log::info("police: \"busted\"");
                break;
            case Wanted::Action::Move: MoveToBarracks(a_player); break;
            case Wanted::Action::Release: Release(a_player); break;
            case Wanted::Action::Done:
                EndShow();
                SKSE::log::info("police: Busted is over");
                break;
            default: break;
            }
        }

        // The vehicle CJ is in: nearly stopped long enough with one of the pursuit at the driver's door (at him, on a bike).
        // v31: the nearest of them is sent running there (VehicleHits::DoorRun).
        bool CheckVehicle(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, float a_delta, Wanted::Bust& a_bust)
        {
            const bool inVehicle = a_pose && (a_pose->flags & Protocol::kPlayerInVehicle) && a_pose->vehicleModel;
            a_bust = inVehicle ? VehicleHits::BustNow(Wanted::BustOf(VehicleKinds::KindOf(a_pose->vehicleModel)), (a_pose->flags & Protocol::kPlayerGettingInOut) != 0)
                               : Wanted::Bust::None;
            if (a_bust == Wanted::Bust::None || state.pursuit.Stars() == 0) {
                state.stop.Reset();
                EndRun(a_player, state.pursuit.Stars() > 0);
                return false;
            }
            const RE::NiPoint3  feet = a_anchor + RE::NiPoint3(a_pose->offset[0], a_pose->offset[1], a_pose->offset[2]);
            const Wanted::Point middle{ feet.x + a_pose->vehicleOffset[0], feet.y + a_pose->vehicleOffset[1] };
            Wanted::Point       target{ feet.x, feet.y };
            if (a_bust == Wanted::Bust::Door) {
                target = Wanted::DoorSpot(middle, a_pose->vehicleHeading);
            }
            float                             reach = (std::numeric_limits<float>::max)();
            std::vector<VehicleHits::Pursuer> pursuers;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->ForEachHighActor([&](RE::Actor* a_actor) {
                    if (Pursues(a_actor) && (!SpawnedOf(a_actor) || Wanted::Arrests(SpawnedOf(a_actor)->unit))) {
                        const auto at = a_actor->GetPosition();
                        if (std::abs(at.z - feet.z) < 200.0f) {
                            const float toDoor = std::hypot(at.x - target.x, at.y - target.y);
                            reach = (std::min)(reach, toDoor);
                            if (VehicleHits::CanRun(BuildOf(a_actor), a_actor->IsOnMount())) {
                                pursuers.push_back({ a_actor->GetFormID(), toDoor });  // no rider, giant or mammoth runs for it
                            }
                        }
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            const float speed = std::hypot(a_pose->velocity[0], a_pose->velocity[1]);
            const auto  orders = state.doorRun.Tick(a_delta, true, a_bust, speed, pursuers);
            if (orders.recall) {
                RecallRunner(a_player, orders.recall, orders.gaveUp, true);
            }
            if (orders.send) {
                SendRunner(orders.send);
            }
            if (const auto runner = state.doorRun.Runner()) {
                Steer(a_player, runner, VehicleHits::RunTo(a_bust, middle, a_pose->vehicleHeading, { feet.x, feet.y }), a_delta);
            }
            return state.stop.Tick(a_delta, true, a_bust, speed, reach);
        }

        // A wanted cheat typed (on the input thread), applied in the next Update: the force comes from the hold the player
        // stands in, else the last one's, else Whiterun's.
        void ApplyCheat(RE::PlayerCharacter* player, int a_how)
        {
            if (NeverWanted()) {
                return;
            }
            if (a_how == 0) {
                // A clean slate: every bounty goes too (else a guard who sees CJ brings the stars back at once), the fight
                // is called off, and all of ours go at once, seen or not.
                ClearBounties(player, "ASNAEB");
                state.pursuit.Clear();
                Dismiss("ASNAEB", true);
                return;
            }
            PickHold(player);
            const int stars = a_how == 2 ? Wanted::kMaxStars : (std::min)(state.pursuit.Stars() + 2, Wanted::kMaxStars);
            state.pursuit.Restore(Wanted::HeatFor(stars), 0.0f);
            SKSE::log::info("police: a wanted cheat: {} star(s), the force from {}", stars, state.hold->name);
        }

        // Skyrim's "go to jail" while CJ has the player: Busted instead, next frame.
        struct GoToPrisonHook
        {
            static void thunk(RE::PlayerCharacter* a_player, RE::TESFaction* a_faction, bool a_removeInventory, bool a_realJail)
            {
                if (!state.owned) {
                    original(a_player, a_faction, a_removeInventory, a_realJail);
                    return;
                }
                state.jailAsked = a_faction;
                a_player->GetPlayerRuntimeData().playerFlags.goToJailQueued = false;
                SKSE::log::info("police: Skyrim sends the player to {}'s jail (remove inventory {}, real jail {}): Busted instead", a_faction ? a_faction->GetName() : "?",
                    a_removeInventory, a_realJail);
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };

        // A hit on the player while CJ has it (Skyrim's difficulty already on it), from Skyrim's people or a trap with nobody
        // behind it: [Balance] DamageTaken of it (Balance), for a guard (any hold's) or one of ours times the stars' share
        // (ShareFor), and CJ's armour takes it first (Vitals::TakeHit) unless it's drowning. The rest is given back, or the
        // more taken, as it lands. Magic's damage is Vitals' own (its ModifyActorValue hook), and so is San Andreas's.
        struct HealthDamageHook
        {
            static void thunk(RE::PlayerCharacter* a_player, RE::Actor* a_attacker, float a_damage)
            {
                original(a_player, a_attacker, a_damage);
                if (state.owned && a_attacker && !a_attacker->IsPlayerRef()) {
                    state.blowFrom = a_attacker->GetPosition();
                    state.blowAt = std::chrono::steady_clock::now();
                }
                static bool inside = false;
                if (!state.owned || inside || (a_attacker && a_attacker->IsPlayerRef()) || state.sequence.Running() || Vitals::IsInvincible() ||
                    Vitals::OwnDamage()) {
                    return;
                }
                const bool  guard = a_attacker && (a_attacker->IsGuard() || IsSpawned(a_attacker));
                const float factor = Balance::TakenFactor(ShareFor(a_attacker));
                const auto  kind = Health::OfHit(a_attacker != nullptr, a_player->GetActorRuntimeData().boolFlags.all(RE::Actor::BOOL_FLAGS::kUnderwater));
                const float armor = Vitals::Armor();
                inside = true;
                const float back = Vitals::TakeHit(a_player, a_damage, factor, kind);
                inside = false;
                if (back > 0.0f) {
                    NoteSpared(a_player);
                }
                static int logged = 0;
                static int trapsLogged = 0;
                if (a_attacker ? logged++ < 24 : trapsLogged++ < 24) {
                    SKSE::log::info("police: {:08X} ({}{}) hit CJ for {:.1f} health{}, x{:.2f}: armour {:.0f} -> {:.0f}, health {:+.1f} of it given back",
                        a_attacker ? a_attacker->GetFormID() : 0, a_attacker ? a_attacker->GetName() : "a trap or nobody", guard ? ", a guard" : "", Health::DamageOf(a_damage),
                        kind == Health::Kind::Drowning ? " (drowning)" : "", factor, armor, Vitals::Armor(), back);
                }
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };

        // The fine paid to a guard: the pursuit is over, as Skyrim's.
        struct PayFineHook
        {
            static void thunk(RE::PlayerCharacter* a_player, RE::TESFaction* a_faction, bool a_goToJail, bool a_removeStolenItems)
            {
                original(a_player, a_faction, a_goToJail, a_removeStolenItems);
                if (state.owned && (state.pursuit.Stars() > 0 || state.owed > 0)) {
                    SKSE::log::info("police: the fine paid to {}: the stars go", a_faction ? a_faction->GetName() : "?");
                    ClearBounties(a_player, "the fine paid");
                    state.pursuit.Clear();
                    Dismiss("the fine paid");
                }
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
        GoToPrisonHook::original = vtable.write_vfunc(0xB9, GoToPrisonHook::thunk);
        PayFineHook::original = vtable.write_vfunc(0xBB, PayFineHook::thunk);
        HealthDamageHook::original = vtable.write_vfunc(0x104, HealthDamageHook::thunk);
        SKSE::log::info("police: PlayerCharacter::GoToPrison, PayFine and HandleHealthDamage hooked: \"go to jail\" is Busted while CJ has the player, the fine ends "
                        "the stars, guards hit CJ by the stars");
    }

    namespace
    {
        // The police's frame (RunFrame), on the main thread at the end of the player's update (Sweep, from PlayerSync's
        // hook), after the frame's player sync. Not from an SKSE task: those ran on Skyrim's worker threads, beside its own
        // actor updates, and setting actors on each other from there broke a fight's data that a quickload then crashed on.
        bool UpdateFrame(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, bool a_cjDrawn, bool a_scene,
            float a_delta)
        {
            state.chasedTimer += a_delta;
            if (Vitals::IsWasted()) {
                if (state.sequence.Running()) {
                    state.sequence.Cancel();
                    SaHud::HideBigMessage();
                    SKSE::log::info("police: Busted gave way to Wasted");
                }
                if (state.pursuit.Stars() > 0 || state.owed > 0) {
                    state.pursuit.Clear();  // San Andreas clears the wanted level with Wasted, and nobody remembers
                    ClearBounties(a_player, "Wasted");
                    Dismiss("Wasted");
                }
                state.jailAsked = nullptr;
                state.stop.Reset();
                EndRun(a_player, false);
                Show(0);
                return false;
            }
            if (state.sequence.Running()) {
                state.jailAsked = nullptr;
                Run(a_player, a_delta, a_cjDrawn);
                Show(state.sequence.Running() ? state.pursuit.Stars() : 0);
                return state.sequence.Running();
            }

            if (state.settle > 0.0f) {
                state.settle -= a_delta;  // a loaded world's actors are still coming in
                Show(state.pursuit.Stars());
                return false;
            }
            const bool never = NeverWanted();
            if (never && !state.neverWanted) {
                ForgiveAll(a_player);
            }
            state.neverWanted = never;
            if (never) {
                state.calmTimer += a_delta;
                if (state.calmTimer >= kCalmEvery) {
                    state.calmTimer = 0.0f;
                    Calm(a_player, false);
                }
            }
            WatchCrimes(a_player);
            if (const int cheat = g_cheat.exchange(-1); cheat >= 0) {
                ApplyCheat(a_player, cheat);
            }
            if (auto* faction = std::exchange(state.jailAsked, nullptr)) {
                const bool inVehicle = a_pose && (a_pose->flags & Protocol::kPlayerInVehicle) && a_pose->vehicleModel;
                const auto bust = inVehicle ? Wanted::BustOf(VehicleKinds::KindOf(a_pose->vehicleModel)) : Wanted::Bust::None;
                if (const auto* hold = Wanted::HoldOf(faction->GetFormID())) {
                    state.hold = hold;
                }
                Begin(a_player, bust == Wanted::Bust::Door ? Wanted::Arrest::Door : bust == Wanted::Bust::KnockOff ? Wanted::Arrest::KnockOff : Wanted::Arrest::OnFoot, faction);
                return true;
            }

            state.lookTimer += a_delta;
            if (state.lookTimer >= kLookEvery) {
                state.lookTimer = 0.0f;
                Look(a_player);
                if (state.pursuit.Stars() > 0) {
                    PickHold(a_player);  // the force comes from wherever CJ is now
                }
            }
            const auto look = state.pursuit.Tick(a_delta, state.seen, never);
            Show(look.stars, look.flashing);
            if (look.stars == 0 && state.loggedStars > 0 && !never) {
                ClearBounties(a_player, "the stars are lost", false);  // nobody remembers
            } else if (state.amnesty.Tick(a_delta, look.stars, state.owed > 0, state.seen)) {
                ClearBounties(a_player, std::format("no guard has seen CJ for {:.0f} s", Wanted::kForgetAfter).c_str(), false);
            }
            if (look.stars != state.loggedStars) {
                SKSE::log::info("police: {} star(s){}", look.stars, look.stars < state.loggedStars && look.stars == 0 ? " (lost them)" : "");
                // A star gained: CJ has something to say about it, now and then while they keep after him.
                if (look.stars > state.loggedStars && state.chasedTimer >= kChasedAfterRise) {
                    Say(Wanted::kLineChased);
                }
                state.loggedStars = look.stars;
            } else if (look.stars > 0 && state.seen && state.chasedTimer >= Wanted::kChasedEvery) {
                Say(Wanted::kLineChased);
            }
            state.cjSpeed = a_pose ? std::hypot(a_pose->velocity[0], a_pose->velocity[1]) : 0.0f;
            if (state.rejoin.Tick(a_delta)) {
                Rejoin(a_player);
            }
            Respond(a_player, a_delta, a_scene);

            Wanted::Bust bust = Wanted::Bust::None;
            if (!a_scene && CheckVehicle(a_player, a_anchor, a_pose, a_delta, bust) && state.hold) {
                Begin(a_player, bust == Wanted::Bust::Door ? Wanted::Arrest::Door : Wanted::Arrest::KnockOff, FactionOf(*state.hold));
                return true;
            }
            return false;
        }

        // Who fights the player as it's knocked down (Skyrim's and ours), for Rejoin.
        void KnockedDownNow(RE::PlayerCharacter* a_player)
        {
            state.downFighters.clear();
            state.rejoin.Reset();
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            const auto feet = a_player->GetPosition();
            lists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (a_actor && !a_actor->IsDead() && a_actor->Is3DLoaded() && Fights(a_actor, a_player) && a_actor->GetPosition().GetDistance(feet) <= Wanted::kSightRange) {
                    state.downFighters.push_back(a_actor->GetHandle());
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            for (const auto& s : state.spawned) {  // ours too, whoever they were fighting this moment
                if (auto ref = s.actor.get(); ref && ref->As<RE::Actor>()) {
                    state.downFighters.push_back(ref->As<RE::Actor>()->GetHandle());
                }
            }
            SKSE::log::info("police: the player is knocked down with {} fighting it (and ours)", state.downFighters.size());
        }

        void RetargetNow(RE::PlayerCharacter* a_player)
        {
            state.rejoin.Start();
            Rejoin(a_player);
            for (auto& s : state.spawned) {
                s.leash.Pulled();
            }
        }

        // What the player's update asked for since the last task ran.
        struct Pending
        {
            bool                  update = false;  // a frame while CJ has the player
            RE::NiPoint3          anchor;
            bool                  hasPose = false;
            Protocol::PlayerState pose{};
            bool                  cjDrawn = false;
            bool                  scene = false;
            float                 delta = 0.0f;  // the frames' since the last task, added up
            bool                  knocked = false;
            bool                  retarget = false;
            bool                  release = false;  // Skyrim has the player back: ours leave
        };

        std::mutex g_pendingLock;
        Pending    g_pending;

        // Copies left in a loaded save by earlier builds (Wanted::LeftoverCopy): every fight with them ends and they're taken
        // out, deleted too, a step a frame like ours leaving. Looked for once a loaded game has settled.
        void PurgeLeftovers(float a_delta)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            const bool settled = player && player->Is3DLoaded() && cell && cell->IsAttached() &&
                                 !(ui && (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->GameIsPaused()));
            state.leftoverSettle = settled ? state.leftoverSettle + a_delta : 0.0f;
            if (state.leftoverSettle < Wanted::kLeftoverSettle) {
                return;
            }
            state.lookForLeftovers = false;
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            std::vector<RE::NiPointer<RE::Actor>> found;
            lists->ForAllActors([&](RE::Actor* a_actor) {
                if (a_actor && !a_actor->IsDisabled()) {
                    auto*       leveled = a_actor->extraList.GetByType<RE::ExtraLeveledCreature>();
                    const auto* madeFrom = leveled && leveled->originalBase ? static_cast<RE::TESForm*>(leveled->originalBase)
                                                                           : static_cast<RE::TESForm*>(a_actor->GetActorBase());
                    if (madeFrom && Wanted::LeftoverCopy(a_actor->GetFormID(), madeFrom->GetFormID())) {
                        found.emplace_back(a_actor);
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            for (const auto& actor : found) {
                SKSE::log::info("police: {:08X} '{}' is a copy left in the save, made from runtime base {:08X}: taken out", actor->GetFormID(), actor->GetName(),
                    actor->extraList.GetByType<RE::ExtraLeveledCreature>() ? actor->extraList.GetByType<RE::ExtraLeveledCreature>()->originalBase->GetFormID() : 0);
                Unlink(actor.get());
                Leaving leaving;
                leaving.actor = actor->CreateRefHandle();
                leaving.purge = true;
                state.leaving.push_back(leaving);
            }
            if (!found.empty()) {
                state.clearOut = true;  // seen or not
            }
            SKSE::log::info("police: the loaded game has settled: {} cop(ies) left in the save by earlier builds", found.size());
        }

        void RunFrame(float a_delta)
        {
            if (state.lookForLeftovers) {
                PurgeLeftovers(a_delta);
            }
            Pending p;
            {
                std::scoped_lock lock(g_pendingLock);
                p = std::exchange(g_pending, Pending{});
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return;
            }
            if (p.release) {
                EndRun(player, false);  // the door runner's offset and script object go first
                Dismiss("Skyrim has the player back", true);
            }
            if (p.knocked) {
                KnockedDownNow(player);
            }
            if (p.update && state.owned) {
                UpdateFrame(player, p.anchor, p.hasPose ? &p.pose : nullptr, p.cjDrawn, p.scene, p.delta);
            }
            if (p.retarget && state.owned) {
                RetargetNow(player);
            }
            Unbind(false);
            SweepLeaving();
        }

    }

    bool Update(RE::PlayerCharacter*, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, bool a_cjDrawn, bool a_scene, float a_delta)
    {
        state.owned = true;
        std::scoped_lock lock(g_pendingLock);
        g_pending.update = true;
        g_pending.anchor = a_anchor;
        g_pending.hasPose = a_pose != nullptr;
        if (a_pose) {
            g_pending.pose = *a_pose;
        }
        g_pending.cjDrawn = a_cjDrawn;
        g_pending.scene = a_scene;
        g_pending.delta += a_delta;
        return state.sequence.Running();
    }

    std::uint8_t TakeLine() { return std::exchange(state.line, std::uint8_t{ 0 }); }

    bool TakeSpared() { return std::exchange(state.spared, false); }

    float ShareFor(RE::Actor* a_attacker)
    {
        // The stars' share only for guards (any hold's) and ours: bandits, creatures, traps and the rest never get it.
        return a_attacker && (a_attacker->IsGuard() || IsSpawned(a_attacker)) ? Wanted::GuardDamage(state.pursuit.Stars()) : 1.0f;
    }

    void NoteSpared(RE::PlayerCharacter* a_player)
    {
        state.spared = state.spared || a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) > 0.0f;
    }

    void CheatStars(int a_how) { g_cheat.store(a_how); }

    bool LastBlow(float a_seconds, RE::NiPoint3& a_from)
    {
        if (state.blowAt == std::chrono::steady_clock::time_point{} ||
            std::chrono::duration<float>(std::chrono::steady_clock::now() - state.blowAt).count() > a_seconds) {
            return false;
        }
        a_from = state.blowFrom;
        return true;
    }

    void OnKnockedDown(RE::PlayerCharacter*)
    {
        std::scoped_lock lock(g_pendingLock);
        g_pending.knocked = true;
    }

    void Retarget(RE::PlayerCharacter*)
    {
        std::scoped_lock lock(g_pendingLock);
        g_pending.retarget = true;
    }

    bool QuickAllowed(bool a_pressed)
    {
        const int alive = static_cast<int>(std::count_if(state.spawned.begin(), state.spawned.end(), [](const Spawned& a_s) {
            auto ref = a_s.actor.get();
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            return actor && !actor->IsDead();
        }));
        const float sinceGone = state.leaving.empty() ? std::chrono::duration<float>(std::chrono::steady_clock::now() - state.goneAt).count() : 0.0f;
        const bool  allowed = Wanted::QuickAllowed(state.pursuit.Stars(), alive, static_cast<int>(state.leaving.size()), sinceGone);
        if (!allowed && state.pursuit.Stars() == 0 && alive == 0) {
            state.clearOut = true;  // only leaving ones: out of sight or not, they go now
        }
        if (!allowed && a_pressed) {
            SKSE::log::info("police: a quick save or quickload refused: {} star(s), {} of ours out, {} leaving, the last gone {:.1f} s ago", state.pursuit.Stars(),
                alive, state.leaving.size(), sinceGone);
        }
        return allowed;
    }

    bool IsBusted() { return state.sequence.Running() && state.frame.active; }

    bool IsArrested() { return state.sequence.Running() && state.frame.busted; }

    void OnRelease()
    {
        state.owned = false;
        state.downFighters.clear();
        state.rejoin.Reset();
        if (state.sequence.Running()) {
            state.sequence.Cancel();
            SKSE::log::info("police: Busted ended with the handover");
        }
        state.jailAsked = nullptr;
        state.line = 0;
        EndShow();
        {
            std::scoped_lock lock(g_pendingLock);  // ours leave in the next RunFrame
            g_pending.update = false;
            g_pending.release = true;
        }
        state.stop.Reset();
        Show(0);
    }

    void OnReanchor()
    {
        if (state.moveAsked) {
            state.arrived = true;
        }
    }

    void Sweep(float a_delta) { RunFrame(a_delta); }

    void OnPostLoadGame()
    {
        state.lookForLeftovers = true;
        state.leftoverSettle = 0.0f;
    }

    void OnGameLoad()
    {
        // A load or a new game, any of them (F9, the pause menu, the reload after a death, the console): Skyrim is about to
        // tear its world down. Nothing is done to any actor from here (no fight stopped, nothing disabled: acting on actors
        // as the load starts was in today's load crashes), nothing recorded before now is run, and no handle of ours
        // outlives it.
        {
            std::scoped_lock lock(g_pendingLock);
            g_pending = {};
        }
        if (!state.spawned.empty() || !state.leaving.empty()) {
            SKSE::log::info("police: a load: {} of ours and {} leaving are left to Skyrim's own clearing", state.spawned.size(), state.leaving.size());
        }
        state.downFighters.clear();
        state.rejoin.Reset();
        state.spawned.clear();  // the save's own references now
        state.leaving.clear();
        state.director.Reset();
        state.settle = kSettle;
        state.clearOut = false;
        state.seeded = false;
        state.bounties.clear();
        state.owed = 0;
        state.amnesty.Reset();
        state.grudges.Reset();
        state.gaps.Reset();
        state.neverWanted = false;
        state.sequence.Cancel();
        state.jailAsked = nullptr;
        state.stop.Reset();
        state.doorRun.Reset();  // whoever ran is the save's again
        Unbind(true);
        state.line = 0;
    }

    void Save(SKSE::SerializationInterface* a_intfc)
    {
        Wanted::Record record;
        record.heat = state.pursuit.Heat();
        record.faction = state.hold ? state.hold->faction : 0;
        record.unseen = state.pursuit.Unseen();
        const auto bytes = Wanted::Write(record);
        if (!a_intfc->WriteRecord(Wanted::kRecordType, Wanted::kVersion, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
            SKSE::log::error("police: the wanted level's co-save record could not be written");
            return;
        }
        SKSE::log::info("police: saved heat {:.0f} ({} star(s)){}", record.heat, state.pursuit.Stars(), state.hold ? std::format(" in {}", state.hold->name) : std::string());
    }

    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size)
    {
        Wanted::Record record;
        if (!Wanted::Read(a_version, a_data, a_size, record)) {
            SKSE::log::warn("police: the wanted level's co-save record (version {}, {} bytes) doesn't read, no stars", a_version, a_size);
            return;
        }
        state.hold = Wanted::HoldOf(record.faction);
        state.pursuit.Restore(state.hold ? record.heat : 0.0f, record.unseen);
        state.loggedStars = state.pursuit.Stars();
        SKSE::log::info("police: loaded {} star(s){}", state.pursuit.Stars(), state.hold ? std::format(" in {}", state.hold->name) : std::string());
    }

    void Revert()
    {
        state.pursuit.Clear();
        state.leaving.clear();
        state.clearOut = false;
        state.hold = nullptr;
        state.loggedStars = 0;
        state.seeded = false;
        state.bounties.clear();
        state.owed = 0;
        state.amnesty.Reset();
        state.grudges.Reset();
        state.spawned.clear();
    }
}
