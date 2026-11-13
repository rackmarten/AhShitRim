#pragma once

#include <optional>
#include <string>

// A measurement (v19), run as the test kit's scenario asr.stream (skse/src/debug/TestKit.cpp, scenarios/stream.json):
// how Skyrim's cell streaming copes with the player moving as fast as a Hydra flies. With Skyrim holding the player
// outdoors, it carries the player in a straight line (heading degrees from north) height metres over where it starts,
// at speed metres a second for seconds, and logs every second and at the end: the frame times and stalls, and how
// often the land under the player and 1 s and 2 s ahead of it was loaded (its cell attached, with collision) and had a
// height. It ends early when San Andreas takes the player, a game loads, or nobody has asked for its seconds for 10 s (the
// kit's request went away). Nothing is saved. Main thread only.
namespace AhShitRim::StreamTest
{
    struct Second
    {
        float at = 0.0f;      // seconds since the flight began, at its end
        float window = 0.0f;  // seconds it covers
        int   frames = 0;
        float worst = 0.0f;   // the longest frame, seconds
        int   over50 = 0;
        int   over100 = 0;
        int   samples = 0;
        int   underAttached = 0, underLand = 0;
        int   ahead1Attached = 0, ahead1Land = 0;
        int   ahead2Attached = 0, ahead2Land = 0;

        void Add(const Second& a_other);
    };

    // Starts a flight; a reason when it can't (San Andreas has the player, the player indoors, a bad number).
    std::string Begin(float a_speed, float a_headingDegrees, float a_heightMetres, float a_seconds);

    void Stop();
    void OnPostLoadGame();

    // Whether it moves the player now (CJ isn't handed over meanwhile).
    bool Running();

    // The next second since the last call, in order; the whole flight once it's over.
    std::optional<Second> NextSecond();
    std::optional<Second> Total();

    // Every player update, after Skyrim's own.
    void Update(RE::PlayerCharacter* a_player, float a_delta);
}
