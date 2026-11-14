#include "cj_melee.h"

#include "cj_shots.h"
#include "log.h"
#include "melee.h"
#include "pose.h"
#include "stealth_kill.h"
#include "vehicles.h"
#include "weapons.h"

#include <plugin.h>

#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CAnimBlendStaticAssociation.h>
#include <CAnimManager.h>
#include <CPad.h>
#include <CPlayerData.h>
#include <CPlayerPed.h>
#include <CStreaming.h>
#include <CTimer.h>
#include <CWeaponInfo.h>
#include <common.h>

#include <safetyhook.hpp>

#include <format>
#include <vector>

namespace AhShitRim::CjMelee
{
    namespace
    {
        constexpr std::uintptr_t kProcessPed = 0x688810;  // CTaskSimplePlayerOnFoot::ProcessPed (thiscall): the player's on-foot control
        constexpr int            kLoggedTouches = 8;      // the first touches are logged in full

        // The knife's stealth kill (v38): ANIM_ID_KILL_* in the knife's animation group (its weapon info's, as San
        // Andreas blends them; stealth_kn when that isn't set), its file kept in memory once loaded.
        constexpr int kKillPartial = 347;         // ANIM_ID_KILL_PARTIAL: the knife raised, over whatever CJ does
        constexpr int kKillKnifePlayer = 348;     // ANIM_ID_KILL_KNIFE_PLAYER: the grab and the stab
        constexpr int kKillKnifePedDamage = 349;  // ANIM_ID_KILL_KNIFE_PED_DAMAGE: San Andreas's victim's half until the stab
        constexpr int kGroupStealthKnife = 87;    // ANIM_GROUP_STEALTH_KN
        constexpr int kMoveIdle = 3;              // ANIM_ID_IDLE, within his own move group
        constexpr int kIfpBase = 25575;           // RESOURCE_ID_IFP: an animation file's streaming id is this plus its block's index
        constexpr int kStealthKillAudio = 0x9C;   // AE_WEAPON_STEALTH_KILL

        SafetyHookInline g_processPed;

        struct Kill
        {
            bool          on = false;
            std::uint32_t victim = 0;  // its form id
            CVector       at;          // its middle, San Andreas's world, as Skyrim last sent it
            float         seconds = 0.0f;
            float         stabAt = 0.0f;
            float         length = 0.0f;
            bool          stabbed = false;
        };

        struct State
        {
            bool          locked = false;  // turn CJ to the target
            CVector       target;          // its middle, San Andreas's world
            std::uint32_t targetId = 0;
            bool          stealth = false;  // v38: Skyrim says the target can be stealth killed (kProxyStealthKill)
            int           logged = 0;
            bool          touching = false;
            float         raised = 0.0f;    // KILL_PARTIAL's blend
            bool          raising = false;  // and it's ours
            bool          blockHeld = false;
            bool          loggedGroup = false;
            Kill          kill;
        } state;

        Pose::Vec3 Vec(const CVector& a_v) { return { a_v.x, a_v.y, a_v.z }; }
        CVector    Cv(const Pose::Vec3& a_v) { return CVector(a_v.x, a_v.y, a_v.z); }

        bool IsCj(const void* a_entity) { return a_entity && a_entity == FindPlayerPed(0); }

        float Seconds() { return CTimer::ms_fTimeStep * 0.02f; }  // San Andreas steps in 1/50 s

        // A proxy in San Andreas's world, metres (as CarHits has it).
        RunOver::Capsule CapsuleOf(const Protocol::ActorProxy& a_proxy, const Pose::Vec3& a_spawnFeet)
        {
            const auto at = [&](const float (&a_p)[3]) {
                return Body::Add(a_spawnFeet, Body::Scale({ a_p[0], a_p[1], a_p[2] }, Pose::kMetersPerUnit));
            };
            return { at(a_proxy.a), at(a_proxy.b), a_proxy.radius * Pose::kMetersPerUnit };
        }

        CVector MiddleOf(const RunOver::Capsule& a_capsule) { return Cv(Body::Scale(Body::Add(a_capsule.a, a_capsule.b), 0.5f)); }

        // ---- The stealth kill ----

        // The knife's animation group with its file in memory (requested when it isn't, then kept); -1 until then.
        int KnifeGroup()
        {
            const auto* info = CWeaponInfo::GetWeaponInfo(WEAPONTYPE_KNIFE, 1);
            const int   group = info && info->m_nAnimToPlay > 0 ? static_cast<int>(info->m_nAnimToPlay) : kGroupStealthKnife;
            // The group's file by its name. (plugin-sdk's ms_aAnimAssocGroups is the address of San Andreas's pointer
            // to the groups, not the groups.)
            const char* name = CAnimManager::GetAnimBlockName(group);
            const int   index = name && *name ? CAnimManager::GetAnimationBlockIndex(name) : -1;
            if (!state.loggedGroup) {
                state.loggedGroup = true;
                Log(std::format("melee: the knife's stealth kill plays from animation group {} ({}, file {})", group, name ? name : "no file", index));
            }
            if (index < 0) {
                return -1;
            }
            CAnimBlock* block = &CAnimManager::ms_aAnimBlocks[index];
            if (!block->bLoaded) {
                CStreaming::RequestModel(kIfpBase + index, 0x2 | 0x8);
                return -1;
            }
            if (!state.blockHeld) {
                state.blockHeld = true;
                CAnimManager::AddAnimBlockRef(index);
            }
            return group;
        }

        float LengthOf(int a_group, int a_anim)
        {
            const auto* assoc = CAnimManager::GetAnimAssociation(a_group, a_anim);
            return assoc && assoc->m_pHeirarchy ? assoc->m_pHeirarchy->m_fTotalTime : 0.0f;
        }

        // The grab or the stab, to Skyrim: a Shot of its own kind from where CJ stands, naming the victim.
        void SendKill(CPed* a_ped, std::uint8_t a_kind)
        {
            CVector  spawnFeet;
            unsigned handover = 0;
            if (!CjShots::Frame(spawnFeet, handover)) {
                return;
            }
            Protocol::ShotEvent shot{};
            shot.handover = handover;
            shot.weaponType = Weapons::Knife;
            shot.kind = a_kind;
            shot.target = state.kill.victim;
            const CVector at = a_ped->GetPosition() - spawnFeet;
            shot.origin[0] = at.x * Pose::kUnitsPerMeter;
            shot.origin[1] = at.y * Pose::kUnitsPerMeter;
            shot.origin[2] = at.z * Pose::kUnitsPerMeter;
            const auto& forward = a_ped->GetForward();
            shot.direction[0] = forward.x;
            shot.direction[1] = forward.y;
            shot.direction[2] = forward.z;
            if (!GetLink().Send(Protocol::MessageType::Shot, &shot, sizeof(shot))) {
                Log("melee: the stealth kill's message didn't fit on the ring");
            }
        }

        // CJ's control back. A kill cut short before its stab still sends it (Skyrim checks the victim and lets go).
        void EndKill(CPed* a_ped, const char* a_why)
        {
            if (!state.kill.stabbed) {
                SendKill(a_ped, Protocol::kShotStealthStab);
            }
            state.kill = {};
            if (auto* assoc = RpAnimBlendClumpGetAssociation(a_ped->m_pRwClump, kKillKnifePlayer)) {
                assoc->m_fBlendDelta = -4.0f;
            }
            CAnimManager::BlendAnimation(a_ped->m_pRwClump, a_ped->m_nAnimGroup, kMoveIdle, 4.0f);
            Log(std::format("melee: the stealth kill is over ({})", a_why));
        }

        // The left button with the knife raised on a flagged target: CJ grabs it, as CTaskSimpleStealthKill starts.
        bool StartKill(CPlayerPed* a_ped)
        {
            const auto& pad = CPad::GetPad(0)->NewState;
            const auto& old = CPad::GetPad(0)->OldState;
            if (!state.raising || !state.stealth || !state.targetId || !StealthKill::Stabs(state.raised, pad.ButtonCircle && !old.ButtonCircle)) {
                return false;
            }
            const int group = KnifeGroup();
            auto*     anim = group >= 0 ? CAnimManager::BlendAnimation(a_ped->m_pRwClump, group, kKillKnifePlayer, 8.0f) : nullptr;
            if (!anim) {
                return false;
            }
            const float cj = anim->m_pHierarchy ? anim->m_pHierarchy->m_fTotalTime : LengthOf(group, kKillKnifePlayer);
            const float victim = LengthOf(group, kKillKnifePedDamage);
            state.kill = {};
            state.kill.on = true;
            state.kill.victim = state.targetId;
            state.kill.at = state.target;
            state.kill.length = StealthKill::KillSeconds(cj);
            state.kill.stabAt = StealthKill::StabSeconds(cj, victim);
            if (auto* partial = RpAnimBlendClumpGetAssociation(a_ped->m_pRwClump, kKillPartial)) {
                partial->m_fBlendDelta = -StealthKill::kRaiseRate;
            }
            state.raised = 0.0f;
            state.raising = false;
            a_ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
            a_ped->m_weaponAudio.ReportStealthKill(WEAPONTYPE_KNIFE, a_ped, kStealthKillAudio);
            SendKill(a_ped, Protocol::kShotStealthGrab);
            Log(std::format("melee: CJ grabs {:08X} for a stealth kill (his anim {:.2f} s, the stab at {:.2f} s, the victim's anim {:.2f} s)", state.kill.victim, cj,
                state.kill.stabAt, victim));
            return true;
        }

        // CJ's half of the kill in place of his on-foot control: held kHold behind his victim and facing it, the stab
        // sent at its frame, his control back when the anim is done.
        void Killing(CPlayerPed* a_ped)
        {
            const float dt = Seconds();
            state.kill.seconds += dt;
            const CVector position = a_ped->GetPosition();
            const auto    step = StealthKill::HoldStep(Vec(position), Vec(state.kill.at), dt);
            if (step.x != 0.0f || step.y != 0.0f) {
                a_ped->SetPosn(position + Cv(step));
            }
            a_ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, a_ped->m_vecMoveSpeed.z);
            const float heading = Melee::Facing(Vec(position), Vec(state.kill.at), a_ped->m_fHeadingCurrent);
            a_ped->m_fHeadingCurrent = heading;
            a_ped->m_fHeadingGoal = heading;
            a_ped->SetHeading(heading);
            if (!state.kill.stabbed && state.kill.seconds >= state.kill.stabAt) {
                SendKill(a_ped, Protocol::kShotStealthStab);
                state.kill.stabbed = true;
                Log(std::format("melee: CJ's stab at {:.2f} s", state.kill.seconds));
            }
            if (state.kill.seconds >= state.kill.length) {
                EndKill(a_ped, "the anim is done");
            } else if (state.kill.seconds > 0.25f && !RpAnimBlendClumpGetAssociation(a_ped->m_pRwClump, kKillKnifePlayer)) {
                EndKill(a_ped, "the anim was taken off");
            }
        }

        // KILL_PARTIAL while Skyrim flags the target and CJ holds the knife on it, blended at San Andreas's rate. Run
        // after San Andreas's own control, which takes it off every frame it has no ped to kill.
        void RaiseKnife(CPlayerPed* a_ped)
        {
            const auto  type = static_cast<std::uint8_t>(a_ped->m_aWeapons[a_ped->m_nSelectedWepSlot].m_eWeaponType);
            const auto* data = a_ped->m_pPlayerData;
            const bool  up = StealthKill::Raises(state.stealth && state.locked, type, CPad::GetPad(0)->NewState.RightShoulder1 != 0,
                data ? data->m_fMoveBlendRatio : 0.0f, a_ped->m_nMoveState == PEDMOVE_SPRINT);
            const int group = up || state.raising ? KnifeGroup() : -1;
            if (group < 0) {
                state.raised = 0.0f;
                state.raising = false;
                return;
            }
            state.raised = StealthKill::Raise(state.raised, up, Seconds());
            auto* assoc = RpAnimBlendClumpGetAssociation(a_ped->m_pRwClump, kKillPartial);
            if (state.raised > 0.0f) {
                if (!assoc) {
                    assoc = CAnimManager::BlendAnimation(a_ped->m_pRwClump, group, kKillPartial, StealthKill::kRaiseRate);
                }
                if (assoc) {
                    assoc->m_fBlendAmount = state.raised;
                    assoc->m_fBlendDelta = 0.0f;
                }
                if (!state.raising) {
                    Log(std::format("melee: CJ raises the knife on {:08X}", state.targetId));
                }
                state.raising = true;
            } else {
                if (assoc) {
                    assoc->m_fBlendAmount = 0.0f;
                    assoc->m_fBlendDelta = -StealthKill::kRaiseRate;
                }
                state.raising = false;
            }
        }

        // The player's on-foot control: as San Andreas has it, then, locked on and standing to fight (not
        // running: then San Andreas's own PlayerControlZelda steers him), turned toward the target at the lock's
        // rate. Both headings are set, so San Andreas doesn't turn him back to the camera before his blow lands.
        // v38: with the knife raised on a flagged target the left button starts the stealth kill instead, and while it
        // runs CJ's control is the kill's (San Andreas's own CTaskSimpleStealthKill needs a ped to kill).
        bool __fastcall ProcessPedHook(void* a_task, void*, CPed* a_ped)
        {
            const bool cj = IsCj(a_ped) && !a_ped->bInVehicle;
            if (cj && state.kill.on) {
                Killing(static_cast<CPlayerPed*>(a_ped));
                return false;
            }
            if (cj && StartKill(static_cast<CPlayerPed*>(a_ped))) {
                return false;
            }
            const bool moving = a_ped && a_ped->m_nMoveState >= PEDMOVE_WALK;
            const bool done = g_processPed.thiscall<bool>(a_task, a_ped);
            if (cj) {
                RaiseKnife(static_cast<CPlayerPed*>(a_ped));
            }
            if (state.locked && !moving && cj) {
                const float rotation = Melee::Turn(a_ped->m_fHeadingCurrent, Vec(a_ped->GetPosition()), Vec(state.target), Seconds());
                a_ped->m_fHeadingCurrent = rotation;
                a_ped->m_fHeadingGoal = rotation;
                a_ped->SetHeading(rotation);
            }
            return done;
        }
    }

    void Install()
    {
        g_processPed = safetyhook::create_inline(reinterpret_cast<void*>(kProcessPed), reinterpret_cast<void*>(&ProcessPedHook));
        Log(g_processPed ? "melee: CTaskSimplePlayerOnFoot::ProcessPed hooked" : "melee: could not hook CTaskSimplePlayerOnFoot::ProcessPed, no lock-on");
    }

    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, bool a_active)
    {
        const bool wasLocked = state.locked;
        const auto wasTarget = state.targetId;
        const bool wasStealth = state.stealth;
        state.locked = false;
        state.targetId = 0;
        state.stealth = false;
        Protocol::ProxyState proxies{};
        const bool onFoot = a_active && !CTimer::m_UserPause && !CTimer::m_CodePause && !Vehicles::Driven(a_ped) && !a_ped->bInVehicle && !a_ped->m_pAttachedTo;
        if (onFoot && GetLink().ReadProxies(proxies) && proxies.handover == a_handover) {
            const Pose::Vec3 spawnFeet = Vec(a_spawn - CVector(0.0f, 0.0f, a_ped->GetDistanceFromCentreOfMassToBaseOfModel() + a_lift));
            const auto       count = (std::min)(proxies.count, Protocol::kProxyMax);
            std::vector<RunOver::Capsule> capsules;
            const Protocol::ActorProxy*   target = nullptr;
            for (std::uint32_t i = 0; i < count; i++) {
                capsules.push_back(CapsuleOf(proxies.proxies[i], spawnFeet));
                if (proxies.proxies[i].flags & Protocol::kProxyTarget) {
                    target = &proxies.proxies[i];
                }
                if (state.kill.on && proxies.proxies[i].id == state.kill.victim) {
                    state.kill.at = MiddleOf(capsules.back());  // the victim, while Skyrim still sends it
                }
            }

            // The lock-on: Skyrim flags the target while the right mouse button is held, or for a swing's combo;
            // San Andreas still has to hold a melee weapon (Skyrim's flag lags a frame behind a weapon switch).
            const auto type = static_cast<std::uint8_t>(a_ped->m_aWeapons[a_ped->m_nSelectedWepSlot].m_eWeaponType);
            if (target && Weapons::IsMelee(type)) {
                state.locked = true;
                state.targetId = target->id;
                state.target = MiddleOf(CapsuleOf(*target, spawnFeet));
                state.stealth = (target->flags & Protocol::kProxyStealthKill) != 0;
            }

            // Bumping into them.
            const CVector    position = a_ped->GetPosition();
            const float      base = a_ped->GetDistanceFromCentreOfMassToBaseOfModel();
            const Pose::Vec3 feet = Vec(position - CVector(0.0f, 0.0f, base));
            const auto&      forward = a_ped->GetForward();
            const auto       resolved = Melee::Resolve(feet, Body::Scale(Vec(a_ped->m_vecMoveSpeed), 50.0f), { -forward.x, -forward.y, 0.0f }, capsules);
            if (resolved.touching) {
                a_ped->SetPosn(position + Cv(resolved.move));
                a_ped->m_vecMoveSpeed = Cv(Body::Scale(resolved.velocity, 1.0f / 50.0f));  // San Andreas's speeds are metres a 1/50 s step
                if (!state.touching && state.logged < kLoggedTouches) {
                    state.logged++;
                    Log(std::format("melee: CJ bumps into {} of the {} actors around him, pushed {:.2f} m", resolved.touching, count, RunOver::Length(resolved.move)));
                }
            }
            state.touching = resolved.touching > 0;
        } else {
            state.touching = false;
        }
        if (state.targetId != wasTarget || state.locked != wasLocked) {
            Log(state.locked ? std::format("melee: CJ locks on to {:08X}", state.targetId) : std::string("melee: lock-on let go"));
        }
        if (state.locked && state.stealth != wasStealth) {
            Log(std::format("melee: {:08X} {} be stealth killed", state.targetId, state.stealth ? "can" : "can't"));
        }
        if (state.kill.on && (!a_active || Vehicles::Driven(a_ped) || a_ped->bInVehicle || a_ped->m_pAttachedTo)) {
            EndKill(a_ped, "something else has CJ");  // Wasted, a scene, a vehicle; a pause only waits
        }
    }
}
