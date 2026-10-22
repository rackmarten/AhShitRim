#include "scenes.h"

#include "cj_body.h"
#include "log.h"
#include "pose.h"
#include "scene.h"
#include "vehicles.h"

#include <plugin.h>

#include <CAnimBlendAssociation.h>
#include <CAnimManager.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CTaskSimpleRunAnim.h>
#include <CTimer.h>
#include <common.h>

#include <format>

namespace AhShitRim::Scenes
{
    namespace
    {
        // gta-reversed's names: ped.ifp's group, and the ids within a ped's own move group.
        constexpr int kGroupDefault = 0;      // ANIM_GROUP_DEFAULT
        constexpr int kMoveWalk = 0;          // ANIM_ID_WALK
        constexpr int kMoveRun = 1;           // ANIM_ID_RUN
        constexpr int kMoveIdle = 3;          // ANIM_ID_IDLE
        constexpr int kSeatIdle = 150;        // ANIM_DEFAULT_SEAT_IDLE
        constexpr int kShovePartial = 164;    // ANIM_DEFAULT_SHOVE_PARTIAL: a push, Fus Ro Dah
        constexpr int kFaceAnger = 157;       // ANIM_DEFAULT_FACANGER_0
        constexpr int kFuckU = 144;           // ANIM_ID_FUCKU: the finger, as CTaskSimpleShakeFist plays it
        constexpr unsigned short kInsultMale = 110;  // CTX_GLOBAL_GENERIC_INSULT_MALE; CJ's voice says CTX_PLY_GENERIC_INSULT_MALE
        constexpr int kTaskPrimary = 3;       // TASK_PRIMARY_PRIMARY, over CJ's own on-foot task
        constexpr unsigned short kAnimLooped = 0x2;           // ANIMATION_LOOPED
        constexpr unsigned short kAnimFinishAutoRemove = 0x8;  // gta-reversed: ANIMATION_IS_FINISH_AUTO_REMOVE

        constexpr float kTurnRate = 2.0f * Pose::kPi;  // rad/s turning to a speaker

        struct State
        {
            Protocol::InputState input{};
            bool                 following = false;
            bool                 usedCollision = true;
            bool                 hadGravity = true;
            Scene::Motion        motion = Scene::Motion::Idle;
            Scene::MotionTracker tracker;
            Scene::SeatMatch     seat;
            int                  seatFrames = 0;
            bool                 haveShout = false;  // shoutCount read once: a change after that is a shout
            std::uint16_t        shoutCount = 0;
            bool                 haveSwear = false;  // likewise for swearCount
            std::uint16_t        swearCount = 0;
        } state;

        float Seconds() { return CTimer::ms_fTimeStep * 0.02f; }  // San Andreas steps in 1/50 s

        void PlayMotion(CPlayerPed* a_ped, Scene::Motion a_motion)
        {
            auto& tasks = a_ped->m_pIntelligence->m_TaskMgr;
            switch (a_motion) {
            case Scene::Motion::Seated:
                tasks.SetTask(new CTaskSimpleRunAnim(kGroupDefault, kSeatIdle, 4.0f, false), kTaskPrimary, false);
                break;
            case Scene::Motion::Walk:
                tasks.SetTask(new CTaskSimpleRunAnim(a_ped->m_nAnimGroup, kMoveWalk, 4.0f, false), kTaskPrimary, false);
                break;
            case Scene::Motion::Run:
                tasks.SetTask(new CTaskSimpleRunAnim(a_ped->m_nAnimGroup, kMoveRun, 4.0f, false), kTaskPrimary, false);
                break;
            default:
                tasks.SetTask(nullptr, kTaskPrimary, false);  // his own on-foot task idles
                CAnimManager::BlendAnimation(a_ped->m_pRwClump, a_ped->m_nAnimGroup, kMoveIdle, 4.0f);
                break;
            }
        }

        // An animation over whatever CJ does, played once and then taken off.
        void PlayOnce(CPlayerPed* a_ped, int a_anim)
        {
            if (auto* assoc = CAnimManager::BlendAnimation(a_ped->m_pRwClump, kGroupDefault, a_anim, 8.0f)) {
                assoc->m_nFlags = static_cast<unsigned short>((assoc->m_nFlags & ~kAnimLooped) | kAnimFinishAutoRemove);
                assoc->Start(0.0f);
            }
        }

        void Begin(CPlayerPed* a_ped)
        {
            if (Vehicles::Driven(a_ped) || a_ped->m_pVehicle) {
                Vehicles::GetOut(a_ped);  // the vehicle stays where it is
            }
            state.following = true;
            state.usedCollision = a_ped->bUsesCollision;
            state.hadGravity = a_ped->bApplyGravity;
            a_ped->bUsesCollision = false;
            a_ped->bApplyGravity = false;
            state.tracker.Reset();
            state.motion = Scene::Motion::Idle;
            Log("scenes: Skyrim moves the player, CJ follows it");
        }
    }

    bool OnInput(CPlayerPed* a_ped, const Protocol::InputState& a_input, bool a_owned)
    {
        state.input = a_input;
        // The first value read only seeds the count, as for the spawn keys.
        if (state.haveShout && a_input.shoutCount != state.shoutCount && a_owned && !Vehicles::Driven(a_ped) && !state.following) {
            PlayOnce(a_ped, kShovePartial);
            PlayOnce(a_ped, kFaceAnger);
            Log("scenes: Skyrim's player shouted, CJ gestures");
        }
        state.shoutCount = a_input.shoutCount;
        state.haveShout = true;

        // The swear key (v16): CJ's own insult, out of the window in a vehicle, with the finger on foot.
        const bool wasted = (a_input.flags & Protocol::kInputWasted) != 0;
        if (state.haveSwear && a_input.swearCount != state.swearCount && a_owned && !state.following && !wasted) {
            a_ped->Say(kInsultMale, 0, 1.0f, true, true);
            if (!Vehicles::Driven(a_ped) && !a_ped->m_pVehicle) {
                PlayOnce(a_ped, kFuckU);
            }
            Log("scenes: CJ swears");
        }
        state.swearCount = a_input.swearCount;
        state.haveSwear = true;

        const bool follow = a_owned && (a_input.flags & Protocol::kInputScripted) != 0;
        if (follow && !state.following) {
            Begin(a_ped);
        } else if (!follow && state.following) {
            End(a_ped);
        }
        return state.following;
    }

    void Update(CPlayerPed* a_ped, const CVector& a_spawn)
    {
        const auto& input = state.input;
        if (state.following) {
            // Skyrim's feet relative to the anchor; a_spawn is CJ's position (not his feet) at it.
            const CVector at(a_spawn.x + input.sceneOffset[0] * Pose::kMetersPerUnit, a_spawn.y + input.sceneOffset[1] * Pose::kMetersPerUnit,
                a_spawn.z + input.sceneOffset[2] * Pose::kMetersPerUnit);
            const float rotation = Pose::SanAndreasHeading(input.sceneHeading);
            const bool  seated = (input.flags & Protocol::kInputSeated) != 0;
            CVector     placed = at;
            if (seated) {
                // His pelvis on the seat (sceneOffset is its surface), not his feet: the sitting
                // animation lowers him backwards and down.
                CVector    pelvis;
                const bool have = CjBody::Pelvis(pelvis);
                state.seat.Update(have, pelvis.x, pelvis.y, pelvis.z, Seconds());
                const float feetToPosition = a_ped->GetDistanceFromCentreOfMassToBaseOfModel();
                const auto  p = state.seat.Place({ at.x, at.y, at.z - feetToPosition }, feetToPosition);
                placed = CVector(p.x, p.y, p.z);
                if (++state.seatFrames == 30) {
                    Log(std::format("scenes: CJ sits with his pelvis ({:.2f}, {:.2f}, {:.2f}) m from where he stands, put on the seat", state.seat.X(),
                        state.seat.Y(), state.seat.Z()));
                }
            } else {
                state.seat.Reset();
                state.seatFrames = 0;
            }
            a_ped->SetPosn(placed);
            a_ped->SetHeading(rotation);
            a_ped->m_fHeadingCurrent = rotation;
            a_ped->m_fHeadingGoal = rotation;
            a_ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
            const auto motion = state.tracker.Update(seated, { at.x, at.y, at.z }, Seconds());
            if (motion != state.motion) {
                state.motion = motion;
                PlayMotion(a_ped, motion);
                Log(std::format("scenes: CJ follows Skyrim's player, {} ({:.1f} m/s)", Scene::NameOf(motion), state.tracker.Speed()));
            }
            return;
        }
        if ((input.flags & Protocol::kInputFace) != 0 && !Vehicles::Driven(a_ped)) {
            const float rotation = Scene::TurnToward(a_ped->m_fHeadingCurrent, Pose::SanAndreasHeading(input.sceneHeading), kTurnRate * Seconds());
            a_ped->SetHeading(rotation);
            a_ped->m_fHeadingCurrent = rotation;
            a_ped->m_fHeadingGoal = rotation;
        }
    }

    void End(CPlayerPed* a_ped)
    {
        if (!state.following) {
            return;
        }
        state.following = false;
        a_ped->bUsesCollision = state.usedCollision;
        a_ped->bApplyGravity = state.hadGravity;
        a_ped->m_pIntelligence->m_TaskMgr.SetTask(nullptr, kTaskPrimary, false);
        CAnimManager::BlendAnimation(a_ped->m_pRwClump, a_ped->m_nAnimGroup, kMoveIdle, 8.0f);
        state.motion = Scene::Motion::Idle;
        Log("scenes: CJ has the player again");
    }

    bool Following() { return state.following; }
}
