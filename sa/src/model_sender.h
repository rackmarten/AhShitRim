#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The ring back to Skyrim carries models as ModelChunks, and Skyrim puts a model together from
// chunks that come in order, so two models can't go at once. Whatever has a model for Skyrim (CJ's
// body, each vehicle) puts it here under a key of its own; the models go one after another, as the
// ring has room, and all of them again every time the link comes up (a Skyrim that just started has
// none of them). Game thread only.
namespace AhShitRim::ModelSender
{
    inline constexpr int kBodyKey = 0;     // CJ
    inline constexpr int kVehicleKey = 1;  // + the vehicle's slot
    inline constexpr int kWeaponKey = 5;   // + the hand (after the vehicles' four keys)
    inline constexpr int kProjectileKey = 7;  // v15: + the projectile type less 16 (grenade to heat seeker: 7 to 11, the satchel 30)
    inline constexpr int kGearKey = 31;       // v19: the jetpack, + 1 the parachute's canopy

    // a_chunks: the model's ModelChunk payloads (SkinModel::Chunks). Replaces what a_key had; one
    // half sent is dropped (its successor starts at offset 0, which drops it in Skyrim too).
    // a_label starts the log line once it is sent ("body: CJ's model").
    void Set(int a_key, std::uint32_t a_id, std::vector<std::vector<std::uint8_t>> a_chunks, std::string a_label);
    void Remove(int a_key);

    // Once a frame: sends what the ring has room for.
    void Pump();

    // A new model's id, never one sent before (SkinModel::ModelIds, one counter for every sender).
    std::uint32_t NewId();
}
