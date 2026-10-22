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

        SafetyHookInline g_gameLogicUpdate;

        struct State
        {
            bool  counting = false;
            float total = 0.0f;
            bool  down = false;
            bool  fallen = false;  // the fall's task is ours to take off
            bool  camera = false;  // the death camera is ours to give back
        } state;

        // San Andreas killed CJ outright: a vehicle blowing up with him in it (CVehicle::KillPedsInVehicle
        // hands him a CEventVehicleDied, and his task dies in the seat), which no health buffer stops.
        // Left dead, CGameLogic::Update ran San Andreas's own Wasted: three seconds later it put him at
        // its nearest hospital, which with no scripts run is San Andreas's (0, 0, 0), under Skyrim's ground
        // and its sea, his position broke there, and its controls stayed off. So he is revived where he
        // is, out of his vehicle, before CGameLogic looks; what the death took off still counts for Skyrim.
        void Revive(CPlayerPed* a_ped)
        {
            auto&      player = CWorld::Players[0];
            const bool dead = !a_ped->IsAlive() || a_ped->m_fHealth <= 0.0f;
            if (!dead && player.m_nPlayerState == PLAYERSTATE_PLAYING && CGameLogic::GameState == kLogicPlaying) {
                return;
            }
            if (dead) {
                const float lost = Health::Lost(Health::kSaHealth, a_ped->m_fHealth);
                if (state.counting && lost > 0.0f) {
                    state.total += lost;
                }
                Vehicles::GetOut(a_ped);  // also drops his dying task
                a_ped->m_fHealth = Health::kSaHealth;
                a_ped->m_fArmour = 0.0f;
                a_ped->m_nDeathTime = 0;
                a_ped->SetInitialState(false);  // standing, as San Andreas's own resurrection leaves him
                a_ped->RestartNonPartialAnims();
                Log(std::format("health: San Andreas killed CJ (it took {:.1f} off, {:.1f} of a bar in all); revived where he is, Skyrim's health is what counts",
                    lost, state.total / Health::kSaFullHealth));
            }
            if (player.m_nPlayerState != PLAYERSTATE_PLAYING || CGameLogic::GameState != kLogicPlaying) {
                Log(std::format("health: San Andreas's own Wasted had started (player state {}, game logic {}), called off",
                    static_cast<int>(player.m_nPlayerState), static_cast<int>(CGameLogic::GameState)));
                player.m_nPlayerState = PLAYERSTATE_PLAYING;
                CGameLogic::GameState = kLogicPlaying;
            }
        }

        void __cdecl GameLogicUpdateHook()
        {
            if (auto* ped = FindPlayerPed(0)) {
                Revive(ped);
            }
            g_gameLogicUpdate.call();
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
