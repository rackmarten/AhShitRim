#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "../../cpp/link.h"

using namespace AhShitRim;
using namespace AhShitRim::Protocol;

namespace
{
    // Each test gets its own section, so tests never see each other's state.
    std::wstring UniqueName()
    {
        static std::atomic<int> counter{ 0 };
        return L"Local\\AhShitRim.Test." + std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(counter++);
    }

    struct Logged
    {
        std::vector<std::string> lines;

        Link::LogFn Fn()
        {
            return [this](std::string_view a_line) { lines.emplace_back(a_line); };
        }

        bool Has(std::string_view a_needle) const
        {
            for (const auto& line : lines) {
                if (line.find(a_needle) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }
    };

    struct Pair
    {
        std::wstring          name = UniqueName();
        Logged                skyrimLog, saLog;
        std::unique_ptr<Link> skyrim = std::make_unique<Link>(Side::Skyrim, skyrimLog.Fn(), name);
        std::unique_ptr<Link> sa = std::make_unique<Link>(Side::SanAndreas, saLog.Fn(), name);
        std::uint64_t         now = 1000;

        void Tick(int a_times = 1, std::uint64_t a_stepMs = 50)
        {
            for (int i = 0; i < a_times; ++i) {
                now += a_stepMs;
                if (skyrim) {
                    skyrim->Tick(now);
                }
                if (sa) {
                    sa->Tick(now);
                }
            }
        }
    };

    // Raw view of a section, for faking a peer of another version.
    struct RawSection
    {
        HANDLE        handle = nullptr;
        std::uint8_t* base = nullptr;

        explicit RawSection(const std::wstring& a_name, std::uint32_t a_size = kMappingSize)
        {
            handle = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, a_size, a_name.c_str());
            base = static_cast<std::uint8_t*>(::MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0));
        }
        ~RawSection()
        {
            ::UnmapViewOfFile(base);
            ::CloseHandle(handle);
        }
        LinkHeader* Header() { return reinterpret_cast<LinkHeader*>(base); }
    };
}

TEST_CASE("link comes up once both sides tick")
{
    Pair p;
    p.Tick(1);
    CHECK_FALSE(p.skyrim->IsUp());  // first heartbeat read only seeds
    CHECK(p.skyrim->GetState() == Link::State::Waiting);
    p.Tick(2);
    CHECK(p.skyrim->IsUp());
    CHECK(p.sa->IsUp());
    CHECK(p.skyrimLog.Has("created shared memory"));
    CHECK(p.saLog.Has("joined shared memory"));
    CHECK(p.skyrimLog.Has("link up: San Andreas plugin pid " + std::to_string(::GetCurrentProcessId()) + ", protocol v" + std::to_string(kProtocolVersion)));
    CHECK(p.saLog.Has("link up: Skyrim plugin pid"));
}

TEST_CASE("the San Andreas side can start first")
{
    const std::wstring name = UniqueName();
    Logged             saLog, skyrimLog;
    Link               sa(Side::SanAndreas, saLog.Fn(), name);
    std::uint64_t      now = 1000;
    for (int i = 0; i < 20; ++i) {
        sa.Tick(now += 50);  // nobody there yet
    }
    CHECK(sa.GetState() == Link::State::Waiting);
    CHECK(saLog.Has("created shared memory"));

    Link skyrim(Side::Skyrim, skyrimLog.Fn(), name);
    for (int i = 0; i < 3; ++i) {
        now += 50;
        sa.Tick(now);
        skyrim.Tick(now);
    }
    CHECK(sa.IsUp());
    CHECK(skyrim.IsUp());
}

TEST_CASE("a peer that left its pid behind without a heartbeat never looks alive")
{
    const std::wstring name = UniqueName();
    RawSection         raw(name);
    raw.Header()->headerSize = sizeof(LinkHeader);
    raw.Header()->mappingSize = kMappingSize;
    raw.Header()->creatorVersion = kProtocolVersion;
    raw.Header()->magic = kMagic;
    PeerInfo& dead = raw.Header()->peers[static_cast<int>(Side::SanAndreas)];
    dead.pid = 1234;  // crashed without detaching
    dead.version = kProtocolVersion;
    dead.heartbeat = 77;

    Logged log;
    Link   skyrim(Side::Skyrim, log.Fn(), name);
    for (std::uint64_t now = 1000; now < 5000; now += 50) {
        skyrim.Tick(now);
    }
    CHECK(skyrim.GetState() == Link::State::Waiting);
    CHECK_FALSE(log.Has("link up"));
}

TEST_CASE("link goes down when the peer stops ticking or detaches")
{
    Pair p;
    p.Tick(3);
    REQUIRE(p.skyrim->IsUp());

    // San Andreas stalls (a long load, say) past the timeout.
    for (int i = 0; i < 50; ++i) {
        p.now += 50;
        p.skyrim->Tick(p.now);
    }
    CHECK_FALSE(p.skyrim->IsUp());
    CHECK(p.skyrimLog.Has("link down: no heartbeat from the San Andreas plugin for 2000 ms"));

    p.Tick(2);
    CHECK(p.skyrim->IsUp());

    p.sa.reset();  // clean detach
    p.Tick(1);
    CHECK_FALSE(p.skyrim->IsUp());
    CHECK(p.skyrimLog.Has("link down: the San Andreas plugin detached"));
}

TEST_CASE("a restarted peer comes back up")
{
    Pair p;
    p.Tick(3);
    REQUIRE(p.skyrim->IsUp());

    p.skyrim.reset();
    p.Tick(1);
    CHECK(p.saLog.Has("link down: the Skyrim plugin detached"));
    p.skyrim = std::make_unique<Link>(Side::Skyrim, p.skyrimLog.Fn(), p.name);
    p.Tick(3);
    CHECK(p.skyrim->IsUp());
    CHECK(p.sa->IsUp());
}

TEST_CASE("a peer speaking another protocol version disables the link")
{
    Pair p;
    p.Tick(3);
    REQUIRE(p.skyrim->IsUp());

    // Pretend the San Andreas side was replaced by one speaking v99.
    p.sa.reset();
    RawSection raw(p.name);
    PeerInfo&  sa = raw.Header()->peers[static_cast<int>(Side::SanAndreas)];
    sa.pid = 1234;
    sa.version = 99;
    p.Tick(1);
    CHECK(p.skyrim->GetState() == Link::State::Disabled);
    CHECK(p.skyrimLog.Has("protocol mismatch: the San Andreas plugin speaks v99, this Skyrim plugin speaks v" + std::to_string(kProtocolVersion)));

    // Disabled stays disabled, even if the peer goes away.
    sa.version = 0;
    p.Tick(5);
    CHECK(p.skyrim->GetState() == Link::State::Disabled);
}

TEST_CASE("a section created by another protocol version disables the joining side")
{
    const std::wstring name = UniqueName();
    RawSection         raw(name, kMappingSize);
    raw.Header()->headerSize = sizeof(LinkHeader);
    raw.Header()->mappingSize = kMappingSize;
    raw.Header()->creatorVersion = 99;
    raw.Header()->magic = kMagic;

    Logged log;
    Link   sa(Side::SanAndreas, log.Fn(), name);
    sa.Tick(1000);
    CHECK(sa.GetState() == Link::State::Disabled);
    CHECK(log.Has("protocol mismatch: the shared memory was created with protocol v99, this San Andreas plugin speaks v" + std::to_string(kProtocolVersion)));
    // Its version is published anyway, so the creator side can report the mismatch as well.
    CHECK(raw.Header()->peers[static_cast<int>(Side::SanAndreas)].version == kProtocolVersion);
}

TEST_CASE("a section still being initialized is waited for")
{
    const std::wstring name = UniqueName();
    RawSection         raw(name);  // zero-filled: magic 0

    Logged log;
    Link   skyrim(Side::Skyrim, log.Fn(), name);
    skyrim.Tick(1000);
    CHECK(skyrim.GetState() == Link::State::Waiting);
    CHECK(raw.Header()->peers[static_cast<int>(Side::Skyrim)].pid == 0);  // nothing published yet
}

TEST_CASE("a foreign section is rejected")
{
    const std::wstring name = UniqueName();
    RawSection         raw(name);
    raw.Header()->magic = 0xDEADBEEF;

    Logged log;
    Link   skyrim(Side::Skyrim, log.Fn(), name);
    skyrim.Tick(1000);
    CHECK(skyrim.GetState() == Link::State::Disabled);
    CHECK(log.Has("not an AhShitRim link"));
}

TEST_CASE("slots are empty until written, and reads need the link up")
{
    Pair        p;
    InputState  input{};
    PlayerState player{};
    p.Tick(3);
    REQUIRE(p.skyrim->IsUp());
    CHECK_FALSE(p.sa->ReadInput(input));
    CHECK_FALSE(p.skyrim->ReadPlayer(player));

    input.frame = 1;
    p.skyrim->WriteInput(input);
    p.sa.reset();  // detaches: the Skyrim side goes down and stops reading
    p.Tick(1);
    CHECK_FALSE(p.skyrim->IsUp());
    CHECK_FALSE(p.skyrim->ReadPlayer(player));
}

TEST_CASE("input and pose round-trip through their slots")
{
    Pair p;
    p.Tick(3);
    REQUIRE(p.sa->IsUp());

    InputState input{};
    input.frame = 42;
    input.flags = kInputOwnsPlayer;
    input.mouseX = 1234.5;
    input.mouseY = -6.25;
    input.moveX = -1.0f;
    input.moveY = 1.0f;
    input.buttons = kButtonSprint | kButtonJump;
    input.handover = 3;
    input.heading = 1.5f;
    p.skyrim->WriteInput(input);

    InputState got{};
    REQUIRE(p.sa->ReadInput(got));
    CHECK(got.frame == 42);
    CHECK(got.flags == kInputOwnsPlayer);
    CHECK(got.mouseX == 1234.5);
    CHECK(got.mouseY == -6.25);
    CHECK(got.moveX == -1.0f);
    CHECK(got.moveY == 1.0f);
    CHECK(got.buttons == (kButtonSprint | kButtonJump));
    CHECK(got.handover == 3);
    CHECK(got.heading == 1.5f);

    PlayerState pose{};
    pose.frame = 7;
    pose.flags = kPlayerValid;
    pose.offset[0] = 70.0f;
    pose.velocity[1] = -140.0f;
    pose.viewPitch = 0.25f;
    pose.handover = 3;
    p.sa->WritePlayer(pose);
    p.sa->WritePlayer(pose);  // the newest write wins, a reader never sees a half-written one

    PlayerState back{};
    REQUIRE(p.skyrim->ReadPlayer(back));
    CHECK(back.frame == 7);
    CHECK(back.offset[0] == 70.0f);
    CHECK(back.velocity[1] == -140.0f);
    CHECK(back.viewPitch == 0.25f);
    CHECK(back.handover == 3);
}

TEST_CASE("the ring delivers collision chunks in order, across the wrap point")
{
    Pair p;
    p.Tick(3);
    REQUIRE(p.sa->IsUp());

    // 4 MiB ring; push 12 MB through it in 300 KB messages.
    std::vector<std::uint8_t> payload(300000);
    std::uint32_t             sent = 0, received = 0;
    for (int round = 0; round < 40; ++round) {
        for (std::size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<std::uint8_t>(sent + i);
        }
        REQUIRE(p.skyrim->Send(MessageType::CollisionChunk, payload.data(), static_cast<std::uint32_t>(payload.size())));
        ++sent;
        p.sa->Receive([&](MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
            CHECK(a_type == MessageType::CollisionChunk);
            REQUIRE(a_size == 300000);
            CHECK(a_data[0] == static_cast<std::uint8_t>(received));
            CHECK(a_data[299999] == static_cast<std::uint8_t>(received + 299999));
            ++received;
        });
    }
    CHECK(received == sent);
}

TEST_CASE("the rings refuse messages when full or too large, and each side reads only its own")
{
    Pair p;
    p.Tick(3);
    const auto                ignore = [](MessageType, const std::uint8_t*, std::uint32_t) {};
    std::vector<std::uint8_t> payload(1500000);
    CHECK(p.skyrim->Send(MessageType::CollisionChunk, payload.data(), 1500000));
    CHECK(p.skyrim->Send(MessageType::CollisionChunk, payload.data(), 1500000));
    CHECK_FALSE(p.skyrim->Send(MessageType::CollisionChunk, payload.data(), 1500000));  // 4 MiB
    CHECK_FALSE(p.skyrim->Send(MessageType::CollisionChunk, payload.data(), kRingToSanAndreasCapacity));
    // The ring back is its own: San Andreas's model still fits.
    CHECK(p.sa->Send(MessageType::ModelChunk, payload.data(), 1));
    std::vector<MessageType> atSkyrim;
    CHECK(p.skyrim->Receive([&](MessageType a_type, const std::uint8_t*, std::uint32_t) { atSkyrim.push_back(a_type); }) == 1);
    CHECK(atSkyrim == std::vector<MessageType>{ MessageType::ModelChunk });
    CHECK(p.sa->Receive(ignore) == 2);
    CHECK(p.skyrim->Send(MessageType::CollisionChunk, payload.data(), 1500000));
}

TEST_CASE("the ring back carries a model across the wrap point")
{
    Pair p;
    p.Tick(3);
    std::vector<std::uint8_t> payload(262160);
    std::uint32_t             sent = 0, received = 0;
    for (int round = 0; round < 40; ++round) {
        payload[0] = static_cast<std::uint8_t>(sent);
        payload.back() = static_cast<std::uint8_t>(sent * 3);
        REQUIRE(p.sa->Send(MessageType::ModelChunk, payload.data(), static_cast<std::uint32_t>(payload.size())));
        ++sent;
        p.skyrim->Receive([&](MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
            CHECK(a_type == MessageType::ModelChunk);
            REQUIRE(a_size == 262160);
            CHECK(a_data[0] == static_cast<std::uint8_t>(received));
            CHECK(a_data[a_size - 1] == static_cast<std::uint8_t>(received * 3));
            ++received;
        });
    }
    CHECK(received == sent);
}

TEST_CASE("a restarted Skyrim drops the model chunks sent to the one before")
{
    Pair p;
    p.Tick(3);
    const auto   ignore = [](MessageType, const std::uint8_t*, std::uint32_t) {};
    std::uint8_t byte = 7;
    REQUIRE(p.sa->Send(MessageType::ModelChunk, &byte, 1));
    p.skyrim.reset();
    p.Tick(1);
    p.skyrim = std::make_unique<Link>(Side::Skyrim, p.skyrimLog.Fn(), p.name);
    p.Tick(3);
    REQUIRE(p.skyrim->IsUp());
    CHECK(p.skyrim->Receive(ignore) == 0);
}

TEST_CASE("the body slot carries CJ's bones and camera from San Andreas to Skyrim")
{
    Pair p;
    p.Tick(3);
    BodyState body{};
    body.frame = 9;
    body.flags = kBodyValid;
    body.boneCount = 32;
    body.model = 4;
    body.cameraFov = 70.0f;
    body.bones[31].translation[2] = 120.0f;
    p.sa->WriteBody(body);

    BodyState back{};
    REQUIRE(p.skyrim->ReadBody(back));
    CHECK(back.frame == 9);
    CHECK(back.boneCount == 32);
    CHECK(back.model == 4);
    CHECK(back.cameraFov == 70.0f);
    CHECK(back.bones[31].translation[2] == 120.0f);
}

TEST_CASE("nothing is sent while the link is down, and a restarted San Andreas drops stale chunks")
{
    Pair p;
    p.Tick(1);
    const auto   ignore = [](MessageType, const std::uint8_t*, std::uint32_t) {};
    std::uint8_t byte = 42;
    CHECK_FALSE(p.skyrim->Send(MessageType::CollisionChunk, &byte, 1));

    p.Tick(3);
    REQUIRE(p.skyrim->Send(MessageType::CollisionChunk, &byte, 1));
    p.sa.reset();
    p.Tick(1);
    p.sa = std::make_unique<Link>(Side::SanAndreas, p.saLog.Fn(), p.name);
    p.Tick(3);
    REQUIRE(p.sa->IsUp());
    CHECK(p.sa->Receive(ignore) == 0);
}
