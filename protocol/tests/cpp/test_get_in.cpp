#include <doctest/doctest.h>

#include <vector>

#include "../../cpp/get_in.h"

using namespace AhShitRim::GetIn;

namespace
{
    // CJ's animations on the frame San Andreas put him on a Sanchez (17 November 2026): the riding animation added at
    // nothing to blend in at once, the get-on (at its end) to blend out at once, two partial ones.
    const std::vector<Blend> kOnSanchez{ { 0.0f, 1000.0f }, { 1.0f, -1000.0f }, { 0.0f, -1.0f }, { 1.0f, -1000.0f } };
    // The frame after: riding, nothing waiting.
    const std::vector<Blend> kRiding{ { 0.0f, 0.0f }, { 0.0f, 2.0f }, { 1.0f, 0.0f } };
}

TEST_CASE("get in: one more animation update on the frame he's put in a vehicle, never otherwise")
{
    CHECK(Pending(false, true, kOnSanchez));
    CHECK_FALSE(Pending(true, true, kOnSanchez));   // already in: San Andreas's own update has run
    CHECK_FALSE(Pending(false, false, kOnSanchez)); // still getting in
    CHECK_FALSE(Pending(true, false, kOnSanchez));  // getting out: his body goes through the door as it is
    CHECK_FALSE(Pending(false, true, kRiding));     // put in with nothing waiting (no instant blend)
    CHECK_FALSE(Pending(false, true, std::vector<Blend>{ { 0.5f, 1000.0f } }));  // already blending in
    CHECK_FALSE(Pending(false, true, std::vector<Blend>{ { 0.0f, 4.0f } }));     // a slow blend: San Andreas's own pace
}

TEST_CASE("get in: the extra update is just long enough to finish every instant blend")
{
    CHECK(kStep == doctest::Approx(0.001f));
    auto check = [](const std::vector<Blend>& a_blends) {
        for (const auto& blend : a_blends) {
            if (blend.delta >= kInstantBlend) {
                CHECK(blend.amount + blend.delta * kStep >= 1.0f);  // sitting in full
            } else if (blend.delta <= -kInstantBlend) {
                CHECK(blend.amount + blend.delta * kStep <= 0.0f);  // the get-on gone
            }
        }
    };
    check(kOnSanchez);
    check({ { 0.0f, 2000.0f }, { 1.0f, -1000.0f }, { 1.0f, -4000.0f } });  // a faster one finishes too, the slower ones still do
}
