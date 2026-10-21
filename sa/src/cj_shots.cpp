#include "cj_shots.h"

#include "log.h"
#include "pose.h"
#include "weapons.h"

#include <plugin.h>

#include <CCamera.h>
#include <CDraw.h>
#include <CPlayerData.h>
#include <CPlayerPed.h>
#include <CTaskSimpleFight.h>
#include <CWeapon.h>
#include <CWeaponInfo.h>
#include <common.h>

#include <safetyhook.hpp>

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <set>

namespace AhShitRim::CjShots
{
    namespace
    {
        // ---- San Andreas 1.0 US addresses (gta-reversed names) ----

        constexpr std::uintptr_t kFireInstantHit = 0x73FB10;  // CWeapon::FireInstantHit (thiscall): the guns' bullets and a shotgun's pellets
        // Its calls to CBirds::HandleGunShot (cdecl, the line's start and end), one before each line test,
        // made for every line it fires, whether it hits anything or not.
        constexpr std::uintptr_t kCallsHandleGunShot[] = { 0x73FD87, 0x7406E6, 0x740B2E, 0x74127A };
        constexpr std::uintptr_t kDoBulletImpact = 0x73B550;  // CWeapon::DoBulletImpact (thiscall): the first-person rifles' bullets (and the drive-bys')
        constexpr std::uintptr_t kFireSniper = 0x73AAC0;      // CWeapon::FireSniper (thiscall): the sniper rifle from the camera
        constexpr std::uintptr_t kFightStrike = 0x6240B0;     // CTaskSimpleFight::FightStrike (thiscall): a blow at its hit frame
        constexpr std::uintptr_t kFireAreaEffect = 0x73E800;  // CWeapon::FireAreaEffect (thiscall): the flamethrower's (and the spray cans') puff

        // melee.dat's combos (CTaskSimpleFight::m_aComboData): one CMeleeInfo of 0x88 bytes each, the first
        // for combo set 4 (MELEE_COMBO_UNARMED_1). Per move (attack 1 to 3, on the ground, moving): the
        // blow's radius (metres) at 0x30, its damage a byte at 0x55.
        constexpr std::uintptr_t kComboData = 0xC170D0;
        constexpr int            kComboSize = 0x88;
        constexpr int            kComboFirst = 4;
        constexpr int            kComboCount = 17;
        constexpr int            kMoveCount = 5;
        constexpr int            kComboRadius = 0x30;
        constexpr int            kComboDamage = 0x55;

        SafetyHookInline g_fireInstantHit;
        SafetyHookInline g_doBulletImpact;
        void(__cdecl* g_handleGunShot)(const CVector*, const CVector*) = nullptr;
        SafetyHookInline g_fireSniper;
        SafetyHookInline g_fightStrike;
        SafetyHookInline g_fireAreaEffect;

        struct State
        {
            CVector                spawnFeet;
            unsigned               handover = 0;
            bool                   framed = false;
            std::uint32_t          strike = 0;
            const void*            lastTask = nullptr;
            const void*            lastAnim = nullptr;
            int                    lastMove = -1;
            std::uint64_t          lastBlowMs = 0;
            std::set<std::uint8_t> loggedWeapons;
            std::set<int>          loggedCombos;
            unsigned               sent = 0;
            unsigned               dropped = 0;
            std::uint64_t          summaryMs = 0;
            CWeapon*               firing = nullptr;  // FireInstantHit runs for this weapon
            CEntity*               firedBy = nullptr;
        } state;

        bool IsCj(const void* a_entity)
        {
            return a_entity && a_entity == FindPlayerPed(0);
        }

        void Send(Protocol::ShotEvent& a_shot, const CVector& a_origin, const char* a_what)
        {
            if (!state.framed) {
                return;
            }
            a_shot.handover = state.handover;
            const CVector at = a_origin - state.spawnFeet;
            a_shot.origin[0] = at.x * Pose::kUnitsPerMeter;
            a_shot.origin[1] = at.y * Pose::kUnitsPerMeter;
            a_shot.origin[2] = at.z * Pose::kUnitsPerMeter;
            for (float v : { a_shot.origin[0], a_shot.origin[1], a_shot.origin[2], a_shot.direction[0], a_shot.direction[1], a_shot.direction[2], a_shot.range, a_shot.damage }) {
                if (!std::isfinite(v)) {
                    return;
                }
            }
            if (GetLink().Send(Protocol::MessageType::Shot, &a_shot, sizeof(a_shot))) {
                state.sent++;
            } else {
                state.dropped++;
            }
            if (state.loggedWeapons.insert(a_shot.weaponType).second) {
                Log(std::format("shots: the first {} of weapon {} sent: from ({:.0f},{:.0f},{:.0f}) along ({:.2f},{:.2f},{:.2f}), reach {:.0f}, damage {:.0f}, skill {}",
                    a_what, a_shot.weaponType, a_shot.origin[0], a_shot.origin[1], a_shot.origin[2], a_shot.direction[0], a_shot.direction[1],
                    a_shot.direction[2], a_shot.range, a_shot.damage, a_shot.skill));
            }
            const std::uint64_t now = ::GetTickCount64();
            if (now - state.summaryMs >= 5000) {
                if (state.summaryMs) {
                    Log(std::format("shots: {} sent to Skyrim, {} dropped (the ring was full or the link down) in the last 5 s or more", state.sent, state.dropped));
                }
                state.summaryMs = now;
                state.sent = state.dropped = 0;
            }
        }

        void SendBullet(CPed* a_ped, eWeaponType a_type, const CVector& a_from, const CVector& a_along, std::uint8_t a_flags)
        {
            CVector dir = a_along;
            const float length = dir.Magnitude();
            if (!(length > 1e-4f)) {
                return;
            }
            dir /= length;
            const auto  skill = static_cast<std::uint8_t>(a_ped->GetWeaponSkill(a_type));
            const auto* info = CWeaponInfo::GetWeaponInfo(a_type, skill);
            Protocol::ShotEvent shot{};
            shot.weaponType = static_cast<std::uint8_t>(a_type);
            shot.kind = Protocol::kShotBullet;
            shot.skill = skill;
            shot.flags = a_flags;
            shot.damage = info ? static_cast<float>(info->m_nDamage) : 0.0f;
            shot.direction[0] = dir.x;
            shot.direction[1] = dir.y;
            shot.direction[2] = dir.z;
            shot.range = (std::max)(length, info ? info->m_fWeaponRange : 0.0f) * Pose::kUnitsPerMeter;
            Send(shot, a_from, (a_flags & Protocol::kShotSpread) ? "shotgun shot" : "bullet");
        }

        bool __fastcall FireInstantHitHook(CWeapon* a_weapon, void*, CEntity* a_firedBy, CVector* a_origin, CVector* a_muzzle, CEntity* a_target, CVector* a_targetPos,
            CVector* a_driveBy, bool a_arg6, bool a_arg7)
        {
            auto* const outerWeapon = state.firing;
            auto* const outerBy = state.firedBy;
            state.firing = a_weapon;
            state.firedBy = a_firedBy;
            const bool fired = g_fireInstantHit.thiscall<bool>(a_weapon, a_firedBy, a_origin, a_muzzle, a_target, a_targetPos, a_driveBy, a_arg6, a_arg7);
            state.firing = outerWeapon;
            state.firedBy = outerBy;
            return fired;
        }

        // How much of San Andreas's spread a shot keeps: none for the first of a burst, more the longer the fire
        // button is held (CPlayerData::m_fAttackButtonCounter, which also grows the crosshair), all of it in the end.
        float SpreadKept(CPed* a_ped)
        {
            auto* data = static_cast<CPlayerPed*>(a_ped)->m_pPlayerData;
            const float held = data && std::isfinite(data->m_fAttackButtonCounter) ? (std::max)(data->m_fAttackButtonCounter, 0.0f) * 0.5f : 1.0f;
            return held / (held + 1.0f);
        }

        // Each line FireInstantHit tests, with San Andreas's spread in it: a bullet, or a shotgun's shot (one line;
        // Skyrim fans it out into the pellets). A bullet's spread is eased in: the first of a burst goes down the
        // crosshair (San Andreas's line without its spread runs from the camera's aim through the same start).
        void __cdecl HandleGunShotHook(const CVector* a_start, const CVector* a_end)
        {
            if (state.firing && IsCj(state.firedBy) && a_start && a_end) {
                const auto type = state.firing->m_eWeaponType;
                const bool shotgun = type == WEAPONTYPE_SHOTGUN || type == WEAPONTYPE_SAWNOFF || type == WEAPONTYPE_SPAS12;
                CVector    along = *a_end - *a_start;
                const float length = along.Magnitude();
                if (!shotgun && length > 1e-3f && TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode == MODE_AIMWEAPON) {
                    CVector source, target;
                    TheCamera.Find3rdPersonCamTargetVector(length, *a_start, &source, &target);
                    CVector aim = target - source;
                    if (aim.Magnitude() > 1e-3f) {
                        aim.Normalize();
                        CVector line = along;
                        line.Normalize();
                        const float kept = SpreadKept(static_cast<CPed*>(state.firedBy));
                        along = (aim + (line - aim) * kept) * length;
                    }
                }
                SendBullet(static_cast<CPed*>(state.firedBy), type, *a_start, along, shotgun ? Protocol::kShotSpread : 0);
            }
            g_handleGunShot(a_start, a_end);
        }

        void __fastcall DoBulletImpactHook(CWeapon* a_weapon, void*, CEntity* a_firedBy, CEntity* a_victim, CVector* a_start, CVector* a_end, CColPoint* a_point,
            int a_incremental)
        {
            // FireInstantHit's lines went already (HandleGunShotHook); this is the rest: the first-person rifles.
            if (!state.firing && IsCj(a_firedBy) && a_start && a_end) {
                const auto camera = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode;
                const bool firstPerson = Weapons::ScopeOf(static_cast<std::uint8_t>(camera)) == Weapons::Scope::Rifle;
                SendBullet(static_cast<CPed*>(a_firedBy), a_weapon->m_eWeaponType, *a_start, *a_end - *a_start,
                    firstPerson ? Protocol::kShotFirstPerson : std::uint8_t{ 0 });
            }
            g_doBulletImpact.thiscall(a_weapon, a_firedBy, a_victim, a_start, a_end, a_point, a_incremental);
        }

        bool __fastcall FireSniperHook(CWeapon* a_weapon, void*, CPed* a_shooter, CEntity* a_victim, CVector* a_target)
        {
            const bool fired = g_fireSniper.thiscall<bool>(a_weapon, a_shooter, a_victim, a_target);
            if (fired && IsCj(a_shooter)) {
                const auto& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
                SendBullet(a_shooter, a_weapon->m_eWeaponType, cam.m_vecSource, cam.m_vecFront, Protocol::kShotFirstPerson);
            }
            return fired;
        }

        void SendBlow(CTaskSimpleFight* a_task, CPed* a_ped, const CVector& a_at)
        {
            const int combo = a_task->m_nComboSet - kComboFirst;
            const int move = a_task->m_nCurrentMove;
            if (combo < 0 || combo >= kComboCount || move < 0 || move >= kMoveCount) {
                return;
            }
            const auto* info = reinterpret_cast<const std::uint8_t*>(kComboData + combo * kComboSize);
            float       radius = 0.0f;
            std::memcpy(&radius, info + kComboRadius + move * 4, sizeof(radius));
            const std::uint8_t damage = info[kComboDamage + move];
            if (state.loggedCombos.insert(combo).second) {
                std::string bytes;
                for (int i = 0x50; i < 0x5C; ++i) {
                    bytes += std::format(" {:02X}", info[i]);
                }
                Log(std::format("shots: melee combo {} move {}: radius {:.2f} m, damage {} (bytes 0x50..0x5B:{})", combo + kComboFirst, move, radius, damage, bytes));
            }
            // A new blow: another task, another animation, another move. The same one over several frames
            // (the chainsaw's cut, a slow swing) keeps its count.
            // (or after a pause: the same task and animation can come back at the same addresses).
            const std::uint64_t now = ::GetTickCount64();
            const bool          paused = now - state.lastBlowMs > 300;
            state.lastBlowMs = now;
            if (paused || a_task != state.lastTask || a_task->m_pAnim != state.lastAnim || move != state.lastMove) {
                state.strike++;
                state.lastTask = a_task;
                state.lastAnim = a_task->m_pAnim;
                state.lastMove = move;
            }
            const auto& weapon = a_ped->m_aWeapons[a_ped->m_nSelectedWepSlot];
            const auto  type = Weapons::IsMelee(static_cast<std::uint8_t>(weapon.m_eWeaponType)) ? weapon.m_eWeaponType : WEAPONTYPE_UNARMED;
            const auto& forward = a_ped->GetForward();
            Protocol::ShotEvent shot{};
            shot.weaponType = static_cast<std::uint8_t>(type);
            shot.kind = Protocol::kShotMelee;
            shot.strike = state.strike;
            shot.damage = static_cast<float>(damage);
            shot.direction[0] = forward.x;
            shot.direction[1] = forward.y;
            shot.direction[2] = forward.z;
            shot.range = (radius > 0.0f && radius < 5.0f ? radius : 0.5f) * Pose::kUnitsPerMeter;
            Send(shot, a_at, "blow");
        }

        void __fastcall FightStrikeHook(CTaskSimpleFight* a_task, void*, CPed* a_ped, CVector* a_at)
        {
            if (IsCj(a_ped) && a_at) {
                SendBlow(a_task, a_ped, *a_at);
            }
            g_fightStrike.thiscall(a_task, a_ped, a_at);
        }

        // The flamethrower's puff, aimed as FireAreaEffect aims it: through the crosshair while the mouse turns the
        // camera behind him, else ahead of him at his look pitch. San Andreas's own (fire creeping over its
        // ground, which no one in Skyrim would see but would still burn him) is skipped for him; the spray can
        // and the extinguisher stay San Andreas's.
        bool __fastcall FireAreaEffectHook(CWeapon* a_weapon, void*, CEntity* a_firedBy, CVector* a_origin, CEntity* a_target, CVector* a_targetPos)
        {
            if (!IsCj(a_firedBy) || !a_origin || a_weapon->m_eWeaponType != WEAPONTYPE_FTHROWER || a_target || a_targetPos) {
                return g_fireAreaEffect.thiscall<bool>(a_weapon, a_firedBy, a_origin, a_target, a_targetPos);
            }
            auto*       ped = static_cast<CPed*>(a_firedBy);
            const auto  skill = static_cast<std::uint8_t>(ped->GetWeaponSkill(WEAPONTYPE_FTHROWER));
            const auto* info = CWeaponInfo::GetWeaponInfo(WEAPONTYPE_FTHROWER, skill);
            const float range = info && info->m_fWeaponRange > 0.0f ? info->m_fWeaponRange : 5.0f;
            const auto  mode = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode;
            CVector     dir;
            if (mode == MODE_AIMWEAPON || mode == MODE_FOLLOWPED) {
                CVector source, target;
                TheCamera.Find3rdPersonCamTargetVector(range, *a_origin, &source, &target);
                dir = target - source;
            } else {
                const float heading = ped->GetHeading();
                const auto* data = static_cast<CPlayerPed*>(ped)->m_pPlayerData;
                dir = CVector(-std::sin(heading), std::cos(heading), data ? -std::tan(data->m_fLookPitch) : 0.0f);
            }
            const float length = dir.Magnitude();
            if (length > 1e-4f) {
                dir /= length;
                Protocol::ShotEvent shot{};
                shot.weaponType = static_cast<std::uint8_t>(WEAPONTYPE_FTHROWER);
                shot.kind = Protocol::kShotFlame;
                shot.skill = skill;
                shot.damage = info ? static_cast<float>(info->m_nDamage) : 0.0f;
                shot.direction[0] = dir.x;
                shot.direction[1] = dir.y;
                shot.direction[2] = dir.z;
                shot.range = range * Pose::kUnitsPerMeter;
                Send(shot, *a_origin, "flame");
            }
            return true;
        }

        // Points the call at a_site to a_detour; returns what it called before, or nullptr when the site is
        // not a call (then it is left alone).
        template <class T>
        T RedirectCall(std::uintptr_t a_site, T a_detour)
        {
            auto* code = reinterpret_cast<std::uint8_t*>(a_site);
            if (code[0] != 0xE8) {
                Log(std::format("shots: 0x{:X} is not a call, not redirected", a_site));
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

        SafetyHookInline Hook(std::uintptr_t a_address, void* a_detour, const char* a_name)
        {
            auto hook = safetyhook::create_inline(reinterpret_cast<void*>(a_address), a_detour);
            Log(hook ? std::format("shots: {} hooked", a_name) : std::format("shots: could not hook {}, those shots won't reach Skyrim", a_name));
            return hook;
        }
    }

    void Install()
    {
        g_fireInstantHit = Hook(kFireInstantHit, reinterpret_cast<void*>(&FireInstantHitHook), "CWeapon::FireInstantHit");
        int redirected = 0;
        for (const auto site : kCallsHandleGunShot) {
            if (auto before = RedirectCall(site, &HandleGunShotHook)) {
                g_handleGunShot = before;
                redirected++;
            }
        }
        Log(std::format("shots: {} of FireInstantHit's {} lines redirected", redirected, std::size(kCallsHandleGunShot)));
        g_doBulletImpact = Hook(kDoBulletImpact, reinterpret_cast<void*>(&DoBulletImpactHook), "CWeapon::DoBulletImpact");
        g_fireSniper = Hook(kFireSniper, reinterpret_cast<void*>(&FireSniperHook), "CWeapon::FireSniper");
        g_fightStrike = Hook(kFightStrike, reinterpret_cast<void*>(&FightStrikeHook), "CTaskSimpleFight::FightStrike");
        g_fireAreaEffect = Hook(kFireAreaEffect, reinterpret_cast<void*>(&FireAreaEffectHook), "CWeapon::FireAreaEffect");
    }

    void SetFrame(const CVector& a_spawnFeet, unsigned a_handover)
    {
        // San Andreas aims a third-person shot through its crosshair, dividing by the screen's aspect ratio
        // (CCamera::Find3rdPersonCamTargetVector), which its minimized window never sets: every bullet's
        // line came out NaN. 4:3, the shape San Andreas's camera field of view is given for.
        if (!(CDraw::ms_fAspectRatio > 0.1f && CDraw::ms_fAspectRatio < 10.0f)) {
            Log(std::format("shots: San Andreas's aspect ratio was {}, set to 4:3 so its aim works", CDraw::ms_fAspectRatio));
            CDraw::ms_fAspectRatio = 4.0f / 3.0f;
        }
        state.spawnFeet = a_spawnFeet;
        state.handover = a_handover;
        state.framed = true;
    }

    bool Frame(CVector& a_spawnFeet, unsigned& a_handover)
    {
        a_spawnFeet = state.spawnFeet;
        a_handover = state.handover;
        return state.framed;
    }
}
