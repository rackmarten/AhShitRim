#include "debug/HitTest.h"

#include "Ini.h"
#include "link/LinkService.h"
#include "pad.h"
#include "player/PlayerSync.h"
#include "player/LockOn.h"
#include "player/Police.h"
#include "player/StealthKills.h"
#include "player/Respawns.h"
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
                        RunConsole("coc " + state.start);  // from the main menu: a new game there
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
            Protocol::PlayerState        pose{};
            const bool                   valid = LinkService::Get().ReadPlayer(pose) && (pose.flags & Protocol::kPlayerValid);
            const bool                   onFoot = valid && !(pose.flags & Protocol::kPlayerInVehicle);
            switch (state.step) {
            case 0:  // Back held a second, with Skyrim on the player
                if (state.timer >= 6.0f) {
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
                        for (const float side : { -0.35f, 0.35f }) {
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
                    PadCheck(LockOn::DebugGunTarget() != 0 && std::fabs(turned) > 3.0f,
                        std::format("LT locks on to {:08X} and the camera turns {:.0f} degrees onto it, the right stick untouched:", LockOn::DebugGunTarget(), turned));
                    PlayerSync::DebugPad(Pad::kRight, 1.0f);
                    PadNext();
                }
                break;
            case 22:
                if (state.timer >= 0.2f) {
                    PlayerSync::DebugPad(Pad::kRight, 0.0f);
                }
                if (state.timer >= 1.5f) {
                    const auto first = firstLock;
                    PadCheck(LockOn::DebugGunTarget() != 0 && LockOn::DebugGunTarget() != first && pose.weaponType == before.weaponType,
                        std::format("the d-pad's right shifts the lock {:08X} -> {:08X}, the weapon stays {}:", first, LockOn::DebugGunTarget(), pose.weaponType));
                    PlayerSync::DebugPad(Pad::kLeftTrigger, 0.0f);
                    // Out of the way, not deleted: disabling them mid-fight here froze Skyrim once.
                    for (const auto& handle : state.attackers) {
                        auto  ref = handle.get();
                        auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                        if (actor) {
                            actor->StopCombat();
                            ref->SetPosition(ref->GetPosition() + RE::NiPoint3(6000.0f, 6000.0f, 0.0f));
                        }
                    }
                    state.attackers.clear();
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
                    SKSE::log::info("hit test: pad: CJ into a Sandking where he stands");
                    PlayerSync::TakeOverInVehicle(kSandking);
                    state.step = 11;
                    state.timer = 0.0f;
                }
                break;
            }
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
                    PlayerSync::DebugPad(Pad::kBack, 1.0f);
                    PadNext();
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
            default: break;
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
        if (scenario != "hits" && scenario != "bust" && scenario != "stealth" && scenario != "respawns" && scenario != "pad") {
            return;
        }
        state.bust = scenario == "bust";
        state.stealth = scenario == "stealth";
        state.respawns = scenario == "respawns";
        state.pad = scenario == "pad";
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
                RunConsole("coc " + state.start);
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
            } else if (state.timer >= 6.0f && state.respawns) {
                SKSE::log::info("hit test: in {:08X}, handing the player over to CJ on foot", a_player->GetParentCell()->GetFormID());
                if (!PlayerSync::DebugOwned()) {
                    PlayerSync::DebugToggle();
                }
                state.phase = Phase::Killing;
                state.timer = 0.0f;
            } else if (state.timer >= 6.0f) {
                SKSE::log::info("hit test: in {:08X}, handing the player over with CJ in a Sandking", a_player->GetParentCell()->GetFormID());
                PlayerSync::TakeOverInVehicle(kSandking);
                state.phase = Phase::Handing;
                state.timer = 0.0f;
            }
            break;
        case Phase::Handing:
            if (InVehicle()) {
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
                RunConsole("coc " + state.start);
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
        default: break;
        }
    }
}
