// C++ end of the AhShitRim link, used by both halves (the SKSE plugin builds it 64-bit, the
// San Andreas ASI plugin 32-bit) and by the protocol tests. Windows-only; the implementation lives
// in link.cpp so this header stays free of <Windows.h>.
//
// Threading: Tick() may run on its own thread; GetState() is safe from any thread.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "../ahshitrim_protocol.h"

namespace AhShitRim
{
    class Link
    {
    public:
        enum class State
        {
            Detached,  // no mapping yet (or mapping failed; retried every tick)
            Waiting,   // mapped, waiting for the creator to finish or the peer to show up
            Up,        // both sides attached, same version, peer heartbeat alive
            Disabled,  // version or layout mismatch; stays disabled until restart
        };

        using LogFn = std::function<void(std::string_view)>;

        Link(Protocol::Side a_side, LogFn a_log, std::wstring a_mappingName = Protocol::kMappingName);
        ~Link();

        Link(const Link&) = delete;
        Link& operator=(const Link&) = delete;

        // Attaches, bumps our heartbeat and checks the peer. Call regularly (10+ times a second).
        void Tick(std::uint64_t a_nowMs);

        State GetState() const { return _state.load(std::memory_order_acquire); }
        bool  IsUp() const { return GetState() == State::Up; }

    private:
        void TryMap();
        void Detach();
        void Disable(const std::string& a_reason);
        std::uint8_t* Base() const { return _base.load(std::memory_order_acquire); }

        const Protocol::Side _side;
        const LogFn          _log;
        const std::wstring   _name;

        void*                      _mapping = nullptr;
        std::atomic<std::uint8_t*> _base{ nullptr };
        std::atomic<State>         _state{ State::Detached };
        std::atomic<bool>          _attached{ false };  // our PeerInfo is published
        bool                       _mapFailLogged = false;
        bool                       _peerSeeded = false;  // first heartbeat read only seeds, so a dead peer never looks alive
        bool                       _peerSeen = false;
        std::uint64_t              _peerHeartbeat = 0;
        std::uint64_t              _peerSeenMs = 0;
    };
}
