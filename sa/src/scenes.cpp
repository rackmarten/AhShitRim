#include "scenes.h"

#include "audio.h"
#include "cj_body.h"
#include "dialogue.h"
#include "log.h"
#include "pose.h"
#include "scene.h"
#include "situations.h"
#include "thuum.h"
#include "vehicles.h"
#include "voice_lines.h"
#include "wanted.h"
#include "weapons.h"

#include <plugin.h>

#include <CAnimBlendAssociation.h>
#include <CAnimManager.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CTaskSimpleRunAnim.h>
#include <CTimer.h>
#include <CVehicle.h>
#include <common.h>

#include <Windows.h>

#include <format>
#include <random>
#include <string>

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
        constexpr unsigned short kChased = 44;        // CTX_GLOBAL_CHASED: CJ with the cops after him (v22, Skyrim's wanted level)
        constexpr unsigned short kArrested = 15;      // CTX_GLOBAL_ARRESTED: what CGameLogic has him say when busted
        constexpr unsigned short kCrashCar = 67;      // CTX_GLOBAL_CRASH_CAR; CJ's CTX_PLY_CRASH_CAR, what he says crashing a car
        constexpr int            kPainStart = 339;    // CTX_GLOBAL_PAIN_START: the pain contexts from here on
        constexpr int            kPainHighFall = 358;  // CTX_GLOBAL_PAIN_CJ_HIGH_FALL, which San Andreas plays with his CRASH_GENERIC lines
        constexpr std::uintptr_t kSpeechNextPlayTime = 0xB61670;  // gta-reversed: gGlobalSpeechContextNextPlayTime, uint32 ms per context
        constexpr std::uintptr_t kStopCurrentSpeech = 0x4E3FB0;   // CAEPedSpeechAudioEntity::StopCurrentSpeech
        constexpr std::uintptr_t kPlayerSpeaking = 0xB613E5;      // gta-reversed: CAEPedSpeechAudioEntity::s_bAPlayerSpeaking, a line of CJ's plays
        constexpr std::uintptr_t kCallGetSoundAndBankIds = 0x4E67E2;  // AddSayEvent's call of CAEPedSpeechAudioEntity::GetSoundAndBankIDs (0x4E5920)
        constexpr std::size_t    kSpeechSoundId = 0xA4;  // CAEPedSpeechAudioEntity::m_SoundID (int16), plugin-sdk's field_A4
        constexpr std::size_t    kSpeechBankId = 0xA6;   // m_BankID (int16), field_A6
        constexpr int kTaskPrimary = 3;       // TASK_PRIMARY_PRIMARY, over CJ's own on-foot task
        constexpr unsigned short kAnimLooped = 0x2;           // ANIMATION_LOOPED
        constexpr unsigned short kAnimFinishAutoRemove = 0x8;  // gta-reversed: ANIMATION_IS_FINISH_AUTO_REMOVE
        constexpr int kMoveStill = 1;   // eMoveState: PEDMOVE_STILL
        constexpr int kMoveSprint = 7;  // PEDMOVE_SPRINT

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
            bool                 haveSpeech = false;  // likewise for insultCount, swearCount and lineCount
            std::uint16_t        insultCount = 0;
            std::uint16_t        swearCount = 0;
            std::uint16_t        lineCount = 0;
            // v29, CJ talking on his own (situations.h): Skyrim's counters, the edges seen here, a song.
            bool                 canSpeak = false;
            Situations::Counter  stealthKills, cash, shopBuys, shopLeaves;
            Situations::Edge     pullGun, chased;
            Situations::Burst    burst;
            Situations::Crash    crash;
            Situations::Singing  singing;
            bool                 song = false;  // the line playing is a song
            float                songAt = 0.0f;  // when it was asked for: it takes a moment to start
        } state;

        float Seconds() { return CTimer::ms_fTimeStep * 0.02f; }  // San Andreas steps in 1/50 s
        float Now() { return static_cast<float>(CTimer::m_snTimeInMilliseconds) * 0.001f; }  // stops while paused
        bool  Speaking() { return *reinterpret_cast<const bool*>(kPlayerSpeaking); }

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
        // CJ's answers in a conversation, a deck per category (dialogue.h).
        VoiceLines::Deck DialogueDeck(std::uint8_t a_kind)
        {
            return VoiceLines::Deck(Dialogue::LinesOf(static_cast<Dialogue::Category>(a_kind)), ::GetTickCount() * (2u * a_kind + 37u));
        }
        std::array<VoiceLines::Deck, Dialogue::kCategoryCount> g_dialogueLines{ DialogueDeck(0), DialogueDeck(1), DialogueDeck(2), DialogueDeck(3),
            DialogueDeck(4), DialogueDeck(5), DialogueDeck(6), DialogueDeck(7) };
        // His lines for each of Skyrim's shouts and the words charged (thuum.h), and his generic ones.
        Thuum::Voice g_shoutLines(VoiceLines::kShoutPicks, VoiceLines::kShoutGeneric, ::GetTickCount() * 53u);
        VoiceLines::Deck   g_crashLines;  // no picks: San Andreas's own CRASH_CAR line for his mood
        // His lines for each situation he talks in on his own (situations.h), and when he does.
        VoiceLines::Deck SituationDeck(std::size_t a_index)
        {
            return VoiceLines::Deck(Situations::LinesOf(static_cast<Situations::Trigger>(a_index)), ::GetTickCount() * (2u * static_cast<std::uint32_t>(a_index) + 71u));
        }
        static_assert(Situations::kTriggerCount == 10);
        std::array<VoiceLines::Deck, Situations::kTriggerCount> g_situationLines{ SituationDeck(0), SituationDeck(1), SituationDeck(2),
            SituationDeck(3), SituationDeck(4), SituationDeck(5), SituationDeck(6), SituationDeck(7), SituationDeck(8), SituationDeck(9) };
        Situations::Chatter g_chatter;
        std::minstd_rand    g_roll(::GetTickCount() | 1u);
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
            const short sound = g_getSoundAndBankIds(a_speech, nullptr, a_context, a_specific);
            // A line San Andreas has CJ say on its own, not his grunts of pain (but his fall): logged, to see which it
            // says against Skyrim's world, and the quiet before a trigger speaks starts over.
            if (const auto* cj = FindPlayerPed(); cj && a_speech == &cj->m_pedSpeech && sound >= 0 && (a_context < kPainStart || a_context == kPainHighFall)) {
                g_chatter.Heard(Now());
                const auto* fields = reinterpret_cast<const std::uint8_t*>(a_speech);
                Log(std::format("scenes: San Andreas has CJ say a line of context {} (bank {} sound {})", a_context,
                    *reinterpret_cast<const short*>(fields + kSpeechBankId), *reinterpret_cast<const short*>(fields + kSpeechSoundId)));
            }
            return sound;
        }

        // Has CJ say a line of a_context now: the next of a_lines when it has any, else San Andreas's
        // own pick. San Andreas waits RepeatTime (7 s, plus up to 1 s) before a context plays again and
        // drops a line while he still speaks, so both are cleared first: each press is heard, cutting
        // off the last line.
        void SayNow(CPlayerPed* a_ped, unsigned short a_context, VoiceLines::Deck& a_lines, const char* a_what)
        {
            g_chatter.Heard(Now());
            state.song = false;
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

        // A situation came up (v29): CJ says its line if the rules let him (a gap after his last line,
        // the trigger's chance and wait, nothing of his playing), and never while he can't speak.
        void Fire(CPlayerPed* a_ped, Situations::Trigger a_trigger)
        {
            if (!state.canSpeak) {
                return;
            }
            const auto& info = Situations::InfoOf(a_trigger);
            const float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(g_roll);
            const bool  speaking = Speaking();
            if (!g_chatter.Speak(a_trigger, Now(), roll, speaking)) {
                if (a_trigger != Situations::Trigger::Fight && a_trigger != Situations::Trigger::Shoot) {  // those come with every blow and burst
                    Log(std::format("scenes: {}, CJ keeps quiet ({})", info.name, speaking ? "he's speaking" : "too soon, or the chance"));
                }
                return;
            }
            auto&      lines = g_situationLines[static_cast<std::size_t>(a_trigger)];
            const auto what = std::format("talks on his own ({}{})", info.name, lines.Empty() ? ", San Andreas picks" : "");
            SayNow(a_ped, info.context, lines, what.c_str());
            state.song = a_trigger == Situations::Trigger::Singing;
            state.songAt = Now();
        }

        // The situations San Andreas sees, once a frame while CJ is his own: pulling a gun, running from a
        // fight, a crash, singing.
        void Talk(CPlayerPed* a_ped)
        {
            const auto& input = state.input;
            CVehicle*   vehicle = Vehicles::Driven(a_ped);
            const bool  onFoot = !vehicle && !a_ped->m_pVehicle;
            const auto  weapon = static_cast<std::uint8_t>(a_ped->m_aWeapons[a_ped->m_nSelectedWepSlot].m_eWeaponType);
            const bool  aiming = (input.buttons & Protocol::kButtonAim) != 0;
            const int   move = static_cast<int>(a_ped->m_nMoveState);
            const float now = Now();
            if (state.pullGun.Update(onFoot && aiming && Weapons::Flashes(weapon))) {
                Fire(a_ped, Situations::Trigger::PullGun);
            }
            if (state.chased.Update(onFoot && move == kMoveSprint && (input.flags & Protocol::kInputHostileNear) != 0)) {
                Fire(a_ped, Situations::Trigger::Chased);
            }
            if (state.crash.Update(vehicle, vehicle ? vehicle->m_fHealth : 0.0f, now)) {
                Fire(a_ped, Situations::Trigger::Crash);
            }
            // Settled: in a vehicle with the radio off, or standing idle on foot, not talking to anyone; and
            // nobody hostile about.
            const bool hostile = (input.flags & Protocol::kInputHostileNear) != 0;
            const bool idle = onFoot && move == kMoveStill && !aiming && (input.flags & Protocol::kInputFace) == 0 && input.moveX == 0.0f &&
                input.moveY == 0.0f;
            const bool settled = state.canSpeak && !hostile && ((vehicle && !Audio::RadioPlays()) || idle);
            state.song = state.song && (Speaking() || now - state.songAt < 1.0f);
            switch (state.singing.Update(settled, now, g_chatter.Quiet(now), state.song)) {
            case Situations::Singing::Action::Start:
                Fire(a_ped, Situations::Trigger::Singing);
                break;
            case Situations::Singing::Action::Stop:
                plugin::CallMethod<kStopCurrentSpeech, CAEPedSpeechAudioEntity*>(&a_ped->m_pedSpeech);
                state.song = false;
                Log("scenes: CJ stops singing");
                break;
            default:
                break;
            }
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
        std::string situations;
        for (std::size_t i = 0; i < Situations::kTriggerCount; ++i) {
            situations += std::format("{}{} {}", i ? ", " : "", Situations::kTriggers[i].key, Situations::LinesOf(static_cast<Situations::Trigger>(i)).size());
        }
        Log(std::format("scenes: {} insult, {} swear and {} shout lines picked ({} generic); on his own: {}{}", VoiceLines::kInsult.size(),
            VoiceLines::kSwear.size(), VoiceLines::kShoutPicks.size(), VoiceLines::kShoutGeneric.size(), situations,
            g_getSoundAndBankIds ? "" : ", but San Andreas picks them (no hook), and CJ keeps quiet in conversations"));
    }

    bool OnInput(CPlayerPed* a_ped, const Protocol::InputState& a_input, bool a_owned)
    {
        state.input = a_input;
        const bool wasted = (a_input.flags & Protocol::kInputWasted) != 0;
        const bool canSpeak = a_owned && !state.following && !wasted;
        // The first value read only seeds the count, as for the spawn keys.
        if (state.haveShout && a_input.shoutCount != state.shoutCount && a_owned) {
            if (!Vehicles::Driven(a_ped) && !state.following) {
                PlayOnce(a_ped, kShovePartial);
                PlayOnce(a_ped, kFaceAnger);
                Log("scenes: Skyrim's player shouted, CJ gestures");
            }
        }
        state.shoutCount = a_input.shoutCount;
        state.haveShout = true;

        // The insult and swear keys (v16): CJ's own lines, also out of the window in a vehicle; an insult
        // on foot comes with the finger.
        if (state.haveSpeech && a_input.insultCount != state.insultCount && canSpeak) {
            SayNow(a_ped, kInsultMale, g_insultLines, "insults");
            if (!Vehicles::Driven(a_ped) && !a_ped->m_pVehicle) {
                PlayOnce(a_ped, kFuckU);
            }
        }
        if (state.haveSpeech && a_input.swearCount != state.swearCount && canSpeak) {
            SayNow(a_ped, kCrashGeneric, g_swearLines, "swears");
        }
        // A line Skyrim asks for (v21): CJ's answer in a conversation, also in a scene. Only his own lines:
        // the insult context merely carries them, so without the hook he says nothing.
        const auto kind = a_input.lineKind;
        if (state.haveSpeech && a_input.lineCount != state.lineCount && canSpeak && Thuum::IsShout(kind)) {
            // v25, the Thu'um: his line for the shout and its words, where the Dragonborn's voice was.
            const int  shout = Thuum::ShoutOf(kind);
            const int  words = Thuum::WordsOf(kind);
            const int  pool = g_shoutLines.PoolWords(shout, words);
            const auto what = std::format("shouts ({}, {} word{}: {})", shout >= 0 ? Thuum::kShouts[shout].name : "not one of the table's", words,
                words == 1 ? "" : "s", pool ? std::format("its {}-word lines", pool) : std::string("his generic lines"));
            SayNow(a_ped, kInsultMale, g_shoutLines.For(shout, words), what.c_str());
        } else if (state.haveSpeech && a_input.lineCount != state.lineCount && a_owned && !wasted && (kind == Wanted::kLineChased || kind == Wanted::kLineArrested)) {
            // v22, the wanted level: San Andreas's own lines of CJ's for it, as it picks them.
            static VoiceLines::Deck none;
            SayNow(a_ped, kind == Wanted::kLineChased ? kChased : kArrested, none, kind == Wanted::kLineChased ? "is chased" : "is arrested");
        } else if (state.haveSpeech && a_input.lineCount != state.lineCount && a_owned && !wasted && g_getSoundAndBankIds && kind > 0 &&
            kind < Dialogue::kCategoryCount && !g_dialogueLines[kind].Empty()) {
            const auto what = std::format("answers ({})", Dialogue::NameOf(static_cast<Dialogue::Category>(kind)));
            SayNow(a_ped, kInsultMale, g_dialogueLines[kind], what.c_str());
        }
        state.insultCount = a_input.insultCount;
        state.swearCount = a_input.swearCount;
        state.lineCount = a_input.lineCount;
        state.haveSpeech = true;

        // v29, CJ talking on his own: what Skyrim saw (situations.h); the first values only seed.
        state.canSpeak = canSpeak && (a_input.flags & Protocol::kInputBusted) == 0;
        if (state.stealthKills.Update(a_input.stealthKillCount)) {
            Fire(a_ped, Situations::Trigger::StealthKill);
        }
        if (state.cash.Update(a_input.cashCount)) {
            Fire(a_ped, Situations::Trigger::Cash);
        }
        if (state.shopBuys.Update(a_input.shopBuyCount)) {
            Fire(a_ped, Situations::Trigger::ShopBuy);
        }
        if (state.shopLeaves.Update(a_input.shopLeaveCount)) {
            Fire(a_ped, Situations::Trigger::ShopLeave);
        }

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
        Talk(a_ped);
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

    void SayCrash(CPlayerPed* a_ped)
    {
        if (a_ped && !state.following) {
            SayNow(a_ped, kCrashCar, g_crashLines, "hit someone with his vehicle");
        }
    }

    void OnHit(CPlayerPed* a_ped, std::uint8_t a_weapon)
    {
        if (Weapons::IsMelee(a_weapon)) {
            Fire(a_ped, Situations::Trigger::Fight);
        } else if (state.burst.Hit(Now())) {
            Fire(a_ped, Situations::Trigger::Shoot);
        }
    }
}
