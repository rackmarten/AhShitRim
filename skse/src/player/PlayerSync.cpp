#include "player/PlayerSync.h"

#include "input.h"
#include "link/LinkService.h"
#include "pose.h"

namespace AhShitRim::PlayerSync
{
    namespace
    {
        // A jump this big between two frames means the game moved the player (fast travel, a
        // script), which ends the handover.
        constexpr float kTeleportUnits = 2048.0f;

        // Main thread only: the input and menu sinks and the Update hook all run there.
        struct State
        {
            bool           owned = false;
            std::uint32_t  handover = 0;
            float          heading = 0.0f;   // the player's heading when the handover started
            RE::NiPoint3   anchor;           // the player's position when the handover started
            RE::NiPoint3   lastPlaced;
            RE::TESObjectCELL* cell = nullptr;
            bool           wasThirdPerson = false;
            bool           sneak = false;     // what we last told Skyrim; its own flag can drop on its own
            int            sneakResend = 0;   // frames until a sneak event the graph ignored is sent again
            std::uint32_t  inputFrame = 0;
            Input::Builder input;

            // Per-second stats while owned.
            float         statTime = 0.0f;
            int           frames = 0;
            int           placed = 0;
            int           newPoses = 0;
            std::uint32_t lastPoseFrame = 0;
        } state;

        // A Skyrim menu has the input: the game is paused, or it shows its cursor (dialogue, which
        // doesn't pause). CJ then stands still and the mouse doesn't turn San Andreas's camera.
        bool MenuHasInput()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::CursorMenu::MENU_NAME));
        }

        bool GamePaused()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && ui->GameIsPaused();
        }

        void Publish(bool a_menu = MenuHasInput(), bool a_paused = GamePaused())
        {
            auto& link = LinkService::Get();
            if (!link.IsUp()) {
                return;
            }
            auto input = state.input.Build(++state.inputFrame, state.owned, !a_menu, a_paused);
            input.handover = state.handover;
            input.heading = state.heading;
            link.WriteInput(input);
        }

        // Releases keys the keyboard no longer holds. A release event can get lost (a menu at the
        // wrong moment), and CJ would then run forever.
        void ReleaseStuckKeys()
        {
            auto* devices = RE::BSInputDeviceManager::GetSingleton();
            auto* keyboard = devices ? devices->GetKeyboard() : nullptr;
            if (!keyboard) {
                return;
            }
            const auto& down = keyboard->GetRuntimeData().curState;
            for (std::uint32_t dik = 0; dik < 0x100; dik++) {
                if ((down[dik] & 0x80) == 0 && state.input.IsKeyDown(dik)) {
                    state.input.SetKey(dik, false);
                    SKSE::log::info("player sync: released stuck key (scan code {:02X})", dik);
                }
            }
        }

        void Release(const char* a_reason)
        {
            if (!state.owned) {
                return;
            }
            state.owned = false;
            state.input.ReleaseAll();
            if (auto* player = RE::PlayerCharacter::GetSingleton(); player && (state.sneak || player->IsSneaking())) {
                player->AsActorState()->actorState1.sneaking = false;
                player->NotifyAnimationGraph("SneakStop"sv);
            }
            state.sneak = false;
            if (state.wasThirdPerson) {
                if (auto* camera = RE::PlayerCamera::GetSingleton()) {
                    camera->ForceThirdPerson();
                }
                state.wasThirdPerson = false;
            }
            Publish();
            SKSE::log::info("player sync: Skyrim owns the player ({})", a_reason);
        }

        void TakeOver(RE::PlayerCharacter* a_player)
        {
            if (!LinkService::Get().IsUp()) {
                SKSE::log::info("player sync: link is down, can't hand the player to San Andreas");
                return;
            }
            if (!a_player->GetParentCell()) {
                return;
            }
            // San Andreas's camera drives the view, and only Skyrim's first-person camera follows it.
            if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && !camera->IsInFirstPerson()) {
                state.wasThirdPerson = camera->IsInThirdPerson();
                camera->ForceFirstPerson();
            }
            state.owned = true;
            state.handover++;
            state.anchor = a_player->GetPosition();
            state.lastPlaced = state.anchor;
            state.cell = a_player->GetParentCell();
            state.heading = a_player->GetAngleZ();
            state.statTime = 0.0f;
            state.frames = state.placed = state.newPoses = 0;
            Publish();
            SKSE::log::info("player sync: San Andreas owns the player (handover {} at ({:.0f},{:.0f},{:.0f}) heading {:.3f})",
                state.handover, state.anchor.x, state.anchor.y, state.anchor.z, state.heading);
        }

        const char* MovedByGame(RE::PlayerCharacter* a_player)
        {
            auto* cell = a_player->GetParentCell();
            if (!cell) {
                return nullptr;  // loading
            }
            if (cell != state.cell && (cell->IsInteriorCell() || state.cell->IsInteriorCell())) {
                return "went through a load door";
            }
            if (a_player->GetPosition().GetDistance(state.lastPlaced) > kTeleportUnits) {
                return "moved by the game";
            }
            state.cell = cell;  // walked into the next exterior cell
            return nullptr;
        }

        void Place(RE::PlayerCharacter* a_player, const Protocol::PlayerState& a_pose)
        {
            state.lastPlaced = state.anchor + RE::NiPoint3(a_pose.offset[0], a_pose.offset[1], a_pose.offset[2]);
            a_player->SetPosition(state.lastPlaced, true);
            a_player->SetHeading(a_pose.viewHeading);  // first person: the body turns with the view
            a_player->data.angle.x = a_pose.viewPitch;
            if (auto* controller = a_player->GetCharController()) {
                controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f));
            }

            // CJ crouching: Skyrim sneaks. As in PerkeleRim, the actor state and the animation graph
            // are both kept in step, and the event is sent again if the graph ignored it.
            const bool sneak = (a_pose.flags & Protocol::kPlayerCrouching) != 0;
            auto*      actorState = a_player->AsActorState();
            if (actorState && sneak != state.sneak) {
                state.sneak = sneak;
                a_player->NotifyAnimationGraph(sneak ? "SneakStart"sv : "SneakStop"sv);
                SKSE::log::info("player sync: sneak {}", sneak ? "on" : "off");
            }
            if (actorState && actorState->IsSneaking() != sneak) {
                actorState->actorState1.sneaking = sneak;
            }
            bool graphSneak = sneak;
            if (a_player->GetGraphVariableBool("IsSneaking"sv, graphSneak) && graphSneak != sneak) {
                if (--state.sneakResend <= 0) {
                    state.sneakResend = 15;
                    a_player->NotifyAnimationGraph(sneak ? "SneakStart"sv : "SneakStop"sv);
                }
            } else {
                state.sneakResend = 0;
            }
        }

        void Drive(RE::PlayerCharacter* a_player, float a_delta)
        {
            auto& link = LinkService::Get();
            if (!link.IsUp()) {
                Release("link down");
                return;
            }
            if (a_player->IsDead()) {
                Release("the player died");
                return;
            }
            if (const char* reason = MovedByGame(a_player)) {
                Release(reason);
                return;
            }
            ReleaseStuckKeys();
            Publish();

            Protocol::PlayerState pose{};
            const bool valid = link.ReadPlayer(pose) && (pose.flags & Protocol::kPlayerValid) != 0 &&
                               pose.handover == state.handover && Pose::Finite(pose);
            if (valid) {
                Place(a_player, pose);
                state.placed++;
                if (pose.frame != state.lastPoseFrame) {
                    state.newPoses++;
                    state.lastPoseFrame = pose.frame;
                }
            }

            state.frames++;
            state.statTime += a_delta;
            if (state.statTime >= 1.0f) {
                SKSE::log::info(
                    "player sync: frames={} placed={} new poses={} offset=({:.0f},{:.0f},{:.0f}) speed={:.1f} m/s heading={:.2f} view=({:.2f},{:.2f}){}",
                    state.frames, state.placed, state.newPoses, pose.offset[0], pose.offset[1], pose.offset[2],
                    std::hypot(pose.velocity[0], pose.velocity[1]) * Pose::kMetersPerUnit, pose.heading, pose.viewHeading,
                    pose.viewPitch, (pose.flags & Protocol::kPlayerGrounded) ? "" : " in the air");
                state.statTime = 0.0f;
                state.frames = state.placed = state.newPoses = 0;
            }
        }

        struct UpdateHook
        {
            static void thunk(RE::PlayerCharacter* a_player, float a_delta)
            {
                original(a_player, a_delta);
                if (state.owned && a_delta > 0.0f) {
                    Drive(a_player, a_delta);
                }
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };

        // Skyrim's own handlers refuse while San Andreas has the player.
        template <int N>
        struct BlockHook
        {
            static bool thunk(RE::PlayerInputHandler* a_handler, RE::InputEvent* a_event)
            {
                return !state.owned && original(a_handler, a_event);
            }
            static inline REL::Relocation<decltype(thunk)> original;

            static void Install(REL::VariantID a_vtable)
            {
                REL::Relocation<std::uintptr_t> vtable{ a_vtable };
                original = vtable.write_vfunc(0x1, thunk);
            }
        };

        struct FavoritesHook
        {
            static bool thunk(RE::FavoritesHandler* a_handler, RE::InputEvent* a_event)
            {
                return !state.owned && original(a_handler, a_event);
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };

        // Only Escape, Tab, M and the console open Skyrim's menus while San Andreas has the player.
        struct MenuOpenHook
        {
            static bool thunk(RE::MenuOpenHandler* a_handler, RE::InputEvent* a_event)
            {
                if (state.owned && a_event && a_event->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                    if (auto* button = a_event->AsButtonEvent()) {
                        const auto key = button->GetIDCode();
                        if (key != Input::kDikEscape && key != Input::kDikTab && key != Input::kDikM && key != Input::kDikGrave) {
                            return false;
                        }
                    }
                }
                return original(a_handler, a_event);
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };

        class InputSink : public RE::BSTEventSink<RE::InputEvent*>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
            {
                bool changed = false;
                for (auto* e = a_event ? *a_event : nullptr; e; e = e->next) {
                    if (auto* move = e->AsMouseMoveEvent()) {
                        if (MenuHasInput()) {
                            continue;  // moves the menu's cursor, not San Andreas's camera
                        }
                        state.input.AddMouse(move->mouseInputX, move->mouseInputY);
                        changed = true;
                        continue;
                    }
                    auto* button = e->AsButtonEvent();
                    if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                        continue;
                    }
                    const auto code = button->GetIDCode();
                    if (code == Input::kDikF10) {
                        if (button->IsDown()) {
                            Toggle();
                        }
                        continue;
                    }
                    state.input.SetKey(code, button->IsPressed());
                    changed = true;
                }
                if (changed && state.owned) {
                    Publish();
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            static void Toggle()
            {
                if (state.owned) {
                    Release("F10");
                } else if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    TakeOver(player);
                }
            }
        };

        // A menu opening pauses the game in the same frame; tell San Andreas right away.
        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (state.owned && a_event) {
                    bool pauses = false;
                    if (a_event->opening) {
                        auto* ui = RE::UI::GetSingleton();
                        auto  menu = ui ? ui->GetMenu(a_event->menuName) : nullptr;
                        pauses = menu && menu->PausesGame();
                    }
                    Publish(pauses || MenuHasInput(), pauses || GamePaused());
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
        UpdateHook::original = vtable.write_vfunc(0xAD, UpdateHook::thunk);
        REL::Relocation<std::uintptr_t> favorites{ RE::VTABLE_FavoritesHandler[0] };
        FavoritesHook::original = favorites.write_vfunc(0x1, FavoritesHook::thunk);
        REL::Relocation<std::uintptr_t> menuOpen{ RE::VTABLE_MenuOpenHandler[0] };
        MenuOpenHook::original = menuOpen.write_vfunc(0x1, MenuOpenHook::thunk);
        BlockHook<0>::Install(RE::VTABLE_MovementHandler[0]);
        BlockHook<1>::Install(RE::VTABLE_LookHandler[0]);
        BlockHook<2>::Install(RE::VTABLE_SprintHandler[0]);
        BlockHook<3>::Install(RE::VTABLE_AutoMoveHandler[0]);
        BlockHook<4>::Install(RE::VTABLE_ToggleRunHandler[0]);
        BlockHook<5>::Install(RE::VTABLE_RunHandler[0]);
        BlockHook<6>::Install(RE::VTABLE_JumpHandler[0]);
        BlockHook<7>::Install(RE::VTABLE_SneakHandler[0]);
        BlockHook<8>::Install(RE::VTABLE_TogglePOVHandler[0]);
        BlockHook<9>::Install(RE::VTABLE_AttackBlockHandler[0]);
        BlockHook<10>::Install(RE::VTABLE_ReadyWeaponHandler[0]);
        BlockHook<11>::Install(RE::VTABLE_ActivateHandler[0]);
        BlockHook<13>::Install(RE::VTABLE_FirstPersonState[1]);  // its zoom and POV keys
        BlockHook<14>::Install(RE::VTABLE_ThirdPersonState[1]);
        SKSE::log::info("player sync: PlayerCharacter::Update and Skyrim's player input handlers hooked, F10 hands the player to San Andreas");
    }

    void OnDataLoaded()
    {
        static InputSink inputSink;
        static MenuSink  menuSink;
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
            input->AddEventSink(&inputSink);
        }
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuSink);
        }
    }

    void OnGameLoad()
    {
        state.wasThirdPerson = false;  // the loaded save has its own camera
        Release("game loading");
    }
}
