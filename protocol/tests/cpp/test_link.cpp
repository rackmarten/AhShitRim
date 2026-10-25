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

namespace
{
    constexpr std::uint32_t kOldVersion = 8;
    constexpr std::uint32_t kDeadPid = 0xFFFFFFF0;  // no process has it

    std::size_t Count(const Logged& a_log, std::string_view a_needle)
    {
        std::size_t n = 0;
        for (const auto& line : a_log.lines) {
            n += line.find(a_needle) != std::string::npos;
        }
        return n;
    }

    Side OtherSide(Side a_side)
    {
        return a_side == Side::Skyrim ? Side::SanAndreas : Side::Skyrim;
    }

    // An old plugin of the other half, as it sits in a section: its PeerInfo, and nothing it ever ticks.
    void PublishOld(RawSection& a_raw, Side a_side, std::uint32_t a_pid = ::GetCurrentProcessId())
    {
        PeerInfo& old = a_raw.Header()->peers[static_cast<int>(a_side)];
        old.pid = a_pid;
        old.version = kOldVersion;
    }

    // A section an old plugin created: its header, its size and its version.
    void CreateOld(RawSection& a_raw)
    {
        a_raw.Header()->headerSize = sizeof(LinkHeader);
        a_raw.Header()->mappingSize = kMappingSize;
        a_raw.Header()->creatorVersion = kOldVersion;
        a_raw.Header()->magic = kMagic;
    }
}

TEST_CASE("an old peer is reported, and the link comes up when ours replaces it, whichever half started first")
{
    for (const Side first : { Side::Skyrim, Side::SanAndreas }) {
        CAPTURE(static_cast<int>(first));
        const Side         second = OtherSide(first);
        const std::wstring name = UniqueName();
        Logged             log, peerLog;
        Link               link(first, log.Fn(), name);
        std::uint64_t      now = 1000;
        link.Tick(now);
        REQUIRE(log.Has("created shared memory"));

        // The old plugin of the other half joins; it never ticks, so this half watches it.
        auto old = std::make_unique<RawSection>(name);
        PublishOld(*old, second);
        for (int i = 0; i < 100; ++i) {  // 5 s
            link.Tick(now += 50);
        }
        CHECK(link.GetState() == Link::State::Mismatch);
        CHECK(link.MismatchVersion() == kOldVersion);
        CHECK(log.Has(std::string("protocol mismatch: the ") + (second == Side::Skyrim ? "Skyrim plugin" : "San Andreas plugin") +
                      " speaks v8, this " + (first == Side::Skyrim ? "Skyrim plugin" : "San Andreas plugin") +
                      " speaks v" + std::to_string(kProtocolVersion)));
        CHECK(log.Has("Install the same version in both"));
        CHECK(Count(log, "protocol mismatch") == 1);  // rate-limited

        // A minute on it's said again.
        for (int i = 0; i < 1200; ++i) {
            link.Tick(now += 50);
        }
        CHECK(Count(log, "protocol mismatch") == 2);

        // The old one quits cleanly; ours then starts.
        old->Header()->peers[static_cast<int>(second)] = PeerInfo{};
        link.Tick(now += 50);
        CHECK(link.GetState() == Link::State::Waiting);
        CHECK(link.MismatchVersion() == 0);
        CHECK(log.Has("of another version is gone"));
        old.reset();

        Link peer(second, peerLog.Fn(), name);
        for (int i = 0; i < 3; ++i) {
            now += 50;
            link.Tick(now);
            peer.Tick(now);
        }
        CHECK(link.IsUp());
        CHECK(peer.IsUp());
        CHECK(link.MismatchVersion() == 0);
        CHECK(peerLog.Has("joined shared memory"));
    }
}

TEST_CASE("an old peer that crashed without detaching isn't reported")
{
    const std::wstring name = UniqueName();
    Logged             log;
    Link               sa(Side::SanAndreas, log.Fn(), name);
    std::uint64_t      now = 1000;
    sa.Tick(now);
    RawSection raw(name);
    PublishOld(raw, Side::Skyrim, kDeadPid);
    for (int i = 0; i < 20; ++i) {
        sa.Tick(now += 50);
    }
    CHECK(sa.GetState() == Link::State::Waiting);
    CHECK(sa.MismatchVersion() == 0);
    CHECK_FALSE(log.Has("protocol mismatch"));
}

TEST_CASE("an up link that meets another version goes down, and comes back with ours")
{
    Pair p;
    p.Tick(3);
    REQUIRE(p.skyrim->IsUp());

    // San Andreas is replaced by an old one that took over its PeerInfo.
    p.sa.reset();
    {
        RawSection raw(p.name);
        PublishOld(raw, Side::SanAndreas);
        p.Tick(1);
        CHECK(p.skyrim->GetState() == Link::State::Mismatch);
        CHECK(p.skyrimLog.Has("protocol mismatch: the San Andreas plugin speaks v8, this Skyrim plugin speaks v" + std::to_string(kProtocolVersion)));
        CHECK_FALSE(p.skyrim->Send(MessageType::ModelChunk, "x", 1));

        // Ours replaces it without a clean detach of the old one.
        p.sa = std::make_unique<Link>(Side::SanAndreas, p.saLog.Fn(), p.name);
    }
    p.Tick(4);
    CHECK(p.skyrim->IsUp());
    CHECK(p.sa->IsUp());
    CHECK(p.skyrimLog.Has("the San Andreas plugin now speaks v" + std::to_string(kProtocolVersion) + " too"));
}

TEST_CASE("a section an old plugin created is let go of, and ours is created once it's gone, whichever half joins")
{
    for (const Side side : { Side::Skyrim, Side::SanAndreas }) {
        CAPTURE(static_cast<int>(side));
        const std::wstring name = UniqueName();
        auto               old = std::make_unique<RawSection>(name);
        CreateOld(*old);
        PublishOld(*old, OtherSide(side));

        Logged        log, peerLog;
        Link          link(side, log.Fn(), name);
        std::uint64_t now = 1000;
        for (int i = 0; i < 100; ++i) {  // 5 s: let go of and opened again every second
            link.Tick(now += 50);
        }
        CHECK(link.GetState() == Link::State::Mismatch);
        CHECK(link.MismatchVersion() == kOldVersion);
        CHECK(Count(log, "protocol mismatch") == 1);
        CHECK_FALSE(log.Has("joined shared memory"));
        // Our version is published, so the old one can say so too.
        CHECK(old->Header()->peers[static_cast<int>(side)].version == kProtocolVersion);

        // The old plugin quits; we hold nothing, so its section goes with it.
        old.reset();
        for (int i = 0; i < 21; ++i) {
            link.Tick(now += 50);
        }
        CHECK(log.Has("created shared memory"));
        CHECK(link.GetState() == Link::State::Waiting);
        CHECK(link.MismatchVersion() == 0);

        Link peer(OtherSide(side), peerLog.Fn(), name);
        for (int i = 0; i < 3; ++i) {
            now += 50;
            link.Tick(now);
            peer.Tick(now);
        }
        CHECK(link.IsUp());
        CHECK(peer.IsUp());
    }
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

TEST_CASE("a section whose creator quit halfway is let go of and made again")
{
    const std::wstring name = UniqueName();
    auto               raw = std::make_unique<RawSection>(name);  // magic stays 0

    Logged        log;
    Link          skyrim(Side::Skyrim, log.Fn(), name);
    std::uint64_t now = 1000;
    skyrim.Tick(now);
    raw.reset();  // the creator is gone; only we hold it now
    for (int i = 0; i < 40; ++i) {
        skyrim.Tick(now += 50);
    }
    CHECK(log.Has("never set up"));
    for (int i = 0; i < 21; ++i) {
        skyrim.Tick(now += 50);
    }
    CHECK(log.Has("created shared memory"));
    CHECK(skyrim.GetState() == Link::State::Waiting);
}

TEST_CASE("a foreign section is let go of, and ours is made once it's gone")
{
    const std::wstring name = UniqueName();
    auto               raw = std::make_unique<RawSection>(name);
    raw->Header()->magic = 0xDEADBEEF;

    Logged        log;
    Link          skyrim(Side::Skyrim, log.Fn(), name);
    std::uint64_t now = 1000;
    for (int i = 0; i < 40; ++i) {
        skyrim.Tick(now += 50);
    }
    CHECK(skyrim.GetState() == Link::State::Detached);
    CHECK(Count(log, "not an AhShitRim link") == 1);

    raw.reset();
    for (int i = 0; i < 21; ++i) {
        skyrim.Tick(now += 50);
    }
    CHECK(log.Has("created shared memory"));
    CHECK(skyrim.GetState() == Link::State::Waiting);
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
    CHECK(back.radioStation == 0);

    AudioState audio{};
    audio.effects = 0.5f;
    audio.music = 0.25f;
    p.skyrim->WriteAudio(audio);
    AudioState heard{};
    REQUIRE(p.sa->ReadAudio(heard));
    CHECK(heard.effects == 0.5f);
    CHECK(heard.music == 0.25f);
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

TEST_CASE("each vehicle slot carries its own vehicle's parts, apart from CJ's body")
{
    Pair p;
    p.Tick(3);
    BodyState car{};
    car.frame = 5;
    car.flags = kBodyValid | kBodyDriven;
    car.boneCount = 9;
    car.model = 77;
    car.bones[8].translation[0] = -80.0f;
    p.sa->WriteVehicle(2, car);
    p.sa->WriteVehicle(kVehicleMax, car);  // ignored

    BodyState back{};
    REQUIRE(p.skyrim->ReadVehicle(2, back));
    CHECK(back.model == 77);
    CHECK(back.flags == (kBodyValid | kBodyDriven));
    CHECK(back.bones[8].translation[0] == -80.0f);
    CHECK_FALSE(p.skyrim->ReadVehicle(1, back));  // never written
    CHECK_FALSE(p.skyrim->ReadVehicle(kVehicleMax, back));
    CHECK_FALSE(p.skyrim->ReadBody(back));
}

TEST_CASE("each hand has a weapon slot of its own, apart from the body and the vehicles")
{
    Pair p;
    p.Tick(3);
    BodyState gun{};
    gun.frame = 12;
    gun.flags = kBodyValid;
    gun.boneCount = 2;
    gun.model = 99;
    gun.bones[1].translation[2] = 33.0f;
    p.sa->WriteWeapon(1, gun);
    p.sa->WriteWeapon(kWeaponHandMax, gun);  // ignored

    BodyState back{};
    REQUIRE(p.skyrim->ReadWeapon(1, back));
    CHECK(back.model == 99);
    CHECK(back.bones[1].translation[2] == 33.0f);
    CHECK_FALSE(p.skyrim->ReadWeapon(0, back));  // never written
    CHECK_FALSE(p.skyrim->ReadWeapon(kWeaponHandMax, back));
    CHECK_FALSE(p.skyrim->ReadBody(back));
    CHECK_FALSE(p.skyrim->ReadVehicle(1, back));
}

TEST_CASE("the proxy slot carries Skyrim's actors to San Andreas, and a vehicle slot its seats back")
{
    Pair p;
    p.Tick(3);
    ProxyState proxies{};
    proxies.frame = 40;
    proxies.handover = 2;
    proxies.count = 2;
    proxies.proxies[1].id = 0x0001A2B3;
    proxies.proxies[1].flags = kProxyHeavy;
    proxies.proxies[1].b[2] = 300.0f;
    proxies.proxies[1].mass = 6000.0f;
    p.skyrim->WriteProxies(proxies);
    p.sa->WriteProxies(proxies);  // San Andreas never writes it, but nothing stops it either: what Skyrim wrote last is read

    ProxyState got{};
    REQUIRE(p.sa->ReadProxies(got));
    CHECK(got.count == 2);
    CHECK(got.proxies[1].id == 0x0001A2B3);
    CHECK(got.proxies[1].b[2] == 300.0f);
    CHECK(got.proxies[1].mass == 6000.0f);

    BodyState car{};
    car.flags = kBodyValid | kBodyDriven;
    car.boneCount = 17;
    car.seatCount = 3;
    car.seats[2].translation[0] = 35.0f;
    p.sa->WriteVehicle(0, car);
    BodyState back{};
    REQUIRE(p.skyrim->ReadVehicle(0, back));
    CHECK(back.seatCount == 3);
    CHECK(back.seats[2].translation[0] == 35.0f);
}

TEST_CASE("an impact goes to Skyrim and arrives whole")
{
    Pair p;
    p.Tick(3);
    ImpactEvent impact{};
    impact.handover = 4;
    impact.id = 0x000A1B2C;
    impact.speed = 18.5f;
    impact.flags = kImpactBike;
    impact.vehicleModel = 468;
    impact.push[1] = 900.0f;
    impact.damage = 220.0f;
    REQUIRE(p.sa->Send(MessageType::Impact, &impact, sizeof(impact)));

    int seen = 0;
    p.skyrim->Receive([&](MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
        REQUIRE(a_type == MessageType::Impact);
        REQUIRE(a_size == sizeof(ImpactEvent));
        ImpactEvent got{};
        std::memcpy(&got, a_data, sizeof(got));
        CHECK(got.id == 0x000A1B2C);
        CHECK(got.speed == 18.5f);
        CHECK(got.vehicleModel == 468);
        CHECK(got.push[1] == 900.0f);
        CHECK(got.damage == 220.0f);
        seen++;
    });
    CHECK(seen == 1);
}

TEST_CASE("a weapon command goes to San Andreas and arrives whole")
{
    Pair p;
    p.Tick(3);
    WeaponCommand command{};
    command.kind = static_cast<std::uint32_t>(WeaponCommandKind::SetLoadout);
    command.argument = 2;
    command.entries[2] = { 22, 0, 17, 117 };
    command.entries[12] = { 40, 0, 1, 1 };
    REQUIRE(p.skyrim->Send(MessageType::WeaponCommand, &command, sizeof(command)));

    int seen = 0;
    p.sa->Receive([&](MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
        REQUIRE(a_type == MessageType::WeaponCommand);
        REQUIRE(a_size == sizeof(WeaponCommand));
        WeaponCommand got{};
        std::memcpy(&got, a_data, sizeof(got));
        CHECK(got.kind == static_cast<std::uint32_t>(WeaponCommandKind::SetLoadout));
        CHECK(got.argument == 2);
        CHECK(got.entries[2].ammo == 117);
        CHECK(got.entries[12].type == 40);
        seen++;
    });
    CHECK(seen == 1);
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
