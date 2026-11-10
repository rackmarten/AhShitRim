#include "debug/HitTest.h"

#include "Ini.h"
#include "car_exit.h"
#include "crew.h"
#include "health.h"
#include "hud/Overlay.h"
#include "input.h"
#include "passengers.h"
#include "link/LinkService.h"
#include "pad.h"
#include "player/Homies.h"
#include "player/LockOn.h"
#include "player/ObjectHits.h"
#include "body/ModelView.h"
#include "player/PlayerSync.h"
#include "player/Shots.h"
#include "player/Police.h"
#include "player/StealthKills.h"
#include "player/Respawns.h"
#include "player/Riders.h"
#include "player/Vitals.h"
#include "wanted.h"
#include "weapons.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <thread>

namespace AhShitRim::HitTest
{
    namespace
    {
        enum class Phase
        {
            Off,
            MainMenu,  // waiting for it to load the most recent save
            Loading,
            Going,     // the coc to the start
            Arriving,
            Handing,   // CJ comes, in the Sandking
            Watching,
            Killing,    // respawns: CJ kills a few of the people around
            Waiting,    // their quests' window, then / for Skyrim's wait menu
            WaitMenu,   // it opens (and is closed) or Skyrim refuses it; after the kills, an hour on the clock
            Looking,    // still there: who gets up out of view, and who waits in view
            Away,       // in an interior, out of their sight
            Returning,  // back outside: are they up at their spots?
            CrewSpots,  // crew (v39): the spots fill, two homies join
            CrewFight,  // wolves come, the crew shoots
            CrewHurt,   // CJ shoots one of his own: hurt, never hostile
            CrewCheat,  // HESOYAM typed with a crew sends nobody off; H on its own still does, and two join again
            CrewOut,    // F10 out and back: the crew comes again
            CrewMoved,  // the player moved as fast travel moves it: the followers come behind
            Standing,   // armour: CJ stands still with full armour, its level logged
            Boarding,   // ridecrew, ridebike: a Huntley (a Sanchez) in front of CJ on foot, F: he gets in, his homies with him
            Seated,     // riders: CJ and his follower sit in the Sandking
            Leaving,    // riders: F, both get out, traced every frame
            Oil,        // oil: the pools lit, watched burning
            CrewWalk,   // CJ walks, stops, runs off at an angle and stops: the followers walk and run after him, facing their way
            Driving,    // drive: CJ drives the Sandking a minute, traced every frame
            Soak,       // soak: crew on foot, a drive, F11, over and over for the whole time
            CrewStray,  // a follower put 70 m behind, as one left by a car or stuck: he's put back near CJ and comes up
            Done,
        };

        constexpr std::uint16_t kSandking = 495;
        constexpr std::uint16_t kHuntley = 579;  // ridecrew: four seats, so the crew of two both ride
        constexpr RE::FormID    kElk = 0x00023A91;  // EncElkMale (checked by its name before it's placed)
        constexpr std::uint16_t kSanchez = 468;  // ridebike: one pillion, so one homie rides and gets off with CJ
        constexpr float         kAhead = 840.0f;  // 12 m: where the attackers are placed

        struct State
        {
            Phase              phase = Phase::Off;
            bool               bust = false;  // the scenario: bust, else hits
            bool               respawns = false;  // the scenario: respawns
            std::string        away = "RiverwoodSleepingGiantInn";
            bool               lights = false;  // or lights: night, CJ in HitTestVehicle, its lamps logged and photographed
            std::uint16_t      vehicle = kSandking;
            std::string        shotsDir;  // HitTestShots: the screenshots' folder (empty: AhShitRim's Screenshots)
            int                shot = 0;
            std::string        start = "Riverwood";
            float              seconds = 90.0f;
            float              timer = 0.0f;
            float              logTimer = 0.0f;
            float              combatTimer = 0.0f;
            bool               newGame = false;  // a new game at the start instead of the most recent save
            std::vector<RE::ObjectRefHandle> attackers;
            bool               stealth = false;  // the scenario: the knife stealth kill (v38)
            bool               pad = false;      // the scenario: the gamepad's actions (DESIGN.md, Controller)
            int                step = 0;         // its steps
            RE::ObjectRefHandle victim;
            std::vector<RE::FormID>          victims;  // respawns
            bool               waitAsked = false;  // the wait menu opened (and was told to close)
            bool               waitedFirst = false;  // the / press before the kills (no enemies near yet) is done
            bool               crew = false;     // the scenario: the Grove Street crew (v39)
            RE::ActorHandle    hurt;             // the follower CJ shot
            bool                riders = false;  // the scenario: a follower rides with CJ and gets out with him
            bool                armour = false;   // the scenario: CJ stands still with armour and none of it may go
            bool                crewRider = false;  // riders with a recruited homie of the crew (v39) as the follower
            bool                bike = false;       // ridebike: ridecrew on a Sanchez
            RE::ObjectRefHandle follower;
            std::vector<RE::ObjectRefHandle> homies;  // ridecrew: the crew's followers, who board with CJ
            int                 boardTry = 0;  // ridecrew: the Huntley (Sanchez) asked for this many times

            // Someone riding with CJ as he gets out, traced every frame from F.
            struct Passenger
            {
                RE::ObjectRefHandle          handle;
                std::string                  name;
                std::vector<CarExit::Sample> trace;          // his feet in the vehicle's frame, up to the frame Riders lets him go
                int                          seatedLeaving = 0;  // frames he still sat (kCrewSeated to San Andreas) while CJ was getting out
                float                        letGo = -1.0f;  // seconds since Riders let him go, -1 before
                float                        letGoZ = 0.0f;  // his feet's height then
                bool                         grounded = false;  // a second after: Skyrim's controller has him on the ground
                float                        drop = 0.0f;    // and how far he came down in that second, metres
                bool                         groundChecked = false;
            };
            std::vector<Passenger>       passengers;
            std::vector<CarExit::Sample> cjTrace;     // and the player's (CJ's feet)
            float               outFor = 0.0f;     // seconds since all were out
            bool               oil = false;   // the scenario: oil lit by a bullet, a Molotov and a rocket
            bool               oilMoved = false;
            bool               oilReset = false;  // the pools placed
            std::string        oilAt = "0009B0E6";  // the reference the player is moved to first (HitTestOilAt): by the floor PlaceOil puts the pools on

            struct Pool
            {
                RE::ObjectRefHandle ref;
                const char*         by = "";
                float               lowest = 100.0f;  // its health at the lowest seen, of 100
                bool                burned = false;   // seen burning: below 94% and above 7% (its fire's stages)
                bool                gone = false;     // burnt out and disabled
            };
            std::vector<Pool> pools;
            float              moveX = 0.0f;     // CrewWalk: CJ's stick
            float              moveY = 0.0f;
            float              walkLog = 0.0f;   // CrewWalk: when a follower was last logged
            float              shotTimer = 0.0f;  // CrewFight: seconds to the next F11 screenshot (the crew's crash trigger in play)
            int                shots = 0;
            int                walks = 0;            // CrewWalk: the walks done (the first at the stables, the second after the move)
            int                regroupedBefore = 0;  // Homies::DebugRegrouped() as the second walk started
            RE::ObjectRefHandle elk;                 // CrewFight: an elk CJ shot, which the crew must leave be
            bool               elkTargeted = false;
            bool               elkFled = false;      // it had CJ as its combat target: the old rule's enemy
            float              walkMax = 0.0f;    // the furthest a follower was while CJ moved, metres
            float              stood = -1.0f;     // how far he was after CJ's first stand
            float              followLog = 0.0f;  // the crew phases: seconds since a follower was last logged
            RE::FormID         followerId = 0;
            RE::NiPoint3       followerWas;
            float               victimAhead = 1.6f * Pose::kUnitsPerMeter;  // the guard with his back to CJ ([Debug] HitTestStealthMeters)
            RE::FormID          nearRef = 0;      // [Debug] HitTestNear: a ref the player is moved to first, its stealth verdict logged
            bool                movedNear = false;
            bool                drive = false;     // the scenario: CJ drives, his body traced in the vehicle as Skyrim draws them
            std::vector<CarExit::Sample> feetTrace, nodeTrace, cameraTrace;  // drive: CJ's drawn feet, his model's node, the camera
            int                 legs = 0;          // drive: the steering's legs so far
            int                 cells = 0;         // drive: exterior cells crossed
            RE::TESObjectCELL*  lastCell = nullptr;
            float               driven = 0.0f;     // drive: how far the player went, metres
            RE::NiPoint3        lastAt;
            bool                soak = false;       // the scenario: a long run of play (crew on foot, a drive, F11, the clock running)
            int                 soakSeg = 0;        // 0 on foot, 1 getting in, 2 driving, 3 getting out
            float               soakT = 0.0f;       // seconds in the segment
            float               soakShot = 0.0f;    // to the next F11
            float               soakLog = 0.0f;     // to the next status line
            int                 soakCycles = 0;     // drives done
            int                 soakShots = 0;
            int                 soakWolves = 0;     // wolf pairs sent
            int                 soakGone = 0;       // times CJ never got into the vehicle
            bool                soakRecruited = false;  // this on-foot stretch's recruit and wolves are done
            bool                soakWolvesSent = false;
        } state;

        // ridecrew's vehicle, or ridebike's.
        std::uint16_t RideModel() { return state.bike ? kSanchez : kHuntley; }
        const char*   RideName() { return state.bike ? "Sanchez" : "Huntley"; }

        constexpr float kOthersAside = 5.0f * Pose::kUnitsPerMeter;  // and a guard facing CJ, off to the side

        void RunConsole(const std::string& a_command)
        {
            auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
            auto* script = factory ? factory->Create() : nullptr;
            if (!script) {
                return;
            }
            script->SetCommand(a_command);
            script->CompileAndRun(nullptr);
            delete script;
            SKSE::log::info("hit test: ran '{}'", a_command);
        }

        void TryLoad(int a_try)
        {
            std::thread([a_try] {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                SKSE::GetTaskInterface()->AddTask([a_try] {
                    auto* saves = RE::BGSSaveLoadManager::GetSingleton();
                    if (state.newGame) {
                        RunConsole(state.start.starts_with("cow ") ? state.start : "coc " + state.start);  // from the main menu: a new game there
                    } else if (saves && saves->PopulateSaveList() && saves->LoadMostRecentSaveGame()) {
                        SKSE::log::info("hit test: loading the most recent save");
                    } else if (a_try < 10) {
                        TryLoad(a_try + 1);
                    } else {
                        SKSE::log::info("hit test: could not load the most recent save");
                        state.phase = Phase::Done;
                    }
                });
            }).detach();
        }

        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (state.phase == Phase::MainMenu && a_event && a_event->opening && a_event->menuName == RE::MainMenu::MENU_NAME) {
                    state.phase = Phase::Loading;
                    TryLoad(1);
                }
                if (state.phase == Phase::WaitMenu && a_event && a_event->menuName == RE::SleepWaitMenu::MENU_NAME) {
                    SKSE::log::info("hit test: Skyrim's wait menu {}", a_event->opening ? "opened" : "closed");
                    if (a_event->opening && !state.waitAsked) {
                        state.waitAsked = true;
                        // It opened through /: closed again, as the hit test can't press its Wait button (driving the menu's
                        // OK from here never waited), and the hour goes on the clock instead.
                        if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                            queue->AddMessage(RE::SleepWaitMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                        }
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } menuSink;

        bool InVehicle()
        {
            Protocol::PlayerState pose{};
            return LinkService::Get().ReadPlayer(pose) && (pose.flags & Protocol::kPlayerValid) && (pose.flags & Protocol::kPlayerInVehicle);
        }

        float CarHealth()
        {
            for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
                Protocol::BodyState vehicle{};
                if (LinkService::Get().ReadVehicle(i, vehicle) && (vehicle.flags & Protocol::kBodyValid) && (vehicle.flags & Protocol::kBodyDriven)) {
                    return vehicle.health;
                }
            }
            return -1.0f;
        }

        // A giant and two guards 12 m ahead, a few metres apart, set on the player.
        void PlaceAttackers(RE::PlayerCharacter* a_player)
        {
            const std::uint32_t kinds[] = { Wanted::kGiant, Wanted::kHolds[0].guard, Wanted::kHolds[0].guard };
            const float         heading = a_player->GetAngleZ();
            for (int i = 0; i < 3; i++) {
                auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kinds[i]);
                auto  ref = base ? a_player->PlaceObjectAtMe(base, false) : nullptr;
                if (!ref) {
                    SKSE::log::warn("hit test: couldn't place {:08X}", kinds[i]);
                    continue;
                }
                const float side = (i - 1) * 280.0f;
                const auto  at = a_player->GetPosition() + RE::NiPoint3(std::sin(heading) * kAhead + std::cos(heading) * side,
                                                                std::cos(heading) * kAhead - std::sin(heading) * side, 0.0f);
                ref->SetPosition(at);
                state.attackers.push_back(ref->GetHandle());
                SKSE::log::info("hit test: placed {} ({:08X}) {:.0f} units ahead", ref->GetName(), ref->GetFormID(), kAhead);
            }
        }

        // Once a second from the join on: how far a follower is from the player, how fast he goes and what his AI is up
        // to, so a follower who stays put shows why.
        void LogFollower(RE::PlayerCharacter* a_player, float a_delta)
        {
            if ((state.followLog += a_delta) < 1.0f) {
                return;
            }
            auto* follower = Homies::DebugFollower();
            if (!follower) {
                state.followerId = 0;
                return;
            }
            const auto at = follower->GetPosition();
            const bool same = follower->GetFormID() == state.followerId;
            auto*      process = follower->GetActorRuntimeData().currentProcess;
            auto*      package = follower->GetCurrentPackage();
            const auto feet = a_player->GetPosition();
            RE::NiPoint3 mesh;
            const bool   onMesh = Homies::DebugNavmesh(a_player, mesh);
            SKSE::log::info("hit test: crew: follower {:08X} {:.1f} m from the player, {:.1f} m/s, AI {}, process {}, package {:08X} type {}, 3D {}{}; "
                            "the player {:.1f} m across and {:+.1f} m up from the nearest navmesh{}",
                follower->GetFormID(), at.GetDistance(feet) / Pose::kUnitsPerMeter,
                same ? at.GetDistance(state.followerWas) / Pose::kUnitsPerMeter / state.followLog : 0.0f, follower->IsAIEnabled() ? "on" : "off",
                process ? static_cast<int>(process->processLevel.get()) : -1, package ? package->GetFormID() : 0u,
                package ? static_cast<int>(package->packData.packType.get()) : -1, follower->Is3DLoaded() ? "loaded" : "not loaded",
                follower->IsInCombat() ? ", in combat" : "", std::hypot(feet.x - mesh.x, feet.y - mesh.y) / Pose::kUnitsPerMeter,
                (feet.z - mesh.z) / Pose::kUnitsPerMeter, onMesh ? "" : " (none found)");
            state.followerId = follower->GetFormID();
            state.followerWas = at;
            state.followLog = 0.0f;
        }

        void SetOn(RE::PlayerCharacter* a_player)
        {
            for (const auto& handle : state.attackers) {
                auto  ref = handle.get();
                auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                if (actor && !actor->IsDead() && actor->Is3DLoaded() && actor->GetActorRuntimeData().currentCombatTarget.get().get() != a_player) {
                    actor->StartCombat(a_player);
                }
            }
        }

        // ---- The pad scenario (DESIGN.md, Controller) ----

        float Meters(const float (&a_from)[3], const float (&a_to)[3])
        {
            const float dx = a_to[0] - a_from[0], dy = a_to[1] - a_from[1];
            return std::sqrt(dx * dx + dy * dy) * Pose::kMetersPerUnit;
        }

        // The heading from a_at with the most room ahead, for the drive: 16 headings, two rays each (0.6 m and 1.2 m up, 40 m
        // long, Skyrim's line of sight layer), the one whose nearer hit is farthest. a_clear: that hit, metres.
        float ClearHeading(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, float& a_clear)
        {
            constexpr float kLength = 40.0f * Pose::kUnitsPerMeter;
            auto*           cell = a_player->GetParentCell();
            auto*           bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto*           world = bhk ? bhk->GetWorld1() : nullptr;
            float           best = a_player->GetAngleZ();
            a_clear = 0.0f;
            if (!world) {
                return best;
            }
            const float              scale = RE::bhkWorld::GetWorldScale();
            RE::hkpWorldRayCastInput input;
            input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
            RE::BSReadLockGuard lock(bhk->worldLock);
            for (int i = 0; i < 16; i++) {
                const float h = static_cast<float>(i) * Pose::kPi / 8.0f;
                const float dx = std::sin(h), dy = std::cos(h);
                float       clear = kLength;
                for (const float up : { 0.6f, 1.2f }) {
                    const RE::NiPoint3 from = a_at + RE::NiPoint3(dx * 70.0f, dy * 70.0f, up * Pose::kUnitsPerMeter);
                    const RE::NiPoint3 to = from + RE::NiPoint3(dx * kLength, dy * kLength, 0.0f);
                    input.from = RE::hkVector4(from.x * scale, from.y * scale, from.z * scale, 0.0f);
                    input.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0f);
                    RE::hkpWorldRayCastOutput output;
                    world->CastRay(input, output);
                    if (output.HasHit()) {
                        clear = (std::min)(clear, kLength * output.hitFraction);
                    }
                }
                if (clear > a_clear * Pose::kUnitsPerMeter) {
                    a_clear = clear * Pose::kMetersPerUnit;
                    best = h;
                }
            }
            return best;
        }

        void PadCheck(bool a_ok, const std::string& a_what) { SKSE::log::info("hit test: pad: {} {}", a_what, a_ok ? "ok" : "FAILED"); }

        void PadNext()
        {
            state.step++;
            state.timer = 0.0f;
        }

        // From Arriving on: the pad's events injected as Skyrim would send them (PlayerSync::DebugPad, DebugStick), each
        // action's effect read back from San Andreas's pose and logged: "hit test: pad: ... ok" or "... FAILED".
        void PadSteps()
        {
            static Protocol::PlayerState before{};
            static float                 peak = 0.0f;  // the drive's top speed, km/h
            static int                   tries = 0;    // d-pad presses looking for a gun
            static RE::FormID            firstLock = 0;
            static RE::NiPoint3          start;        // where the coc put the player: the drive starts there
            static int                   rounds = 0;   // in and out of the car with Y
            static RE::FormID            disabled = 0;  // the locked guard disabled on purpose
            Protocol::PlayerState        pose{};
            const bool                   valid = LinkService::Get().ReadPlayer(pose) && (pose.flags & Protocol::kPlayerValid);
            const bool                   onFoot = valid && !(pose.flags & Protocol::kPlayerInVehicle);
            switch (state.step) {
            case 0:  // Back held a second, with Skyrim on the player
                if (state.timer >= 6.0f && PlayerSync::DebugOwned()) {
                    // The most recent save was made with CJ on the player: Skyrim has it first, as the steps below expect.
                    SKSE::log::info("hit test: pad: the save had CJ on the player, handing it back first");
                    PlayerSync::DebugToggle();
                    state.timer = 2.0f;
                } else if (state.timer >= 6.0f) {
                    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                        start = player->GetPosition();
                    }
                    SKSE::log::info("hit test: pad: holding Back with Skyrim on the player");
                    PlayerSync::DebugPad(Pad::kBack, 1.0f);
                    PadNext();
                }
                break;
            case 1:
                if (state.timer >= 1.5f) {
                    PlayerSync::DebugPad(Pad::kBack, 0.0f);
                    PadCheck(PlayerSync::DebugOwned(), "Back held 1.5 s hands the player to CJ:");
                    PadNext();
                }
                break;
            case 2:  // the left stick forward
                if (onFoot && state.timer >= 5.0f) {
                    before = pose;
                    PlayerSync::DebugStick(false, 0.0f, 1.0f);
                    PadNext();
                } else if (state.timer > 40.0f) {
                    PadCheck(false, "CJ came on foot:");
                    state.phase = Phase::Done;
                }
                break;
            case 3:
                if (state.timer >= 2.0f) {
                    const float moved = Meters(before.offset, pose.offset);
                    PadCheck(moved > 1.5f, std::format("the left stick forward 2 s walks CJ {:.1f} m:", moved));
                    PlayerSync::DebugStick(false, 0.0f, 0.0f);
                    before = pose;
                    PlayerSync::DebugStick(true, 1.0f, 0.0f);
                    PadNext();
                }
                break;
            case 4:  // the right stick right
                if (state.timer >= 1.0f) {
                    PlayerSync::DebugStick(true, 0.0f, 0.0f);
                    float turned = (pose.viewHeading - before.viewHeading) * 57.29578f;
                    while (turned > 180.0f) {
                        turned -= 360.0f;
                    }
                    while (turned < -180.0f) {
                        turned += 360.0f;
                    }
                    PadCheck(std::fabs(turned) > 20.0f, std::format("the right stick 1 s turns the camera {:.0f} degrees:", turned));
                    PlayerSync::DebugType("LXGIWYL");
                    PadNext();
                }
                break;
            case 5:  // the d-pad's right: the next weapon
                if (state.timer >= 3.0f) {
                    before = pose;
                    PlayerSync::DebugPad(Pad::kRight, 1.0f);
                    PadNext();
                }
                break;
            case 6:
                if (state.timer >= 0.2f) {
                    PlayerSync::DebugPad(Pad::kRight, 0.0f);
                    PadNext();
                }
                break;
            case 7:
                if (state.timer >= 1.5f) {
                    if (tries == 0) {
                        PadCheck(pose.weaponType != before.weaponType, std::format("the d-pad's right switches weapon {} to {}:", before.weaponType, pose.weaponType));
                    }
                    // On to a gun (the pistols to the sniper rifle) for LT and RT: the set's melee weapons have no aim camera or bullets.
                    if ((pose.weaponType < 22 || pose.weaponType > 34) && ++tries < 10) {
                        PlayerSync::DebugPad(Pad::kRight, 1.0f);
                        state.step = 6;
                        state.timer = 0.0f;
                        break;
                    }
                    PlayerSync::DebugPad(Pad::kLeftTrigger, 0.9f);
                    PadNext();
                }
                break;
            case 8:  // LT aims, RT fires
                if (state.timer >= 1.0f) {
                    PadCheck((pose.weaponFlags & Protocol::kWeaponAiming) != 0, std::format("LT aims (weapon {}):", pose.weaponType));
                    before = pose;
                    PlayerSync::DebugPad(Pad::kRightTrigger, 1.0f);
                    PadNext();
                }
                break;
            case 9:
                if (state.timer >= 0.6f) {
                    PlayerSync::DebugPad(Pad::kRightTrigger, 0.0f);
                    PadNext();
                }
                break;
            case 10:
                if (state.timer >= 1.0f) {
                    const int fired = static_cast<std::uint16_t>(pose.shots - before.shots);
                    PadCheck(fired > 0, std::format("RT fires {} shots:", fired));
                    PlayerSync::DebugPad(Pad::kLeftTrigger, 0.0f);
                    // The gun lock: two guards ahead, one each side of the camera, set on CJ.
                    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                        const float view = pose.viewHeading;
                        for (const float side : { -0.22f, 0.22f }) {
                            auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(Wanted::kHolds[0].guard);
                            auto  ref = base ? player->PlaceObjectAtMe(base, false) : nullptr;
                            if (!ref) {
                                continue;
                            }
                            const float h = view + side;  // the pose's view heading is Skyrim's convention (Pose::HeadingOf)
                            ref->SetPosition(player->GetPosition() + RE::NiPoint3(std::sin(h) * 700.0f, std::cos(h) * 700.0f, 0.0f));
                            state.attackers.push_back(ref->GetHandle());
                        }
                        SKSE::log::info("hit test: pad: {} guards placed 10 m ahead, either side of the camera", state.attackers.size());
                    }
                    state.step = 20;
                    state.timer = 0.0f;
                }
                break;
            case 20:  // LT with the pistol: the lock turns the camera by itself
                if (state.timer >= 3.0f) {
                    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                        SetOn(player);
                    }
                    before = pose;
                    PlayerSync::DebugPad(Pad::kLeftTrigger, 0.9f);
                    PadNext();
                }
                break;
            case 21:
                if (state.timer >= 2.0f) {
                    float turned = (pose.viewHeading - before.viewHeading) * 57.29578f;
                    while (turned > 180.0f) {
                        turned -= 360.0f;
                    }
                    while (turned < -180.0f) {
                        turned += 360.0f;
                    }
                    before = pose;
                    firstLock = LockOn::DebugGunTarget();
                    tries = 0;  // the shift's side, tried once more the other way
                    PadCheck(LockOn::DebugGunTarget() != 0 && std::fabs(turned) > 3.0f,
                        std::format("LT locks on to {:08X} and the camera turns {:.0f} degrees onto it, the right stick untouched:", LockOn::DebugGunTarget(), turned));
                    PlayerSync::DebugPad(Pad::kRight, 1.0f);
                    PadNext();
                }
                break;
            case 22:
                if (state.timer >= 0.2f) {
                    PlayerSync::DebugPad(Pad::kRight, 0.0f);
                    PlayerSync::DebugPad(Pad::kLeft, 0.0f);
                }
                // The lock may have taken the right-hand guard: then the shift is to the left.
                if (state.timer >= 1.0f && LockOn::DebugGunTarget() == firstLock && !tries) {
                    tries = 1;
                    PlayerSync::DebugPad(Pad::kLeft, 1.0f);
                    state.timer = 0.0f;
                    break;
                }
                if (state.timer >= 1.5f) {
                    const auto first = firstLock;
                    PadCheck(LockOn::DebugGunTarget() != 0 && LockOn::DebugGunTarget() != first && pose.weaponType == before.weaponType,
                        std::format("the d-pad's right shifts the lock {:08X} -> {:08X}, the weapon stays {}:", first, LockOn::DebugGunTarget(), pose.weaponType));
                    // The locked guard disabled while LT still holds it, as a script, a respawn or a cell change would: through
                    // SKSE's task queue, outside the player's update (the lock keeps form ids only and looks its actors up again
                    // every frame).
                    disabled = LockOn::DebugGunTarget();
                    SKSE::log::info("hit test: pad: disabling the locked guard {:08X} through SKSE's task queue, LT still held", disabled);
                    SKSE::GetTaskInterface()->AddTask([id = disabled]() {
                        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
                        SKSE::log::info("hit test: pad: task: disabling {:08X}", id);
                        if (actor) {
                            actor->Disable();
                        }
                        SKSE::log::info("hit test: pad: task: {:08X} disabled", id);
                    });
                    state.step = 23;
                    state.timer = 0.0f;
                }
                break;
            case 23:
                if (state.timer >= 2.0f) {
                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(disabled);
                    PadCheck(actor && actor->IsDisabled() && LockOn::DebugGunTarget() != disabled,
                        std::format("the locked guard disabled ({}): the lock is on {:08X} now, and Skyrim keeps going:", actor && actor->IsDisabled() ? "it is" : "it isn't",
                            LockOn::DebugGunTarget()));
                    PlayerSync::DebugPad(Pad::kLeftTrigger, 0.0f);
                    // The other one out of the way until the end, where it's disabled from inside the player's update instead.
                    for (const auto& handle : state.attackers) {
                        auto  ref = handle.get();
                        auto* guard = ref ? ref->As<RE::Actor>() : nullptr;
                        if (guard && !guard->IsDisabled()) {
                            guard->StopCombat();
                            ref->SetPosition(ref->GetPosition() + RE::NiPoint3(6000.0f, 6000.0f, 0.0f));
                        }
                    }
                    tries = PlayerSync::DebugPadCheats();
                    SKSE::log::info("hit test: pad: entering HESOYAM and AEZAKMI as their console sequences");
                    state.step = 30;
                    state.timer = 0.0f;
                }
                break;
            case 30:  // HESOYAM, then AEZAKMI (B, its Circle, tapped: no tween menu), a press every 0.15 s
            {
                static constexpr std::uint32_t kCodes[] = { Pad::kRightShoulder, Pad::kRightTrigger, Pad::kLeftShoulder, Pad::kA, Pad::kLeft, Pad::kDown, Pad::kRight,
                    Pad::kUp, Pad::kLeft, Pad::kDown, Pad::kRight, Pad::kUp, Pad::kB, Pad::kRight, Pad::kB, Pad::kRight, Pad::kLeft, Pad::kX, Pad::kY, Pad::kUp };
                static std::size_t pressed = 0;
                const auto         at = static_cast<std::size_t>(state.timer / 0.15f);
                while (pressed < std::size(kCodes) && pressed <= at) {
                    PlayerSync::DebugPad(kCodes[pressed], 1.0f);
                    PlayerSync::DebugPad(kCodes[pressed], 0.0f);
                    pressed++;
                }
                if (pressed >= std::size(kCodes) && state.timer >= 4.0f) {
                    auto*      ui = RE::UI::GetSingleton();
                    const bool tween = ui && ui->IsMenuOpen(RE::TweenMenu::MENU_NAME);
                    const int  done = PlayerSync::DebugPadCheats() - tries;
                    PadCheck(done == 2 && !tween, std::format("the console sequences finish {} cheats (2 wanted), B's taps open {}:", done, tween ? "the tween menu" : "nothing"));
                    state.step = 40;
                    state.timer = 0.0f;
                }
                break;
            }
            case 40:  // the drive starts back where the coc put the player, facing the most room (wedged in Riverwood otherwise)
                if (PlayerSync::DebugOwned()) {
                    PlayerSync::DebugToggle();
                }
                state.step = 41;
                state.timer = 0.0f;
                break;
            case 41:
                if (state.timer >= 1.5f) {
                    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                        float      clear = 0.0f;
                        const auto heading = ClearHeading(player, start, clear);
                        player->SetPosition(start, true);
                        player->data.angle.z = heading;
                        SKSE::log::info("hit test: pad: the drive starts at the coc spot, heading {:.2f}, {:.0f} m clear ahead", heading, clear);
                    }
                    state.step = 42;
                    state.timer = 0.0f;
                }
                break;
            case 42:
                if (state.timer >= 1.5f) {
                    SKSE::log::info("hit test: pad: CJ into a Sandking there");
                    PlayerSync::TakeOverInVehicle(kSandking);
                    state.step = 11;
                    state.timer = 0.0f;
                }
                break;
            case 11:  // RT accelerates
                if (valid && (pose.flags & Protocol::kPlayerInVehicle) && state.timer >= 4.0f) {
                    before = pose;
                    peak = 0.0f;
                    PlayerSync::DebugPad(Pad::kRightTrigger, 1.0f);
                    PadNext();
                } else if (state.timer > 30.0f) {
                    PadCheck(false, "CJ got into the Sandking:");
                    state.phase = Phase::Done;
                }
                break;
            case 12:
                peak = (std::max)(peak, std::sqrt(pose.velocity[0] * pose.velocity[0] + pose.velocity[1] * pose.velocity[1]) * Pose::kMetersPerUnit * 3.6f);
                if (state.timer >= 3.0f) {
                    const float moved = Meters(before.offset, pose.offset);
                    PadCheck(peak > 15.0f && moved > 3.0f, std::format("RT 3 s drives the Sandking {:.0f} m, at up to {:.0f} km/h:", moved, peak));
                    PlayerSync::DebugPad(Pad::kRightTrigger, 0.0f);
                    PlayerSync::DebugPad(Pad::kRightShoulder, 1.0f);  // RB, the handbrake, to stop
                    before = pose;
                    PlayerSync::DebugPad(Pad::kUp, 1.0f);
                    PadNext();
                }
                break;
            case 13:
                if (state.timer >= 0.2f) {
                    PlayerSync::DebugPad(Pad::kUp, 0.0f);
                    PadNext();
                }
                break;
            case 14:  // the d-pad's up: the next station
                if (state.timer >= 2.0f) {
                    PadCheck(pose.radioStation != before.radioStation, std::format("the d-pad's up tunes the radio from {} to {}:", before.radioStation, pose.radioStation));
                    PlayerSync::DebugPad(Pad::kRightShoulder, 0.0f);
                    PlayerSync::DebugPad(Pad::kY, 1.0f);
                    PadNext();
                }
                break;
            case 15:  // Y gets out
                if (state.timer >= 0.4f) {
                    PlayerSync::DebugPad(Pad::kY, 0.0f);
                }
                if ((onFoot && state.timer >= 0.4f) || state.timer > 10.0f) {
                    PadCheck(onFoot, "Y gets CJ out:");
                    rounds = 0;
                    state.step = 50;
                    state.timer = 0.0f;
                }
                break;
            case 50:  // in and out with Y five times, watching that Skyrim keeps going (a stall here once stopped its log)
                // San Andreas ignores Y while CJ is still climbing in or out (its enter and leave tasks): wait for those.
                if (state.timer >= 1.5f && !(pose.flags & Protocol::kPlayerGettingInOut)) {
                    PlayerSync::DebugPad(Pad::kY, 1.0f);
                    PadNext();
                }
                break;
            case 51:
                if (state.timer >= 0.3f) {
                    PlayerSync::DebugPad(Pad::kY, 0.0f);
                }
                if ((valid && (pose.flags & Protocol::kPlayerInVehicle) && state.timer >= 0.3f) || state.timer > 8.0f) {
                    if (!(valid && (pose.flags & Protocol::kPlayerInVehicle))) {
                        PadCheck(false, std::format("round {}: Y gets CJ into the Sandking:", rounds + 1));
                    }
                    state.step = 52;
                    state.timer = 0.0f;
                }
                break;
            case 52:
                if (state.timer >= 1.5f && !(pose.flags & Protocol::kPlayerGettingInOut)) {
                    PlayerSync::DebugPad(Pad::kY, 1.0f);
                    PadNext();
                }
                break;
            case 53:
                if (state.timer >= 0.3f) {
                    PlayerSync::DebugPad(Pad::kY, 0.0f);
                }
                if ((onFoot && state.timer >= 0.3f) || state.timer > 8.0f) {
                    rounds++;
                    SKSE::log::info("hit test: pad: round {}: CJ {} with Y", rounds, onFoot ? "got in and out" : "didn't get out");
                    if (rounds < 5) {
                        state.step = 50;
                        state.timer = 0.0f;
                        break;
                    }
                    PadCheck(onFoot, "five rounds in and out of the Sandking with Y, Skyrim still going:");
                    state.step = 60;
                    state.timer = 0.0f;
                }
                break;
            case 16:  // Back held: Skyrim has the player again
                if (state.timer >= 1.5f) {
                    PlayerSync::DebugPad(Pad::kBack, 0.0f);
                    PadCheck(!PlayerSync::DebugOwned(), "Back held 1.5 s hands the player back to Skyrim:");
                    SKSE::log::info("hit test: done");
                    state.phase = Phase::Done;
                }
                break;
            case 60:  // the other guard back, set on CJ, locked on with LT, then disabled from inside the player's update
                for (const auto& handle : state.attackers) {
                    auto  ref = handle.get();
                    auto* guard = ref ? ref->As<RE::Actor>() : nullptr;
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (guard && player && !guard->IsDisabled()) {
                        const float h = pose.viewHeading;  // in front of the camera, for the lock
                        ref->SetPosition(player->GetPosition() + RE::NiPoint3(std::sin(h) * 600.0f, std::cos(h) * 600.0f, 0.0f));
                        guard->StartCombat(player);
                        disabled = guard->GetFormID();
                    }
                }
                PlayerSync::DebugPad(Pad::kLeftTrigger, 0.9f);
                state.step = 61;
                state.timer = 0.0f;
                break;
            case 61:
                if (state.timer >= 3.0f) {
                    // Disabled directly while locked. Not SetDelete in the same frame: an actor disabled and deleted at once from inside
                    // the player's update froze Skyrim the frame after, lock or no lock (two runs: Disable and SetDelete froze with
                    // nobody locked on; Disable alone, from here or SKSE's task queue, locked or not, didn't). The plugin's own code
                    // never does that (Police disables an actor and deletes only what isn't one, a sweep later).
                    auto* guard = RE::TESForm::LookupByID<RE::Actor>(disabled);
                    SKSE::log::info("hit test: pad: disabling {:08X} directly from the player's update, the lock on {:08X}, {}", disabled, LockOn::DebugGunTarget(),
                        guard && guard->IsInCombat() ? "in combat" : "not in combat");
                    if (guard) {
                        guard->Disable();
                    }
                    SKSE::log::info("hit test: pad: the direct Disable returned");
                    state.attackers.clear();
                    state.step = 62;
                    state.timer = 0.0f;
                }
                break;
            case 62:
                if (state.timer >= 2.0f) {
                    PadCheck(LockOn::DebugGunTarget() != disabled, std::format("2 s later Skyrim keeps going, the lock on {:08X}:", LockOn::DebugGunTarget()));
                    PlayerSync::DebugPad(Pad::kLeftTrigger, 0.0f);
                    PlayerSync::DebugPad(Pad::kBack, 1.0f);
                    state.step = 16;
                    state.timer = 0.0f;
                }
                break;
            default: break;
            }
        }

        // ---- The riders scenario ----

        // One of Whiterun's guards beside the player, made a follower, to ride in the Sandking's passenger seat.
        void PlaceFollower(RE::PlayerCharacter* a_player)
        {
            if (state.crewRider) {
                // The crew's homie instead: CJ on foot first so the spots fill, then one of them joins (hit-test.ps1 -Scenario ridecrew).
                if (!PlayerSync::DebugOwned()) {
                    SKSE::log::info("hit test: riders: handing the player over to CJ on foot for the crew");
                    PlayerSync::DebugToggle();
                    state.timer = 6.0f;
                    return;
                }
                if (state.timer < 18.0f) {
                    return;
                }
                const int joined = Homies::DebugRecruit(a_player, 2);  // the crew of two one title allows
                SKSE::log::info("hit test: riders: {} homies about, {} joined", Homies::DebugCount(false), joined);
                state.homies.clear();
                for (auto* homie : Homies::DebugFollowers()) {
                    state.homies.push_back(homie->GetHandle());
                    SKSE::log::info("hit test: riders: homie {} ({:08X}) follows CJ", homie->GetName(), homie->GetFormID());
                }
                if (!state.homies.empty()) {
                    state.follower = state.homies.front();
                    state.timer = 2.0f;  // a few seconds to walk up to CJ before the Huntley (Sanchez)
                } else if (state.timer >= 30.0f) {
                    SKSE::log::warn("hit test: could not recruit a homie");
                    state.phase = Phase::Done;
                }
                return;
            }
            auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(Wanted::kHolds[0].guard);
            auto  ref = base ? a_player->PlaceObjectAtMe(base, false) : nullptr;
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            if (!actor) {
                SKSE::log::warn("hit test: couldn't place a follower");
                return;
            }
            const float heading = a_player->GetAngleZ();
            ref->SetPosition(a_player->GetPosition() + RE::NiPoint3(std::cos(heading) * 140.0f, -std::sin(heading) * 140.0f, 0.0f));
            actor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
            state.follower = ref->GetHandle();
            SKSE::log::info("hit test: placed {} ({:08X}) beside the player as a follower", actor->GetName(), actor->GetFormID());
        }

        // a_at in the vehicle's frame as last drawn (Riders), metres.
        bool InVehicleFrame(const RE::NiPoint3& a_at, Pose::Vec3& a_out)
        {
            RE::NiPoint3       feet;
            Protocol::BodyBone frame{};
            if (!Riders::DebugVehicle(feet, frame)) {
                return false;
            }
            const auto local = Passengers::Unrotate(Passengers::QuatOf(frame), { a_at.x - feet.x, a_at.y - feet.y, a_at.z - feet.z });
            a_out = { local.x * Pose::kMetersPerUnit, local.y * Pose::kMetersPerUnit, local.z * Pose::kMetersPerUnit };
            return true;
        }

        void Report(const char* a_who, const std::vector<CarExit::Sample>& a_trace, bool a_judge)
        {
            const auto t = CarExit::Check(a_trace);
            SKSE::log::info("hit test: {} got out: {} frames, biggest step {:.2f} m, back towards the middle {:.2f} m, out {:.2f} -> {:.2f} m ({:.2f} when let go){}",
                a_who, t.samples, t.maxStep, t.maxBack, t.startOut, t.endOut, t.outAtRelease, !a_judge ? "" : CarExit::Smooth(t) ? ": smooth" : ": NOT SMOOTH");
        }

        // Who rides with CJ now, from the follower (riders) or the crew's followers (ridecrew): logged, and traced from F.
        void TakePassengers()
        {
            state.passengers.clear();
            for (const auto& handle : state.crewRider ? state.homies : std::vector<RE::ObjectRefHandle>{ state.follower }) {
                auto  ref = handle.get();
                auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                if (!actor) {
                    continue;
                }
                const bool sits = Riders::Sits(actor->GetFormID());
                if (state.bike && !Riders::Rides(actor->GetFormID())) {
                    SKSE::log::info("hit test: {} ({:08X}) stays off, the Sanchez's one pillion taken", actor->GetName(), actor->GetFormID());
                    continue;
                }
                Pose::Vec3 at{};
                InVehicleFrame(actor->GetPosition(), at);
                SKSE::log::info("hit test: {} ({:08X}) {} at {:.2f} {:.2f} {:.2f} in the vehicle's frame", actor->GetName(), actor->GetFormID(),
                    sits ? "sits" : Riders::Rides(actor->GetFormID()) ? "rides, not sitting" : "NEVER SAT", at.x, at.y, at.z);
                state.passengers.push_back({ handle, std::format("{} ({:08X})", actor->GetName(), actor->GetFormID()) });
            }
            if (state.bike && state.passengers.empty()) {
                SKSE::log::warn("hit test: NOBODY RIDES the Sanchez's pillion");
            }
        }

        // Every frame from F until all have been out a while: each passenger's feet in the vehicle's frame (up to the frame
        // Riders lets him go: a homie walks off to his formation behind CJ then), and CJ's; whether a passenger still sat
        // while CJ was getting out (a homie's kCrewSeated, which sits his puppet in San Andreas); and, for the crew, where
        // each stands once let go: on the ground a second later, then in his place behind CJ.
        void TraceLeaving(RE::PlayerCharacter* a_player, float a_delta)
        {
            Protocol::PlayerState pose{};
            const bool cjIn = InVehicle();
            const bool cjLeaving = cjIn && LinkService::Get().ReadPlayer(pose) && (pose.flags & Protocol::kPlayerGettingInOut) != 0;
            Pose::Vec3 cj{};
            if (InVehicleFrame(a_player->GetPosition(), cj)) {
                state.cjTrace.push_back({ cj, cjIn });
            }
            bool        allOut = !cjIn;
            std::string line;
            for (auto& p : state.passengers) {
                auto       ref = p.handle.get();
                auto*      actor = ref ? ref->As<RE::Actor>() : nullptr;
                const bool riding = actor && Riders::Rides(actor->GetFormID());
                const bool sits = actor && Riders::Sits(actor->GetFormID());
                Pose::Vec3 at{};
                if (actor && InVehicleFrame(actor->GetPosition(), at) && p.letGo < 0.0f) {
                    p.trace.push_back({ at, riding });
                }
                if ((cjLeaving || !cjIn) && sits) {
                    p.seatedLeaving++;
                }
                if (actor && !riding) {
                    if (p.letGo < 0.0f) {
                        p.letGo = 0.0f;
                        p.letGoZ = actor->GetPosition().z;
                    } else {
                        p.letGo += a_delta;
                    }
                    if (p.letGo >= 1.0f && !p.groundChecked) {
                        auto* controller = actor->GetCharController();
                        p.groundChecked = true;
                        p.grounded = controller && controller->context.currentState == RE::hkpCharacterStateType::kOnGround;
                        p.drop = (p.letGoZ - actor->GetPosition().z) * Pose::kMetersPerUnit;
                    }
                }
                allOut = allOut && !riding;
                line += std::format(", {} {:6.2f} {:6.2f} {:6.2f} {}", actor ? actor->GetName() : "(gone)", at.x, at.y, at.z, sits ? "sits" : riding ? "steps out" : "out");
            }
            SKSE::log::info("hit test: leaving {:5.0f} ms, CJ {:6.2f} {:6.2f} {:6.2f} {}{}{}", state.timer * 1000.0f, cj.x, cj.y, cj.z, cjIn ? "in" : "out",
                cjLeaving ? " getting out" : "", line);
            state.outFor = allOut ? state.outFor + a_delta : 0.0f;
            // The crew gets six seconds out to fall in behind CJ; a follower of Skyrim's is let go where he stands.
            if (state.outFor >= (state.crewRider ? 6.0f : 1.0f) || state.timer > 16.0f) {
                const float h = a_player->GetAngleZ();
                for (std::size_t i = 0; i < state.passengers.size(); i++) {
                    auto& p = state.passengers[i];
                    Report(p.name.c_str(), p.trace, true);
                    SKSE::log::info("hit test: {}: {} frames still seated while CJ got out or was out{}", p.name, p.seatedLeaving, p.seatedLeaving == 0 ? ": none" : ": SEATED THROUGH HIS GET-OUT");
                    if (!state.crewRider) {
                        continue;
                    }
                    auto  ref = p.handle.get();
                    auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                    SKSE::log::info("hit test: {}: a second after Riders let him go {}, {:.2f} m lower{}", p.name,
                        p.groundChecked ? (p.grounded ? "on the ground" : "not on the ground") : "never let go", p.drop,
                        p.groundChecked && p.grounded && std::fabs(p.drop) <= 0.5f ? ": landed" : ": NOT LANDED");
                    if (!actor) {
                        continue;
                    }
                    // In CJ's frame (the player's heading, which KeepOffsetFromActor keeps the formation to): x right, y ahead.
                    const auto  d = actor->GetPosition() - a_player->GetPosition();
                    const float right = (d.x * std::cos(h) - d.y * std::sin(h)) * Pose::kMetersPerUnit;
                    const float ahead = (d.x * std::sin(h) + d.y * std::cos(h)) * Pose::kMetersPerUnit;
                    float       fromPlace = 1e9f;
                    for (int j = 0; j < Crew::kFollowerMax; j++) {  // whichever place Homies gave him
                        const auto place = Crew::FormationAt({ a_player->GetPosition().x, a_player->GetPosition().y, a_player->GetPosition().z }, h, j);
                        fromPlace = (std::min)(fromPlace, std::hypot(actor->GetPosition().x - place.x, actor->GetPosition().y - place.y) * Pose::kMetersPerUnit);
                    }
                    SKSE::log::info("hit test: {}: {:.0f} s after, {:.2f} m right and {:.2f} m ahead of CJ, {:.2f} m from a place of the formation{}", p.name, p.letGo,
                        right, ahead, fromPlace, ahead < 0.0f && fromPlace <= 1.5f ? ": behind him" : ": NOT IN HIS PLACE");
                }
                // San Andreas's line-up moves CJ's position to the door as the get-out begins and his animation keeps his
                // body in the seat, so his feet jump where his body doesn't: logged, not judged (fake_vehicles judges his body).
                Report("CJ (the player's position)", state.cjTrace, false);
                SKSE::log::info("hit test: done after the riders run");
                state.phase = Phase::Done;
            }
        }

        // ---- The drive scenario ----

        // Every frame for a minute while CJ drives the Sandking (W, and A or D a second every 4 s), from a second in (the
        // handover settling): in the vehicle's frame as Skyrim drew it, his feet as drawn, his model's node as Skyrim's
        // scene has it after its update, and Skyrim's camera. His feet and node must not jump in it (CarExit::Seated), nor
        // the camera: a frame his body is drawn behind the vehicle shows here. How far he drove and the cells
        // he crossed are logged too: a car stuck on a rock proves nothing.
        void TraceDriving(RE::PlayerCharacter* a_player)
        {
            const int leg = static_cast<int>(state.timer);
            if (leg != state.legs) {
                state.legs = leg;
                PlayerSync::DebugKey(Input::kDikA, leg % 8 == 2);
                PlayerSync::DebugKey(Input::kDikD, leg % 8 == 6);
            }
            auto* cell = a_player->GetParentCell();
            if (state.lastCell && cell != state.lastCell) {
                state.cells++;
            }
            state.lastCell = cell;
            if (state.timer > 0.0f && state.lastAt != RE::NiPoint3()) {
                state.driven += a_player->GetPosition().GetDistance(state.lastAt) * Pose::kMetersPerUnit;
            }
            state.lastAt = a_player->GetPosition();
            Protocol::BodyState pose{};
            RE::NiPoint3        feet, camera, node, parent;
            Pose::Vec3          f{}, n{}, c{};
            const bool          drawn = PlayerSync::DebugDrawn(pose, feet, camera);
            const bool          shown = drawn && ModelView::DebugWorld(pose.model, node, parent);
            auto*               cameraRoot = RE::PlayerCamera::GetSingleton() ? RE::PlayerCamera::GetSingleton()->cameraRoot.get() : nullptr;
            const bool settled = state.timer >= 1.0f;
            if (drawn && InVehicleFrame(feet, f) && settled) {
                state.feetTrace.push_back({ f, true });
            }
            if (shown && InVehicleFrame(node, n) && settled) {
                state.nodeTrace.push_back({ n, true });
            }
            if (cameraRoot && InVehicleFrame(cameraRoot->world.translate, c) && settled) {
                state.cameraTrace.push_back({ c, true });
            }
            const auto& p = a_player->GetPosition();
            SKSE::log::info("hit test: driving {:5.0f} ms, feet {:6.2f} {:6.2f} {:6.2f}{}, node {:6.2f} {:6.2f} {:6.2f} (its parent at {:.0f} {:.0f} {:.0f}), camera {:6.2f} {:6.2f} {:6.2f}, player at {:.0f} {:.0f} in {:08X}",
                state.timer * 1000.0f, f.x, f.y, f.z, drawn ? "" : " (not drawn)", n.x, n.y, n.z, parent.x, parent.y, parent.z, c.x, c.y, c.z, p.x, p.y,
                a_player->GetParentCell() ? a_player->GetParentCell()->GetFormID() : 0);
            if (state.timer >= state.seconds) {
                PlayerSync::DebugKey(Input::kDikW, false);
                PlayerSync::DebugKey(Input::kDikA, false);
                PlayerSync::DebugKey(Input::kDikD, false);
                for (const auto& [who, trace] : { std::pair{ "his feet as drawn", &state.feetTrace }, std::pair{ "his model's node", &state.nodeTrace },
                         std::pair{ "the camera", &state.cameraTrace } }) {
                    const auto r = CarExit::CheckRide(*trace);
                    // The camera is San Andreas's chase camera: it swings behind the vehicle as it turns, smoothly. And
                    // only a jump counts for CJ too: his feet are straight down from his middle in the world, so they
                    // go round the seat as the vehicle rolls over.
                    const bool camera = trace == &state.cameraTrace;
                    SKSE::log::info("hit test: drove {:.0f} s, {}: {} frames, off where it was at most {:.2f} m (frame {}), biggest step {:.2f} m{}", state.timer,
                        who, r.samples, r.maxOff, r.worstAt, r.maxStep, camera ? (r.maxStep <= 1.0f ? ": smooth" : ": JUMPS") : CarExit::Seated(r, true) ? ": seated" : ": NOT SEATED");
                }
                SKSE::log::info("hit test: drove {:.0f} m through {} cells{}", state.driven, state.cells, state.cells < 3 ? ": TOO SHORT A DRIVE TO TELL" : "");
                SKSE::log::info("hit test: done after the drive");
                state.phase = Phase::Done;
            }
        }

        // ---- The soak scenario ----

        void SoakWolves(RE::PlayerCharacter* a_player)
        {
            const float heading = a_player->GetAngleZ();
            for (int i = 0; i < 2; i++) {
                auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(Wanted::kWolf);
                auto  ref = base ? a_player->PlaceObjectAtMe(base, false) : nullptr;
                if (ref) {
                    const float side = (i == 0 ? -1.0f : 1.0f) * 200.0f;
                    ref->SetPosition(a_player->GetPosition() + RE::NiPoint3(std::sin(heading) * 1050.0f + std::cos(heading) * side,
                                                                   std::cos(heading) * 1050.0f - std::sin(heading) * side, 0.0f));
                }
            }
            state.soakWolves++;
        }

        void SoakStep(RE::PlayerCharacter* a_player, float a_delta)
        {
            constexpr float kFoot = 90.0f, kDrive = 60.0f;
            state.soakT += a_delta;
            state.soakShot += a_delta;
            state.soakLog += a_delta;
            if (state.soakShot >= 30.0f) {  // F11 now and then
                state.soakShot = 0.0f;
                state.soakShots++;
                SKSE::log::info("hit test: soak: F11 screenshot {}", state.soakShots);
                Overlay::Screenshot((std::filesystem::temp_directory_path() / "ahshitrim-hit-test-shots").string());
            }
            if (state.soakLog >= 30.0f) {
                state.soakLog = 0.0f;
                float nearest = -1.0f;
                if (auto* follower = Homies::DebugFollower()) {
                    nearest = follower->GetPosition().GetDistance(a_player->GetPosition()) * Pose::kMetersPerUnit;
                }
                static const char* const segs[] = { "on foot", "getting in", "driving", "getting out" };
                auto* calendar = RE::Calendar::GetSingleton();
                SKSE::log::info("hit test: soak: {:.0f} s, {}, game hour {:.1f}, {} following ({} about, nearest {:.1f} m), player health {:.0f}, armour {:.0f}, {} drives, {} wolf pairs, {} regrouped, in {:08X}, CJ has the player: {}",
                    state.timer, segs[state.soakSeg], calendar ? calendar->GetHour() : 0.0f, Homies::DebugCount(true), Homies::DebugCount(false), nearest,
                    a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth), Vitals::Armor(), state.soakCycles, state.soakWolves, Homies::DebugRegrouped(),
                    a_player->GetParentCell() ? a_player->GetParentCell()->GetFormID() : 0, PlayerSync::DebugOwned() ? "yes" : "NO");
            }
            if (state.timer >= state.seconds) {
                PlayerSync::DebugKey(Input::kDikW, false);
                PlayerSync::DebugKey(Input::kDikA, false);
                PlayerSync::DebugKey(Input::kDikD, false);
                state.moveX = state.moveY = 0.0f;
                RunConsole("set timescale to 20");
                SKSE::log::info("hit test: done after {:.0f} s of soak, {} drives, {} wolf pairs, {} F11 screenshots, {} times never in the vehicle", state.timer, state.soakCycles,
                    state.soakWolves, state.soakShots, state.soakGone);
                state.phase = Phase::Done;
                return;
            }
            switch (state.soakSeg) {
            case 0: {  // on foot: the crew joins at 10 s, wolves at 15 s, CJ walks and runs about
                const float t = state.soakT;
                if (!state.soakRecruited && t >= 10.0f) {
                    state.soakRecruited = true;
                    if (Homies::DebugCount(true) < 2) {
                        const int joined = Homies::DebugRecruit(a_player, 3);
                        SKSE::log::info("hit test: soak: {} joined the crew, {} following", joined, Homies::DebugCount(true));
                    }
                }
                if (!state.soakWolvesSent && t >= 15.0f) {
                    state.soakWolvesSent = true;
                    SoakWolves(a_player);
                }
                const float u = std::fmod(t, 20.0f);
                const bool  leftWard = static_cast<int>(t / 20.0f) % 2 == 1;
                state.moveX = u >= 9.0f && u < 15.0f ? (leftWard ? -0.7f : 0.7f) : 0.0f;
                state.moveY = u < 6.0f ? 0.35f : u >= 9.0f && u < 15.0f ? 1.0f : 0.0f;
                if (t >= kFoot) {
                    state.moveX = state.moveY = 0.0f;
                    SKSE::log::info("hit test: soak: CJ takes the Sandking, {} following", Homies::DebugCount(true));
                    PlayerSync::TakeOverInVehicle(kSandking);
                    state.soakSeg = 1;
                    state.soakT = 0.0f;
                }
                break;
            }
            case 1:
                if (InVehicle()) {
                    PlayerSync::DebugKey(Input::kDikW, true);
                    state.legs = 0;
                    state.soakSeg = 2;
                    state.soakT = 0.0f;
                } else if (state.soakT > 30.0f) {
                    state.soakGone++;
                    SKSE::log::info("hit test: soak: CJ never got into the Sandking, on foot again");
                    state.soakSeg = 0;
                    state.soakT = 0.0f;
                    state.soakRecruited = state.soakWolvesSent = false;
                }
                break;
            case 2: {
                const int leg = static_cast<int>(state.soakT);
                if (leg != state.legs) {
                    state.legs = leg;
                    PlayerSync::DebugKey(Input::kDikA, leg % 8 == 2);
                    PlayerSync::DebugKey(Input::kDikD, leg % 8 == 6);
                }
                if (state.soakT >= kDrive) {
                    PlayerSync::DebugKey(Input::kDikW, false);
                    PlayerSync::DebugKey(Input::kDikA, false);
                    PlayerSync::DebugKey(Input::kDikD, false);
                    PlayerSync::DebugKey(Input::kDikF, true);
                    SKSE::log::info("hit test: soak: F, CJ gets out ({} following)", Homies::DebugCount(true));
                    state.soakSeg = 3;
                    state.soakT = 0.0f;
                }
                break;
            }
            default:
                if (state.soakT >= 0.3f) {
                    PlayerSync::DebugKey(Input::kDikF, false);
                }
                if (state.soakT >= 8.0f) {
                    state.soakCycles++;
                    state.soakSeg = 0;
                    state.soakT = 0.0f;
                    state.soakRecruited = state.soakWolvesSent = false;
                }
                break;
            }
        }

        // ---- The stealth scenario (v38) ----

        bool OnFootPose(Protocol::PlayerState& a_pose)
        {
            return LinkService::Get().ReadPlayer(a_pose) && (a_pose.flags & Protocol::kPlayerValid) && !(a_pose.flags & Protocol::kPlayerInVehicle);
        }

        // CJ's loadout: only the knife, in his hands.
        void GiveKnife()
        {
            Protocol::WeaponCommand command{};
            command.kind = static_cast<std::uint32_t>(Protocol::WeaponCommandKind::SetLoadout);
            command.argument = 1;  // the melee slot
            command.entries[1].type = Weapons::Knife;
            command.entries[1].ammo = 1;
            const bool sent = LinkService::Get().Send(Protocol::MessageType::WeaponCommand, &command, sizeof(command));
            SKSE::log::info("hit test: {} CJ the knife", sent ? "gave" : "couldn't give");
        }

        RE::Actor* PlaceAt(RE::PlayerCharacter* a_player, std::uint32_t a_base, const RE::NiPoint3& a_at, float a_heading)
        {
            auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(a_base);
            auto  ref = base ? a_player->PlaceObjectAtMe(base, false) : nullptr;
            if (!ref) {
                SKSE::log::warn("hit test: couldn't place {:08X}", a_base);
                return nullptr;
            }
            ref->SetPosition(a_at);
            ref->SetAngle(RE::NiPoint3(0.0f, 0.0f, a_heading));
            state.attackers.push_back(ref->GetHandle());
            SKSE::log::info("hit test: placed {} ({:08X})", ref->GetName(), ref->GetFormID());
            return ref->As<RE::Actor>();
        }

        // A guard just ahead of CJ with his back to him, and one to the right facing CJ. (A wolf would set on CJ and be
        // the lock-on's first choice, as a hostile is.)
        void PlaceVictims(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_feet, float a_heading)
        {
            const RE::NiPoint3 ahead(std::sin(a_heading), std::cos(a_heading), 0.0f);
            const RE::NiPoint3 right(std::cos(a_heading), -std::sin(a_heading), 0.0f);
            if (auto* guard = PlaceAt(a_player, Wanted::kHolds[0].guard, a_feet + ahead * state.victimAhead, a_heading)) {
                state.victim = guard->GetHandle();
            }
            PlaceAt(a_player, Wanted::kHolds[0].guard, a_feet + ahead * state.victimAhead + right * kOthersAside, a_heading + Pose::kPi);
        }

        // Skyrim's sneak kill moves, by editor id and the event that plays them: what a victim could play.
        void LogKillMoves()
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) {
                return;
            }
            int found = 0;
            for (auto* idle : data->GetFormArray<RE::TESIdleForm>()) {
                const char* id = idle ? idle->GetFormEditorID() : nullptr;
                std::string lower = id ? id : "";
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find("killmove") == std::string::npos || lower.find("sneak") == std::string::npos || found >= 60) {
                    continue;
                }
                found++;
                SKSE::log::info("hit test: kill move idle {} ({:08X}), event '{}', file '{}', parent {}", id, idle->GetFormID(), idle->animEventName.c_str(),
                    idle->animFileName.c_str(), idle->parentIdle ? idle->parentIdle->GetFormEditorID() : "none");
            }
            SKSE::log::info("hit test: {} sneak kill move idles", found);
        }

        // What StealthKill::Check makes of each one placed, from where the player (CJ) stands.
        void LogVerdicts(RE::PlayerCharacter* a_player)
        {
            const auto feet = a_player->GetPosition();
            for (const auto& handle : state.attackers) {
                auto  ref = handle.get();
                auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                if (!actor) {
                    continue;
                }
                const auto target = StealthKills::TargetOf(actor, a_player);
                SKSE::log::info("hit test: {} ({:08X}) {}: {} at {:.2f} m, {}, detection {}, health {:.0f}", actor->GetName(), actor->GetFormID(),
                    actor->IsDead() ? "dead" : "alive", StealthKill::Why(StealthKill::Check({ feet.x, feet.y, feet.z }, Weapons::Knife, target)),
                    StealthKill::Distance({ feet.x, feet.y, feet.z }, target.feet), StealthKill::Behind({ feet.x, feet.y, feet.z }, target.feet, target.heading) ? "back to CJ" : "facing him",
                    actor->RequestDetectionLevel(a_player), actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth));
            }
            if (auto* nearby = state.nearRef ? RE::TESForm::LookupByID<RE::Actor>(state.nearRef) : nullptr; nearby && nearby->Is3DLoaded()) {
                const auto target = StealthKills::TargetOf(nearby, a_player);
                const auto off = StealthKills::OffItsFeet(nearby);
                SKSE::log::info("hit test: near {} ({:08X}): {}{}{} at {:.2f} m, detection {}", nearby->GetName(), nearby->GetFormID(),
                    StealthKill::Why(StealthKill::Check({ feet.x, feet.y, feet.z }, Weapons::Knife, target)), off.empty() ? "" : ", ", off,
                    StealthKill::Distance({ feet.x, feet.y, feet.z }, target.feet), nearby->RequestDetectionLevel(a_player));
            }
        }

        // Every actor about that a stealth kill would call off its feet, and why (Skyrim's sit, knock and ragdoll states).
        void LogOffTheirFeet(RE::PlayerCharacter* a_player)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            int about = 0;
            for (auto& handle : lists->highActorHandles) {
                auto actor = handle.get();
                if (!actor || actor.get() == a_player || actor->IsDead()) {
                    continue;
                }
                about++;
                if (const auto off = StealthKills::OffItsFeet(actor.get()); !off.empty()) {
                    SKSE::log::info("hit test: {} ({:08X}) is off its feet: {}", actor->GetName(), actor->GetFormID(), off);
                }
            }
            SKSE::log::info("hit test: {} actors about", about);
        }

        // From Arriving on: CJ on foot with the knife, the three placed, then the right button held and the left one
        // clicked as a player would, the verdicts and the victim logged as it goes.
        void Stealth(RE::PlayerCharacter* a_player, float a_delta)
        {
            Protocol::PlayerState pose{};
            const bool onFoot = OnFootPose(pose);
            state.logTimer += a_delta;
            if (state.step >= 3 && state.logTimer >= 1.0f) {
                state.logTimer = 0.0f;
                LogVerdicts(a_player);
            }
            switch (state.step) {
            case 0:  // hand over on foot
                if (state.nearRef && !state.movedNear && state.timer >= 1.0f) {
                    RunConsole(std::format("player.moveto {:08X}", state.nearRef));
                    state.movedNear = true;
                    state.timer = 0.0f;
                }
                if (state.nearRef && state.logTimer >= 1.0f) {
                    state.logTimer = 0.0f;
                    LogVerdicts(a_player);
                }
                if (state.timer >= 6.0f) {
                    if (!PlayerSync::DebugOwned()) {
                        PlayerSync::DebugToggle();
                    }
                    SKSE::log::info("hit test: handing the player over on foot");
                    state.step = 1;
                    state.timer = 0.0f;
                }
                break;
            case 1:  // the knife
                if (onFoot && state.timer >= 4.0f) {
                    LogKillMoves();
                    GiveKnife();
                    state.step = 2;
                    state.timer = 0.0f;
                } else if (state.timer > 40.0f) {
                    SKSE::log::info("hit test: CJ never came on foot, stopping");
                    state.phase = Phase::Done;
                }
                break;
            case 2:  // in his hands, then the three
                if (onFoot && pose.weaponType == Weapons::Knife && state.timer >= 2.0f) {
                    SKSE::log::info("hit test: CJ holds the knife, placing a guard {:.1f} m ahead with his back to him", state.victimAhead * Pose::kMetersPerUnit);
                    PlaceVictims(a_player, a_player->GetPosition(), pose.heading);
                    LogOffTheirFeet(a_player);
                    state.step = 3;
                    state.timer = 0.0f;
                } else if (state.timer > 20.0f) {
                    SKSE::log::info("hit test: CJ never held the knife (weapon {}), stopping", pose.weaponType);
                    state.phase = Phase::Done;
                }
                break;
            case 3:  // the right button
                if (state.timer >= 3.0f) {
                    SKSE::log::info("hit test: the right mouse button held");
                    PlayerSync::DebugButtons(true, false);
                    state.step = 4;
                    state.timer = 0.0f;
                }
                break;
            case 4:  // the left one, clicked
                if (state.timer >= 1.5f) {
                    SKSE::log::info("hit test: the left mouse button clicked");
                    PlayerSync::DebugButtons(true, true);
                    state.step = 5;
                    state.timer = 0.0f;
                }
                break;
            case 5:
                if (state.timer >= 0.2f) {
                    PlayerSync::DebugButtons(true, false);
                    state.step = 6;
                    state.timer = 0.0f;
                }
                break;
            case 6:  // let go, and watch
                if (state.timer >= 3.0f) {
                    PlayerSync::DebugButtons(false, false);
                }
                if (state.timer >= state.seconds) {
                    auto  ref = state.victim.get();
                    auto* victim = ref ? ref->As<RE::Actor>() : nullptr;
                    SKSE::log::info("hit test: done after {:.0f} s, the guard behind is {}", state.timer, victim && victim->IsDead() ? "dead" : "alive");
                    state.phase = Phase::Done;
                }
                break;
            default: break;
            }
        }

        // Respawns: what a kill would do to everyone placed in the editor nearby, and the first few that come back
        // (and one that doesn't, if any) killed by the player.
        void KillAround(RE::PlayerCharacter* a_player)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            std::vector<RE::Actor*> back, stay;
            for (auto& handle : lists->highActorHandles) {
                auto  ref = handle.get();
                auto* actor = ref.get();
                if (!actor || actor == a_player || actor->IsDead() || actor->GetFormID() >= 0xFF000000 || actor->IsPlayerTeammate() ||
                    actor->GetPosition().GetDistance(a_player->GetPosition()) > 6000.0f) {
                    continue;
                }
                const std::string why = Respawns::DryRun(actor, a_player);
                SKSE::log::info("hit test: {} ({:08X}) killed by CJ: {}", actor->GetDisplayFullName(), actor->GetFormID(), why);
                (why.starts_with("back") ? back : stay).push_back(actor);
            }
            if (back.size() > 3) {
                back.resize(3);
            }
            if (!stay.empty()) {
                back.push_back(stay.front());
            }
            for (auto* actor : back) {
                state.victims.push_back(actor->GetFormID());
                actor->KillImpl(a_player, 100000.0f, true, true);
                SKSE::log::info("hit test: the player killed {} ({:08X})", actor->GetDisplayFullName(), actor->GetFormID());
            }
        }

        void Survey()
        {
            for (const auto id : state.victims) {
                auto*        actor = RE::TESForm::LookupByID<RE::Actor>(id);
                RE::NiPoint3 at, angle;
                RE::TESForm* where = nullptr;
                const bool   home = actor && actor->GetEditorLocation(at, angle, where, nullptr);
                SKSE::log::info("hit test: {:08X} {} is {}{}", id, actor ? actor->GetDisplayFullName() : "?", !actor ? "not in memory" : actor->IsDead() ? "dead" : "up",
                    home && actor && !actor->IsDead() ? std::format(", {:.0f} units from the editor spot", actor->GetPosition().GetDistance(at)) : "");
            }
        }

        float HealthOf(RE::TESObjectREFR* a_ref)
        {
            const auto* health = a_ref->extraList.GetByType<RE::ExtraObjectHealth>();
            return health ? health->health : 100.0f;
        }

        // Three fresh oil pools (Skyrim's TrapOilPool01) placed on the floor where the save's own may be burnt out already:
        // at three of a row of pools in BleakFallsBarrow01, 3 m apart. A bullet lands in the first, a Molotov bursts on the
        // second, a rocket on the third, each through what CJ's own shots and projectiles call (ObjectHits).
        constexpr std::uint32_t kOilPool = 0x000862CC;
        constexpr std::uint32_t kOilSpots[3] = { 0x0009B0E6, 0x0009B0E0, 0x0009B0DE };

        void PlaceOil()
        {
            auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kOilPool);
            for (const auto spot : kOilSpots) {
                auto* at = RE::TESForm::LookupByID<RE::TESObjectREFR>(spot);
                auto  made = base && at ? at->PlaceObjectAtMe(base, false) : nullptr;
                if (!made) {
                    SKSE::log::warn("hit test: couldn't place oil at {:08X}", spot);
                    continue;
                }
                state.pools.push_back({ made->GetHandle() });
                SKSE::log::info("hit test: placed the oil {:08X} at ({:.0f}, {:.0f}, {:.0f})", made->GetFormID(), made->GetPositionX(), made->GetPositionY(), made->GetPositionZ());
            }
        }

        void LightOil(RE::PlayerCharacter* a_player)
        {
            const char* by[] = { "a bullet", "a Molotov", "a rocket" };
            for (std::size_t i = 0; i < state.pools.size() && i < 3; i++) {
                auto  ref = state.pools[i].ref.get();
                auto* root = ref ? ref->Get3D() : nullptr;
                state.pools[i].by = by[i];
                if (!root) {
                    SKSE::log::info("hit test: the oil for {} has no 3D", by[i]);
                    continue;
                }
                const auto at = root->worldBound.center;
                SKSE::log::info("hit test: {} on the oil {:08X} at ({:.0f}, {:.0f}, {:.0f}), health {:.0f}", by[i], ref->GetFormID(), at.x, at.y, at.z, HealthOf(ref.get()));
                if (i == 0) {
                    ObjectHits::BulletLanded(a_player, at, Weapons::Pistol);
                } else {
                    ObjectHits::Around(a_player, at, 5.0f,  // just the pool, not its neighbours
                         i == 1 ? Traps::Hit::Fire : Traps::Hit::Blast, 0.0f);
                }
            }
        }

        // True once every pool burnt out.
        bool WatchOil()
        {
            bool all = true;
            for (auto& pool : state.pools) {
                const auto ref = pool.ref.get();
                if (!ref || ref->IsDisabled()) {
                    pool.gone = true;
                    continue;
                }
                const float health = HealthOf(ref.get());
                pool.lowest = (std::min)(pool.lowest, health);
                pool.burned = pool.burned || (health < 94.0f && health > 7.0f);
                all = false;
            }
            return all;
        }
    }

    bool Drive(float& a_moveX, float& a_moveY)
    {
        if (state.phase != Phase::CrewWalk && !(state.phase == Phase::Soak && state.soakSeg == 0)) {
            return false;
        }
        a_moveX = state.moveX;
        a_moveY = state.moveY;
        return true;
    }

    void OnDataLoaded()
    {
        const auto scenario = Ini::Value("Debug", "HitTest");
        if (scenario != "hits" && scenario != "bust" && scenario != "stealth" && scenario != "respawns" && scenario != "crew" && scenario != "pad" && scenario != "riders" && scenario != "ridecrew" && scenario != "ridebike" && scenario != "armour" && scenario != "oil" && scenario != "drive" && scenario != "soak" && scenario != "lights") {
            return;
        }
        state.crew = scenario == "crew";
        state.soak = scenario == "soak";
        if (state.crew || state.soak) {
            Homies::DebugThanes(1);  // Thane of Whiterun; hit-test.ps1 starts it at the Whiterun stables
        }
        state.bust = scenario == "bust";
        state.stealth = scenario == "stealth";
        state.respawns = scenario == "respawns";
        state.pad = scenario == "pad";
        state.riders = scenario == "riders" || scenario == "ridecrew" || scenario == "ridebike";
        state.crewRider = scenario == "ridecrew" || scenario == "ridebike";
        state.bike = scenario == "ridebike";
        state.armour = scenario == "armour";
        state.oil = scenario == "oil";
        state.lights = scenario == "lights";
        if (const auto vehicle = Ini::Value("Debug", "HitTestVehicle"); !vehicle.empty()) {
            try {
                state.vehicle = static_cast<std::uint16_t>(std::stoi(vehicle));
            } catch (...) {
            }
        }
        state.shotsDir = Ini::Value("Debug", "HitTestShots");
        if (const auto at = Ini::Value("Debug", "HitTestOilAt"); !at.empty()) {
            state.oilAt = at;
        }
        if (state.crewRider) {
            Homies::DebugThanes(1);
        }
        state.drive = scenario == "drive";
        state.seconds = state.stealth ? 12.0f : state.drive ? 60.0f : state.seconds;
        if (const auto start = Ini::Value("Debug", "HitTestStart"); !start.empty()) {
            state.start = start;
        }
        if (const auto away = Ini::Value("Debug", "HitTestAway"); !away.empty()) {
            state.away = away;
        }
        if (const auto seconds = Ini::Value("Debug", "HitTestSeconds"); !seconds.empty()) {
            try {
                state.seconds = std::stof(seconds);
            } catch (...) {
            }
        }
        state.newGame = Ini::Value("Debug", "HitTestNewGame") == "1";
        try {
            if (const auto meters = Ini::Value("Debug", "HitTestStealthMeters"); !meters.empty()) {
                state.victimAhead = std::stof(meters) * Pose::kUnitsPerMeter;
            }
            if (const auto nearId = Ini::Value("Debug", "HitTestNear"); !nearId.empty()) {
                state.nearRef = static_cast<RE::FormID>(std::stoul(nearId, nullptr, 16));
            }
        } catch (...) {
        }
        state.phase = Phase::MainMenu;
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuSink);
        }
        SKSE::log::info("hit test: on ({}), from {} for {:.0f} s{}", scenario, state.start, state.seconds, state.newGame ? ", a new game" : "");
    }

    void Update(RE::PlayerCharacter* a_player, float a_delta)
    {
        if (state.phase == Phase::Off || state.phase == Phase::Done || a_delta <= 0.0f) {
            return;
        }
        auto* ui = RE::UI::GetSingleton();
        if ((ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) || !a_player->Is3DLoaded() || !a_player->GetParentCell()) {
            state.timer = 0.0f;
            return;
        }
        state.timer += a_delta;
        if (state.phase >= Phase::CrewFight && state.phase <= Phase::CrewMoved) {
            LogFollower(a_player, a_delta);
        }
        switch (state.phase) {
        case Phase::MainMenu:
        case Phase::Loading:
            if (state.timer >= 4.0f) {
                RunConsole(state.start.starts_with("cow ") ? state.start : "coc " + state.start);
                state.phase = Phase::Going;
                state.timer = 0.0f;
            }
            break;
        case Phase::Going:
            state.phase = Phase::Arriving;  // the coc's load screen comes and goes; timer counts from after it
            state.timer = 0.0f;
            break;
        case Phase::Arriving:
            if (state.oil) {
                if (state.oil && state.timer >= 4.0f && !state.oilMoved) {
                    state.oilMoved = true;
                    RunConsole("tgm");  // the player stands in the fire
                    RunConsole("player.moveto " + state.oilAt);
                } else if (state.oil && state.timer >= 6.0f && !state.oilReset) {
                    state.oilReset = true;
                    PlaceOil();
                } else if (state.oil && state.timer >= 10.0f) {
                    SKSE::log::info("hit test: in {:08X}, lighting the oil", a_player->GetParentCell()->GetFormID());
                    LightOil(a_player);
                    state.phase = state.pools.empty() ? Phase::Done : Phase::Oil;
                    if (state.pools.empty()) {
                        SKSE::log::info("hit test: done, no oil here");
                    }
                    state.timer = state.logTimer = 0.0f;
                }
            } else if (state.pad) {
                PadSteps();
            } else if (state.stealth) {
                Stealth(a_player, a_delta);
            } else if (state.timer >= 6.0f && state.crew) {
                SKSE::log::info("hit test: in {:08X}, handing the player over to CJ on foot for the crew", a_player->GetParentCell()->GetFormID());
                if (!PlayerSync::DebugOwned()) {
                    PlayerSync::DebugToggle();
                }
                state.phase = Phase::CrewSpots;
                state.timer = 0.0f;
            } else if (state.timer >= 6.0f && state.soak) {
                SKSE::log::info("hit test: in {:08X}, handing the player over to CJ on foot for the soak", a_player->GetParentCell()->GetFormID());
                if (!PlayerSync::DebugOwned()) {
                    PlayerSync::DebugToggle();
                }
                if (const auto hour = Ini::Value("Debug", "HitTestHour"); !hour.empty()) {
                    RunConsole("set gamehour to " + hour);
                }
                const auto scale = Ini::Value("Debug", "HitTestTimescale");
                RunConsole("set timescale to " + (scale.empty() ? std::string("60") : scale));
                state.phase = Phase::Soak;
                state.timer = state.soakT = state.soakShot = state.soakLog = 0.0f;
            } else if (state.timer >= 6.0f && state.armour) {
                SKSE::log::info("hit test: in {:08X}, handing the player over to CJ on foot, to stand still with armour", a_player->GetParentCell()->GetFormID());
                if (!PlayerSync::DebugOwned()) {
                    PlayerSync::DebugToggle();
                }
                Vitals::SetArmor(Health::kSaFullHealth);
                RunConsole("set timescale to 3600");  // an hour of game time a second: San Andreas's hunger (48 hours without food) comes within a minute
                state.phase = Phase::Standing;
                state.timer = state.logTimer = 0.0f;
            } else if (state.timer >= 6.0f && state.respawns) {
                SKSE::log::info("hit test: in {:08X}, handing the player over to CJ on foot", a_player->GetParentCell()->GetFormID());
                if (!PlayerSync::DebugOwned()) {
                    PlayerSync::DebugToggle();
                }
                state.phase = Phase::Killing;
                state.timer = 0.0f;
            } else if (state.timer >= 6.0f && state.riders && !state.follower) {
                PlaceFollower(a_player);
            } else if (state.timer >= 8.0f && state.crewRider) {
                // Not TakeOverInVehicle: a new handover sends the crew away and brings it back once CJ sits, so nobody boards.
                const auto why = PlayerSync::DebugSpawn(RideModel());
                SKSE::log::info("hit test: riders: a {} in front of CJ on foot{}", RideName(), why.empty() ? "" : ", refused: " + why);
                state.phase = why.empty() ? Phase::Boarding : Phase::Done;
                state.boardTry = 0;
                state.timer = 0.0f;
            } else if (state.lights && state.timer >= 4.0f && !state.oilMoved) {
                state.oilMoved = true;
                RunConsole("set gamehour to 23");  // night: every vehicle's lights on
            } else if (state.timer >= (state.riders ? 8.0f : 6.0f)) {
                SKSE::log::info("hit test: in {:08X}, handing the player over with CJ in vehicle model {}", a_player->GetParentCell()->GetFormID(), state.vehicle);
                PlayerSync::TakeOverInVehicle(state.lights ? state.vehicle : kSandking);
                state.phase = Phase::Handing;
                state.timer = 0.0f;
            }
            break;
        case Phase::Handing:
            if (InVehicle() && state.drive) {
                SKSE::log::info("hit test: CJ is in the Sandking, W: he drives for {:.0f} s", state.seconds);
                PlayerSync::DebugKey(Input::kDikW, true);
                state.feetTrace.clear();
                state.nodeTrace.clear();
                state.cameraTrace.clear();
                state.legs = 0;
                state.cells = 0;
                state.lastCell = nullptr;
                state.driven = 0.0f;
                state.lastAt = RE::NiPoint3();
                state.phase = Phase::Driving;
                state.timer = 0.0f;
            } else if (InVehicle() && state.riders) {
                SKSE::log::info("hit test: CJ is in the Sandking, {} riding with him", Riders::Riding().size());
                state.phase = Phase::Seated;
                state.timer = 0.0f;
            } else if (InVehicle()) {
                SKSE::log::info("hit test: CJ is in the vehicle (health {:.0f}), the {} begin", CarHealth(),
                    state.lights ? "lights" : state.bust ? "stars" : "attackers");
                if (state.lights) {
                    // nothing set on him: the lamps are watched and photographed
                } else if (state.bust) {
                    Police::CheatStars(1);
                } else {
                    PlaceAttackers(a_player);
                }
                state.phase = Phase::Watching;
                state.timer = state.logTimer = state.combatTimer = 0.0f;
            } else if (state.timer > 30.0f) {
                SKSE::log::info("hit test: CJ never got into the Sandking, stopping");
                state.phase = Phase::Done;
            }
            break;
        case Phase::Watching:
            state.combatTimer += a_delta;
            if (state.lights && state.shot < 2 && state.timer >= 6.0f + 4.0f * state.shot) {
                state.shot++;
                Overlay::Screenshot(state.shotsDir);  // its path goes to the log
            }
            if (!state.bust && !state.lights && state.combatTimer >= 1.5f) {
                state.combatTimer = 0.0f;
                SetOn(a_player);
            }
            state.logTimer += a_delta;
            if (state.logTimer >= 2.0f) {
                state.logTimer = 0.0f;
                SKSE::log::info("hit test: {:.0f} s: car health {:.0f}, player health {:.0f}{}{}{}", state.timer, CarHealth(),
                    a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth), InVehicle() ? ", CJ in the car" : ", CJ on foot",
                    Vitals::IsWasted() ? ", wasted" : "", Police::IsBusted() ? ", busted" : "");
            }
            if (state.timer >= state.seconds) {
                SKSE::log::info("hit test: done after {:.0f} s", state.timer);
                state.phase = Phase::Done;
            }
            break;
        case Phase::CrewSpots:
            if (state.timer >= 10.0f) {
                SKSE::log::info("hit test: crew: {} homies about after 10 s", Homies::DebugCount(false));
                const int joined = Homies::DebugRecruit(a_player, 3);  // the third is refused: a crew of 2 for one title
                SKSE::log::info("hit test: crew: {} joined, {} following", joined, Homies::DebugCount(true));
                // Two wolves 15 m ahead, set on the player.
                const float heading = a_player->GetAngleZ();
                for (int i = 0; i < 2; i++) {
                    auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(Wanted::kWolf);
                    auto  ref = base ? a_player->PlaceObjectAtMe(base, false) : nullptr;
                    if (ref) {
                        const float side = (i == 0 ? -1.0f : 1.0f) * 200.0f;
                        ref->SetPosition(a_player->GetPosition() + RE::NiPoint3(std::sin(heading) * 1050.0f + std::cos(heading) * side,
                                                                       std::cos(heading) * 1050.0f - std::sin(heading) * side, 0.0f));
                        state.attackers.push_back(ref->GetHandle());
                    }
                }
                // An elk 20 m off to the side, shot by CJ: it runs from him (CJ its combat target), and nobody shoots it.
                auto* elkBase = RE::TESForm::LookupByID<RE::TESNPC>(kElk);
                auto  elk = elkBase && std::string_view(elkBase->GetName()) == "Elk" ? a_player->PlaceObjectAtMe(elkBase, false) : nullptr;
                if (elk) {
                    elk->SetPosition(a_player->GetPosition() + RE::NiPoint3(std::cos(heading) * 1400.0f, -std::sin(heading) * 1400.0f, 0.0f));
                    state.elk = elk->GetHandle();
                } else {
                    SKSE::log::info("hit test: crew: no elk ({:08X} isn't one here): the wildlife check INCONCLUSIVE", kElk);
                }
                state.elkTargeted = false;
                state.elkFled = false;
                state.phase = Phase::CrewFight;
                state.timer = state.logTimer = 0.0f;
            }
            break;
        case Phase::CrewFight:
            SetOn(a_player);
            state.shotTimer += a_delta;
            if (state.shots < 6 && state.shotTimer >= 1.0f) {  // F11 a few times while the crew runs
                state.shotTimer = 0.0f;
                state.shots++;
                SKSE::log::info("hit test: crew: F11 screenshot {}", state.shots);
                Overlay::Screenshot((std::filesystem::temp_directory_path() / "ahshitrim-hit-test-shots").string());
            }
            if (auto elk = state.elk.get(); elk && elk->As<RE::Actor>()) {
                auto* animal = elk->As<RE::Actor>();
                if (state.timer >= 1.0f && state.timer - a_delta < 1.0f) {
                    Shots::Hurt(a_player, animal, 22, 5.0f, animal->GetPosition() + RE::NiPoint3(0.0f, 0.0f, 80.0f), RE::NiPoint3(0.0f, 1.0f, 0.0f), true);
                    SKSE::log::info("hit test: crew: CJ shot the elk");
                }
                state.elkTargeted = state.elkTargeted || Homies::DebugTargets(animal);
                state.elkFled = state.elkFled || (!animal->IsDead() && animal->GetActorRuntimeData().currentCombatTarget.get().get() == a_player);
            }
            state.logTimer += a_delta;
            if (state.logTimer >= 2.0f) {
                state.logTimer = 0.0f;
                int alive = 0;
                for (const auto& handle : state.attackers) {
                    auto  ref = handle.get();
                    auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                    alive += actor && !actor->IsDead();
                }
                SKSE::log::info("hit test: crew: {:.0f} s of the fight, {} wolves alive, {} following", state.timer, alive, Homies::DebugCount(true));
            }
            if (state.timer >= 15.0f) {
                if (auto elk = state.elk.get(); elk && elk->As<RE::Actor>()) {
                    auto*       animal = elk->As<RE::Actor>();
                    const char* verdict = state.elkTargeted ? "FAILED" : state.elkFled ? "ok" : "INCONCLUSIVE (it never had CJ as its target, so the old rule wouldn't have shot it either)";
                    SKSE::log::info("hit test: crew: the elk CJ shot is {}{}, the crew {}: {}", animal->IsDead() ? "dead" : "up",
                        state.elkFled ? ", had CJ as its combat target" : "", state.elkTargeted ? "aimed at it" : "left it be", verdict);
                    elk->Disable();
                    elk->SetDelete(true);
                    state.elk = {};
                }
                if (auto* homie = Homies::DebugFollower()) {
                    const std::string_view name = homie->GetDisplayFullName();
                    SKSE::log::info("hit test: crew: a follower's name is \"{}\": {}", name, name == Crew::kHomieName ? "ok" : "FAILED");
                } else {
                    SKSE::log::info("hit test: crew: no follower to read the name of: FAILED");
                }
                if (auto* homie = Homies::DebugFollower()) {
                    const float before = homie->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
                    Shots::Hurt(a_player, homie, 22, 25.0f, homie->GetPosition() + RE::NiPoint3(0.0f, 0.0f, 90.0f), RE::NiPoint3(0.0f, 1.0f, 0.0f), true);
                    state.hurt = homie->CreateRefHandle();
                    SKSE::log::info("hit test: crew: CJ shot {:08X}: health {:.0f} -> {:.0f}", homie->GetFormID(), before,
                        homie->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth));
                } else {
                    SKSE::log::info("hit test: crew: nobody follows CJ to shoot");
                }
                state.phase = Phase::CrewHurt;
                state.timer = 0.0f;
            }
            break;
        case Phase::CrewHurt:
            if (state.timer >= 3.0f) {
                auto* homie = state.hurt.get().get();
                if (homie) {
                    const bool fights = homie->GetActorRuntimeData().currentCombatTarget.get().get() == a_player;
                    SKSE::log::info("hit test: crew: the homie CJ shot is {}{}, {}hostile to the player, {} following", homie->IsDead() ? "dead" : "up",
                        fights ? " and fighting CJ" : "", homie->IsHostileToActor(a_player) ? "" : "not ", Homies::DebugCount(true));
                }
                PlayerSync::DebugType("HESOYAM");
                state.phase = Phase::CrewCheat;
                state.step = 0;
                state.timer = 0.0f;
            }
            break;
        case Phase::CrewCheat:
            if (state.step == 0 && state.timer >= 1.5f) {
                const int following = Homies::DebugCount(true);
                SKSE::log::info("hit test: crew: {} following after HESOYAM was typed (2 before): {}", following, following == 2 ? "ok" : "FAILED");
                PlayerSync::DebugType("H");
                state.step = 1;
                state.timer = 0.0f;
            } else if (state.step == 1 && state.timer >= 1.5f) {
                const int following = Homies::DebugCount(true);
                SKSE::log::info("hit test: crew: {} following after H on its own: {}", following, following == 0 ? "ok" : "FAILED");
                const int joined = Homies::DebugRecruit(a_player, 2);
                SKSE::log::info("hit test: crew: {} joined again, {} following", joined, Homies::DebugCount(true));
                state.step = 2;
                state.timer = 0.0f;
            } else if (state.step == 2 && state.timer >= 3.0f) {
                const unsigned lines = Homies::DebugHushed();
                SKSE::log::info("hit test: crew: {} Skyrim subtitle(s) of theirs taken off and {} voice(s) stopped so far: {}; F10 out", lines,
                    Homies::DebugHushedSounds(), lines ? "ok" : "INCONCLUSIVE (none of them spoke)");
                PlayerSync::DebugToggle();
                state.phase = Phase::CrewOut;
                state.timer = 0.0f;
            }
            break;
        case Phase::CrewOut:
            if (state.timer >= 3.0f && !PlayerSync::DebugOwned()) {
                SKSE::log::info("hit test: crew: {} homies about with Skyrim's player, F10 back", Homies::DebugCount(false));
                PlayerSync::DebugToggle();
                state.timer = 0.0f;
            } else if (state.timer >= 8.0f && PlayerSync::DebugOwned()) {
                SKSE::log::info("hit test: crew: {} following after F10 out and back; CJ walks the stables' road", Homies::DebugCount(true));
                state.phase = Phase::CrewWalk;
                state.walkMax = 0.0f;
                state.stood = -1.0f;
                state.regroupedBefore = Homies::DebugRegrouped();
                state.timer = 0.0f;
            }
            break;
        case Phase::CrewStray:
            if (state.timer >= 12.0f) {
                auto*       follower = Homies::DebugFollower();
                const float off = follower ? follower->GetPosition().GetDistance(a_player->GetPosition()) / Pose::kUnitsPerMeter : INFINITY;
                const int   regrouped = Homies::DebugRegrouped() - state.regroupedBefore;
                SKSE::log::info("hit test: crew: 12 s after, {} put back near CJ and the follower {:.1f} m off (within 6): {}", regrouped, off,
                    regrouped > 0 && off <= 6.0f ? "ok" : "FAILED");
                SKSE::log::info("hit test: crew: the player moves on, as fast travel moves it");
                RunConsole("cow Tamriel 5 -3");
                state.phase = Phase::CrewMoved;
                state.timer = 0.0f;
            }
            break;
        case Phase::CrewMoved:
            if (state.timer >= 12.0f) {
                SKSE::log::info("hit test: crew: {} following after the move ({} about); CJ walks again, up against Whiterun's walls", Homies::DebugCount(true),
                    Homies::DebugCount(false));
                state.phase = Phase::CrewWalk;
                state.walkMax = 0.0f;
                state.stood = -1.0f;
                state.regroupedBefore = Homies::DebugRegrouped();
                state.timer = 0.0f;
            }
            break;
        case Phase::CrewWalk:
        {
            // 0-6 s a walk ahead, 6-10 s standing, 10-16 s a run off to the right, 16-20 s standing.
            const float t = state.timer;
            const float x = t >= 10.0f && t < 16.0f ? 0.7f : 0.0f;
            const float y = t < 6.0f ? 0.35f : t >= 10.0f && t < 16.0f ? 1.0f : 0.0f;
            if (x != state.moveX || y != state.moveY) {
                SKSE::log::info("hit test: crew: CJ's stick at ({:.2f},{:.2f}), {} following", x, y, Homies::DebugCount(true));
            }
            state.moveX = x;
            state.moveY = y;
            auto*       follower = Homies::DebugFollower();
            const float off = follower ? follower->GetPosition().GetDistance(a_player->GetPosition()) / Pose::kUnitsPerMeter : INFINITY;
            if (x != 0.0f || y != 0.0f) {
                state.walkMax = (std::max)(state.walkMax, off);
            } else if (t >= 9.5f && t < 10.0f) {
                state.stood = off;  // the last of the first stand
            }
            if (t >= 20.0f) {
                state.moveX = state.moveY = 0.0f;
                if (state.walks++ == 0) {
                    // On the stables' road, all navmesh: his AI keeps up (KeepOffsetFromActor, catching up at a run).
                    constexpr float kMovingWithin = 12.0f, kStoodWithin = 5.0f;
                    SKSE::log::info("hit test: crew: on the stables' road a follower was at most {:.1f} m off while CJ walked and ran (within {:.0f}), and "
                                    "{:.1f} and {:.1f} m after CJ stood 4 s (within {:.0f}), {} following: {}",
                        state.walkMax, kMovingWithin, state.stood, off, kStoodWithin, Homies::DebugCount(true),
                        state.walkMax <= kMovingWithin && state.stood >= 0.0f && state.stood <= kStoodWithin && off <= kStoodWithin && Homies::DebugCount(true) == 2 ? "ok" : "FAILED");
                    const float stray = Homies::DebugStray(a_player, 70.0f);
                    SKSE::log::info("hit test: crew: a follower put {:.0f} m behind CJ on the navmesh, as a car or a rock leaves one", stray);
                    state.regroupedBefore = Homies::DebugRegrouped();
                    state.phase = Phase::CrewStray;
                } else {
                    // Up against the walls CJ walks where Skyrim has no navmesh: nobody's AI can follow him there, and with no
                    // navmesh near him at all there's nowhere to put a straggler either, so he waits at its edge, still in the
                    // crew. With navmesh near CJ he's put back within kRegroupAfter (or never fell that far behind).
                    const int    regrouped = Homies::DebugRegrouped() - state.regroupedBefore;
                    RE::NiPoint3 mesh;
                    const bool   unreachable = !Homies::DebugNavmesh(a_player, mesh);
                    const bool   cameUp = off <= Crew::kRegroupAfter && (regrouped > 0 || state.walkMax <= Crew::kRegroupAfter);
                    SKSE::log::info("hit test: crew: after the walk against the walls a follower is {:.1f} m off (at most {:.1f} on the way), {} put back near CJ, "
                                    "{} following{}: {}",
                        off, state.walkMax, regrouped, Homies::DebugCount(true), unreachable && !cameUp ? ", CJ where Skyrim has no navmesh near him (he waits at its edge)" : "",
                        (cameUp || unreachable) && Homies::DebugCount(true) == 2 ? "ok" : "FAILED");
                    SKSE::log::info("hit test: done after the crew run");
                    state.phase = Phase::Done;
                }
                state.timer = 0.0f;
            }
            break;
        }
        case Phase::Killing:
            if (!PlayerSync::DebugOwned()) {
                if (state.timer > 30.0f) {
                    SKSE::log::info("hit test: CJ never got the player, stopping");
                    state.phase = Phase::Done;
                }
                break;
            }
            if (state.timer >= 3.0f && !state.waitedFirst) {
                // First / before anyone is killed, so the wait menu opens: once a guard is, Skyrim refuses to wait with them near.
                state.timer = 5.0f;
                state.phase = Phase::Waiting;
                break;
            }
            if (state.timer >= 3.0f) {
                KillAround(a_player);
                state.phase = Phase::Waiting;
                state.timer = 0.0f;
            }
            break;
        case Phase::Waiting:
            if (state.timer >= 5.0f) {
                state.waitAsked = false;
                SKSE::log::info("hit test: pressing / for Skyrim's wait menu");
                state.phase = Phase::WaitMenu;
                state.timer = 0.0f;
                PlayerSync::DebugWait();
            }
            break;
        case Phase::WaitMenu:
            if (ui && ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME)) {
                break;
            }
            if (state.waitAsked || state.timer >= 6.0f) {
                auto* calendar = RE::Calendar::GetSingleton();
                if (calendar && calendar->gameDaysPassed && calendar->gameHour) {
                    if (!state.waitedFirst) {
                        SKSE::log::info("hit test: the wait menu {} before the kills", state.waitAsked ? "opened through / and was closed" : "never opened through /");
                    } else {
                        constexpr float kHours = 1.1f;
                        calendar->gameDaysPassed->value += kHours / 24.0f;
                        calendar->gameHour->value += kHours;
                        if (calendar->gameHour->value >= 24.0f) {
                            calendar->gameHour->value -= 24.0f;
                        }
                        SKSE::log::info("hit test: the wait menu {} after the kills: {:.1f} game hours on the clock, staying put",
                            state.waitAsked ? "opened through / and was closed" : "never opened through / (refused)", kHours);
                    }
                }
                state.phase = state.waitedFirst ? Phase::Looking : Phase::Killing;
                state.waitedFirst = true;
                state.timer = 0.0f;
            }
            break;
        case Phase::Looking:
            if (state.timer >= 4.0f) {
                Survey();
                RunConsole("coc " + state.away);
                state.phase = Phase::Away;
                state.timer = 0.0f;
            }
            break;
        case Phase::Away:
            if (state.timer >= 10.0f) {
                SKSE::log::info("hit test: in {:08X}, out of their sight", a_player->GetParentCell()->GetFormID());
                Survey();
                RunConsole(state.start.starts_with("cow ") ? state.start : "coc " + state.start);
                state.phase = Phase::Returning;
                state.timer = 0.0f;
            }
            break;
        case Phase::Returning:
            if (state.timer >= 8.0f) {
                Survey();
                SKSE::log::info("hit test: done after the respawn run");
                state.phase = Phase::Done;
            }
            break;
        case Phase::Standing:
            state.logTimer += a_delta;
            if (state.logTimer >= 10.0f) {
                state.logTimer = 0.0f;
                SKSE::log::info("hit test: {:.0f} s: armour {:.1f}, player health {:.0f}", state.timer, Vitals::Armor(),
                    a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth));
            }
            if (state.timer >= state.seconds) {
                SKSE::log::info("hit test: done after {:.0f} s standing still, armour {:.1f} of {:.0f} ({})", state.timer, Vitals::Armor(), Health::kSaFullHealth,
                    Vitals::Armor() >= Health::kSaFullHealth ? "kept" : "DRAINED");
                RunConsole("set timescale to 20");
                state.phase = Phase::Done;
            }
            break;
        case Phase::Boarding: {
            // F (San Andreas's enter key) once the Huntley (Sanchez) is there: CJ walks to it and gets in, the crew boarding with him.
            // San Andreas spawns nothing without a flat, clear spot in front of him (the crew or a parked vehicle in the way):
            // then he walks on a second and it's asked for again, every 8 s. F again while he gets in would stop him.
            const float t = std::fmod(state.timer, 8.0f);
            if (static_cast<int>(state.timer / 8.0f) != state.boardTry) {
                state.boardTry = static_cast<int>(state.timer / 8.0f);
                const auto why = PlayerSync::DebugSpawn(RideModel());
                SKSE::log::info("hit test: riders: CJ isn't in yet, the {} asked for again{}", RideName(), why.empty() ? "" : ", refused: " + why);
            }
            Protocol::PlayerState pose{};
            const bool entering = LinkService::Get().ReadPlayer(pose) && (pose.flags & Protocol::kPlayerGettingInOut) != 0;
            PlayerSync::DebugKey(Input::kDikF, t >= 2.0f && t < 2.3f);
            PlayerSync::DebugKey(Input::kDikW, t >= 6.0f && t < 7.0f && !entering);
            if (InVehicle()) {
                PlayerSync::DebugKey(Input::kDikF, false);
                PlayerSync::DebugKey(Input::kDikW, false);
                SKSE::log::info("hit test: CJ is in the {}, {} riding with him", RideName(), Riders::Riding().size());
                state.phase = Phase::Seated;
                state.timer = 0.0f;
            } else if (state.timer > 32.0f) {
                PlayerSync::DebugKey(Input::kDikF, false);
                PlayerSync::DebugKey(Input::kDikW, false);
                SKSE::log::warn("hit test: CJ never got into the {}, stopping", RideName());
                state.phase = Phase::Done;
            }
            break;
        }
        case Phase::Seated:
            if (state.timer >= 3.0f) {
                SKSE::log::info("hit test: {} riding, F: CJ gets out", Riders::Riding().size());
                TakePassengers();
                PlayerSync::DebugKey(Input::kDikF, true);
                state.cjTrace.clear();
                state.outFor = 0.0f;
                state.phase = Phase::Leaving;
                state.timer = 0.0f;
            }
            break;
        case Phase::Leaving:
            if (state.timer >= 0.3f) {
                PlayerSync::DebugKey(Input::kDikF, false);
            }
            TraceLeaving(a_player, a_delta);
            break;
        case Phase::Oil: {
            const bool out = WatchOil();
            state.logTimer += a_delta;
            if (state.logTimer >= 2.0f || out || state.timer >= state.seconds) {
                state.logTimer = 0.0f;
                for (const auto& pool : state.pools) {
                    const auto ref = pool.ref.get();
                    SKSE::log::info("hit test: {:.0f} s: the oil {:08X} ({}): {}, lowest health {:.0f}", state.timer, ref ? ref->GetFormID() : 0, pool.by,
                        pool.gone ? "burnt out" : ref ? std::format("health {:.0f}", HealthOf(ref.get())) : "gone", pool.lowest);
                }
            }
            if (out || state.timer >= state.seconds) {
                int burned = 0;
                for (const auto& pool : state.pools) {
                    burned += pool.burned ? 1 : 0;
                    SKSE::log::info("hit test: the oil lit by {} {}", pool.by, pool.burned ? (pool.gone ? "burned and burnt out" : "burned") : "never burned");
                }
                SKSE::log::info("hit test: done after {:.0f} s, {} of {} pools burned", state.timer, burned, state.pools.size());
                state.phase = Phase::Done;
            }
            break;
        }
        case Phase::Driving:
            TraceDriving(a_player);
            break;
        case Phase::Soak:
            SoakStep(a_player, a_delta);
            break;
        default: break;
        }
    }
}
