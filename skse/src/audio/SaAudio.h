#pragma once

#include "../ahshitrim_protocol.h"

// San Andreas's sound and radio from the Skyrim side (v10):
//  - every frame, Skyrim's master, effects and music sliders go to San Andreas as its volumes
//    (AudioState), and 0 while Skyrim has the player;
//  - while a San Andreas station plays in CJ's vehicle, its name shows in San Andreas's style and
//    Skyrim's own music is paused (its current music type, as a BSMusicEvent);
//  - on the two stations after San Andreas's own, San Andreas stays quiet and Skyrim plays them:
//    Radio Sovngarde, its own music tracks read by path from its archives, and Sweetroll FM, the
//    CC0 tracks installed under Data\Music\AhShitRim\SweetrollFM (protocol/cpp/radio.h picks them).
namespace AhShitRim::Audio
{
    void OnDataLoaded();  // finds Skyrim's sound categories; call on kDataLoaded

    // Every frame from PlayerCharacter::Update. a_pose is CJ's newest pose (nullptr while Skyrim has
    // the player or there is none); a_paused while a Skyrim menu pauses the game; a_cue while San
    // Andreas plays a sound cue (v24: the mission passed tune, CJ's intro line), which is heard
    // whoever has the player and pauses Skyrim's music as a station does.
    void Update(float a_delta, bool a_owned, bool a_paused, const Protocol::PlayerState* a_pose, bool a_cue = false);

    // A pausing menu opened: the station Skyrim plays pauses at once (PlayerCharacter::Update stops while the
    // game is paused; the next Update resumes it).
    void SetPaused(bool a_paused);
}
