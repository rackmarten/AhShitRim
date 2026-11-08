#include <doctest/doctest.h>

#include <vector>

#include "../../cpp/car_exit.h"

using namespace AhShitRim;
using namespace AhShitRim::CarExit;

TEST_CASE("car exit: San Andreas's own tasks place CJ while he gets out, the ASI seats him the rest of the time")
{
    CHECK(Reseat(false));
    CHECK_FALSE(Reseat(true));         // the leave-car task: its get-out lines him up with the door
    CHECK_FALSE(Reseat(false, true));  // the bust's drag out of the driver's door
}

namespace
{
    // A get-out measured in San Andreas (fake_vehicles, AHSHITRIM_ONLY=exit, a Sandking's driver's door): his middle's
    // distance out to the left, every other frame, and whether he was still in the vehicle.
    std::vector<Sample> Measured(std::initializer_list<float> a_out, std::size_t a_inFrames)
    {
        std::vector<Sample> out;
        for (const float x : a_out) {
            out.push_back({ { -x, 0.0f, 0.0f }, out.size() < a_inFrames });
        }
        return out;
    }
}

TEST_CASE("car exit: out through the door with San Andreas's door swing is smooth")
{
    // After the fix: out from the seat, past where he ends while pushing the door shut, then stood beside it.
    const auto samples = Measured({ 0.49f, 0.52f, 0.56f, 0.61f, 0.66f, 0.72f, 0.78f, 0.84f, 1.02f, 1.21f, 1.42f, 1.59f, 1.71f, 1.78f, 1.79f, 1.78f, 1.77f,
                                   1.81f, 1.88f, 1.98f, 2.10f, 2.24f, 2.23f, 2.01f, 1.79f, 1.70f, 1.69f, 1.72f, 1.76f, 1.81f, 1.82f, 1.82f, 1.83f },
        30);
    const auto t = Check(samples);
    CHECK(t.maxStep < 0.25f);
    CHECK(t.maxBack < kMaxBack);
    CHECK(t.endOut == doctest::Approx(1.83f));
    CHECK(t.outAtRelease == doctest::Approx(1.82f));
    CHECK(Smooth(t));
}

TEST_CASE("car exit: the animation in the seat, back in the car, then outside is not")
{
    // Before the fix: seated again every frame, the animation swung him across the car and back into the seat,
    // and the leave-car task put him outside at the end.
    const auto samples = Measured({ 0.49f, 0.45f, 0.40f, 0.29f, 0.10f, 0.06f, 0.26f, 0.40f, 0.55f, 0.51f, 0.47f, 0.50f, 0.61f, 0.78f, 0.98f, 0.79f, 0.57f, 0.41f,
                                   0.36f, 0.41f, 0.46f, 0.50f, 2.01f, 2.01f, 2.02f },
        22);
    const auto t = Check(samples);
    CHECK(t.maxStep > 1.0f);   // the teleport
    CHECK(t.maxBack > 0.5f);   // back into the seat
    CHECK_FALSE(Smooth(t));
}

TEST_CASE("car exit: a get-out that never left the seat, or was let go of while still in it, is not smooth")
{
    CHECK_FALSE(Smooth(Check(Measured({ 0.5f, 0.5f, 0.5f, 0.5f }, 2))));
    CHECK_FALSE(Smooth(Check({})));
    // Let go of in the seat, then walked out (a slow slide with no jump): most of the way was after.
    std::vector<float> slow;
    for (int i = 0; i <= 30; i++) {
        slow.push_back(0.5f + 0.05f * i);
    }
    std::vector<Sample> samples;
    for (std::size_t i = 0; i < slow.size(); i++) {
        samples.push_back({ { slow[i], 0.0f, 0.0f }, i < 3 });
    }
    CHECK_FALSE(Smooth(Check(samples)));
}
