#include <doctest/doctest.h>

#include "../../cpp/hud_names.h"

using namespace AhShitRim::HudNames;

namespace
{
    // Runs a_seconds in steps of a tenth.
    void Run(Names& a_names, float a_seconds, Frame a_frame = {})
    {
        a_frame.delta = 0.1f;
        for (float t = 0.0f; t < a_seconds - 0.001f; t += 0.1f) {
            a_names.Update(a_frame);
        }
    }
}

TEST_CASE("a zone name fades in over a second, stays three and fades out over one")
{
    Names names;
    names.SetZone("Riverwood");
    CHECK(names.Zone().phase == Phase::FadeIn);
    Run(names, 0.5f);
    CHECK(names.Zone().alpha == doctest::Approx(0.5f));
    Run(names, 0.6f);
    CHECK(names.Zone().phase == Phase::Shown);
    CHECK(names.Zone().alpha == doctest::Approx(1.0f));
    Run(names, 1.5f);  // three seconds from coming up, the fade in's second among them, as San Andreas counts
    CHECK(names.Zone().phase == Phase::Shown);
    Run(names, 1.0f);
    CHECK(names.Zone().phase == Phase::FadeOut);
    CHECK(names.Zone().alpha < 1.0f);
    Run(names, 1.2f);
    CHECK(names.Zone().phase == Phase::Hidden);
    CHECK(names.Zone().alpha == 0.0f);
}

TEST_CASE("a new zone while one shows fades the old one out first; the same one stays")
{
    Names names;
    names.SetZone("Riverwood");
    Run(names, 1.5f);
    names.SetZone("Riverwood");
    CHECK(names.Zone().phase == Phase::Shown);
    names.SetZone("Whiterun");
    CHECK(names.Zone().phase == Phase::Switch);
    CHECK(names.Zone().printed == "Riverwood");
    Run(names, 0.5f);
    CHECK(names.Zone().alpha == doctest::Approx(0.5f));
    names.SetZone("Dragonsreach");  // another while switching: that one comes up instead
    Run(names, 0.7f);
    CHECK(names.Zone().phase == Phase::FadeIn);
    CHECK(names.Zone().printed == "Dragonsreach");
    // Once it's gone the same name comes up again on entering it again.
    Run(names, 6.0f);
    CHECK(names.Zone().phase == Phase::Hidden);
    names.SetZone("Dragonsreach");
    CHECK(names.Zone().phase == Phase::FadeIn);
    names.SetZone("");
    CHECK(names.Zone().printed == "Dragonsreach");
}

TEST_CASE("the subtitles and the big texts put the zone name away")
{
    Names names;
    names.SetZone("Riverwood");
    Run(names, 1.5f);
    Frame subtitle;
    subtitle.subtitle = true;
    Run(names, 0.1f, subtitle);
    CHECK(names.Zone().alpha == 0.0f);
    CHECK(names.Zone().phase == Phase::FadeOut);
    Run(names, 1.2f);
    CHECK(names.Zone().phase == Phase::Hidden);

    names.SetZone("Whiterun");
    Run(names, 1.5f);
    Frame big;
    big.bigText = true;
    Run(names, 0.1f, big);
    CHECK(names.Zone().alpha == 0.0f);
    CHECK(names.Zone().phase == Phase::FadeOut);
}

TEST_CASE("the zone name waits in an aircraft and while the screen fades")
{
    Names names;
    names.SetZone("Riverwood");
    Run(names, 0.5f);
    Frame aircraft;
    aircraft.drawZone = false;
    Run(names, 5.0f, aircraft);
    CHECK(names.Zone().phase == Phase::FadeIn);
    CHECK(names.Zone().alpha == 0.0f);
    Frame fading;
    fading.screenFading = true;
    Run(names, 2.0f, fading);
    CHECK(names.Zone().phase == Phase::FadeIn);
    CHECK(names.Zone().alpha == doctest::Approx(0.5f));
}

TEST_CASE("the vehicle name comes up getting in, goes getting out, and the two names take turns")
{
    Names names;
    names.SetZone("Riverwood");
    Run(names, 1.5f);
    names.SetVehicle("Sanchez");
    CHECK(names.Vehicle().phase == Phase::FadeIn);
    CHECK(names.Zone().phase == Phase::FadeOut);
    Run(names, 1.2f);
    CHECK(names.Vehicle().phase == Phase::Shown);
    CHECK(names.Zone().phase == Phase::Hidden);
    names.SetVehicle("Sanchez");  // every frame
    Run(names, 2.0f);
    CHECK(names.Vehicle().phase == Phase::FadeOut);
    Run(names, 1.2f);
    CHECK(names.Vehicle().phase == Phase::Hidden);
    names.SetVehicle("Sanchez");  // still in it: no second time
    CHECK(names.Vehicle().phase == Phase::Hidden);

    names.SetVehicle("");
    names.SetVehicle("BMX");
    CHECK(names.Vehicle().phase == Phase::FadeIn);
    names.SetZone("Whiterun");
    CHECK(names.Vehicle().phase == Phase::FadeOut);
    CHECK(names.Zone().phase == Phase::FadeIn);

    names.SetVehicle("Infernus");  // straight from one into another
    CHECK(names.Vehicle().phase == Phase::Switch);
    Run(names, 1.0f);
    CHECK(names.Vehicle().printed == "Infernus");

    Frame subtitle;
    subtitle.subtitle = true;
    Run(names, 0.1f, subtitle);
    CHECK(names.Vehicle().alpha == 0.0f);

    names.Clear();
    CHECK(names.Zone().phase == Phase::Hidden);
    CHECK(names.Vehicle().phase == Phase::Hidden);
}
