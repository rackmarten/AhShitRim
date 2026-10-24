#include "debug/MissionTest.h"

#include "hud/Overlay.h"
#include "player/Missions.h"
#include "player/PlayerSync.h"

#include <cstdlib>

namespace AhShitRim::MissionTest
{
    namespace
    {
        constexpr float kFirstSeconds = 5.0f;     // in the game this long before the intro line
        constexpr float kCommandSeconds = 10.0f;  // between the steps

        struct State
        {
            bool                     on = false;
            bool                     handBack = false;  // "skyrim": Skyrim takes the player back first, so the cues play while San Andreas is muted
            bool                     loading = false;
            bool                     inGame = false;
            float                    time = 0.0f;
            std::size_t              step = 0;  // 0 the intro line, then the commands
            std::vector<std::string> commands;
            float                    shotIn = -1.0f;  // seconds to a screenshot of what the last command put up, -1 for none
            std::string              shots;           // AHSHITRIM_MISSION_SHOTS: where they go
        } state;

        // The main menu has no save list yet when it opens: tried every 2 s, 10 times (as the stream test does).
        void TryLoad(int a_try)
        {
            std::thread([a_try] {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                SKSE::GetTaskInterface()->AddTask([a_try] {
                    auto* saves = RE::BGSSaveLoadManager::GetSingleton();
                    if (saves && saves->PopulateSaveList() && saves->LoadMostRecentSaveGame()) {
                        SKSE::log::info("mission test: loading the most recent save");
                    } else if (a_try < 10) {
                        TryLoad(a_try + 1);
                    }
                });
            }).detach();
        }

        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (state.on && !state.loading && a_event && a_event->opening && a_event->menuName == RE::MainMenu::MENU_NAME) {
                    state.loading = true;
                    TryLoad(1);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

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
            SKSE::log::info("mission test: ran '{}'", a_command);
        }
    }

    void OnDataLoaded()
    {
        const char* value = std::getenv("AHSHITRIM_MISSION_TEST");
        if (!value || !*value) {
            return;
        }
        state.handBack = std::string(value) == "skyrim";
        std::string text = std::string(value) == "1" || state.handBack ? "resetquest MQ103;setstage MQ103 10;completequest MQ103" : value;
        for (std::size_t at = 0; at <= text.size();) {
            const auto end = (std::min)(text.find(';', at), text.size());
            if (end > at) {
                state.commands.push_back(text.substr(at, end - at));
            }
            at = end + 1;
        }
        if (const char* shots = std::getenv("AHSHITRIM_MISSION_SHOTS")) {
            state.shots = shots;
        }
        state.on = true;
        static MenuSink sink;
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
        }
        SKSE::log::info("mission test: on, {} commands after the intro line", state.commands.size());
    }

    void Update(RE::PlayerCharacter* a_player, float a_delta)
    {
        if (!state.on || a_delta <= 0.0f || !a_player->Is3DLoaded() || !a_player->GetParentCell()) {
            return;
        }
        if (!state.inGame) {
            state.inGame = true;
            state.time = 0.0f;
            SKSE::log::info("mission test: in the game");
        }
        state.time += a_delta;
        if (state.step == 0 && state.time >= kFirstSeconds) {
            if (state.handBack) {
                PlayerSync::OnGameLoad();  // as a load does: Skyrim has the player
                SKSE::log::info("mission test: Skyrim has the player");
            }
            Missions::TestIntro();
            state.step = 1;
            state.time = 0.0f;
        } else if (state.step >= 1 && state.step <= state.commands.size() && state.time >= kCommandSeconds) {
            RunConsole(state.commands[state.step - 1]);
            state.step++;
            state.time = 0.0f;
            state.shotIn = state.shots.empty() ? -1.0f : 1.5f;
        }
        if (state.shotIn >= 0.0f && (state.shotIn -= a_delta) < 0.0f) {
            Overlay::Screenshot(state.shots);
        }
    }

    bool On() { return state.on; }
}
