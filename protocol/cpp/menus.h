// Closing Skyrim's inventory, barter and container menus while San Andreas has the player, the game-free
// half (skse/src/player/Looting.cpp feeds it). A barter is opened from a dialogue: Skyrim takes its
// dialogue menu down for it and puts it back when the barter closes. Closing the barter from its own
// opening event raced that swap, and now and then the dialogue menu never came back: the conversation
// (its camera, the letterbox, the cursor) stayed on with no topics and no way out. So a menu is closed
// only once it has been open a moment with the dialogue menu gone, and a dialogue left without its menu
// gets the menu shown again. Header-only and free of game types, so the protocol tests cover it.

#pragma once

namespace AhShitRim::Menus
{
    // When to close one of the menus: a_open while it is open, a_dialogueMenu while Skyrim's dialogue menu
    // still is (it is being swapped out), a_seconds real time since the last step (menus pause the game).
    class Closer
    {
    public:
        static constexpr float kSettle = 0.15f;  // open this long, the dialogue menu gone, before it is closed
        static constexpr float kRetry = 0.5f;    // a close that didn't take is sent again after this

        // Whether to close it now.
        bool Step(bool a_open, bool a_dialogueMenu, float a_seconds)
        {
            if (!a_open) {
                _openFor = 0.0f;
                _sinceClose = kRetry;
                return false;
            }
            _sinceClose += a_seconds;
            if (a_dialogueMenu) {
                _openFor = 0.0f;  // the swap isn't over
                return false;
            }
            _openFor += a_seconds;
            if (_openFor < kSettle || _sinceClose < kRetry) {
                return false;
            }
            _sinceClose = 0.0f;
            return true;
        }

    private:
        float _openFor = 0.0f;
        float _sinceClose = kRetry;
    };

    // A dialogue whose barter was closed: if the dialogue goes on (Skyrim's topic manager still has a
    // speaker) with neither its menu nor the barter's open for a second, its menu is shown again, at most
    // a few times. Armed when a barter is closed; done once the dialogue menu is back or the dialogue is over.
    class DialogueWatch
    {
    public:
        static constexpr float kOrphaned = 1.0f;
        static constexpr int   kTries = 3;

        void Arm()
        {
            _armed = true;
            _orphanedFor = 0.0f;
            _tries = 0;
        }

        bool Armed() const { return _armed; }

        // Whether to show the dialogue menu again now. a_talking: the topic manager has a speaker.
        bool Step(bool a_talking, bool a_dialogueMenu, bool a_barter, float a_seconds)
        {
            if (!_armed) {
                return false;
            }
            if (!a_talking || (a_dialogueMenu && !a_barter)) {
                _armed = false;  // over, or back as it should be
                return false;
            }
            if (a_dialogueMenu || a_barter) {
                _orphanedFor = 0.0f;
                return false;
            }
            _orphanedFor += a_seconds;
            if (_orphanedFor < kOrphaned) {
                return false;
            }
            _orphanedFor = 0.0f;
            if (++_tries >= kTries) {
                _armed = false;
            }
            return true;
        }

    private:
        bool  _armed = false;
        float _orphanedFor = 0.0f;
        int   _tries = 0;
    };

    // F10 (or any end of the handover) with a dialogue on and neither its menu nor a barter open: the dialogue
    // was left without its menu, and showing it again is the way out.
    constexpr bool Orphaned(bool a_talking, bool a_dialogueMenu, bool a_barter) { return a_talking && !a_dialogueMenu && !a_barter; }
}
