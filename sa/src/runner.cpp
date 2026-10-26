#include "runner.h"

#include "audio.h"
#include "car_hits.h"
#include "chunk_world.h"
#include "cj_body.h"
#include "cj_busted.h"
#include "cj_cheats.h"
#include "cj_gear.h"
#include "cj_pickups.h"
#include "cj_melee.h"
#include "cj_projectiles.h"
#include "cj_shots.h"
#include "cj_weapons.h"
#include "ground.h"
#include "input.h"
#include "log.h"
#include "model_sender.h"
#include "pose.h"
#include "scenes.h"
#include "vitals.h"
#include "vehicle_kinds.h"
#include "vehicles.h"
#include "window.h"

#include <plugin.h>

#include <CAudioEngine.h>
#include <CCamera.h>
#include <CCarCtrl.h>
#include <CClothes.h>
#include <CMenuManager.h>
#include <CPad.h>
#include <CPedIntelligence.h>
#include <CPedClothesDesc.h>
#include <CPlayerData.h>
#include <CPlayerPed.h>
#include <CPools.h>
#include <CPopulation.h>
#include <CStreaming.h>
#include <CTaskSimplePlayerOnFoot.h>
#include <CTimer.h>
#include <CStats.h>
#include <CVehicle.h>
#include <CWanted.h>
#include <CWorld.h>
#include <common.h>

#include <safetyhook.hpp>

#include <Windows.h>
#include <timeapi.h>

#include <algorithm>
#include <cmath>
#include <format>

#pragma comment(lib, "winmm.lib")

namespace AhShitRim::Runner
{
    namespace
    {
        // ---- San Andreas 1.0 US addresses (gta-reversed names) ----

        int32_t& gGameState = *reinterpret_cast<int32_t*>(0xC8D4C0);
        constexpr int32_t kGameStateFrontendLoading = 5;  // GAME_STATE_FRONTEND_LOADING: past the splashes and movies

        // Call sites, redirected rather than hooking the functions' first bytes: SilentPatch patches
        // calls inside Idle's first bytes after this ASI has loaded, which broke an inline hook there.
        constexpr std::uintptr_t kCallIdle = 0x53ECBD;          // AppEventHandler: Idle(void* render), one in-game frame
        constexpr std::uintptr_t kCallFrontendIdle = 0x53ECCB;  // AppEventHandler: FrontendIdle(), one main menu frame
        constexpr std::uintptr_t kCallUpdatePads = 0x53BEE6;    // CGame::Process: CPad::UpdatePads
        constexpr std::uintptr_t kScriptsProcess = 0x46A000;  // CTheScripts::Process
        constexpr std::uintptr_t kStartLoadingTune = 0x507410;  // CAudioEngine::StartLoadingTune (thiscall)

        // Hooked to do nothing: no map streaming, no population.
        constexpr std::uintptr_t kNoOps[] = {
            0x410860,  // CColStore::LoadCollision
            0x410AD0,  // CColStore::EnsureCollisionIsInMemory
            0x410C00,  // CColStore::RequestCollision
            0x405170,  // CIplStore::LoadIpls
            0x4053F0,  // CIplStore::EnsureIplsAreInMemory
            0x4045B0,  // CIplStore::AddIplsNeededAtPosn
            0x616650,  // CPopulation::Update
            0x4341C0,  // CCarCtrl::GenerateRandomCars
            0x6F3F40,  // CTheCarGenerators::Process
            0x6CD2F0,  // CPlane::DoPlaneGenerationAndRemoval
            0x6F7900,  // CTrain::DoTrainGenerationAndRemoval
        };
        constexpr std::uintptr_t kHasCollisionLoaded = 0x410CE0;  // CColStore::HasCollisionLoaded: always yes

        // CJ is put this far above the handover spot, so he never starts inside the ground.
        constexpr float kSpawnLift = 0.05f;

        // Physics breaks above 30 frames a second.
        constexpr double kFrameSeconds = 1.0 / 30.0;

        void(__cdecl* g_idle)(void*) = nullptr;  // whatever the call sites called before
        void(__cdecl* g_frontendIdle)() = nullptr;
        void(__cdecl* g_updatePads)() = nullptr;
        SafetyHookInline g_scriptsProcess;
        SafetyHookInline g_startLoadingTune;
        SafetyHookInline g_noOps[std::size(kNoOps)];
        SafetyHookInline g_hasCollisionLoaded;

        struct State
        {
            bool          inGame = false;
            bool          menuSkipped = false;
            CVector       spawn;  // CJ's position at the last (re)spawn
            std::uint32_t handover = 0;
            bool          owned = false;
            bool          paused = false;
            bool          haveMouse = false;
            double        mouseX = 0.0;
            double        mouseY = 0.0;
            bool          haveSpawn = false;  // spawnCount was read once: a change after that is a request
            std::uint16_t spawnCount = 0;
            bool          carPending = false;  // a loaded save's car waits for the ground
            Vehicles::Restoration car;
            int           carReadyFrames = 0;
            bool          wasted = false;
            Protocol::InputState lastInput{};  // the last one read, for a frame whose read failed
            std::uint32_t frame = 0;

            LARGE_INTEGER frequency{};
            LARGE_INTEGER lastFrame{};

            // Stats.
            bool          settledLogged = false;
            std::uint64_t statStartMs = 0;
            int   frames = 0;
            int   inputs = 0;
        } state;

        void __cdecl NoOp() {}
        bool __cdecl Yes() { return true; }

        void Mute()
        {
            AudioEngine.SetEffectsMasterVolume(0);
            AudioEngine.SetMusicMasterVolume(0);
        }

        // The loading tune is muted rather than skipped: loading ends by stopping it.
        void __fastcall StartLoadingTuneHook(CAudioEngine* a_audio)
        {
            Mute();
            g_startLoadingTune.thiscall(a_audio);
        }

        // ---- Straight into a game ----

        void __cdecl FrontendIdleHook()
        {
            // The main menu never opens: with it inactive, the game loop goes on to load a new game.
            FrontEndMenuManager.m_bStartUpFrontEndRequested = false;  // gta-reversed: m_bActivateMenuNextFrame
            FrontEndMenuManager.m_bMenuActive = false;
            if (!state.menuSkipped) {
                state.menuSkipped = true;
                Mute();
                Log("runner: main menu skipped, starting a new game");
            }
        }

        // ---- Empty world, CJ ----

        template <class TPool>
        int RemoveAll(TPool* a_pool)
        {
            int removed = 0;
            for (int i = 0; a_pool && i < a_pool->m_nSize; ++i) {
                if (auto* entity = a_pool->GetAt(i)) {
                    CWorld::Remove(entity);
                    entity->bUsesCollision = false;
                    entity->bIsVisible = false;
                    ++removed;
                }
            }
            return removed;
        }

        void EmptyWorld()
        {
            const int buildings = RemoveAll(CPools::ms_pBuildingPool);
            const int dummies = RemoveAll(CPools::ms_pDummyPool);
            const int objects = RemoveAll(CPools::ms_pObjectPool);
            CPopulation::PedDensityMultiplier = 0.0f;
            CCarCtrl::CarDensityMultiplier = 0.0f;
            CWanted::SetMaximumWantedLevel(0);
            Log(std::format("runner: world emptied ({} buildings, {} dummies, {} objects taken out)", buildings, dummies, objects));
        }

        void PlaceCj(CPlayerPed* a_ped, float a_skyrimHeading)
        {
            const float rotation = Pose::SanAndreasHeading(a_skyrimHeading);
            CVector     at(0.0f, 0.0f, Ground::kHeight + a_ped->GetDistanceFromCentreOfMassToBaseOfModel() + kSpawnLift);
            ChunkWorld::Place(a_ped, at, rotation);  // a load or a handover starts clean
            TheCamera.SetCameraDirectlyBehindForFollowPed_CamOnAString();
            state.spawn = at;
        }

        // What the script command CREATE_PLAYER does (gta-reversed's PlayerCommands.cpp).
        CPlayerPed* CreateCj()
        {
            if (CStreaming::ms_aInfoForModel[0].m_nLoadState != 1) {  // LOADSTATE_LOADED
                CStreaming::RequestSpecialModel(0, "player", 0x2 | 0x8 | 0x10);  // game required, keep in memory, priority
                CStreaming::LoadAllRequestedModels(true);
            }
            CPlayerPed::SetupPlayerPed(0);
            auto* ped = FindPlayerPed(0);
            if (!ped) {
                return nullptr;
            }
            ped->SetCharCreatedBy(2);  // PED_MISSION
            CPlayerPed::DeactivatePlayerPed(0);
            PlaceCj(ped, 0.0f);
            CPlayerPed::ReactivatePlayerPed(0);
            ped->m_pIntelligence->m_TaskMgr.SetTask(new CTaskSimplePlayerOnFoot(), 4, false);  // TASK_PRIMARY_DEFAULT
            // The clothes the intro script gives him (GIVE_PLAYER_CLOTHING_OUTSIDE_SHOP, then
            // BUILD_PLAYER_MODEL); without them he is built in his underwear.
            if (ped->m_pPlayerData && ped->m_pPlayerData->m_pPedClothesDesc) {
                auto* clothes = ped->m_pPlayerData->m_pPedClothesDesc;
                clothes->SetTextureAndModel("vest", "vest", 0);                // CLOTHES_TEXTURE_TORSO
                clothes->SetTextureAndModel("jeansdenim", "jeans", 2);         // CLOTHES_TEXTURE_LEGS
                clothes->SetTextureAndModel("sneakerbincblk", "sneaker", 3);   // CLOTHES_TEXTURE_SHOES
            }
            CClothes::RebuildPlayer(ped, false);
            return ped;
        }

        // The mission script never runs. CGame::Initialise runs it once to create the player (and
        // then processes the camera, which needs him), so CJ is created here instead.
        void __cdecl ScriptsProcessHook()
        {
            if (!FindPlayerPed(0)) {
                Log(CreateCj() ? "runner: CJ created in place of the mission script" : "runner: could not create CJ");
            }
        }

        void OnGameStarted()
        {
            EmptyWorld();
            auto* ped = FindPlayerPed(0);
            if (ped) {
                PlaceCj(ped, 0.0f);
            }
            TheCamera.Restore();
            TheCamera.SetCameraDirectlyBehindForFollowPed_CamOnAString();
            TheCamera.SetFadeColour(0, 0, 0);
            TheCamera.Fade(0.0f, 1);  // fade in
            FrontEndMenuManager.m_bStartUpFrontEndRequested = false;
            FrontEndMenuManager.m_bMenuActive = false;
            FrontEndMenuManager.m_bPrefsVsync = false;  // gta-reversed: m_bPrefsFrameLimiter; the Idle hook caps the rate
            Mute();
            // A fresh game's CJ is out of breath after a few seconds of sprinting: full stamina
            // gives him San Andreas's longest sprint (he still tires, unlike the cheat).
            CStats::SetStatValue(STAT_STAMINA, 1000.0f);
            state.inGame = true;
            Log(ped ? "runner: CJ waits for Skyrim's ground (stamina at its maximum)" : "runner: could not create CJ");
        }

        // ---- Frame rate ----

        void WaitForFrame()
        {
            if (!state.frequency.QuadPart) {
                ::QueryPerformanceFrequency(&state.frequency);
                ::QueryPerformanceCounter(&state.lastFrame);
                return;
            }
            const LONGLONG frame = static_cast<LONGLONG>(kFrameSeconds * state.frequency.QuadPart);
            LARGE_INTEGER  now;
            ::QueryPerformanceCounter(&now);
            const LONGLONG due = state.lastFrame.QuadPart + frame;
            if (now.QuadPart < due) {
                const DWORD ms = static_cast<DWORD>((due - now.QuadPart) * 1000 / state.frequency.QuadPart);
                if (ms > 1) {
                    ::Sleep(ms - 1);
                }
                do {
                    ::QueryPerformanceCounter(&now);
                } while (now.QuadPart < due);
            }
            // Fell behind by more than a frame (a load, a hitch): start counting again from now.
            state.lastFrame.QuadPart = now.QuadPart - due > frame ? now.QuadPart : due;
        }

        // ---- Input and pose ----

        // CJ getting into or out of a vehicle (v35): San Andreas's cops never bust him in the middle of it, nor does Skyrim.
        bool GettingInOrOut(CPlayerPed* a_ped)
        {
            if (!a_ped->m_pIntelligence) {
                return false;
            }
            auto& tasks = a_ped->m_pIntelligence->m_TaskMgr;
            return tasks.FindActiveTaskByType(TASK_COMPLEX_ENTER_CAR_AS_DRIVER) || tasks.FindActiveTaskByType(TASK_COMPLEX_ENTER_CAR_AS_PASSENGER) ||
                   tasks.FindActiveTaskByType(TASK_COMPLEX_LEAVE_CAR);
        }

        void PublishPose(CPlayerPed* a_ped)
        {
            Pose::SanAndreasPose sa;
            const auto&          pos = a_ped->GetPosition();
            sa.position = { pos.x, pos.y, pos.z };
            sa.spawn = { state.spawn.x, state.spawn.y, state.spawn.z };
            // m_vecMoveSpeed is per 1/50 s step. In a vehicle CJ's own is zero; Skyrim samples the
            // ground ahead by the vehicle's.
            const CVehicle* vehicle = Vehicles::Driven(a_ped);
            const CVector&  speed = vehicle ? vehicle->m_vecMoveSpeed : a_ped->m_vecMoveSpeed;
            sa.velocity = { speed.x * 50.0f, speed.y * 50.0f, speed.z * 50.0f };
            const auto& forward = a_ped->GetForward();
            sa.forward = { forward.x, forward.y, forward.z };
            const auto& front = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront;
            sa.view = { front.x, front.y, front.z };
            sa.grounded = a_ped->bIsStanding || vehicle;
            sa.crouching = a_ped->bIsDucking && !vehicle;
            sa.onFire = a_ped->m_pFire != nullptr;
            sa.damage = Vitals::Damage();
            if (vehicle) {
                // Saved with the game: Skyrim puts it back under CJ on load.
                sa.vehicleModel = static_cast<std::uint16_t>(vehicle->m_nModelIndex);
                sa.vehicleColours[0] = vehicle->m_nPrimaryColor;
                sa.vehicleColours[1] = vehicle->m_nSecondaryColor;
                sa.vehicleColours[2] = vehicle->m_nTertiaryColor;
                sa.vehicleColours[3] = vehicle->m_nQuaternaryColor;
                const auto& at = vehicle->GetPosition();
                sa.vehicleOffset = { at.x - pos.x, at.y - pos.y, at.z - pos.z };
                const auto& ahead = vehicle->GetForward();
                sa.vehicleForward = { ahead.x, ahead.y, ahead.z };
            }
            CjWeapons::Fill(a_ped, sa);
            auto pose = Pose::ToPlayerState(sa, ++state.frame, state.handover);
            if (vehicle) {
                pose.flags |= Protocol::kPlayerInVehicle;
            }
            if (GettingInOrOut(a_ped)) {
                pose.flags |= Protocol::kPlayerGettingInOut;
            }
            Audio::Fill(pose, Vehicles::Driven(a_ped));
            GetLink().WritePlayer(pose);
        }

        void ApplyInput(const Protocol::InputState& a_input, CPlayerPed* a_ped)
        {
            const bool owned = (a_input.flags & Protocol::kInputOwnsPlayer) != 0;
            const bool paused = (a_input.flags & Protocol::kInputPaused) != 0;
            if (owned != state.owned) {
                state.owned = owned;
                state.haveMouse = false;
                state.statStartMs = 0;
                state.frames = state.inputs = 0;
                Log(owned ? "runner: Skyrim hands the player to CJ" : "runner: Skyrim has the player back");
            }
            if (paused != state.paused) {
                state.paused = paused;
                CTimer::m_UserPause = paused;
                Log(paused ? "runner: a Skyrim menu pauses the game" : "runner: unpaused");
            }
            state.wasted = owned && (a_input.flags & Protocol::kInputWasted) != 0;
            Vitals::SetWasted(a_ped, state.wasted, state.wasted && (a_input.flags & Protocol::kInputWastedCamera) != 0);
            CjBusted::Set(a_ped, owned && (a_input.flags & Protocol::kInputBusted) != 0);  // v22
            if (a_input.handover != state.handover) {
                state.handover = a_input.handover;
                state.carPending = false;
                CjGear::Reset(a_ped);
                Vehicles::Clear(a_ped);
                ChunkWorld::Begin(a_input.handover, a_input.anchor);
                PlaceCj(a_ped, a_input.heading);
                Log(std::format("runner: handover {} (new, or re-anchored by a load door or fast travel), CJ at the anchor facing {:.2f} rad (Skyrim)", a_input.handover, a_input.heading));
                // A loaded save's car comes with its handover: put back under CJ once the ground is in.
                if (owned && a_input.carModel) {
                    state.car = {};
                    state.car.model = a_input.carModel;
                    std::copy(std::begin(a_input.carColours), std::end(a_input.carColours), state.car.colours);
                    for (int i = 0; i < 3; i++) {
                        state.car.offset[i] = a_input.carOffset[i] * Pose::kMetersPerUnit;
                    }
                    state.car.heading = Pose::SanAndreasHeading(a_input.carHeading);
                    state.carPending = true;
                    state.carReadyFrames = 0;
                    Log(std::format("runner: the save's car (model {}) waits for the ground", a_input.carModel));
                }
            }
            // A spawn key pressed in Skyrim. The first value read only seeds the count, so a San
            // Andreas that started after Skyrim doesn't spawn the last vehicle asked for again.
            if (state.haveSpawn && a_input.spawnCount != state.spawnCount && owned && a_input.spawnModel) {
                if (VehicleKinds::IsGear(a_input.spawnModel)) {
                    CjGear::Give(a_ped, a_input.spawnModel);  // v19: the jetpack or the parachute
                } else {
                    Vehicles::Spawn(a_ped, a_input.spawnModel, (a_input.spawnFlags & Protocol::kSpawnAirborne) != 0);
                }
            }
            state.spawnCount = a_input.spawnCount;
            state.haveSpawn = true;
            Audio::OnInput(a_input, Audio::HasRadio(Vehicles::Driven(a_ped)));
            const bool following = Scenes::OnInput(a_ped, a_input, owned);
            // The wheel switches weapons on foot and tunes the radio in a vehicle; either way its steps are followed.
            const auto presses = CjWeapons::Wheel(a_input, owned && !following && Vehicles::Driven(a_ped) == nullptr);
            if (!owned) {
                return;
            }

            const auto pad = Input::ToPad(a_input, Vehicles::Driven(a_ped) != nullptr);
            auto&      s = CPad::GetPad(0)->NewState;
            s = {};
            if (following) {
                // Skyrim moves the player: CJ follows it, the keys do nothing; the mouse still looks round.
            } else {
                s.LeftStickX = pad.leftStickX;
                s.LeftStickY = pad.leftStickY;
                s.ButtonCross = pad.buttonCross;
                s.ButtonSquare = pad.buttonSquare;
                s.ButtonTriangle = pad.buttonTriangle;
                s.RightShoulder1 = pad.rightShoulder1;
                s.ShockButtonL = pad.shockButtonL;
                s.m_bPedWalk = pad.pedWalk;
                s.ButtonCircle = pad.buttonCircle;  // fire (a melee weapon attacks); the right shoulder aims; in a vehicle (v19) its guns
                s.LeftShoulder1 = pad.leftShoulder1;  // v19, in a vehicle: its second weapon
                s.RightShoulder2 = presses.next || pad.rightShoulder2 ? 255 : 0;  // the wheel: next weapon; in a vehicle (v19) look right
                s.LeftShoulder2 = presses.prev || pad.leftShoulder2 ? 255 : 0;    // the previous one; look left
                s.RightStickX = pad.rightStickX;  // v19, in a vehicle: the turret keys
                s.RightStickY = pad.rightStickY;
                s.ShockButtonR = pad.shockButtonR;  // v20, in a vehicle: the hydraulics' jump; a plane's landing gear
                s.Select = pad.select;              // v23, in a vehicle: the next camera distance
            }

            // Skyrim sends totals; San Andreas wants this frame's movement.
            double dx = 0.0, dy = 0.0;
            if (state.haveMouse) {
                dx = std::clamp(a_input.mouseX - state.mouseX, -1000.0, 1000.0);
                dy = std::clamp(a_input.mouseY - state.mouseY, -1000.0, 1000.0);
            }
            state.mouseX = a_input.mouseX;
            state.mouseY = a_input.mouseY;
            state.haveMouse = true;
            auto& mouse = CPad::NewMouseControllerState;
            mouse = {};
            // Vertical movement is flipped: passed through as is, CJ's camera looked down when the
            // mouse moved up (tested in game). San Andreas's own invert settings don't apply here.
            mouse.x = static_cast<float>(dx);
            mouse.y = static_cast<float>(-dy);
            state.inputs++;
        }

        void __cdecl UpdatePadsHook()
        {
            g_updatePads();
            auto* ped = state.inGame ? FindPlayerPed(0) : nullptr;
            Protocol::InputState input{};
            if (ped && GetLink().ReadInput(input)) {
                state.lastInput = input;
                ApplyInput(input, ped);
            } else if (ped && GetLink().IsUp() && state.haveSpawn) {
                // The slot was being written and stayed torn: the input stands as it was. (Taken as
                // the link going down, it handed the player back for a frame, and the spawn count
                // starting over spawned the last vehicle again.)
                ApplyInput(state.lastInput, ped);
            } else if (state.owned || state.paused) {
                input = {};  // link down: Skyrim has the player back, nothing is paused
                input.handover = state.handover;
                input.spawnCount = state.spawnCount;
                input.radioNext = state.lastInput.radioNext;  // the wheel's counts stand, so a link coming back isn't a flood of steps
                input.radioPrev = state.lastInput.radioPrev;
                if (ped) {
                    ApplyInput(input, ped);
                }
            }
        }

        void __cdecl IdleHook(void* a_render)
        {
            WaitForFrame();
            auto* player = state.inGame ? FindPlayerPed(0) : nullptr;
            // Skyrim's health is the only one: whatever this frame does to CJ is reported, not suffered.
            Vitals::Begin(player, state.owned && !state.paused && !state.wasted && ChunkWorld::Ready() && !Scenes::Following());
            // Hidden, nothing is drawn: Idle simulates and returns before rendering.
            g_idle(Window::Visible() ? a_render : nullptr);
            Vitals::End(player);
            if (!state.inGame) {
                return;
            }

            auto* ped = FindPlayerPed(0);
            if (!ped) {
                return;
            }
            CPopulation::PedDensityMultiplier = 0.0f;
            CCarCtrl::CarDensityMultiplier = 0.0f;
            if (auto* wanted = ped->GetWanted()) {
                wanted->m_nWantedLevel = 0;
            }
            ChunkWorld::Receive();
            CjCheats::Update(ped);
            ChunkWorld::Update(ped, state.spawn, Scenes::Following());
            Scenes::Update(ped, state.spawn);
            if (state.carPending && state.owned && ChunkWorld::Ready() && ++state.carReadyFrames >= 20) {  // the chunks around him too
                state.carPending = false;
                Vehicles::Restore(ped, state.spawn, state.car);
            }
            // Skyrim's actors push back on CJ's vehicle before it is posed and published, so a car a mammoth stopped is
            // drawn where it stopped.
            CarHits::Update(ped, state.spawn, kSpawnLift, state.handover, state.owned && !state.wasted && !Scenes::Following());
            CjMelee::Update(ped, state.spawn, kSpawnLift, state.handover, state.owned && !state.wasted && !Scenes::Following());  // on foot: bumping into them, the lock-on
            Vehicles::Update(ped, state.spawn, kSpawnLift, state.handover, state.frame + 1);
            Audio::Update(state.owned, state.paused);
            PublishPose(ped);
            CjBody::Update(ped, state.spawn, kSpawnLift, state.handover, state.frame);
            CjGear::Update(ped, state.owned && !state.wasted && !Scenes::Following(), state.spawn, kSpawnLift, state.handover, state.frame);
            CjWeapons::Update(ped, state.spawn, kSpawnLift, state.handover, state.frame);
            CjProjectiles::Update(ped, state.owned);
            CjPickups::Update(ped, state.owned);
            ModelSender::Pump();

            // Stats every 5 s while Skyrim has handed the player over, and once 5 s after the start.
            const std::uint64_t now = ::GetTickCount64();
            if (!state.statStartMs) {
                state.statStartMs = now;
            }
            state.frames++;
            if ((state.owned || !state.settledLogged) && now - state.statStartMs >= 5000) {
                state.settledLogged = true;
                const auto& p = ped->GetPosition();
                const CVehicle* vehicle = Vehicles::Driven(ped);
                const CVector&  speed = vehicle ? vehicle->m_vecMoveSpeed : ped->m_vecMoveSpeed;
                Log(std::format("runner: {} frames, {} inputs in {:.1f} s; CJ at ({:.1f},{:.1f},{:.1f}) speed {:.1f} m/s{}",
                    state.frames, state.inputs, (now - state.statStartMs) / 1000.0,
                    p.x, p.y, p.z, std::hypot(speed.x, speed.y) * 50.0f, vehicle ? ", in a vehicle" : ped->bIsStanding ? ", standing" : ", in the air"));
            }
            if (now - state.statStartMs >= 5000) {
                state.statStartMs = now;
                state.frames = state.inputs = 0;
            }
        }

        // Points the call at a_site to a_detour; returns what it called before, or nullptr when the
        // site is not a call (then it is left alone).
        template <class T>
        T RedirectCall(std::uintptr_t a_site, T a_detour)
        {
            auto* code = reinterpret_cast<std::uint8_t*>(a_site);
            if (code[0] != 0xE8) {
                Log(std::format("runner: 0x{:X} is not a call, not redirected", a_site));
                return nullptr;
            }
            auto*       rel = reinterpret_cast<std::int32_t*>(code + 1);
            const auto  before = static_cast<std::uintptr_t>(a_site + 5 + *rel);
            DWORD       old = 0;
            ::VirtualProtect(rel, 4, PAGE_EXECUTE_READWRITE, &old);
            *rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(a_detour) - (a_site + 5));
            ::VirtualProtect(rel, 4, old, &old);
            return reinterpret_cast<T>(before);
        }

        template <class T>
        SafetyHookInline Hook(std::uintptr_t a_address, T a_detour)
        {
            auto hook = safetyhook::create_inline(reinterpret_cast<void*>(a_address), reinterpret_cast<void*>(a_detour));
            if (!hook) {
                Log(std::format("runner: could not hook 0x{:X}", a_address));
            }
            return hook;
        }
    }

    void Install()
    {
        ::timeBeginPeriod(1);
        g_scriptsProcess = Hook(kScriptsProcess, &ScriptsProcessHook);
        g_startLoadingTune = Hook(kStartLoadingTune, &StartLoadingTuneHook);
        Audio::Install();
        for (std::size_t i = 0; i < std::size(kNoOps); ++i) {
            g_noOps[i] = Hook(kNoOps[i], &NoOp);
        }
        g_hasCollisionLoaded = Hook(kHasCollisionLoaded, &Yes);
        CjBody::Install();

        // WinMain shows the window once RenderWare is up, right before its game loop starts: skip the
        // splashes and movies there. (plugin-sdk's initRwEvent is a void() event on a bool function
        // and lost its result, so San Andreas quit.)
        // The frame hooks go in there too, after every other plugin (SilentPatch) has patched the game.
        Window::OnShown([] {
            gGameState = kGameStateFrontendLoading;
            Log("runner: splashes and movies skipped");
            g_idle = RedirectCall(kCallIdle, &IdleHook);
            g_frontendIdle = RedirectCall(kCallFrontendIdle, &FrontendIdleHook);
            g_updatePads = RedirectCall(kCallUpdatePads, &UpdatePadsHook);
            CjShots::Install();
            CjProjectiles::Install();
            CjMelee::Install();
            Vitals::Install();
            Scenes::Install();
            if (!g_idle || !g_frontendIdle || !g_updatePads) {
                Log("runner: a frame hook is missing; San Andreas will not run as intended");
            }
        });
        plugin::Events::initGameEvent += [] { OnGameStarted(); };
        Log("runner: hooks installed (no scripts, no map streaming, no population, 30 fps)");
    }
}
