#include <doctest/doctest.h>

#include "../../cpp/vehicle_kinds.h"

#include <ostream>
#include <set>
#include <string>

using namespace AhShitRim::VehicleKinds;

TEST_CASE("vehicle kinds: every San Andreas model by id")
{
    CHECK(KindOf(495) == Kind::Car);  // the Sandking
    CHECK(KindOf(432) == Kind::Car);  // the Rhino is an automobile to San Andreas
    CHECK(KindOf(522) == Kind::Bike);  // NRG-500
    CHECK(KindOf(468) == Kind::Bike);  // Sanchez
    CHECK(KindOf(586) == Kind::Bike);  // Wayfarer (its vehicles.ide row has tabs for commas)
    CHECK(KindOf(481) == Kind::Bicycle);  // BMX
    CHECK(KindOf(510) == Kind::Bicycle);  // mountain bike
    CHECK(KindOf(471) == Kind::Quad);
    CHECK(KindOf(444) == Kind::MonsterTruck);
    CHECK(KindOf(406) == Kind::MonsterTruck);  // the Dumper
    CHECK(KindOf(425) == Kind::Heli);  // Hunter
    CHECK(KindOf(548) == Kind::Heli);  // Cargobob
    CHECK(KindOf(520) == Kind::Plane);  // Hydra
    CHECK(KindOf(593) == Kind::Plane);  // Dodo
    CHECK(KindOf(539) == Kind::Plane);  // Vortex: a hovercraft, a plane to San Andreas
    CHECK(KindOf(430) == Kind::Boat);  // Predator
    CHECK(KindOf(595) == Kind::Boat);  // Launch
    CHECK(KindOf(449) == Kind::Train);  // tram
    CHECK(KindOf(537) == Kind::Train);
    CHECK(KindOf(435) == Kind::Trailer);
    CHECK(KindOf(441) == Kind::Remote);  // RC Bandit, a car to San Andreas
    CHECK(KindOf(464) == Kind::Remote);  // RC Baron, a plane
    CHECK(KindOf(465) == Kind::Remote);  // RC Raider, a heli
    CHECK(KindOf(399) == Kind::Unknown);
    CHECK(KindOf(612) == Kind::Unknown);
    CHECK(KindOf(370) == Kind::Unknown);  // the jetpack isn't a vehicle
    CHECK(KindOf(0) == Kind::Unknown);

    // San Andreas 1.0's 212 vehicle models, by vehicles.ide's types (the RC ones apart).
    int counts[12]{};
    for (std::uint16_t id = kFirstModel; id <= kLastModel; id++) {
        counts[static_cast<int>(KindOf(id))]++;
    }
    CHECK(counts[static_cast<int>(Kind::Unknown)] == 0);
    CHECK(counts[static_cast<int>(Kind::Car)] == 144 - 3);  // 144 automobiles, the RC Bandit, Tiger and Cam among them
    CHECK(counts[static_cast<int>(Kind::Heli)] == 9);       // 11 less the RC Raider and Goblin
    CHECK(counts[static_cast<int>(Kind::Plane)] == 12);     // 12 less the RC Baron, plus the Dodo whose row has tabs
    CHECK(counts[static_cast<int>(Kind::Boat)] == 10);
    CHECK(counts[static_cast<int>(Kind::Train)] == 6);
    CHECK(counts[static_cast<int>(Kind::Trailer)] == 9);
    CHECK(counts[static_cast<int>(Kind::Bike)] == 10);
    CHECK(counts[static_cast<int>(Kind::Bicycle)] == 3);
    CHECK(counts[static_cast<int>(Kind::MonsterTruck)] == 5);
    CHECK(counts[static_cast<int>(Kind::Quad)] == 1);
    CHECK(counts[static_cast<int>(Kind::Remote)] == 6);
}

TEST_CASE("vehicle kinds: San Andreas's own type agrees with the id")
{
    CHECK(FromSaType(0, 495) == Kind::Car);
    CHECK(FromSaType(0, 441) == Kind::Remote);  // the RC Bandit is an automobile
    CHECK(FromSaType(1, 444) == Kind::MonsterTruck);
    CHECK(FromSaType(2, 471) == Kind::Quad);
    CHECK(FromSaType(3, 425) == Kind::Heli);
    CHECK(FromSaType(4, 520) == Kind::Plane);
    CHECK(FromSaType(5, 430) == Kind::Boat);
    CHECK(FromSaType(6, 449) == Kind::Train);
    CHECK(FromSaType(7, 465) == Kind::Remote);
    CHECK(FromSaType(8, 464) == Kind::Remote);
    CHECK(FromSaType(9, 522) == Kind::Bike);
    CHECK(FromSaType(10, 481) == Kind::Bicycle);
    CHECK(FromSaType(11, 435) == Kind::Trailer);
    CHECK(FromSaType(-1, 400) == Kind::Unknown);
    CHECK(FromSaType(12, 400) == Kind::Unknown);
}

TEST_CASE("vehicle kinds: the seat table")
{
    // Land and air vehicles spawn; boats, trains, trailers and the RC ones are refused, with a reason.
    for (const Kind kind : { Kind::Car, Kind::MonsterTruck, Kind::Quad, Kind::Bike, Kind::Bicycle, Kind::Heli, Kind::Plane }) {
        const auto t = TraitsOf(kind);
        CHECK(t.spawnable);
        CHECK(t.refusal.empty());
        CHECK(t.seat != Seat::None);
        CHECK(t.seats >= 1);
        CHECK(t.maxUneven >= 0.8f);
    }
    for (const Kind kind : { Kind::Boat, Kind::Train, Kind::Trailer, Kind::Remote, Kind::Unknown }) {
        const auto t = TraitsOf(kind);
        CHECK_FALSE(t.spawnable);
        CHECK_FALSE(t.refusal.empty());
        CHECK(t.seat == Seat::None);
    }
    CHECK(TraitsOf(Kind::Boat).refusal.find("water") != std::string_view::npos);

    // How CJ sits: astride a bike or a quad (seated by its lean), pedalling a bicycle, in a cabin or a cockpit.
    CHECK(TraitsOf(Kind::Bike).seat == Seat::Saddle);
    CHECK(TraitsOf(Kind::Quad).seat == Seat::Saddle);
    CHECK(TraitsOf(Kind::Bicycle).seat == Seat::Pedals);
    CHECK(TraitsOf(Kind::Bicycle).seats == 1);
    CHECK(TraitsOf(Kind::Car).seat == Seat::Cabin);
    CHECK(TraitsOf(Kind::MonsterTruck).seat == Seat::Cabin);
    CHECK(TraitsOf(Kind::Heli).seat == Seat::Cockpit);
    CHECK(TraitsOf(Kind::Plane).seat == Seat::Cockpit);
    CHECK(TraitsOf(Kind::Plane).seats == 1);  // the Hydra, the Hunter: CJ alone
    CHECK(TraitsOf(Kind::Heli).flies);
    CHECK(TraitsOf(Kind::Plane).flies);
    CHECK_FALSE(TraitsOf(Kind::Bike).flies);
    // An aircraft's footprint is wide: it takes more slope than a car's 0.8 m.
    CHECK(TraitsOf(Kind::Heli).maxUneven > TraitsOf(Kind::Car).maxUneven);

    // Guns.
    CHECK(GunsOf(432) == Guns::Cannon);  // Rhino
    CHECK(GunsOf(425) == Guns::Fixed);   // Hunter
    CHECK(GunsOf(520) == Guns::Missiles);  // Hydra
    CHECK(GunsOf(495) == Guns::None);
}

TEST_CASE("vehicle kinds: rotors and propellers")
{
    CHECK(SpinnerOf("static_rotor") == Spinner::Standing);
    CHECK(SpinnerOf("static_rotor2") == Spinner::Standing);
    CHECK(SpinnerOf("moving_rotor") == Spinner::Moving);
    CHECK(SpinnerOf("Moving_Rotor2") == Spinner::Moving);
    CHECK(SpinnerOf("static_prop") == Spinner::Standing);
    CHECK(SpinnerOf("moving_prop2") == Spinner::Moving);
    CHECK(SpinnerOf("chassis") == Spinner::None);
    CHECK(SpinnerOf("") == Spinner::None);
    CHECK(SpinnerOf("moving") == Spinner::None);

    // Standing still or turning slowly the blades show; spinning fast the blurred disc does.
    CHECK(SpinnerShows(Spinner::Standing, 0.0f));
    CHECK_FALSE(SpinnerShows(Spinner::Moving, 0.0f));
    CHECK(SpinnerShows(Spinner::Standing, 0.05f));
    CHECK_FALSE(SpinnerShows(Spinner::Standing, 0.22f));
    CHECK(SpinnerShows(Spinner::Moving, 0.22f));
    CHECK(SpinnerShows(Spinner::Moving, -0.3f));
    CHECK(SpinnerShows(Spinner::None, 0.0f));
    CHECK(SpinnerShows(Spinner::None, 1.0f));
}

TEST_CASE("vehicle kinds: the dev path")
{
    std::set<std::uint16_t> seen;
    for (const auto& pick : kDevPicks) {
        CHECK(seen.insert(pick.model).second);  // each once
        CHECK(Spawnable(pick.model));
        CHECK_FALSE(pick.name.empty());
    }
    // The ones the checklist needs are in it.
    constexpr std::uint16_t kNeeded[] = { 522, 481, 425, 520, 432, 370, 371 };
    for (const std::uint16_t model : kNeeded) {
        CHECK(seen.count(model) == 1);
    }
    CHECK(DevName(520) == "Hydra");
    CHECK(DevName(kJetpack) == "Jetpack");
    CHECK(DevName(495).empty());
    CHECK(IsGear(kJetpack));
    CHECK(IsGear(kParachute));
    CHECK_FALSE(IsGear(520));
    CHECK(Spawnable(495));
    CHECK_FALSE(Spawnable(430));  // a boat
    CHECK_FALSE(Spawnable(1000));
}

TEST_CASE("vehicle kinds: the dev picker and its list")
{
    DevPicker picker;
    CHECK(picker.Count() == std::size(kDevPicks));
    CHECK(picker.Current() == 522);
    CHECK(picker.Next() == 481);
    for (std::size_t i = 0; i < picker.Count(); i++) {
        picker.Next();
    }
    CHECK(picker.Current() == 481);  // round again

    CHECK(ParseModels("520, 425 432,,") == std::vector<std::uint16_t>{ 520, 425, 432 });
    CHECK(ParseModels(" 370;x371 ") == std::vector<std::uint16_t>{ 370, 371 });
    CHECK(ParseModels("99999, 7") == std::vector<std::uint16_t>{ 7 });
    CHECK(ParseModels("").empty());
    picker.SetModels(ParseModels("520,425"));
    CHECK(picker.Count() == 2);
    CHECK(picker.Current() == 520);
    CHECK(picker.Next() == 425);
    picker.SetModels({});  // an empty list keeps what it had
    CHECK(picker.Count() == 2);

    CHECK(Label(520) == "Hydra (520)");
    CHECK(Label(600) == "model 600 (car)");
    CHECK(Label(430) == "model 430 (boat)");
}
