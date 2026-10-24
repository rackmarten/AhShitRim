#include "debug/LoadTest.h"

#include "Ini.h"
#include "player/People.h"
#include "player/PlayerSync.h"
#include "player/Police.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace AhShitRim::LoadTest
{
    namespace
    {
        enum class Phase
        {
            Off,
            MainMenu,  // waiting for the main menu, to load the first save
            Loading,   // a load asked for, until it's done (kPostLoadGame)
            Settling,  // the loading screen gone, the player in: a few seconds
            Handing,   // the player to San Andreas
            Acting,    // the cycle's scenario
            Done,
        };

        enum class Scenario
        {
            Idle,
            Flee,
            Chase,
            Asnaeb,
            Travel,
            Aezakmi,
            Handback,
        };
        constexpr Scenario kScenarios[] = { Scenario::Idle, Scenario::Flee, Scenario::Chase, Scenario::Asnaeb, Scenario::Travel, Scenario::Aezakmi,
            Scenario::Handback };

        const char* NameOf(Scenario a_scenario)
        {
            switch (a_scenario) {
            case Scenario::Idle: return "idle";
            case Scenario::Flee: return "flee";
            case Scenario::Chase: return "chase";
            case Scenario::Asnaeb: return "asnaeb";
            case Scenario::Travel: return "travel";
            case Scenario::Aezakmi: return "aezakmi";
            case Scenario::Handback: return "handback";
            default: return "?";
            }
        }

        constexpr float kSettleSeconds = 6.0f;
        constexpr float kHandSeconds = 10.0f;  // the handover resumes by itself (a save that had it) or F10 gives it

        struct State
        {
            Phase                    phase = Phase::Off;
            int                      cycles = 0;
            int                      cycle = 0;  // the one running (from 1)
            std::vector<std::string> saves{ "StressA", "StressB" };
            std::vector<Scenario>    scenarios{ std::begin(kScenarios), std::end(kScenarios) };  // LoadTestScenarios, in turn
            Scenario                 scenario = Scenario::Idle;
            float                    timer = 0.0f;
            int                      step = 0;
            bool                     travelled = false;
        } state;

        std::filesystem::path Folder()
        {
            const auto dir = SKSE::log::log_directory();
            // .../My Games/Skyrim Special Edition/SKSE -> .../My Games/AhShitRim
            return dir ? dir->parent_path().parent_path() / "AhShitRim" : std::filesystem::path("AhShitRim");
        }

        int ReadProgress()
        {
            std::ifstream in(Folder() / "loadtest-progress.txt");
            int           done = 0;
            in >> done;
            return in ? done : 0;
        }

        void WriteProgress(int a_done)
        {
            std::error_code error;
            std::filesystem::create_directories(Folder(), error);
            std::ofstream(Folder() / "loadtest-progress.txt", std::ios::trunc) << a_done << "\n";
        }

        void Note(const std::string& a_what)
        {
            std::error_code error;
            std::filesystem::create_directories(Folder(), error);
            const auto now = std::chrono::system_clock::now();
            std::ofstream(Folder() / "loadtest.csv", std::ios::app)
                << std::format("{:%F %T},{},{},{},{}\n", std::chrono::floor<std::chrono::seconds>(now), state.cycle, state.saves[state.cycle % state.saves.size()],
                       NameOf(state.scenario), a_what);
            SKSE::log::info("load test: cycle {} ({}): {}", state.cycle, NameOf(state.scenario), a_what);
        }

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
            SKSE::log::info("load test: ran '{}'", a_command);
        }

        // The next cycle: counted first (a crash in it isn't run again), then its save loaded.
        void NextCycle()
        {
            state.cycle = ReadProgress() + 1;
            if (state.cycle > state.cycles) {
                state.phase = Phase::Done;
                SKSE::log::info("load test: all {} cycles done", state.cycles);
                return;
            }
            WriteProgress(state.cycle);
            state.scenario = state.scenarios[static_cast<std::size_t>(state.cycle - 1) % state.scenarios.size()];
            state.travelled = false;
            Note("load");
            state.phase = Phase::Loading;
            state.timer = 0.0f;
            RunConsole("load " + state.saves[state.cycle % state.saves.size()]);
        }

        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (state.phase == Phase::MainMenu && a_event && a_event->opening && a_event->menuName == RE::MainMenu::MENU_NAME) {
                    state.phase = Phase::Loading;
                    std::thread([] {
                        std::this_thread::sleep_for(std::chrono::seconds(4));
                        SKSE::GetTaskInterface()->AddUITask([] { NextCycle(); });  // as the console's own commands run
                    }).detach();
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } menuSink;

        bool Settled(RE::PlayerCharacter* a_player)
        {
            auto* ui = RE::UI::GetSingleton();
            auto* cell = a_player->GetParentCell();
            return !(ui && (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->GameIsPaused())) && a_player->Is3DLoaded() && cell && cell->IsAttached();
        }

        void Act(RE::PlayerCharacter* a_player)
        {
            const float t = state.timer;
            auto        at = [&](float a_when) {
                if (state.step == 0 && t >= a_when) {
                    state.step = 1;
                    return true;
                }
                return false;
            };
            auto then = [&](int a_step, float a_when) {
                if (state.step == a_step && t >= a_when) {
                    state.step = a_step + 1;
                    return true;
                }
                return false;
            };
            bool finished = false;
            switch (state.scenario) {
            case Scenario::Idle: finished = t >= 5.0f; break;
            case Scenario::Flee:
                if (at(0.0f)) {
                    People::Blast(a_player->GetPosition());
                    Note("blast");
                }
                finished = t >= 8.0f;
                break;
            case Scenario::Chase:
                if (at(0.0f)) {
                    PlayerSync::DebugType("BAGUVIX");
                    Police::CheatStars(2);
                    Note("six stars, loading with the force out");
                }
                finished = t >= 18.0f;
                break;
            case Scenario::Asnaeb:
            case Scenario::Aezakmi:
                if (at(0.0f)) {
                    PlayerSync::DebugType("BAGUVIX");
                    Police::CheatStars(2);
                    Note("six stars");
                }
                if (then(1, 15.0f)) {
                    if (state.scenario == Scenario::Asnaeb) {
                        Police::CheatStars(0);
                        Note("ASNAEB");
                    } else {
                        PlayerSync::DebugType("AEZAKMI");
                        Note("AEZAKMI");
                    }
                }
                if (state.scenario == Scenario::Aezakmi && then(2, 18.0f)) {
                    PlayerSync::DebugType("AEZAKMI");  // off again
                }
                finished = t >= 20.0f;
                break;
            case Scenario::Travel:
                if (!state.travelled && at(0.0f)) {
                    state.travelled = true;
                    Note("coc Riverwood");
                    RunConsole("coc Riverwood");
                    state.phase = Phase::Settling;  // its load screen, then the cycle ends
                    state.timer = 0.0f;
                    return;
                }
                finished = state.travelled;
                break;
            case Scenario::Handback:
                if (at(1.0f) && PlayerSync::DebugOwned()) {
                    PlayerSync::DebugToggle();
                    Note("F10: back to Skyrim");
                }
                finished = t >= 5.0f;
                break;
            }
            if (finished) {
                Note("ok");
                NextCycle();
            }
        }
    }

    void OnDataLoaded()
    {
        const auto cycles = Ini::Value("Debug", "LoadTest");
        try {
            state.cycles = cycles.empty() ? 0 : std::stoi(cycles);
        } catch (...) {
            state.cycles = 0;
        }
        if (state.cycles <= 0) {
            return;
        }
        if (const auto saves = Ini::Value("Debug", "LoadTestSaves"); !saves.empty()) {
            state.saves.clear();
            for (std::size_t from = 0; from <= saves.size();) {
                const auto comma = saves.find(',', from);
                const auto name = saves.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
                if (!name.empty()) {
                    state.saves.push_back(name);
                }
                from = comma == std::string::npos ? saves.size() + 1 : comma + 1;
            }
        }
        if (const auto names = Ini::Value("Debug", "LoadTestScenarios"); !names.empty()) {
            std::vector<Scenario> picked;
            for (const auto scenario : kScenarios) {
                if (names.find(NameOf(scenario)) != std::string::npos) {
                    picked.push_back(scenario);
                }
            }
            if (!picked.empty()) {
                state.scenarios = picked;
            }
        }
        if (state.saves.empty() || ReadProgress() >= state.cycles) {
            SKSE::log::info("load test: on, but all {} cycles are done", state.cycles);
            return;
        }
        state.phase = Phase::MainMenu;
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuSink);
        }
        SKSE::log::info("load test: on, cycle {} of {} next, between {} saves", ReadProgress() + 1, state.cycles, state.saves.size());
    }

    void OnPostLoadGame()
    {
        if (state.phase == Phase::Loading) {
            state.phase = Phase::Settling;
            state.timer = 0.0f;
            Note("loaded");
        }
    }

    void Update(RE::PlayerCharacter* a_player, float a_delta)
    {
        if (state.phase == Phase::Off || state.phase == Phase::Done || state.phase == Phase::MainMenu || state.phase == Phase::Loading || a_delta <= 0.0f) {
            return;
        }
        if (!Settled(a_player)) {
            state.timer = 0.0f;
            return;
        }
        state.timer += a_delta;
        switch (state.phase) {
        case Phase::Settling:
            if (state.timer >= kSettleSeconds) {
                if (state.travelled) {
                    Note("ok");
                    NextCycle();
                    return;
                }
                state.phase = state.scenario == Scenario::Handback || PlayerSync::DebugOwned() ? Phase::Acting : Phase::Handing;
                state.timer = 0.0f;
                state.step = 0;
                if (state.phase == Phase::Handing) {
                    PlayerSync::DebugToggle();
                    Note("F10: to San Andreas");
                }
            }
            break;
        case Phase::Handing:
            if (PlayerSync::DebugOwned() || state.timer >= kHandSeconds) {
                state.phase = Phase::Acting;
                state.timer = 0.0f;
                state.step = 0;
                Note(PlayerSync::DebugOwned() ? "San Andreas has the player" : "no handover, on anyway");
            }
            break;
        case Phase::Acting: Act(a_player); break;
        default: break;
        }
    }
}
