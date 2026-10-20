#pragma once

#include "../ahshitrim_protocol.h"

class CPlayerPed;

// San Andreas's own sound while Skyrim has handed the player over (protocol v9): CJ, his vehicles,
// his weapons and his speech, played by San Andreas's audio engine where its camera is (which
// Skyrim's follows), and its radio in vehicles.
// - San Andreas makes every sound buffer with DirectSound's global focus, so it plays while its
//   window is hidden and Skyrim has the focus; it only needed unmuting.
// - Volumes come from Skyrim's sliders (AudioState): effects for everything but the radio, music
//   for the radio; 0 while Skyrim has the player. A pausing Skyrim menu pauses every sound.
// - The world's own ambience stays off: city noise and weather, area ambience tracks, the police
//   scanner, distant gunfire.
// - The mouse wheel in Skyrim dials the radio (Radio::Dial). After San Andreas's stations come Radio
//   Sovngarde and Sweetroll FM: San Andreas's radio is off there, and Skyrim plays them.
// Game thread only.
namespace AhShitRim::Audio
{
    void Install();  // hooks; at ASI load

    // From the pad update, with each input read: the wheel's radio steps.
    void OnInput(const Protocol::InputState& a_input, bool a_inVehicle);

    // Once a frame after the simulation: volumes, pausing.
    void Update(bool a_owned, bool a_paused);

    // The radio in CJ's pose: station and retuning.
    void Fill(Protocol::PlayerState& a_pose, bool a_inVehicle);
}
