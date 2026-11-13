#include "debug/LoadTest.h"

#include "player/People.h"
#include "player/PlayerSync.h"
#include "player/Police.h"

#include <chrono>
#include <deque>
#include <format>
#include <string>

namespace AhShitRim::LoadTest
{
    namespace
    {
        enum class Phase
        {
            Off,
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
            Phase            phase = Phase::Off;
            std::string      save;
            Scenario         scenario = Scenario::Idle;
            float            timer = 0.0f;
            float            clock = 0.0f;  // for the notes
            int              step = 0;
            bool             travelled = false;
            std::deque<Note> notes;
        } state;

        void Say(const std::string& a_what)
        {
            SKSE::log::info("load test: {} ({}): {}", state.save, NameOf(state.scenario), a_what);
            const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
            state.notes.push_back({ std::format("{:%F %T}", now), state.clock, a_what });
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

        // Who is in a fight as the load goes: whatever crashes in the load's combat clearing is one of these.
        void Census()
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            int fighting = 0;
            lists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (!a_actor || !a_actor->IsInCombat()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                fighting++;
                auto*      base = a_actor->GetActorBase();
                auto*      leveled = a_actor->extraList.GetByType<RE::ExtraLeveledCreature>();
                const auto target = a_actor->GetActorRuntimeData().currentCombatTarget.get();
                SKSE::log::info("load test:   fighting {:08X} '{}' base {:08X}{} from {:08X}, target {:08X}{}", a_actor->GetFormID(), a_actor->GetName(),
                    base ? base->GetFormID() : 0, base && base->IsDynamicForm() ? " (runtime)" : "",
                    leveled && leveled->originalBase ? leveled->originalBase->GetFormID() : 0, target ? target->GetFormID() : 0,
                    a_actor->IsDead() ? ", dead" : "");
                return RE::BSContainer::ForEachResult::kContinue;
            });
            SKSE::log::info("load test: {} in a fight as the load goes", fighting);
        }

        bool Settled(RE::PlayerCharacter* a_player)
        {
            auto* ui = RE::UI::GetSingleton();
            auto* cell = a_player->GetParentCell();
            return !(ui && (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->GameIsPaused())) && a_player->Is3DLoaded() && cell && cell->IsAttached();
        }

        void Finish()
        {
            Say("ok");
            state.phase = Phase::Done;
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
                    Say("blast");
                }
                finished = t >= 8.0f;
                break;
            case Scenario::Chase:
                if (at(0.0f)) {
                    PlayerSync::DebugType("BAGUVIX");
                    Police::CheatStars(2);
                    Say("six stars, loading with the force out");
                }
                finished = t >= 18.0f;
                break;
            case Scenario::Asnaeb:
            case Scenario::Aezakmi:
                if (at(0.0f)) {
                    PlayerSync::DebugType("BAGUVIX");
                    Police::CheatStars(2);
                    Say("six stars");
                }
                if (then(1, 15.0f)) {
                    if (state.scenario == Scenario::Asnaeb) {
                        Police::CheatStars(0);
                        Say("ASNAEB");
                    } else {
                        PlayerSync::DebugType("AEZAKMI");
                        Say("AEZAKMI");
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
                    Say("coc Riverwood");
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
                    Say("F10: back to Skyrim");
                }
                finished = t >= 5.0f;
                break;
            }
            if (finished) {
                Finish();
            }
        }
    }

    std::string Begin(const std::string& a_save, const std::string& a_scenario)
    {
        if (Running()) {
            return "a cycle is still going";
        }
        if (a_save.empty() || a_save.find_first_of(" \t\"';") != std::string::npos) {
            return "save is a save's file name without spaces or quotes";
        }
        std::optional<Scenario> scenario;
        for (const auto s : kScenarios) {
            if (a_scenario == NameOf(s)) {
                scenario = s;
            }
        }
        if (!scenario) {
            return "scenario is one of " + Scenarios();
        }
        state = State{};
        state.save = a_save;
        state.scenario = *scenario;
        Census();
        Say("load");
        state.phase = Phase::Loading;
        // As the console's own commands run (its UI task), not from the middle of a frame (the test kit's command point):
        // a load begun there crashed in the actors' update after it.
        SKSE::GetTaskInterface()->AddUITask([command = "load " + a_save] { RunConsole(command); });
        return {};
    }

    void Stop()
    {
        if (Running()) {
            Say("stopped");
        }
        state.phase = Phase::Off;
    }

    bool Running() { return state.phase != Phase::Off && state.phase != Phase::Done; }

    bool Done() { return state.phase == Phase::Done; }

    std::optional<Note> NextNote()
    {
        if (state.notes.empty()) {
            return std::nullopt;
        }
        auto note = state.notes.front();
        state.notes.pop_front();
        return note;
    }

    std::string Scenarios()
    {
        std::string names;
        for (const auto s : kScenarios) {
            names += names.empty() ? "" : ", ";
            names += NameOf(s);
        }
        return names;
    }

    void OnPostLoadGame()
    {
        if (state.phase == Phase::Loading) {
            state.phase = Phase::Settling;
            state.timer = 0.0f;
            Say("loaded");
        }
    }

    void Update(RE::PlayerCharacter* a_player, float a_delta)
    {
        if (state.phase == Phase::Off || state.phase == Phase::Done || state.phase == Phase::Loading || a_delta <= 0.0f) {
            return;
        }
        if (!Settled(a_player)) {
            state.timer = 0.0f;
            return;
        }
        state.timer += a_delta;
        state.clock += a_delta;
        switch (state.phase) {
        case Phase::Settling:
            if (state.timer >= kSettleSeconds) {
                if (state.travelled) {
                    Finish();
                    return;
                }
                state.phase = state.scenario == Scenario::Handback || PlayerSync::DebugOwned() ? Phase::Acting : Phase::Handing;
                state.timer = 0.0f;
                state.step = 0;
                if (state.phase == Phase::Handing) {
                    PlayerSync::DebugToggle();
                    Say("F10: to San Andreas");
                }
            }
            break;
        case Phase::Handing:
            if (PlayerSync::DebugOwned() || state.timer >= kHandSeconds) {
                state.phase = Phase::Acting;
                state.timer = 0.0f;
                state.step = 0;
                Say(PlayerSync::DebugOwned() ? "San Andreas has the player" : "no handover, on anyway");
            }
            break;
        case Phase::Acting: Act(a_player); break;
        default: break;
        }
    }
}
