#include "player/PlayerSync.h"

#include "body/ModelView.h"
#include "body.h"
#include "camera.h"
#include "collision/CollisionSampler.h"
#include "hud/SaHud.h"
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
            Body::Interpolator body;      // CJ's skeleton and San Andreas's camera, smoothed
            Body::Interpolator vehicles[Protocol::kVehicleMax];  // each vehicle's parts, smoothed
            bool           playerHidden = false;  // we culled the player's own 3D for CJ's
            RE::NiAVObject* culledRoot[2] = {};   // third- and first-person roots this culled
            float          savedFov = 0.0f;       // Skyrim's field of view before San Andreas's, 0 = not changed
            bool           cameraParentLogged = false;
            bool           cjLogged = false;      // logged the first frame CJ was drawn this handover

            // Per-second stats while owned.
            float         statTime = 0.0f;
            int           frames = 0;
            int           placed = 0;
            int           newPoses = 0;
            int           drawn = 0;
            std::uint32_t lastPoseFrame = 0;
        } state;

        double NowMs()
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

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
            input.anchor[0] = state.anchor.x;
            input.anchor[1] = state.anchor.y;
            input.anchor[2] = state.anchor.z;
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

        // ---- CJ in Skyrim ----

        // The player's own 3D (third- and first-person) is hidden while CJ is drawn in its place.
        // Only the roots this culled are shown again: Skyrim culls one of them itself depending on
        // the camera, and that stays as it was.
        void HidePlayer(RE::PlayerCharacter* a_player, bool a_hide)
        {
            for (int i = 0; i < 2; i++) {
                auto* root = a_player->Get3D(i == 1);
                if (!root) {
                    continue;
                }
                if (a_hide && !root->GetAppCulled()) {
                    root->SetAppCulled(true);
                    state.culledRoot[i] = root;
                } else if (!a_hide && state.culledRoot[i] == root) {
                    root->SetAppCulled(false);
                }
                if (!a_hide) {
                    state.culledRoot[i] = nullptr;
                }
            }
            state.playerHidden = a_hide;
        }

        // Skyrim's sneak indicator (the eye, a clip of the HUD movie) stays up after CJ stands up from a
        // crouch, as it did after Kumpo stood up in PerkeleRim, so it is hidden every frame CJ doesn't
        // crouch, and shown again once.
        void HideSneakEye(bool a_hide)
        {
            static bool hidden = false;
            if (!a_hide && !hidden) {
                return;
            }
            hidden = a_hide;
            SKSE::GetTaskInterface()->AddUITask([a_hide]() {
                auto* ui = RE::UI::GetSingleton();
                auto  menu = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
                auto* movie = menu ? menu->uiMovie.get() : nullptr;
                RE::GFxValue eye;
                if (!movie || !movie->GetVariable(&eye, "_root.HUDMovieBaseInstance.StealthMeterInstance") || !eye.IsDisplayObject()) {
                    static bool logged = false;
                    if (movie && !std::exchange(logged, true)) {
                        SKSE::log::warn("player sync: the sneak eye clip (_root.HUDMovieBaseInstance.StealthMeterInstance) was not found");
                    }
                    return;
                }
                eye.SetMember("_visible", RE::GFxValue(!a_hide));
            });
        }

        // Back to Skyrim drawing its own player: CJ and the vehicles hidden, the player's 3D and field
        // of view back.
        void DrawSkyrimPlayer()
        {
            ModelView::HideAll();
            HideSneakEye(false);
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && state.playerHidden) {
                HidePlayer(player, false);
            }
            if (state.savedFov > 0.0f) {
                if (auto* camera = RE::PlayerCamera::GetSingleton()) {
                    camera->GetRuntimeData2().worldFOV = state.savedFov;
                }
                state.savedFov = 0.0f;
            }
            state.body.Reset();
            for (auto& vehicle : state.vehicles) {
                vehicle.Reset();
            }
        }

        // Puts Skyrim's camera where San Andreas's is (BodyState camera, relative to the handover
        // spot), looking where it looks, with its field of view, so the view behind CJ is San
        // Andreas's own. Skyrim's camera stays in its first-person state, which only draws the
        // player's hidden arms; Skyrim builds the camera before this hook (from the angles the
        // player had, a_fromHeading and a_fromPitch), so the finished camera node is overridden, as
        // PerkeleRim does for MSC's camera.
        bool PoseCamera(const Protocol::BodyState& a_body, float a_fromHeading, float a_fromPitch)
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            auto* root = camera ? camera->cameraRoot.get() : nullptr;
            if (!root || !camera->IsInFirstPerson()) {
                return false;
            }
            Camera::Matrix built, posed;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    built[r][c] = root->world.rotate.entry[r][c];
                }
            }
            const Pose::Vec3 forward{ a_body.cameraForward[0], a_body.cameraForward[1], a_body.cameraForward[2] };
            Camera::Retarget(built, a_fromHeading, a_fromPitch, Pose::HeadingOf(forward.x, forward.y), Pose::PitchOf(forward), 0.0f, posed);
            RE::NiMatrix3 rotate;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    rotate.entry[r][c] = posed[r][c];
                }
            }
            const auto at = state.anchor + RE::NiPoint3(a_body.cameraPosition[0], a_body.cameraPosition[1], a_body.cameraPosition[2]);
            // Update keeps the node's local transform, so give it the wanted world one in its parent's
            // frame (scale 1).
            if (auto* parent = root->parent) {
                const auto toParent = parent->world.rotate.Transpose();
                root->local.rotate = toParent * rotate;
                root->local.translate = toParent * (at - parent->world.translate);
                if (!state.cameraParentLogged) {
                    state.cameraParentLogged = true;
                    SKSE::log::info("player sync: camera root hangs under {}", parent->name.c_str());
                }
            } else {
                root->local.rotate = rotate;
                root->local.translate = at;
            }
            RE::NiUpdateData update{};
            root->Update(update);
            if (a_body.cameraFov > 1.0f && a_body.cameraFov < 170.0f) {
                auto& data = camera->GetRuntimeData2();
                if (state.savedFov <= 0.0f) {
                    state.savedFov = data.worldFOV;
                }
                data.worldFOV = a_body.cameraFov;
            }
            return true;
        }

        // CJ drawn where San Andreas has him, posed by his skeleton, and Skyrim's camera on San
        // Andreas's, both smoothed between San Andreas's frames. Until his model and skeleton are
        // in, Skyrim's first-person view of its own player as before.
        void DrawCj(RE::PlayerCharacter* a_player, float a_fromHeading, float a_fromPitch)
        {
            const double        now = NowMs();
            Protocol::BodyState body{};
            if (LinkService::Get().ReadBody(body) && (body.flags & Protocol::kBodyValid) != 0 && body.handover == state.handover &&
                body.boneCount <= Protocol::kBodyBoneMax && Body::Finite(body)) {
                state.body.Push(body, now);
            }
            Protocol::BodyState pose{};
            if (!state.body.Sample(now, pose) || pose.handover != state.handover ||
                !ModelView::Show(pose, state.anchor + RE::NiPoint3(pose.feet[0], pose.feet[1], pose.feet[2]))) {
                HidePlayer(a_player, false);
                return;
            }
            HidePlayer(a_player, true);
            PoseCamera(pose, a_fromHeading, a_fromPitch);
            state.drawn++;
            if (!state.cjLogged) {
                state.cjLogged = true;
                SKSE::log::info("player sync: CJ drawn from San Andreas's model {:08X}, Skyrim's camera on San Andreas's (fov {:.0f})", pose.model,
                    pose.cameraFov);
            }
        }

        // San Andreas's vehicles where it has them, their parts posed, smoothed like CJ. A slot whose
        // vehicle is gone reads with no flags and draws nothing.
        void DrawVehicles()
        {
            const double now = NowMs();
            for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
                // A read that stayed torn keeps drawing from what came before.
                Protocol::BodyState vehicle{};
                if (LinkService::Get().ReadVehicle(i, vehicle)) {
                    if ((vehicle.flags & Protocol::kBodyValid) == 0 || vehicle.handover != state.handover) {
                        state.vehicles[i].Reset();
                        continue;
                    }
                    if (vehicle.boneCount <= Protocol::kBodyBoneMax && Body::Finite(vehicle)) {
                        state.vehicles[i].Push(vehicle, now);
                    }
                }
                Protocol::BodyState pose{};
                if (state.vehicles[i].Sample(now, pose)) {
                    ModelView::Show(pose, state.anchor + RE::NiPoint3(pose.feet[0], pose.feet[1], pose.feet[2]));
                }
            }
        }

        void Release(const char* a_reason)
        {
            if (!state.owned) {
                return;
            }
            state.owned = false;
            CollisionSampler::Stop();
            DrawSkyrimPlayer();
            SaHud::ClearText();
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
            state.frames = state.placed = state.newPoses = state.drawn = 0;
            state.body.Reset();
            state.cjLogged = false;
            Publish();
            auto* worldspace = a_player->GetWorldspace();
            CollisionSampler::Reset(state.handover, worldspace ? worldspace->GetFormID() : 0);
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
            // The angles Skyrim built this frame's camera for, before Place changes them.
            const float fromHeading = a_player->GetAngleZ();
            const float fromPitch = a_player->data.angle.x;
            if (valid) {
                Place(a_player, pose);
                state.placed++;
                if (pose.frame != state.lastPoseFrame) {
                    state.newPoses++;
                    state.lastPoseFrame = pose.frame;
                }
            }
            CollisionSampler::Update(a_player, valid ? RE::NiPoint3(pose.velocity[0], pose.velocity[1], pose.velocity[2]) : RE::NiPoint3());
            DrawCj(a_player, fromHeading, fromPitch);
            DrawVehicles();
            ModelView::EndFrame();
            HideSneakEye(!state.sneak);

            state.frames++;
            state.statTime += a_delta;
            if (state.statTime >= 1.0f) {
                SKSE::log::info(
                    "player sync: frames={} placed={} new poses={} cj drawn={} offset=({:.0f},{:.0f},{:.0f}) speed={:.1f} m/s heading={:.2f} view=({:.2f},{:.2f}){}",
                    state.frames, state.placed, state.newPoses, state.drawn, pose.offset[0], pose.offset[1], pose.offset[2],
                    std::hypot(pose.velocity[0], pose.velocity[1]) * Pose::kMetersPerUnit, pose.heading, pose.viewHeading,
                    pose.viewPitch, (pose.flags & Protocol::kPlayerInVehicle) ? " in a vehicle" : (pose.flags & Protocol::kPlayerGrounded) ? "" : " in the air");
                state.statTime = 0.0f;
                state.frames = state.placed = state.newPoses = state.drawn = 0;
            }
        }

        struct UpdateHook
        {
            static void thunk(RE::PlayerCharacter* a_player, float a_delta)
            {
                original(a_player, a_delta);
                // CJ's model comes whenever the link comes up, also while Skyrim has the player.
                LinkService::Get().Receive([](Protocol::MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
                    if (a_type == Protocol::MessageType::ModelChunk) {
                        ModelView::OnChunk(a_data, a_size);
                    }
                });
                if (state.owned && a_delta > 0.0f) {
                    Drive(a_player, a_delta);
                }
                SaHud::Update(a_delta, state.owned, MenuHasInput());
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
                    if (code == Input::kDikSlash && state.owned) {
                        if (button->IsDown() && !MenuHasInput()) {
                            SaHud::NextDemo();  // the dev key: San Andreas's text styles in turn
                        }
                        continue;
                    }
                    // A spawn key typed into the console or a menu spawns nothing.
                    state.input.SetKey(code, button->IsPressed(), !MenuHasInput());
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
