// San Andreas's wanted level on top of Skyrim's crime system (v22), the game-free half: how many stars a
// hold's bounty and the crime just committed are worth, how they flash and fade while CJ is out of the
// guards' sight, how many guards, riders and soldiers each star brings, when a guard busts CJ out of a stopped
// vehicle, the Busted sequence, the fine and where CJ wakes, and the stars' record in the co-save. Header-only
// and free of game types, so the protocol tests cover it; the SKSE plugin feeds it (skse/src/player/Wanted.cpp).
//
// The bounty stays Skyrim's: the stars are only the pursuit. They rise with a crime (the bounty one of the nine
// holds puts on the player went up), go to 0 once CJ has been out of every pursuer's sight long enough (the
// bounty stays, and a guard who fights him later brings them back), and to 0 with Busted, Wasted or the fine
// paid. AEZAKMI keeps them at 0 and the bounty where it was.
//
// The co-save record ("ASWL", its own beside ASRM so it can grow apart from it): version 1 is the stars (u8),
// the hold's crime faction (u32) and the seconds CJ has been unseen (f32).

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "vehicle_kinds.h"

namespace AhShitRim::Wanted
{
    inline constexpr int kMaxStars = 6;

    // ---- The holds ----

    enum class Side : std::uint8_t
    {
        Imperial,
        Sons,
    };

    struct Hold
    {
        std::uint32_t    faction;  // its crime faction in Skyrim.esm (CrimeFaction*)
        std::string_view name;
        std::uint32_t    guard;     // a guard to copy for the riders when none of the hold's is about (Skyrim.esm NPC_)
        Side             side;      // who holds it when the war starts: whose soldiers join at 5 stars
    };

    // The nine holds. A guard's crime faction says which hold he is from; the riders copy one of those about
    // (the hold's guards as the war has left them), else the guard here. CJ wakes after Busted at the hold's
    // barracks: its crime faction's exterior jail marker (JAIL), where Skyrim lets the player out of its jail.
    inline constexpr std::array<Hold, 9> kHolds = { {
        { 0x000267EA, "Whiterun", 0x000EA0A2, Side::Imperial },     // GuardWhiterunImperialTemplate
        { 0x000267E3, "Eastmarch", 0x00045C31, Side::Sons },        // GuardWindhelmSons
        { 0x0002816B, "the Rift", 0x00045854, Side::Sons },         // GuardRiftenSonsBase
        { 0x0002816C, "the Reach", 0x000210CE, Side::Imperial },    // GuardMarkarthImperialDayShift01
        { 0x00029DB0, "Haafingar", 0x000853C4, Side::Imperial },    // GuardSolitudeImperialBase
        { 0x0002816D, "Hjaalmarch", 0x0002C81B, Side::Imperial },   // GuardHjaalmarchImperial
        { 0x0002816E, "the Pale", 0x0008757D, Side::Sons },         // GuardDawnstarSonsNoShift
        { 0x0002816F, "Winterhold", 0x00024B63, Side::Sons },       // GuardWinterholdSons
        { 0x00028170, "Falkreath", 0x0010107B, Side::Imperial },    // GuardFalkreathImperial
    } };

    // The hold whose crime faction is a_faction, or nullptr.
    constexpr const Hold* HoldOf(std::uint32_t a_faction)
    {
        for (const auto& hold : kHolds) {
            if (hold.faction == a_faction) {
                return &hold;
            }
        }
        return nullptr;
    }

    // Skyrim.esm's other forms the response spawns.
    inline constexpr std::uint32_t kHorse = 0x00023AB2;             // EncHorseSaddledBrown, a saddled horse
    inline constexpr std::uint32_t kSoldiersImperial = 0x0001FC5B;  // LCharSoldierImperial, a levelled legionnaire
    inline constexpr std::uint32_t kSoldiersSons = 0x0001FC5C;      // LCharSoldierSons, a levelled Stormcloak

    // ---- Stars ----

    // The least bounty each star needs: any bounty is one, an assault's 40 two, a murder's 1000 four.
    inline constexpr std::array<int, kMaxStars> kBountyStars = { 1, 40, 500, 1000, 3000, 6000 };

    constexpr int StarsForBounty(int a_bounty)
    {
        int stars = 0;
        for (const int least : kBountyStars) {
            stars += a_bounty >= least ? 1 : 0;
        }
        return stars;
    }

    // What the crime just committed is worth on its own, by the bounty it added (Skyrim's defaults: trespass 5,
    // theft half the value, assault 40, murder 1000).
    constexpr int StarsForCrime(int a_added)
    {
        return a_added >= 1000 ? 4 : a_added >= 40 ? 2 : a_added > 0 ? 1 : 0;
    }

    // A violent crime (an assault or worse) while already wanted is one star more, as more shooting is in San Andreas.
    inline constexpr int kViolent = 40;

    // How long CJ has to stay out of every pursuer's sight before the stars go, by the stars: longer the more there
    // are. San Andreas flashes them all that while.
    constexpr float LoseSeconds(int a_stars) { return 10.0f + 5.0f * static_cast<float>(std::clamp(a_stars, 1, kMaxStars)); }

    inline constexpr float kChangeFlash = 2.0f;  // the stars flash this long after they change, as San Andreas's do

    struct Look
    {
        int  stars = 0;
        bool flashing = false;
    };

    // The pursuit: the stars and how long CJ has been out of sight.
    class Pursuit
    {
    public:
        int   Stars() const { return _stars; }
        float Unseen() const { return _unseen; }

        // A crime: the hold's bounty is a_bounty now, a_added more than before. Returns the stars.
        int Crime(int a_bounty, int a_added, bool a_neverWanted)
        {
            if (a_neverWanted) {
                Clear();
                return 0;
            }
            int stars = (std::max)({ _stars, StarsForBounty(a_bounty), StarsForCrime(a_added) });
            if (_stars > 0 && a_added >= kViolent) {
                stars = (std::max)(stars, _stars + 1);
            }
            Set((std::min)(stars, kMaxStars));
            _unseen = 0.0f;
            return _stars;
        }

        // A guard of the hold fights CJ while no stars show: he is recognised by his bounty.
        void Recognised(int a_bounty, bool a_neverWanted)
        {
            if (_stars == 0 && !a_neverWanted && a_bounty > 0) {
                Set(StarsForBounty(a_bounty));
                _unseen = 0.0f;
            }
        }

        // A frame of a_delta seconds: a_seen while a pursuer sees CJ, a_bounty the hold's bounty now. No bounty, no
        // stars (it was paid or forgiven).
        Look Tick(float a_delta, bool a_seen, int a_bounty, bool a_neverWanted)
        {
            _changed += a_delta;
            if (a_neverWanted || a_bounty <= 0) {
                Clear();
            }
            if (_stars > 0) {
                _unseen = a_seen ? 0.0f : _unseen + a_delta;
                if (_unseen >= LoseSeconds(_stars)) {
                    Clear();
                }
            }
            return { _stars, _stars > 0 && (_unseen > 0.0f || _changed < kChangeFlash) };
        }

        void Clear()
        {
            _stars = 0;
            _unseen = 0.0f;
        }

        // A loaded save's.
        void Restore(int a_stars, float a_unseen)
        {
            _stars = std::clamp(a_stars, 0, kMaxStars);
            _unseen = std::isfinite(a_unseen) ? std::clamp(a_unseen, 0.0f, LoseSeconds(kMaxStars)) : 0.0f;
            _changed = kChangeFlash;
        }

    private:
        void Set(int a_stars)
        {
            if (a_stars != _stars) {
                _changed = 0.0f;
            }
            _stars = a_stars;
        }

        int   _stars = 0;
        float _unseen = 0.0f;
        float _changed = kChangeFlash;
    };

    // ---- The response ----

    struct Response
    {
        float joinRadius = 0.0f;  // Skyrim units: the hold's guards this close join the fight (0: only those Skyrim sends)
        int   riders = 0;         // mounted guards spawned to chase CJ
        int   soldiers = 0;       // the hold's soldiers spawned to join
    };

    // More guards join as the stars rise; riders from 3, soldiers from 5.
    constexpr Response ResponseFor(int a_stars)
    {
        switch (std::clamp(a_stars, 0, kMaxStars)) {
        case 2: return { 2000.0f, 0, 0 };
        case 3: return { 4000.0f, 1, 0 };
        case 4: return { 6000.0f, 2, 0 };
        case 5: return { 8000.0f, 2, 2 };
        case 6: return { 12000.0f, 3, 4 };
        default: return {};
        }
    }

    inline constexpr float kSpawnEvery = 5.0f;       // seconds between one spawned pursuer and the next
    inline constexpr float kSpawnDistance = 3500.0f;  // Skyrim units behind CJ they come from
    inline constexpr float kSightRange = 5000.0f;     // a pursuer further off doesn't count as seeing CJ

    // ---- Busted in a vehicle ----

    enum class Bust : std::uint8_t
    {
        None,     // a vehicle no guard busts CJ out of (a boat, an aircraft) or on foot (Skyrim's own arrest)
        Door,     // a car: a guard at the driver's door opens it and drags him out
        KnockOff, // a bike, bicycle or quad: a guard reaching him knocks him off
    };

    constexpr Bust BustOf(VehicleKinds::Kind a_kind)
    {
        switch (a_kind) {
        case VehicleKinds::Kind::Car:
        case VehicleKinds::Kind::MonsterTruck: return Bust::Door;
        case VehicleKinds::Kind::Bike:
        case VehicleKinds::Kind::Bicycle:
        case VehicleKinds::Kind::Quad: return Bust::KnockOff;
        default: return Bust::None;
        }
    }

    inline constexpr float kStoppedSpeed = 105.0f;  // Skyrim units per second, 1.5 m/s: nearly stopped
    inline constexpr float kStoppedFor = 1.5f;      // seconds nearly stopped before a guard can bust him
    inline constexpr float kReach = 110.0f;         // Skyrim units, 1.5 m: a guard this close to the door (or to CJ on a bike) has him
    inline constexpr float kDoorSide = 95.0f;       // the driver's door: this far left of the car's middle (San Andreas drives on the right, its driver sits left)
    inline constexpr float kDoorAhead = 15.0f;      // and this far forward

    struct Point
    {
        float x = 0.0f, y = 0.0f;
    };

    // Where a guard has to stand to open the driver's door of a car whose middle is a_middle, heading a_heading
    // (Skyrim radians: 0 north, clockwise).
    inline Point DoorSpot(Point a_middle, float a_heading)
    {
        const float ahead[2] = { std::sin(a_heading), std::cos(a_heading) };
        const float right[2] = { std::cos(a_heading), -std::sin(a_heading) };
        return { a_middle.x + ahead[0] * kDoorAhead - right[0] * kDoorSide, a_middle.y + ahead[1] * kDoorAhead - right[1] * kDoorSide };
    }

    // How long CJ's vehicle has been nearly stopped while he's wanted; a guard within reach then busts him.
    class StopWatch
    {
    public:
        // A frame of a_delta seconds: a_wanted while stars show, a_speed the vehicle's (Skyrim units per second),
        // a_reach the nearest guard's distance to the door (to CJ on a bike), a_bust what the vehicle allows. Moving
        // keeps him safe. Returns whether he is busted now.
        bool Tick(float a_delta, bool a_wanted, Bust a_bust, float a_speed, float a_reach)
        {
            if (!a_wanted || a_bust == Bust::None || !std::isfinite(a_speed) || a_speed > kStoppedSpeed) {
                _stopped = 0.0f;
                return false;
            }
            _stopped += a_delta;
            return _stopped >= kStoppedFor && std::isfinite(a_reach) && a_reach <= kReach;
        }

        float Stopped() const { return _stopped; }
        void  Reset() { _stopped = 0.0f; }

    private:
        float _stopped = 0.0f;
    };

    // ---- Busted ----

    enum class Arrest : std::uint8_t
    {
        OnFoot,    // Skyrim's "go to jail": CJ puts his hands up
        Door,      // dragged out of a car
        KnockOff,  // knocked off a bike
    };

    enum class Phase
    {
        Idle,
        Arrest,  // dragged out, knocked off, or his hands up
        Shown,   // "busted"
        Fading,  // to black, the text with it
        Moving,  // black, waiting for the player to be outside the barracks
        Waking,  // the fine paid, fading back in
    };

    enum class Action
    {
        None,
        Begin,
        Busted,   // "busted" comes up
        Move,     // take the player to the barracks
        Release,  // the fine, the bounty cleared, the stars gone
        Done,
    };

    // San Andreas's own (CGameLogic): "busted" up for 3 s, a 3 s fade out, a 2 s fade in. The arrest before
    // it as long as its animation: San Andreas's jacked one takes about 2.5 s, falling off a bike about 1.5 s.
    struct Timing
    {
        float dragged = 2.5f;
        float knocked = 1.5f;
        float handsUp = 1.0f;
        float shown = 3.0f;
        float fadeOut = 3.0f;
        float moveTimeout = 12.0f;
        float wake = 2.0f;
    };

    struct Frame
    {
        Action action = Action::None;
        bool   active = false;  // the player can't act
        bool   busted = false;  // San Andreas gets kInputBusted: CJ is dragged out, knocked off or holds his hands up
        float  fade = 0.0f;
    };

    // How long "busted" stays up, from Action::Busted: until the screen is black.
    inline float TextSeconds(const Timing& a_timing = {}) { return a_timing.shown + a_timing.fadeOut; }

    class Sequence
    {
    public:
        explicit Sequence(Timing a_timing = {}) : _t(a_timing) {}

        Phase  GetPhase() const { return _phase; }
        Arrest GetArrest() const { return _arrest; }
        bool   Running() const { return _phase != Phase::Idle; }

        Frame Start(Arrest a_arrest)
        {
            _arrest = a_arrest;
            _phase = Phase::Arrest;
            _time = 0.0f;
            Frame f = Shape(0.0f);
            f.action = Action::Begin;
            return f;
        }

        // a_arrived: the player is outside the barracks (or there is nowhere to go), only looked at once Move was asked.
        Frame Tick(float a_delta, bool a_arrived)
        {
            if (_phase == Phase::Idle) {
                return {};
            }
            _time += a_delta;
            Action action = Action::None;
            switch (_phase) {
            case Phase::Arrest:
                if (_time >= ArrestSeconds()) {
                    _phase = Phase::Shown;
                    _time = 0.0f;
                    action = Action::Busted;
                }
                break;
            case Phase::Shown:
                if (_time >= _t.shown) {
                    _phase = Phase::Fading;
                    _time = 0.0f;
                }
                break;
            case Phase::Fading:
                if (_time >= _t.fadeOut) {
                    _phase = Phase::Moving;
                    _time = 0.0f;
                    action = Action::Move;
                }
                break;
            case Phase::Moving:
                if (a_arrived || _time >= _t.moveTimeout) {
                    _phase = Phase::Waking;
                    _time = 0.0f;
                    action = Action::Release;
                }
                break;
            case Phase::Waking:
                if (_time >= _t.wake) {
                    _phase = Phase::Idle;
                    Frame done;
                    done.action = Action::Done;
                    return done;
                }
                break;
            default: break;
            }
            Frame f = Shape(_time);
            f.action = action;
            return f;
        }

        void Cancel() { _phase = Phase::Idle; }

    private:
        float ArrestSeconds() const { return _arrest == Arrest::Door ? _t.dragged : _arrest == Arrest::KnockOff ? _t.knocked : _t.handsUp; }

        Frame Shape(float a_time) const
        {
            Frame f;
            f.active = _phase != Phase::Idle && _phase != Phase::Waking;
            f.busted = _phase == Phase::Arrest || _phase == Phase::Shown || _phase == Phase::Fading;
            switch (_phase) {
            case Phase::Fading: f.fade = (std::min)(a_time / _t.fadeOut, 1.0f); break;
            case Phase::Moving: f.fade = 1.0f; break;
            case Phase::Waking: f.fade = 1.0f - (std::min)(a_time / _t.wake, 1.0f); break;
            default: break;
            }
            return f;
        }

        Timing _t;
        Phase  _phase = Phase::Idle;
        Arrest _arrest = Arrest::OnFoot;
        float  _time = 0.0f;
    };

    // The fine Busted takes: the whole bounty, or all the gold if that's short. The bounty is cleared either way.
    constexpr int FineFor(int a_bounty, int a_gold) { return (std::max)(0, (std::min)(a_bounty, a_gold)); }

    // ---- The co-save ----

    inline constexpr std::uint32_t kRecordType = 0x4C575341;  // "ASWL"
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::size_t   kSizeV1 = 1 + 4 + 4;

    struct Record
    {
        int           stars = 0;
        std::uint32_t faction = 0;  // the hold's crime faction, 0 for none
        float         unseen = 0.0f;
    };

    inline std::vector<std::uint8_t> Write(const Record& a_record)
    {
        std::vector<std::uint8_t> out(kSizeV1);
        out[0] = static_cast<std::uint8_t>(std::clamp(a_record.stars, 0, kMaxStars));
        std::memcpy(out.data() + 1, &a_record.faction, 4);
        std::memcpy(out.data() + 5, &a_record.unseen, 4);
        return out;
    }

    // Reads a record of any version up to the current one; false if it doesn't read (a_out is left empty).
    inline bool Read(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size, Record& a_out)
    {
        a_out = {};
        if (a_version < 1 || a_version > kVersion || a_size < kSizeV1 || a_data[0] > kMaxStars) {
            return false;
        }
        Record record;
        record.stars = a_data[0];
        std::memcpy(&record.faction, a_data + 1, 4);
        std::memcpy(&record.unseen, a_data + 5, 4);
        if (!std::isfinite(record.unseen) || record.unseen < 0.0f) {
            return false;
        }
        a_out = record;
        return true;
    }
}
