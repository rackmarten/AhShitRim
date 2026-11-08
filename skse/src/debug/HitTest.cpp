#include "debug/HitTest.h"

#include "Ini.h"
#include "link/LinkService.h"
#include "player/ObjectHits.h"
#include "player/PlayerSync.h"
#include "player/Police.h"
#include "player/Vitals.h"
#include "wanted.h"

#include <algorithm>
#include <chrono>
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
            Oil,       // the oil scenario: the pools lit, watched burning
            Done,
        };

        constexpr std::uint16_t kSandking = 495;
        constexpr float         kAhead = 840.0f;  // 12 m: where the attackers are placed

        struct State
        {
            Phase              phase = Phase::Off;
            bool               bust = false;  // the scenario: bust, else hits
            bool               oil = false;   // or oil
            std::string        start = "Riverwood";
            float              seconds = 90.0f;
            float              timer = 0.0f;
            float              logTimer = 0.0f;
            float              combatTimer = 0.0f;
            bool               newGame = false;  // a new game at the start instead of the most recent save
            std::vector<RE::ObjectRefHandle> attackers;

            struct Pool
            {
                RE::ObjectRefHandle ref;
                const char*         by = "";
                float               lowest = 100.0f;  // its health at the lowest seen, of 100
                bool                burned = false;   // seen burning: below 94% and above 7% (its fire's stages)
                bool                gone = false;     // burnt out and disabled
            };
            std::vector<Pool> pools;
        } state;

        float HealthOf(RE::TESObjectREFR* a_ref)
        {
            const auto* health = a_ref->extraList.GetByType<RE::ExtraObjectHealth>();
            return health ? health->health : 100.0f;
        }

        // Up to three of the pools round the player: a bullet lands in the first, a Molotov bursts on the second, a rocket
        // on the third, each through what CJ's own shots and projectiles call (ObjectHits).
        void LightOil(RE::PlayerCharacter* a_player)
        {
            const auto pools = ObjectHits::OilNear(a_player);
            SKSE::log::info("hit test: {} oil pools near the player", pools.size());
            const char* by[] = { "a bullet", "a Molotov", "a rocket" };
            for (std::size_t i = 0; i < pools.size() && i < 3; i++) {
                auto*      ref = pools[i];
                const auto at = ref->Get3D() ? ref->Get3D()->worldBound.center : ref->GetPosition();
                SKSE::log::info("hit test: {} on the oil {:08X} at ({:.0f}, {:.0f}, {:.0f}), health {:.0f}", by[i], ref->GetFormID(), at.x, at.y, at.z, HealthOf(ref));
                if (i == 0) {
                    ObjectHits::BulletLanded(a_player, at, Weapons::Pistol);
                } else {
                    ObjectHits::Around(a_player, at, 100.0f, i == 1 ? Traps::Hit::Fire : Traps::Hit::Blast, 0.0f);
                }
                state.pools.push_back({ ref->GetHandle(), by[i] });
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
    }

    void OnDataLoaded()
    {
        const auto scenario = Ini::Value("Debug", "HitTest");
        if (scenario != "hits" && scenario != "bust" && scenario != "oil") {
            return;
        }
        state.bust = scenario == "bust";
        state.oil = scenario == "oil";
        if (const auto start = Ini::Value("Debug", "HitTestStart"); !start.empty()) {
            state.start = start;
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
            if (state.oil && state.timer >= 6.0f) {
                SKSE::log::info("hit test: in {:08X}, lighting the oil", a_player->GetParentCell()->GetFormID());
                LightOil(a_player);
                state.phase = state.pools.empty() ? Phase::Done : Phase::Oil;
                if (state.pools.empty()) {
                    SKSE::log::info("hit test: done, no oil here");
                }
                state.timer = state.logTimer = 0.0f;
            } else if (!state.oil && state.timer >= 6.0f) {
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
        default: break;
        }
    }
}
