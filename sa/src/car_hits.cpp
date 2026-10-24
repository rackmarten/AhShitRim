#include "car_hits.h"

#include "log.h"
#include "pose.h"
#include "run_over.h"
#include "scenes.h"
#include "vehicle_hits.h"
#include "vehicles.h"

#include <plugin.h>

#include <CModelInfo.h>
#include <CPools.h>
#include <CPlayerPed.h>
#include <CTimer.h>
#include <CVehicle.h>
#include <CVehicleModelInfo.h>
#include <CWorld.h>
#include <common.h>

#include <cmath>
#include <format>

namespace AhShitRim::CarHits
{
    namespace
    {
        constexpr float kJumpMax = 10.0f;  // metres: a vehicle that moved more in a frame was put somewhere (an origin move), not driven
        constexpr int   kLoggedHits = 12;   // the first hits are logged in full

        struct State
        {
            int               vehicle = -1;  // the CPools handle of the vehicle last frame's position belongs to
            RunOver::Frame    last;
            RunOver::HitMemory hits;
            RunOver::SpeechGap speech;
            int               logged = 0;
            unsigned          sent = 0, dropped = 0;
            int               loggedTaken = 0;  // Skyrim's hits on the vehicle (v24), the first logged in full
            unsigned          taken = 0, warded = 0;
        } state;

        Pose::Vec3 Vec(const CVector& a_v) { return { a_v.x, a_v.y, a_v.z }; }
        CVector    Cv(const Pose::Vec3& a_v) { return CVector(a_v.x, a_v.y, a_v.z); }

        // CJ's crash line, one every RunOver::kSpeechGap at most, whether he hit someone or someone hit his vehicle.
        void SayCrash(CPlayerPed* a_ped, double a_now, const char* a_why)
        {
            if (state.speech.Ready(a_now)) {
                Scenes::SayCrash(a_ped, a_why);
            }
        }

        // What San Andreas's own proofs keep off the vehicle (a mission's, the Rhino's).
        bool Warded(CVehicle* a_vehicle, VehicleHits::Attack a_attack)
        {
            using enum VehicleHits::Attack;
            const CVehicle& proof = *a_vehicle;
            switch (a_attack) {
            case Arrow:
            case Bolt: return proof.bBulletProof;
            case Melee:
            case Bash:
            case Unarmed:
            case Claws: return proof.bMeleeProof;
            case Spell:
            case Flames: return proof.bFireProof;
            case Explosion: return proof.bExplosionProof;
            default: return false;
            }
        }

        RunOver::Frame FrameOf(CVehicle* a_vehicle)
        {
            return { Vec(a_vehicle->GetPosition()), Vec(a_vehicle->GetRight()), Vec(a_vehicle->GetForward()), Vec(a_vehicle->GetUp()) };
        }

        double Now() { return CTimer::m_snTimeInMilliseconds / 1000.0; }  // game time: a pause doesn't count

        // A proxy in San Andreas's world: its capsule and velocity from Skyrim's units relative to the anchor.
        RunOver::Capsule CapsuleOf(const Protocol::ActorProxy& a_proxy, const Pose::Vec3& a_spawnFeet)
        {
            const auto at = [&](const float (&a_p)[3]) {
                return Body::Add(a_spawnFeet, Body::Scale({ a_p[0], a_p[1], a_p[2] }, Pose::kMetersPerUnit));
            };
            return { at(a_proxy.a), at(a_proxy.b), a_proxy.radius * Pose::kMetersPerUnit };
        }

        void Send(CVehicle* a_vehicle, const Protocol::ActorProxy& a_proxy, const RunOver::Touch& a_touch, const RunOver::Response& a_hit,
            const Pose::Vec3& a_spawnFeet, unsigned a_handover, bool a_heavy)
        {
            Protocol::ImpactEvent impact{};
            impact.handover = a_handover;
            impact.id = a_proxy.id;
            impact.speed = a_hit.closing;
            impact.flags = static_cast<std::uint8_t>((a_heavy ? Protocol::kImpactHeavy : 0) | (a_vehicle->m_nVehicleClass == VEHICLE_BIKE ? Protocol::kImpactBike : 0));
            impact.vehicleModel = static_cast<std::uint16_t>(a_vehicle->m_nModelIndex);
            const auto at = Body::Scale(Body::Sub(a_touch.point, a_spawnFeet), Pose::kUnitsPerMeter);
            impact.at[0] = at.x;
            impact.at[1] = at.y;
            impact.at[2] = at.z;
            if (!a_heavy) {
                impact.push[0] = a_hit.actorVelocity.x * Pose::kUnitsPerMeter;
                impact.push[1] = a_hit.actorVelocity.y * Pose::kUnitsPerMeter;
                impact.push[2] = a_hit.actorVelocity.z * Pose::kUnitsPerMeter;
            }
            impact.damage = RunOver::ImpactDamage(a_hit.closing, a_vehicle->m_fMass);
            if (GetLink().Send(Protocol::MessageType::Impact, &impact, sizeof(impact))) {
                state.sent++;
            } else {
                state.dropped++;
            }
            if (state.logged < kLoggedHits) {
                state.logged++;
                Log(std::format("car hits: hit {:08X}{} at {:.1f} m/s (mass {:.0f} kg against {:.0f}), damage {:.0f}{}", a_proxy.id, a_heavy ? " (heavy)" : "",
                    a_hit.closing, a_vehicle->m_fMass, a_proxy.mass, impact.damage, state.dropped ? std::format(", {} dropped so far", state.dropped) : ""));
            }
        }
    }

    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, bool a_active)
    {
        CVehicle* vehicle = a_active && !CTimer::m_UserPause && !CTimer::m_CodePause ? Vehicles::Driven(a_ped) : nullptr;
        auto*     info = vehicle ? CModelInfo::GetModelInfo(vehicle->m_nModelIndex) : nullptr;
        if (!vehicle || !info || !info->m_pColModel) {
            state.vehicle = -1;
            return;
        }
        const RunOver::Frame now = FrameOf(vehicle);
        const int            ref = CPools::GetVehicleRef(vehicle);
        RunOver::Frame       from = state.vehicle == ref ? state.last : now;
        if (!(RunOver::Length(Body::Sub(now.pos, from.pos)) <= kJumpMax)) {
            from = now;
        }
        state.vehicle = ref;
        state.last = now;

        Protocol::ProxyState proxies{};
        if (!GetLink().ReadProxies(proxies) || proxies.handover != a_handover) {
            return;
        }
        const auto&       bounds = info->m_pColModel->m_boundBox;
        const RunOver::Box box{ Vec(bounds.m_vecMin), Vec(bounds.m_vecMax) };
        const Pose::Vec3  spawnFeet = Vec(a_spawn - CVector(0.0f, 0.0f, a_ped->GetDistanceFromCentreOfMassToBaseOfModel() + a_lift));
        const double      time = Now();
        const auto        count = (std::min)(proxies.count, Protocol::kProxyMax);
        for (std::uint32_t i = 0; i < count; i++) {
            const auto& proxy = proxies.proxies[i];
            const auto  capsule = CapsuleOf(proxy, spawnFeet);
            const auto  touch = RunOver::Sweep(box, from, state.last, capsule);
            if (!touch) {
                continue;
            }
            const bool       heavy = (proxy.flags & Protocol::kProxyHeavy) != 0;
            const Pose::Vec3 velocity = Body::Scale(Vec(vehicle->m_vecMoveSpeed), 50.0f);  // San Andreas's speeds are metres a 1/50 s step
            const Pose::Vec3 actor = Body::Scale({ proxy.velocity[0], proxy.velocity[1], proxy.velocity[2] }, Pose::kMetersPerUnit);
            const auto       hit = RunOver::Respond(vehicle->m_fMass, velocity, actor, proxy.mass, heavy, touch->normal);
            if (!(hit.closing > 0.0f)) {
                continue;
            }
            const bool hard = hit.closing >= RunOver::kMinImpact;
            const bool lands = hard && state.hits.Lands(proxy.id, time);
            if (!heavy && !lands) {
                continue;  // a nudge, or someone it is still going through: the vehicle drives on
            }
            // The push, as an impulse where they touched (it turns the vehicle too). San Andreas's ApplyForce takes
            // the impulse in kilograms times metres a step and the point relative to the vehicle's position.
            const Pose::Vec3 impulse = Body::Scale(hit.vehicleChange, vehicle->m_fMass / 50.0f);
            vehicle->ApplyForce(Cv(impulse), Cv(Body::Sub(touch->point, now.pos)), true);
            if (heavy) {
                // A wall: back where it first touched this frame, so it doesn't end up inside a mammoth.
                if (touch->along < 1.0f) {
                    const CVector back = Cv(Body::Add(from.pos, Body::Scale(Body::Sub(now.pos, from.pos), touch->along)));
                    CWorld::Remove(vehicle);
                    vehicle->SetPosn(back);
                    CWorld::Add(vehicle);
                    state.last.pos = Vec(back);
                }
                if (lands) {
                    vehicle->m_fHealth = (std::max)(0.0f, vehicle->m_fHealth - RunOver::HeavyCrashDamage(hit.closing));
                }
            }
            if (lands) {
                Send(vehicle, proxy, *touch, hit, spawnFeet, a_handover, heavy);
                SayCrash(a_ped, time, "hit someone with his vehicle");
            }
        }
    }

    void TakeHit(CPlayerPed* a_ped, unsigned char a_attack, float a_damage, const CVector& a_at)
    {
        CVehicle*  vehicle = a_ped ? Vehicles::Driven(a_ped) : nullptr;
        const auto attack = static_cast<VehicleHits::Attack>(a_attack);
        if (!vehicle || vehicle->m_nStatus == STATUS_WRECKED || !std::isfinite(a_damage) || a_damage <= 0.0f) {
            return;  // he got out (or it blew up) before the hit came
        }
        if (!vehicle->bCanBeDamaged || Warded(vehicle, attack)) {
            if (state.warded++ == 0) {
                Log(std::format("car hits: Skyrim's {} on vehicle {} does nothing: San Andreas has it proof against it", VehicleHits::NameOf(attack), vehicle->m_nModelIndex));
            }
            return;
        }
        const float before = vehicle->m_fHealth;
        const float damage = (std::min)(a_damage, VehicleHits::kDamageMax) * (vehicle->bTakeLessDamage ? 0.25f : 1.0f);  // a stronger one, as San Andreas has it
        vehicle->m_fHealth = (std::max)(0.0f, before - damage);
        state.taken++;
        if (state.loggedTaken < kLoggedHits) {
            state.loggedTaken++;
            const CVector& at = vehicle->GetPosition();
            Log(std::format("car hits: Skyrim's {} hit vehicle {} {:.1f} m from its middle: health {:.0f} -> {:.0f}", VehicleHits::NameOf(attack), vehicle->m_nModelIndex,
                (a_at - at).Magnitude(), before, vehicle->m_fHealth));
        }
        SayCrash(a_ped, Now(), "has his vehicle hit");
    }
}
