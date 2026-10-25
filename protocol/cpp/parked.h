// San Andreas's parked vehicles in Skyrim's world (v36), the game-free half: the spots they wait at, when San Andreas
// puts one out and takes it away again, and when a taken or wrecked one comes back, as San Andreas's parked car
// generators (CCarGenerator) do. Header-only and free of game types, so the protocol tests cover it; San Andreas's
// half is in sa/src/vehicles.cpp (UpdateParked), which makes them as mission vehicles and parks them with the spot
// rule of protocol/cpp/vehicle.h.
//
// The spots are Skyrim world coordinates (Skyrim units, 70 to the metre) read from Skyrim.esm, as the pickups' are:
// beside a stable's carriage driver or horse, a few metres off a village's or a place's marker. San Andreas sets each
// down on the first ground under it that will do, so a spot's height only needs to be near.

#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "pickups.h"
#include "vehicle.h"

namespace AhShitRim::Parked
{
    using Vec3 = Pose::Vec3;
    using Pickups::Hold;
    using Pickups::World;

    inline constexpr float kMeter = Pose::kUnitsPerMeter;

    // ---- When one is out ----
    //
    // CCarGenerator::CheckIfWithinRangeOfAnyPlayers makes a car within 160 m of the player (and past 140 m, where he
    // doesn't see it come). San Andreas only has Skyrim's ground within a few chunks of CJ (Ground::kSampleRadius,
    // 60 to 90 m), so a vehicle here is put out once CJ is within kSpawnRadius and the ground under its spot is in.
    // One nobody has used goes again once CJ is past kFarRadius from it.
    inline constexpr float kSpawnRadius = 100.0f;  // metres across
    inline constexpr float kFarRadius = 160.0f;    // San Andreas's generation distance
    inline constexpr float kRetrySeconds = 1.0f;   // a spot whose ground isn't in, or that has no room, is tried again after this

    // CCarGenerator::Process: once the player takes the car, the next one waits 60 s more (m_nNextGenTime += 60000)
    // and until he has been past the generation distance (bWaitUntilFarFromPlayer). A wreck, or one gone some other
    // way, waits the same.
    inline constexpr float kBackSeconds = 60.0f;

    // Whether an untouched parked vehicle a_distance metres from CJ (across) goes: he is far and nobody uses it.
    inline bool Drops(float a_distance, bool a_used) { return !a_used && !(a_distance <= kFarRadius); }

    // ---- The spots ----

    struct Spot
    {
        std::uint16_t    id;
        std::uint16_t    model;    // San Andreas's vehicle model id
        Hold             hold;
        World            world;
        Vec3             at;       // Skyrim units, near the ground
        float            heading;  // the way it faces, Skyrim radians (0 north, clockwise)
        std::string_view name;
    };

    // San Andreas's models the table uses.
    inline constexpr std::uint16_t kSanchez = 468;
    inline constexpr std::uint16_t kBmx = 481;
    inline constexpr std::uint16_t kRhino = 432;
    inline constexpr std::uint16_t kPatriot = 470;
    inline constexpr std::uint16_t kHunter = 425;
    inline constexpr std::uint16_t kSeasparrow = 447;
    inline constexpr std::uint16_t kMonster = 444;
    inline constexpr std::uint16_t kQuad = 471;
    inline constexpr std::uint16_t kRomero = 442;
    inline constexpr std::uint16_t kCombine = 532;
    inline constexpr std::uint16_t kCropduster = 512;
    inline constexpr std::uint16_t kCamper = 483;
    inline constexpr std::uint16_t kDozer = 486;
    inline constexpr std::uint16_t kFreeway = 463;

    // A spot this far off the marker it stands by, clear of the pickups there (on the marker and 2 m to either side).
    inline constexpr float kAside = 8.0f * kMeter;

    // clang-format off
    inline constexpr Spot kSpots[] = {
        // A Sanchez at each of the five stables, beside the carriage driver.
        { 1, kSanchez, Hold::Whiterun, World::Tamriel, { 17480.0f, -11640.0f, -4700.0f }, 4.8f, "the Whiterun stables" },
        { 2, kSanchez, Hold::Haafingar, World::Tamriel, { -73700.0f, 98500.0f, -11200.0f }, 4.0f, "the Solitude stables at Katla's Farm" },
        { 3, kSanchez, Hold::Eastmarch, World::Tamriel, { 136900.0f, 25100.0f, -12790.0f }, 3.2f, "the Windhelm stables" },
        { 4, kSanchez, Hold::Rift, World::Tamriel, { 173450.0f, -91450.0f, 11130.0f }, 2.1f, "the Riften stables" },
        { 5, kSanchez, Hold::Reach, World::Tamriel, { -169500.0f, 5650.0f, -4060.0f }, 6.14f, "the Markarth stables" },

        // A BMX in every village and small town.
        { 20, kBmx, Hold::Whiterun, World::Tamriel, { 22243.0f, -44616.0f + kAside, -136.0f }, 0.0f, "Riverwood" },
        { 21, kBmx, Hold::Whiterun, World::Tamriel, { -83782.0f, 7902.0f + kAside, -4118.0f }, 0.0f, "Rorikstead" },
        { 22, kBmx, Hold::Rift, World::Tamriel, { 77183.0f, -65456.0f + kAside, 10869.0f }, 0.0f, "Ivarstead" },
        { 23, kBmx, Hold::Haafingar, World::Tamriel, { -101407.0f, 84423.0f + kAside, -12003.0f }, 0.0f, "Dragon Bridge" },
        { 24, kBmx, Hold::Reach, World::Tamriel, { -132797.0f, 38451.0f + kAside, -6200.0f }, 0.0f, "Karthwasten" },
        { 25, kBmx, Hold::Eastmarch, World::Tamriel, { 134765.0f, 12937.0f + kAside, -12073.0f }, 0.0f, "Kynesgrove" },
        { 26, kBmx, Hold::Rift, World::Tamriel, { 163299.0f, -60431.0f + kAside, 7107.0f }, 0.0f, "Shor's Stone" },
        { 27, kBmx, Hold::Eastmarch, World::Tamriel, { 121354.0f, -34306.0f + kAside, -9809.0f }, 0.0f, "Darkwater Crossing" },
        { 28, kBmx, Hold::Reach, World::Tamriel, { -117661.0f, 6257.0f + kAside, -5419.0f }, 0.0f, "Soljund's Sinkhole" },
        { 29, kBmx, Hold::Hjaalmarch, World::Tamriel, { -6557.0f, 64347.0f + kAside, -11432.0f }, 0.0f, "Stonehills" },
        { 30, kBmx, Hold::Falkreath, World::Tamriel, { -30566.0f, -86850.0f + kAside, -3124.0f }, 0.0f, "Falkreath" },
        { 31, kBmx, Hold::Hjaalmarch, World::Tamriel, { -38384.0f, 63751.0f + kAside, -13910.0f }, 0.0f, "Morthal" },
        { 32, kBmx, Hold::Pale, World::Tamriel, { 33278.0f, 105558.0f + kAside, -13633.0f }, 0.0f, "Dawnstar" },
        { 33, kBmx, Hold::Winterhold, World::Tamriel, { 109299.0f, 102777.0f + kAside, -8867.0f }, 0.0f, "Winterhold" },
        { 34, kBmx, Hold::Pale, World::Tamriel, { 73031.0f, 45290.0f + kAside, -6517.0f }, 0.0f, "Heljarchen" },

        // Themed spots, in San Andreas's spirit.
        { 50, kRhino, Hold::Whiterun, World::Tamriel, { -13759.0f + kAside, -8448.0f, -5552.0f }, 1.57f, "Fort Greymoor: a tank at the fort" },
        { 51, kPatriot, Hold::Falkreath, World::Tamriel, { -67967.0f, -51506.0f + kAside, 512.0f }, 0.0f, "the Imperial camp in Falkreath: an army jeep" },
        { 52, kHunter, Hold::Falkreath, World::Tamriel, { 1482.0f, -41162.0f - kAside, 5727.0f }, 3.14f, "Bleak Falls Barrow: a gunship on the heights over Riverwood" },
        { 53, kSeasparrow, Hold::Haafingar, World::Tamriel, { -47782.0f, 128632.0f - kAside, -12908.0f }, 3.14f, "the Solitude Lighthouse: a seaplane helicopter by the sea" },
        { 54, kMonster, Hold::Whiterun, World::Tamriel, { 1539.0f + kAside, 1306.0f, -5796.0f }, 1.57f, "Bleakwind Basin: a monster truck at the giants' camp" },
        { 55, kQuad, Hold::Falkreath, World::Tamriel, { -56672.0f + kAside, -54925.0f, 1559.0f }, 1.57f, "Hunter's Rest: a quad at the hunters' camp" },
        { 56, kRomero, Hold::Falkreath, World::Tamriel, { -30566.0f - kAside, -86850.0f, -3124.0f }, 4.71f, "Falkreath: a hearse in the town of graves" },
        { 57, kCombine, Hold::Whiterun, World::Tamriel, { 19072.0f, -18080.0f - kAside, -4360.0f }, 3.14f, "Pelagia Farm: a combine harvester in the fields" },
        { 58, kCropduster, Hold::Whiterun, World::Tamriel, { -84151.0f, 5351.0f - kAside, -4199.0f }, 3.14f, "the Rorikstead farms: a crop duster" },
        { 59, kCamper, Hold::Falkreath, World::Tamriel, { -43534.0f + kAside, -59851.0f, 992.0f }, 1.57f, "Half-Moon Mill: a camper van by Lake Ilinalta" },
        { 60, kDozer, Hold::Reach, World::Tamriel, { -165398.0f + kAside, 861.0f, -4501.0f }, 1.57f, "Left Hand Mine: a dozer at the mine" },
        { 61, kFreeway, Hold::Reach, World::Tamriel, { -114906.0f, -7748.0f - kAside, -5452.0f }, 3.14f, "Old Hroldan: a chopper outside the roadside inn" },
    };
    // clang-format on

    inline const Spot* SpotById(std::uint16_t a_id)
    {
        for (const auto& spot : kSpots) {
            if (spot.id == a_id) {
                return &spot;
            }
        }
        return nullptr;
    }

    // How far a spot is from CJ, metres across, when he is in a_world (InputState.world): infinite in another world
    // or an interior, whose coordinates are no place in Tamriel.
    inline float DistanceTo(const Spot& a_spot, std::uint8_t a_world, const Vec3& a_cj)
    {
        if (a_world != Pickups::WorldByte(a_spot.world)) {
            return INFINITY;
        }
        return std::hypot(a_spot.at.x - a_cj.x, a_spot.at.y - a_cj.y) / kMeter;
    }

    // ---- Where it goes in San Andreas ----

    // A Skyrim world point in San Andreas, through the handover: a_anchor (Skyrim units) is where a_spawnFeet (San
    // Andreas metres) is.
    inline Vec3 ToSanAndreas(const Vec3& a_skyrim, const Vec3& a_anchor, const Vec3& a_spawnFeet)
    {
        return { a_spawnFeet.x + (a_skyrim.x - a_anchor.x) * Pose::kMetersPerUnit, a_spawnFeet.y + (a_skyrim.y - a_anchor.y) * Pose::kMetersPerUnit,
            a_spawnFeet.z + (a_skyrim.z - a_anchor.z) * Pose::kMetersPerUnit };
    }

    // And back.
    inline Vec3 ToSkyrim(const Vec3& a_sa, const Vec3& a_anchor, const Vec3& a_spawnFeet)
    {
        return { a_anchor.x + (a_sa.x - a_spawnFeet.x) * kMeter, a_anchor.y + (a_sa.y - a_spawnFeet.y) * kMeter, a_anchor.z + (a_sa.z - a_spawnFeet.z) * kMeter };
    }

    // Its ground is looked for this far above and below the spot's height first; the footprint's samples then go from
    // that ground, as a spawned vehicle's go from CJ's feet.
    inline constexpr float kGroundAbove = 3.0f;  // metres, as a pickup's ray (Pickups::kRayAbove): a roof or a tree over the spot isn't its ground
    inline constexpr float kGroundBelow = 30.0f;

    // The spots to try for a parked vehicle at a_at (San Andreas metres, z its ground) facing a_heading (San Andreas
    // radians), best first: the spot itself, then kAroundRings rings of kAroundSteps round it, each facing the same way.
    inline constexpr int   kAroundRings = 3;
    inline constexpr int   kAroundSteps = 8;
    inline constexpr float kAroundStep = 3.0f;  // metres between rings

    inline std::vector<Vehicle::Spot> Candidates(const Vec3& a_at, float a_heading)
    {
        const Vec3                 forward{ -std::sin(a_heading), std::cos(a_heading), 0.0f };  // San Andreas's: a heading h faces (-sin h, cos h)
        std::vector<Vehicle::Spot> out;
        out.push_back({ a_at, forward, a_heading });
        for (int ring = 1; ring <= kAroundRings; ring++) {
            for (int step = 0; step < kAroundSteps; step++) {
                const float angle = 6.283185307f * static_cast<float>(step) / kAroundSteps;
                const float r = ring * kAroundStep;
                out.push_back({ { a_at.x + std::cos(angle) * r, a_at.y + std::sin(angle) * r, a_at.z }, forward, a_heading });
            }
        }
        return out;
    }

    // ---- Taken and back ----

    enum class Status : std::uint8_t
    {
        Waiting,  // none out: one is put out once CJ comes near
        Out,      // San Andreas has it parked there, untouched
        Taken,    // CJ took it, it was wrecked or it went some other way: back after kBackSeconds, once CJ was far
    };

    // Each spot's status, for this session only: a load or a restart starts every spot afresh, as San Andreas's
    // generators do.
    class Board
    {
    public:
        Status StatusOf(std::uint16_t a_id) const
        {
            const auto it = _spots.find(a_id);
            return it == _spots.end() ? Status::Waiting : it->second.status;
        }

        // Once a frame for each spot, a_distance metres from CJ (DistanceTo): whether San Andreas should try to put its
        // vehicle out now. A taken one comes back (to Waiting) once kBackSeconds have passed and CJ has been past
        // kFarRadius since; a waiting one is tried within kSpawnRadius, at most once every kRetrySeconds.
        bool Tick(const Spot& a_spot, double a_now, float a_distance)
        {
            auto& entry = _spots[a_spot.id];
            if (entry.status == Status::Taken) {
                if (!(a_distance <= kFarRadius)) {
                    entry.beenFar = true;
                }
                if (!entry.beenFar || a_now - entry.since < kBackSeconds) {
                    return false;
                }
                entry = Entry{};
            }
            if (entry.status != Status::Waiting || !(a_distance <= kSpawnRadius)) {
                return false;
            }
            if (entry.tried && a_now - entry.since < kRetrySeconds) {
                return false;
            }
            return true;
        }

        // San Andreas tried and couldn't (no ground there yet, no room, no free vehicle slot, the model loading).
        void Tried(std::uint16_t a_id, double a_now)
        {
            auto& entry = _spots[a_id];
            entry.tried = true;
            entry.since = a_now;
        }

        void Out(std::uint16_t a_id) { _spots[a_id] = Entry{ Status::Out }; }

        // An untouched one went because CJ was far (Drops): it waits for him again.
        void Dropped(std::uint16_t a_id) { _spots[a_id] = Entry{}; }

        // CJ got in, it was wrecked, or it went some other way (another vehicle needed its slot).
        void Taken(std::uint16_t a_id, double a_now)
        {
            auto& entry = _spots[a_id];
            entry = Entry{ Status::Taken };
            entry.since = a_now;
        }

        // A new handover or a re-anchor removes every vehicle: the untouched ones wait again, the taken stay taken.
        void Handover()
        {
            for (auto& [id, entry] : _spots) {
                if (entry.status == Status::Out) {
                    entry = Entry{};
                }
            }
        }

    private:
        struct Entry
        {
            Status status = Status::Waiting;
            double since = 0.0;     // Taken: when; Waiting: the last try
            bool   tried = false;   // Waiting: tried at since
            bool   beenFar = false;  // Taken: CJ has been past kFarRadius since
        };

        std::map<std::uint16_t, Entry> _spots;
    };
}
