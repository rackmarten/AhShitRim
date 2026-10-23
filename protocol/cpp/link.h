// C++ end of the AhShitRim link, used by both halves (the SKSE plugin builds it 64-bit, the
// San Andreas ASI plugin 32-bit) and by the protocol tests. Windows-only; the implementation lives
// in link.cpp so this header stays free of <Windows.h>.
//
// Threading: Tick() may run on its own thread; GetState() and the slot reads and writes are safe
// from any thread.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

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

        // Latest-value slots. Writes need our side attached (they're dropped otherwise), reads need
        // the link up; a read returns false when the slot was never written or stayed torn.
        void WriteInput(const Protocol::InputState& a_value);
        bool ReadInput(Protocol::InputState& a_out) const;
        void WritePlayer(const Protocol::PlayerState& a_value);
        bool ReadPlayer(Protocol::PlayerState& a_out) const;
        void WriteAudio(const Protocol::AudioState& a_value);  // v7
        bool ReadAudio(Protocol::AudioState& a_out) const;
        void WriteBody(const Protocol::BodyState& a_value);
        bool ReadBody(Protocol::BodyState& a_out) const;
        // Vehicle slot a_index (< kVehicleMax, v6); out-of-range indices are ignored.
        void WriteVehicle(std::uint32_t a_index, const Protocol::BodyState& a_value);
        bool ReadVehicle(std::uint32_t a_index, Protocol::BodyState& a_out) const;
        // Weapon slot a_hand (< kWeaponHandMax, v12): the model in CJ's right (0) or left (1) hand, posed
        // like a body (feet are CJ's, the bones the weapon's parts); out-of-range hands are ignored.
        void WriteWeapon(std::uint32_t a_hand, const Protocol::BodyState& a_value);
        bool ReadWeapon(std::uint32_t a_hand, Protocol::BodyState& a_out) const;
        // The gear slot (v19): the jetpack or the open parachute on CJ, posed like a body (feet are CJ's).
        void WriteGear(const Protocol::BodyState& a_value);
        bool ReadGear(Protocol::BodyState& a_out) const;

        // The rings: Send puts a message in the ring to the other side, and returns false when the
        // link is down, the ring is full or the message can never fit. Receive drains the ring to
        // our side, calling a_handler(type, payload, size) per message, and returns the message count.
        bool        Send(Protocol::MessageType a_type, const void* a_payload, std::uint32_t a_size);
        std::size_t Receive(const std::function<void(Protocol::MessageType, const std::uint8_t*, std::uint32_t)>& a_handler);

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
        std::vector<std::uint8_t>  _receiveBuffer;
    };
}
