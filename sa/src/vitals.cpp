#include "vitals.h"

#include "health.h"
#include "log.h"

#include <plugin.h>

#include <CAnimBlendAssociation.h>
#include <CAnimManager.h>
#include <CCamera.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CTaskSimpleRunAnim.h>

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

        struct State
        {
            bool  counting = false;
            float total = 0.0f;
            bool  down = false;
            bool  fallen = false;  // the fall's task is ours to take off
            bool  camera = false;  // the death camera is ours to give back
            bool  deadLogged = false;
        } state;
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
        if (!a_ped->IsAlive() && !std::exchange(state.deadLogged, true)) {
            Log("health: CJ died in San Andreas anyway (his health was a full bar and a buffer); Skyrim's health is what counts");
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
