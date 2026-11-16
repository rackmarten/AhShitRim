#include "player/Riders.h"

#include "Ini.h"
#include "passengers.h"

namespace AhShitRim::Riders
{
    namespace
    {
        struct Rider
        {
            RE::ActorHandle handle;
            RE::FormID      id = 0;
            int             seat = 0;
            RE::NiMatrix3   turn;           // his seat's frame in the world as last held: columns right, forward, up
            bool            turned = false;
        };

        struct State
        {
            std::vector<Rider>  riders;
            bool                boarded = false;  // CJ got in and the followers were seated (or there were none)
            Protocol::BodyBone  seats[Protocol::kSeatMax]{};  // the vehicle's seats last frame, for getting out
            RE::NiPoint3        feet;
            std::string         sitEvent = "IdleSitCrossLeggedEnter";
            std::string         standEvent = "IdleForceDefaultState";
            float               drop = Passengers::kSeatDrop;
        } state;

        RE::Actor* ActorOf(const Rider& a_rider)
        {
            auto ptr = a_rider.handle.get();
            return ptr.get();
        }

        void Hold(RE::Actor* a_actor, const RE::NiPoint3& a_at, float a_heading)
        {
            a_actor->SetPosition(a_at, true);
            a_actor->SetHeading(a_heading);
            if (auto* controller = a_actor->GetCharController()) {
                controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f));
                controller->fallStartHeight = a_at.z;
                controller->fallTime = 0.0f;
            }
        }

        // The followers near the player sit down in the vehicle's free seats.
        void Board(RE::PlayerCharacter* a_player, const Protocol::BodyState& a_vehicle)
        {
            state.boarded = true;
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists || a_vehicle.seatCount == 0) {
                return;
            }
            std::vector<RE::Actor*>             actors;
            std::vector<Passengers::Follower>   followers;
            const auto                          centre = a_player->GetPosition();
            for (auto& handle : lists->highActorHandles) {
                auto  ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == a_player || !actor->IsPlayerTeammate() || actor->IsDead() || !actor->Is3DLoaded() || actor->IsOnMount() ||
                    actor->IsAMount() || actor->IsInKillMove()) {
                    continue;
                }
                actors.push_back(actor);
                followers.push_back({ actor->GetFormID(), actor->GetPosition().GetDistance(centre) });
            }
            const auto seats = Passengers::Assign(followers, a_vehicle.seatCount);
            for (std::size_t i = 0; i < actors.size(); i++) {
                if (seats[i] < 0) {
                    continue;
                }
                auto* actor = actors[i];
                state.riders.push_back({ actor->CreateRefHandle(), actor->GetFormID(), seats[i] });
                actor->NotifyAnimationGraph(state.sitEvent);
                SKSE::log::info("riders: {} ({:08X}) gets in, seat {} of {}", actor->GetName(), actor->GetFormID(), seats[i], a_vehicle.seatCount);
            }
        }

        // A seat's frame in the world: its rotation's axes as the columns of a matrix (X right, Y forward, Z up, as a
        // Skyrim model's are).
        RE::NiMatrix3 FrameOf(const Protocol::BodyBone& a_seat)
        {
            const auto    q = Passengers::QuatOf(a_seat);
            const Passengers::Vec3 axes[3] = { Passengers::Rotate(q, { 1.0f, 0.0f, 0.0f }), Passengers::Rotate(q, { 0.0f, 1.0f, 0.0f }),
                Passengers::Rotate(q, { 0.0f, 0.0f, 1.0f }) };
            RE::NiMatrix3 out;
            for (int c = 0; c < 3; c++) {
                out.entry[0][c] = axes[c].x;
                out.entry[1][c] = axes[c].y;
                out.entry[2][c] = axes[c].z;
            }
            return out;
        }

        // A passenger turned as his seat is, every frame after Skyrim updated him: Skyrim stands an actor upright at his
        // heading, so on a leaning bike he sat bolt upright beside the seat (his feet are under it along the seat's own
        // down, Passengers::SeatedFeet, but his hips straight up from them). His 3D root turns to the seat's frame about
        // his feet, which puts his hips back on the seat and rolls all of him with the bike, as San Andreas carries its
        // riders in a bike's lean matrix.
        void Turn(RE::Actor* a_actor, const Rider& a_rider)
        {
            auto* root = a_actor->Get3D();
            if (!root || !a_rider.turned) {
                return;
            }
            root->local.rotate = root->parent ? root->parent->world.rotate.Transpose() * a_rider.turn : a_rider.turn;
            RE::NiUpdateData update{};
            root->Update(update);
        }

        struct UpdateHook
        {
            static void thunk(RE::Character* a_actor, float a_delta)
            {
                original(a_actor, a_delta);
                if (state.riders.empty() || !a_actor) {
                    return;
                }
                const auto id = a_actor->GetFormID();
                for (const auto& rider : state.riders) {
                    if (rider.id == id) {
                        Turn(a_actor, rider);
                        break;
                    }
                }
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };

        // Every passenger stands up: beside its door when a_out (CJ got out), else where it is, upright again.
        void GetOut(bool a_out)
        {
            for (const auto& rider : state.riders) {
                auto* actor = ActorOf(rider);
                if (!actor || actor->IsDisabled() || !actor->Is3DLoaded()) {
                    // Gone, or unloading (a load that began with them seated): nothing of theirs is touched, they're just let go.
                    SKSE::log::info("riders: {:08X} is let go without standing up (not loaded)", rider.id);
                    continue;
                }
                if (a_out && !actor->IsDead()) {
                    const auto exit = Passengers::ExitAt(state.seats[rider.seat]);
                    Hold(actor, state.feet + RE::NiPoint3(exit.x, exit.y, exit.z), actor->GetAngleZ());
                }
                actor->Update3DPosition(true);  // his 3D upright at his heading, not on the seat's lean
                actor->NotifyAnimationGraph(state.standEvent);
                SKSE::log::info("riders: {} ({:08X}) gets out{}", actor->GetName(), actor->GetFormID(), a_out ? "" : " where it is");
            }
            state.riders.clear();
        }
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_Character[0] };
        UpdateHook::original = vtable.write_vfunc(0xAD, UpdateHook::thunk);
    }

    void OnDataLoaded()
    {
        if (auto sit = Ini::Value("Passengers", "SitEvent"); !sit.empty()) {
            state.sitEvent = sit;
        }
        if (auto stand = Ini::Value("Passengers", "StandEvent"); !stand.empty()) {
            state.standEvent = stand;
        }
        if (const auto drop = Ini::Value("Passengers", "SeatDrop"); !drop.empty()) {
            try {
                state.drop = std::stof(drop);
            } catch (...) {
                SKSE::log::warn("riders: [Passengers] SeatDrop={} isn't a number, kept {:.0f}", drop, state.drop);
            }
        }
        SKSE::log::info("riders: passengers sit with {} and stand with {}, feet {:.0f} units under the seat", state.sitEvent, state.standEvent, state.drop);
    }

    void Update(RE::PlayerCharacter* a_player, const Protocol::BodyState* a_vehicle, const RE::NiPoint3& a_feet)
    {
        if (!a_vehicle) {
            if (state.boarded) {
                GetOut(true);
                state.boarded = false;
            }
            return;
        }
        if (!state.boarded) {
            Board(a_player, *a_vehicle);
        }
        const auto seats = (std::min)(a_vehicle->seatCount, Protocol::kSeatMax);
        std::erase_if(state.riders, [&](Rider& a_rider) {
            auto* actor = ActorOf(a_rider);
            if (!actor || actor->IsDead() || !actor->Is3DLoaded() || static_cast<std::uint32_t>(a_rider.seat) >= seats) {
                // Its seat went (the vehicle blew up): out beside it, as when CJ gets out. Dead or alive, its 3D stands
                // upright again, off the seat's lean.
                if (actor && !actor->IsDead() && actor->Is3DLoaded()) {
                    const auto exit = Passengers::ExitAt(state.seats[a_rider.seat]);
                    Hold(actor, state.feet + RE::NiPoint3(exit.x, exit.y, exit.z), actor->GetAngleZ());
                    actor->NotifyAnimationGraph(state.standEvent);
                }
                if (actor && actor->Is3DLoaded()) {
                    actor->Update3DPosition(true);
                }
                SKSE::log::info("riders: {:08X} doesn't ride any more", a_rider.id);
                return true;
            }
            const auto& seat = a_vehicle->seats[a_rider.seat];
            const auto  feet = Passengers::SeatedFeet(seat, state.drop);
            Hold(actor, a_feet + RE::NiPoint3(feet.x, feet.y, feet.z), Passengers::HeadingOf(seat));
            a_rider.turn = FrameOf(seat);
            a_rider.turned = true;
            Turn(actor, a_rider);  // now too: the kit and the render see him turned even on a frame Skyrim doesn't update him
            return false;
        });
        std::copy_n(a_vehicle->seats, seats, state.seats);
        state.feet = a_feet;
    }

    void Clear()
    {
        GetOut(false);
        state.boarded = false;
    }

    bool Rides(RE::FormID a_id)
    {
        return std::any_of(state.riders.begin(), state.riders.end(), [&](const Rider& a_rider) { return a_rider.id == a_id; });
    }

    std::vector<RE::FormID> Riding()
    {
        std::vector<RE::FormID> out;
        for (const auto& rider : state.riders) {
            out.push_back(rider.id);
        }
        return out;
    }

    bool DebugSeatOf(RE::FormID a_id, Protocol::BodyBone& a_seat, RE::NiPoint3& a_vehicleFeet)
    {
        const auto found = std::find_if(state.riders.begin(), state.riders.end(), [&](const Rider& a_rider) { return a_rider.id == a_id; });
        if (found == state.riders.end() || found->seat < 0 || found->seat >= static_cast<int>(Protocol::kSeatMax)) {
            return false;
        }
        a_seat = state.seats[found->seat];
        a_vehicleFeet = state.feet;
        return true;
    }
}
