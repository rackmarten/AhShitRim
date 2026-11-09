#include "debug/HitTest.h"

#include "Ini.h"
#include "crew.h"
#include "link/LinkService.h"
#include "player/Homies.h"
#include "player/PlayerSync.h"
#include "player/Shots.h"
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
            CrewSpots,  // crew (v39): the spots fill, two homies join
            CrewFight,  // wolves come, the crew shoots
            CrewHurt,   // CJ shoots one of his own: hurt, never hostile
            CrewOut,    // F10 out and back: the crew comes again
            CrewWalk,   // CJ walks, stops, runs off at an angle and stops: the followers walk and run after him, facing their way
            CrewStray,  // a follower put 70 m behind, as one left by a car or stuck: he's put back near CJ and comes up
            CrewMoved,  // the player moved as fast travel moves it: the followers come behind; then the walk again
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
            int                step = 0;         // its steps
            RE::ObjectRefHandle victim;
            std::vector<RE::FormID>          victims;  // respawns
            bool               crew = false;     // the scenario: the Grove Street crew (v39)
            RE::ActorHandle    hurt;             // the follower CJ shot
            float              moveX = 0.0f;     // CrewWalk: CJ's stick
            float              moveY = 0.0f;
            int                walks = 0;
            int                regroupedBefore = 0;  // Homies::DebugRegrouped() as the second walk started         // CrewWalk: the walks done (the first at the stables, the second after the move)
            float              walkMax = 0.0f;    // the furthest a follower was while CJ moved, metres
            float              stood = -1.0f;     // how far he was after CJ's first stand
            float              followLog = 0.0f;  // the crew phases: seconds since a follower was last logged
            RE::FormID         followerId = 0;
            RE::NiPoint3       followerWas;
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

    bool Drive(float& a_moveX, float& a_moveY)
    {
        if (state.phase != Phase::CrewWalk) {
            return false;
        }
        a_moveX = state.moveX;
        a_moveY = state.moveY;
        return true;
    }

    void OnDataLoaded()
    {
        const auto scenario = Ini::Value("Debug", "HitTest");
        if (scenario != "hits" && scenario != "bust" && scenario != "stealth" && scenario != "respawns" && scenario != "crew") {
            return;
        }
        state.crew = scenario == "crew";
        if (state.crew) {
            Homies::DebugThanes(1);  // Thane of Whiterun; hit-test.ps1 starts it at the Whiterun stables
        }
        state.bust = scenario == "bust";
        state.stealth = scenario == "stealth";
        state.respawns = scenario == "respawns";
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
            if (state.stealth) {
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
        default: break;
        }
    }
}
