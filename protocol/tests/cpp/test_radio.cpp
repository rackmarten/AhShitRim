#include <doctest/doctest.h>

#include <algorithm>
#include <map>
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

TEST_CASE("a sound cue is heard while Skyrim has the player")
{
    auto v = FromSliders(0.5f, 0.8f, 0.4f, false, true);
    CHECK(v.effects == doctest::Approx(0.4f));
    CHECK(v.music == doctest::Approx(0.2f));
    v = FromSliders(0.5f, 0.8f, 0.4f, true, true);  // and as ever while CJ has it
    CHECK(v.effects == doctest::Approx(0.4f));
}

TEST_CASE("Skyrim's stations play as loud as San Andreas's, which follow effects once music is set high")
{
    // San Andreas's rule: 4 dB down, and more with music set far above effects.
    CHECK(SaRadioDb({ 1.0f, 0.2f }) == doctest::Approx(-4.0f));
    CHECK(SaRadioDb({ 0.53f, 0.31f }) == doctest::Approx(-8.34f).epsilon(0.01));
    CHECK(SaRadioDb({ 0.0f, 0.5f }) == doctest::Approx(-4.0f));

    // At the sliders it was measured at, a station plays 6 dB over its music volume.
    CHECK(StationVolume({ 0.53f, 0.31f }) == doctest::Approx(0.31f * 2.0f).epsilon(0.01));

    // With music above effects / 2.8, more music doesn't make it louder, as in San Andreas.
    CHECK(StationVolume({ 0.53f, 0.62f }) == doctest::Approx(StationVolume({ 0.53f, 0.31f })).epsilon(0.001));
    CHECK(StationVolume({ 0.8f, 0.62f }) > StationVolume({ 0.53f, 0.62f }));

    // Below that it follows music, never past full volume, and silent with no music.
    CHECK(StationVolume({ 1.0f, 0.1f }) < StationVolume({ 1.0f, 0.2f }));
    CHECK(StationVolume({ 1.0f, 1.0f }) <= 1.0f);
    CHECK(StationVolume({ 1.0f, 0.0f }) == 0.0f);
    CHECK(StationVolume(FromSliders(1.0f, 1.0f, 1.0f, false)) == 0.0f);
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

TEST_CASE("the wheel dials through San Andreas's stations, Radio Sovngarde, Sweetroll FM and off, round and round")
{
    CHECK(Dial(1, 1) == 2);
    CHECK(Dial(11, 1) == kSovngarde);
    CHECK(Dial(kSovngarde, 1) == kSweetroll);
    CHECK(Dial(kSweetroll, 1) == kRadioOff);
    CHECK(Dial(kRadioOff, 1) == 1);
    CHECK(Dial(1, -1) == kRadioOff);
    CHECK(Dial(kRadioOff, -1) == kSweetroll);
    CHECK(Dial(3, -2) == 1);
    CHECK(Dial(kEmergency, 1) == 1);
    CHECK(Dial(-1, -1) == kRadioOff);
    CHECK(Dial(5, 28) == 5);
    std::int8_t at = 1;
    for (int i = 0; i < 14; ++i) {
        at = Dial(at, 1);
    }
    CHECK(at == 1);  // 14 stops round
}

TEST_CASE("San Andreas's radio is off on the stations Skyrim plays and on off")
{
    CHECK(SaStation(1) == 1);
    CHECK(SaStation(11) == 11);
    CHECK(SaStation(kSovngarde) == kSaRadioOff);
    CHECK(SaStation(kSweetroll) == kSaRadioOff);
    CHECK(SaStation(kRadioOff) == kSaRadioOff);
    CHECK(SkyrimPlays(kSovngarde));
    CHECK(SkyrimPlays(kSweetroll));
    CHECK_FALSE(SkyrimPlays(11));
    CHECK_FALSE(SkyrimPlays(kRadioOff));
}

TEST_CASE("bicycles have no radio; cars, bikes and emergency vehicles do")
{
    using AhShitRim::VehicleKinds::Kind;
    using AhShitRim::VehicleKinds::KindOf;
    for (const std::uint16_t bicycle : { 481, 509, 510 }) {  // BMX, Bike, Mountain Bike
        CHECK_FALSE(HasRadio(KindOf(bicycle), kSaRadioCivilian));
    }
    CHECK(HasRadio(KindOf(477), kSaRadioCivilian));   // ZR-350
    CHECK(HasRadio(KindOf(495), kSaRadioCivilian));   // Sandking
    CHECK(HasRadio(KindOf(522), kSaRadioCivilian));   // NRG-500
    CHECK(HasRadio(KindOf(596), kSaRadioEmergency));  // Police LS: Emergency AA, tunable
    CHECK_FALSE(HasRadio(KindOf(477), -1));           // San Andreas gives it none
    CHECK_FALSE(HasRadio(KindOf(370), kSaRadioCivilian));  // the jetpack is no vehicle
    CHECK_FALSE(HasRadio(KindOf(371), kSaRadioCivilian));  // nor the parachute
}

TEST_CASE("the last station carries over into every vehicle, off and Skyrim's stations included")
{
    CHECK(StartStation(-1, 6) == 6);                    // nothing listened to yet (kRadioNone): the vehicle's own
    CHECK(StartStation(kEmergency, 6) == 6);            // Emergency AA (a police car's own) never carries over
    CHECK(StartStation(2, 6) == 2);                     // K-Rose in the last car, K-Rose in this one
    CHECK(StartStation(kSovngarde, 6) == kSovngarde);   // San Andreas's radio starts off, Skyrim plays it
    CHECK(SaStation(StartStation(kSovngarde, 6)) == kSaRadioOff);
    CHECK(StartStation(kSweetroll, 0) == kSweetroll);   // in a police car too
    CHECK(StartStation(kRadioOff, 6) == kRadioOff);     // turned off stays off
    CHECK(SaStation(StartStation(kRadioOff, 6)) == kSaRadioOff);
}

TEST_CASE("getting in, San Andreas's stale station id never replaces the dial")
{
    // 0.1.0's carry-over: Radio X's 7 read before the radio ran became the dial, in every vehicle
    CHECK(Follow(kSovngarde, 7, false).station == kSovngarde);
    CHECK(Follow(kSovngarde, 7, false).dial == kSovngarde);
    CHECK(Follow(2, 7, false).dial == 2);
    CHECK(Follow(kRadioOff, 7, false).station == kRadioOff);
    CHECK(Follow(-1, 7, false).station == -1);  // nothing listened to yet: no name until San Andreas's own comes on
    CHECK(Follow(-1, 7, false).dial == -1);
    // running
    CHECK(Follow(-1, 3, true).station == 3);  // the first vehicle's own station
    CHECK(Follow(-1, 3, true).dial == 3);
    CHECK(Follow(2, 2, true).station == 2);
    CHECK(Follow(kSovngarde, kSaRadioOff, true).station == kSovngarde);  // San Andreas off, Skyrim plays it
    CHECK(Follow(kSweetroll, kSaRadioOff, true).dial == kSweetroll);
    CHECK(Follow(2, kSaRadioOff, true).station == kRadioOff);  // turned off
    CHECK(Follow(-1, kSaRadioOff, true).dial == kRadioOff);
    CHECK(Follow(kRadioOff, kEmergency, true).station == kEmergency);  // San Andreas's own choice still shows
}

TEST_CASE("getting in, the carried-over station is retuned to once if San Andreas's radio comes on elsewhere")
{
    Boarding b;
    b.Start(2, 1000);  // K-Rose carried over
    CHECK(b.Pending());
    CHECK(b.Step(false, 8, 1030) == -1);  // not running yet, a stale id
    CHECK(b.Pending());
    CHECK(b.Step(true, 2, 1100) == -1);  // came on on K-Rose: nothing to do
    CHECK_FALSE(b.Pending());

    b.Start(2, 2000);
    CHECK(b.Step(true, kEmergency, 2100) == 2);  // a police car's Emergency AA: retuned once
    CHECK_FALSE(b.Pending());
    CHECK(b.Step(true, kEmergency, 2200) == -1);

    b.Start(kSovngarde, 3000);
    CHECK(b.Step(true, 5, 3100) == kSaRadioOff);  // San Andreas's own retune to its last station: off, Skyrim plays
    b.Start(kSovngarde, 4000);
    CHECK(b.Step(true, kSaRadioOff, 4100) == -1);  // off already

    b.Start(-1, 5000);  // nothing carried over: the vehicle's own
    CHECK_FALSE(b.Pending());
    CHECK(b.Step(true, 5, 5100) == -1);

    b.Start(2, 6000);
    CHECK(b.Step(false, 8, 6000 + kBoardingMs + 1) == -1);  // never came on: given up
    CHECK_FALSE(b.Pending());
}

TEST_CASE("station names and which ones silence Skyrim's music")
{
    CHECK(StationName(6) == "Radio Los Santos");
    CHECK(StationName(kSovngarde) == "Radio Sovngarde");
    CHECK(StationName(kSweetroll) == "Sweetroll FM");
    CHECK(StationName(kRadioOff) == "Radio Off");
    CHECK(StationName(13) == "Sweetroll FM");
    CHECK(StationName(14) == "Radio Off");
    CHECK(StationName(-1).empty());
    CHECK(StationName(15).empty());
    CHECK(Plays(1));
    CHECK(Plays(kSovngarde));
    CHECK(Plays(13));
    CHECK_FALSE(Plays(14));
    CHECK_FALSE(Plays(kRadioOff));
    CHECK_FALSE(Plays(-1));
}

TEST_CASE("Sweetroll FM plays every track once before any repeats, never one twice in a row")
{
    Shuffle s(kSweetrollTracks, 5);
    s.Tune();
    std::string_view last;
    for (int round = 0; round < 50; ++round) {
        std::map<std::string_view, int> played;
        for (std::size_t i = 0; i < kSweetrollTracks.size(); ++i) {
            const auto track = s.Next();
            CHECK(track != last);
            CHECK(std::find(kSweetrollTracks.begin(), kSweetrollTracks.end(), track) != kSweetrollTracks.end());
            played[track]++;
            last = track;
        }
        CHECK(played.size() == kSweetrollTracks.size());
    }
}

TEST_CASE("Sweetroll FM starts on a random track when tuned in")
{
    std::map<std::string_view, int> firsts;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        Shuffle s(kSweetrollTracks, seed * 7919u);
        s.Tune();
        firsts[s.Next()]++;
    }
    CHECK(firsts.size() > 3);
}

TEST_CASE("Sweetroll FM with two tracks alternates, with one repeats it")
{
    constexpr std::string_view two[] = { "a.xwm", "b.xwm" };
    Shuffle s(two, 9);
    auto last = s.Next();
    for (int i = 0; i < 20; ++i) {
        const auto track = s.Next();
        CHECK(track != last);
        last = track;
    }
    constexpr std::string_view one[] = { "a.xwm" };
    s.SetTracks(one);
    CHECK(s.Next() == "a.xwm");
    CHECK(s.Next() == "a.xwm");
}

TEST_CASE("Sweetroll FM with no tracks installed plays nothing")
{
    Shuffle s;
    CHECK(s.Size() == 0);
    s.Tune();
    CHECK(s.Next().empty());
    CHECK(s.Current().empty());
    s.SetTracks(kSweetrollTracks);
    CHECK_FALSE(s.Next().empty());
    s.SetTracks({});
    CHECK(s.Next().empty());
}

TEST_CASE("Sweetroll FM's tracks are under Music\\AhShitRim\\SweetrollFM as .xwm")
{
    for (const auto track : kSweetrollTracks) {
        CHECK(track.starts_with("music\\ahshitrim\\sweetrollfm\\"));
        CHECK(track.ends_with(".xwm"));
    }
}
