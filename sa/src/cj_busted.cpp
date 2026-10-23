#include "cj_busted.h"

#include "log.h"
#include "vehicles.h"

#include <plugin.h>

#include <CAnimBlendAssociation.h>
#include <CAnimManager.h>
#include <CAutomobile.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CTaskSimpleRunAnim.h>
#include <CVehicle.h>
#include <common.h>

#include <format>

namespace AhShitRim::CjBusted
{
    namespace
    {
        // gta-reversed's names.
        constexpr int       kGroupDefault = 0;      // ANIM_GROUP_DEFAULT
        constexpr int       kKdLeft = 22;           // ANIM_ID_KD_LEFT: knocked down to the left, ending on the ground
        constexpr int       kHandsUp = 142;         // ANIM_ID_HANDSUP
        constexpr int       kMoveIdle = 3;          // ANIM_ID_IDLE, within his own move group
        constexpr int       kTaskPrimary = 3;       // TASK_PRIMARY_PRIMARY
        constexpr unsigned  kDriverDoor = 10;       // TARGET_DOOR_DRIVER (and CAR_DOOR_LF, its component)
        constexpr uintptr_t kDraggedOutCtor = 0x648490;  // CTaskComplexCarSlowBeDraggedOut(CVehicle*, eTargetDoor, bool isDraggedBySelf)
        constexpr unsigned  kDraggedOutSize = 0x20;

        enum class Mode
        {
            None,
            Dragged,  // the drag out of the car runs
            HandsUp,
            Down,     // knocked off a bike, lying there
            Seated,   // a boat or an aircraft: he stays put
        };

        struct State
        {
            bool busted = false;
            Mode mode = Mode::None;
        } state;

        void HandsUp(CPlayerPed* a_ped)
        {
            a_ped->m_pIntelligence->m_TaskMgr.SetTask(new CTaskSimpleRunAnim(kGroupDefault, kHandsUp, 4.0f, true), kTaskPrimary, false);
            state.mode = Mode::HandsUp;
        }

        // San Andreas's own jacked task through the driver's door, the door opened for it: no jacker opens it here.
        bool DragOut(CPlayerPed* a_ped, CVehicle* a_vehicle)
        {
            a_vehicle->OpenDoor(a_ped, static_cast<int>(kDriverDoor), DOOR_FRONT_LEFT, 1.0f, true);
            auto* task = static_cast<CTask*>(CTask::operator new(kDraggedOutSize));
            if (!task) {
                return false;
            }
            reinterpret_cast<void(__thiscall*)(CTask*, CVehicle*, unsigned, bool)>(kDraggedOutCtor)(task, a_vehicle, kDriverDoor, false);
            a_ped->m_pIntelligence->m_TaskMgr.SetTask(task, kTaskPrimary, false);
            state.mode = Mode::Dragged;
            return true;
        }

        // Off the bike to its left, knocked down there.
        void KnockOff(CPlayerPed* a_ped, CVehicle* a_vehicle)
        {
            const CVector at = a_vehicle->GetPosition() - a_vehicle->GetRight() * 1.0f + CVector(0.0f, 0.0f, 0.5f);
            Vehicles::GetOut(a_ped);
            a_ped->SetPosn(at);
            a_ped->m_pIntelligence->m_TaskMgr.SetTask(new CTaskSimpleRunAnim(kGroupDefault, kKdLeft, 4.0f, true), kTaskPrimary, false);
            state.mode = Mode::Down;
        }

        void Arrest(CPlayerPed* a_ped)
        {
            CVehicle* vehicle = a_ped->bInVehicle ? a_ped->m_pVehicle : nullptr;
            if (!vehicle) {
                HandsUp(a_ped);
                Log("busted: CJ puts his hands up");
                return;
            }
            switch (vehicle->m_nVehicleSubClass) {
            case VEHICLE_AUTOMOBILE:
            case VEHICLE_MTRUCK:
                if (DragOut(a_ped, vehicle)) {
                    Log(std::format("busted: CJ is dragged out of his model {} by the driver's door", vehicle->m_nModelIndex));
                    return;
                }
                Vehicles::GetOut(a_ped);
                HandsUp(a_ped);
                Log("busted: the jacked task didn't come; CJ out of his car, hands up");
                return;
            case VEHICLE_BIKE:
            case VEHICLE_BMX:
            case VEHICLE_QUAD:
                KnockOff(a_ped, vehicle);
                Log(std::format("busted: CJ is knocked off his model {}", vehicle->m_nModelIndex));
                return;
            default:
                state.mode = Mode::Seated;
                Log(std::format("busted: CJ stays in his model {} (no door to drag him out of)", vehicle->m_nModelIndex));
                return;
            }
        }

        // The arrest's animations off him, his idle snapped back (as Vitals does after Wasted's fall).
        void StandUp(CPlayerPed* a_ped)
        {
            auto& tasks = a_ped->m_pIntelligence->m_TaskMgr;
            if (state.mode != Mode::Seated) {
                tasks.SetTask(nullptr, kTaskPrimary, false);
            }
            for (const int anim : { kHandsUp, kKdLeft }) {
                if (auto* assoc = RpAnimBlendClumpGetAssociation(a_ped->m_pRwClump, static_cast<unsigned int>(anim))) {
                    assoc->m_fBlendDelta = -1000.0f;
                }
            }
            if (!a_ped->bInVehicle) {
                a_ped->SetMoveState(PEDMOVE_STILL);
                CAnimManager::BlendAnimation(a_ped->m_pRwClump, a_ped->m_nAnimGroup, kMoveIdle, 1000.0f);
            }
            state.mode = Mode::None;
        }
    }

    void Set(CPlayerPed* a_ped, bool a_busted)
    {
        if (!a_ped) {
            return;
        }
        if (a_busted != state.busted) {
            state.busted = a_busted;
            if (a_busted) {
                Arrest(a_ped);
            } else {
                StandUp(a_ped);
                Log("busted: over, CJ stands up");
            }
            return;
        }
        // Dragged out and on his feet: his hands go up.
        if (a_busted && state.mode == Mode::Dragged && !a_ped->bInVehicle) {
            auto* task = a_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[kTaskPrimary];
            if (!task || task->GetId() != TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT) {
                HandsUp(a_ped);
                Log("busted: out of the car, CJ puts his hands up");
            }
        }
    }
}
