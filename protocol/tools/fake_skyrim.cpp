// Stands in for the Skyrim plugin, to drive a running San Andreas without Skyrim: links up as the
// Skyrim side, hands the player over twice, sends flat ground (7 x 7 collision chunks) around the
// handover spot, holds W for a few seconds and prints CJ's poses. Built with the protocol tests;
// run it through scripts/fake-skyrim.ps1 while San Andreas runs (launch-sa.ps1).
//
// Usage: fake_skyrim [seconds of W, default 3]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "../cpp/collision.h"
#include "../cpp/input.h"
#include "../cpp/link.h"
#include "../cpp/pose.h"

using namespace AhShitRim;
using namespace std::chrono;

// Somewhere in Skyrim's Tamriel, away from the chunk grid's lines.
constexpr float kAnchor[3] = { 10000.0f, -20000.0f, 3000.0f };

int main(int argc, char** argv)
{
    const double walkSeconds = argc > 1 ? std::atof(argv[1]) : 3.0;
    Link link(Protocol::Side::Skyrim, [](std::string_view a_line) { std::printf("link: %.*s\n", static_cast<int>(a_line.size()), a_line.data()); });

    const auto start = steady_clock::now();
    const auto ms = [&] { return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now() - start).count()); };
    while (!link.IsUp()) {
        if (ms() > 10000) {
            std::printf("no San Andreas plugin on the link after 10 s (is San Andreas running?)\n");
            return 1;
        }
        link.Tick(ms());
        std::this_thread::sleep_for(50ms);
    }

    Input::Builder input;
    std::uint32_t  frame = 0;
    std::uint32_t  handoverId = 0;
    float          anchor[3] = { kAnchor[0], kAnchor[1], kAnchor[2] };
    const auto     publish = [&](bool a_owned) {
        auto s = input.Build(++frame, a_owned, true);
        s.handover = handoverId;
        s.heading = 0.0f;
        s.anchor[0] = anchor[0];
        s.anchor[1] = anchor[1];
        s.anchor[2] = anchor[2];
        link.WriteInput(s);
    };

    // Two handovers, the second elsewhere and higher up: San Andreas drops the first one's ground
    // while CJ still stands on it (that once crashed it).
    int failed = 0;
    for (int round = 0; round < 2; ++round) {
        handoverId = static_cast<std::uint32_t>(round + 1);
        anchor[1] = kAnchor[1] + round * 4000.0f;
        anchor[2] = kAnchor[2] + round * 700.0f;

        // Hand over, and send flat ground at the handover spot's height around it.
        publish(true);
        const auto cx = Collision::ChunkIndex(anchor[0]), cy = Collision::ChunkIndex(anchor[1]);
        int        chunks = 0;
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                Collision::Heightfield f;
                f.Reset(cx + dx, cy + dy, 32);
                std::fill(f.heights.begin(), f.heights.end(), anchor[2]);
                std::fill(f.hit.begin(), f.hit.end(), std::uint8_t{ 1 });
                const auto payload = Collision::BuildChunkPayload(f, 0x3C, handoverId);
                chunks += link.Send(Protocol::MessageType::CollisionChunk, payload.data(), static_cast<std::uint32_t>(payload.size()));
            }
        }
        std::printf("handover %u: sent %d flat collision chunks\n", handoverId, chunks);

        // Stand for a second, walk forwards, then stand again and hand back.
        const std::uint64_t handover = ms();
        Protocol::PlayerState pose{};
        std::uint64_t         lastPrint = 0;
        bool                  walking = false;
        while (ms() - handover < static_cast<std::uint64_t>((walkSeconds + 2.0) * 1000)) {
            const double t = (ms() - handover) / 1000.0;
            const bool   walk = t >= 1.0 && t < 1.0 + walkSeconds;
            if (walk != walking) {
                walking = walk;
                input.SetKey(Input::kDikW, walk);
                std::printf("%.1f s: %s W\n", t, walk ? "holding" : "letting go of");
            }
            publish(true);
            link.Tick(ms());
            if (ms() - lastPrint >= 500 && link.ReadPlayer(pose)) {
                lastPrint = ms();
                std::printf("%.1f s: pose frame %u handover %u%s offset (%.0f, %.0f, %.0f) units, speed %.1f m/s, heading %.2f, view %.2f/%.2f\n",
                    t, pose.frame, pose.handover, (pose.flags & Protocol::kPlayerGrounded) ? " grounded" : " in the air",
                    pose.offset[0], pose.offset[1], pose.offset[2],
                    std::hypot(pose.velocity[0], pose.velocity[1]) * Pose::kMetersPerUnit, pose.heading, pose.viewHeading, pose.viewPitch);
            }
            std::this_thread::sleep_for(16ms);
        }
        publish(false);
        link.Tick(ms());
        const float metres = std::hypot(pose.offset[0], pose.offset[1]) * Pose::kMetersPerUnit;
        const float height = pose.offset[2] * Pose::kMetersPerUnit;
        std::printf("handover %u: CJ ended %.1f m from the handover spot, %.2f m above it%s\n", handoverId, metres, height,
            (pose.flags & Protocol::kPlayerGrounded) ? ", standing" : ", in the air");
        failed += !(pose.handover == handoverId && metres > 1.0f && std::abs(height) < 0.5f && link.IsUp());
    }
    return failed ? 2 : 0;
}
