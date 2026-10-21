#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "link.h"

#include <algorithm>
#include <cstring>
#include <format>

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

    void Link::TryMap()
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
        _base.store(base, std::memory_order_release);
        _state.store(State::Waiting, std::memory_order_release);
        _log(existed ? "joined shared memory, waiting for peer" : "created shared memory, waiting for peer");
    }

    void Link::Tick(std::uint64_t a_nowMs)
    {
        const State state = GetState();
        if (state == State::Disabled) {
            return;
        }
        if (!Base()) {
            TryMap();
            if (!Base()) {
                return;
            }
        }

        auto* header = At<LinkHeader>(Base(), kOffsetHeader);
        const std::uint32_t magic = Atomic(header->magic).load(std::memory_order_acquire);
        if (magic == 0) {
            return;  // creator is still initializing
        }
        if (magic != kMagic || header->headerSize != sizeof(LinkHeader)) {
            Disable("the shared memory is not an AhShitRim link (unknown magic or header size)");
            return;
        }

        PeerInfo& self = header->peers[static_cast<std::uint32_t>(_side)];
        PeerInfo& peer = header->peers[static_cast<std::uint32_t>(Other(_side))];

        if (!_attached.load(std::memory_order_relaxed)) {
            // Publish ourselves first, even on a version mismatch, so the peer can report it too.
            Atomic(self.pid).store(::GetCurrentProcessId(), std::memory_order_relaxed);
            Atomic(self.version).store(kProtocolVersion, std::memory_order_release);
            if (header->creatorVersion != kProtocolVersion || header->mappingSize != kMappingSize) {
                Disable(std::format(
                    "protocol mismatch: the shared memory was created with protocol v{}, this {} speaks v{}; update both halves to the same release",
                    header->creatorVersion, SideName(_side), kProtocolVersion));
                return;
            }
            // Anything still in our ring was sent to an earlier instance of us.
            RingHeader* ring = Inbound(Base(), _side);
            Atomic(ring->tail).store(Atomic(ring->head).load(std::memory_order_acquire), std::memory_order_release);
            _attached.store(true, std::memory_order_release);
        }

        Atomic(self.heartbeat).fetch_add(1, std::memory_order_release);

        const std::uint32_t peerVersion = Atomic(peer.version).load(std::memory_order_acquire);
        const std::uint32_t peerPid = Atomic(peer.pid).load(std::memory_order_acquire);
        if (peerVersion != 0 && peerVersion != kProtocolVersion) {
            Disable(std::format(
                "protocol mismatch: the {} speaks v{}, this {} speaks v{}; update both halves to the same release",
                SideName(Other(_side)), peerVersion, SideName(_side), kProtocolVersion));
            return;
        }

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

    void Link::Disable(const std::string& a_reason)
    {
        _state.store(State::Disabled, std::memory_order_release);
        _log("link disabled: " + a_reason);
    }

    void Link::Detach()
    {
        std::uint8_t* base = Base();
        if (!base) {
            return;
        }
        if (_attached.load(std::memory_order_acquire) || GetState() == State::Disabled) {
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
        if (!_attached.load(std::memory_order_acquire) || GetState() == State::Disabled) {
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
