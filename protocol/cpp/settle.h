// When the game has settled after a load, the game-free half. A save that had San Andreas on the player takes it again
// on its own, which makes the player essential, places it, hides its 3D and starts streaming the ground: done while
// Skyrim was still finishing the load (its loading menu just gone, its garbage collector still at work), a freed actor
// turned up in a vanilla actor's AI and the game crashed in the same second (SkyrimSE.exe+02E3DCC). So the handover
// waits for kSeconds of normal frames in a row: the link up, no loading menu, not paused, the player's 3D in and its
// cell attached. Header-only and free of game types, so the protocol tests cover it; skse/src/player/PlayerSync.cpp
// feeds it.

#pragma once

namespace AhShitRim::Settle
{
    inline constexpr float kSeconds = 2.0f;

    struct Frame
    {
        bool  linkUp = false;
        bool  loadingMenu = false;  // Skyrim's loading screen is up
        bool  paused = false;       // a menu pauses the game
        bool  player3D = false;     // the player's 3D is loaded
        bool  cellAttached = false; // the player's cell is in
        bool  busy = false;         // something of ours moves the player first (the stream test)
        float delta = 0.0f;         // seconds since the last frame
    };

    constexpr bool Normal(const Frame& a_frame)
    {
        return a_frame.linkUp && !a_frame.loadingMenu && !a_frame.paused && a_frame.player3D && a_frame.cellAttached && !a_frame.busy;
    }

    // Armed by a load (Start); Tick says, once, when the game has been normal for kSeconds in a row.
    class Gate
    {
    public:
        void Start()
        {
            _armed = true;
            _normal = 0.0f;
        }

        bool Tick(const Frame& a_frame)
        {
            if (!_armed) {
                return false;
            }
            if (!Normal(a_frame) || !(a_frame.delta >= 0.0f)) {
                _normal = 0.0f;  // anything off starts the wait over
                return false;
            }
            _normal += a_frame.delta;
            if (_normal < kSeconds) {
                return false;
            }
            _armed = false;
            return true;
        }

        bool  Armed() const { return _armed; }
        float NormalSeconds() const { return _normal; }
        void  Reset() { _armed = false; }

    private:
        bool  _armed = false;
        float _normal = 0.0f;  // seconds of normal frames in a row
    };
}
