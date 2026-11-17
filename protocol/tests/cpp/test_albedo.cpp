#include <doctest/doctest.h>

#include "../../cpp/albedo.h"

using namespace AhShitRim::Albedo;

TEST_CASE("albedo: San Andreas's white is brought into Skyrim's range, darker colours kept")
{
    const Curve curve;
    // Full white, a light grey, the knee's grey, a dark paint, a saturated red, a bright yellow.
    std::uint8_t pixels[] = { 255, 255, 255, 200, 230, 230, 230, 255, 127, 127, 127, 255, 40, 50, 60, 255,
        220, 10, 10, 255, 255, 230, 0, 255 };
    ToSkyrim(pixels, 6, curve);

    CHECK(pixels[0] == pixels[1]);  // white stays grey, no tint
    CHECK(pixels[0] == pixels[2]);
    CHECK(pixels[0] == doctest::Approx(curve.white * 255.0f).epsilon(0.01));
    CHECK(pixels[3] == 200);  // alpha kept

    CHECK(pixels[4] < 230);  // a light grey brought down, less than white
    CHECK(pixels[4] > pixels[0] - 20);
    CHECK(pixels[4] < pixels[0]);

    CHECK(pixels[8] == 127);  // at the knee: unchanged
    CHECK(pixels[12] == 40);  // dark paint unchanged
    CHECK(pixels[13] == 50);
    CHECK(pixels[14] == 60);
    CHECK(pixels[16] == 220);  // a saturated red is dark by luma: unchanged
    CHECK(pixels[17] == 10);

    // A bright yellow keeps its hue: both channels scaled alike, blue still none.
    CHECK(pixels[20] < 255);
    CHECK(static_cast<float>(pixels[21]) / pixels[20] == doctest::Approx(230.0f / 255.0f).epsilon(0.02));
    CHECK(pixels[22] == 0);
}

TEST_CASE("albedo: the roll-off is continuous and keeps the order of brightness")
{
    const Curve curve;
    CHECK(Rolled(curve.knee, curve) == doctest::Approx(curve.knee));
    CHECK(Rolled(1.0f, curve) == doctest::Approx(curve.white));
    float last = 0.0f;
    for (int i = 0; i <= 100; i++) {
        const float rolled = Rolled(i / 100.0f, curve);
        CHECK(rolled >= last);
        CHECK(rolled <= i / 100.0f + 1e-6f);  // never brighter
        last = rolled;
    }
    // A curve whose knee is at full leaves everything as it is.
    std::uint8_t white[] = { 255, 255, 255, 255 };
    ToSkyrim(white, 1, Curve{ 1.0f, 0.5f });
    CHECK(white[0] == 255);
}
