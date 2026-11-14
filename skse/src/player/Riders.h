#pragma once

#include <cstdint>

namespace AhShitRim::Protocol
{
    struct BodyState;
    struct BodyBone;
}

// The player's followers riding with CJ (protocol v28; the rules in protocol/cpp/passengers.h). When CJ gets into
// a vehicle, the followers near him (up to its passenger seats, nearest first) sit down in its seats, which San
// Andreas sends in the vehicle's slot: each plays a sitting idle ([Passengers] SitEvent in AhShitRim.ini) and is
// put on its seat every frame, turned as the vehicle, smoothed with it. When he gets out they stand up
// ([Passengers] StandEvent) and step out beside their doors with him, down to the ground (Passengers::ExitPath),
// instead of sitting through his get-out and then appearing outside. No drive-bys: they only ride.
// Main thread only.
namespace AhShitRim::Riders
{
    void OnDataLoaded();  // reads [Passengers] from the ini

    // Every frame San Andreas has the player, after the vehicles are drawn: a_vehicle is the one CJ is in as
    // drawn (nullptr while he is on foot), a_feet where it is in Skyrim's world, a_gettingInOut San Andreas's
    // enter or leave task running (PlayerState kPlayerGettingInOut) or CJ already out of it, a_delta the frame's seconds.
    void Update(RE::PlayerCharacter* a_player, const Protocol::BodyState* a_vehicle, const RE::NiPoint3& a_feet, bool a_gettingInOut, float a_delta);

    // Back to Skyrim's own player, or a re-anchor (fast travel takes the followers along by itself): the
    // passengers stand up where they are.
    void Clear();

    // Whether a_id rides with CJ now (CarHits leaves them out).
    bool Rides(RE::FormID a_id);

    // Rides, and still sitting: false once it has begun to step out with CJ (the crew's homie stands up then, v39).
    bool Sits(RE::FormID a_id);

    // Who rides with CJ now (v35: Hitboxes covers them as it covers CJ).
    std::vector<RE::FormID> Riding();

    // The hit test's riders scenario (debug): where the vehicle CJ was last in was drawn (its feet) and how it was
    // turned; false before he was in one.
    bool DebugVehicle(RE::NiPoint3& a_feet, Protocol::BodyBone& a_frame);
}
