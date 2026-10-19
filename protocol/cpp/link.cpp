#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "link.h"

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
}
