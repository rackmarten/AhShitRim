#include "cj_projectiles.h"

#include "body.h"
#include "cj_shots.h"
#include "cj_weapons.h"
#include "explosives.h"
#include "log.h"
#include "model_sender.h"
#include "pose.h"
#include "skin_model.h"
#include "weapons.h"

#include <plugin.h>

#include <CAudioEngine.h>
#include <CCamera.h>
#include <CFireManager.h>
#include <CProjectile.h>
#include <CProjectileInfo.h>
#include <CTimer.h>
#include <common.h>

#include <safetyhook.hpp>

#include <Windows.h>

#include <array>
#include <deque>
#include <vector>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <set>

namespace AhShitRim::CjProjectiles
{
    namespace
    {
        // ---- San Andreas 1.0 US addresses (gta-reversed names) ----

        constexpr std::uintptr_t kAddProjectile = 0x737C80;  // CProjectileInfo::AddProjectile (cdecl)
        constexpr std::uintptr_t kRemoveNotAdd = 0x737C00;   // CProjectileInfo::RemoveNotAdd (cdecl): one with no room to go, blown up where it is
        constexpr std::uintptr_t kUpdate = 0x738B20;         // CProjectileInfo::Update (cdecl): flies them, blows them up
        constexpr std::uintptr_t kRemoveDetonator = 0x738860;  // CProjectileInfo::RemoveDetonatorProjectiles (cdecl): the detonator, from CWorld::UseDetonator
        constexpr std::uint32_t  kSlots = 32;                // gaProjectileInfo, CProjectileInfo::ms_apProjectile
        constexpr std::uintptr_t kExplosionAudio = 0xC888D0;     // CExplosion::m_ExplosionAudioEntity
        constexpr std::uintptr_t kAddExplosionAudio = 0x4DCBE0;  // CAEExplosionAudioEntity::AddAudioEvent (thiscall)
        constexpr int            kAeExplosion = 0x81;            // AE_EXPLOSION
        constexpr std::uintptr_t kReportObjectDestruction = 0x506ED0;  // CAudioEngine::ReportObjectDestruction (thiscall): a Molotov's bottle smashing
        constexpr std::uint32_t  kStandInGrace = 2000;  // ms a rocket's stand-in outlives its fuse if Skyrim never says it went off

        SafetyHookInline g_addProjectile;
        SafetyHookInline g_removeNotAdd;
        SafetyHookInline g_update;
        SafetyHookInline g_removeDetonator;

        // A rocket Skyrim flies keeps a stand-in in San Andreas's world, without collision, flown along the same
        // straight line, so the sound San Andreas tied to it (the launch and its roar) follows it and stops when
        // it goes off. A Molotov keeps one too, left where it was thrown and put where it burst at the end, to
        // smash there with San Andreas's own bottle sound. A satchel keeps one where it was thrown until the detonator
        // or Skyrim says it's gone, so San Andreas knows CJ has satchels out (the detonator stays his).
        struct StandIn
        {
            std::uint32_t id = 0;
            std::uint32_t slot = 0;
            CProjectile*  projectile = nullptr;
            CVector       position;
            CVector       velocity;  // metres a second
            std::uint32_t untilMs = 0;
            unsigned      handover = 0;
            bool          satchel = false;  // kept through re-anchors: Skyrim's stays where it is
        };

        struct Model
        {
            std::uint32_t                      id = 0;
            std::uint32_t                      partCount = 0;
            std::array<Protocol::BodyBone, Protocol::kProjectilePartMax> parts{};
            bool                               failed = false;
        };

        struct State
        {
            std::map<int, Model> models;  // by San Andreas's model index
            std::uint32_t        builds = 0;
            std::set<int>        logged;  // projectile types logged once
            std::uint32_t        nextId = 0;
            std::vector<StandIn> standIns;
            std::deque<Protocol::EffectEvent> effects;
            std::set<int>        loggedEffects;
        } state;

        bool IsCj(const void* a_entity) { return a_entity && a_entity == FindPlayerPed(0); }

        Body::RwRows ToRows(const RwMatrix& a_m)
        {
            return { { a_m.right.x, a_m.right.y, a_m.right.z }, { a_m.up.x, a_m.up.y, a_m.up.z }, { a_m.at.x, a_m.at.y, a_m.at.z },
                { a_m.pos.x, a_m.pos.y, a_m.pos.z } };
        }

        // Where a part sits on the projectile: its frame's matrices up to the instance's own frame.
        Body::RwRows PartOnObject(RpAtomic* a_atomic, RwFrame* a_root)
        {
            Body::RwRows rows;
            for (RwFrame* frame = RpAtomicGetFrame(a_atomic); frame && frame != a_root; frame = RwFrameGetParent(frame)) {
                rows = Body::Then(rows, ToRows(*RwFrameGetMatrix(frame)));
            }
            return rows;
        }

        // The model for a projectile's model index, built and sent the first time one is seen. Each kind has a
        // ModelSender key of its own (by projectile type, 16 to 20); a model keeps its id for good.
        const Model* ModelFor(CProjectile* a_projectile, std::uint8_t a_type)
        {
            const int index = a_projectile->m_nModelIndex;
            auto&     model = state.models[index];
            if (model.id || model.failed) {
                return model.id ? &model : nullptr;
            }
            RwObject*              object = a_projectile->m_pRwObject;
            std::vector<RpAtomic*> parts;
            const auto             name = Explosives::WeaponOf(a_type);
            auto                   blob = CjWeapons::BuildModel(object, name, parts);
            if (blob.empty()) {
                model.failed = true;
                Log(std::format("projectiles: model {} of projectile {} has nothing to draw", index, a_type));
                return nullptr;
            }
            RwFrame* root = object ? static_cast<RwFrame*>(object->parent) : nullptr;
            model.partCount = static_cast<std::uint32_t>((std::min)(parts.size(), static_cast<std::size_t>(Protocol::kProjectilePartMax)));
            for (std::uint32_t i = 0; i < model.partCount; ++i) {
                model.parts[i] = Body::ToBone(PartOnObject(parts[i], root), {});
            }
            if (parts.size() > Protocol::kProjectilePartMax) {
                Log(std::format("projectiles: model {} has {} parts, the first {} are drawn", index, parts.size(), Protocol::kProjectilePartMax));
            }
            model.id = (static_cast<std::uint32_t>(::GetTickCount64()) << 4 | (++state.builds & 0xF)) | 1;
            ModelSender::Set(ModelSender::kProjectileKey + (a_type - Weapons::Grenade), model.id, SkinModel::Chunks(model.id, blob),
                std::format("projectiles: model {} for projectile {}", index, a_type));
            return &model;
        }

        bool Frame(Protocol::ProjectileEvent& a_event, const CVector& a_at, CVector& a_spawnFeet)
        {
            unsigned handover = 0;
            if (!CjShots::Frame(a_spawnFeet, handover)) {
                return false;
            }
            a_event.handover = handover;
            const CVector at = a_at - a_spawnFeet;
            a_event.origin[0] = at.x * Pose::kUnitsPerMeter;
            a_event.origin[1] = at.y * Pose::kUnitsPerMeter;
            a_event.origin[2] = at.z * Pose::kUnitsPerMeter;
            a_event.rotation[3] = 1.0f;
            return true;
        }

        void Send(const Protocol::ProjectileEvent& a_event, const char* a_what)
        {
            for (float v : { a_event.origin[0], a_event.origin[1], a_event.origin[2], a_event.velocity[0], a_event.velocity[1], a_event.velocity[2] }) {
                if (!std::isfinite(v)) {
                    return;
                }
            }
            const bool sent = GetLink().Send(Protocol::MessageType::Projectile, &a_event, sizeof(a_event));
            if (state.logged.insert(a_event.weaponType * 2 + ((a_event.flags & Protocol::kProjectileBurst) ? 1 : 0)).second) {
                Log(std::format("projectiles: the first {} of type {} {}: from ({:.0f},{:.0f},{:.0f}) at ({:.0f},{:.0f},{:.0f}) a second, fuse {} ms, model {:08X} of {} parts",
                    a_what, a_event.weaponType, sent ? "sent" : "dropped (ring full or link down)", a_event.origin[0], a_event.origin[1], a_event.origin[2],
                    a_event.velocity[0], a_event.velocity[1], a_event.velocity[2], a_event.fuseMs, a_event.model, a_event.partCount));
            }
        }

        bool KeepStandIn(std::uint32_t a_slot, CProjectile* a_projectile, const Protocol::ProjectileEvent& a_event, Explosives::Kind a_kind)
        {
            CVector  feet;
            unsigned handover = 0;
            CjShots::Frame(feet, handover);
            const auto& m = a_projectile->GetMatrix();
            const auto  v = Explosives::LaunchVelocity(Explosives::Kind::Rocket, { a_event.velocity[0], a_event.velocity[1], a_event.velocity[2] },
                 { m.up.x, m.up.y, m.up.z });
            StandIn s;
            s.id = a_event.id;
            s.slot = a_slot;
            s.projectile = a_projectile;
            s.position = a_projectile->GetPosition();
            s.velocity = a_kind == Explosives::Kind::Rocket ? CVector(v.x, v.y, v.z) / Pose::kUnitsPerMeter : CVector(0.0f, 0.0f, 0.0f);
            s.satchel = a_kind == Explosives::Kind::Satchel;
            s.untilMs = s.satchel ? 0xFFFFFFFFu - 60000u
                                  : CTimer::m_snTimeInMilliseconds + static_cast<std::uint32_t>(Explosives::FuseSeconds(a_kind, a_event.fuseMs) * 1000.0f) + kStandInGrace;
            s.handover = handover;
            a_projectile->bUsesCollision = false;
            a_projectile->bApplyGravity = false;
            a_projectile->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
            a_projectile->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
            gaProjectileInfo[a_slot].m_nDestroyTime = s.satchel ? 0x7FFFFFFF : static_cast<int>(s.untilMs + 60000);
            state.standIns.push_back(s);
            return true;
        }

        void DropStandIn(const StandIn& a_s)
        {
            if (CProjectileInfo::ms_apProjectile[a_s.slot] == a_s.projectile) {
                CProjectileInfo::RemoveIfThisIsAProjectile(a_s.projectile);
            }
        }

        // San Andreas would fly and blow up the stand-ins too: they sit out its update.
        void __cdecl UpdateHook()
        {
            for (const auto& s : state.standIns) {
                gaProjectileInfo[s.slot].m_bActive = false;
            }
            g_update.call();
            for (const auto& s : state.standIns) {
                if (CProjectileInfo::ms_apProjectile[s.slot] == s.projectile) {
                    gaProjectileInfo[s.slot].m_bActive = true;
                }
            }
        }

        void Effect(CPlayerPed* a_ped, const Protocol::EffectEvent& a_effect, const CVector& a_spawnFeet)
        {
            const CVector at = a_spawnFeet + CVector(a_effect.at[0], a_effect.at[1], a_effect.at[2]) / Pose::kUnitsPerMeter;
            if (state.loggedEffects.insert(a_effect.kind).second) {
                Log(std::format("projectiles: the first effect {} from Skyrim at ({:.1f},{:.1f},{:.1f})", a_effect.kind, at.x, at.y, at.z));
            }
            switch (static_cast<Protocol::EffectKind>(a_effect.kind)) {
            case Protocol::EffectKind::ProjectileGone:
                for (auto it = state.standIns.begin(); it != state.standIns.end(); ++it) {
                    if (it->id == a_effect.projectile) {
                        DropStandIn(*it);
                        state.standIns.erase(it);
                        break;
                    }
                }
                break;
            case Protocol::EffectKind::Explosion:
                {
                    CVector    where = at;
                    const auto add = reinterpret_cast<void(__thiscall*)(void*, int, CVector*, float)>(kAddExplosionAudio);
                    const bool molotov = a_effect.weaponType == Weapons::Molotov;
                    add(reinterpret_cast<void*>(kExplosionAudio), kAeExplosion, &where, molotov ? -36.0f : 0.0f);  // as San Andreas's own Molotov: its fire is the sound
                    if (molotov) {
                        // The bottle smashes as San Andreas's own does: its stand-in, put where it burst, destroyed.
                        for (const auto& s : state.standIns) {
                            if (s.id == a_effect.projectile && CProjectileInfo::ms_apProjectile[s.slot] == s.projectile) {
                                s.projectile->SetPosition(where);
                                const auto smash = reinterpret_cast<void(__thiscall*)(void*, CEntity*)>(kReportObjectDestruction);
                                smash(&AudioEngine, s.projectile);
                                break;
                            }
                        }
                    }
                    // San Andreas's own shake (CExplosion::AddExplosion's), weaker the farther off; Skyrim's camera follows it.
                    TheCamera.CamShake(molotov ? 0.2f : 0.6f, where.x, where.y, where.z);
                    break;
                }
            case Protocol::EffectKind::Fire:
                gFireManager.StartFire(at, 0.8f, 0, nullptr, static_cast<unsigned int>((std::max)(a_effect.seconds, 0.5f) * 1000.0f), 0, 0);
                break;
            case Protocol::EffectKind::BurnCj:
                if (a_ped && !a_ped->m_pFire) {
                    gFireManager.StartFire(a_ped, nullptr, 0.8f, 1, 7000, 0);
                }
                break;
            default: break;
            }
        }

        // San Andreas just made it in slot a_slot: read it, send it, and take it out of San Andreas's world (no blast).
        void Take(std::uint32_t a_slot, std::uint8_t a_type)
        {
            CProjectile*     projectile = CProjectileInfo::ms_apProjectile[a_slot];
            CProjectileInfo& info = gaProjectileInfo[a_slot];
            if (!projectile) {
                info.m_bActive = false;  // nothing to fly: San Andreas's slot must not go off by itself
                return;
            }
            Protocol::ProjectileEvent event{};
            event.weaponType = static_cast<std::uint8_t>(info.m_nWeaponType);
            CVector spawnFeet;
            if (Frame(event, projectile->GetPosition(), spawnFeet)) {
                const CVector& speed = projectile->m_vecMoveSpeed;  // metres a step, 50 steps a second
                event.velocity[0] = speed.x * 50.0f * Pose::kUnitsPerMeter;
                event.velocity[1] = speed.y * 50.0f * Pose::kUnitsPerMeter;
                event.velocity[2] = speed.z * 50.0f * Pose::kUnitsPerMeter;
                const auto& m = projectile->GetMatrix();
                const auto  q = Body::FromAxes({ m.right.x, m.right.y, m.right.z }, { m.up.x, m.up.y, m.up.z });
                event.rotation[0] = q.x;
                event.rotation[1] = q.y;
                event.rotation[2] = q.z;
                event.rotation[3] = q.w;
                const int left = info.m_nDestroyTime - static_cast<int>(CTimer::m_snTimeInMilliseconds);
                event.fuseMs = left > 0 ? static_cast<std::uint32_t>(left) : 0;
                if (const auto* model = ModelFor(projectile, a_type)) {
                    event.model = model->id;
                    event.partCount = static_cast<std::uint8_t>(model->partCount);
                    for (std::uint32_t i = 0; i < model->partCount; ++i) {
                        event.parts[i] = model->parts[i];
                    }
                }
                event.id = ++state.nextId;
                Send(event, "launch");
                const auto kind = Explosives::KindOf(a_type);
                if ((kind == Explosives::Kind::Rocket || kind == Explosives::Kind::Molotov || kind == Explosives::Kind::Satchel) &&
                    KeepStandIn(a_slot, projectile, event, kind)) {
                    return;
                }
            }
            CProjectileInfo::RemoveIfThisIsAProjectile(projectile);
        }

        bool __cdecl AddProjectileHook(CEntity* a_creator, eWeaponType a_type, CVector a_at, float a_force, CVector* a_dir, CEntity* a_victim)
        {
            const auto type = static_cast<std::uint8_t>(a_type);
            if (!IsCj(a_creator) || Explosives::KindOf(type) == Explosives::Kind::None) {
                return g_addProjectile.call<bool>(a_creator, a_type, a_at, a_force, a_dir, a_victim);
            }
            bool was[kSlots];
            for (std::uint32_t i = 0; i < kSlots; ++i) {
                was[i] = gaProjectileInfo[i].m_bActive;
            }
            const bool added = g_addProjectile.call<bool>(a_creator, a_type, a_at, a_force, a_dir, a_victim);
            for (std::uint32_t i = 0; i < kSlots; ++i) {
                if (!was[i] && gaProjectileInfo[i].m_bActive && gaProjectileInfo[i].m_pCreator == a_creator) {
                    Take(i, type);
                }
            }
            return added;
        }

        void __cdecl RemoveNotAddHook(CEntity* a_creator, eWeaponType a_type, CVector a_at)
        {
            const auto type = static_cast<std::uint8_t>(a_type);
            if (!IsCj(a_creator) || Explosives::KindOf(type) == Explosives::Kind::None) {
                g_removeNotAdd.call(a_creator, a_type, a_at);
                return;
            }
            Protocol::ProjectileEvent event{};
            event.weaponType = type;
            event.flags = Protocol::kProjectileBurst;
            CVector spawnFeet;
            if (Frame(event, a_at, spawnFeet)) {
                Send(event, "burst");
            }
        }

        // The detonator: Skyrim blows CJ's satchels (they're its, stuck where they landed) and San Andreas drops their
        // stand-ins first, so its own blows only what isn't CJ's.
        void __cdecl RemoveDetonatorHook()
        {
            Protocol::ProjectileEvent event{};
            event.weaponType = Weapons::Detonator;
            CVector spawnFeet;
            if (auto* cj = FindPlayerPed(0); cj && Frame(event, cj->GetPosition(), spawnFeet)) {
                event.id = ++state.nextId;
                Send(event, "detonation");
            }
            for (auto it = state.standIns.begin(); it != state.standIns.end();) {
                if (it->satchel) {
                    DropStandIn(*it);
                    it = state.standIns.erase(it);
                } else {
                    ++it;
                }
            }
            g_removeDetonator.call();
        }

        SafetyHookInline Hook(std::uintptr_t a_address, void* a_detour, const char* a_name)
        {
            auto hook = safetyhook::create_inline(reinterpret_cast<void*>(a_address), a_detour);
            Log(hook ? std::format("projectiles: {} hooked", a_name) : std::format("projectiles: could not hook {}, CJ's explosives stay San Andreas's", a_name));
            return hook;
        }
    }

    void Install()
    {
        g_addProjectile = Hook(kAddProjectile, reinterpret_cast<void*>(&AddProjectileHook), "CProjectileInfo::AddProjectile");
        g_removeNotAdd = Hook(kRemoveNotAdd, reinterpret_cast<void*>(&RemoveNotAddHook), "CProjectileInfo::RemoveNotAdd");
        g_update = Hook(kUpdate, reinterpret_cast<void*>(&UpdateHook), "CProjectileInfo::Update");
        g_removeDetonator = Hook(kRemoveDetonator, reinterpret_cast<void*>(&RemoveDetonatorHook), "CProjectileInfo::RemoveDetonatorProjectiles");
    }

    void Queue(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        if (a_size != sizeof(Protocol::EffectEvent) || state.effects.size() >= 256) {
            return;
        }
        Protocol::EffectEvent effect{};
        std::memcpy(&effect, a_data, sizeof(effect));
        state.effects.push_back(effect);
    }

    void Update(CPlayerPed* a_ped, bool a_owned)
    {
        CVector    spawnFeet;
        unsigned   handover = 0;
        const bool framed = CjShots::Frame(spawnFeet, handover);
        while (!state.effects.empty()) {
            const auto effect = state.effects.front();
            state.effects.pop_front();
            if (framed && effect.handover == handover) {
                Effect(a_ped, effect, spawnFeet);
            }
        }
        const float dt = CTimer::ms_fTimeStep / 50.0f;
        for (auto it = state.standIns.begin(); it != state.standIns.end();) {
            const bool stale = (it->satchel ? !a_owned : it->handover != handover) || CTimer::m_snTimeInMilliseconds > it->untilMs;
            if (stale || CProjectileInfo::ms_apProjectile[it->slot] != it->projectile) {
                if (stale) {
                    DropStandIn(*it);
                }
                it = state.standIns.erase(it);
                continue;
            }
            it->position += it->velocity * dt;
            it->projectile->SetPosition(it->position);
            ++it;
        }
    }
}
