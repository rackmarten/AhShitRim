#include "scenes.h"

#include "cj_body.h"
#include "log.h"
#include "pose.h"
#include "scene.h"
#include "vehicles.h"
#include "voice_lines.h"

#include <plugin.h>

#include <CAnimBlendAssociation.h>
#include <CAnimManager.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CTaskSimpleRunAnim.h>
#include <CTimer.h>
#include <common.h>

#include <Windows.h>

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
        constexpr unsigned short kCrashGeneric = 68;  // CTX_GLOBAL_CRASH_GENERIC; CJ's CTX_PLY_CRASH_GENERIC, his "oh shit" lines
        constexpr std::uintptr_t kSpeechNextPlayTime = 0xB61670;  // gta-reversed: gGlobalSpeechContextNextPlayTime, uint32 ms per context
        constexpr std::uintptr_t kStopCurrentSpeech = 0x4E3FB0;   // CAEPedSpeechAudioEntity::StopCurrentSpeech
        constexpr std::uintptr_t kCallGetSoundAndBankIds = 0x4E67E2;  // AddSayEvent's call of CAEPedSpeechAudioEntity::GetSoundAndBankIDs (0x4E5920)
        constexpr std::size_t    kSpeechSoundId = 0xA4;  // CAEPedSpeechAudioEntity::m_SoundID (int16), plugin-sdk's field_A4
        constexpr std::size_t    kSpeechBankId = 0xA6;   // m_BankID (int16), field_A6
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
            bool                 haveSpeech = false;  // likewise for insultCount and swearCount
            std::uint16_t        insultCount = 0;
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

        // The lines picked for each key (voice_lines.h), and the one the next Say plays instead of San
        // Andreas's own pick: GetSoundAndBankIDs, called from AddSayEvent, chooses a bank and a sound
        // for the context and CJ's mood and leaves them in the entity, which then loads and plays them.
        using GetSoundAndBankIds = short(__fastcall*)(CAEPedSpeechAudioEntity*, void*, int, short*);
        GetSoundAndBankIds g_getSoundAndBankIds = nullptr;
        VoiceLines::Deck   g_insultLines(VoiceLines::kInsult, ::GetTickCount());
        VoiceLines::Deck   g_swearLines(VoiceLines::kSwear, ::GetTickCount() * 31u);
        const CAEPedSpeechAudioEntity* g_forcedFor = nullptr;
        VoiceLines::Line               g_forced{};

        short __fastcall GetSoundAndBankIdsHook(CAEPedSpeechAudioEntity* a_speech, void*, int a_context, short* a_specific)
        {
            if (a_speech == g_forcedFor) {
                g_forcedFor = nullptr;
                auto* fields = reinterpret_cast<std::uint8_t*>(a_speech);
                *reinterpret_cast<short*>(fields + kSpeechSoundId) = static_cast<short>(g_forced.sound);
                *reinterpret_cast<short*>(fields + kSpeechBankId) = static_cast<short>(g_forced.bank);
                *a_specific = 0;  // only read inside AddSayEvent's caller for -1
                return 0;
            }
            return g_getSoundAndBankIds(a_speech, nullptr, a_context, a_specific);
        }

        // Has CJ say a line of a_context now: the next of a_lines when it has any, else San Andreas's
        // own pick. San Andreas waits RepeatTime (7 s, plus up to 1 s) before a context plays again and
        // drops a line while he still speaks, so both are cleared first: each press is heard, cutting
        // off the last line.
        void SayNow(CPlayerPed* a_ped, unsigned short a_context, VoiceLines::Deck& a_lines, const char* a_what)
        {
            plugin::CallMethod<kStopCurrentSpeech, CAEPedSpeechAudioEntity*>(&a_ped->m_pedSpeech);
            reinterpret_cast<std::uint32_t*>(kSpeechNextPlayTime)[a_context] = 0;
            if (!a_lines.Empty() && g_getSoundAndBankIds) {
                g_forced = a_lines.Next();
                g_forcedFor = &a_ped->m_pedSpeech;
                Log(std::format("scenes: CJ {}, bank {} sound {}", a_what, g_forced.bank, g_forced.sound));
            } else {
                Log(std::format("scenes: CJ {}", a_what));
            }
            a_ped->Say(a_context, 0, 1.0f, true, true);
            g_forcedFor = nullptr;  // Say gave up before picking (streaming busy, say): the next one picks again
        }

        // Points the call at a_site to a_detour; returns what it called before, or nullptr when the site is
        // not a call (then it is left alone).
        template <class T>
        T RedirectCall(std::uintptr_t a_site, T a_detour)
        {
            auto* code = reinterpret_cast<std::uint8_t*>(a_site);
            if (code[0] != 0xE8) {
                Log(std::format("scenes: 0x{:X} is not a call, not redirected", a_site));
                return nullptr;
            }
            auto*      rel = reinterpret_cast<std::int32_t*>(code + 1);
            const auto before = static_cast<std::uintptr_t>(a_site + 5 + *rel);
            DWORD      old = 0;
            ::VirtualProtect(rel, 4, PAGE_EXECUTE_READWRITE, &old);
            *rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(a_detour) - (a_site + 5));
            ::VirtualProtect(rel, 4, old, &old);
            return reinterpret_cast<T>(before);
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

    void Install()
    {
        g_getSoundAndBankIds = RedirectCall(kCallGetSoundAndBankIds, &GetSoundAndBankIdsHook);
        Log(std::format("scenes: {} insult and {} swear lines picked{}", VoiceLines::kInsult.size(), VoiceLines::kSwear.size(),
            g_getSoundAndBankIds ? "" : ", but San Andreas picks them (no hook)"));
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

        // The insult and swear keys (v16): CJ's own lines, also out of the window in a vehicle; an insult
        // on foot comes with the finger.
        const bool wasted = (a_input.flags & Protocol::kInputWasted) != 0;
        const bool canSpeak = a_owned && !state.following && !wasted;
        if (state.haveSpeech && a_input.insultCount != state.insultCount && canSpeak) {
            SayNow(a_ped, kInsultMale, g_insultLines, "insults");
            if (!Vehicles::Driven(a_ped) && !a_ped->m_pVehicle) {
                PlayOnce(a_ped, kFuckU);
            }
        }
        if (state.haveSpeech && a_input.swearCount != state.swearCount && canSpeak) {
            SayNow(a_ped, kCrashGeneric, g_swearLines, "swears");
        }
        state.insultCount = a_input.insultCount;
        state.swearCount = a_input.swearCount;
        state.haveSpeech = true;

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
