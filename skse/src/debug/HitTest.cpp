#include "debug/HitTest.h"

#include "Ini.h"
#include "car_exit.h"
#include "input.h"
#include "passengers.h"
#include "link/LinkService.h"
#include "pad.h"
#include "player/Homies.h"
#include "player/LockOn.h"
#include "player/PlayerSync.h"
#include "player/Shots.h"
#include "player/Police.h"
#include "player/StealthKills.h"
#include "player/Respawns.h"
#include "player/Riders.h"
#include "player/Vitals.h"
#include "wanted.h"
#include "weapons.h"

#include <chrono>
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
            Waiting,    // their quests' window, then an hour on the clock
            Looking,    // still there: who gets up out of view, and who waits in view
            Away,       // in an interior, out of their sight
            Returning,  // back outside: are they up at their spots?
            CrewSpots,  // crew (v39): the spots fill, two homies join
            CrewFight,  // wolves come, the crew shoots
            CrewHurt,   // CJ shoots one of his own: hurt, never hostile
            CrewOut,    // F10 out and back: the crew comes again
            CrewMoved,  // the player moved as fast travel moves it: the followers come behind
            Seated,     // riders: CJ and his follower sit in the Sandking
            Leaving,    // riders: F, both get out, traced every frame
            Done,
        };

        constexpr std::uint16_t kSandking = 495;
        constexpr float         kAhead = 840.0f;  // 12 m: where the attackers are placed

        struct State
        {
            Phase              phase = Phase::Off;
            bool               bust = false;  // the scenario: bust, else hits
            bool               respawns = false;  // the scenario: respawns
            std::string        away = "RiverwoodSleepingGiantInn";
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
            bool               crew = false;     // the scenario: the Grove Street crew (v39)
            RE::ActorHandle    hurt;             // the follower CJ shot
            bool                riders = false;  // the scenario: a follower rides with CJ and gets out with him
            bool                crewRider = false;  // riders with a recruited homie of the crew (v39) as the follower
            RE::ObjectRefHandle follower;
            std::vector<CarExit::Sample> riderTrace;  // the follower's feet in the vehicle's frame, every frame
            std::vector<CarExit::Sample> cjTrace;     // and the player's (CJ's feet)
            float               outFor = 0.0f;     // seconds since both were out
        } state;

        constexpr float kVictimAhead = 1.6f * Pose::kUnitsPerMeter;  // the stealth scenario's guard, back to CJ
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
                if (state.timer >= 6.0f) {
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
                const int joined = Homies::DebugRecruit(a_player, 1);
                auto*     homie = Homies::DebugFollower();
                SKSE::log::info("hit test: riders: {} homies about, {} joined", Homies::DebugCount(false), joined);
                if (homie) {
                    state.follower = homie->GetHandle();
                    state.timer = 2.0f;  // a few seconds to walk up to CJ before the Sandking
                    SKSE::log::info("hit test: riders: homie {} ({:08X}) is the follower", homie->GetName(), homie->GetFormID());
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

        // Every frame from F until both have been out a second: the follower's feet and CJ's.
        void TraceLeaving(RE::PlayerCharacter* a_player, float a_delta)
        {
            auto       ref = state.follower.get();
            auto*      actor = ref ? ref->As<RE::Actor>() : nullptr;
            const bool riding = actor && Riders::Rides(actor->GetFormID());
            const bool cjIn = InVehicle();
            Pose::Vec3 at{}, cj{};
            if (actor && InVehicleFrame(actor->GetPosition(), at)) {
                state.riderTrace.push_back({ at, riding });
            }
            if (InVehicleFrame(a_player->GetPosition(), cj)) {
                state.cjTrace.push_back({ cj, cjIn });
            }
            SKSE::log::info("hit test: leaving {:5.0f} ms, follower {:6.2f} {:6.2f} {:6.2f} {}, CJ {:6.2f} {:6.2f} {:6.2f} {}", state.timer * 1000.0f, at.x, at.y,
                at.z, riding ? "in" : "out", cj.x, cj.y, cj.z, cjIn ? "in" : "out");
            state.outFor = !riding && !cjIn ? state.outFor + a_delta : 0.0f;
            if (state.outFor >= 1.0f || state.timer > 10.0f) {
                if (state.crewRider) {
                    // A homie walks off to his formation behind CJ once let go: judge the step out, up to there.
                    const auto let = std::find_if(state.riderTrace.begin(), state.riderTrace.end(), [](const CarExit::Sample& a_sample) { return !a_sample.inVehicle; });
                    state.riderTrace.erase(let == state.riderTrace.end() ? let : let + 1, state.riderTrace.end());
                }
                Report("the follower", state.riderTrace, true);
                // San Andreas's line-up moves CJ's position to the door as the get-out begins and his animation keeps his
                // body in the seat, so his feet jump where his body doesn't: logged, not judged (fake_vehicles judges his body).
                Report("CJ (the player's position)", state.cjTrace, false);
                SKSE::log::info("hit test: done after the riders run");
                state.phase = Phase::Done;
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
            if (auto* guard = PlaceAt(a_player, Wanted::kHolds[0].guard, a_feet + ahead * kVictimAhead, a_heading)) {
                state.victim = guard->GetHandle();
            }
            PlaceAt(a_player, Wanted::kHolds[0].guard, a_feet + ahead * kVictimAhead + right * kOthersAside, a_heading + Pose::kPi);
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
                    SKSE::log::info("hit test: CJ holds the knife, placing a guard {:.1f} m ahead with his back to him", kVictimAhead * Pose::kMetersPerUnit);
                    PlaceVictims(a_player, a_player->GetPosition(), pose.heading);
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
    }

    void OnDataLoaded()
    {
        const auto scenario = Ini::Value("Debug", "HitTest");
        if (scenario != "hits" && scenario != "bust" && scenario != "stealth" && scenario != "respawns" && scenario != "crew" && scenario != "pad" && scenario != "riders" && scenario != "ridecrew") {
            return;
        }
        state.crew = scenario == "crew";
        if (state.crew) {
            Homies::DebugThanes(1);  // Thane of Whiterun; hit-test.ps1 starts it at the Whiterun stables
        }
        state.bust = scenario == "bust";
        state.stealth = scenario == "stealth";
        state.respawns = scenario == "respawns";
        state.pad = scenario == "pad";
        state.riders = scenario == "riders" || scenario == "ridecrew";
        state.crewRider = scenario == "ridecrew";
        if (state.crewRider) {
            Homies::DebugThanes(1);
        }
        state.seconds = state.stealth ? 12.0f : state.seconds;
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
            if (state.pad) {
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
            } else if (state.timer >= 6.0f && state.respawns) {
                SKSE::log::info("hit test: in {:08X}, handing the player over to CJ on foot", a_player->GetParentCell()->GetFormID());
                if (!PlayerSync::DebugOwned()) {
                    PlayerSync::DebugToggle();
                }
                state.phase = Phase::Killing;
                state.timer = 0.0f;
            } else if (state.timer >= 6.0f && state.riders && !state.follower) {
                PlaceFollower(a_player);
            } else if (state.timer >= (state.riders ? 8.0f : 6.0f)) {
                SKSE::log::info("hit test: in {:08X}, handing the player over with CJ in a Sandking", a_player->GetParentCell()->GetFormID());
                PlayerSync::TakeOverInVehicle(kSandking);
                state.phase = Phase::Handing;
                state.timer = 0.0f;
            }
            break;
        case Phase::Handing:
            if (InVehicle() && state.riders) {
                SKSE::log::info("hit test: CJ is in the Sandking, {} riding with him", Riders::Riding().size());
                state.phase = Phase::Seated;
                state.timer = 0.0f;
            } else if (InVehicle()) {
                SKSE::log::info("hit test: CJ is in the Sandking (health {:.0f}), the {} begin", CarHealth(), state.bust ? "stars" : "attackers");
                if (state.bust) {
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
            if (!state.bust && state.combatTimer >= 1.5f) {
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
                state.phase = Phase::CrewFight;
                state.timer = state.logTimer = 0.0f;
            }
            break;
        case Phase::CrewFight:
            SetOn(a_player);
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
                SKSE::log::info("hit test: crew: F10 out");
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
                SKSE::log::info("hit test: crew: {} following after F10 out and back; the player moves on, as fast travel moves it", Homies::DebugCount(true));
                RunConsole("cow Tamriel 5 -3");
                state.phase = Phase::CrewMoved;
                state.timer = 0.0f;
            }
            break;
        case Phase::CrewMoved:
            if (state.timer >= 12.0f) {
                SKSE::log::info("hit test: crew: {} following after the move ({} about)", Homies::DebugCount(true), Homies::DebugCount(false));
                SKSE::log::info("hit test: done after the crew run");
                state.phase = Phase::Done;
            }
            break;
        case Phase::Killing:
            if (!PlayerSync::DebugOwned()) {
                if (state.timer > 30.0f) {
                    SKSE::log::info("hit test: CJ never got the player, stopping");
                    state.phase = Phase::Done;
                }
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
                if (auto* calendar = RE::Calendar::GetSingleton(); calendar && calendar->gameDaysPassed && calendar->gameHour) {
                    constexpr float kHours = 1.1f;
                    calendar->gameDaysPassed->value += kHours / 24.0f;
                    calendar->gameHour->value += kHours;
                    if (calendar->gameHour->value >= 24.0f) {
                        calendar->gameHour->value -= 24.0f;
                    }
                    SKSE::log::info("hit test: {:.1f} game hours on, staying put", kHours);
                }
                state.phase = Phase::Looking;
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
        case Phase::Seated:
            if (state.timer >= 3.0f) {
                if (state.crewRider) {
                    // The handover into the Sandking sends the crew away and brings it back as new actors: trace whoever rides now.
                    const auto riding = Riders::Riding();
                    if (!riding.empty()) {
                        if (auto* rider = RE::TESForm::LookupByID<RE::Actor>(riding.front())) {
                            state.follower = rider->GetHandle();
                        }
                    }
                }
                SKSE::log::info("hit test: {} riding, F: CJ gets out", Riders::Riding().size());
                PlayerSync::DebugKey(Input::kDikF, true);
                state.riderTrace.clear();
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
        default: break;
        }
    }
}
