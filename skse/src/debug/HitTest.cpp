#include "debug/HitTest.h"

#include "Ini.h"
#include "link/LinkService.h"
#include "player/PlayerSync.h"
#include "player/Police.h"
#include "player/Respawns.h"
#include "player/Vitals.h"
#include "wanted.h"

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
            std::vector<RE::FormID>          victims;  // respawns
        } state;

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
        if (scenario != "hits" && scenario != "bust" && scenario != "respawns") {
            return;
        }
        state.bust = scenario == "bust";
        state.respawns = scenario == "respawns";
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
            if (state.timer >= 6.0f && state.respawns) {
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
                    SKSE::log::info("hit test: {:.1f} game hours on", kHours);
                }
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
