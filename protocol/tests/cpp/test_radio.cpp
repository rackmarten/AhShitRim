#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>

#include "../../cpp/radio.h"

using namespace AhShitRim::Radio;

TEST_CASE("San Andreas's volumes follow Skyrim's sliders, silent while Skyrim has the player")
{
    auto v = FromSliders(0.5f, 0.8f, 0.4f, true);
    CHECK(v.effects == doctest::Approx(0.4f));
    CHECK(v.music == doctest::Approx(0.2f));
    v = FromSliders(2.0f, -1.0f, 1.0f, true);  // out of range clamps
    CHECK(v.effects == 0.0f);
    CHECK(v.music == 1.0f);
    v = FromSliders(1.0f, 1.0f, 1.0f, false);
    CHECK(v.effects == 0.0f);
    CHECK(v.music == 0.0f);
}

TEST_CASE("exploration tracks follow the time of day")
{
    CHECK(ExploreFor(6.0f).data() == kExploreMorning.data());
    CHECK(ExploreFor(12.0f).data() == kExploreDay.data());
    CHECK(ExploreFor(18.5f).data() == kExploreDusk.data());
    CHECK(ExploreFor(23.0f).data() == kExploreNight.data());
    CHECK(ExploreFor(2.0f).data() == kExploreNight.data());
}

TEST_CASE("Radio Sovngarde starts with the main theme, then a bard piece after every two exploration tracks")
{
    Sovngarde s(42);
    s.Tune();
    CHECK(s.Next(false, 12.0f).kind == Kind::MainTheme);
    CHECK(s.Current().path == kMainTheme);
    CHECK(s.Next(false, 12.0f).kind == Kind::Explore);
    CHECK(s.Next(false, 12.0f).kind == Kind::Explore);
    CHECK(s.Next(false, 12.0f).kind == Kind::Bard);
    CHECK(s.Next(false, 12.0f).kind == Kind::Explore);
    CHECK(s.Next(false, 12.0f).kind == Kind::Explore);
    CHECK(s.Next(false, 12.0f).kind == Kind::Bard);
    s.Tune();  // tuning in again starts over
    CHECK(s.Next(false, 12.0f).kind == Kind::MainTheme);
}

TEST_CASE("Radio Sovngarde plays combat tracks in a fight and cuts the wrong mood short")
{
    Sovngarde s(7);
    s.Tune();
    s.Next(false, 12.0f);
    CHECK_FALSE(s.MoodChanged(false));
    CHECK(s.MoodChanged(true));  // a fight during the main theme
    const auto fight = s.Next(true, 12.0f);
    CHECK(fight.kind == Kind::Combat);
    CHECK(std::find(kCombat.begin(), kCombat.end(), fight.path) != kCombat.end());
    CHECK_FALSE(s.MoodChanged(true));
    CHECK(s.MoodChanged(false));  // the fight is over
    CHECK(s.Next(false, 23.0f).kind == Kind::Explore);
    CHECK(std::find(kExploreNight.begin(), kExploreNight.end(), s.Current().path) != kExploreNight.end());
}

TEST_CASE("Radio Sovngarde never plays the same track twice in a row")
{
    Sovngarde s(3);
    s.Tune();
    s.Next(true, 12.0f);
    for (int i = 0; i < 200; ++i) {
        const auto before = s.Current().path;
        CHECK(s.Next(true, 12.0f).path != before);
    }
}

TEST_CASE("nothing plays before tuning in")
{
    Sovngarde s;
    CHECK(s.Current().kind == Kind::None);
    CHECK_FALSE(s.MoodChanged(true));
}

TEST_CASE("the wheel dials through San Andreas's stations, Radio Sovngarde and off, round and round")
{
    CHECK(Dial(1, 1) == 2);
    CHECK(Dial(11, 1) == kSovngarde);
    CHECK(Dial(kSovngarde, 1) == kRadioOff);
    CHECK(Dial(kRadioOff, 1) == 1);
    CHECK(Dial(1, -1) == kRadioOff);
    CHECK(Dial(3, -2) == 1);
    CHECK(Dial(kEmergency, 1) == 1);
    CHECK(Dial(-1, -1) == kRadioOff);
    CHECK(Dial(5, 26) == 5);
}

TEST_CASE("station names and which ones silence Skyrim's music")
{
    CHECK(StationName(6) == "Radio Los Santos");
    CHECK(StationName(kSovngarde) == "Radio Sovngarde");
    CHECK(StationName(kRadioOff) == "Radio Off");
    CHECK(StationName(-1).empty());
    CHECK(StationName(14).empty());
    CHECK(Plays(1));
    CHECK(Plays(kSovngarde));
    CHECK_FALSE(Plays(kRadioOff));
    CHECK_FALSE(Plays(-1));
}
