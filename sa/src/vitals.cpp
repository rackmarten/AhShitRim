#include "vitals.h"

#include "health.h"
#include "log.h"
#include "vehicles.h"

#include <plugin.h>

#include <CAnimBlendAssociation.h>
#include <CAnimManager.h>
#include <CCamera.h>
#include <CGameLogic.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CTaskSimpleRunAnim.h>
#include <CTimer.h>
#include <CWorld.h>
#include <common.h>

#include <safetyhook.hpp>

#include <format>
#include <utility>

namespace AhShitRim::Vitals
{
    namespace
    {
        // gta-reversed's names. San Andreas's own CTaskSimpleDie would kill him and start its own
        // Wasted and hospital (CGameLogic), so the fall is a plain animation held on its last frame.
        constexpr int   kGroupDefault = 0;     // ANIM_GROUP_DEFAULT
        constexpr int   kKoSkidBack = 26;      // ANIM_ID_KO_SKID_BACK: pitched onto his front, as San Andreas's deaths mostly end
        constexpr int   kMoveIdle = 3;         // ANIM_ID_IDLE, within his own move group
        constexpr int   kTaskPrimary = 3;      // TASK_PRIMARY_PRIMARY, over CJ's own on-foot task
        constexpr short kModeDeadBaby = 0x1D;  // MODE_PED_DEAD_BABY: the camera rising over the dead player, swinging side to side
        constexpr int   kScriptCamera = 1;     // TakeControl's who: the script, as Restore gives back
        constexpr char  kLogicPlaying = 0;     // GAMELOGIC_STATE_PLAYING
        constexpr std::uintptr_t kGameLogicUpdate = 0x442AD0;  // CGameLogic::Update
        constexpr unsigned kSeatWaitMs = 3000;  // dead in his seat with no Wasted coming this long (game time): revived

        SafetyHookInline g_gameLogicUpdate;

        struct State
        {
            bool  counting = false;
            float total = 0.0f;
            bool  down = false;
            bool  fallen = false;  // the fall's task is ours to take off
            bool  camera = false;  // the death camera is ours to give back
            bool     seated = false;   // San Andreas killed him in his seat, and he stays there dead for Wasted
            unsigned seatedMs = 0;
        } state;

        bool Dead(CPlayerPed* a_ped) { return !a_ped->IsAlive() || a_ped->m_fHealth <= 0.0f; }

        // Out of his vehicle, alive and standing, as San Andreas's own resurrection leaves him; what the
        // death took off still counts for Skyrim.
        void Revive(CPlayerPed* a_ped)
        {
            const float lost = Health::Lost(Health::kSaHealth, a_ped->m_fHealth);
            if (state.counting && lost > 0.0f) {
                state.total += lost;
            }
            Vehicles::GetOut(a_ped);  // also drops his dying task
            a_ped->m_fHealth = Health::kSaHealth;
            a_ped->m_fArmour = 0.0f;
            a_ped->m_nDeathTime = 0;
            a_ped->SetInitialState(false);
            a_ped->RestartNonPartialAnims();
            state.seated = false;
            Log(std::format("health: CJ revived in San Andreas ({:.1f} of a bar taken off in all); Skyrim's health is what counts", state.total / Health::kSaFullHealth));
        }

        // San Andreas killed CJ outright: a vehicle blowing up with him in it (CVehicle::KillPedsInVehicle
        // hands him a CEventVehicleDied, and his task dies in the seat), which no health buffer stops.
        // Left to it, CGameLogic::Update ran San Andreas's own Wasted: three seconds later it put him at
        // its nearest hospital, which with no scripts run is San Andreas's (0, 0, 0), under Skyrim's ground
        // and its sea, his position broke there, and its controls stayed off. So CGameLogic never sees him
        // dead. In a vehicle he stays dead in his seat, as San Andreas plays it, while Skyrim's Wasted runs
        // (the death took a whole bar and more off), and is revived when it is over; with no Wasted coming
        // (the damage didn't count) he is revived after kSeatWaitMs. Anywhere else he is revived at once.
        void __cdecl GameLogicUpdateHook()
        {
            auto* ped = FindPlayerPed(0);
            if (!ped) {
                g_gameLogicUpdate.call();
                return;
            }
            const bool dead = Dead(ped);
            if (dead && ped->bInVehicle && ped->m_pVehicle) {
                if (!state.seated) {
                    state.seated = true;
                    state.seatedMs = CTimer::m_snTimeInMilliseconds;
                    Log("health: San Andreas killed CJ in his seat; he stays there dead for Wasted");
                }
            } else {
                state.seated = false;
            }
            if (state.seated && !state.down && CTimer::m_snTimeInMilliseconds - state.seatedMs > kSeatWaitMs) {
                Revive(ped);  // no Wasted came
            } else if (dead && !state.seated) {
                Revive(ped);
            }

            auto& player = CWorld::Players[0];
            if (player.m_nPlayerState != PLAYERSTATE_PLAYING || CGameLogic::GameState != kLogicPlaying) {
                Log(std::format("health: San Andreas's own Wasted had started (player state {}, game logic {}), called off",
                    static_cast<int>(player.m_nPlayerState), static_cast<int>(CGameLogic::GameState)));
                player.m_nPlayerState = PLAYERSTATE_PLAYING;
                CGameLogic::GameState = kLogicPlaying;
            }
            // Dead in his seat, he looks like any driver to CGameLogic.
            const auto pedState = ped->m_ePedState;
            if (state.seated) {
                ped->m_ePedState = PEDSTATE_DRIVING;
            }
            g_gameLogicUpdate.call();
            ped->m_ePedState = pedState;
        }
    }

    void Install()
    {
        g_gameLogicUpdate = safetyhook::create_inline(reinterpret_cast<void*>(kGameLogicUpdate), reinterpret_cast<void*>(&GameLogicUpdateHook));
        Log(g_gameLogicUpdate ? "health: CGameLogic::Update hooked, San Andreas never runs its own Wasted" :
                                "health: could not hook CGameLogic::Update; CJ killed in San Andreas would run its own Wasted");
    }

    void Begin(CPlayerPed* a_ped, bool a_counting)
    {
        if (!a_ped) {
            return;
        }
        state.counting = a_counting;
        a_ped->m_fMaxHealth = Health::kSaHealth;
        a_ped->m_fHealth = Health::kSaHealth;
        a_ped->m_fArmour = 0.0f;
    }

    void End(CPlayerPed* a_ped)
    {
        if (!a_ped) {
            return;
        }
        const float lost = Health::Lost(Health::kSaHealth, a_ped->m_fHealth);
        if (state.counting && lost > 0.0f) {
            state.total += lost;
            Log(std::format("health: San Andreas took {:.1f} off CJ ({:.1f} of a bar in all)", lost, state.total / Health::kSaFullHealth));
        }
    }

    void SetWasted(CPlayerPed* a_ped, bool a_down, bool a_camera)
    {
        if (!a_ped) {
            return;
        }
        if (a_down != state.down) {
            state.down = a_down;
            auto& tasks = a_ped->m_pIntelligence->m_TaskMgr;
            if (a_down) {
                const bool inVehicle = a_ped->bInVehicle && a_ped->m_pVehicle;
                if (!inVehicle) {  // in a vehicle he stays in the seat
                    tasks.SetTask(new CTaskSimpleRunAnim(kGroupDefault, kKoSkidBack, 4.0f, true), kTaskPrimary, false);
                    state.fallen = true;
                }
                Log(inVehicle ? "health: Wasted, CJ dies in his seat" : "health: Wasted, CJ falls dead on his front");
            } else {
                if (state.seated) {
                    Revive(a_ped);  // out of the wreck under the fade, before the handover puts him at the temple
                }
                if (std::exchange(state.fallen, false)) {
                    // He stands up, out of sight under the fade. A task holding its last frame leaves its
                    // animation on him when it ends (CTaskSimpleAnim's destructor only marks it for removal),
                    // so the fall is taken off and his idle snapped back, as CTaskSimpleDie's abort does. (Not
                    // CPed::SetMoveAnim: plugin-sdk calls it through vtable slot 24, which is CPed::Save.)
                    tasks.SetTask(nullptr, kTaskPrimary, false);
                    if (auto* fall = RpAnimBlendClumpGetAssociation(a_ped->m_pRwClump, static_cast<unsigned int>(kKoSkidBack))) {
                        fall->m_fBlendDelta = -1000.0f;
                    }
                    a_ped->SetMoveState(PEDMOVE_STILL);
                    CAnimManager::BlendAnimation(a_ped->m_pRwClump, a_ped->m_nAnimGroup, kMoveIdle, 1000.0f);
                }
                Log("health: Wasted is over, CJ is up again");
            }
        }
        if (a_camera != state.camera) {
            state.camera = a_camera;
            if (a_camera) {
                TheCamera.TakeControl(a_ped, kModeDeadBaby, SWITCHTYPE_JUMPCUT, kScriptCamera);  // a cut, as San Andreas does
                Log("health: San Andreas's death camera rises over CJ");
            } else {
                TheCamera.Restore();
                TheCamera.SetCameraDirectlyBehindForFollowPed_CamOnAString();
            }
        }
    }

    float Damage() { return state.total; }
}
