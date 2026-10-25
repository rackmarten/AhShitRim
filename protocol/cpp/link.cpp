#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "link.h"

#include <algorithm>
#include <cstring>
#include <format>

// The release a plugin build belongs to (both plugins' CMake set it), for the version stamp.
#ifndef AHSHITRIM_VERSION
#    define AHSHITRIM_VERSION "dev"
#endif

namespace AhShitRim
{
    struct VersionStamp
    {
        char text[64];
    };

    consteval VersionStamp MakeVersionStamp()
    {
        VersionStamp stamp{};
        std::size_t  n = 0;
        auto         put = [&](const char* a_text) {
            while (*a_text) {
                stamp.text[n++] = *a_text++;
            }
        };
        put("AhShitRim-link-stamp: protocol ");
        char digits[10]{};
        int  count = 0;
        for (std::uint32_t version = Protocol::kProtocolVersion; version || count == 0; version /= 10) {
            digits[count++] = static_cast<char>('0' + version % 10);
        }
        while (count) {
            stamp.text[n++] = digits[--count];
        }
        put(", release ");
        put(AHSHITRIM_VERSION);
        return stamp;
    }
}

// The launcher finds this text in AhShitRim.dll and AhShitRim.asi and refuses to start two halves that speak
// different versions. Exported, so the linker keeps it.
extern "C" __declspec(dllexport) const AhShitRim::VersionStamp AhShitRimLinkStamp = AhShitRim::MakeVersionStamp();

namespace AhShitRim
{
    using namespace Protocol;

    namespace
    {
        template <class T>
        T* At(std::uint8_t* a_base, std::uint32_t a_offset)
        {
            return reinterpret_cast<T*>(a_base + a_offset);
        }

        template <class T>
        std::atomic_ref<T> Atomic(T& a_value)
        {
            return std::atomic_ref<T>(a_value);
        }

        const char* SideName(Side a_side)
        {
            return a_side == Side::Skyrim ? "Skyrim plugin" : "San Andreas plugin";
        }

        Side Other(Side a_side)
        {
            return a_side == Side::Skyrim ? Side::SanAndreas : Side::Skyrim;
        }

        // The ring a_side reads: Skyrim reads the one from San Andreas and the other way round.
        RingHeader* Inbound(std::uint8_t* a_base, Side a_side)
        {
            return At<RingHeader>(a_base, a_side == Side::SanAndreas ? kOffsetRingToSanAndreas : kOffsetRingToSkyrim);
        }

        std::uint8_t* RingData(RingHeader* a_ring)
        {
            return reinterpret_cast<std::uint8_t*>(a_ring + 1);
        }

        void CopyIn(RingHeader* a_ring, std::uint64_t a_pos, const void* a_src, std::uint32_t a_size)
        {
            const std::uint64_t cap = a_ring->capacity;
            const std::uint64_t at = a_pos & (cap - 1);
            const std::uint64_t first = (std::min)(static_cast<std::uint64_t>(a_size), cap - at);
            std::memcpy(RingData(a_ring) + at, a_src, static_cast<std::size_t>(first));
            std::memcpy(RingData(a_ring), static_cast<const std::uint8_t*>(a_src) + first, static_cast<std::size_t>(a_size - first));
        }

        void CopyOut(RingHeader* a_ring, std::uint64_t a_pos, void* a_dst, std::uint32_t a_size)
        {
            const std::uint64_t cap = a_ring->capacity;
            const std::uint64_t at = a_pos & (cap - 1);
            const std::uint64_t first = (std::min)(static_cast<std::uint64_t>(a_size), cap - at);
            std::memcpy(a_dst, RingData(a_ring) + at, static_cast<std::size_t>(first));
            std::memcpy(static_cast<std::uint8_t*>(a_dst) + first, RingData(a_ring), static_cast<std::size_t>(a_size - first));
        }

        template <class TSlot, class T>
        void SeqWrite(TSlot* a_slot, const T& a_value)
        {
            auto seq = Atomic(a_slot->seq);
            const std::uint32_t n = seq.load(std::memory_order_relaxed);
            seq.store(n + 1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            std::memcpy(&a_slot->value, &a_value, sizeof(T));
            seq.store(n + 2, std::memory_order_release);
        }

        template <class TSlot, class T>
        bool SeqRead(TSlot* a_slot, T& a_out)
        {
            auto seq = Atomic(a_slot->seq);
            for (int attempt = 0; attempt < 64; ++attempt) {
                const std::uint32_t before = seq.load(std::memory_order_acquire);
                if (before == 0) {
                    return false;
                }
                if (before & 1) {
                    continue;
                }
                std::memcpy(&a_out, &a_slot->value, sizeof(T));
                std::atomic_thread_fence(std::memory_order_acquire);
                if (seq.load(std::memory_order_relaxed) == before) {
                    return true;
                }
            }
            return false;
        }
    }

    Link::Link(Side a_side, LogFn a_log, std::wstring a_mappingName) :
        _side(a_side),
        _log(std::move(a_log)),
        _name(std::move(a_mappingName))
    {}

    Link::~Link()
    {
        Detach();
    }

    void Link::TryMap(std::uint64_t a_nowMs)
    {
        HANDLE mapping = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kMappingSize, _name.c_str());
        if (!mapping) {
            if (!_mapFailLogged) {
                _log(std::format("cannot create shared memory (error {}), retrying", ::GetLastError()));
                _mapFailLogged = true;
            }
            return;
        }
        const bool existed = ::GetLastError() == ERROR_ALREADY_EXISTS;

        // Size 0 maps the whole section, whatever size its creator gave it. Until the version check
        // passes only the frozen LinkHeader is touched.
        void* view = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!view) {
            if (!_mapFailLogged) {
                _log(std::format("cannot map shared memory (error {}), retrying", ::GetLastError()));
                _mapFailLogged = true;
            }
            ::CloseHandle(mapping);
            return;
        }

        auto* base = static_cast<std::uint8_t*>(view);
        if (!existed) {
            // Fresh sections are zero-filled. Publish magic last so the peer never sees half a header.
            auto* header = At<LinkHeader>(base, kOffsetHeader);
            header->headerSize = sizeof(LinkHeader);
            header->mappingSize = kMappingSize;
            header->creatorVersion = kProtocolVersion;
            Inbound(base, Side::SanAndreas)->capacity = kRingToSanAndreasCapacity;
            Inbound(base, Side::Skyrim)->capacity = kRingToSkyrimCapacity;
            Atomic(header->magic).store(kMagic, std::memory_order_release);
        }

        _mapping = mapping;
        _mappedMs = a_nowMs;
        _joined = existed;
        _base.store(base, std::memory_order_release);
        if (!existed) {
            _otherVersion.store(0, std::memory_order_release);
            _notice.Due(0, a_nowMs);
            _state.store(State::Waiting, std::memory_order_release);
            _log("created shared memory, waiting for peer");
        } else if (GetState() != State::Mismatch) {
            _state.store(State::Waiting, std::memory_order_release);
        }
    }

    namespace
    {
        // A peer that crashed leaves its PeerInfo behind; its version only counts while its process runs.
        bool ProcessAlive(std::uint32_t a_pid)
        {
            if (a_pid == 0) {
                return false;
            }
            if (a_pid == ::GetCurrentProcessId()) {
                return true;
            }
            HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, a_pid);
            if (!process) {
                return ::GetLastError() == ERROR_ACCESS_DENIED;  // it exists, we just may not look
            }
            DWORD      code = 0;
            const bool alive = ::GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
            ::CloseHandle(process);
            return alive;
        }
    }

    void Link::Tick(std::uint64_t a_nowMs)
    {
        if (!Base()) {
            if (a_nowMs < _retryAtMs) {
                return;
            }
            TryMap(a_nowMs);
            if (!Base()) {
                return;
            }
        }

        auto*     header = At<LinkHeader>(Base(), kOffsetHeader);
        PeerInfo& self = header->peers[static_cast<std::uint32_t>(_side)];
        PeerInfo& peer = header->peers[static_cast<std::uint32_t>(Other(_side))];

        // Until we attach, the section may not be ours. Once attached its header never changes, and only
        // a section we never attached to is let go of, so no other thread is still using it then.
        if (!_attached.load(std::memory_order_relaxed)) {
            const std::uint32_t magic = Atomic(header->magic).load(std::memory_order_acquire);
            switch (Handshake::CheckSection(magic, header->headerSize, header->mappingSize, header->creatorVersion)) {
            case Handshake::Section::Initializing:
                if (a_nowMs - _mappedMs >= Handshake::kInitTimeoutMs) {
                    _log("the shared memory was never set up (its creator quit halfway), letting go of it");
                    Release(a_nowMs);
                }
                return;
            case Handshake::Section::Foreign:
                if (!_foreignLogged) {
                    _log("the shared memory is not an AhShitRim link (unknown magic or header size), retrying");
                    _foreignLogged = true;
                }
                Release(a_nowMs);
                return;
            case Handshake::Section::OtherVersion:
                // Publish ourselves first, so the other half can report the mismatch too, then let go: the
                // section dies with its last holder and the next try creates one of ours.
                Atomic(self.pid).store(::GetCurrentProcessId(), std::memory_order_relaxed);
                Atomic(self.version).store(kProtocolVersion, std::memory_order_release);
                Mismatched(Handshake::OtherVersion(Atomic(peer.version).load(std::memory_order_acquire), header->creatorVersion), a_nowMs);
                Release(a_nowMs);
                return;
            case Handshake::Section::Usable:
                break;
            }
            Atomic(self.pid).store(::GetCurrentProcessId(), std::memory_order_relaxed);
            Atomic(self.version).store(kProtocolVersion, std::memory_order_release);
            // Anything still in our ring was sent to an earlier instance of us.
            RingHeader* ring = Inbound(Base(), _side);
            Atomic(ring->tail).store(Atomic(ring->head).load(std::memory_order_acquire), std::memory_order_release);
            _foreignLogged = false;
            _attached.store(true, std::memory_order_release);
            if (_joined) {
                _log("joined shared memory, waiting for peer");  // only once it's ours, so opening another version's stays quiet
            }
        }

        Atomic(self.heartbeat).fetch_add(1, std::memory_order_release);

        const std::uint32_t peerVersion = Atomic(peer.version).load(std::memory_order_acquire);
        const std::uint32_t peerPid = Atomic(peer.pid).load(std::memory_order_acquire);
        if (peerVersion != 0 && peerVersion != kProtocolVersion && ProcessAlive(peerPid)) {
            if (GetState() == State::Up) {
                _log(std::format("link down: the {} now speaks v{}", SideName(Other(_side)), peerVersion));
            }
            _peerSeeded = false;
            _peerSeen = false;
            Mismatched(peerVersion, a_nowMs);
            return;
        }
        if (GetState() == State::Mismatch) {
            // The other version is gone (detached, crashed, or replaced by ours): handshake again.
            _otherVersion.store(0, std::memory_order_release);
            _notice.Due(0, a_nowMs);
            _state.store(State::Waiting, std::memory_order_release);
            _log(peerVersion == kProtocolVersion ?
                     std::format("the {} now speaks v{} too, waiting for its heartbeat", SideName(Other(_side)), kProtocolVersion) :
                     std::format("the {} of another version is gone, waiting for peer", SideName(Other(_side))));
        }
        const State state = GetState();

        const std::uint64_t heartbeat = Atomic(peer.heartbeat).load(std::memory_order_acquire);
        if (!_peerSeeded) {
            _peerSeeded = true;
            _peerHeartbeat = heartbeat;
        } else if (heartbeat != _peerHeartbeat) {
            _peerHeartbeat = heartbeat;
            _peerSeen = true;
            _peerSeenMs = a_nowMs;
        }

        const bool alive = peerPid != 0 && peerVersion == kProtocolVersion && _peerSeen &&
                           a_nowMs - _peerSeenMs < kPeerTimeoutMs;
        if (alive && state != State::Up) {
            _state.store(State::Up, std::memory_order_release);
            _log(std::format("link up: {} pid {}, protocol v{}", SideName(Other(_side)), peerPid, kProtocolVersion));
        } else if (!alive && state == State::Up) {
            _state.store(State::Waiting, std::memory_order_release);
            _log(peerPid == 0 ?
                     std::format("link down: the {} detached", SideName(Other(_side))) :
                     std::format("link down: no heartbeat from the {} for {} ms", SideName(Other(_side)), kPeerTimeoutMs));
        }
    }

    void Link::Mismatched(std::uint32_t a_otherVersion, std::uint64_t a_nowMs)
    {
        _otherVersion.store(a_otherVersion, std::memory_order_release);
        _state.store(State::Mismatch, std::memory_order_release);
        if (_notice.Due(a_otherVersion, a_nowMs)) {
            _log(std::format("protocol mismatch: the {} speaks v{}, this {} speaks v{}; waiting for it to restart with v{}. {}",
                SideName(Other(_side)), a_otherVersion, SideName(_side), kProtocolVersion, kProtocolVersion,
                Handshake::MismatchMessage(_side, a_otherVersion)));
        }
    }

    // Lets go of a section we never attached to (our PeerInfo, if published, stays for the other half to read)
    // and opens one again after kRemapMs.
    void Link::Release(std::uint64_t a_nowMs)
    {
        std::uint8_t* base = Base();
        _base.store(nullptr, std::memory_order_release);
        ::UnmapViewOfFile(base);
        ::CloseHandle(static_cast<HANDLE>(_mapping));
        _mapping = nullptr;
        _retryAtMs = a_nowMs + Handshake::kRemapMs;
        if (GetState() != State::Mismatch) {
            _state.store(State::Detached, std::memory_order_release);
        }
    }

    void Link::Detach()
    {
        std::uint8_t* base = Base();
        if (!base) {
            return;
        }
        if (_attached.load(std::memory_order_acquire)) {
            PeerInfo& self = At<LinkHeader>(base, kOffsetHeader)->peers[static_cast<std::uint32_t>(_side)];
            Atomic(self.pid).store(0, std::memory_order_release);
            Atomic(self.version).store(0, std::memory_order_release);
        }
        _attached.store(false, std::memory_order_release);
        _base.store(nullptr, std::memory_order_release);
        ::UnmapViewOfFile(base);
        ::CloseHandle(static_cast<HANDLE>(_mapping));
        _mapping = nullptr;
        _state.store(State::Detached, std::memory_order_release);
    }

    void Link::WriteInput(const InputState& a_value)
    {
        if (std::uint8_t* base = Base(); base && _attached.load(std::memory_order_acquire)) {
            SeqWrite(At<InputSlot>(base, kOffsetInputSlot), a_value);
        }
    }

    bool Link::ReadInput(InputState& a_out) const
    {
        return IsUp() && SeqRead(At<InputSlot>(Base(), kOffsetInputSlot), a_out);
    }

    void Link::WritePlayer(const PlayerState& a_value)
    {
        if (std::uint8_t* base = Base(); base && _attached.load(std::memory_order_acquire)) {
            SeqWrite(At<PlayerSlot>(base, kOffsetPlayerSlot), a_value);
        }
    }

    bool Link::ReadPlayer(PlayerState& a_out) const
    {
        return IsUp() && SeqRead(At<PlayerSlot>(Base(), kOffsetPlayerSlot), a_out);
    }

    void Link::WriteAudio(const AudioState& a_value)
    {
        if (std::uint8_t* base = Base(); base && _attached.load(std::memory_order_acquire)) {
            SeqWrite(At<AudioSlot>(base, kOffsetAudioSlot), a_value);
        }
    }

    bool Link::ReadAudio(AudioState& a_out) const
    {
        return IsUp() && SeqRead(At<AudioSlot>(Base(), kOffsetAudioSlot), a_out);
    }

    void Link::WriteBody(const BodyState& a_value)
    {
        if (std::uint8_t* base = Base(); base && _attached.load(std::memory_order_acquire)) {
            SeqWrite(At<BodySlot>(base, kOffsetBodySlot), a_value);
        }
    }

    bool Link::ReadBody(BodyState& a_out) const
    {
        return IsUp() && SeqRead(At<BodySlot>(Base(), kOffsetBodySlot), a_out);
    }

    void Link::WriteVehicle(std::uint32_t a_index, const BodyState& a_value)
    {
        if (std::uint8_t* base = Base(); base && a_index < kVehicleMax && _attached.load(std::memory_order_acquire)) {
            SeqWrite(At<BodySlot>(base, kOffsetVehicleSlots + a_index * sizeof(BodySlot)), a_value);
        }
    }

    bool Link::ReadVehicle(std::uint32_t a_index, BodyState& a_out) const
    {
        return IsUp() && a_index < kVehicleMax && SeqRead(At<BodySlot>(Base(), kOffsetVehicleSlots + a_index * sizeof(BodySlot)), a_out);
    }

    void Link::WriteWeapon(std::uint32_t a_hand, const BodyState& a_value)
    {
        if (std::uint8_t* base = Base(); base && a_hand < kWeaponHandMax && _attached.load(std::memory_order_acquire)) {
            SeqWrite(At<BodySlot>(base, kOffsetWeaponSlots + a_hand * sizeof(BodySlot)), a_value);
        }
    }

    bool Link::ReadWeapon(std::uint32_t a_hand, BodyState& a_out) const
    {
        return IsUp() && a_hand < kWeaponHandMax && SeqRead(At<BodySlot>(Base(), kOffsetWeaponSlots + a_hand * sizeof(BodySlot)), a_out);
    }

    void Link::WriteGear(const BodyState& a_value)
    {
        if (std::uint8_t* base = Base(); base && _attached.load(std::memory_order_acquire)) {
            SeqWrite(At<BodySlot>(base, kOffsetGearSlot), a_value);
        }
    }

    bool Link::ReadGear(BodyState& a_out) const
    {
        return IsUp() && SeqRead(At<BodySlot>(Base(), kOffsetGearSlot), a_out);
    }

    void Link::WriteProxies(const ProxyState& a_value)
    {
        if (std::uint8_t* base = Base(); base && _attached.load(std::memory_order_acquire)) {
            SeqWrite(At<ProxySlot>(base, kOffsetProxySlot), a_value);
        }
    }

    bool Link::ReadProxies(ProxyState& a_out) const
    {
        return IsUp() && SeqRead(At<ProxySlot>(Base(), kOffsetProxySlot), a_out);
    }

    bool Link::Send(MessageType a_type, const void* a_payload, std::uint32_t a_size)
    {
        if (!IsUp()) {
            return false;
        }
        RingHeader*         ring = Inbound(Base(), Other(_side));
        const std::uint64_t total = sizeof(MessageHeader) + static_cast<std::uint64_t>(a_size);
        const std::uint64_t head = Atomic(ring->head).load(std::memory_order_relaxed);
        const std::uint64_t tail = Atomic(ring->tail).load(std::memory_order_acquire);
        if (total > ring->capacity - (head - tail)) {
            return false;
        }
        const MessageHeader message{ static_cast<std::uint16_t>(a_type), 0, a_size };
        CopyIn(ring, head, &message, sizeof(message));
        CopyIn(ring, head + sizeof(message), a_payload, a_size);
        Atomic(ring->head).store(head + total, std::memory_order_release);
        return true;
    }

    std::size_t Link::Receive(const std::function<void(MessageType, const std::uint8_t*, std::uint32_t)>& a_handler)
    {
        if (!_attached.load(std::memory_order_acquire) || GetState() == State::Mismatch) {
            return 0;
        }
        RingHeader*         ring = Inbound(Base(), _side);
        const std::uint64_t head = Atomic(ring->head).load(std::memory_order_acquire);
        std::uint64_t       tail = Atomic(ring->tail).load(std::memory_order_relaxed);
        std::size_t         count = 0;
        while (head - tail >= sizeof(MessageHeader)) {
            MessageHeader message{};
            CopyOut(ring, tail, &message, sizeof(message));
            if (sizeof(MessageHeader) + static_cast<std::uint64_t>(message.size) > head - tail) {
                _log(std::format("inbound ring corrupt (message of {} bytes), dropping it", message.size));
                tail = head;
                break;
            }
            _receiveBuffer.resize(message.size);
            CopyOut(ring, tail + sizeof(message), _receiveBuffer.data(), message.size);
            tail += sizeof(message) + message.size;
            Atomic(ring->tail).store(tail, std::memory_order_release);
            a_handler(static_cast<MessageType>(message.type), _receiveBuffer.data(), message.size);
            ++count;
        }
        Atomic(ring->tail).store(tail, std::memory_order_release);
        return count;
    }
}
