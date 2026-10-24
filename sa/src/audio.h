#pragma once

#include "../ahshitrim_protocol.h"
#include "hud_sounds.h"

class CPlayerPed;

// San Andreas's own sound while Skyrim has handed the player over (protocol v10): CJ, his vehicles,
// his weapons and his speech, played by San Andreas's audio engine where its camera is (which
// Skyrim's follows), and its radio in vehicles.
// - San Andreas makes every sound buffer with DirectSound's global focus, so it plays while its
//   window is hidden and Skyrim has the focus; it only needed unmuting.
// - Volumes come from Skyrim's sliders (AudioState): effects for everything but the radio, music
//   for the radio; 0 while Skyrim has the player. A pausing Skyrim menu pauses every sound.
// - The world's own ambience stays off: city noise and weather, area ambience tracks, the police
//   scanner, distant gunfire.
// - Skyrim sends San Andreas's HUD sounds (v30, FrontendSound messages, protocol/cpp/hud_sounds.h): the help box's
//   beep, pickups, a purchase; played as San Andreas plays them, on the effects volume.
// - The mouse wheel in Skyrim dials the radio (Radio::Dial). After San Andreas's stations come Radio
//   Sovngarde and Sweetroll FM: San Andreas's radio is off there, and Skyrim plays them.
// - Sound cues (v24, InputState.cue, protocol/cpp/missions.h), whoever has the player: the mission passed
//   tune and CJ's intro line, by San Andreas's own routines and ids.
// Game thread only.
namespace AhShitRim::Audio
{
    void Install();  // hooks; at ASI load

    // From the pad update, with each input read: the wheel's radio steps, a new sound cue.
    void OnInput(const Protocol::InputState& a_input, bool a_inVehicle);

    // One of San Andreas's HUD sounds, as it plays them (CAudioEngine::ReportFrontendAudioEvent).
    void Play(HudSounds::Sound a_sound);

    // A FrontendSound message from Skyrim, from the ring's drain: played at once.
    void PlayFrontend(const std::uint8_t* a_data, std::uint32_t a_size);

    // Once a frame after the simulation: volumes, pausing, the cue playing.
    void Update(bool a_owned, bool a_paused);

    // The radio in CJ's pose: station and retuning.
    void Fill(Protocol::PlayerState& a_pose, bool a_inVehicle);

    // A station plays (San Andreas's, or one of Skyrim's; not off): as of the last Fill.
    bool RadioPlays();
}
