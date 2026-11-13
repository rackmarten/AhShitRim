#pragma once

#include <optional>
#include <string>

// A check of Skyrim's attacks on CJ's vehicle without anyone at the keys (v31), run as the test kit's scenario
// asr.hits (skse/src/debug/TestKit.cpp, scenarios/hits.json and bust.json, scripts/hit-test.ps1). With CJ in a vehicle
// (asr.spawn 495 in=true puts him in a Sandking) it
//  - hits: places a giant and two of Whiterun's guards 12 m ahead and sets them on the player, so their blows land on
//    the car (it smokes, burns and blows up, and CJ inside keeps his health until it does),
//  - bust: types the wanted cheat for two stars, so the guards come and one runs to the stopped car's door to bust him,
// and samples the car's and the player's health every 2 s for the seconds asked. At the end the attackers go and the
// stars are cleared, so the session goes on without them. Nothing is saved. Main thread only.
namespace AhShitRim::HitTest
{
    struct Sample
    {
        float seconds = 0.0f;
        float carHealth = -1.0f;  // -1: CJ drives nothing
        float playerHealth = 0.0f;
        bool  inCar = false;
        bool  wasted = false;
        bool  busted = false;
    };

    struct Result
    {
        bool  done = false;
        float seconds = 0.0f;
        int   attackers = 0;       // placed (hits)
        float carStart = -1.0f;
        float carMin = -1.0f;
        float carEnd = -1.0f;
        float playerMin = 0.0f;
        bool  inCarAtEnd = false;
        bool  wasted = false;      // at any sample
        bool  busted = false;      // at any sample
        std::string stopped;       // why it ended early: a game loaded (the player died, and Skyrim loaded the last save)
    };

    // Starts it; a reason when it can't (CJ isn't driving). Ends a run still going first.
    std::string Begin(bool a_bust, float a_seconds);

    // Ends a run (its attackers go, the stars are cleared); nothing when none runs.
    void Stop();

    bool Running();

    // A game loaded: a run still going ends, failed (what follows is another game's).
    void OnPostLoadGame();

    // The next sample since the last call, in order.
    std::optional<Sample> NextSample();

    const Result& Last();

    // Every player update, after Skyrim's own.
    void Update(RE::PlayerCharacter* a_player, float a_delta);
}
