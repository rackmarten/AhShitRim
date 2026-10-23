#include "player/Police.h"

#include "hud/SaHud.h"
#include "player/CheatCodes.h"
#include "player/Vitals.h"
#include "vehicle_kinds.h"
#include "wanted.h"

#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <utility>
#include <vector>

namespace AhShitRim::Police
{
    namespace
    {
        constexpr RE::FormID kGold = 0x0000000F;
        constexpr float      kLookEvery = 0.25f;  // seconds between looks for who sees CJ
        constexpr float      kJoinEvery = 1.0f;   // seconds between calls for the hold's guards to join
        constexpr float      kMountAfter = 0.5f;  // a rider gets on his horse this long after both are in
        constexpr float      kFightAfter = 1.5f;  // and goes for CJ this long after that
        constexpr float      kGoneRange = 4000.0f;   // a pursuer leaving goes once this far, or out of the player's sight
        constexpr float      kSpawnRise = 300.0f;    // a spawn spot more than this above or below the player is a tower or a ledge: not used
        constexpr float      kOnMarker = 150.0f;     // after the move, the player this far from the barracks' marker is moved there again

        struct Spawned
        {
            RE::ObjectRefHandle actor;
            RE::ObjectRefHandle horse;  // a rider's, none for a soldier
            float               age = 0.0f;  // seconds since both were in (3D loaded)
            bool                mounted = false;
            bool                fighting = false;
        };

        // A pursuer spawned for the pursuit that is no longer needed: taken out a step a frame (the rider, then his
        // horse, each disabled and then deleted), once the player can't see it. All at once, in a fight, crashed.
        struct Leaving
        {
            RE::ObjectRefHandle actor;
            RE::ObjectRefHandle horse;
            int                 step = 0;  // 0 disable the actor, 1 delete it, 2 disable the horse, 3 delete it
        };

        struct State
        {
            bool                     owned = false;
            Wanted::Pursuit          pursuit;
            const Wanted::Hold*      hold = nullptr;  // whose pursuit it is
            std::array<std::uint32_t, Wanted::kHolds.size()> violent{};     // each hold's bounty last frame, its two halves
            std::array<std::uint32_t, Wanted::kHolds.size()> nonViolent{};
            bool                     seeded = false;  // the bounties above are this game's
            bool                     seen = false;
            float                    lookTimer = 0.0f;
            float                    joinTimer = 0.0f;
            float                    spawnTimer = 0.0f;
            std::vector<Spawned>     spawned;
            std::vector<Leaving>     leaving;
            bool                     clearOut = false;  // the leaving go whether the player sees them or not (Skyrim has it back, Busted)
            int                      spawnTries = 0;    // turns the next spawn spot round CJ
            Wanted::StopWatch        stop;
            Wanted::Sequence         sequence;
            Wanted::Frame            frame;
            RE::TESFaction*          bustFaction = nullptr;
            RE::TESFaction*          jailAsked = nullptr;  // GoToPrison came while CJ has the player: Busted next frame
            bool                     moveAsked = false;
            bool                     arrived = false;
            bool                     movedAgain = false;  // the player was moved to the marker a second time, its ground in
            int                      loggedStars = 0;
            int                      shownStars = -1;  // what the HUD was last told (so the HUD demo's stars stay up)
            bool                     shownFlashing = false;
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

        std::size_t IndexOf(const Wanted::Hold& a_hold) { return static_cast<std::size_t>(&a_hold - Wanted::kHolds.data()); }

        int BountyOf(RE::PlayerCharacter* a_player, const Wanted::Hold* a_hold)
        {
            auto* faction = a_hold ? FactionOf(*a_hold) : nullptr;
            return faction ? static_cast<int>(a_player->GetViolentCrimeGoldValue(faction) + a_player->GetNonViolentCrimeGoldValue(faction)) : 0;
        }

        bool NeverWanted() { return CheatCodes::On(Cheats::SaCheat::NeverWanted); }

        bool IsSpawned(RE::Actor* a_actor)
        {
            for (const auto& s : state.spawned) {
                if (s.actor.get().get() == a_actor) {
                    return true;
                }
            }
            return false;
        }

        // Whether a_actor is one of the pursuit's: a guard of its hold, or one we spawned.
        bool Pursues(RE::Actor* a_actor)
        {
            if (!a_actor || a_actor->IsDead() || a_actor->IsPlayerRef()) {
                return false;
            }
            if (IsSpawned(a_actor)) {
                return true;
            }
            auto* faction = a_actor->GetCrimeFaction();
            return state.hold && a_actor->IsGuard() && faction && faction->GetFormID() == state.hold->faction;
        }

        bool Fights(RE::Actor* a_actor, RE::PlayerCharacter* a_player) { return a_actor->GetActorRuntimeData().currentCombatTarget.get().get() == a_player; }

        // ---- The bounties ----

        // Reads each hold's bounty: one that went up is a crime there. AEZAKMI puts it back and calls the fight off.
        void WatchCrimes(RE::PlayerCharacter* a_player)
        {
            const bool never = NeverWanted();
            for (const auto& hold : Wanted::kHolds) {
                auto* faction = FactionOf(hold);
                if (!faction) {
                    continue;
                }
                const auto i = IndexOf(hold);
                const std::uint32_t violent = a_player->GetViolentCrimeGoldValue(faction);
                const std::uint32_t nonViolent = a_player->GetNonViolentCrimeGoldValue(faction);
                const int           before = static_cast<int>(state.violent[i] + state.nonViolent[i]);
                const int           now = static_cast<int>(violent + nonViolent);
                if (state.seeded && now > before) {
                    if (never) {
                        a_player->SetCrimeGoldValue(faction, true, state.violent[i]);
                        a_player->SetCrimeGoldValue(faction, false, state.nonViolent[i]);
                        if (auto* lists = RE::ProcessLists::GetSingleton()) {
                            lists->StopCombatAndAlarmOnActor(a_player, false);
                        }
                        SKSE::log::info("police: AEZAKMI: a crime in {} ({} gold) undone, the bounty stays {}", hold.name, now - before, before);
                        continue;
                    }
                    state.hold = &hold;
                    const int stars = state.pursuit.Crime(now - before, false);
                    SKSE::log::info("police: a crime in {}: the bounty is {} ({} more), heat {:.0f}, {} star(s)", hold.name, now, now - before, state.pursuit.Heat(),
                        stars);
                }
                state.violent[i] = violent;
                state.nonViolent[i] = nonViolent;
            }
            state.seeded = true;
        }

        // ---- Who sees CJ ----

        // Whether any pursuer of the hold sees the player; while no stars show, a guard of a hold with a bounty on
        // the player fighting it brings them back.
        void Look(RE::PlayerCharacter* a_player)
        {
            state.seen = false;
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            const auto feet = a_player->GetPosition();
            const bool never = NeverWanted();
            lists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (!a_actor || a_actor->IsDead() || a_actor->GetPosition().GetDistance(feet) > Wanted::kSightRange) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (state.pursuit.Stars() == 0) {
                    auto*       faction = a_actor->IsGuard() && Fights(a_actor, a_player) ? a_actor->GetCrimeFaction() : nullptr;
                    const auto* hold = faction ? Wanted::HoldOf(faction->GetFormID()) : nullptr;
                    const int   bounty = BountyOf(a_player, hold);
                    if (hold && bounty > 0 && !never) {
                        state.hold = hold;
                        state.pursuit.Recognised(bounty, false);
                        SKSE::log::info("police: {} ({:08X}) of {} recognises CJ: the bounty is {}, {} star(s)", a_actor->GetName(), a_actor->GetFormID(), hold->name,
                            bounty, state.pursuit.Stars());
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                bool unused = false;
                if (Pursues(a_actor) && a_actor->HasLineOfSight(a_player, unused)) {
                    state.seen = true;
                    return RE::BSContainer::ForEachResult::kStop;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        // ---- The response ----

        // Everyone spawned stops fighting and leaves: Sweep takes them out once the player can't see them.
        void Dismiss(const char* a_why, bool a_clearOut = false)
        {
            state.clearOut = state.clearOut || a_clearOut;
            if (state.spawned.empty()) {
                return;
            }
            for (auto& s : state.spawned) {
                if (auto ref = s.actor.get()) {
                    if (auto* actor = ref->As<RE::Actor>(); actor && !actor->IsDead()) {
                        actor->StopCombat();
                    }
                }
                state.leaving.push_back({ s.actor, s.horse, 0 });
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
            auto* actor = a_ref->As<RE::Actor>();
            if ((actor && actor->IsDead()) || a_ref->GetPosition().GetDistance(a_player->GetPosition()) > kGoneRange) {
                return true;
            }
            bool unused = false;
            return !a_player->HasLineOfSight(a_ref, unused);
        }

        // One step a frame of the first leaving pursuer that can go.
        void SweepLeaving()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            for (auto it = state.leaving.begin(); it != state.leaving.end(); ++it) {
                auto actor = it->actor.get();
                auto horse = it->horse.get();
                if (it->step == 0 && !CanGo(player, actor.get())) {
                    continue;
                }
                RE::TESObjectREFR* ref = it->step < 2 ? actor.get() : horse.get();
                if (ref) {
                    if (it->step % 2 == 0) {
                        ref->Disable();
                    } else {
                        ref->SetDelete(true);
                    }
                }
                if (++it->step >= 4) {
                    state.leaving.erase(it);
                }
                break;
            }
            if (state.leaving.empty()) {
                state.clearOut = false;
            }
        }

        // The hold's guards round about who aren't fighting yet join.
        void CallGuards(RE::PlayerCharacter* a_player, float a_radius)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists || a_radius <= 0.0f) {
                return;
            }
            const auto feet = a_player->GetPosition();
            int        joined = 0;
            lists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (Pursues(a_actor) && !IsSpawned(a_actor) && !Fights(a_actor, a_player) && a_actor->GetPosition().GetDistance(feet) <= a_radius) {
                    joined += a_actor->StartCombat(a_player) ? 1 : 0;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            if (joined > 0) {
                SKSE::log::info("police: {} guard(s) of {} within {:.0f} units join the chase", joined, state.hold->name, a_radius);
            }
        }

        // A guard of the hold about, whose base the riders copy (the hold's guards as the war left them), else the table's.
        RE::TESBoundObject* GuardBase(RE::PlayerCharacter* a_player)
        {
            RE::TESBoundObject* found = nullptr;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->ForEachHighActor([&](RE::Actor* a_actor) {
                    if (Pursues(a_actor) && !IsSpawned(a_actor) && a_actor != a_player) {
                        found = a_actor->GetObjectReference();
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            return found ? found : RE::TESForm::LookupByID<RE::TESBoundObject>(state.hold->guard);
        }

        // One of a_base, kSpawnDistance behind the player (turned a_turn radians round it) on the nearest navmesh, at about
        // the player's height: a spot on a tower or a ledge is given up.
        RE::TESObjectREFR* Place(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_base, float a_aside, float a_turn)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* cell = a_player->GetParentCell();
            if (!data || !a_base || !cell) {
                return nullptr;
            }
            const float heading = a_player->GetAngleZ() + a_turn;
            const auto  feet = a_player->GetPosition();
            const RE::NiPoint3 at(feet.x - std::sin(heading) * Wanted::kSpawnDistance + std::cos(heading) * a_aside,
                feet.y - std::cos(heading) * Wanted::kSpawnDistance - std::sin(heading) * a_aside, feet.z + 50.0f);
            const auto handle = data->CreateReferenceAtLocation(a_base, at, RE::NiPoint3(0.0f, 0.0f, heading), cell, a_player->GetWorldspace(), nullptr, nullptr,
                RE::ObjectRefHandle(), false, true);
            auto ref = handle.get();
            if (!ref) {
                return nullptr;
            }
            ref->MoveToNearestNavmesh();
            if (std::abs(ref->GetPosition().z - feet.z) > kSpawnRise) {
                SKSE::log::info("police: a spawn spot {:.0f} units {} the player given up (a tower or a ledge)", std::abs(ref->GetPosition().z - feet.z),
                    ref->GetPosition().z > feet.z ? "above" : "below");
                state.leaving.push_back({ ref->CreateRefHandle(), {}, 0 });
                return nullptr;
            }
            return ref.get();
        }

        void Spawn(RE::PlayerCharacter* a_player, bool a_rider)
        {
            const float aside = static_cast<float>(state.spawnTries % 3) * 250.0f - 250.0f;
            const float turn = static_cast<float>((state.spawnTries++ % 5) - 2) * 0.6f;  // behind, and round to either side
            RE::TESBoundObject* base = a_rider ? GuardBase(a_player) :
                                                 RE::TESForm::LookupByID<RE::TESBoundObject>(state.hold->side == Wanted::Side::Imperial ? Wanted::kSoldiersImperial : Wanted::kSoldiersSons);
            auto* actor = Place(a_player, base, aside, turn);
            if (!actor) {
                SKSE::log::warn("police: a {} for {} couldn't be placed (base {:08X})", a_rider ? "rider" : "soldier", state.hold->name, base ? base->GetFormID() : 0);
                return;
            }
            Spawned s;
            s.actor = actor->CreateRefHandle();
            if (a_rider) {
                if (auto* horse = Place(a_player, RE::TESForm::LookupByID<RE::TESBoundObject>(Wanted::kHorse), aside + 120.0f, turn)) {
                    s.horse = horse->CreateRefHandle();
                }
            }
            const auto at = actor->GetPosition();
            SKSE::log::info("police: a {} for {} ({:08X} of base {:08X}{}) comes from ({:.0f},{:.0f},{:.0f})", a_rider ? "rider" : "soldier", state.hold->name,
                actor->GetFormID(), base->GetFormID(), a_rider ? (s.horse ? ", on a horse" : ", but no horse") : "", at.x, at.y, at.z);
            state.spawned.push_back(s);
        }

        // The riders get on their horses once both are in, then everyone spawned goes for CJ.
        void Lead(RE::PlayerCharacter* a_player, float a_delta)
        {
            for (auto& s : state.spawned) {
                auto actorRef = s.actor.get();
                auto* actor = actorRef ? actorRef->As<RE::Actor>() : nullptr;
                auto  horse = s.horse.get();
                if (!actor || !actor->Is3DLoaded() || (horse && !horse->Is3DLoaded())) {
                    continue;
                }
                s.age += a_delta;
                if (horse && !s.mounted && s.age >= kMountAfter) {
                    // An NPC activating a horse gets on it, as followers do.
                    s.mounted = true;
                    const bool took = horse->ActivateRef(actor, 0, nullptr, 0, false);
                    SKSE::log::info("police: rider {:08X} gets on horse {:08X} ({})", actor->GetFormID(), horse->GetFormID(), took ? "activated" : "the horse refused");
                }
                if (!s.fighting && s.age >= (horse ? kMountAfter + kFightAfter : kMountAfter)) {
                    s.fighting = true;
                    const bool fights = actor->StartCombat(a_player);
                    SKSE::log::info("police: {:08X} goes for CJ ({}{})", actor->GetFormID(), fights ? "fighting" : "won't fight",
                        horse ? (actor->IsOnMount() ? ", mounted" : ", on foot") : "");
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
            const auto response = Wanted::ResponseFor(stars);
            state.joinTimer += a_delta;
            if (state.joinTimer >= kJoinEvery) {
                state.joinTimer = 0.0f;
                CallGuards(a_player, response.joinRadius);
            }
            Lead(a_player, a_delta);
            auto* cell = a_player->GetParentCell();
            state.spawnTimer += a_delta;
            if (a_scene || !cell || cell->IsInteriorCell() || state.spawnTimer < Wanted::kSpawnEvery) {
                return;
            }
            int riders = 0, soldiers = 0;
            for (const auto& s : state.spawned) {
                (s.horse ? riders : soldiers)++;
            }
            if (riders < response.riders) {
                state.spawnTimer = 0.0f;
                Spawn(a_player, true);
            } else if (soldiers < response.soldiers) {
                state.spawnTimer = 0.0f;
                Spawn(a_player, false);
            }
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
            static constexpr const char* kHow[] = { "on foot, his hands up", "dragged out of the car", "knocked off his bike" };
            SKSE::log::info("police: BUSTED by {} ({}), bounty {}, {} gold", a_faction ? a_faction->GetName() : "?", kHow[static_cast<int>(a_arrest)],
                a_faction ? a_player->GetCrimeGoldValue(a_faction) : 0, a_player->GetGoldAmount());
        }

        // `player.moveto` the hold's exterior jail marker: the game's own teleport, a load door's worth of loading.
        RE::TESObjectREFR* BarracksMarker() { return state.bustFaction ? state.bustFaction->crimeData.factionJailMarker : nullptr; }

        void MoveToBarracks(RE::PlayerCharacter* a_player)
        {
            state.moveAsked = state.arrived = false;
            auto* marker = BarracksMarker();
            auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
            auto* script = marker && factory ? factory->Create() : nullptr;
            if (!script) {
                SKSE::log::warn("police: no barracks to take the player to ({}), it wakes where it was", marker ? "no script" : "the faction has no jail marker");
                return;
            }
            script->SetCommand(std::format("player.moveto {:08X}", marker->GetFormID()));
            script->CompileAndRun(a_player);
            delete script;
            state.moveAsked = true;
            SKSE::log::info("police: the player is taken to the barracks (player.moveto {:08X})", marker->GetFormID());
        }

        // The fine from the gold (all of it if short), the bounty cleared, the stars gone, everyone's fight over.
        void Release(RE::PlayerCharacter* a_player)
        {
            auto*       faction = state.bustFaction;
            const auto* hold = faction ? Wanted::HoldOf(faction->GetFormID()) : nullptr;
            const int   bounty = faction ? static_cast<int>(a_player->GetCrimeGoldValue(faction)) : 0;
            const int   fine = Wanted::FineFor(bounty, a_player->GetGoldAmount());
            if (fine > 0) {
                if (auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold)) {
                    a_player->RemoveItem(gold, fine, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                }
            }
            if (faction) {
                a_player->ClearAllCrimeGold(faction);
            }
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->StopCombatAndAlarmOnActor(a_player, false);
            }
            a_player->StopCombat();
            state.pursuit.Clear();
            state.seeded = false;  // the cleared bounty is no crime the other way either
            Dismiss("Busted", true);
            auto* cell = a_player->GetParentCell();
            SKSE::log::info("police: Busted is paid: a {} gold fine of a {} bounty, in {:08X}{}", fine, bounty, cell ? cell->GetFormID() : 0,
                state.moveAsked && !state.arrived ? " (the barracks never opened: where it was)" : "");
            SaHud::HideBigMessage();
            const std::string where = hold ? std::format("You were let go outside the barracks of {}.", hold->name) : std::string("You were let go.");
            SaHud::ShowHelp(fine > 0 ? std::format("{} The guards took {} gold for the bounty.", where, fine) : where);
        }

        void Run(RE::PlayerCharacter* a_player, float a_delta, bool a_cjDrawn)
        {
            // The first move can drop the player under the ground while the cell loads (Whiterun's came out 338 units
            // below its marker): once there, it is moved onto the marker again, the ground in.
            if (auto* marker = BarracksMarker(); marker && state.moveAsked && state.arrived && a_cjDrawn && !state.movedAgain &&
                                                 a_player->GetPosition().GetDistance(marker->GetPosition()) > kOnMarker) {
                state.movedAgain = true;
                SKSE::log::info("police: the player came out {:.0f} units from the barracks' marker, moved onto it again",
                    a_player->GetPosition().GetDistance(marker->GetPosition()));
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
        bool CheckVehicle(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, float a_delta, Wanted::Bust& a_bust)
        {
            const bool inVehicle = a_pose && (a_pose->flags & Protocol::kPlayerInVehicle) && a_pose->vehicleModel;
            a_bust = inVehicle ? Wanted::BustOf(VehicleKinds::KindOf(a_pose->vehicleModel)) : Wanted::Bust::None;
            if (a_bust == Wanted::Bust::None || state.pursuit.Stars() == 0) {
                state.stop.Reset();
                return false;
            }
            const RE::NiPoint3 feet = a_anchor + RE::NiPoint3(a_pose->offset[0], a_pose->offset[1], a_pose->offset[2]);
            Wanted::Point      target{ feet.x, feet.y };
            if (a_bust == Wanted::Bust::Door) {
                target = Wanted::DoorSpot({ feet.x + a_pose->vehicleOffset[0], feet.y + a_pose->vehicleOffset[1] }, a_pose->vehicleHeading);
            }
            float reach = (std::numeric_limits<float>::max)();
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->ForEachHighActor([&](RE::Actor* a_actor) {
                    if (Pursues(a_actor)) {
                        const auto at = a_actor->GetPosition();
                        if (std::abs(at.z - feet.z) < 200.0f) {
                            reach = (std::min)(reach, std::hypot(at.x - target.x, at.y - target.y));
                        }
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            const float speed = std::hypot(a_pose->velocity[0], a_pose->velocity[1]);
            return state.stop.Tick(a_delta, true, a_bust, speed, reach);
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
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
        GoToPrisonHook::original = vtable.write_vfunc(0xB9, GoToPrisonHook::thunk);
        SKSE::log::info("police: PlayerCharacter::GoToPrison hooked: \"go to jail\" is Busted while CJ has the player");
    }

    bool Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, bool a_cjDrawn, bool a_scene, float a_delta)
    {
        state.owned = true;
        if (Vitals::IsWasted()) {
            if (state.sequence.Running()) {
                state.sequence.Cancel();
                SaHud::HideBigMessage();
                SKSE::log::info("police: Busted gave way to Wasted");
            }
            state.jailAsked = nullptr;
            state.stop.Reset();
            Show(state.pursuit.Stars());
            return false;
        }
        if (state.sequence.Running()) {
            state.jailAsked = nullptr;
            Run(a_player, a_delta, a_cjDrawn);
            Show(state.sequence.Running() ? state.pursuit.Stars() : 0);
            return state.sequence.Running();
        }

        WatchCrimes(a_player);
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
        }
        const auto look = state.pursuit.Tick(a_delta, state.seen, BountyOf(a_player, state.hold), NeverWanted());
        Show(look.stars, look.flashing);
        if (look.stars != state.loggedStars) {
            SKSE::log::info("police: {} star(s){}", look.stars, look.stars < state.loggedStars && look.stars == 0 ? " (lost them)" : "");
            state.loggedStars = look.stars;
        }
        Respond(a_player, a_delta, a_scene);

        Wanted::Bust bust = Wanted::Bust::None;
        if (!a_scene && CheckVehicle(a_player, a_anchor, a_pose, a_delta, bust) && state.hold) {
            Begin(a_player, bust == Wanted::Bust::Door ? Wanted::Arrest::Door : Wanted::Arrest::KnockOff, FactionOf(*state.hold));
            return true;
        }
        return false;
    }

    bool IsBusted() { return state.sequence.Running() && state.frame.active; }

    bool IsArrested() { return state.sequence.Running() && state.frame.busted; }

    void OnRelease()
    {
        state.owned = false;
        if (state.sequence.Running()) {
            state.sequence.Cancel();
            SKSE::log::info("police: Busted ended with the handover");
        }
        state.jailAsked = nullptr;
        EndShow();
        Dismiss("Skyrim has the player back", true);
        state.stop.Reset();
        Show(0);
    }

    void OnReanchor()
    {
        if (state.moveAsked) {
            state.arrived = true;
        }
    }

    void Sweep() { SweepLeaving(); }

    void OnGameLoad()
    {
        state.spawned.clear();  // the save's own references now
        state.leaving.clear();
        state.clearOut = false;
        state.seeded = false;
        state.sequence.Cancel();
        state.jailAsked = nullptr;
        state.stop.Reset();
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
        state.spawned.clear();
    }
}
