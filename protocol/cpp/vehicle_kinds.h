// What kind of vehicle a San Andreas model is, whether AhShitRim can spawn it, how CJ sits in it and what
// it fires (protocol v19). Header-only and free of game types, so the protocol tests cover it; the ASI
// checks it against San Andreas's own model info, and Skyrim's dev keys use it to name what they spawn
// and to refuse what can't come yet without asking San Andreas.
//
// San Andreas 1.0 has vehicle models 400 to 611 (data/vehicles.ide). Most are cars; the others are
// listed here by id, from vehicles.ide's type column, and the radio-controlled ones from their names.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace AhShitRim::VehicleKinds
{
    enum class Kind : std::uint8_t
    {
        Unknown,  // not a vehicle model
        Car,
        MonsterTruck,
        Quad,
        Bike,     // a motorbike (or a moped)
        Bicycle,  // San Andreas's bmx type: the BMX, the bike and the mountain bike
        Heli,
        Plane,    // the Vortex hovercraft and the Skimmer seaplane are planes to San Andreas too
        Boat,
        Train,    // trams too
        Trailer,
        Remote,   // the radio-controlled ones: CJ drives them from outside
    };

    inline constexpr std::uint16_t kFirstModel = 400;
    inline constexpr std::uint16_t kLastModel = 611;

    // The dev path's gear (v19): spawnModel values that aren't vehicles. San Andreas's own models for them.
    inline constexpr std::uint16_t kJetpack = 370;    // CTaskSimpleJetPack, the jetpack cheat's
    inline constexpr std::uint16_t kParachute = 371;  // the parachute weapon (gun_para), the parachute cheat's

    constexpr bool IsGear(std::uint16_t a_model) { return a_model == kJetpack || a_model == kParachute; }

    namespace Ids
    {
        inline constexpr std::uint16_t kMonsterTrucks[] = { 406, 444, 556, 557, 573 };
        inline constexpr std::uint16_t kQuads[] = { 471 };
        inline constexpr std::uint16_t kBikes[] = { 448, 461, 462, 463, 468, 521, 522, 523, 581, 586 };
        inline constexpr std::uint16_t kBicycles[] = { 481, 509, 510 };
        inline constexpr std::uint16_t kHelis[] = { 417, 425, 447, 469, 487, 488, 497, 548, 563 };
        inline constexpr std::uint16_t kPlanes[] = { 460, 476, 511, 512, 513, 519, 520, 539, 553, 577, 592, 593 };
        inline constexpr std::uint16_t kBoats[] = { 430, 446, 452, 453, 454, 472, 473, 484, 493, 595 };
        inline constexpr std::uint16_t kTrains[] = { 449, 537, 538, 569, 570, 590 };
        inline constexpr std::uint16_t kTrailers[] = { 435, 450, 584, 591, 606, 607, 608, 610, 611 };
        inline constexpr std::uint16_t kRemotes[] = { 441, 464, 465, 501, 564, 594 };  // RC Bandit, Baron, Raider, Goblin, Tiger, Cam

        template <std::size_t N>
        constexpr bool In(const std::uint16_t (&a_ids)[N], std::uint16_t a_model)
        {
            return std::find(std::begin(a_ids), std::end(a_ids), a_model) != std::end(a_ids);
        }
    }

    // The kind of San Andreas 1.0 vehicle model a_model.
    constexpr Kind KindOf(std::uint16_t a_model)
    {
        using namespace Ids;
        if (a_model < kFirstModel || a_model > kLastModel) {
            return Kind::Unknown;
        }
        if (In(kRemotes, a_model)) return Kind::Remote;
        if (In(kMonsterTrucks, a_model)) return Kind::MonsterTruck;
        if (In(kQuads, a_model)) return Kind::Quad;
        if (In(kBikes, a_model)) return Kind::Bike;
        if (In(kBicycles, a_model)) return Kind::Bicycle;
        if (In(kHelis, a_model)) return Kind::Heli;
        if (In(kPlanes, a_model)) return Kind::Plane;
        if (In(kBoats, a_model)) return Kind::Boat;
        if (In(kTrains, a_model)) return Kind::Train;
        if (In(kTrailers, a_model)) return Kind::Trailer;
        return Kind::Car;
    }

    // The kind from San Andreas's own model info (CVehicleModelInfo::m_nVehicleType, eVehicleType: 0 automobile, 1
    // monster truck, 2 quad, 3 heli, 4 plane, 5 boat, 6 train, 7 and 8 the radio-controlled heli and plane, 9 bike,
    // 10 bmx, 11 trailer). The RC Bandit, Tiger and Cam are automobiles to San Andreas, so the id says those.
    constexpr Kind FromSaType(int a_type, std::uint16_t a_model)
    {
        if (Ids::In(Ids::kRemotes, a_model)) {
            return Kind::Remote;
        }
        switch (a_type) {
        case 0: return Kind::Car;
        case 1: return Kind::MonsterTruck;
        case 2: return Kind::Quad;
        case 3: return Kind::Heli;
        case 4: return Kind::Plane;
        case 5: return Kind::Boat;
        case 6: return Kind::Train;
        case 7:
        case 8: return Kind::Remote;
        case 9: return Kind::Bike;
        case 10: return Kind::Bicycle;
        case 11: return Kind::Trailer;
        default: return Kind::Unknown;
        }
    }

    constexpr std::string_view NameOf(Kind a_kind)
    {
        switch (a_kind) {
        case Kind::Car: return "car";
        case Kind::MonsterTruck: return "monster truck";
        case Kind::Quad: return "quad";
        case Kind::Bike: return "motorbike";
        case Kind::Bicycle: return "bicycle";
        case Kind::Heli: return "helicopter";
        case Kind::Plane: return "plane";
        case Kind::Boat: return "boat";
        case Kind::Train: return "train";
        case Kind::Trailer: return "trailer";
        case Kind::Remote: return "radio-controlled vehicle";
        default: return "not a vehicle";
        }
    }

    // ---- What a kind can do ----

    // How CJ sits: San Andreas picks his animations (its own rider pose for a bike, a bicycle's pedalling), this
    // says what that means for the rest of AhShitRim.
    enum class Seat : std::uint8_t
    {
        None,     // CJ can't sit in it
        Cabin,    // in a seat behind a door: cars, monster trucks, the Rhino
        Saddle,   // astride, leaning with it: San Andreas seats him by its lean matrix every frame
        Pedals,   // astride a bicycle, pedalling
        Cockpit,  // a helicopter's or a plane's seat
    };

    // What it fires, from San Andreas's own vehicle weapons (CVehicle::ProcessWeapons, CAutomobile::TankControl).
    enum class Guns : std::uint8_t
    {
        None,
        Cannon,    // the Rhino's turret: it follows the camera, the fire key shoots a shell
        Fixed,     // nose guns: the Hunter's (and its rockets), the Seasparrow's, the Rustler's
        Missiles,  // the Hydra's missiles
    };

    struct Traits
    {
        bool             spawnable = false;
        std::string_view refusal;  // why not, for the help box ("Boats need water, which San Andreas doesn't have here yet")
        Seat             seat = Seat::None;
        int              seats = 0;  // CJ's and the passengers', most models of the kind (San Andreas's own say per model)
        bool             flies = false;
        float            maxUneven = 0.8f;  // metres from the highest to the lowest ground under its footprint it may spawn on
    };

    // The seat table: one row per kind.
    constexpr Traits TraitsOf(Kind a_kind)
    {
        switch (a_kind) {
        case Kind::Car: return { true, {}, Seat::Cabin, 4, false, 0.8f };
        case Kind::MonsterTruck: return { true, {}, Seat::Cabin, 2, false, 1.2f };
        case Kind::Quad: return { true, {}, Seat::Saddle, 2, false, 0.8f };
        case Kind::Bike: return { true, {}, Seat::Saddle, 2, false, 0.8f };
        case Kind::Bicycle: return { true, {}, Seat::Pedals, 1, false, 0.8f };
        // A helicopter sits on skids or wheels and a plane on its gear: a wider footprint takes a little more slope.
        case Kind::Heli: return { true, {}, Seat::Cockpit, 2, true, 1.5f };
        case Kind::Plane: return { true, {}, Seat::Cockpit, 1, true, 1.5f };
        case Kind::Boat: return { false, "Boats need water, and San Andreas has none here yet" };
        case Kind::Train: return { false, "Trains need San Andreas's tracks, which aren't in Skyrim" };
        case Kind::Trailer: return { false, "Trailers can't be driven, only towed" };
        case Kind::Remote: return { false, "Radio-controlled vehicles aren't supported yet" };
        default: return { false, "That's not one of San Andreas's vehicles" };
        }
    }

    // The fixed guns and cannons by model: the Rhino (432), the Hunter (425), the Seasparrow (447), the Hydra
    // (520) and the Rustler (476). Everything else has none (drive-bys are CJ's own guns).
    constexpr Guns GunsOf(std::uint16_t a_model)
    {
        switch (a_model) {
        case 432: return Guns::Cannon;
        case 425:
        case 447:
        case 476: return Guns::Fixed;
        case 520: return Guns::Missiles;
        default: return Guns::None;
        }
    }

    // ---- Rotors and propellers ----
    //
    // A helicopter's rotor and a prop plane's propeller come twice in the model: standing (static_rotor,
    // static_prop) and a blurred disc for spinning (moving_rotor, moving_prop). San Andreas draws one or the other by
    // how fast it turns (alpha in CHeli::Render), Skyrim hides the other part (BodyState.hiddenParts). Speeds are
    // San Andreas's: CAutomobile::m_fHeliRotorSpeed, CPlane::m_fPropSpeed, radians a 1/50 s step.

    inline constexpr float kSpinBlur = 0.1f;  // turning faster, the blurred disc shows (the rotor is a blur before it lifts, at 0.22)

    enum class Spinner : std::uint8_t
    {
        None,
        Standing,  // static_rotor, static_rotor2, static_prop, static_prop2
        Moving,    // moving_rotor, moving_rotor2, moving_prop, moving_prop2
    };

    // Which of the two a part's frame name is, by San Andreas's node names (case as in the models, compared without it).
    constexpr Spinner SpinnerOf(std::string_view a_frame)
    {
        const auto starts = [&](std::string_view a_prefix) {
            if (a_frame.size() < a_prefix.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a_prefix.size(); i++) {
                const char c = a_frame[i] >= 'A' && a_frame[i] <= 'Z' ? static_cast<char>(a_frame[i] - 'A' + 'a') : a_frame[i];
                if (c != a_prefix[i]) {
                    return false;
                }
            }
            return true;
        };
        if (starts("static_rotor") || starts("static_prop")) {
            return Spinner::Standing;
        }
        if (starts("moving_rotor") || starts("moving_prop")) {
            return Spinner::Moving;
        }
        return Spinner::None;
    }

    // Whether a part that is a_spinner shows at a_speed.
    constexpr bool SpinnerShows(Spinner a_spinner, float a_speed)
    {
        const bool blurred = a_speed > kSpinBlur || a_speed < -kSpinBlur;
        return a_spinner == Spinner::None || (a_spinner == Spinner::Moving) == blurred;
    }

    // ---- The Rhino's road wheels ----
    //
    // The Rhino's model has six wheel frames, but San Andreas hides the middle pair and draws the front wheel again four
    // times along each side between the front and back ones when it renders the tank (CAutomobile::Render; its eight extra
    // suspension lines are theirs). Skyrim gets those as parts of their own, copies of that side's front wheel, put evenly
    // between the front and back wheels every frame.

    inline constexpr int kRoadWheelsPerSide = 4;

    constexpr bool HasRoadWheels(int a_model) { return a_model == 432; }

    // 0 lf, 1 lb, 2 rf, 3 rb for the wheel frames the road wheels go between (the dummies San Andreas puts the wheel on),
    // -1 for any other frame.
    constexpr int RoadWheelOf(std::string_view a_frame)
    {
        if (a_frame == "wheel_lf_dummy") return 0;
        if (a_frame == "wheel_lb_dummy") return 1;
        if (a_frame == "wheel_rf_dummy") return 2;
        if (a_frame == "wheel_rb_dummy") return 3;
        return -1;
    }

    struct ExtraWheel
    {
        int   part = 0;      // its own part (bone)
        int   front = 0;     // the parts it goes between
        int   back = 0;
        float along = 0.0f;  // how far from front to back, 0..1
    };

    // The road wheels for a model whose wheel frames are the parts a_wheels (lf, lb, rf, rb; -1 for one it lacks), numbered
    // from a_firstPart on: four a side, the left side first; none for a side without both its wheels.
    inline std::vector<ExtraWheel> RoadWheels(const int (&a_wheels)[4], int a_firstPart)
    {
        std::vector<ExtraWheel> out;
        for (int side = 0; side < 2; side++) {
            const int front = a_wheels[side * 2], back = a_wheels[side * 2 + 1];
            if (front < 0 || back < 0) {
                continue;
            }
            for (int k = 1; k <= kRoadWheelsPerSide; k++) {
                out.push_back({ a_firstPart + static_cast<int>(out.size()), front, back, static_cast<float>(k) / (kRoadWheelsPerSide + 1) });
            }
        }
        return out;
    }

    // A point a_along of the way from a_from to a_to.
    template <class V>
    constexpr V Between(const V& a_from, const V& a_to, float a_along)
    {
        return { a_from.x + (a_to.x - a_from.x) * a_along, a_from.y + (a_to.y - a_from.y) * a_along, a_from.z + (a_to.z - a_from.z) * a_along };
    }

    // ---- The dev path ----
    //
    // Skyrim's dev keys pick from these and spawn the pick by model id (a model id from AhShitRim.ini's [Debug]
    // Vehicles list goes the same way). Player-facing spawning is the cheats'.

    struct DevPick
    {
        std::uint16_t    model;
        std::string_view name;
    };

    inline constexpr DevPick kDevPicks[] = {
        { 522, "NRG-500" },
        { 481, "BMX" },
        { 471, "Quad" },
        { 444, "Monster" },
        { 432, "Rhino" },
        { 425, "Hunter" },
        { 520, "Hydra" },
        { 469, "Sparrow" },
        { 487, "Maverick" },
        { 593, "Dodo" },
        { 513, "Stuntplane" },
        { 539, "Vortex" },
        { kJetpack, "Jetpack" },
        { kParachute, "Parachute" },
    };

    // The dev list's name for a_model, or empty for one not in it.
    constexpr std::string_view DevName(std::uint16_t a_model)
    {
        for (const auto& pick : kDevPicks) {
            if (pick.model == a_model) {
                return pick.name;
            }
        }
        return {};
    }

    // Whether a spawn request for a_model goes to San Andreas: a vehicle of a spawnable kind or the gear.
    constexpr bool Spawnable(std::uint16_t a_model)
    {
        return IsGear(a_model) || TraitsOf(KindOf(a_model)).spawnable;
    }

    // What the help box calls a_model: "Hydra (520)", or for one not in the dev list "model 600 (car)".
    inline std::string Label(std::uint16_t a_model)
    {
        if (const auto name = DevName(a_model); !name.empty()) {
            return std::string(name) + " (" + std::to_string(a_model) + ")";
        }
        return "model " + std::to_string(a_model) + " (" + std::string(NameOf(KindOf(a_model))) + ")";
    }

    // Model ids from a list such as AhShitRim.ini's "[Debug] Vehicles=522, 481 425": numbers split by commas or
    // spaces; anything else is skipped, as are ids past 65535.
    inline std::vector<std::uint16_t> ParseModels(std::string_view a_text)
    {
        std::vector<std::uint16_t> out;
        std::uint32_t              value = 0;
        bool                       inNumber = false, tooBig = false;
        const auto                 end = [&] {
            if (inNumber && !tooBig) {
                out.push_back(static_cast<std::uint16_t>(value));
            }
            value = 0;
            inNumber = tooBig = false;
        };
        for (const char c : a_text) {
            if (c >= '0' && c <= '9') {
                value = value * 10 + static_cast<std::uint32_t>(c - '0');
                tooBig = tooBig || value > 65535;
                inNumber = true;
            } else {
                end();
            }
        }
        end();
        return out;
    }

    // The dev keys' pick: ; steps through the list, ' spawns what it is on. Starts on the first.
    class DevPicker
    {
    public:
        DevPicker()
        {
            for (const auto& pick : kDevPicks) {
                _models.push_back(pick.model);
            }
        }

        // An empty list keeps the built-in one.
        void SetModels(std::vector<std::uint16_t> a_models)
        {
            if (!a_models.empty()) {
                _models = std::move(a_models);
                _at = 0;
            }
        }

        std::uint16_t Current() const { return _models[_at]; }

        std::uint16_t Next()
        {
            _at = (_at + 1) % _models.size();
            return Current();
        }

        std::size_t Count() const { return _models.size(); }

    private:
        std::vector<std::uint16_t> _models;
        std::size_t                _at = 0;
    };
}
