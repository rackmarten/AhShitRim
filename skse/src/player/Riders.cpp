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
            float           out = -1.0f;  // seconds since it began to step out with CJ (Passengers::ExitPath), -1 while it sits
        };

        struct State
        {
            std::vector<Rider>  riders;
            bool                boarded = false;  // CJ got in and the followers were seated (or there were none)
            Protocol::BodyBone  seats[Protocol::kSeatMax]{};  // the vehicle's seats last frame, for getting out
            RE::NiPoint3        feet;
            Protocol::BodyBone  frame{};  // how the vehicle is turned, last frame
            float               floor = std::numeric_limits<float>::quiet_NaN();  // its ground from its feet (Passengers::FloorOf)
            bool                settled = false;  // a frame in the vehicle with no enter or leave task: a leave task after it is CJ getting out
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

        // Every passenger stands up: beside its door when a_out (CJ got out), else where it is.
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
                    // Where its step out ends (on the ground beside its door), done or not.
                    const auto exit = Passengers::ExitPath(state.seats[rider.seat], Passengers::kExitSeconds, state.floor, state.drop);
                    Hold(actor, state.feet + RE::NiPoint3(exit.x, exit.y, exit.z), actor->GetAngleZ());
                }
                if (rider.out < 0.0f) {
                    actor->NotifyAnimationGraph(state.standEvent);
                }
                SKSE::log::info("riders: {} ({:08X}) gets out{}", actor->GetName(), actor->GetFormID(), a_out ? "" : " where it is");
            }
            state.riders.clear();
        }
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

    void Update(RE::PlayerCharacter* a_player, const Protocol::BodyState* a_vehicle, const RE::NiPoint3& a_feet, bool a_gettingInOut, float a_delta)
    {
        if (!a_vehicle) {
            if (state.boarded) {
                GetOut(true);
                state.boarded = false;
            }
            state.settled = false;
            return;
        }
        if (!state.boarded) {
            Board(a_player, *a_vehicle);
        }
        // San Andreas's leave task began after CJ sat (getting in has an enter task too): he gets out, so do they.
        state.settled = state.settled || !a_gettingInOut;
        const bool leaving = state.settled && a_gettingInOut;
        const auto seats = (std::min)(a_vehicle->seatCount, Protocol::kSeatMax);
        std::erase_if(state.riders, [&](const Rider& a_rider) {
            auto* actor = ActorOf(a_rider);
            if (!actor || actor->IsDead() || !actor->Is3DLoaded() || static_cast<std::uint32_t>(a_rider.seat) >= seats) {
                // Its seat went (the vehicle blew up): out beside it, as when CJ gets out.
                if (actor && !actor->IsDead() && actor->Is3DLoaded()) {
                    const auto exit = Passengers::ExitAt(state.seats[a_rider.seat]);
                    Hold(actor, state.feet + RE::NiPoint3(exit.x, exit.y, exit.z), actor->GetAngleZ());
                    actor->NotifyAnimationGraph(state.standEvent);
                }
                SKSE::log::info("riders: {:08X} doesn't ride any more", a_rider.id);
                return true;
            }
            return false;
        });
        const float floor = Passengers::FloorOf(*a_vehicle);
        for (auto& rider : state.riders) {
            auto*       actor = ActorOf(rider);
            const auto& seat = a_vehicle->seats[rider.seat];
            if (leaving && rider.out < 0.0f) {
                rider.out = 0.0f;
                actor->NotifyAnimationGraph(state.standEvent);
                SKSE::log::info("riders: {} ({:08X}) steps out with CJ", actor->GetName(), actor->GetFormID());
            } else if (!leaving && rider.out >= 0.0f) {
                rider.out = -1.0f;  // he stayed in after all
                actor->NotifyAnimationGraph(state.sitEvent);
            }
            // Sat on its seat, or on its way out beside its door (Passengers::ExitPath).
            if (rider.out >= 0.0f) {
                rider.out += a_delta;
            }
            const auto at = rider.out >= 0.0f ? Passengers::ExitPath(seat, rider.out, floor, state.drop) : Passengers::SeatedFeet(seat, state.drop);
            Hold(actor, a_feet + RE::NiPoint3(at.x, at.y, at.z), Passengers::HeadingOf(seat));
        }
        std::copy_n(a_vehicle->seats, seats, state.seats);
        state.feet = a_feet;
        state.frame = a_vehicle->vehicleFrame;
        state.floor = floor;
    }

    void Clear()
    {
        GetOut(false);
        state.boarded = false;
        state.settled = false;
    }

    bool DebugVehicle(RE::NiPoint3& a_feet, Protocol::BodyBone& a_frame)
    {
        a_feet = state.feet;
        a_frame = state.frame;
        return state.frame.rotation[3] != 0.0f || state.frame.rotation[0] != 0.0f || state.frame.rotation[1] != 0.0f || state.frame.rotation[2] != 0.0f;
    }

    bool Rides(RE::FormID a_id)
    {
        return std::any_of(state.riders.begin(), state.riders.end(), [&](const Rider& a_rider) { return a_rider.id == a_id; });
    }

    bool Sits(RE::FormID a_id)
    {
        return std::any_of(state.riders.begin(), state.riders.end(), [&](const Rider& a_rider) { return a_rider.id == a_id && a_rider.out < 0.0f; });
    }

    std::vector<RE::FormID> Riding()
    {
        std::vector<RE::FormID> out;
        for (const auto& rider : state.riders) {
            out.push_back(rider.id);
        }
        return out;
    }
}
