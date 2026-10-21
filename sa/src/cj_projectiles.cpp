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

#include <CProjectile.h>
#include <CProjectileInfo.h>
#include <CTimer.h>
#include <common.h>

#include <safetyhook.hpp>

#include <Windows.h>

#include <array>
#include <cmath>
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
        constexpr std::uint32_t  kSlots = 32;                // gaProjectileInfo, CProjectileInfo::ms_apProjectile

        SafetyHookInline g_addProjectile;
        SafetyHookInline g_removeNotAdd;

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
                Send(event, "launch");
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
    }
}
