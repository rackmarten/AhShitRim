#include "debug/StreamTest.h"

#include "Ini.h"
#include "pose.h"

namespace AhShitRim::StreamTest
{
    namespace
    {
        enum class Phase
        {
            Off,
            MainMenu,   // waiting for the main menu to load the save from
            Loading,    // the save loads
            Settle,     // in the game: coc, then waiting for the cell
            Flying,
            Done,
        };

        struct Second
        {
            int   frames = 0;
            float worst = 0.0f;
            int   over50 = 0;
            int   over100 = 0;
            int   samples = 0;
            int   underAttached = 0, underLand = 0;
            int   ahead1Attached = 0, ahead1Land = 0;
            int   ahead2Attached = 0, ahead2Land = 0;

            void Add(const Second& a_other)
            {
                frames += a_other.frames;
                worst = (std::max)(worst, a_other.worst);
                over50 += a_other.over50;
                over100 += a_other.over100;
                samples += a_other.samples;
                underAttached += a_other.underAttached;
                underLand += a_other.underLand;
                ahead1Attached += a_other.ahead1Attached;
                ahead1Land += a_other.ahead1Land;
                ahead2Attached += a_other.ahead2Attached;
                ahead2Land += a_other.ahead2Land;
            }
        };

        struct State
        {
            Phase        phase = Phase::Off;
            float        speed = 0.0f;  // metres a second
            std::string  start = "Riverwood";
            float        heading = 0.0f;  // radians from north, clockwise
            float        height = 30.0f;
            float        seconds = 60.0f;
            bool         load = true;
            float        time = 0.0f;
            bool         cocDone = false;
            RE::NiPoint3 from;
            Second       second, total;
            float        secondTime = 0.0f;
        } state;

        // The main menu has no save list yet when it opens: it is read first, and tried again every 2 s, 10 times.
        void TryLoad(int a_try)
        {
            std::thread([a_try] {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                SKSE::GetTaskInterface()->AddTask([a_try] {
                    auto* saves = RE::BGSSaveLoadManager::GetSingleton();
                    const bool loading = saves && saves->PopulateSaveList() && saves->LoadMostRecentSaveGame();
                    if (loading) {
                        SKSE::log::info("stream test: loading the most recent save");
                    } else if (a_try < 10) {
                        TryLoad(a_try + 1);
                    } else {
                        SKSE::log::info("stream test: could not load the most recent save; load one yourself");
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
        };

        float Number(std::string_view a_key, float a_default)
        {
            const auto text = Ini::Value("Debug", a_key);
            if (text.empty()) {
                return a_default;
            }
            try {
                return std::stof(text);
            } catch (...) {
                return a_default;
            }
        }

        void RunConsole(const std::string& a_command)
        {
            auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
            auto* script = factory ? factory->Create() : nullptr;
            if (!script) {
                SKSE::log::warn("stream test: no script to run '{}' with", a_command);
                return;
            }
            script->SetCommand(a_command);
            script->CompileAndRun(nullptr);
            delete script;
            SKSE::log::info("stream test: ran '{}'", a_command);
        }

        // Whether the land at a_at is in an attached cell (Havok has it, so the sampler can read it), and has a height.
        void Look(const RE::NiPoint3& a_at, int& a_attached, int& a_land)
        {
            auto*      tes = RE::TES::GetSingleton();
            auto*      cell = tes ? tes->GetCell(a_at) : nullptr;
            a_attached += cell && cell->IsAttached();
            float      z = 0.0f;
            a_land += tes && tes->GetLandHeight(a_at, z);
        }

        int Percent(int a_part, int a_whole) { return a_whole ? a_part * 100 / a_whole : 0; }

        void Log(const char* a_what, float a_at, float a_window, const Second& a_s)
        {
            SKSE::log::info(
                "stream test: {} {:.0f} s, {:.0f} m: {} frames ({:.0f} a second), worst {:.0f} ms, {} over 50 ms, {} over 100 ms; land under the player "
                "attached {}% / height {}%, 1 s ahead {}% / {}%, 2 s ahead {}% / {}%",
                a_what, a_at, a_at * state.speed, a_s.frames, a_s.frames / (std::max)(a_window, 0.001f), a_s.worst * 1000.0f,
                a_s.over50, a_s.over100, Percent(a_s.underAttached, a_s.samples), Percent(a_s.underLand, a_s.samples), Percent(a_s.ahead1Attached, a_s.samples),
                Percent(a_s.ahead1Land, a_s.samples), Percent(a_s.ahead2Attached, a_s.samples), Percent(a_s.ahead2Land, a_s.samples));
        }
    }

    void OnDataLoaded()
    {
        state.speed = Number("StreamTest", 0.0f);
        if (state.speed <= 0.0f) {
            return;
        }
        if (const auto start = Ini::Value("Debug", "StreamTestStart"); !start.empty()) {
            state.start = start;
        }
        state.heading = Number("StreamTestHeading", 0.0f) * 0.0174532925f;
        state.height = Number("StreamTestHeight", 30.0f);
        state.seconds = Number("StreamTestSeconds", 60.0f);
        state.load = Number("StreamTestLoad", 1.0f) != 0.0f;
        state.phase = state.load ? Phase::MainMenu : Phase::Loading;
        static MenuSink sink;
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
        }
        SKSE::log::info("stream test: on ([Debug] StreamTest={} m/s from {}, heading {:.0f}, {} m up, {} s{})", state.speed, state.start,
            state.heading * 57.2957795f, state.height, state.seconds, state.load ? ", loading the most recent save" : "");
    }

    void OnPostLoadGame()
    {
        if (state.phase == Phase::Loading || state.phase == Phase::MainMenu) {
            state.phase = Phase::Settle;
            state.time = 0.0f;
            state.cocDone = false;
            SKSE::log::info("stream test: the game is loaded, coc {} in 3 s", state.start);
        }
    }

    bool Running() { return state.phase == Phase::Settle || state.phase == Phase::Flying; }

    void Update(RE::PlayerCharacter* a_player, float a_delta)
    {
        // A game is up (the post-load message doesn't come for every load): on with the test.
        if ((state.phase == Phase::MainMenu || state.phase == Phase::Loading) && a_delta > 0.0f && a_player->Is3DLoaded() && a_player->GetParentCell()) {
            SKSE::log::info("stream test: in the game, going to {}", state.start);
            OnPostLoadGame();
        }
        if (!Running() || a_delta <= 0.0f) {
            return;
        }
        state.time += a_delta;
        if (state.phase == Phase::Settle) {
            if (!state.cocDone && state.time > 3.0f) {
                state.cocDone = true;
                RunConsole("coc " + state.start);
                state.time = 0.0f;
            }
            auto* cell = a_player->GetParentCell();
            if (state.cocDone && state.time > 10.0f && cell && !cell->IsInteriorCell()) {
                state.phase = Phase::Flying;
                state.time = 0.0f;
                state.secondTime = 0.0f;
                state.second = state.total = {};
                state.from = a_player->GetPosition() + RE::NiPoint3(0.0f, 0.0f, state.height * Pose::kUnitsPerMeter);
                SKSE::log::info("stream test: flying from ({:.0f},{:.0f},{:.0f}) in {:08X}", state.from.x, state.from.y, state.from.z, cell->GetFormID());
            }
            return;
        }

        const RE::NiPoint3 dir(std::sin(state.heading), std::cos(state.heading), 0.0f);
        const float        units = state.speed * Pose::kUnitsPerMeter;
        const RE::NiPoint3 at = state.from + dir * (units * state.time);
        a_player->SetPosition(at, true);
        if (auto* controller = a_player->GetCharController()) {
            controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f));
            controller->fallStartHeight = at.z;
            controller->fallTime = 0.0f;
        }
        auto& s = state.second;
        s.frames++;
        s.worst = (std::max)(s.worst, a_delta);
        s.over50 += a_delta > 0.05f;
        s.over100 += a_delta > 0.1f;
        s.samples++;
        Look(at, s.underAttached, s.underLand);
        Look(at + dir * units, s.ahead1Attached, s.ahead1Land);
        Look(at + dir * (2.0f * units), s.ahead2Attached, s.ahead2Land);
        state.secondTime += a_delta;
        if (state.secondTime >= 1.0f) {
            Log("at", state.time, state.secondTime, s);
            state.total.Add(s);
            s = {};
            state.secondTime = 0.0f;
        }
        if (state.time >= state.seconds) {
            state.total.Add(s);
            Log("in all", state.time, state.time, state.total);
            state.phase = Phase::Done;
        }
    }
}
