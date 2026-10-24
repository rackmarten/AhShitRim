#include "player/PlayerSync.h"

#include "Ini.h"
#include "anchor.h"
#include "audio/SaAudio.h"
#include "body/ModelView.h"
#include "body.h"
#include "camera.h"
#include "cheats.h"
#include "collision/CollisionSampler.h"
#include "debug/StreamTest.h"
#include "cosave.h"
#include "hud/Overlay.h"
#include "hud/SaHud.h"
#include "input.h"
#include "knock.h"
#include "link/LinkService.h"
#include "player/Activation.h"
#include "player/AmmuNation.h"
#include "player/Arms.h"
#include "player/CheatCodes.h"
#include "player/People.h"
#include "player/Police.h"
#include "player/GunRewards.h"
#include "player/Respawns.h"
#include "player/Looting.h"
#include "player/Conversation.h"
#include "player/Projectiles.h"
#include "player/Shots.h"
#include "player/VehicleFx.h"
#include "player/Vitals.h"
#include "pose.h"
#include "scene.h"
#include "vehicle_kinds.h"
#include "weapons.h"

namespace AhShitRim::PlayerSync
{
    namespace
    {
        // Main thread only: the input and menu sinks and the Update hook all run there.
        struct State
        {
            bool           owned = false;
            std::uint32_t  handover = 0;
            float          heading = 0.0f;   // the player's heading when the handover started, or the last re-anchor
            RE::NiPoint3   anchor;           // the player's position then
            bool           reanchoring = false;  // re-anchored, and CJ isn't drawn there yet: the player's 3D stays hidden
            bool           positioned = false;   // Skyrim positioned the player (PositionPlayerEvent) since the last anchor
            bool           placing = false;      // Place is moving the player for CJ: not Skyrim positioning it
            RE::NiPoint3   lastPlaced;
            RE::TESObjectCELL* cell = nullptr;
            Scene::Control control = Scene::Control::Cj;  // who moves the player this frame
            float          voiceRecovery = 0.0f;  // the player's shout cooldown last frame
            RE::FormID     seatLogged = 0;        // the furniture whose seat was last logged
            std::uint16_t  shoutCount = 0;
            bool           wasThirdPerson = false;
            Cosave::Record resume;               // what a loaded save held
            bool           resumePending = false;  // it had San Andreas on the player: take it again once the game is up
            Cosave::Car    carRequest;           // the saved car, sent along with the handover that resumes
            bool           carForNext = false;
            std::uint32_t  carHandover = 0;      // the handover the car request belongs to
            bool           sneak = false;     // what we last told Skyrim; its own flag can drop on its own
            int            sneakResend = 0;   // frames until a sneak event the graph ignored is sent again
            std::uint32_t  inputFrame = 0;
            Input::Builder input;
            Body::Interpolator body;      // CJ's skeleton and San Andreas's camera, smoothed
            Body::Interpolator vehicles[Protocol::kVehicleMax];  // each vehicle's parts, smoothed
            Body::Interpolator gear;      // v19: the jetpack or the parachute on CJ, smoothed
            VehicleKinds::DevPicker devPicker;  // v19: what ' spawns, ; steps through it
            bool           devKeys = true;      // [Debug] VehicleKeys
            bool           playerHidden = false;  // we culled the player's own 3D for CJ's
            RE::NiAVObject* culledRoot[2] = {};   // third- and first-person roots this culled
            std::vector<RE::NiPointer<RE::BSGeometry>> culledShapes;  // and their shapes
            float          savedFov = 0.0f;       // Skyrim's field of view before San Andreas's, 0 = not changed
            bool           cameraParentLogged = false;
            Camera::Matrix cameraOffset{};          // Skyrim's camera node on top of the player's angles, from the last frame without a menu
            bool           haveCameraOffset = false;
            bool           cameraAxes = false;      // the offset is only Skyrim's own axes (Camera::AxesOf)
            bool           cjLogged = false;      // logged the first frame CJ was drawn this handover
            bool           shopActivate = false;  // E pressed while Ammu-Nation is open, for its next frame
            Knock::Down    down;                  // the player knocked down (ragdolled) in Skyrim, and for how long

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

        // Where the player sits: the marker of the furniture it uses (Skyrim's process says which,
        // else the nearest), on the furniture's 3D. Its root goes there; its position can stay where
        // it got on (a carriage).
        bool SeatOf(RE::PlayerCharacter* a_player, RE::NiPoint3& a_at)
        {
            const auto furniture = a_player->GetOccupiedFurniture().get();
            auto*      root = furniture ? furniture->Get3D() : nullptr;
            auto*      node = root ? RE::BSFurnitureMarkerNode::FindBSFurnitureMarkerNode(root) : nullptr;
            if (!node || node->markers.empty()) {
                return false;
            }
            auto*               process = a_player->GetActorRuntimeData().currentProcess;
            const std::uint32_t id = process && process->middleHigh ? process->middleHigh->currentFurnitureMarkerID : UINT32_MAX;
            float               rotate[3][3];
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    rotate[r][c] = root->world.rotate.entry[r][c];
                }
            }
            const Pose::Vec3 translate{ root->world.translate.x, root->world.translate.y, root->world.translate.z };
            const auto       feet = a_player->GetPosition();
            std::uint32_t    best = id;
            Pose::Vec3       at{};
            float            nearest = (std::numeric_limits<float>::max)();
            for (std::uint32_t i = 0; i < node->markers.size(); i++) {
                const auto& m = node->markers[i].offset;
                const auto  p = Scene::MarkerInWorld(rotate, translate, root->world.scale, { m.x, m.y, m.z });
                const float d = std::hypot(p.x - feet.x, p.y - feet.y, p.z - feet.z);
                if (i == id || (id >= node->markers.size() && d < nearest)) {
                    at = p;
                    best = i;
                    nearest = d;
                }
            }
            if (best >= node->markers.size()) {
                return false;
            }
            a_at = RE::NiPoint3(at.x, at.y, at.z);
            if (state.seatLogged != furniture->GetFormID()) {
                state.seatLogged = furniture->GetFormID();
                SKSE::log::info("player sync: the player sits on {:08X} at marker {} of {} (process says {}), {:.0f} units from its position",
                    furniture->GetFormID(), best, node->markers.size(), id, a_at.GetDistance(feet));
            }
            return true;
        }

        void Publish(bool a_menu = MenuHasInput(), bool a_paused = GamePaused())
        {
            auto& link = LinkService::Get();
            if (!link.IsUp()) {
                return;
            }
            const bool wasted = state.owned && (Vitals::IsWasted() || Police::IsBusted());  // the player is dying or arrested: CJ stands still
            // In Ammu-Nation the keys are the shop's: CJ stands still.
            auto input = state.input.Build(++state.inputFrame, state.owned, !a_menu && !wasted && !AmmuNation::IsOpen(), a_paused);
            if (state.owned && Vitals::IsDown()) {
                input.flags |= Protocol::kInputWasted | (Vitals::HasDeathCamera() ? Protocol::kInputWastedCamera : 0u);
            }
            if (state.owned && Police::IsArrested()) {
                input.flags |= Protocol::kInputBusted;  // v22: dragged out, knocked off, or his hands up
            }
            if (state.owned && state.carHandover == state.handover && state.carRequest.model) {
                input.carModel = state.carRequest.model;
                std::copy(std::begin(state.carRequest.colours), std::end(state.carRequest.colours), input.carColours);
                std::copy(std::begin(state.carRequest.offset), std::end(state.carRequest.offset), input.carOffset);
                input.carHeading = state.carRequest.heading;
            }
            input.handover = state.handover;
            input.heading = state.heading;
            input.anchor[0] = state.anchor.x;
            input.anchor[1] = state.anchor.y;
            input.anchor[2] = state.anchor.z;
            input.shoutCount = state.shoutCount;
            if (auto* player = RE::PlayerCharacter::GetSingleton(); player && state.owned) {
                const auto feet = player->GetPosition();
                RE::NiPoint3 at = feet;
                if (state.control == Scene::Control::Seated) {
                    SeatOf(player, at);
                }
                const float offset[3] = { at.x - state.anchor.x, at.y - state.anchor.y, at.z - state.anchor.z };
                Scene::Follow(input, state.control, offset, player->GetAngleZ());
                // Talking: CJ turns to the speaker while the dialogue (and a barter or gift over it) is open.
                auto* topics = RE::MenuTopicManager::GetSingleton();
                if (auto speaker = topics && topics->menuOpen && a_menu ? topics->speaker.get() : nullptr) {
                    const auto at = speaker->GetPosition();
                    Scene::Face(input, feet.x, feet.y, at.x, at.y);
                }
            }
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
        // Its shapes are culled one by one too: getting into a carriage, something showed the root
        // again after this culled it, and the player climbed in next to CJ.
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
                } else {
                    RE::BSVisit::TraverseScenegraphGeometries(root, [](RE::BSGeometry* a_shape) {
                        if (!a_shape->GetAppCulled()) {
                            a_shape->SetAppCulled(true);
                            state.culledShapes.emplace_back(a_shape);
                        }
                        return RE::BSVisit::BSVisitControl::kContinue;
                    });
                }
            }
            if (!a_hide) {
                for (auto& shape : state.culledShapes) {
                    shape->SetAppCulled(false);
                }
                state.culledShapes.clear();
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
            Arms::Hide();
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
            state.gear.Reset();
        }

        constexpr float kCameraShakeMax = 0.5f;  // radians Skyrim's camera may be off its own axes and still count as a shake

        // Puts Skyrim's camera where San Andreas's is (BodyState camera, relative to the handover
        // spot), looking where it looks, with its field of view, so the view behind CJ is San
        // Andreas's own. Skyrim's camera stays in its first-person state, which only draws the
        // player's hidden arms; Skyrim builds the camera before this hook (from the angles the
        // player had, a_fromHeading and a_fromPitch), so the finished camera node is overridden, as
        // PerkeleRim does for MSC's camera.
        bool PoseCamera(const Protocol::BodyState& a_body, float a_fromHeading, float a_fromPitch, bool a_menu)
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
            // In a dialogue Skyrim turns its camera to the speaker on top of the player's angles;
            // San Andreas's camera stays where it was, so the offset from before the menu is kept.
            // Only Skyrim's own axes are kept from it: a hit's shake, or Improved Camera turning the view
            // with the hidden player's head as it staggers, would otherwise shake San Andreas's camera.
            // A bigger turn than a shake keeps the axes from before.
            if (!a_menu || !state.haveCameraOffset) {
                Camera::Matrix offset;
                Camera::OffsetOf(built, a_fromHeading, a_fromPitch, offset);
                if (Camera::AxesOf(offset, kCameraShakeMax, state.cameraOffset)) {
                    state.cameraAxes = true;
                } else if (!state.haveCameraOffset || !state.cameraAxes) {
                    // No axes yet: the whole offset, as it is, until there are.
                    for (int r = 0; r < 3; r++) {
                        for (int c = 0; c < 3; c++) {
                            state.cameraOffset[r][c] = offset[r][c];
                        }
                    }
                    state.cameraAxes = false;
                }
                state.haveCameraOffset = true;
            }
            // With San Andreas's roll: looking down, its death camera swings about the view. In a
            // dialogue the cutscene's shot instead (Conversation), level.
            auto                 at = state.anchor + RE::NiPoint3(a_body.cameraPosition[0], a_body.cameraPosition[1], a_body.cameraPosition[2]);
            float                fov = a_body.cameraFov;
            auto                 angles = Camera::AnglesOf(a_body.cameraForward, a_body.cameraUp);
            const RE::NiPoint3   feet = state.anchor + RE::NiPoint3(a_body.feet[0], a_body.feet[1], a_body.feet[2]);
            Dialogue::CameraShot shot;
            if (Conversation::Shot(RE::PlayerCharacter::GetSingleton(), feet, at, shot)) {
                const float forward[3] = { shot.forward.x, shot.forward.y, shot.forward.z };
                constexpr float up[3] = { 0.0f, 0.0f, 1.0f };
                angles = Camera::AnglesOf(forward, up);
                at = RE::NiPoint3(shot.position.x, shot.position.y, shot.position.z);
                fov = shot.fov;
            }
            Camera::Apply(state.cameraOffset, angles.heading, angles.pitch, angles.roll, posed);
            RE::NiMatrix3 rotate;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    rotate.entry[r][c] = posed[r][c];
                }
            }
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
            if (fov > 1.0f && fov < 170.0f) {
                auto& data = camera->GetRuntimeData2();
                if (state.savedFov <= 0.0f) {
                    state.savedFov = data.worldFOV;
                }
                data.worldFOV = fov;
            }
            return true;
        }

        // CJ drawn where San Andreas has him, posed by his skeleton, and Skyrim's camera on San
        // Andreas's, both smoothed between San Andreas's frames. Until his model and skeleton are
        // in, Skyrim's first-person view of its own player as before.
        void DrawCj(RE::PlayerCharacter* a_player, float a_fromHeading, float a_fromPitch, bool a_menu, bool a_hideBody)
        {
            const double        now = NowMs();
            Protocol::BodyState body{};
            if (LinkService::Get().ReadBody(body) && (body.flags & Protocol::kBodyValid) != 0 && body.handover == state.handover &&
                body.boneCount <= Protocol::kBodyBoneMax && Body::Finite(body)) {
                state.body.Push(body, now);
            }
            Protocol::BodyState pose{};
            // Through a scope (sniper rifle, rocket launcher, camera) San Andreas's camera sits in CJ's head: he isn't drawn.
            if (!state.body.Sample(now, pose) || pose.handover != state.handover ||
                (!a_hideBody && !ModelView::Show(pose, state.anchor + RE::NiPoint3(pose.feet[0], pose.feet[1], pose.feet[2])))) {
                HidePlayer(a_player, state.reanchoring && state.playerHidden);  // CJ comes back in a frame or two
                return;
            }
            HidePlayer(a_player, true);
            state.reanchoring = false;
            PoseCamera(pose, a_fromHeading, a_fromPitch, a_menu);
            state.drawn++;
            if (!state.cjLogged) {
                state.cjLogged = true;
                SKSE::log::info("player sync: CJ drawn from San Andreas's model {:08X}, Skyrim's camera on San Andreas's (fov {:.0f})", pose.model,
                    pose.cameraFov);
            }
        }

        // San Andreas's vehicles where it has them, their parts posed, smoothed like CJ, with their damage's
        // smoke and fire (VehicleFx). A slot whose vehicle is gone reads with no flags and draws nothing.
        void DrawVehicles(RE::PlayerCharacter* a_player, float a_delta)
        {
            const double now = NowMs();
            for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
                // A read that stayed torn keeps drawing from what came before.
                Protocol::BodyState vehicle{};
                if (LinkService::Get().ReadVehicle(i, vehicle)) {
                    if ((vehicle.flags & Protocol::kBodyValid) == 0 || vehicle.handover != state.handover) {
                        state.vehicles[i].Reset();
                        VehicleFx::Show(a_player, i, nullptr, {}, state.handover, a_delta);
                        continue;
                    }
                    if (vehicle.boneCount <= Protocol::kBodyBoneMax && Body::Finite(vehicle)) {
                        state.vehicles[i].Push(vehicle, now);
                    }
                }
                Protocol::BodyState pose{};
                if (state.vehicles[i].Sample(now, pose)) {
                    const RE::NiPoint3 feet = state.anchor + RE::NiPoint3(pose.feet[0], pose.feet[1], pose.feet[2]);
                    ModelView::Show(pose, feet);
                    VehicleFx::Show(a_player, i, &pose, feet, pose.handover, a_delta);
                }
            }
        }

        // CJ's gear (v19): the jetpack on his back or the open parachute over him, posed like his weapons, smoothed.
        void DrawGear()
        {
            const double        now = NowMs();
            Protocol::BodyState gear{};
            if (LinkService::Get().ReadGear(gear)) {
                if ((gear.flags & Protocol::kBodyValid) == 0 || gear.handover != state.handover) {
                    state.gear.Reset();
                    return;
                }
                if (gear.boneCount <= Protocol::kBodyBoneMax && Body::Finite(gear)) {
                    state.gear.Push(gear, now);
                }
            }
            Protocol::BodyState pose{};
            if (state.gear.Sample(now, pose)) {
                ModelView::Show(pose, state.anchor + RE::NiPoint3(pose.feet[0], pose.feet[1], pose.feet[2]));
            }
        }

        void Release(const char* a_reason)
        {
            if (!state.owned) {
                return;
            }
            state.owned = false;
            state.reanchoring = false;
            state.control = Scene::Control::Cj;
            Vitals::OnRelease(RE::PlayerCharacter::GetSingleton());
            Police::OnRelease();
            CollisionSampler::Stop();
            Activation::Clear();
            AmmuNation::Close();
            Conversation::Clear();
            Shots::Clear();
            Projectiles::Clear();
            VehicleFx::Clear();
            People::Clear();
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

        // A new anchor at the player's feet: a handover, or a re-anchor. San Andreas puts CJ there.
        void AnchorAt(RE::PlayerCharacter* a_player)
        {
            state.handover++;
            state.carHandover = state.carForNext ? state.handover : 0;
            state.carForNext = false;
            state.positioned = false;
            state.anchor = a_player->GetPosition();
            state.lastPlaced = state.anchor;
            state.cell = a_player->GetParentCell();
            state.heading = a_player->GetAngleZ();
            state.statTime = 0.0f;
            state.frames = state.placed = state.newPoses = state.drawn = 0;
            state.body.Reset();
            state.cjLogged = false;
            Publish();  // before the chunks: San Andreas takes this anchor's chunks once it has read it
            auto* worldspace = a_player->GetWorldspace();
            CollisionSampler::Reset(state.handover, worldspace ? worldspace->GetFormID() : 0);
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
            state.control = Scene::Control::Cj;
            state.down.Reset();
            Vitals::OnTakeOver(a_player);
            state.haveCameraOffset = false;
            state.voiceRecovery = a_player->GetVoiceRecoveryTime();  // a cooldown already running isn't a shout
            AnchorAt(a_player);
            SKSE::log::info("player sync: San Andreas owns the player (handover {} at ({:.0f},{:.0f},{:.0f}) heading {:.3f})",
                state.handover, state.anchor.x, state.anchor.y, state.anchor.z, state.heading);
        }

        // The game moved the player (a load door, fast travel, a script) while CJ has it: CJ goes
        // along. San Andreas drops its ground and every vehicle (a car is left behind), puts CJ at
        // the new anchor and holds him there until the ground under him is in; the handover goes on.
        void Reanchor(RE::PlayerCharacter* a_player, Anchor::Move a_move)
        {
            const auto from = state.anchor;
            if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && !camera->IsInFirstPerson()) {
                camera->ForceFirstPerson();  // a load can put Skyrim's camera back in its own state
            }
            for (auto& vehicle : state.vehicles) {
                vehicle.Reset();
            }
            state.gear.Reset();
            VehicleFx::Clear();
            People::Clear();
            Activation::Clear();
            AmmuNation::Close();
            state.reanchoring = true;
            Vitals::OnReanchor();
            Police::OnReanchor();
            AnchorAt(a_player);
            auto* cell = a_player->GetParentCell();
            SKSE::log::info("player sync: the player {}, CJ re-anchored (handover {} at ({:.0f},{:.0f},{:.0f}) heading {:.3f}, {} {:08X}, from ({:.0f},{:.0f},{:.0f}))",
                Anchor::NameOf(a_move), state.handover, state.anchor.x, state.anchor.y, state.anchor.z, state.heading,
                cell && cell->IsInteriorCell() ? "interior" : "exterior", cell ? cell->GetFormID() : 0, from.x, from.y, from.z);
        }

        // Whether the game moved the player since CJ put it there last frame.
        Anchor::Move MovedByGame(RE::PlayerCharacter* a_player)
        {
            auto*         cell = a_player->GetParentCell();
            Anchor::Frame frame;
            frame.hasCell = cell != nullptr;
            frame.cellChanged = cell != state.cell;
            frame.interior = cell && cell->IsInteriorCell();
            frame.wasInterior = state.cell && state.cell->IsInteriorCell();
            frame.worldspaceChanged = cell && state.cell && cell->GetRuntimeData().worldSpace != state.cell->GetRuntimeData().worldSpace;
            frame.jump = a_player->GetPosition().GetDistance(state.lastPlaced);
            frame.positioned = state.positioned;
            const auto move = Anchor::Classify(frame);
            if (move == Anchor::Move::None) {
                state.cell = cell;  // walked into the next exterior cell
            }
            return move;
        }

        void Place(RE::PlayerCharacter* a_player, const Protocol::PlayerState& a_pose)
        {
            state.lastPlaced = state.anchor + RE::NiPoint3(a_pose.offset[0], a_pose.offset[1], a_pose.offset[2]);
            state.placing = true;
            a_player->SetPosition(state.lastPlaced, true);
            state.placing = false;
            a_player->SetHeading(a_pose.viewHeading);  // first person: the body turns with the view
            a_player->data.angle.x = a_pose.viewPitch;
            if (auto* controller = a_player->GetCharController()) {
                controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f));
                // San Andreas works out CJ's falls (Vitals takes them off the player); the player itself never falls, so
                // Skyrim's own fall damage on landing (a jetpack, a parachute, a jump off a bike) would come on top.
                controller->fallStartHeight = state.lastPlaced.z;
                controller->fallTime = 0.0f;
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

        // Who moves the player this frame: CJ, or Skyrim in a scene (controls taken away, a package
        // walking the player, a bed) or a seat, where CJ follows the player instead.
        Scene::Control ControlOf(RE::PlayerCharacter* a_player)
        {
            Scene::PlayerFrame frame;
            if (auto* actorState = a_player->AsActorState()) {
                frame.sitSleepState = static_cast<std::uint8_t>(actorState->GetSitSleepState());
            }
            auto* controls = RE::ControlMap::GetSingleton();
            frame.movementEnabled = !controls || controls->IsMovementControlsEnabled();
            const auto& movement = a_player->GetActorRuntimeData().movementController;
            frame.aiDriven = movement && movement->GetAIDriven();
            const auto control = Scene::Classify(frame);
            if (control != state.control) {
                SKSE::log::info("player sync: {} moves the player (sit state {}, movement controls {}, AI driven {})",
                    control == Scene::Control::Cj ? "CJ" : std::string("Skyrim, ") + Scene::NameOf(control), frame.sitSleepState,
                    frame.movementEnabled ? "on" : "off", frame.aiDriven);
            }
            return control;
        }

        // Whether CJ drives or rides (the last pose says): E then is San Andreas's look right, not Skyrim's activate.
        bool InVehicle()
        {
            Protocol::PlayerState pose{};
            return LinkService::Get().ReadPlayer(pose) && pose.handover == state.handover && (pose.flags & Protocol::kPlayerInVehicle) != 0;
        }

        // A cheat's vehicle, jetpack or parachute (CheatCodes), asked for as a spawn key asks. Returns why not, or nothing.
        std::string CheatSpawn(std::uint16_t a_model)
        {
            if (!VehicleKinds::Spawnable(a_model)) {
                return std::string(VehicleKinds::TraitsOf(VehicleKinds::KindOf(a_model)).refusal);
            }
            if (!VehicleKinds::IsGear(a_model) && InVehicle()) {
                return "Get out of the vehicle first";  // San Andreas spawns vehicles for CJ on foot
            }
            state.input.RequestSpawn(a_model);
            Publish();
            return {};
        }

        // The model of the vehicle CJ is in (the last pose says), 0 on foot.
        std::uint16_t CjVehicle()
        {
            Protocol::PlayerState pose{};
            return LinkService::Get().ReadPlayer(pose) && pose.handover == state.handover && (pose.flags & Protocol::kPlayerInVehicle) != 0 ? pose.vehicleModel : 0;
        }

        // Whether the vehicle CJ drives has San Andreas's hydraulics (its slot says so, v20).
        bool CjHydraulics()
        {
            for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
                Protocol::BodyState vehicle{};
                if (LinkService::Get().ReadVehicle(i, vehicle) && (vehicle.flags & Protocol::kBodyValid) && (vehicle.flags & Protocol::kBodyDriven) &&
                    vehicle.handover == state.handover) {
                    return (vehicle.flags & Protocol::kBodyHydraulics) != 0;
                }
            }
            return false;
        }

        // A key pressed while CJ has the player goes into San Andreas's cheat string (CheatCodes).
        void TypeCheat(std::uint32_t a_dik)
        {
            if (state.owned && !MenuHasInput() && !Vitals::IsWasted() && !Police::IsBusted() && Cheats::LetterOf(a_dik)) {
                static const CheatCodes::World world{ CheatSpawn, CjVehicle, CjHydraulics };
                CheatCodes::Key(a_dik, world);
            }
        }

        // Skyrim's own Shout key fires the shout from the player, which stands where CJ does and
        // looks where San Andreas's camera looks; San Andreas plays CJ's gesture when it goes off.
        void WatchShout(RE::PlayerCharacter* a_player)
        {
            const float recovery = a_player->GetVoiceRecoveryTime();
            if (Scene::ShoutReleased(state.voiceRecovery, recovery)) {
                state.shoutCount++;
                const auto* power = a_player->GetActorRuntimeData().selectedPower;
                SKSE::log::info("player sync: the player shouted ({}, {:.0f} s cooldown), CJ gestures", power ? power->GetName() : "?", recovery);
            }
            state.voiceRecovery = recovery;
        }

        // Whether a blow knocked the player down in Skyrim (a giant's club, a mammoth, a shout): ragdolled, or getting up.
        bool KnockedDown(RE::PlayerCharacter* a_player)
        {
            const auto* actorState = a_player->AsActorState();
            return (actorState && actorState->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) || a_player->IsInRagdollState();
        }

        const char* KnockStateName(RE::PlayerCharacter* a_player)
        {
            static constexpr const char* kNames[] = { "normal", "explode", "explode lead-in", "out", "out lead-in", "queued", "get-up", "down", "waiting" };
            const auto* actorState = a_player->AsActorState();
            const auto  knock = actorState ? static_cast<std::size_t>(actorState->GetKnockState()) : 0;
            return knock < std::size(kNames) ? kNames[knock] : "?";
        }

        // Down too long (Knock::kDownMost): Skyrim's own fix for an actor stuck in its ragdoll, and its knock state put back to
        // normal (in game the player was stuck queued and ragdolled, and only the reset got it up). The log says which state
        // it was stuck in and whether it took.
        void GetUp(RE::PlayerCharacter* a_player, int a_try)
        {
            const char* before = KnockStateName(a_player);
            const bool  ragdoll = a_player->IsInRagdollState();
            a_player->PotentiallyFixRagdollState();
            if (auto* actorState = a_player->AsActorState(); actorState && actorState->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) {
                actorState->actorState1.knockState = RE::KNOCK_STATE_ENUM::kNormal;
            }
            SKSE::log::info("player sync: the player is still down after {:.1f} s (knock state {}{}), made to get up (try {}): now {}{}, back where CJ is",
                state.down.Seconds(), before, ragdoll ? ", ragdolled" : "", a_try, KnockStateName(a_player), a_player->IsInRagdollState() ? ", still ragdolled" : "");
        }

        // San Andreas throws CJ away from the blow (Knock), as its explosions throw peds.
        void ThrowCj(RE::PlayerCharacter* a_player, const Protocol::PlayerState* a_pose)
        {
            RE::NiPoint3 from;
            const RE::NiPoint3 feet = a_pose ? state.anchor + RE::NiPoint3(a_pose->offset[0], a_pose->offset[1], a_pose->offset[2]) : a_player->GetPosition();
            if (!Police::LastBlow(Knock::kBlowSeconds, from)) {
                const float heading = a_player->GetAngleZ();
                from = feet + RE::NiPoint3(std::sin(heading), std::cos(heading), 0.0f) * 100.0f;  // no blow seen: from in front
            }
            Protocol::EffectEvent effect{};
            effect.handover = state.handover;
            effect.kind = static_cast<std::uint8_t>(Protocol::EffectKind::KnockCj);
            const auto at = from - state.anchor;
            effect.at[0] = at.x;
            effect.at[1] = at.y;
            effect.at[2] = at.z;
            effect.seconds = Knock::kSpeed;
            const bool sent = LinkService::Get().Send(Protocol::MessageType::Effect, &effect, sizeof(effect));
            SKSE::log::info("player sync: a blow from ({:.0f},{:.0f}) knocked the player down: it falls out of sight, San Andreas throws CJ{}", from.x, from.y,
                sent ? "" : " (the link is down)");
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
            if (const auto move = MovedByGame(a_player); Anchor::Reanchors(move)) {
                Reanchor(a_player, move);
            }
            // Dying or arrested, the player is nobody's to move: CJ stands where he is.
            if (const auto control = Vitals::IsWasted() || Police::IsBusted() ? Scene::Control::Cj : ControlOf(a_player); control != state.control) {
                state.control = control;
                state.seatLogged = 0;
                Activation::Clear();
            }
            const bool scene = state.control != Scene::Control::Cj;
            if (scene) {
                state.lastPlaced = a_player->GetPosition();  // Skyrim moving the player in a scene isn't a jump
            }
            WatchShout(a_player);
            // A dialogue's cutscene: CJ greets, answers each topic and says goodbye in his own voice.
            if (const auto line = Conversation::Update(a_delta); line != Dialogue::Category::None && !Vitals::IsDown()) {
                state.input.AddLine(static_cast<std::uint8_t>(line));
            }
            ReleaseStuckKeys();
            Publish();

            Protocol::PlayerState pose{};
            const bool valid = link.ReadPlayer(pose) && (pose.flags & Protocol::kPlayerValid) != 0 &&
                               pose.handover == state.handover && Pose::Finite(pose);
            // The angles Skyrim built this frame's camera for, before Place changes them.
            const float fromHeading = a_player->GetAngleZ();
            const float fromPitch = a_player->data.angle.x;
            // Knocked down, Skyrim's player falls and gets up out of sight, unpinned (pinned to CJ every frame its ragdoll never
            // settled and its camera stuck), while San Andreas throws CJ; then it goes back to where he is. It may stay down
            // no longer than CJ does (Knock::Down): then it's made to get up and goes back to him anyway, so the pursuers
            // don't keep swinging at where it lies.
            const bool knocked = KnockedDown(a_player);
            switch (state.down.Tick(a_delta, knocked)) {
            case Knock::Step::Fell:
                ThrowCj(a_player, valid ? &pose : nullptr);
                Police::OnKnockedDown(a_player);
                break;
            case Knock::Step::Force:
                GetUp(a_player, state.down.Tries());
                Police::Retarget(a_player);
                break;
            case Knock::Step::Up:
                SKSE::log::info("player sync: the player is up again, back where CJ is");
                Police::Retarget(a_player);
                break;
            default: break;
            }
            const bool unpinned = state.down.Unpinned();
            if (unpinned) {
                state.lastPlaced = a_player->GetPosition();  // its fall isn't the game moving it
            }
            if (valid && !scene && !unpinned) {  // in a scene Skyrim moves the player, and San Andreas puts CJ there
                Place(a_player, pose);
                state.placed++;
                if (pose.frame != state.lastPoseFrame) {
                    state.newPoses++;
                    state.lastPoseFrame = pose.frame;
                }
            }
            Vitals::Update(a_player, valid ? &pose : nullptr, !state.reanchoring);
            Police::Update(a_player, state.anchor, valid ? &pose : nullptr, !state.reanchoring, scene, a_delta);
            if (const auto line = Police::TakeLine(); line && !Vitals::IsDown()) {
                state.input.AddLine(line);  // CJ chased or arrested
            }
            const bool held = Vitals::IsWasted() || Police::IsBusted();  // dying or arrested
            if (held || ((knocked || state.control == Scene::Control::Cj) && !MenuHasInput())) {
                // Bleeding out, busted or knocked down, Skyrim may take its camera elsewhere (its ragdoll and bleedout cameras);
                // San Andreas's drives the view whenever CJ is his own.
                if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && !camera->IsInFirstPerson()) {
                    camera->ForceFirstPerson();
                }
            }
            CollisionSampler::Update(a_player, valid ? RE::NiPoint3(pose.velocity[0], pose.velocity[1], pose.velocity[2]) : RE::NiPoint3());
            // Ammu-Nation: open until CJ leaves it (or walks off, or Skyrim takes the player over, or he dies).
            if (AmmuNation::IsOpen() && (scene || held)) {
                AmmuNation::Close();
            }
            if (AmmuNation::IsOpen() && !MenuHasInput()) {
                const auto actions = state.input.Build(0, true, true);
                AmmuNation::Update(a_player, { actions.moveY, actions.buttons, std::exchange(state.shopActivate, false) }, a_delta, valid ? &pose : nullptr);
            }
            Activation::Update(a_player, valid ? &pose : nullptr, MenuHasInput() || scene || AmmuNation::IsOpen());
            const auto scope = valid ? Weapons::ScopeOf(pose.cameraMode) : Weapons::Scope::None;
            const bool lookingThrough = scope == Weapons::Scope::Sniper || scope == Weapons::Scope::Rocket || scope == Weapons::Scope::Camera;
            DrawCj(a_player, fromHeading, fromPitch, MenuHasInput(), lookingThrough);
            DrawVehicles(a_player, a_delta);
            DrawGear();
            if (valid) {
                Arms::Draw(state.anchor, state.handover, pose, NowMs(), a_delta, lookingThrough);
            }
            Shots::Update(a_player, state.anchor, state.handover);
            const RE::NiPoint3 cjFeet = state.anchor + (valid ? RE::NiPoint3(pose.offset[0], pose.offset[1], pose.offset[2]) : RE::NiPoint3());
            Looting::Update(a_player, cjFeet, valid && !(pose.flags & Protocol::kPlayerInVehicle) && !scene && !held && !MenuHasInput(), a_delta);
            Projectiles::Update(a_player, state.anchor, state.handover, a_delta, valid && (pose.flags & Protocol::kPlayerOnFire) ? &cjFeet : nullptr,
                valid ? pose.weaponType : 0, valid && (pose.weaponFlags & Protocol::kWeaponAiming));
            People::Update(a_player, state.anchor, valid ? &pose : nullptr, scene || MenuHasInput());
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

        // A save that had San Andreas on the player: once the game is up and the link too, the handover
        // resumes, and the car it was in goes back under CJ.
        void TryResume(RE::PlayerCharacter* a_player)
        {
            auto* ui = RE::UI::GetSingleton();
            if (!LinkService::Get().IsUp() || (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) || !a_player->Is3DLoaded() || !a_player->GetParentCell() ||
                StreamTest::Running()) {
                return;  // the stream test moves the player first; CJ comes back after it
            }
            state.resumePending = false;
            state.carRequest = state.resume.car;
            state.carForNext = state.carRequest.model != 0;
            SKSE::log::info("player sync: the loaded save had San Andreas on the player, resuming{}",
                state.carForNext ? std::format(" in a vehicle of model {}", state.carRequest.model) : std::string());
            TakeOver(a_player);
            state.carForNext = false;
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
                    } else if (a_type == Protocol::MessageType::Shot && state.owned) {
                        Shots::Queue(a_data, a_size);
                    } else if (a_type == Protocol::MessageType::Projectile && state.owned) {
                        Projectiles::Queue(a_data, a_size);
                    }
                });
                if (state.owned && a_delta > 0.0f) {
                    Drive(a_player, a_delta);
                } else if (state.resumePending && !state.owned) {
                    TryResume(a_player);
                }
                if (!state.owned) {
                    StreamTest::Update(a_player, a_delta);
                }
                Arms::Update();
                CheatCodes::Update();
                Police::Sweep();
                GunRewards::Update();
                Respawns::Update(state.owned, a_delta);
                Protocol::PlayerState pose{};
                const bool posed = state.owned && LinkService::Get().ReadPlayer(pose) && pose.handover == state.handover;
                SaHud::SetWeapon(posed ? &pose : nullptr);
                if (Overlay::ScreenshotSaved()) {
                    SaHud::ShowHelp("Screenshot saved", 3.0f);
                }
                SaHud::Update(a_delta, state.owned, MenuHasInput());
                Audio::Update(a_delta, state.owned, GamePaused(), posed ? &pose : nullptr);
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };

        constexpr int kMovementHook = 0;
        constexpr int kActivateHook = 11;

        // Skyrim's own handlers refuse while San Andreas has the player, but for its movement and
        // activate keys while the player sits: those stand it up again. In a dialogue the activate
        // key skips the speaker's line, as in Skyrim. Its Shout key isn't blocked.
        template <int N>
        struct BlockHook
        {
            static bool thunk(RE::PlayerInputHandler* a_handler, RE::InputEvent* a_event)
            {
                const bool standUp = (N == kMovementHook || N == kActivateHook) && state.control == Scene::Control::Seated;
                const bool talking = N == kActivateHook && Conversation::DialogueOpen();
                return (!state.owned || standUp || talking) && original(a_handler, a_event);
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

        // Only Escape, Tab and the console open Skyrim's menus while San Andreas has the player (Input::OpensSkyrimMenu).
        // M, the map, doesn't: it's a letter of the cheat codes, and the map is in Tab's menu.
        struct MenuOpenHook
        {
            static bool thunk(RE::MenuOpenHandler* a_handler, RE::InputEvent* a_event)
            {
                if (state.owned && a_event && a_event->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                    if (auto* button = a_event->AsButtonEvent(); button && !Input::OpensSkyrimMenu(button->GetIDCode())) {
                        return false;
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
                        if (MenuHasInput() || AmmuNation::IsOpen()) {
                            continue;  // moves the menu's cursor, not San Andreas's camera
                        }
                        state.input.AddMouse(move->mouseInputX, move->mouseInputY);
                        changed = true;
                        continue;
                    }
                    auto* button = e->AsButtonEvent();
                    if (button && button->GetDevice() == RE::INPUT_DEVICE::kMouse) {
                        if (!state.owned) {
                            continue;
                        }
                        const auto id = button->GetIDCode();
                        const bool forCj = !MenuHasInput() && !AmmuNation::IsOpen();  // CJ's to use: no menu or shop has the input
                        if (id == RE::BSWin32MouseDevice::Key::kLeftButton) {
                            state.input.SetFire(button->IsPressed() && forCj);  // fires the weapon on foot
                            changed = true;
                        } else if (id == RE::BSWin32MouseDevice::Key::kRightButton) {
                            state.input.SetAim(button->IsPressed() && forCj);
                            changed = true;
                        } else if (id == RE::BSWin32MouseDevice::Key::kMiddleButton) {
                            // The swear key: CJ swears to himself, San Andreas plays it.
                            if (button->IsDown() && forCj && state.control == Scene::Control::Cj && !Vitals::IsDown()) {
                                state.input.AddSwear();
                                changed = true;
                            }
                        } else if (button->IsDown() && forCj &&
                                   (id == RE::BSWin32MouseDevice::Key::kWheelUp || id == RE::BSWin32MouseDevice::Key::kWheelDown)) {
                            state.input.AddWheel(id == RE::BSWin32MouseDevice::Key::kWheelUp);  // the radio in a vehicle, the weapons on foot
                            changed = true;
                        }
                        continue;
                    }
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
                    if (code == Input::kDikF11) {
                        // A screenshot of what's on screen, San Andreas's HUD and Skyrim's own included.
                        if (button->IsDown()) {
                            Overlay::Screenshot(Ini::Value("Screenshots", "Dir"));  // the launcher's Screenshots folder
                        }
                        continue;
                    }
                    // Every letter goes into San Andreas's cheat string as well.
                    if (button->IsDown()) {
                        TypeCheat(code);
                    }
                    if (code == Input::kDikX && state.owned) {
                        // The insult key: CJ insults whoever's about, San Andreas plays it.
                        if (button->IsDown() && !MenuHasInput() && !AmmuNation::IsOpen() && state.control == Scene::Control::Cj && !Vitals::IsDown()) {
                            state.input.AddInsult();
                            changed = true;
                        }
                        continue;
                    }
                    if (code == Input::kDikE && state.owned && !InVehicle()) {
                        if (button->IsDown() && !MenuHasInput() && !Police::IsBusted()) {
                            if (AmmuNation::IsOpen()) {
                                state.shopActivate = true;  // buys, in the shop
                            } else if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                                Activation::Activate(player, state.input.Layout());  // what CJ faces, through Skyrim's own activation
                            }
                        }
                        continue;
                    }
                    if (code == Input::kDikEquals && state.owned && Arms::DebugKeyEnabled()) {
                        if (button->IsDown() && !MenuHasInput()) {
                            Arms::DebugKey();  // the dev key: San Andreas's next weapon set
                        }
                        continue;
                    }
                    if (code == Input::kDikSemicolon && state.owned && state.devKeys) {
                        if (button->IsDown() && !MenuHasInput()) {
                            NextDevPick();  // the dev key: the next vehicle ' spawns
                        }
                        continue;
                    }
                    if (code == Input::kDikApostrophe && state.owned && state.devKeys && button->IsDown() && !MenuHasInput() &&
                        !VehicleKinds::Spawnable(state.input.DevModel())) {
                        const auto model = state.input.DevModel();
                        SaHud::ShowHelp(std::string(VehicleKinds::TraitsOf(VehicleKinds::KindOf(model)).refusal), 4.0f);
                        SKSE::log::info("player sync: the dev key would spawn {}, which can't come: {}", VehicleKinds::Label(model),
                            VehicleKinds::TraitsOf(VehicleKinds::KindOf(model)).refusal);
                        continue;
                    }
                    if (code == Input::kDikApostrophe && !state.devKeys) {
                        continue;
                    }
                    if (code == Input::kDikSlash && state.owned) {
                        if (button->IsDown() && !MenuHasInput()) {
                            SaHud::NextDemo();  // the dev key: San Andreas's text styles in turn
                        }
                        continue;
                    }
                    // A spawn key typed into the console or a menu (or the shop) spawns nothing.
                    state.input.SetKey(code, button->IsPressed(), !MenuHasInput() && !AmmuNation::IsOpen());
                    changed = true;
                }
                if (changed && state.owned) {
                    Publish();
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            static void NextDevPick()
            {
                const auto model = state.devPicker.Next();
                state.input.SetDevModel(model);
                const bool can = VehicleKinds::Spawnable(model);
                SaHud::ShowHelp(can ? std::format("Dev pick: {}. ' spawns it", VehicleKinds::Label(model))
                                    : std::format("Dev pick: {}, which can't spawn", VehicleKinds::Label(model)),
                    4.0f);
                SKSE::log::info("player sync: dev pick {}", VehicleKinds::Label(model));
            }

            static void Toggle()
            {
                if (state.owned) {
                    Release("F10");
                } else if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    TakeOver(player);
                }
            }
        };

        // Skyrim positioning the player: a load door (a city's gate too), fast travel, coc, a script's
        // MoveTo. Drive re-anchors CJ once the player has a cell there.
        class PositionSink : public RE::BSTEventSink<RE::PositionPlayerEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::PositionPlayerEvent* a_event, RE::BSTEventSource<RE::PositionPlayerEvent>*) override
            {
                if (state.owned && !state.placing && a_event && a_event->type.any(RE::PositionPlayerEvent::EVENT_TYPE::kFinish)) {
                    state.positioned = true;
                    SKSE::log::info("player sync: Skyrim positioned the player");
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // A menu opening pauses the game in the same frame; tell San Andreas right away.
        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                bool pauses = false;
                if (state.owned && a_event) {
                    if (a_event->opening) {
                        Looting::OnMenuOpening(a_event->menuName.c_str());  // no inventory, barter or container menu as CJ
                        auto* ui = RE::UI::GetSingleton();
                        auto  menu = ui ? ui->GetMenu(a_event->menuName) : nullptr;
                        pauses = menu && menu->PausesGame();
                    }
                    Publish(pauses || MenuHasInput(), pauses || GamePaused());
                }
                if (pauses || GamePaused()) {
                    Audio::SetPaused(true);
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
        BlockHook<kMovementHook>::Install(RE::VTABLE_MovementHandler[0]);
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
        BlockHook<kActivateHook>::Install(RE::VTABLE_ActivateHandler[0]);
        BlockHook<13>::Install(RE::VTABLE_FirstPersonState[1]);  // its zoom and POV keys
        BlockHook<14>::Install(RE::VTABLE_ThirdPersonState[1]);
        Conversation::Install();
        Police::Install();
        SKSE::log::info("player sync: PlayerCharacter::Update and Skyrim's player input handlers hooked, F10 hands the player to San Andreas");
    }

    void OnDataLoaded()
    {
        static InputSink inputSink;
        static MenuSink  menuSink;
        const auto layout = Ini::Value("Input", "KeyLayout");
        state.input.SetLayout(Input::ParseKeyLayout(layout));
        SKSE::log::info("player sync: {} key layout ([Input] KeyLayout={} in {})", Input::NameOf(state.input.Layout()), layout.empty() ? "unset" : layout,
            Ini::kPath);
        const auto devKeys = Ini::Value("Debug", "VehicleKeys");
        state.devKeys = devKeys.empty() || devKeys != "0";
        const auto devList = Ini::Value("Debug", "Vehicles");
        state.devPicker.SetModels(VehicleKinds::ParseModels(devList));
        state.input.SetDevModel(state.devKeys ? state.devPicker.Current() : 0);
        SKSE::log::info("player sync: the dev vehicle keys (; picks, ' spawns) are {} ([Debug] VehicleKeys={}), {} picks{} ([Debug] Vehicles={}), the first {}",
            state.devKeys ? "on" : "off", devKeys.empty() ? "unset" : devKeys, state.devPicker.Count(), devList.empty() ? " built in" : "",
            devList.empty() ? "unset" : devList, VehicleKinds::Label(state.devPicker.Current()));
        Arms::OnDataLoaded();
        CheatCodes::OnDataLoaded();
        Shots::OnDataLoaded();
        People::OnDataLoaded();
        Projectiles::OnDataLoaded();
        GunRewards::OnDataLoaded();
        Respawns::OnDataLoaded();
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
            input->AddEventSink(&inputSink);
        }
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuSink);
        }
        static PositionSink positionSink;
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            player->AsPositionPlayerEventSource()->AddEventSink(&positionSink);
        }
    }

    void OnGameLoad()
    {
        state.wasThirdPerson = false;  // the loaded save has its own camera
        state.resumePending = false;
        GunRewards::OnGameLoad();
        Respawns::OnGameLoad();
        Release("game loading");
        CheatCodes::OnGameLoad();
        Police::OnGameLoad();
    }

    void Snapshot(Cosave::Record& a_out)
    {
        a_out = {};
        a_out.handover = state.owned;
        a_out.armor = Vitals::Armor();
        Arms::Snapshot(a_out.weapons);
        Protocol::PlayerState pose{};
        if (state.owned && LinkService::Get().ReadPlayer(pose) && (pose.flags & Protocol::kPlayerValid) != 0 && pose.handover == state.handover &&
            (pose.flags & Protocol::kPlayerInVehicle) != 0 && pose.vehicleModel && Pose::Finite(pose)) {
            a_out.car.model = pose.vehicleModel;
            std::copy(std::begin(pose.vehicleColours), std::end(pose.vehicleColours), a_out.car.colours);
            std::copy(std::begin(pose.vehicleOffset), std::end(pose.vehicleOffset), a_out.car.offset);
            a_out.car.heading = pose.vehicleHeading;
        }
    }

    void Restore(const Cosave::Record& a_record)
    {
        state.resume = a_record;
        state.resumePending = a_record.handover;
        Vitals::SetArmor(a_record.armor);
        Arms::Restore(a_record.weapons);
    }

    void Revert()
    {
        state.resume = {};
        state.resumePending = false;
        Vitals::SetArmor(0.0f);
        Arms::Revert();
    }
}
