// When the game moves the player while CJ has it, the game-free half: whether a frame's position is
// CJ walking on, or a load door, fast travel or a script putting the player somewhere else, which
// re-anchors CJ there. Header-only and free of game types, so the protocol tests cover it; the SKSE
// plugin asks it every frame (skse/src/player/PlayerSync.cpp).
//
// A re-anchor is a handover that keeps going: Skyrim counts InputState.handover up and sends the
// player's new feet and heading as the anchor, and San Andreas does what it does for any new
// handover (drops its ground and every vehicle, puts CJ at the anchor under a fresh floating origin,
// holds him there until the ground under him is in). CJ himself, his crouch and whatever else San
// Andreas keeps on him stay as they were.

#pragma once

namespace AhShitRim::Anchor
{
    // A jump this far from where Skyrim put the player for CJ last frame is the game moving it. CJ's
    // own movement doesn't count (the player is put where he is every frame, so a car at full speed
    // jumps nothing): only Skyrim's own nudges do, its character controller easing the player out of
    // a wall by well under a metre. A load door within one cell moves it further than this.
    inline constexpr float kTeleportUnits = 512.0f;

    enum class Move
    {
        None,      // CJ walked or drove on in the same cell, or into the next exterior one
        Loading,   // no cell yet (a loading screen): wait
        LoadDoor,  // into or out of an interior, or into another worldspace (a city's gate)
        Teleport,  // fast travel, a script, a load door within a cell: far from where CJ was put last frame
    };

    struct Frame
    {
        bool  hasCell = false;            // the player has a parent cell
        bool  cellChanged = false;        // ... other than the one it had
        bool  interior = false;           // the cell now is an interior
        bool  wasInterior = false;        // the cell before was
        bool  worldspaceChanged = false;  // an exterior in another worldspace than before (Tamriel to Whiterun)
        float jump = 0.0f;                // units from where Skyrim put the player for CJ last frame
    };

    inline Move Classify(const Frame& a_frame)
    {
        if (!a_frame.hasCell) {
            return Move::Loading;
        }
        if (a_frame.cellChanged && (a_frame.interior || a_frame.wasInterior || a_frame.worldspaceChanged)) {
            return Move::LoadDoor;
        }
        if (a_frame.jump > kTeleportUnits) {
            return Move::Teleport;
        }
        return Move::None;
    }

    inline bool Reanchors(Move a_move) { return a_move == Move::LoadDoor || a_move == Move::Teleport; }

    inline const char* NameOf(Move a_move)
    {
        switch (a_move) {
        case Move::LoadDoor: return "went through a load door";
        case Move::Teleport: return "moved by the game";
        case Move::Loading: return "loading";
        default: return "walked on";
        }
    }
}
