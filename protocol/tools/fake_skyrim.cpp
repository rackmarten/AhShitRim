// Stands in for the Skyrim plugin, to drive a running San Andreas without Skyrim: links up as the
// Skyrim side, hands the player over, holds W for a few seconds and prints CJ's poses. Built with
// the protocol tests; run it through scripts/fake-skyrim.ps1 while San Andreas runs (launch-sa.ps1).
//
// Usage: fake_skyrim [seconds of W, default 3]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "../cpp/input.h"
#include "../cpp/link.h"
#include "../cpp/pose.h"

using namespace AhShitRim;
using namespace std::chrono;

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
    const auto     publish = [&](bool a_owned) {
        auto s = input.Build(++frame, a_owned, true);
        s.handover = 1;
        s.heading = 0.0f;
        link.WriteInput(s);
    };

    // Hand over and stand for a second, walk forwards, then stand again and hand back.
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
    std::printf("CJ ended %.1f m from the handover spot\n", metres);
    return pose.handover == 1 && metres > 1.0f ? 0 : 2;
}
