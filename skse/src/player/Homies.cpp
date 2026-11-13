#include "player/Homies.h"

#include "Ini.h"
#include "body.h"
#include "body/ModelView.h"
#include "crew.h"
#include "hud/SaHud.h"
#include "melee.h"
#include "link/LinkService.h"
#include "player/Riders.h"
#include "wanted.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <vector>

namespace AhShitRim::Homies
{
    namespace
    {
        constexpr RE::FormID kBase = Wanted::kCaptainImperial;  // a vanilla, non-unique soldier (Wanted::Spawnable); his 3D is hidden anyway
        constexpr RE::FormID kXMarker = 0x0000003B;
        constexpr RE::FormID kPlayerFaction = 0x00000DB1;
        constexpr RE::FormID kTamriel = 0x0000003C;
        constexpr float      kSnapFar = 600.0f;          // a spot's navmesh further than this from it is somewhere else
        constexpr float      kDrawWithin = 150.0f;       // metres: San Andreas plays the homies this near the player
        constexpr float      kGateNear = 220.0f;         // metres: the city gates looked at
        constexpr float      kHoldNear = 30000.0f;       // units: a gate belongs to the hold whose stable is this near
        constexpr float      kLookEvery = 0.25f;         // seconds between the followers' looks for a target
        constexpr float      kScanEvery = 1.0f;          // seconds between looks for spots
        constexpr float      kThanesEvery = 10.0f;       // seconds between reads of the Thane titles
        constexpr float      kOffsetEvery = 5.0f;        // seconds between telling a follower his place again ([Crew] FollowPackage=0)
        constexpr RE::FormID kFollowPackage = 0x0005C84B;  // Skyrim.esm's PlayerFollowerPackage: Lydia's, the player 256 to 384 units off
        constexpr float      kFollowEvery = 1.0f;        // seconds between checks that a follower still runs it
        constexpr float      kGearEvery = 1.0f;          // seconds between looks for Skyrim gear on a homie (his outfit comes back)
        constexpr float      kCullEvery = 0.5f;          // seconds between hiding his 3D again (a weapon drawn or a quiver adds shapes)
        constexpr float      kDeadFor = 60.0f;           // a dead homie lies there this long (or until out of reach)
        constexpr float      kTargetRange = 50.0f;       // metres from CJ an enemy may be to be shot at
        constexpr int        kMakesPerFrame = 2;
        constexpr int        kRegroupTries = 5;          // spots behind CJ tried to put a straggler back on
        constexpr float      kRegroupRise = 300.0f;      // units: a spot this far above or below the player is a ledge
        constexpr float      kRegroupGain = 10.0f;       // metres nearer CJ a straggler must come to be moved at all
        constexpr std::uint32_t kCopyBase = 0x6A000000;  // even ids for the homies' copies of San Andreas's models

        // The stables and the open towns' ways in (Skyrim.esm's spots, as the parked vehicles' table has them), in
        // Wanted::kHolds' order of holds.
        struct Anchor
        {
            int          hold;    // Wanted::kHolds / Crew::kThaneHolds index
            RE::NiPoint3 at;
            bool         walled;  // a walled capital: the stable here, the gates found at runtime
            const char*  name;
        };

        const Anchor kAnchors[] = {
            { 0, { 17480.0f, -11640.0f, -4700.0f }, true, "the Whiterun stables" },
            { 4, { -73700.0f, 98500.0f, -11200.0f }, true, "the Solitude stables" },
            { 1, { 136900.0f, 25100.0f, -12790.0f }, true, "the Windhelm stables" },
            { 2, { 173450.0f, -91450.0f, 11130.0f }, true, "the Riften stables" },
            { 3, { -169500.0f, 5650.0f, -4060.0f }, true, "the Markarth stables" },
            { 8, { -30566.0f, -86850.0f, -3124.0f }, false, "Falkreath" },
            { 5, { -38384.0f, 63751.0f, -13910.0f }, false, "Morthal" },
            { 6, { 33278.0f, 105558.0f, -13633.0f }, false, "Dawnstar" },
            { 7, { 109299.0f, 102777.0f, -8867.0f }, false, "Winterhold" },
        };

        enum class SpotKind : std::uint8_t
        {
            Stable,
            GateOutside,
            GateInside,
            Town,
        };

        struct Spot
        {
            std::uint64_t key = 0;
            int           hold = -1;
            SpotKind      kind = SpotKind::Stable;
            RE::FormID    world = 0;
            RE::NiPoint3  centre;
            float         heading = 0.0f;
            int           count = 0;
            std::vector<Crew::Standing> places;
            std::vector<RE::FormID>     standing;  // per place: the homie there, 0 for none
            bool          filled = false;
            bool          seen = false;  // found by the last scan
            std::string   name;
        };

        struct Homie
        {
            RE::ActorHandle handle;
            RE::FormID      id = 0;
            std::uint16_t   model = 0;
            std::uint8_t    weapon = 0;
            bool            follower = false;
            int             formation = 0;
            std::uint64_t   spot = 0;  // where he stood, 0 for none
            float           deadFor = -1.0f;
            float           offsetIn = 0.0f;
            float           gearIn = 0.0f, cullIn = 0.0f;
            Crew::Burst     burst;
            Crew::Straggler straggler;
            RE::ActorHandle target;
            bool            aiming = false, firing = false;
            RE::NiPoint3    aim;
            Body::Interpolator body, gun;
        };

        struct Remembered
        {
            std::uint16_t model = 0;
            std::uint8_t  weapon = 0;
            float         health = 1.0f;
        };

        struct Bound
        {
            RE::FormID                                id = 0;
            RE::BSTSmartPointer<RE::BSScript::Object> object;
        };

        struct State
        {
            bool                     enabled = true;
            int                      spotHomies = Crew::kSpotHomies;
            float                    health = 150.0f;
            float                    damageFactor = Crew::kDamageFactor;
            int                      forcedThanes = -1;
            std::uint16_t            thanes = 0;
            bool                     thanesLogged = false;
            float                    thanesIn = 0.0f, scanIn = 0.0f, lookIn = 0.0f;
            std::vector<Spot>        spots;
            std::vector<Homie>       homies;
            Crew::Slots              slots;
            std::vector<Remembered>  remake;       // followers to make again behind CJ
            bool                     remakePending = false;
            std::vector<RE::FormID>  purge;        // a loaded save's homies, taken out once it has settled
            bool                     lookForLeftovers = false;
            float                    settle = 0.0f;
            std::vector<RE::ObjectRefHandle> leaving;  // to be disabled, a few a frame
            std::vector<RE::ObjectRefHandle> markers;  // ground probes, deleted the next frame
            std::vector<Bound>       bound;
            std::uint32_t            roll = 0x9E3779B9u;
            std::vector<bool>        keys;  // G presses waiting for the next Update: whether the aim key was held
            RE::TESPackage*          follow = nullptr;  // kFollowPackage, or null to keep them in formation the old way
            unsigned                 followPuts = 0, gearStripped = 0;
            unsigned                 hushedLines = 0;   // subtitles of theirs taken off (a line each)
            unsigned                 hushedSounds = 0;  // their voice handles stopped
            std::uint32_t            handover = 0;
            std::uint32_t            freshFor = 0;
            float                    reanchorWait = 0.0f;
            float                    fresh = 0.0f;  // seconds left in which spots fill in view: the loading screen just went
            unsigned                 made = 0, drawn = 0;
            unsigned                 regroupMisses = 0, regrouped = 0;
            float                    statIn = 5.0f;
        } state;

        std::uint32_t Roll()
        {
            state.roll ^= state.roll << 13;
            state.roll ^= state.roll >> 17;
            state.roll ^= state.roll << 5;
            return state.roll;
        }

        double NowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        Crew::Vec3 V(const RE::NiPoint3& a_p) { return { a_p.x, a_p.y, a_p.z }; }
        RE::NiPoint3 N(const Crew::Vec3& a_p) { return { a_p.x, a_p.y, a_p.z }; }
        Wanted::Point Flat(const RE::NiPoint3& a_p) { return { a_p.x, a_p.y }; }

        RE::Actor* Get(const Homie& a_homie) { return a_homie.handle.get().get(); }

        int Followers()
        {
            return static_cast<int>(std::count_if(state.homies.begin(), state.homies.end(), [](const Homie& a_h) { return a_h.follower; }));
        }

        // ---- Papyrus (KeepOffsetFromActor has no CommonLib function) ----

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
                state.bound.push_back({ a_actor->GetFormID(), object });
            }
            auto*                                                    args = RE::MakeFunctionArguments(std::move(a_args)...);
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            return vm->DispatchMethodCall(object, a_function, args, callback);
        }

        void Unbind(RE::FormID a_id)
        {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            std::erase_if(state.bound, [&](const Bound& a_bound) {
                if (a_bound.id != a_id && a_id != 0) {
                    return false;
                }
                if (vm && a_bound.object) {
                    vm->UnbindObject(a_bound.object);
                }
                return true;
            });
        }

        // ---- The Thane titles ----

        void ReadThanes()
        {
            std::array<int, 9> imperial{}, sons{};
            auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>(Crew::kThaneQuest);
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            bool  read = false;
            if (quest && policy) {
                const auto handle = policy->GetHandleForObject(quest->GetFormType(), quest);
                RE::BSTSmartPointer<RE::BSScript::Object> object;
                if (handle != policy->EmptyHandle() && vm->FindBoundObject(handle, std::string(Crew::kThaneScript).c_str(), object) && object) {
                    read = true;
                    for (std::size_t i = 0; i < Crew::kThaneHolds.size(); i++) {
                        const auto& hold = Crew::kThaneHolds[i];
                        if (auto* v = object->GetProperty(std::string(hold.imperial)); v && v->IsInt()) {
                            imperial[i] = v->GetSInt();
                        }
                        if (auto* v = object->GetProperty(std::string(hold.sons)); v && v->IsInt()) {
                            sons[i] = v->GetSInt();
                        }
                    }
                }
            }
            const auto mask = Crew::ThaneMask(imperial, sons, state.forcedThanes);
            if (mask != state.thanes || !state.thanesLogged) {
                std::string holds;
                for (std::size_t i = 0; i < Crew::kThaneHolds.size(); i++) {
                    if (mask & (1u << i)) {
                        holds += std::format(" {}", Crew::kThaneHolds[i].name);
                    }
                }
                SKSE::log::info("homies: Thane of{} ({} titles, a crew of {}){}", holds.empty() ? " nowhere" : holds, Crew::Titles(mask), Crew::SizeFor(Crew::Titles(mask)),
                    state.forcedThanes >= 0 ? ", as [Debug] CrewThanes says" : read ? "" : ": the Thane quest's script wasn't found");
                state.thanesLogged = true;
            }
            state.thanes = mask;
        }

        int Titles() { return Crew::Titles(state.thanes); }

        // ---- Making and taking out ----

        // The loaded cell a_at is in, in the player's worldspace, else the player's own. A reference belongs in the cell
        // it stands in: a probe's navmesh search (MoveToNearestNavmesh) only looks at its own cell's navmesh, and the
        // player's cell, with CJ walking where Skyrim has none (up against Whiterun's walls), had none in reach.
        RE::TESObjectCELL* CellAt(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at)
        {
            auto* own = a_player->GetParentCell();
            if (!own || own->IsInteriorCell()) {
                return own;
            }
            auto* tes = RE::TES::GetSingleton();
            auto* cell = tes ? tes->GetCell(a_at) : nullptr;
            return cell && cell->IsAttached() && !cell->IsInteriorCell() && cell->GetRuntimeData().worldSpace == a_player->GetWorldspace() ? cell : own;
        }

        RE::NiPointer<RE::TESObjectREFR> Create(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_base, const RE::NiPoint3& a_at, float a_heading,
            RE::TESObjectCELL* a_cell = nullptr)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* cell = a_cell ? a_cell : CellAt(a_player, a_at);
            if (!data || !a_base || !cell) {
                return {};
            }
            return data
                ->CreateReferenceAtLocation(a_base, a_at, RE::NiPoint3(0.0f, 0.0f, a_heading), cell, a_player->GetWorldspace(), nullptr, nullptr,
                    RE::ObjectRefHandle(), false, true)
                .get();
        }

        // The nearest navmesh to a_at, on loaded ground close to it (Police's probe: a marker, deleted the next frame).
        bool GroundAt(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, RE::NiPoint3& a_out, RE::NiPointer<RE::TESObjectREFR>* a_marker = nullptr)
        {
            auto marker = Create(a_player, RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker), a_at, 0.0f);
            if (!marker) {
                return false;
            }
            const bool found = marker->MoveToNearestNavmesh();
            a_out = marker->GetPosition();
            state.markers.push_back(marker->CreateRefHandle());
            if (a_marker) {
                *a_marker = marker;
            }
            auto* cell = marker->GetParentCell();
            return found && std::hypot(a_out.x - a_at.x, a_out.y - a_at.y) <= kSnapFar && cell && cell->IsAttached();
        }

        // The soldier's Skyrim gear that could show or be used: his bow, quiver and arrows, any blade, shield or torch. Only
        // San Andreas's gun is his. His armour stays: it's hidden with the rest of his 3D and keeps him as tough as before.
        // His outfit comes back now and then (on his 3D loading, a reset), so Update looks every kGearEvery.
        void Strip(RE::Actor* a_actor)
        {
            auto gear = a_actor->GetInventory([](RE::TESBoundObject& a_object) {
                if (a_object.IsWeapon() || a_object.IsAmmo() || a_object.Is(RE::FormType::Light)) {
                    return true;
                }
                const auto* armor = a_object.As<RE::TESObjectARMO>();
                return armor && armor->IsShield();
            });
            int removed = 0;
            for (auto& [object, entry] : gear) {
                if (object && entry.first > 0) {
                    a_actor->RemoveItem(object, entry.first, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                    removed += entry.first;
                }
            }
            if (removed && state.gearStripped++ % 20 == 0) {
                SKSE::log::info("homies: {:08X}'s Skyrim gear taken off: {} item(s) ({} times so far)", a_actor->GetFormID(), removed, state.gearStripped);
            }
            if (removed && a_actor->AsActorState()->IsWeaponDrawn()) {
                a_actor->DrawWeaponMagicHands(false);
            }
        }

        // His soldier's hellos to the player ("Hmm?", "Out with it.", "I'm waiting.", as CJ comes near or looks at him)
        // never start: Skyrim only greets the player from an actor that may talk to him, a race's Allow PC Dialogue,
        // which ExtraCanTalkToPlayer overrides for this one actor (Papyrus's AllowPCDialogue). Hush still stops anything
        // else of Skyrim's he says. E never picks a homie, so nothing is lost.
        void Mute(RE::Actor* a_actor)
        {
            if (a_actor->CanTalkToPlayer()) {
                a_actor->AllowPCDialogue(false);
            }
        }

        // A homie at a_at: the soldier in nobody's faction but the player's, with San Andreas's Families model and gun.
        Homie* Make(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, float a_heading, std::uint16_t a_model, std::uint8_t a_weapon, bool a_follower,
            float a_healthShare = 1.0f)
        {
            auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kBase);
            auto  ref = Create(a_player, base, a_at, a_heading, a_player->GetParentCell());  // Skyrim moves an actor on to the cell he walks into
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            if (!actor) {
                if (ref) {
                    state.leaving.push_back(ref->CreateRefHandle());
                }
                SKSE::log::warn("homies: base {:08X} made no actor at ({:.0f},{:.0f},{:.0f})", kBase, a_at.x, a_at.y, a_at.z);
                return nullptr;
            }
            std::vector<RE::TESFaction*> factions;
            actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t a_rank) {
                if (a_faction && a_rank >= 0) {
                    factions.push_back(a_faction);
                }
                return false;
            });
            for (auto* faction : factions) {
                actor->RemoveFromFaction(faction);
            }
            if (auto* player = RE::TESForm::LookupByID<RE::TESFaction>(kPlayerFaction)) {
                actor->AddToFaction(player, 0);
            }
            auto* av = actor->AsActorValueOwner();
            av->SetActorValue(RE::ActorValue::kAggression, 0.0f);     // he starts no fight of his own: his are picked here
            av->SetActorValue(RE::ActorValue::kConfidence, 4.0f);     // and runs from none
            av->SetBaseActorValue(RE::ActorValue::kHealth, state.health);
            if (a_healthShare < 1.0f) {
                av->DamageActorValue(RE::ActorValue::kHealth, state.health * (1.0f - (std::max)(0.05f, a_healthShare)));
            }
            actor->formFlags |= RE::TESObjectREFR::RecordFlags::kIgnoreFriendlyHits;
            actor->SetDisplayName(RE::BSFixedString(std::string(Crew::kHomieName).c_str()), true);  // not "Imperial Soldier"
            Strip(actor);
            Mute(actor);
            Homie h;
            h.handle = actor->CreateRefHandle();
            h.id = actor->GetFormID();
            h.model = a_model;
            h.weapon = a_weapon;
            h.follower = a_follower;
            h.burst = Crew::Burst(Roll());
            if (a_follower) {
                actor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
            } else {
                actor->EnableAI(false);  // he stands about, San Andreas plays his idles
            }
            state.homies.push_back(std::move(h));
            state.made++;
            return &state.homies.back();
        }

        void Leave(Homie& a_homie, const char* a_why)
        {
            if (auto* actor = Get(a_homie)) {
                CallActor(actor, "ClearKeepOffsetFromActor");
                actor->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
                if (state.follow && actor->GetCurrentPackage() == state.follow) {
                    actor->EvaluatePackage(true, false);  // off the follow package, onto his own
                }
                state.leaving.push_back(actor->CreateRefHandle());
            }
            Unbind(a_homie.id);
            state.slots.Free(a_homie.id);
            for (auto& spot : state.spots) {
                for (auto& id : spot.standing) {
                    if (id == a_homie.id) {
                        id = 0;
                    }
                }
            }
            SKSE::log::info("homies: {:08X} goes ({})", a_homie.id, a_why);
        }

        void LeaveAll(bool a_rememberFollowers, const char* a_why)
        {
            if (a_rememberFollowers) {
                state.remake.clear();
            }
            for (auto& h : state.homies) {
                auto* actor = Get(h);
                if (a_rememberFollowers && h.follower && actor && !actor->IsDead()) {
                    auto*       av = actor->AsActorValueOwner();
                    const float max = (std::max)(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth));
                    state.remake.push_back({ h.model, h.weapon, std::clamp(av->GetActorValue(RE::ActorValue::kHealth) / max, 0.05f, 1.0f) });
                }
                Leave(h, a_why);
            }
            state.homies.clear();
            state.spots.clear();
            state.slots.Clear();
            state.remakePending = !state.remake.empty();
        }

        // ---- Spots ----

        std::uint64_t KeyOf(RE::FormID a_world, const RE::NiPoint3& a_at, SpotKind a_kind)
        {
            const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(a_at.x / 64.0f) & 0xFFFFF);
            const auto y = static_cast<std::uint64_t>(static_cast<std::int64_t>(a_at.y / 64.0f) & 0xFFFFF);
            return (static_cast<std::uint64_t>(a_world & 0xFFFF) << 44) ^ (x << 22) ^ y ^ (static_cast<std::uint64_t>(a_kind) << 62);
        }

        int HoldNear(const RE::NiPoint3& a_at, bool* a_walled = nullptr)
        {
            int   best = -1;
            float bestDistance = kHoldNear;
            for (const auto& a : kAnchors) {
                const float d = std::hypot(a.at.x - a_at.x, a.at.y - a_at.y);
                if (d < bestDistance) {
                    bestDistance = d;
                    best = a.hold;
                    if (a_walled) {
                        *a_walled = a.walled;
                    }
                }
            }
            return best;
        }

        void Offer(std::vector<Spot>& a_found, int a_hold, SpotKind a_kind, RE::FormID a_world, const RE::NiPoint3& a_centre, float a_heading, std::string a_name)
        {
            Spot s;
            s.key = KeyOf(a_world, a_centre, a_kind);
            s.hold = a_hold;
            s.kind = a_kind;
            s.world = a_world;
            s.centre = a_centre;
            s.heading = a_heading;
            s.name = std::move(a_name);
            a_found.push_back(std::move(s));
        }

        // The load doors between Tamriel and a city's own worldspace near the player: where walking through one of them
        // puts the player on this side, a few metres further in.
        void FindGates(RE::PlayerCharacter* a_player, std::vector<Spot>& a_found)
        {
            auto* here = a_player->GetWorldspace();
            auto* tes = RE::TES::GetSingleton();
            if (!here || !tes) {
                return;
            }
            const auto feet = a_player->GetPosition();
            tes->ForEachReferenceInRange(a_player, kGateNear * Crew::kMeter, [&](RE::TESObjectREFR* a_ref) {
                if (!a_ref || !a_ref->GetBaseObject() || !a_ref->GetBaseObject()->Is(RE::FormType::Door)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                auto* teleport = a_ref->extraList.GetByType<RE::ExtraTeleport>();
                auto* data = teleport ? teleport->teleportData : nullptr;
                auto  other = data ? data->linkedDoor.get() : RE::NiPointer<RE::TESObjectREFR>();
                auto* there = other ? other->GetWorldspace() : nullptr;
                if (!other || !there || there == here || (here->GetFormID() != kTamriel && there->GetFormID() != kTamriel)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                // Where the far door's teleport puts the player: in front of this one.
                auto* back = other->extraList.GetByType<RE::ExtraTeleport>();
                auto* landing = back ? back->teleportData : nullptr;
                if (!landing) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                RE::NiPoint3 out = landing->position - a_ref->GetPosition();
                out.z = 0.0f;
                const float length = out.Length();
                const RE::NiPoint3 dir = length > 1.0f ? out / length : RE::NiPoint3(0.0f, 1.0f, 0.0f);
                const RE::NiPoint3 centre = landing->position + dir * (6.0f * Crew::kMeter);
                bool       walled = false;
                const int  hold = HoldNear(centre, &walled);
                if (hold < 0) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                const bool inside = here->GetFormID() != kTamriel;
                Offer(a_found, hold, inside ? SpotKind::GateInside : SpotKind::GateOutside, here->GetFormID(), centre, Pose::HeadingOf(dir.x, dir.y),
                    std::format("{} gate {}", Crew::kThaneHolds[static_cast<std::size_t>(hold)].name, inside ? "inside" : "outside"));
                (void)feet;
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        void Scan(RE::PlayerCharacter* a_player)
        {
            auto* world = a_player->GetWorldspace();
            auto* cell = a_player->GetParentCell();
            if (!world || !cell || cell->IsInteriorCell()) {
                return;
            }
            std::vector<Spot> found;
            const auto        feet = a_player->GetPosition();
            if (world->GetFormID() == kTamriel) {
                for (const auto& a : kAnchors) {
                    if (std::hypot(a.at.x - feet.x, a.at.y - feet.y) <= kGateNear * Crew::kMeter) {
                        Offer(found, a.hold, a.walled ? SpotKind::Stable : SpotKind::Town, kTamriel, a.at, 0.0f, a.name);
                    }
                }
            }
            FindGates(a_player, found);
            static std::size_t lastFound = SIZE_MAX;
            if (found.size() != lastFound) {
                lastFound = found.size();
                SKSE::log::info("homies: {} spot(s) near the player at ({:.0f},{:.0f},{:.0f}) in {:08X}", found.size(), feet.x, feet.y, feet.z, world->GetFormID());
            }
            // The hold's share, split among its spots of each kind.
            for (auto& s : found) {
                bool walled = s.kind != SpotKind::Town;
                const auto share = Crew::ShareFor(walled, state.spotHomies);
                const int  total = s.kind == SpotKind::Stable ? share.stable : s.kind == SpotKind::GateOutside ? share.gateOutside :
                                   s.kind == SpotKind::GateInside ? share.gateInside : share.town;
                const int  same = static_cast<int>(std::count_if(found.begin(), found.end(), [&](const Spot& a_o) { return a_o.hold == s.hold && a_o.kind == s.kind; }));
                s.count = (std::max)(2, total / (std::max)(1, same));
            }
            for (auto& spot : state.spots) {
                spot.seen = false;
            }
            for (auto& s : found) {
                auto it = std::find_if(state.spots.begin(), state.spots.end(), [&](const Spot& a_o) { return a_o.key == s.key; });
                if (it != state.spots.end()) {
                    it->seen = true;
                    continue;
                }
                s.places = Crew::Groups(V(s.centre), s.heading, s.count);
                s.standing.assign(s.places.size(), 0);
                s.seen = true;
                SKSE::log::info("homies: a spot at {} for {} ({:.0f},{:.0f},{:.0f}){}", s.name, s.count, s.centre.x, s.centre.y, s.centre.z,
                    (state.thanes >> s.hold) & 1 ? "" : ", not the player's hold");
                state.spots.push_back(std::move(s));
            }
        }

        // Fills and empties the spots of the player's holds, a couple of homies a frame.
        void Tend(RE::PlayerCharacter* a_player, float a_viewHeading)
        {
            const auto feet = a_player->GetPosition();
            const auto world = a_player->GetWorldspace() ? a_player->GetWorldspace()->GetFormID() : 0;
            const auto camera = Wanted::CameraOf(Flat(feet), a_viewHeading);
            int        made = 0;
            for (auto& spot : state.spots) {
                const bool  ours = (state.thanes >> spot.hold) & 1;
                const float distance = spot.world == world ? std::hypot(spot.centre.x - feet.x, spot.centre.y - feet.y) / Crew::kMeter : INFINITY;
                if (!ours || Crew::Empties(distance, true)) {
                    for (auto& id : spot.standing) {
                        if (!id) {
                            continue;
                        }
                        auto it = std::find_if(state.homies.begin(), state.homies.end(), [&](const Homie& a_h) { return a_h.id == id && !a_h.follower; });
                        if (it != state.homies.end()) {
                            Leave(*it, "his spot is far");
                            state.homies.erase(it);
                        }
                        id = 0;
                    }
                    spot.filled = false;
                    continue;
                }
                const bool missing = std::any_of(spot.standing.begin(), spot.standing.end(), [](RE::FormID a_id) { return a_id == 0; });
                const bool fresh = state.fresh > 0.0f;
                const bool inView = !fresh && Wanted::InView(camera, a_viewHeading, Flat(spot.centre));
                if (!Crew::Fills(distance, inView, !missing)) {
                    continue;
                }
                for (std::size_t i = 0; i < spot.places.size() && made < (fresh ? 8 : kMakesPerFrame); i++) {
                    if (spot.standing[i]) {
                        continue;
                    }
                    const auto&  place = spot.places[i];
                    RE::NiPoint3 at;
                    if (!GroundAt(a_player, RE::NiPoint3(place.at.x, place.at.y, spot.centre.z + 50.0f), at) ||
                        (!fresh && Wanted::InView(camera, a_viewHeading, Flat(at)))) {
                        continue;
                    }
                    const auto model = Crew::ModelFor(Roll());
                    if (auto* h = Make(a_player, at, place.heading, model, Crew::WeaponFor(Titles(), Roll()), false)) {
                        h->spot = spot.key;
                        spot.standing[i] = h->id;
                        made++;
                    }
                }
                spot.filled = std::none_of(spot.standing.begin(), spot.standing.end(), [](RE::FormID a_id) { return a_id == 0; });
            }
            // Spots no scan found any more (another worldspace) are dropped once empty.
            std::erase_if(state.spots, [](const Spot& a_s) {
                return !a_s.seen && std::none_of(a_s.standing.begin(), a_s.standing.end(), [](RE::FormID a_id) { return a_id != 0; });
            });
        }

        // ---- The crew ----

        int FreeFormation()
        {
            for (int i = 0; i < Crew::kFollowerMax; i++) {
                if (std::none_of(state.homies.begin(), state.homies.end(), [&](const Homie& a_h) { return a_h.follower && a_h.formation == i; })) {
                    return i;
                }
            }
            return Crew::kFollowerMax - 1;
        }

        void TellPlace(RE::PlayerCharacter* a_player, Homie& a_homie)
        {
            auto* actor = Get(a_homie);
            if (!actor) {
                return;
            }
            if (state.follow) {
                // Skyrim's own follower package: he paths after the player round rocks and walls as Lydia does, keeps 4 to
                // 5.5 m off and stops there. Put on him as a temporary package, which his AI drops when it re-evaluates
                // (a fight called off, a door): looked at again every kFollowEvery.
                a_homie.offsetIn = kFollowEvery;
                if (actor->GetCurrentPackage() == state.follow) {
                    return;
                }
                actor->PutCreatedPackage(state.follow, true, false, true);
                if (state.followPuts++ % 20 == 0) {
                    auto* now = actor->GetCurrentPackage();
                    SKSE::log::info("homies: {:08X} put on the follow package ({} so far); he runs {:08X}{}", a_homie.id, state.followPuts,
                        now ? now->GetFormID() : 0, now == state.follow ? "" : ": NOT the follow package");
                }
                return;
            }
            const auto offset = Crew::FormationOffset(a_homie.formation);
            if (!CallActor(actor, "KeepOffsetFromActor", static_cast<RE::Actor*>(a_player), offset.x, offset.y, 0.0f, 0.0f, 0.0f, 0.0f, 300.0f, 60.0f)) {
                SKSE::log::warn("homies: {:08X} couldn't be told his place (KeepOffsetFromActor didn't go)", a_homie.id);
            }
            a_homie.offsetIn = kOffsetEvery;
        }

        // A follower whose AI found no way to CJ (Crew::Straggler): put on the navmesh behind CJ, out of view, near enough
        // to come up to his place (Wanted::SpawnSpot's near ring, straight behind first, then round to either side).
        void Regroup(RE::PlayerCharacter* a_player, Homie& a_homie, RE::Actor* a_actor, float a_viewHeading)
        {
            const auto  feet = a_player->GetPosition();
            const auto  camera = Wanted::CameraOf(Flat(feet), a_viewHeading);
            const float was = a_actor->GetPosition().GetDistance(feet) / Crew::kMeter;
            for (int i = 0; i < kRegroupTries; i++) {
                const auto   spot = Wanted::SpawnSpot(Flat(feet), a_viewHeading, i, Wanted::kSpawnNear + 100.0f);
                RE::NiPoint3                     at;
                RE::NiPointer<RE::TESObjectREFR> marker;
                if (!GroundAt(a_player, RE::NiPoint3(spot.x, spot.y, feet.z + 50.0f), at, &marker) || std::abs(at.z - feet.z) > kRegroupRise ||
                    Wanted::InView(camera, a_viewHeading, Flat(at))) {
                    continue;
                }
                a_actor->MoveTo(marker.get());  // into the marker's cell, which SetPosition wouldn't change
                TellPlace(a_player, a_homie);
                state.regrouped++;
                SKSE::log::info("homies: {:08X} was {:.0f} m off with no way to CJ: put back {:.0f} m behind him", a_homie.id, was, at.GetDistance(feet) / Crew::kMeter);
                return;
            }
            // None there (CJ in a cell with no navmesh at all): the navmesh nearest CJ in the follower's own cell, as close
            // as his AI can get, if that's out of view and kRegroupGain nearer.
            auto*       own = CellAt(a_player, a_actor->GetPosition());
            std::string why = own && own->IsAttached() ? "no navmesh in his cell" : "his cell isn't loaded";
            auto        marker = own && own->IsAttached() ? Create(a_player, RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker), feet, 0.0f, own) : nullptr;
            if (marker) {
                state.markers.push_back(marker->CreateRefHandle());
                if (marker->MoveToNearestNavmesh()) {
                    const auto  at = marker->GetPosition();
                    const float off = at.GetDistance(feet) / Crew::kMeter;
                    const bool  seen = Wanted::InView(camera, a_viewHeading, Flat(at));
                    if (was - off >= kRegroupGain && std::abs(at.z - feet.z) <= kRegroupRise && !seen) {
                        a_actor->MoveTo(marker.get());
                        TellPlace(a_player, a_homie);
                        state.regrouped++;
                        SKSE::log::info("homies: {:08X} was {:.0f} m off with no way to CJ: put back on the navmesh nearest him, {:.0f} m off", a_homie.id, was, off);
                        return;
                    }
                    why = std::format("the nearest in his cell {:.0f} m from CJ{}{}", off, seen ? ", in view" : "",
                        std::abs(at.z - feet.z) > kRegroupRise ? ", too high or low" : "");
                }
            }
            if (state.regroupMisses++ % 10 == 0) {
                SKSE::log::info("homies: {:08X} is {:.0f} m off and no navmesh behind CJ out of view to put him back on ({}; tried again every {:.0f} s)", a_homie.id,
                    was, why, Crew::kRegroupWait);
            }
        }

        void Join(RE::PlayerCharacter* a_player, Homie& a_homie)
        {
            auto* actor = Get(a_homie);
            if (!actor) {
                return;
            }
            for (auto& spot : state.spots) {
                for (auto& id : spot.standing) {
                    if (id == a_homie.id) {
                        id = 0;  // the spot fills again, out of view
                    }
                }
            }
            a_homie.follower = true;
            a_homie.spot = 0;
            a_homie.formation = FreeFormation();
            actor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
            actor->EnableAI(true);
            TellPlace(a_player, a_homie);
            SKSE::log::info("homies: {:08X} joins the crew ({} of {}), place {}", a_homie.id, Followers(), Crew::SizeFor(Titles()), a_homie.formation);
        }

        void Dismiss(Homie& a_homie)
        {
            a_homie.follower = false;
            Leave(a_homie, "dismissed");
        }

        // Every living homie, followers and not, for G: the one CJ's lock-on is on, or, aiming a gun, the one nearest his
        // crosshair (as LockOn marks an enemy), flagged.
        std::vector<Crew::Candidate> Candidates(RE::FormID a_target)
        {
            float x = 0.0f, y = 0.0f, aspect = 0.0f;
            auto* camera = RE::Main::WorldRootCamera();
            const bool crosshair = camera && SaHud::AimCrosshair(x, y, aspect);
            std::vector<Crew::Candidate> out;
            std::vector<Melee::OnScreen> shown;
            for (const auto& h : state.homies) {
                auto* actor = Get(h);
                if (!actor || actor->IsDead()) {
                    continue;
                }
                out.push_back({ h.id, V(actor->GetPosition()), h.id == a_target });
                Melee::OnScreen on;
                if (crosshair) {
                    float depth = 0.0f;
                    on.visible = camera->WorldPtToScreenPt3(actor->GetPosition() + RE::NiPoint3(0.0f, 0.0f, 0.65f * actor->GetHeight()), on.x, on.y, depth, 1e-5f);
                    on.y = 1.0f - on.y;  // Skyrim's screen points go up
                    on.distance = actor->GetPosition().GetDistance(RE::PlayerCharacter::GetSingleton()->GetPosition()) / Crew::kMeter;
                }
                shown.push_back(on);
            }
            if (crosshair) {
                if (const auto closest = Melee::NearCrosshair(shown, x, y, aspect); !closest.empty()) {
                    out[closest.front()].targeted = true;
                }
            }
            return out;
        }

        // G: only while CJ aims, at the homie he aims at (Crew::AimPick). One standing about joins, one of the crew
        // leaves. Typed in a cheat, with no aim, it does nothing.
        void HandleKey(RE::PlayerCharacter* a_player, bool a_aiming, float a_heading, RE::FormID a_target)
        {
            if (!a_aiming) {
                return;
            }
            const auto pick = Crew::AimPick(V(a_player->GetPosition()), a_heading, Candidates(a_target));
            auto       it = std::find_if(state.homies.begin(), state.homies.end(), [&](const Homie& a_h) { return a_h.id == pick; });
            if (!pick || it == state.homies.end()) {
                SKSE::log::info("homies: G while aiming, at no homie");
                return;
            }
            if (it->follower) {
                Dismiss(*it);
                state.homies.erase(it);
                return;
            }
            if (Titles() == 0) {
                SaHud::ShowHelp(std::string(Crew::kNoThaneLine), 4.0f);
                return;
            }
            if (Followers() >= Crew::SizeFor(Titles())) {
                SaHud::ShowHelp(std::string(Crew::kFullLine), 4.0f);
                SKSE::log::info("homies: the crew is full at {}", Crew::SizeFor(Titles()));
                return;
            }
            Join(a_player, *it);
        }

        // Their soldier's Skyrim lines (greetings, idle chatter, combat calls): a homie says nothing of Skyrim's. His
        // voice is stopped as it starts and its subtitle taken off.
        void Hush()
        {
            for (auto& h : state.homies) {
                auto* actor = Get(h);
                auto* process = actor ? actor->GetActorRuntimeData().currentProcess : nullptr;
                auto* high = process ? process->high : nullptr;
                if (!high) {
                    continue;
                }
                for (auto& sound : high->soundHandles) {
                    if (sound.IsPlaying()) {
                        sound.Stop();
                        if (state.hushedSounds++ % 20 == 0) {
                            SKSE::log::info("homies: a homie's Skyrim voice stopped ({} so far)", state.hushedSounds);
                        }
                    }
                }
            }
            if (auto* manager = RE::SubtitleManager::GetSingleton(); manager && !state.homies.empty()) {
                RE::BSSpinLockGuard guard(manager->lock);
                for (auto it = manager->subtitles.begin(); it != manager->subtitles.end();) {
                    auto speaker = it->speaker.get();
                    if (speaker && IsHomie(speaker.get())) {
                        if (state.hushedLines++ < 10) {
                            SKSE::log::info("homies: a homie's Skyrim subtitle taken off: \"{}\"", it->subtitle.c_str());
                        }
                        it = manager->subtitles.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }

        // Whether a_actor is one the followers shoot (Crew::IsFoe): hostile to the player, or fighting him or the crew
        // without fleeing; never an animal that isn't aggressive (an elk running from CJ has him as its combat target).
        bool IsFoe(RE::PlayerCharacter* a_player, RE::Actor* a_actor)
        {
            auto*       foe = a_actor->GetActorRuntimeData().currentCombatTarget.get().get();
            const bool  hostile = a_actor->IsHostileToActor(a_player);
            const bool  fightsUs = foe == a_player || (foe && IsHomie(foe));
            if (!hostile && !fightsUs) {
                return false;  // the common case, before the keyword scan
            }
            const auto* combat = a_actor->GetActorRuntimeData().combatController;
            const bool  fleeing = combat && combat->state && combat->IsFleeing();
            const bool  animal = a_actor->HasKeywordString("ActorTypeAnimal");
            const bool  aggressive = a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression) > 0.0f;
            return Crew::IsFoe(hostile, fightsUs, fleeing, animal, aggressive);
        }

        // The followers' targets: the nearest foe (IsFoe) in sight and in his gun's range. Skyrim's own fights are
        // called off: theirs are picked here.
        void Look(RE::PlayerCharacter* a_player)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            const auto                   cj = a_player->GetPosition();
            std::vector<RE::Actor*>      enemies;
            for (auto& handle : lists->highActorHandles) {
                auto  ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == a_player || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded() || IsHomie(actor) ||
                    actor->IsPlayerTeammate() || actor->GetPosition().GetDistance(cj) > kTargetRange * Crew::kMeter) {
                    continue;
                }
                if (IsFoe(a_player, actor)) {
                    enemies.push_back(actor);
                }
            }
            for (auto& h : state.homies) {
                auto* actor = Get(h);
                h.target = {};
                if (!actor || !h.follower || actor->IsDead()) {
                    continue;
                }
                if (actor->IsInCombat()) {
                    actor->StopCombat();
                }
                RE::Actor* best = nullptr;
                float      bestDistance = INFINITY;
                for (auto* e : enemies) {
                    const float d = e->GetPosition().GetDistance(actor->GetPosition()) / Crew::kMeter;
                    bool        unused = false;
                    if (d < bestDistance && Crew::MayShoot(d, h.weapon, actor->HasLineOfSight(e, unused))) {
                        best = e;
                        bestDistance = d;
                    }
                }
                if (best) {
                    h.target = best->CreateRefHandle();
                }
            }
        }

        RE::NiPoint3 ChestOf(RE::Actor* a_actor)
        {
            const float height = (std::max)(60.0f, (a_actor->GetBoundMax().z - a_actor->GetBoundMin().z) * a_actor->GetScale());
            return a_actor->GetPosition() + RE::NiPoint3(0.0f, 0.0f, height * 0.65f);
        }

        // His Skyrim 3D hidden, shape by shape (the skeleton stays for his AI), again every kCullEvery: a drawn weapon, a
        // quiver or an outfit put back adds shapes to it after the first pass.
        void Cull(Homie& a_homie, RE::Actor* a_actor, float a_delta)
        {
            auto* root = a_actor->Get3D();
            if (!root || (a_homie.cullIn -= a_delta) > 0.0f) {
                return;
            }
            a_homie.cullIn = kCullEvery;
            RE::BSVisit::TraverseScenegraphGeometries(root, [](RE::BSGeometry* a_shape) {
                if (!a_shape->GetAppCulled()) {
                    a_shape->SetAppCulled(true);
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        }

        std::uint32_t CopyId(std::uint32_t a_slot, std::uint32_t a_of, bool a_gun)
        {
            return kCopyBase | (a_slot << 16) | ((a_of & 0x3FFF) << 2) | (a_gun ? 2u : 0u);
        }

        void Draw(Homie& a_homie, int a_slot, RE::Actor* a_actor, std::uint32_t a_handover)
        {
            const double       now = NowMs();
            Protocol::CrewBody body{};
            if (LinkService::Get().ReadCrew(static_cast<std::uint32_t>(a_slot), body) && body.id == a_homie.id) {
                if ((body.body.flags & Protocol::kBodyValid) && body.body.handover == a_handover && Body::Finite(body.body)) {
                    a_homie.body.Push(body.body, now);
                }
                if ((body.weapon.flags & Protocol::kBodyValid) && body.weapon.handover == a_handover && Body::Finite(body.weapon)) {
                    a_homie.gun.Push(body.weapon, now);
                } else {
                    a_homie.gun.Reset();
                }
            }
            const auto         feet = a_actor->GetPosition();
            Protocol::BodyState pose{};
            if (a_homie.body.Sample(now, pose) && ModelView::Has(pose.model)) {
                const auto copy = CopyId(static_cast<std::uint32_t>(a_slot), pose.model, false);
                if (ModelView::AddCopy(pose.model, copy)) {
                    pose.model = copy;
                    if (ModelView::Show(pose, feet)) {
                        state.drawn++;
                    }
                }
            }
            Protocol::BodyState gun{};
            if (a_homie.gun.Sample(now, gun) && ModelView::Has(gun.model)) {
                const auto copy = CopyId(static_cast<std::uint32_t>(a_slot), gun.model, true);
                if (ModelView::AddCopy(gun.model, copy)) {
                    gun.model = copy;
                    ModelView::Show(gun, feet);
                }
            }
        }

        void Remake(RE::PlayerCharacter* a_player)
        {
            const auto feet = a_player->GetPosition();
            const float heading = a_player->GetAngleZ();
            int         i = 0;
            for (const auto& r : state.remake) {
                const auto   place = Crew::FormationAt(V(feet), heading, i);
                RE::NiPoint3 at;
                if (!GroundAt(a_player, N(place) + RE::NiPoint3(0.0f, 0.0f, 50.0f), at)) {
                    at = feet;
                }
                if (auto* h = Make(a_player, at, heading, r.model, r.weapon, true, r.health)) {
                    h->formation = i;
                    TellPlace(a_player, *h);
                }
                i++;
            }
            SKSE::log::info("homies: {} follower(s) back behind CJ", state.remake.size());
            state.remake.clear();
            state.remakePending = false;
        }
    }

    void OnDataLoaded()
    {
        const auto number = [](const char* a_section, const char* a_key, float a_default) {
            const auto text = Ini::Value(a_section, a_key);
            if (text.empty()) {
                return a_default;
            }
            try {
                return std::stof(text);
            } catch (...) {
                SKSE::log::warn("homies: [{}] {}={} isn't a number, kept {}", a_section, a_key, text, a_default);
                return a_default;
            }
        };
        state.enabled = number("Crew", "Enabled", 1.0f) != 0.0f;
        state.spotHomies = std::clamp(static_cast<int>(number("Crew", "SpotHomies", static_cast<float>(Crew::kSpotHomies))), 0, 15);
        state.health = (std::max)(10.0f, number("Crew", "Health", 150.0f));
        state.damageFactor = (std::max)(0.0f, number("Crew", "DamageFactor", Crew::kDamageFactor));
        state.forcedThanes = static_cast<int>(number("Debug", "CrewThanes", -1.0f));
        state.follow = nullptr;
        if (number("Crew", "FollowPackage", 1.0f) != 0.0f) {
            state.follow = RE::TESForm::LookupByID<RE::TESPackage>(kFollowPackage);
            if (!state.follow) {
                SKSE::log::warn("homies: no follow package {:08X} (PlayerFollowerPackage): the followers keep to their formation instead", kFollowPackage);
            }
        }
        SKSE::log::info("homies: the followers {}", state.follow ? "follow with Skyrim's follower package" : "keep to San Andreas's formation (KeepOffsetFromActor)");
        SKSE::log::info("homies: the crew {} ({} homies a hold, health {:.0f}, their shots {:.2f} of CJ's{})", state.enabled ? "is on" : "is off ([Crew] Enabled=0)",
            state.spotHomies, state.health, state.damageFactor, state.forcedThanes >= 0 ? std::format(", [Debug] CrewThanes={}", state.forcedThanes) : "");
    }

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, const Protocol::PlayerState* a_pose, bool a_onFoot,
        RE::FormID a_target, float a_delta)
    {
        if (!state.enabled || !a_player) {
            return;
        }
        state.handover = a_handover;
        state.slots.Tick(a_delta);
        auto* cell = a_player->GetParentCell();
        const bool settled = a_player->Is3DLoaded() && cell && cell->IsAttached() && !state.lookForLeftovers;
        const float viewHeading = a_pose ? a_pose->viewHeading : a_player->GetAngleZ();

        if ((state.thanesIn -= a_delta) <= 0.0f) {
            state.thanesIn = kThanesEvery;
            ReadThanes();
        }
        if (state.remakePending && settled) {
            state.reanchorWait -= a_delta;
            if (state.reanchorWait <= 0.0f) {
                Remake(a_player);
            }
        }
        if (a_handover != state.freshFor) {
            state.freshFor = a_handover;  // a handover or a re-anchor: its first moments come out of a load or F10
            state.fresh = 3.0f;
            state.scanIn = 0.0f;
        }
        if (settled && (state.scanIn -= a_delta) <= 0.0f) {
            state.scanIn = kScanEvery;
            Scan(a_player);
        }
        if (settled) {
            state.fresh -= a_delta;
        }
        if (settled) {
            Tend(a_player, viewHeading);
        }
        Hush();
        for (const bool aimHeld : std::exchange(state.keys, {})) {
            if (a_onFoot) {
                const bool aiming = aimHeld || (a_pose && (a_pose->weaponFlags & Protocol::kWeaponAiming));
                HandleKey(a_player, aiming, a_pose ? a_pose->heading : a_player->GetAngleZ(), a_target);
            }
        }
        if ((state.lookIn -= a_delta) <= 0.0f) {
            state.lookIn = kLookEvery;
            Look(a_player);
        }

        // Each homie: dead, lost, his fight, his place, his 3D hidden, then San Andreas's input and his drawing.
        const auto  cj = a_player->GetPosition();
        const float cjHeading = a_pose ? a_pose->heading : a_player->GetAngleZ();
        Protocol::CrewInput input{};
        input.frame = static_cast<std::uint32_t>(RE::GetDurationOfApplicationRunTime());
        input.handover = a_handover;
        for (auto it = state.homies.begin(); it != state.homies.end();) {
            auto& h = *it;
            auto* actor = Get(h);
            if (!actor || actor->IsDisabled()) {
                Leave(h, "gone");
                it = state.homies.erase(it);
                continue;
            }
            const float distance = actor->GetPosition().GetDistance(cj) / Crew::kMeter;
            if (actor->IsDead()) {
                if (h.deadFor < 0.0f) {
                    h.deadFor = 0.0f;
                    if (h.follower) {
                        h.follower = false;
                        SKSE::log::info("homies: {:08X} of the crew is dead", h.id);
                    }
                }
                h.deadFor += a_delta;
                if (h.deadFor > kDeadFor || distance > kDrawWithin) {
                    Leave(h, "dead");
                    it = state.homies.erase(it);
                    continue;
                }
            } else if (h.follower) {
                if (distance > Crew::kLostAfter) {
                    Leave(h, "lost");
                    it = state.homies.erase(it);
                    continue;
                }
                // Put back only on foot (a car's passengers are Riders'), in the player's worldspace (a load door or fast
                // travel is Remake's), and not while a remake or a load's first moments are under way.
                auto*      at = actor->GetParentCell();
                const bool here = at && at->IsAttached() && actor->GetWorldspace() == a_player->GetWorldspace() &&
                                  (!at->IsInteriorCell() || at == cell);
                if (!a_onFoot || !settled || !here || state.remakePending || state.fresh > 0.0f || Riders::Rides(h.id)) {
                    h.straggler.Hold();
                } else if (h.straggler.Update(distance, a_delta)) {
                    Regroup(a_player, h, actor, viewHeading);
                }
                if ((h.offsetIn -= a_delta) <= 0.0f && !Riders::Rides(h.id)) {
                    TellPlace(a_player, h);  // not while he rides with CJ: Riders seats him
                }
            }
            if (actor->Is3DLoaded()) {
                Cull(h, actor, a_delta);
            }
            if ((h.gearIn -= a_delta) <= 0.0f) {
                h.gearIn = kGearEvery;
                Strip(actor);
                Mute(actor);
            }
            // His fight: aim at his target and fire in bursts.
            auto* target = h.target.get().get();
            auto* targetActor = target ? target->As<RE::Actor>() : nullptr;
            const bool may = targetActor && !targetActor->IsDead() && !actor->IsDead();
            h.firing = h.burst.Update(a_delta, may);
            h.aiming = may;
            if (may) {
                h.aim = ChestOf(targetActor);
            }
            const int slot = distance <= kDrawWithin && actor->Is3DLoaded() ? state.slots.Take(h.id) : -1;
            if (slot >= 0) {
                auto& m = input.members[slot];
                m.id = h.id;
                m.model = h.model;
                m.weapon = h.weapon;
                m.flags = Protocol::kCrewPresent;
                if (h.follower) {
                    m.flags |= Protocol::kCrewFollower;
                }
                if (actor->IsDead()) {
                    m.flags |= Protocol::kCrewDead;
                }
                if (Riders::Sits(h.id)) {  // not once he steps out with CJ (Riders): he stands up as he walks out
                    m.flags |= Protocol::kCrewSeated;
                }
                if (h.aiming) {
                    m.flags |= Protocol::kCrewAiming;
                }
                if (h.firing) {
                    m.flags |= Protocol::kCrewFiring;
                }
                const auto feet = actor->GetPosition() - a_anchor;
                m.feet[0] = feet.x;
                m.feet[1] = feet.y;
                m.feet[2] = feet.z;
                // Which way he faces standing: his aim, CJ's way for a follower on foot, else his own. San Andreas
                // turns him the way he goes while he walks or runs (Crew::Facing).
                if (h.aiming) {
                    m.heading = Pose::HeadingOf(h.aim.x - actor->GetPosition().x, h.aim.y - actor->GetPosition().y);
                } else if (h.follower && !Riders::Rides(h.id)) {
                    m.heading = cjHeading;
                } else {
                    m.heading = actor->GetAngleZ();
                }
                const auto aim = (h.aiming ? h.aim : actor->GetPosition() + RE::NiPoint3(std::sin(m.heading), std::cos(m.heading), 0.0f) * 500.0f) - a_anchor;
                m.aim[0] = aim.x;
                m.aim[1] = aim.y;
                m.aim[2] = aim.z;
                Draw(h, slot, actor, a_handover);
            } else {
                state.slots.Free(h.id);
            }
            ++it;
        }
        LinkService::Get().WriteCrewInput(input);

        if ((state.statIn -= a_delta) <= 0.0f) {
            state.statIn = 5.0f;
            if (!state.homies.empty() || state.made) {
                SKSE::log::info("homies: {} about ({} following, crew of {}), {} spots, {} made and {} drawn in the last 5 s", state.homies.size(), Followers(),
                    Crew::SizeFor(Titles()), state.spots.size(), state.made, state.drawn);
            }
            state.made = state.drawn = 0;
        }
    }

    void Sweep(float a_delta)
    {
        for (auto& marker : state.markers) {
            if (auto ref = marker.get()) {
                ref->SetDelete(true);
            }
        }
        state.markers.clear();
        int steps = 0;
        while (!state.leaving.empty() && steps++ < 4) {
            if (auto ref = state.leaving.back().get()) {
                ref->Disable();
            }
            state.leaving.pop_back();
        }
        if (state.lookForLeftovers) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            const bool settled = player && player->Is3DLoaded() && cell && cell->IsAttached() && !(ui && (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->GameIsPaused()));
            state.settle = settled ? state.settle + a_delta : 0.0f;
            if (state.settle >= Wanted::kLeftoverSettle) {
                state.lookForLeftovers = false;
                int taken = 0;
                for (const auto id : state.purge) {
                    if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id); actor && !actor->IsDisabled()) {
                        actor->Disable();
                        taken++;
                    }
                }
                SKSE::log::info("homies: the loaded game has settled: {} homie(s) the save kept taken out, {} follower(s) to come back", taken, state.remake.size());
                state.purge.clear();
            }
        }
    }

    void OnKey(bool a_aiming)
    {
        state.keys.push_back(a_aiming);
    }

    void OnRelease(bool a_loading)
    {
        state.keys.clear();
        if (a_loading) {
            // The world goes with the load: nothing of it is touched. The save being loaded says who comes back.
            state.homies.clear();
            state.spots.clear();
            state.slots.Clear();
            state.leaving.clear();
            state.markers.clear();
            Unbind(0);
            return;
        }
        LeaveAll(true, "Skyrim has the player back");
        LinkService::Get().WriteCrewInput(Protocol::CrewInput{});
    }

    void OnReanchor()
    {
        LeaveAll(true, "the player moved");
        state.reanchorWait = 1.0f;  // once the new cell is in
    }

    bool IsHomie(const RE::TESObjectREFR* a_ref)
    {
        if (!a_ref) {
            return false;
        }
        const auto id = a_ref->GetFormID();
        return std::any_of(state.homies.begin(), state.homies.end(), [&](const Homie& a_h) { return a_h.id == id; });
    }

    RE::Actor* ActorOf(std::uint32_t a_slot)
    {
        const auto id = state.slots.IdAt(a_slot);
        if (!id) {
            return nullptr;
        }
        for (const auto& h : state.homies) {
            if (h.id == id) {
                return Get(h);
            }
        }
        return nullptr;
    }

    float DamageFactor() { return state.damageFactor; }

    void DebugThanes(int a_count)
    {
        state.forcedThanes = a_count;
        state.thanesIn = 0.0f;
    }

    int DebugRecruit(RE::PlayerCharacter* a_player, int a_count)
    {
        int joined = 0;
        const auto feet = a_player->GetPosition();
        std::sort(state.homies.begin(), state.homies.end(), [&](const Homie& a, const Homie& b) {
            auto* x = Get(a);
            auto* y = Get(b);
            return (x ? x->GetPosition().GetDistance(feet) : 1e9f) < (y ? y->GetPosition().GetDistance(feet) : 1e9f);
        });
        for (auto& h : state.homies) {
            auto* actor = Get(h);
            if (joined >= a_count || h.follower || !actor || actor->IsDead()) {
                continue;
            }
            if (Followers() >= Crew::SizeFor(Titles())) {
                SKSE::log::info("homies: the crew is full at {}", Crew::SizeFor(Titles()));
                break;
            }
            Join(a_player, h);
            joined++;
        }
        return joined;
    }

    bool DebugTargets(const RE::Actor* a_actor)
    {
        for (const auto& h : state.homies) {
            if (h.follower && a_actor && h.target.get().get() == a_actor) {
                return true;
            }
        }
        return false;
    }

    unsigned DebugHushed() { return state.hushedLines; }
    unsigned DebugHushedSounds() { return state.hushedSounds; }

    int DebugCount(bool a_followers)
    {
        return a_followers ? Followers() : static_cast<int>(state.homies.size());
    }

    std::vector<RE::Actor*> DebugFollowers()
    {
        std::vector<RE::Actor*> out;
        for (const auto& h : state.homies) {
            if (auto* actor = h.follower ? Get(h) : nullptr) {
                out.push_back(actor);
            }
        }
        return out;
    }

    int DebugRegrouped() { return static_cast<int>(state.regrouped); }

    float DebugStray(RE::PlayerCharacter* a_player, float a_metres)
    {
        auto* follower = DebugFollower();
        if (!follower) {
            return -1.0f;
        }
        const auto feet = a_player->GetPosition();
        for (int i = 0; i < 8; i++) {
            const auto                       spot = Wanted::SpawnSpot(Flat(feet), a_player->GetAngleZ(), i, a_metres * Crew::kMeter);
            RE::NiPoint3                     at;
            RE::NiPointer<RE::TESObjectREFR> marker;
            if (GroundAt(a_player, RE::NiPoint3(spot.x, spot.y, feet.z + 50.0f), at, &marker) && std::abs(at.z - feet.z) <= kRegroupRise) {
                follower->MoveTo(marker.get());
                return at.GetDistance(feet) / Crew::kMeter;
            }
        }
        return -1.0f;
    }

    bool DebugNavmesh(RE::PlayerCharacter* a_player, RE::NiPoint3& a_nearest)
    {
        auto marker = Create(a_player, RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker), a_player->GetPosition(), 0.0f);
        if (!marker) {
            return false;
        }
        const bool found = marker->MoveToNearestNavmesh();
        a_nearest = marker->GetPosition();
        state.markers.push_back(marker->CreateRefHandle());
        return found;
    }

    RE::Actor* DebugFollower()
    {
        for (const auto& h : state.homies) {
            if (h.follower) {
                return Get(h);
            }
        }
        return nullptr;
    }

    void Save(SKSE::SerializationInterface* a_intfc)
    {
        std::vector<std::uint8_t> bytes;
        const auto put = [&](const void* a_p, std::size_t a_n) {
            const auto* p = static_cast<const std::uint8_t*>(a_p);
            bytes.insert(bytes.end(), p, p + a_n);
        };
        std::vector<Remembered> followers = state.remake;
        std::vector<RE::FormID> ids;
        for (const auto& h : state.homies) {
            ids.push_back(h.id);
            auto* actor = Get(h);
            if (h.follower && actor && !actor->IsDead()) {
                auto*       av = actor->AsActorValueOwner();
                const float max = (std::max)(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth));
                followers.push_back({ h.model, h.weapon, std::clamp(av->GetActorValue(RE::ActorValue::kHealth) / max, 0.05f, 1.0f) });
            }
        }
        for (const auto& l : state.leaving) {
            if (auto ref = l.get()) {
                ids.push_back(ref->GetFormID());
            }
        }
        const auto followerCount = static_cast<std::uint32_t>(followers.size());
        put(&followerCount, 4);
        for (const auto& f : followers) {
            const std::uint8_t pad = 0;
            put(&f.model, 2);
            put(&f.weapon, 1);
            put(&pad, 1);
            put(&f.health, 4);
        }
        const auto idCount = static_cast<std::uint32_t>(ids.size());
        put(&idCount, 4);
        for (const auto id : ids) {
            put(&id, 4);
        }
        if (!a_intfc->WriteRecord(kRecordType, kRecordVersion, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
            SKSE::log::error("homies: the crew's co-save record could not be written");
            return;
        }
        SKSE::log::info("homies: saved {} follower(s) and {} homie(s) to take out on load", followers.size(), ids.size());
    }

    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size)
    {
        state.remake.clear();
        state.purge.clear();
        if (a_version != kRecordVersion) {
            SKSE::log::warn("homies: the crew's co-save record is version {}, not {}: ignored", a_version, kRecordVersion);
            return;
        }
        std::size_t at = 0;
        const auto  get = [&](void* a_p, std::size_t a_n) {
            if (at + a_n > a_size) {
                return false;
            }
            std::memcpy(a_p, a_data + at, a_n);
            at += a_n;
            return true;
        };
        std::uint32_t count = 0;
        if (!get(&count, 4) || count > 64) {
            return;
        }
        for (std::uint32_t i = 0; i < count; i++) {
            Remembered r;
            std::uint8_t pad = 0;
            if (!get(&r.model, 2) || !get(&r.weapon, 1) || !get(&pad, 1) || !get(&r.health, 4)) {
                return;
            }
            if (std::find(Crew::kModels.begin(), Crew::kModels.end(), r.model) != Crew::kModels.end()) {
                state.remake.push_back(r);
            }
        }
        std::uint32_t ids = 0;
        if (get(&ids, 4) && ids <= 4096) {
            for (std::uint32_t i = 0; i < ids; i++) {
                RE::FormID id = 0;
                if (!get(&id, 4)) {
                    break;
                }
                state.purge.push_back(id);
            }
        }
        state.lookForLeftovers = true;
        state.settle = 0.0f;
        state.remakePending = !state.remake.empty();
        state.reanchorWait = 1.0f;
        SKSE::log::info("homies: loaded {} follower(s), {} homie(s) of the save to take out once it settles", state.remake.size(), state.purge.size());
    }

    void Revert()
    {
        state.homies.clear();
        state.spots.clear();
        state.slots.Clear();
        state.remake.clear();
        state.remakePending = false;
        state.purge.clear();
        state.lookForLeftovers = false;
        state.leaving.clear();
        state.markers.clear();
        Unbind(0);
    }
}
