#include "cj_melee.h"

#include "log.h"
#include "melee.h"
#include "pose.h"
#include "vehicles.h"
#include "weapons.h"

#include <plugin.h>

#include <CPlayerPed.h>
#include <CTimer.h>
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

        SafetyHookInline g_processPed;

        struct State
        {
            bool          locked = false;  // turn CJ to the target
            CVector       target;          // its middle, San Andreas's world
            std::uint32_t targetId = 0;
            int           logged = 0;
            bool          touching = false;
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

        // The player's on-foot control: as San Andreas has it, then, locked on and standing to fight (not
        // running: then San Andreas's own PlayerControlZelda steers him), turned toward the target at the lock's
        // rate. Both headings are set, so San Andreas doesn't turn him back to the camera before his blow lands.
        bool __fastcall ProcessPedHook(void* a_task, void*, CPed* a_ped)
        {
            const bool moving = a_ped && a_ped->m_nMoveState >= PEDMOVE_WALK;
            const bool done = g_processPed.thiscall<bool>(a_task, a_ped);
            if (state.locked && !moving && IsCj(a_ped) && !a_ped->bInVehicle) {
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
        const bool     wasLocked = state.locked;
        const auto     wasTarget = state.targetId;
        state.locked = false;
        state.targetId = 0;
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
            }

            // The lock-on: Skyrim flags the target while the right mouse button is held, or for a swing's combo;
            // San Andreas still has to hold a melee weapon (Skyrim's flag lags a frame behind a weapon switch).
            const auto type = static_cast<std::uint8_t>(a_ped->m_aWeapons[a_ped->m_nSelectedWepSlot].m_eWeaponType);
            if (target && Weapons::IsMelee(type)) {
                const auto capsule = CapsuleOf(*target, spawnFeet);
                state.locked = true;
                state.targetId = target->id;
                state.target = Cv(Body::Scale(Body::Add(capsule.a, capsule.b), 0.5f));
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
    }
}
