#include "player/WorldPickups.h"

#include "body/ModelView.h"
#include "explosives.h"
#include "health.h"
#include "link/LinkService.h"
#include "pickups.h"
#include "player/Vitals.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <set>
#include <vector>

namespace AhShitRim::WorldPickups
{
    using namespace Pickups;

    namespace
    {
        constexpr std::uint32_t kCopyBase = 0xA5D00000;  // + 2 per copy of a pickup's model: even, never San Andreas's
        constexpr RE::FormID    kGold = 0x0000000F;
        constexpr int           kPassMax = 8;
        constexpr int           kPlaceTries = 30;     // rays tried for a spot before it floats where the table says
        constexpr float         kHeartRetry = 1.0f;   // seconds between looking for a place for the heart
        constexpr float         kDropAbove = 1.0f * kMeter;   // a dropped one is set down from a metre over where it fell
        constexpr float         kDropBelow = 10.0f * kMeter;
        constexpr int           kLogged = 60;

        struct Model
        {
            std::uint32_t                                                id = 0;
            std::uint32_t                                                partCount = 0;
            std::array<Protocol::BodyBone, Protocol::kPickupPartMax> parts{};
        };

        // Where a table spot was set down, once it was near enough for its ground to be loaded.
        struct Placed
        {
            int          tries = 0;
            bool         done = false;
            RE::NiPoint3 middle;
        };

        // A pickup that isn't in the table: the heart, or one San Andreas dropped.
        struct Loose
        {
            Kind          kind = Kind::None;
            std::uint8_t  weapon = 0;
            std::uint32_t amount = 0;
            RE::NiPoint3  middle;
            double        born = 0.0;
            bool          heart = false;
            bool          armed = false;  // CJ has stepped off it since it came (he drops the jetpack at his feet)
            const void*   place = nullptr;
        };

        struct State
        {
            std::map<std::uint16_t, Model>   models;  // by ModelKey
            std::set<std::uint16_t>          asked;   // since San Andreas last had the player
            Board                            board;
            std::map<std::uint16_t, Placed>  placed;  // by spot id
            std::vector<Loose>               loose;
            std::vector<Protocol::PickupEvent> dropped;
            HeartRule                        heart;
            float                            heartRetry = 0.0f;
            double                           clock = 0.0;  // seconds San Andreas has had the player, the respawns' clock
            std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> copies;  // (model, nth shown) to its copy's id
            std::uint32_t                    copyCount = 0;
            int                              logged = 0;
        } state;

        RE::NiPoint3 Ni(const Vec3& a_v) { return { a_v.x, a_v.y, a_v.z }; }
        Vec3         V(const RE::NiPoint3& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        void Log(const std::string& a_line)
        {
            if (state.logged < kLogged) {
                state.logged++;
                SKSE::log::info("pickups: {}", a_line);
            }
        }

        const char* NameOf(Kind a_kind)
        {
            switch (a_kind) {
            case Kind::Health: return "heart";
            case Kind::Armour: return "armour";
            case Kind::Parachute: return "parachute";
            case Kind::Weapon: return "weapon";
            case Kind::Money: return "money";
            case Kind::Jetpack: return "jetpack";
            default: return "nothing";
            }
        }

        const void* PlaceOf(RE::PlayerCharacter* a_player)
        {
            auto* cell = a_player->GetParentCell();
            if (cell && cell->IsInteriorCell()) {
                return cell;
            }
            return a_player->GetWorldspace();
        }

        bool Stops(RE::COL_LAYER a_layer)
        {
            switch (a_layer) {
            case RE::COL_LAYER::kStatic:
            case RE::COL_LAYER::kAnimStatic:
            case RE::COL_LAYER::kTransparent:
            case RE::COL_LAYER::kTrees:
            case RE::COL_LAYER::kProps:
            case RE::COL_LAYER::kWater:
            case RE::COL_LAYER::kTerrain:
            case RE::COL_LAYER::kGround: return true;
            default: return false;
            }
        }

        // Where the segment a_from to a_to first meets the land, water or a static (not actors or clutter).
        bool Surface(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, RE::NiPoint3& a_at)
        {
            auto*        cell = a_player->GetParentCell();
            auto*        bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto*        world = bhk ? bhk->GetWorld1() : nullptr;
            RE::NiPoint3 dir = a_to - a_from;
            const float  length = dir.Unitize();
            if (!world || !(length > 1e-3f)) {
                return false;
            }
            const float              scale = RE::bhkWorld::GetWorldScale();
            RE::hkpWorldRayCastInput input;
            input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kProjectile);
            input.to = RE::hkVector4(a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f);
            RE::BSReadLockGuard lock(bhk->worldLock);
            float               from = 0.0f;
            for (int pass = 0; pass < kPassMax && from < length; ++pass) {
                const RE::NiPoint3 start = a_from + dir * from;
                input.from = RE::hkVector4(start.x * scale, start.y * scale, start.z * scale, 0.0f);
                RE::hkpWorldRayCastOutput output;
                world->CastRay(input, output);
                if (!output.HasHit()) {
                    return false;
                }
                const float distance = from + (length - from) * output.hitFraction;
                if (distance > length) {
                    return false;
                }
                auto* ref = RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable);
                if (ref == a_player || (ref && ref->As<RE::Actor>()) || !Stops(output.rootCollidable->GetCollisionLayer())) {
                    from = distance + 1.0f;
                    continue;
                }
                a_at = a_from + dir * distance;
                return true;
            }
            return false;
        }

        // Whether the camera sees a_point: nothing solid on the line to it.
        bool InSight(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_point)
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            if (!camera || !camera->cameraRoot) {
                return true;
            }
            const RE::NiPoint3 eye = camera->cameraRoot->world.translate;
            RE::NiPoint3       hit;
            return !Surface(a_player, eye, a_point, hit);
        }

        // ---- Models ----

        void Ask(Kind a_kind, std::uint8_t a_weapon)
        {
            const auto key = ModelKey(a_kind, a_weapon);
            if (state.models.count(key) || state.asked.count(key)) {
                return;
            }
            Protocol::PickupCommand command{};
            command.kind = static_cast<std::uint8_t>(Protocol::PickupCommandKind::Model);
            command.pickup = static_cast<std::uint8_t>(a_kind);
            command.weapon = a_kind == Kind::Weapon ? a_weapon : 0;
            if (LinkService::Get().Send(Protocol::MessageType::PickupCommand, &command, sizeof(command))) {
                state.asked.insert(key);
                Log(std::format("asked San Andreas for the {}'s model{}", NameOf(a_kind), a_kind == Kind::Weapon ? std::format(" (weapon {})", a_weapon) : ""));
            }
        }

        // The id to show the a_nth (from 0) pickup drawn with a_model this frame by: the model, then copies of it.
        std::uint32_t CopyOf(std::uint32_t a_model, std::uint32_t a_nth)
        {
            if (a_nth == 0) {
                return a_model;
            }
            auto& id = state.copies[{ a_model, a_nth }];
            if (!id) {
                id = kCopyBase + 2 * ++state.copyCount;
            }
            return ModelView::AddCopy(a_model, id) ? id : 0;
        }

        // Draws a pickup of a_kind spinning at a_middle; asks for its model while there is none. False while it can't be drawn.
        bool Draw(Kind a_kind, std::uint8_t a_weapon, const RE::NiPoint3& a_middle, std::map<std::uint32_t, std::uint32_t>& a_shown)
        {
            const auto it = state.models.find(ModelKey(a_kind, a_weapon));
            if (it == state.models.end()) {
                Ask(a_kind, a_weapon);
                return false;
            }
            const Model& model = it->second;
            if (!model.partCount || !ModelView::Has(model.id)) {
                return false;
            }
            const auto id = CopyOf(model.id, a_shown[model.id]++);
            if (!id) {
                return false;
            }
            const auto          turn = Explosives::AxisAngle({ 0.0f, 0.0f, 1.0f }, SpinAt(state.clock));
            Protocol::BodyState pose{};
            pose.flags = Protocol::kBodyValid;
            pose.model = id;
            pose.boneCount = model.partCount;
            for (std::uint32_t i = 0; i < model.partCount; ++i) {
                pose.bones[i] = Explosives::PartBone(turn, model.parts[i]);
            }
            return ModelView::Show(pose, a_middle);
        }

        // ---- Taking one ----

        Cj CjNow(RE::PlayerCharacter* a_player, const Protocol::PlayerState& a_pose)
        {
            Cj cj;
            cj.health = a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            cj.healthMax = a_player->GetActorValueMax(RE::ActorValue::kHealth);
            cj.armour = Vitals::Armor();
            cj.inVehicle = (a_pose.flags & Protocol::kPlayerInVehicle) != 0;
            Protocol::BodyState gear{};
            cj.jetpack = LinkService::Get().ReadGear(gear) && (gear.flags & Protocol::kBodyJetpack) != 0;
            return cj;
        }

        void Give(RE::PlayerCharacter* a_player, Kind a_kind, std::uint8_t a_weapon, std::uint32_t a_amount, std::string_view a_where)
        {
            switch (a_kind) {
            case Kind::Health: {
                auto*       values = a_player->AsActorValueOwner();
                const float max = a_player->GetActorValueMax(RE::ActorValue::kHealth);
                const float now = values->GetActorValue(RE::ActorValue::kHealth);
                if (max > now) {
                    values->RestoreActorValue(RE::ActorValue::kHealth, max - now);
                }
                break;
            }
            case Kind::Armour:
                Vitals::SetArmor(Health::kSaFullHealth);
                break;
            case Kind::Money:
                if (auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold); gold && a_amount) {
                    a_player->AddObjectToContainer(gold, nullptr, static_cast<std::int32_t>(a_amount), nullptr);
                }
                break;
            case Kind::Weapon: {
                Protocol::WeaponCommand command{};
                command.kind = static_cast<std::uint32_t>(Protocol::WeaponCommandKind::GiveWeapon);
                command.argument = a_weapon;
                command.entries[0].type = a_weapon;
                command.entries[0].ammo = a_amount ? a_amount : StreetAmmo(a_weapon);
                LinkService::Get().Send(Protocol::MessageType::WeaponCommand, &command, sizeof(command));
                break;
            }
            default:
                break;  // the parachute and the jetpack: San Andreas's, below
            }
            // San Andreas's sound for it; the parachute in his hands, the jetpack on him.
            Protocol::PickupCommand command{};
            command.kind = static_cast<std::uint8_t>(Protocol::PickupCommandKind::Collected);
            command.pickup = static_cast<std::uint8_t>(a_kind);
            command.weapon = a_weapon;
            LinkService::Get().Send(Protocol::MessageType::PickupCommand, &command, sizeof(command));
            SKSE::log::info("pickups: CJ took the {}{} at {}", NameOf(a_kind), a_kind == Kind::Weapon ? std::format(" (weapon {})", a_weapon) : std::string(),
                a_where);
        }

        // ---- The table ----

        // Sets a spot down on what is under it (a roof's from high above), once its ground is loaded.
        bool PlaceSpot(RE::PlayerCharacter* a_player, const Spot& a_spot, Placed& a_placed)
        {
            if (a_placed.done) {
                return true;
            }
            RE::NiPoint3 ground;
            if (Surface(a_player, { a_spot.at.x, a_spot.at.y, RayFrom(a_spot) }, { a_spot.at.x, a_spot.at.y, RayTo(a_spot) }, ground)) {
                a_placed.middle = ground + RE::NiPoint3(0.0f, 0.0f, kFloat);
                a_placed.done = true;
                Log(std::format("the {} at {} (spot {}) set down at ({:.0f},{:.0f},{:.0f}), {:.0f} units from the table's height", NameOf(a_spot.kind), a_spot.name,
                    a_spot.id, ground.x, ground.y, ground.z, ground.z - a_spot.at.z));
            } else if (++a_placed.tries >= kPlaceTries) {
                a_placed.middle = RE::NiPoint3(a_spot.at.x, a_spot.at.y, a_spot.at.z + kFloat);
                a_placed.done = true;
                Log(std::format("nothing under the {} at {} (spot {}): it floats at the table's height", NameOf(a_spot.kind), a_spot.name, a_spot.id));
            }
            return a_placed.done;
        }

        void UpdateTable(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_feet, const Cj* a_cj, std::map<std::uint32_t, std::uint32_t>& a_shown)
        {
            auto*      cell = a_player->GetParentCell();
            auto*      worldspace = a_player->GetWorldspace();
            const auto form = cell && !cell->IsInteriorCell() && worldspace ? worldspace->GetFormID() : 0;
            for (const auto& spot : kSpots) {
                if (WorldForm(spot.world) != form) {
                    continue;
                }
                const float across = std::hypot(spot.at.x - a_feet.x, spot.at.y - a_feet.y);
                if (!state.board.Tick(spot, state.clock, across) || across > kDrawRange) {
                    continue;
                }
                auto& placed = state.placed[spot.id];
                if (!PlaceSpot(a_player, spot, placed)) {
                    continue;
                }
                Draw(spot.kind, spot.weapon, placed.middle, a_shown);
                if (a_cj && Touches(V(a_feet), V(placed.middle)) && Wants(spot.kind, *a_cj)) {
                    state.board.Take(spot, state.clock);
                    Give(a_player, spot.kind, spot.weapon, spot.amount, std::format("{} (spot {})", spot.name, spot.id));
                }
            }
        }

        // ---- The heart and San Andreas's own ----

        void SpawnHeart(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_feet, float a_heading)
        {
            for (const auto& place : HeartPlaces(V(a_feet), a_heading)) {
                RE::NiPoint3 ground;
                if (!Surface(a_player, RE::NiPoint3(place.x, place.y, place.z + 3.0f * kMeter), RE::NiPoint3(place.x, place.y, place.z - 10.0f * kMeter), ground)) {
                    continue;
                }
                const RE::NiPoint3 middle = ground + RE::NiPoint3(0.0f, 0.0f, kFloat);
                if (!InSight(a_player, middle)) {
                    continue;
                }
                Loose heart;
                heart.kind = Kind::Health;
                heart.middle = middle;
                heart.born = state.clock;
                heart.heart = true;
                heart.place = PlaceOf(a_player);
                state.loose.push_back(heart);
                state.heart.Spawned(state.clock);
                Log(std::format("CJ is hurt: a heart at ({:.0f},{:.0f},{:.0f}), {:.1f} m from him", middle.x, middle.y, middle.z, middle.GetDistance(a_feet) / kMeter));
                return;
            }
        }

        void UpdateLoose(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_feet, const Cj* a_cj, std::map<std::uint32_t, std::uint32_t>& a_shown)
        {
            const void* place = PlaceOf(a_player);
            for (auto it = state.loose.begin(); it != state.loose.end();) {
                const bool gone = it->place != place ||
                                  (it->heart && (state.clock - it->born > kHeartLife || it->middle.GetDistance(a_feet) > kHeartGone));
                if (gone) {
                    it = state.loose.erase(it);
                    continue;
                }
                const bool touches = Touches(V(a_feet), V(it->middle));
                if (!touches) {
                    it->armed = true;
                }
                if (it->middle.GetDistance(a_feet) <= kDrawRange) {
                    Draw(it->kind, it->weapon, it->middle, a_shown);
                }
                if (a_cj && it->armed && touches && Wants(it->kind, *a_cj)) {
                    const auto taken = *it;
                    it = state.loose.erase(it);
                    Give(a_player, taken.kind, taken.weapon, taken.amount, taken.heart ? "the heart" : "where San Andreas dropped it");
                    continue;
                }
                ++it;
            }
        }

        void Drop(RE::PlayerCharacter* a_player, const Protocol::PickupEvent& a_event, const RE::NiPoint3& a_anchor)
        {
            const RE::NiPoint3 at = a_anchor + RE::NiPoint3(a_event.at[0], a_event.at[1], a_event.at[2]);
            if (!std::isfinite(at.x + at.y + at.z)) {
                return;
            }
            RE::NiPoint3 ground;
            Loose        loose;
            loose.kind = static_cast<Kind>(a_event.pickup);
            loose.weapon = a_event.weapon;
            loose.amount = a_event.amount;
            loose.middle = Surface(a_player, at + RE::NiPoint3(0.0f, 0.0f, kDropAbove), at - RE::NiPoint3(0.0f, 0.0f, kDropBelow), ground) ?
                               ground + RE::NiPoint3(0.0f, 0.0f, kFloat) :
                               at;
            loose.born = state.clock;
            loose.place = PlaceOf(a_player);
            state.loose.push_back(loose);
            Log(std::format("San Andreas dropped the {} at ({:.0f},{:.0f},{:.0f})", NameOf(loose.kind), loose.middle.x, loose.middle.y, loose.middle.z));
        }
    }

    void Queue(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        if (a_size < sizeof(Protocol::PickupEvent)) {
            return;
        }
        Protocol::PickupEvent event;
        std::memcpy(&event, a_data, sizeof(event));
        if (event.kind == static_cast<std::uint8_t>(Protocol::PickupEventKind::Model)) {
            auto& model = state.models[ModelKey(static_cast<Kind>(event.pickup), event.weapon)];
            model.id = event.model;
            model.partCount = (std::min)(static_cast<std::uint32_t>(event.partCount), Protocol::kPickupPartMax);
            std::memcpy(model.parts.data(), event.parts, sizeof(event.parts));
            Log(std::format("the {}'s model is {:08X} ({} parts)", NameOf(static_cast<Kind>(event.pickup)), model.id, model.partCount));
        } else if (event.kind == static_cast<std::uint8_t>(Protocol::PickupEventKind::Dropped) && state.dropped.size() < 16) {
            state.dropped.push_back(event);
        }
    }

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, float a_delta, const Protocol::PlayerState* a_pose,
        bool a_held)
    {
        state.clock += std::clamp(a_delta, 0.0f, 0.25f);

        for (const auto& event : state.dropped) {
            if (event.handover == a_handover) {
                Drop(a_player, event, a_anchor);
            }
        }
        state.dropped.clear();

        const RE::NiPoint3 feet = a_anchor + (a_pose ? RE::NiPoint3(a_pose->offset[0], a_pose->offset[1], a_pose->offset[2]) : RE::NiPoint3());
        Cj                 cj;
        const bool         free = a_pose && !a_held;
        if (free) {
            cj = CjNow(a_player, *a_pose);
            state.heartRetry -= a_delta;
            const bool heartOut = std::any_of(state.loose.begin(), state.loose.end(), [](const Loose& a_loose) { return a_loose.heart; });
            if (state.heartRetry <= 0.0f && !cj.inVehicle && state.heart.Due(cj.health, cj.healthMax, state.clock, heartOut)) {
                state.heartRetry = kHeartRetry;
                SpawnHeart(a_player, feet, a_pose->viewHeading);
            }
        }
        std::map<std::uint32_t, std::uint32_t> shown;  // by model: how many are drawn with it so far
        UpdateTable(a_player, feet, free ? &cj : nullptr, shown);
        UpdateLoose(a_player, feet, free ? &cj : nullptr, shown);
    }

    void Clear()
    {
        state.loose.clear();
        state.dropped.clear();
        // Skyrim keeps a model it built for good; one that never got built (San Andreas went away halfway) is asked for
        // again next time, of a San Andreas that may have started again since.
        for (auto it = state.models.begin(); it != state.models.end();) {
            it = ModelView::Has(it->second.id) ? std::next(it) : state.models.erase(it);
        }
        state.asked.clear();
    }

    std::vector<std::uint16_t> Taken() { return state.board.Once(); }

    void Restore(const std::vector<std::uint16_t>& a_taken)
    {
        state.board.Restore(a_taken);
        state.heart.Reset();
        Clear();
        SKSE::log::info("pickups: the loaded save took {} one-off pickups", a_taken.size());
    }

    void Revert()
    {
        state.board.Clear();
        state.heart.Reset();
        Clear();
    }
}
