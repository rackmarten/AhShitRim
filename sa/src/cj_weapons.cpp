#include "cj_weapons.h"

#include "body.h"
#include "cj_body.h"
#include "cj_shots.h"
#include "log.h"
#include "model_sender.h"
#include "rw_texture.h"
#include "skin_build.h"
#include "skin_model.h"
#include "texture_decode.h"
#include "vehicle.h"
#include "weapons.h"

#include <plugin.h>

#include <CCamera.h>
#include <CCheat.h>
#include <CClumpModelInfo.h>
#include <CModelInfo.h>
#include <CPedIntelligence.h>
#include <CPlayerData.h>
#include <CPlayerPed.h>
#include <CStats.h>
#include <CStreaming.h>
#include <CTimer.h>
#include <CWeapon.h>
#include <CWeaponInfo.h>
#include <NodeName.h>
#include <common.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace AhShitRim::CjWeapons
{
    static_assert(ModelSender::kWeaponKey == 1 + Protocol::kVehicleMax);

    namespace
    {
        // The bones San Andreas puts a weapon on (VisibilityPlugins::RenderWeaponPedsForPC).
        constexpr int kBoneRightHand = 24;
        constexpr int kBoneLeftHand = 34;
        constexpr int kBoneSpine = 3;  // a parachute

        // eModelStreamingFlags: wanted by the game, and kept once loaded.
        constexpr int kStreamKeep = 0x2 | 0x8;
        constexpr int kLoaded = 1;  // LOADSTATE_LOADED

        constexpr unsigned short kStatPistolSkill = 69;  // STAT_PISTOL_SKILL, the first of the 11 weapon skills
        constexpr float          kSkillMax = 1000.0f;
        constexpr std::uint32_t  kHitsMax = 64;  // hits one command can count

        // The left hand's weapon is the right one's model turned over and nudged (the same routine).
        const RwV3d kXAxis{ 1.0f, 0.0f, 0.0f };
        const RwV3d kYAxis{ 0.0f, 1.0f, 0.0f };
        const RwV3d kLeftNudge{ 0.04f, -0.05f, 0.0f };
        const RwV3d kParachuteNudge{ 0.1f, -0.15f, 0.0f };

        // Weapons the debug key's fourth set adds: what the three cheats leave out that fits in the slots
        // free after them.
        struct Give
        {
            eWeaponType type;
            unsigned    ammo;
        };
        constexpr Give kExtras[] = {
            { WEAPONTYPE_TEARGAS, 10 }, { WEAPONTYPE_SATCHEL_CHARGE, 5 }, { WEAPONTYPE_DETONATOR, 1 }, { WEAPONTYPE_MINIGUN, 500 },
            { WEAPONTYPE_CAMERA, 36 }, { WEAPONTYPE_PARACHUTE, 1 },
        };

        struct Cache
        {
            std::vector<std::uint8_t> blob;
            std::uint32_t             partCount = 0;
            std::uint32_t             ids[Protocol::kWeaponHandMax] = {};  // sent under these, one per hand, for good
            bool                      failed = false;
        };

        struct State
        {
            Weapons::WheelPulse               wheel;
            std::deque<std::vector<std::uint8_t>> commands;
            std::map<int, Cache>              cache;  // by the weapon's model index
            RpClump*                          clump = nullptr;
            int                               modelIndex = -1;
            std::vector<RwFrame*>             parts;  // the clump's drawn parts, in bone order
            std::uint32_t                     partCount = 0;
            bool                              shown = false;
            std::uint32_t                     ids[Protocol::kWeaponHandMax] = {};
            bool                              sentTwin = false;
            std::uint32_t                     builds = 0;
            // For the pose.
            std::uint8_t                      lastType = 0;
            std::uint8_t                      lastSlot = 0;
            std::uint32_t                     lastAmmo = 0;
            std::uint16_t                     shots = 0;
            bool                              twin = false;
            Pose::Vec3                        muzzle[2];
            bool                              mouseLogged = false;
        } state;

        Body::RwRows ToFrame(const RwMatrix& a_m)
        {
            return { { a_m.right.x, a_m.right.y, a_m.right.z }, { a_m.up.x, a_m.up.y, a_m.up.z }, { a_m.at.x, a_m.at.y, a_m.at.z },
                { a_m.pos.x, a_m.pos.y, a_m.pos.z } };
        }

        Pose::Vec3 Vec(const CVector& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        RpClump* ClumpOf(CPlayerPed* a_ped) { return reinterpret_cast<RpClump*>(a_ped->m_pWeaponObject); }

        // ---- Models in memory ----

        // The weapon's model (and its second one) loaded and kept: a weapon in a slot but not in his hand
        // has no instance yet, and San Andreas makes one the moment he switches to it.
        bool EnsureModels(eWeaponType a_type)
        {
            const auto* info = CWeaponInfo::GetWeaponInfo(a_type, 1);
            if (!info) {
                return false;
            }
            bool asked = false;
            for (const int model : { info->m_nModelId, info->m_nModelId2 }) {
                if (model > 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != kLoaded) {
                    CStreaming::RequestModel(model, kStreamKeep);
                    asked = true;
                }
            }
            if (asked) {
                CStreaming::LoadAllRequestedModels(false);
            }
            return info->m_nModelId <= 0 || CStreaming::ms_aInfoForModel[info->m_nModelId].m_nLoadState == kLoaded;
        }

        void EnsureLoadout(CPlayerPed* a_ped)
        {
            for (auto& weapon : a_ped->m_aWeapons) {
                if (weapon.m_eWeaponType != WEAPONTYPE_UNARMED) {
                    EnsureModels(weapon.m_eWeaponType);
                }
            }
        }

        // ---- Commands from Skyrim ----

        void GiveOne(CPlayerPed* a_ped, eWeaponType a_type, unsigned a_ammo)
        {
            if (!EnsureModels(a_type)) {
                Log(std::format("weapons: the model of weapon {} didn't load, not given", static_cast<unsigned>(a_type)));
                return;
            }
            a_ped->GiveWeapon(a_type, a_ammo, true);
        }

        void SetLoadout(CPlayerPed* a_ped, const Protocol::WeaponCommand& a_command)
        {
            Weapons::Loadout loadout;
            std::copy(std::begin(a_command.entries), std::end(a_command.entries), std::begin(loadout));
            Weapons::Sanitise(loadout);
            a_ped->ClearWeapons();
            int given = 0;
            for (const auto& entry : loadout) {
                if (entry.type == Weapons::Unarmed) {
                    continue;
                }
                GiveOne(a_ped, static_cast<eWeaponType>(entry.type), entry.ammo);
                const auto slot = CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(entry.type), 1)->m_nSlot;
                if (slot < Protocol::kWeaponSlotCount && a_ped->m_aWeapons[slot].m_eWeaponType == static_cast<eWeaponType>(entry.type)) {
                    a_ped->m_aWeapons[slot].m_nAmmoInClip = (std::min)(static_cast<unsigned>(entry.clip), a_ped->m_aWeapons[slot].m_nAmmoTotal);
                    given++;
                }
            }
            const unsigned held = a_command.argument < Protocol::kWeaponSlotCount ? a_command.argument : 0;
            a_ped->SetCurrentWeapon(Weapons::Holds(loadout, held) ? static_cast<int>(held) : 0);
            std::string skills;
            for (std::uint32_t i = 0; i < Protocol::kWeaponSkillCount; ++i) {
                const float value = (std::min)(static_cast<float>(a_command.skills[i]), kSkillMax);
                CStats::SetStatValue(static_cast<unsigned short>(kStatPistolSkill + i), value);
                skills += std::format(" {:.0f}", value);
            }
            Log(std::format("weapons: loadout set from Skyrim, {} weapons, holding slot {}, skills{}", given, a_ped->m_nSelectedWepSlot, skills));
        }

        // Skyrim's hits raise CJ's skill with the weapon as San Andreas's own hits would.
        void Hits(CPlayerPed* a_ped, std::uint32_t a_type, std::uint32_t a_count)
        {
            if (a_type > 0xFF || !Weapons::Carried(static_cast<std::uint8_t>(a_type))) {
                return;
            }
            const auto type = static_cast<eWeaponType>(a_type);
            const auto before = a_ped->GetWeaponSkill(type);
            for (std::uint32_t i = 0; i < (std::min)(a_count, kHitsMax); ++i) {
                CStats::UpdateStatsWhenWeaponHit(type);
            }
            if (const auto after = a_ped->GetWeaponSkill(type); after != before) {
                static constexpr const char* kLevels[] = { "poor", "gangster", "hitman" };
                Log(std::format("weapons: CJ's skill with weapon {} is {} now", a_type, after >= 0 && after <= 2 ? kLevels[after] : "?"));
            }
        }

        void GiveSet(CPlayerPed* a_ped, unsigned a_set)
        {
            switch (a_set) {
            case 1: CCheat::WeaponCheat1(); break;
            case 2: CCheat::WeaponCheat2(); break;
            case 3: CCheat::WeaponCheat3(); break;
            case 4:
                for (const auto& give : kExtras) {
                    GiveOne(a_ped, give.type, give.ammo);
                }
                break;
            default: Log(std::format("weapons: weapon set {} doesn't exist", a_set)); return;
            }
            EnsureLoadout(a_ped);
            Log(std::format("weapons: weapon set {} given", a_set));
        }

        void Apply(CPlayerPed* a_ped, const std::vector<std::uint8_t>& a_bytes)
        {
            Protocol::WeaponCommand command{};
            std::memcpy(&command, a_bytes.data(), sizeof(command));
            switch (static_cast<Protocol::WeaponCommandKind>(command.kind)) {
            case Protocol::WeaponCommandKind::SetLoadout: SetLoadout(a_ped, command); break;
            case Protocol::WeaponCommandKind::GiveSet: GiveSet(a_ped, command.argument); break;
            case Protocol::WeaponCommandKind::GiveWeapon: {
                const auto type = static_cast<std::uint8_t>(command.argument);
                if (command.argument <= 0xFF && Weapons::Carried(type)) {
                    GiveOne(a_ped, static_cast<eWeaponType>(type), command.entries[0].ammo ? command.entries[0].ammo : 1);
                    EnsureLoadout(a_ped);
                    Log(std::format("weapons: weapon {} given", command.argument));
                } else {
                    Log(std::format("weapons: weapon {} can't be given", command.argument));
                }
                break;
            }
            case Protocol::WeaponCommandKind::Hit: Hits(a_ped, command.argument, command.entries[0].ammo); return;  // his hands stay as they are
            default: Log(std::format("weapons: command {} unknown", command.kind)); break;
            }
            // Giving and clearing weapons frees the clump in his hand, and the next one can be made at the same
            // address: his hand is looked at again, parts and all.
            state.clump = nullptr;
            state.modelIndex = -1;
            state.parts.clear();
            state.partCount = 0;
        }

        // ---- The model of the weapon in his hand ----

        // The clump's drawn parts, in the order they become bones. The flash is San Andreas's own effect
        // (Skyrim gets a light), and an atomic San Andreas isn't drawing isn't a part.
        std::vector<RpAtomic*> PartsOf(RpClump* a_clump)
        {
            std::vector<RpAtomic*> all;
            RpClumpForAllAtomics(
                a_clump,
                [](RpAtomic* a_atomic, void* a_data) -> RpAtomic* {
                    static_cast<std::vector<RpAtomic*>*>(a_data)->push_back(a_atomic);
                    return a_atomic;
                },
                &all);
            std::vector<RpAtomic*> parts;
            for (auto* atomic : all) {
                if (!atomic->geometry || (RpAtomicGetFlags(atomic) & rpATOMICRENDER) == 0 || parts.size() >= Protocol::kBodyBoneMax) {
                    continue;
                }
                const char*            raw = GetFrameNodeName(RpAtomicGetFrame(atomic));
                const std::string_view name = raw ? raw : "";
                if (name == "gunflash") {
                    continue;
                }
                parts.push_back(atomic);
            }
            return parts;
        }

        bool Build(CPlayerPed* a_ped, eWeaponType a_type, const std::vector<RpAtomic*>& a_parts, Cache& a_out)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto name = std::string(Weapons::ModelName(static_cast<std::uint8_t>(a_type)));
            SkinModel::ModelIn model;
            model.name = name.substr(0, 31);
            model.kind = Protocol::kSkinModelWeapon;
            std::map<RwRaster*, std::int32_t> byRaster;
            std::vector<SkinModel::MeshIn>    meshes;
            std::string                       textureLog;
            std::uint32_t                     flipped = 0, skipped = 0;
            for (std::uint32_t part = 0; part < a_parts.size(); ++part) {
                RpAtomic*   atomic = a_parts[part];
                RpGeometry* geometry = atomic->geometry;
                if ((geometry->flags & rpGEOMETRYNATIVE) != 0 || !geometry->morphTarget || !geometry->morphTarget[0].verts || !geometry->triangles) {
                    skipped++;
                    model.boneIds.push_back(part);  // keeps the bone order: it just has no mesh
                    continue;
                }
                std::vector<SkinBuild::Material> materials;
                for (int m = 0; m < geometry->matList.numMaterials; m++) {
                    const RpMaterial*   material = geometry->matList.materials[m];
                    RwTexture*          texture = material->texture;
                    SkinBuild::Material out;
                    out.color[0] = material->color.red;
                    out.color[1] = material->color.green;
                    out.color[2] = material->color.blue;
                    out.color[3] = material->color.alpha;
                    if (texture && texture->raster) {
                        const auto found = byRaster.find(texture->raster);
                        if (found != byRaster.end()) {
                            out.texture = found->second;
                        } else {
                            std::string why;
                            auto        read = RwTextures::Read(texture, why);
                            if (read.rgba.empty()) {
                                Log(std::format("weapons: texture {} of the {} left white: {}", static_cast<const char*>(texture->name), name, why));
                                out.texture = -1;
                            } else {
                                textureLog += std::format(" {} {}x{}", static_cast<const char*>(texture->name), read.width, read.height);
                                out.texture = static_cast<std::int32_t>(model.textures.size());
                                model.textures.push_back(std::move(read));
                            }
                            byRaster[texture->raster] = out.texture;
                        }
                    }
                    if (out.color[3] < 255) {
                        out.flags |= Protocol::kSkinMeshBlend;
                    } else if (out.texture >= 0 && TextureDecode::HasAlpha(model.textures[out.texture].rgba)) {
                        out.flags |= Protocol::kSkinMeshCutout;
                    }
                    materials.push_back(out);
                }
                // Every vertex on this part's bone alone, in the part's own space.
                const auto                 vertexCount = static_cast<std::uint32_t>(geometry->numVertices);
                std::vector<std::uint32_t> bones(vertexCount, part);
                std::vector<float>         weights(static_cast<std::size_t>(vertexCount) * 4, 0.0f);
                for (std::uint32_t v = 0; v < vertexCount; v++) {
                    weights[v * 4] = 1.0f;
                }
                SkinBuild::Geometry in;
                in.vertexCount = vertexCount;
                in.positions = reinterpret_cast<const float*>(geometry->morphTarget[0].verts);
                in.normals = reinterpret_cast<const float*>(geometry->morphTarget[0].normals);
                in.uvs = geometry->numTexCoordSets > 0 ? reinterpret_cast<const float*>(geometry->texCoords[0]) : nullptr;
                in.boneIndices = bones.data();
                in.weights = weights.data();
                in.boneCount = Protocol::kBodyBoneMax;
                in.triangleCount = static_cast<std::uint32_t>(geometry->numTriangles);
                in.triangles = reinterpret_cast<const std::uint16_t*>(geometry->triangles);
                SkinBuild::Stats stats;
                auto             partMeshes = SkinBuild::Meshes(in, materials, &stats);
                flipped += stats.flipped;
                Vehicle::BakeMeshes(partMeshes, Vehicle::Bake(ToFrame(*RwFrameGetLTM(RpAtomicGetFrame(atomic)))));
                for (auto& mesh : partMeshes) {
                    meshes.push_back(std::move(mesh));
                }
                model.boneIds.push_back(part);
            }
            model.meshes = Vehicle::Merge(std::move(meshes));
            if (model.meshes.empty()) {
                Log(std::format("weapons: the {} has nothing to draw ({} parts, {} skipped)", name, a_parts.size(), skipped));
                return false;
            }
            std::uint32_t vertices = 0, triangles = 0;
            for (const auto& mesh : model.meshes) {
                vertices += static_cast<std::uint32_t>(mesh.vertices.size());
                triangles += static_cast<std::uint32_t>(mesh.indices.size() / 3);
            }
            a_out.blob = SkinModel::Serialize(model);
            if (a_out.blob.size() > Protocol::kModelSizeMax || !SkinModel::Parse(a_out.blob.data(), a_out.blob.size())) {
                Log(std::format("weapons: the {}'s model ({} bytes) would not load in Skyrim, not sent", name, a_out.blob.size()));
                a_out.blob.clear();
                return false;
            }
            a_out.partCount = static_cast<std::uint32_t>(a_parts.size());
            const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            Log(std::format("weapons: the {}'s model read in {:.1f} ms: {} parts ({} skipped), {} meshes, {} vertices, {} triangles ({} turned round), "
                            "{} textures:{}; {} bytes",
                name, ms, a_parts.size(), skipped, model.meshes.size(), vertices, triangles, flipped, model.textures.size(),
                textureLog.empty() ? " none" : textureLog, a_out.blob.size()));
            (void)a_ped;
            return true;
        }

        // A weapon model keeps its id for good (a hand's own), so switching back to it doesn't build it again in
        // Skyrim, which only ever builds a model id once; the send is repeated, as a Skyrim that started since
        // has none of them.
        void SendHand(int a_hand, Cache& a_cache, eWeaponType a_type)
        {
            if (!a_cache.ids[a_hand]) {
                a_cache.ids[a_hand] = (static_cast<std::uint32_t>(::GetTickCount64()) << 4 | (++state.builds & 0xF)) | 1;
            }
            state.ids[a_hand] = a_cache.ids[a_hand];
            auto chunks = SkinModel::Chunks(state.ids[a_hand], a_cache.blob);
            ModelSender::Set(ModelSender::kWeaponKey + a_hand, state.ids[a_hand], std::move(chunks),
                std::format("weapons: the {}'s model for the {} hand", Weapons::ModelName(static_cast<std::uint8_t>(a_type)), a_hand == 0 ? "right" : "left"));
        }

        void Hide()
        {
            if (!state.shown) {
                return;
            }
            state.shown = false;
            for (std::uint32_t hand = 0; hand < Protocol::kWeaponHandMax; ++hand) {
                GetLink().WriteWeapon(hand, Protocol::BodyState{});
            }
            ModelSender::Remove(ModelSender::kWeaponKey);
            ModelSender::Remove(ModelSender::kWeaponKey + 1);
            state.clump = nullptr;
            state.modelIndex = -1;
            state.parts.clear();
            state.partCount = 0;
            state.sentTwin = false;
        }

        // San Andreas puts the weapon's frame on the hand bone with the hand's matrix when it draws a ped
        // (RenderWeaponPedsForPC), a left hand's twin turned over; hidden it never does, so it is done
        // here. Returns false without the bone.
        bool PlaceOnHand(CPlayerPed* a_ped, RpClump* a_clump, eWeaponType a_type, bool a_left)
        {
            RwMatrix hand;
            if (!CjBody::BoneWorld(a_ped, a_type == WEAPONTYPE_PARACHUTE ? kBoneSpine : a_left ? kBoneLeftHand : kBoneRightHand, hand)) {
                return false;
            }
            RwFrame*  frame = RpClumpGetFrame(a_clump);
            RwMatrix* matrix = RwFrameGetMatrix(frame);
            std::memcpy(matrix, &hand, sizeof(RwMatrix));
            if (a_type == WEAPONTYPE_PARACHUTE) {
                RwMatrixTranslate(matrix, &kParachuteNudge, rwCOMBINEPRECONCAT);
                RwMatrixRotate(matrix, &kYAxis, 90.0f, rwCOMBINEPRECONCAT);
            } else if (a_left) {
                RwMatrixRotate(matrix, &kXAxis, 180.0f, rwCOMBINEPRECONCAT);
                RwMatrixTranslate(matrix, &kLeftNudge, rwCOMBINEPRECONCAT);
            }
            RwFrameUpdateObjects(frame);
            return true;
        }

        void WriteHand(int a_hand, CPlayerPed* a_ped, eWeaponType a_type, const CVector& a_feet, const CVector& a_spawnFeet, unsigned a_handover,
            unsigned a_frame)
        {
            Protocol::BodyState s{};
            s.frame = a_frame;
            s.flags = Protocol::kBodyValid;
            s.handover = a_handover;
            s.model = state.ids[a_hand];
            s.boneCount = state.partCount;
            s.timeMs = CjBody::NowMs();
            const CVector d = a_feet - a_spawnFeet;
            s.feet[0] = d.x * Pose::kUnitsPerMeter;
            s.feet[1] = d.y * Pose::kUnitsPerMeter;
            s.feet[2] = d.z * Pose::kUnitsPerMeter;
            s.cameraForward[1] = 1.0f;
            for (std::uint32_t i = 0; i < state.partCount && i < state.parts.size(); ++i) {
                s.bones[i] = Body::ToBone(ToFrame(*RwFrameGetLTM(state.parts[i])), Vec(a_feet));
            }
            if (Body::Finite(s)) {
                GetLink().WriteWeapon(static_cast<std::uint32_t>(a_hand), s);
            }
            // Where a shot leaves: the weapon's fire offset in the weapon's frame.
            if (const auto* info = CWeaponInfo::GetWeaponInfo(a_type, a_ped->GetWeaponSkill(a_type)); info) {
                const float offset[3] = { info->m_vecFireOffset.x, info->m_vecFireOffset.y, info->m_vecFireOffset.z };
                float       out[3];
                Weapons::Muzzle(ToFrame(*RwFrameGetLTM(RpClumpGetFrame(state.clump))), offset, Vec(a_feet), out);
                state.muzzle[a_hand] = { out[0] * Pose::kMetersPerUnit, out[1] * Pose::kMetersPerUnit, out[2] * Pose::kMetersPerUnit };
            }
        }

        // The minigun's barrels turn as the gun spins up; San Andreas does that in PreRender, which never
        // runs while it is hidden.
        void SpinMinigun(CPlayerPed* a_ped, RpClump* a_clump)
        {
            auto* data = a_ped->m_pPlayerData;
            if (!data || CTimer::m_UserPause) {
                return;
            }
            if (RwFrame* barrels = CClumpModelInfo::GetFrameFromName(a_clump, "minigun2")) {
                RwMatrixRotate(RwFrameGetMatrix(barrels), &kXAxis, CTimer::ms_fTimeStep * data->m_fGunSpinSpeed * (180.0f / 3.14159265f), rwCOMBINEPRECONCAT);
            }
        }

        void Refresh(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame)
        {
            auto*      clump = ClumpOf(a_ped);
            const auto type = a_ped->m_aWeapons[a_ped->m_nSelectedWepSlot].m_eWeaponType;
            const bool carried = type != WEAPONTYPE_UNARMED && Weapons::Carried(static_cast<std::uint8_t>(type));
            // The weapon's model by its info: the ped's own record of it (m_nWeaponModelId) is -1 for fists
            // and knuckles even with the model in his hand.
            const auto* info = carried ? CWeaponInfo::GetWeaponInfo(type, a_ped->GetWeaponSkill(type)) : nullptr;
            const int   modelIndex = info ? info->m_nModelId : -1;
            if (!clump || !info || modelIndex <= 0) {
                if (state.shown) {
                    Log(std::format("weapons: nothing in his hand any more (clump {}, type {}, model {})", clump != nullptr, static_cast<unsigned>(type), modelIndex));
                }
                Hide();
                return;
            }
            // A new instance (a switch, a reload of the clump) gets its parts looked up again, and its model
            // sent if it is a weapon not seen before.
            const bool twin = info->m_nFlags.bTwinPistol != 0;
            if (clump != state.clump || modelIndex != state.modelIndex || twin != state.sentTwin) {
                const auto atomics = PartsOf(clump);
                auto&      cache = state.cache[modelIndex];
                if (cache.blob.empty() && !cache.failed) {
                    cache.failed = !Build(a_ped, type, atomics, cache);
                }
                if (cache.failed) {
                    Hide();
                    state.clump = clump;  // not tried again until the instance changes
                    state.modelIndex = modelIndex;
                    return;
                }
                state.parts.clear();
                for (auto* atomic : atomics) {
                    state.parts.push_back(RpAtomicGetFrame(atomic));
                }
                state.partCount = (std::min)(static_cast<std::uint32_t>(state.parts.size()), cache.partCount);
                SendHand(0, cache, type);
                if (twin) {
                    SendHand(1, cache, type);
                } else {
                    ModelSender::Remove(ModelSender::kWeaponKey + 1);
                    GetLink().WriteWeapon(1, Protocol::BodyState{});
                }
                state.clump = clump;
                state.modelIndex = modelIndex;
                state.sentTwin = twin;
                Log(std::format("weapons: CJ holds weapon {} (model {}){}", static_cast<unsigned>(type), modelIndex, twin ? ", in both hands" : ""));
            }
            if (state.partCount == 0) {
                return;
            }
            state.twin = twin;
            state.shown = true;
            if (type == WEAPONTYPE_MINIGUN) {
                SpinMinigun(a_ped, clump);
            }
            const float   ground = a_ped->GetDistanceFromCentreOfMassToBaseOfModel();
            const CVector feet = a_ped->GetPosition() - CVector(0.0f, 0.0f, ground);
            const CVector spawnFeet = a_spawn - CVector(0.0f, 0.0f, ground + a_lift);
            for (int hand = 0; hand < (twin ? 2 : 1); ++hand) {
                if (PlaceOnHand(a_ped, clump, type, hand == 1)) {
                    WriteHand(hand, a_ped, type, feet, spawnFeet, a_handover, a_frame);
                }
            }
        }
    }

    void Queue(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        if (a_size != sizeof(Protocol::WeaponCommand)) {
            Log(std::format("weapons: a command of {} bytes (expected {}), ignored", a_size, sizeof(Protocol::WeaponCommand)));
            return;
        }
        state.commands.emplace_back(a_data, a_data + a_size);
        while (state.commands.size() > 16) {
            state.commands.pop_front();
        }
    }

    Weapons::WheelPulse::Presses Wheel(const Protocol::InputState& a_input, bool a_active)
    {
        return state.wheel.Step(a_input.radioNext, a_input.radioPrev, a_active);
    }

    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame)
    {
        if (!state.mouseLogged) {
            state.mouseLogged = true;
            CCamera::m_bUseMouse3rdPerson = true;  // the mouse aims in the third person (free aim)
            Log("weapons: mouse aiming on");
        }
        while (!state.commands.empty()) {
            Apply(a_ped, state.commands.front());
            state.commands.pop_front();
        }
        Refresh(a_ped, a_spawn, a_lift, a_handover, a_frame);
        CjShots::SetFrame(a_spawn - CVector(0.0f, 0.0f, a_ped->GetDistanceFromCentreOfMassToBaseOfModel() + a_lift), a_handover);
        // San Andreas fires CJ's gun while it finishes drawing him (CPedIntelligence::ProcessAfterPreRender,
        // from CWorld::ProcessPedsAfterPreRender, after PreRender put the bones right): the gun task only
        // marks the shot, and hidden nothing ever fires it. Bones are posed (CjBody), so it does the same.
        // The pitch San Andreas's aim camera hands his aim is a quarter turn off in this setup: with the camera level the
        // gun pointed 70 degrees up and his torso leaned back to its limit, and the mouse barely moved it. His aim
        // follows where the camera looks (up is negative), corrected after the frame for the next one.
        if (TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode == MODE_AIMWEAPON && a_ped->m_pPlayerData) {
            a_ped->m_pPlayerData->m_fLookPitch = Weapons::AimLookPitch(TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront.z);
        }
        if (!CTimer::m_UserPause && !CTimer::bSkipProcessThisFrame) {
            a_ped->bCalledPreRender = true;
            a_ped->m_pIntelligence->ProcessAfterPreRender();
        }
    }

    std::vector<std::uint8_t> BuildModel(RwObject* a_object, std::uint8_t a_type, std::vector<RpAtomic*>& a_parts)
    {
        a_parts.clear();
        if (!a_object) {
            return {};
        }
        if (RwObjectGetType(a_object) == rpCLUMP) {
            a_parts = PartsOf(reinterpret_cast<RpClump*>(a_object));
        } else if (RwObjectGetType(a_object) == rpATOMIC) {
            a_parts.push_back(reinterpret_cast<RpAtomic*>(a_object));
        }
        Cache cache;
        if (a_parts.empty() || !Build(nullptr, static_cast<eWeaponType>(a_type), a_parts, cache)) {
            return {};
        }
        return std::move(cache.blob);
    }

    void Fill(CPlayerPed* a_ped, Pose::SanAndreasPose& a_pose)
    {
        const auto slot = static_cast<std::uint8_t>((std::min)(static_cast<unsigned>(a_ped->m_nSelectedWepSlot), Protocol::kWeaponSlotCount - 1));
        const auto& weapon = a_ped->m_aWeapons[slot];
        const auto  type = weapon.m_eWeaponType <= Weapons::kLastCarried ? static_cast<std::uint8_t>(weapon.m_eWeaponType) : std::uint8_t{ 0 };
        a_pose.weaponType = type;
        a_pose.weaponSlot = slot;
        if (type == state.lastType && slot == state.lastSlot) {
            state.shots = static_cast<std::uint16_t>(state.shots + Weapons::ShotsBetween(state.lastAmmo, weapon.m_nAmmoTotal));
        }
        state.lastType = type;
        state.lastSlot = slot;
        state.lastAmmo = weapon.m_nAmmoTotal;
        a_pose.shots = state.shots;

        const auto camera = static_cast<std::uint8_t>(TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode);
        a_pose.cameraMode = camera;
        if (const auto* info = CWeaponInfo::GetWeaponInfo(weapon.m_eWeaponType, a_ped->GetWeaponSkill(weapon.m_eWeaponType))) {
            a_pose.ammoStyle = Weapons::AmmoStyleOf(type, info->m_nAmmoClip);
        }
        a_pose.aimRadius = a_ped->GetWeaponRadiusOnScreen();
        a_pose.crosshair[0] = CCamera::m_f3rdPersonCHairMultX;
        a_pose.crosshair[1] = CCamera::m_f3rdPersonCHairMultY;

        std::uint16_t flags = 0;
        if (state.shown) {
            flags |= Protocol::kWeaponInHand;
            if (state.twin) {
                flags |= Protocol::kWeaponTwin;
            }
        }
        if (Weapons::ScopeOf(camera) != Weapons::Scope::None) {
            flags |= Protocol::kWeaponAiming;
        }
        if (weapon.m_nState == WEAPONSTATE_FIRING) {
            flags |= Protocol::kWeaponFiring;
        }
        if (weapon.m_nState == WEAPONSTATE_RELOADING) {
            flags |= Protocol::kWeaponReloading;
        }
        if (Weapons::IsMelee(type)) {
            flags |= Protocol::kWeaponMelee;
        }
        a_pose.weaponFlags = flags;
        a_pose.muzzle[0] = state.muzzle[0];
        a_pose.muzzle[1] = state.muzzle[1];
        for (std::uint32_t i = 0; i < Protocol::kWeaponSlotCount; ++i) {
            const auto& w = a_ped->m_aWeapons[i];
            if (w.m_eWeaponType != WEAPONTYPE_UNARMED && w.m_eWeaponType <= Weapons::kLastCarried) {
                a_pose.loadout[i] = { static_cast<std::uint8_t>(w.m_eWeaponType), 0,
                    static_cast<std::uint16_t>((std::min)(w.m_nAmmoInClip, 65535u)), (std::min)(w.m_nAmmoTotal, Weapons::kAmmoMax) };
            }
        }
        for (std::uint32_t i = 0; i < Protocol::kWeaponSkillCount; ++i) {
            const float value = CStats::GetStatValue(static_cast<unsigned short>(kStatPistolSkill + i));
            a_pose.skills[i] = static_cast<std::uint16_t>(std::isfinite(value) ? std::clamp(value, 0.0f, kSkillMax) : 0.0f);
        }
    }
}
