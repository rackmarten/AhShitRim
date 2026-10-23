#pragma once

#include <cstdint>

namespace AhShitRim::Protocol
{
    struct BodyState;
}

// The player's followers riding with CJ (protocol v23; the rules in protocol/cpp/passengers.h). When CJ gets into
// a vehicle, the followers near him (up to its passenger seats, nearest first) sit down in its seats, which San
// Andreas sends in the vehicle's slot: each plays a sitting idle ([Passengers] SitEvent in AhShitRim.ini) and is
// put on its seat every frame, turned as the vehicle, smoothed with it. When he gets out they stand up beside
// their doors ([Passengers] StandEvent). No drive-bys: they only ride.
// Main thread only.
namespace AhShitRim::Riders
{
    void OnDataLoaded();  // reads [Passengers] from the ini

    // Every frame San Andreas has the player, after the vehicles are drawn: a_vehicle is the one CJ is in as
    // drawn (nullptr while he is on foot), a_feet where it is in Skyrim's world.
    void Update(RE::PlayerCharacter* a_player, const Protocol::BodyState* a_vehicle, const RE::NiPoint3& a_feet);

    // Back to Skyrim's own player, or a re-anchor (fast travel takes the followers along by itself): the
    // passengers stand up where they are.
    void Clear();

    // Whether a_id rides with CJ now (CarHits leaves them out).
    bool Rides(RE::FormID a_id);
}
