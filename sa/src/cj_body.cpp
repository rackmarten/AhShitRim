#include "cj_body.h"

#include "log.h"
#include "model_sender.h"
#include "rw_texture.h"
#include "skin_build.h"
#include "skin_model.h"
#include "texture_decode.h"

#include <plugin.h>

#include <CCamera.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CTaskSimpleUseGun.h>
#include <CTimer.h>
#include <CWeaponInfo.h>
#include <common.h>

#include <safetyhook.hpp>

#include <Windows.h>

#include <chrono>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace AhShitRim::CjBody
{
    namespace
    {
        // ---- San Andreas 1.0 US addresses (gta-reversed names) ----

        constexpr std::uintptr_t kUpdateAnim = 0x535F00;  // CEntity::UpdateAnim (thiscall)
        // CPed::ShoulderBoneRotation: a static cdecl function (gta-reversed), which plugin-sdk calls as a
        // thiscall member: the argument was never popped and a register CjBody's caller kept came back
        // wrong (San Andreas crashed in the runner's stats).
        const auto ShoulderBoneRotation = reinterpret_cast<void(__cdecl*)(RpClump*)>(0x5DF560);

        SafetyHookInline g_updateAnim;

        struct State
        {
            RpGeometry*   geometry = nullptr;  // what the model was built from; a new one (clothes changed) rebuilds it
            std::uint64_t signature = 0;       // and its shape, since a rebuilt geometry can come back at the same address
            bool          failed = false;      // the last build failed; tried again when the geometry changes
            std::uint32_t model = 0;
            std::uint32_t boneCount = 0;
            bool          poseLogged = false;
            bool          havePelvis = false;
            CVector       pelvis;  // his pelvis joint from his position, last posed frame (metres)
        } state;

        constexpr int kBonePelvis = 2;  // BONE_PELVIS
        constexpr int kBoneRightUpperArm = 22, kBoneRightForeArm = 23, kBoneRightHand = 24, kBoneLeftUpperArm = 32, kBoneLeftForeArm = 33, kBoneLeftHand = 34;
        constexpr int kModeAimWeapon = 0x35;  // MODE_AIMWEAPON: free aim in the third person

        int IndexOf(RpHAnimHierarchy* a_hierarchy, int a_bone)
        {
            for (int i = 0; i < a_hierarchy->numNodes; i++) {
                if (a_hierarchy->pNodeInfo[i].nodeID == a_bone) {
                    return i;
                }
            }
            return -1;
        }

        // Each node's parent, from the hierarchy's push and pop flags (RenderWare walks the nodes depth first: a
        // node without the pop flag is its next node's parent; one with the push flag has a sibling after its
        // subtree).
        std::vector<int> ParentsOf(RpHAnimHierarchy* a_hierarchy)
        {
            std::vector<int> parents(a_hierarchy->numNodes, -1), stack;
            int              current = -1;
            for (int i = 0; i < a_hierarchy->numNodes; i++) {
                parents[i] = current;
                const auto flags = a_hierarchy->pNodeInfo[i].flags;
                if (flags & rpHANIMPUSHPARENTMATRIX) {
                    stack.push_back(current);
                }
                if (flags & rpHANIMPOPPARENTMATRIX) {
                    current = stack.empty() ? -1 : stack.back();
                    if (!stack.empty()) {
                        stack.pop_back();
                    }
                } else {
                    current = i;
                }
            }
            return parents;
        }

        bool Under(const std::vector<int>& a_parents, int a_node, int a_root)
        {
            for (int n = a_node; n >= 0; n = a_parents[n]) {
                if (n == a_root) {
                    return true;
                }
            }
            return false;
        }

        CVector Rotate(const CVector& a_axis, float a_cos, float a_sin, const CVector& a_v)
        {
            // Rodrigues: v cos + (k x v) sin + k (k . v)(1 - cos)
            const CVector cross(a_axis.y * a_v.z - a_axis.z * a_v.y, a_axis.z * a_v.x - a_axis.x * a_v.z, a_axis.x * a_v.y - a_axis.y * a_v.x);
            const float   dot = a_axis.x * a_v.x + a_axis.y * a_v.y + a_axis.z * a_v.z;
            return a_v * a_cos + cross * a_sin + a_axis * (dot * (1.0f - a_cos));
        }

        CVector Pos(const RwMatrix& a_m) { return CVector(a_m.pos.x, a_m.pos.y, a_m.pos.z); }

        // Turns node a_root and everything under it about a_pivot so that a_from points along a_to.
        void Turn(RpHAnimHierarchy* a_hierarchy, const std::vector<int>& a_parents, RwMatrix* a_matrices, int a_root, const CVector& a_pivot, CVector a_from, CVector a_to)
        {
            if (a_from.Magnitude() < 1e-4f || a_to.Magnitude() < 1e-4f) {
                return;
            }
            a_from.Normalize();
            a_to.Normalize();
            CVector     axis(a_from.y * a_to.z - a_from.z * a_to.y, a_from.z * a_to.x - a_from.x * a_to.z, a_from.x * a_to.y - a_from.y * a_to.x);
            const float sin = axis.Magnitude();
            const float cos = a_from.x * a_to.x + a_from.y * a_to.y + a_from.z * a_to.z;
            if (sin < 1e-4f) {
                return;
            }
            axis /= sin;
            for (int i = 0; i < a_hierarchy->numNodes; i++) {
                if (!Under(a_parents, i, a_root)) {
                    continue;
                }
                RwMatrix&     m = a_matrices[i];
                const CVector right = Rotate(axis, cos, sin, CVector(m.right.x, m.right.y, m.right.z));
                const CVector up = Rotate(axis, cos, sin, CVector(m.up.x, m.up.y, m.up.z));
                const CVector at = Rotate(axis, cos, sin, CVector(m.at.x, m.at.y, m.at.z));
                const CVector pos = a_pivot + Rotate(axis, cos, sin, Pos(m) - a_pivot);
                m.right = { right.x, right.y, right.z };
                m.up = { up.x, up.y, up.z };
                m.at = { at.x, at.y, at.z };
                m.pos = { pos.x, pos.y, pos.z };
            }
        }

        constexpr float kArmAcross = 0.26f;  // sin 15 degrees: how far an arm reaches across his front

        // The line an arm aiming along a_wanted takes: kept on its own side of his body (the right arm from
        // a bit left of straight ahead round to his right, the left arm the other way), so aiming across him
        // or behind him the arm stops and the wrist turns the rest of the way.
        CVector ArmLine(CPlayerPed* a_ped, const CVector& a_wanted, bool a_left)
        {
            CVector forward = a_ped->GetForward(), right = a_ped->GetRight();
            forward.z = right.z = 0.0f;
            const float horizontal = std::hypot(a_wanted.x, a_wanted.y);
            if (forward.Magnitude() < 1e-3f || right.Magnitude() < 1e-3f || horizontal < 1e-3f) {
                return a_wanted;
            }
            forward.Normalize();
            right.Normalize();
            const float side = a_left ? -1.0f : 1.0f;
            const float ahead = (a_wanted.x * forward.x + a_wanted.y * forward.y) / horizontal;
            const float out = side * (a_wanted.x * right.x + a_wanted.y * right.y) / horizontal;
            if (out >= -kArmAcross && ahead >= 0.0f) {
                return a_wanted;
            }
            // Across his body or behind him: as far across as the arm goes, at the same height.
            const float   across = ahead >= 0.0f ? -kArmAcross : (out >= 0.0f ? 1.0f : -kArmAcross);
            const float   straight = std::sqrt((std::max)(0.0f, 1.0f - across * across));
            const CVector flat = forward * straight + right * (side * across);
            return CVector(flat.x * horizontal, flat.y * horizontal, a_wanted.z);
        }

        // San Andreas points a one-handed gun with its arm IK (IKChainManager_c), which hidden leaves the arm
        // half raised and the gun pointing at the sky. So while he uses the gun (aiming or firing) the arm is
        // posed here as San Andreas's pistol stance looks: upper arm and forearm straight along the line from
        // the shoulder to the aim point, then the hand turned so the barrel (the weapon's fire offset on the
        // hand, as the weapon is put on it) points there too. Only the pose: shots go where San Andreas fires them.
        void AimArms(CPlayerPed* a_ped, RpHAnimHierarchy* a_hierarchy, RwMatrix* a_matrices)
        {
            const auto& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
            const bool  aiming = cam.m_nMode == kModeAimWeapon;
            auto*       task = a_ped->m_pIntelligence ? a_ped->m_pIntelligence->GetTaskUseGun() : nullptr;
            if (a_ped->bIsDucking || (!aiming && !task)) {
                return;  // he isn't using the gun: his arms are the animation's
            }
            const auto  type = a_ped->m_aWeapons[a_ped->m_nSelectedWepSlot].m_eWeaponType;
            const auto* info = CWeaponInfo::GetWeaponInfo(type, a_ped->GetWeaponSkill(type));
            if (!info || !info->m_nFlags.bAimWithArm) {
                return;
            }
            // Free aim: where San Andreas's crosshair aims. Firing without the aim camera: ahead of him.
            CVector source, target;
            if (aiming) {
                TheCamera.Find3rdPersonCamTargetVector(20.0f, a_ped->GetPosition() + CVector(0.0f, 0.0f, 0.7f), &source, &target);
            } else {
                target = a_ped->GetPosition() + a_ped->GetForward() * 20.0f + CVector(0.0f, 0.0f, 0.5f);
            }
            const CVector offset(info->m_vecFireOffset.x, info->m_vecFireOffset.y, info->m_vecFireOffset.z);
            const auto    parents = ParentsOf(a_hierarchy);
            for (int arm = 0; arm < (info->m_nFlags.bTwinPistol ? 2 : 1); ++arm) {
                const int upper = IndexOf(a_hierarchy, arm == 0 ? kBoneRightUpperArm : kBoneLeftUpperArm);
                const int fore = IndexOf(a_hierarchy, arm == 0 ? kBoneRightForeArm : kBoneLeftForeArm);
                const int hand = IndexOf(a_hierarchy, arm == 0 ? kBoneRightHand : kBoneLeftHand);
                if (upper < 0 || fore < 0 || hand < 0) {
                    continue;
                }
                const CVector shoulder = Pos(a_matrices[upper]);
                const CVector wanted = ArmLine(a_ped, target - shoulder, arm == 1);
                // The upper arm along the line, then the forearm, then the barrel at the target itself.
                Turn(a_hierarchy, parents, a_matrices, upper, shoulder, Pos(a_matrices[fore]) - shoulder, wanted);
                const CVector elbow = Pos(a_matrices[fore]);
                Turn(a_hierarchy, parents, a_matrices, fore, elbow, Pos(a_matrices[hand]) - elbow, wanted);
                // The left hand's weapon is turned over about its X axis (RenderWeaponPedsForPC).
                const CVector   fire = arm == 0 ? offset : CVector(offset.x, -offset.y, -offset.z);
                const RwMatrix& h = a_matrices[hand];
                const CVector   barrel = CVector(h.right.x, h.right.y, h.right.z) * fire.x + CVector(h.up.x, h.up.y, h.up.z) * fire.y + CVector(h.at.x, h.at.y, h.at.z) * fire.z;
                Turn(a_hierarchy, parents, a_matrices, hand, Pos(h), barrel, target - Pos(h));
            }
        }

        // Full animation updates for CJ, as if he were on screen: hidden, San Andreas draws nothing,
        // never sees him on screen and only moves his root (RpAnimBlendClumpUpdateAnimations with
        // onScreen false skips the bones). Everyone else as before.
        void __fastcall UpdateAnimHook(CEntity* a_entity)
        {
            if (a_entity && a_entity == FindPlayerPed(0)) {
                a_entity->bOffscreen = false;
                a_entity->bDontUpdateHierarchy = false;
                auto* clump = a_entity->m_pRwClump;
                if (clump && a_entity->m_pRwObject->type == rpCLUMP && RpAnimBlendClumpGetFirstAssociation(clump)) {
                    RpAnimBlendClumpUpdateAnimations(clump, CTimer::ms_fTimeStep / 50.0f, true);
                }
                return;
            }
            g_updateAnim.thiscall(a_entity);
        }

        RpAtomic* SkinnedAtomic(RpClump* a_clump, int* a_atomics = nullptr)
        {
            struct Found
            {
                RpAtomic* atomic = nullptr;
                int       count = 0;
            } found;
            RpClumpForAllAtomics(
                a_clump,
                [](RpAtomic* a_atomic, void* a_data) -> RpAtomic* {
                    auto* f = static_cast<Found*>(a_data);
                    f->count++;
                    if (!f->atomic && a_atomic->geometry && RpSkinGeometryGetSkin(a_atomic->geometry)) {
                        f->atomic = a_atomic;
                    }
                    return a_atomic;
                },
                &found);
            if (a_atomics) {
                *a_atomics = found.count;
            }
            return found.atomic;
        }

        std::uint64_t Signature(RpGeometry* a_geometry)
        {
            if (!a_geometry) {
                return 0;
            }
            const auto* skin = RpSkinGeometryGetSkin(a_geometry);
            return (static_cast<std::uint64_t>(a_geometry->numVertices) << 40) ^ (static_cast<std::uint64_t>(a_geometry->numTriangles) << 16) ^
                   (static_cast<std::uint64_t>(a_geometry->matList.numMaterials) << 8) ^ (skin ? RpSkinGetNumBones(const_cast<RpSkin*>(skin)) : 0) ^
                   reinterpret_cast<std::uintptr_t>(a_geometry->morphTarget ? a_geometry->morphTarget[0].verts : nullptr);
        }

        Body::RwRows ToFrame(const RwMatrix& a_m)
        {
            return { { a_m.right.x, a_m.right.y, a_m.right.z }, { a_m.up.x, a_m.up.y, a_m.up.z }, { a_m.at.x, a_m.at.y, a_m.at.z },
                { a_m.pos.x, a_m.pos.y, a_m.pos.z } };
        }

        RwFrame* FrameOf(RpAtomic* a_atomic) { return static_cast<RwFrame*>(a_atomic->object.object.parent); }

        // Reads CJ's model from his clump; false (logged) when it can't.
        bool Build(CPlayerPed* a_ped)
        {
            const auto start = std::chrono::steady_clock::now();
            auto*      clump = a_ped->m_pRwClump;
            int        atomics = 0;
            RpAtomic*  atomic = clump ? SkinnedAtomic(clump, &atomics) : nullptr;
            RpGeometry* geometry = atomic ? atomic->geometry : nullptr;
            RpHAnimHierarchy* hierarchy = clump ? GetAnimHierarchyFromSkinClump(clump) : nullptr;
            state.geometry = geometry;
            state.signature = Signature(geometry);
            state.failed = true;
            if (!geometry || !hierarchy) {
                Log(std::format("body: CJ has no skinned geometry ({} atomics) or no skeleton, nothing to draw in Skyrim", atomics));
                return false;
            }
            RpSkin* skin = RpSkinGeometryGetSkin(geometry);
            if ((geometry->flags & rpGEOMETRYNATIVE) != 0 || !geometry->morphTarget || !geometry->morphTarget[0].verts || !geometry->triangles) {
                Log(std::format("body: CJ's geometry has no vertex data in memory (flags 0x{:X})", geometry->flags));
                return false;
            }
            const std::uint32_t bones = (std::min)(RpSkinGetNumBones(skin), static_cast<RwUInt32>(hierarchy->numNodes));
            if (bones == 0 || bones > Protocol::kBodyBoneMax) {
                Log(std::format("body: CJ's skin has {} bones and his skeleton {}; at most {} are supported", RpSkinGetNumBones(skin),
                    hierarchy->numNodes, Protocol::kBodyBoneMax));
                return false;
            }

            SkinModel::ModelIn model;
            model.name = "CJ";
            for (std::uint32_t i = 0; i < bones; i++) {
                model.boneIds.push_back(static_cast<std::uint32_t>(hierarchy->pNodeInfo[i].nodeID));
            }

            // Textures, one per raster, and the materials that use them.
            std::map<RwRaster*, std::int32_t>  byRaster;
            std::vector<SkinBuild::Material> materials;
            std::string                        textureLog;
            for (int m = 0; m < geometry->matList.numMaterials; m++) {
                const RpMaterial*   material = geometry->matList.materials[m];
                SkinBuild::Material out;
                out.color[0] = material->color.red;
                out.color[1] = material->color.green;
                out.color[2] = material->color.blue;
                out.color[3] = material->color.alpha;
                if (RwTexture* texture = material->texture; texture && texture->raster) {
                    const auto found = byRaster.find(texture->raster);
                    if (found != byRaster.end()) {
                        out.texture = found->second;
                    } else {
                        std::string why;
                        auto        read = RwTextures::Read(texture, why);
                        if (read.rgba.empty()) {
                            Log(std::format("body: texture {} of CJ left white: {}", static_cast<const char*>(texture->name), why));
                            out.texture = -1;
                        } else {
                            textureLog += std::format(" {} {}x{}", static_cast<const char*>(texture->name), read.width, read.height);
                            out.texture = static_cast<std::int32_t>(model.textures.size());
                            model.textures.push_back(std::move(read));
                        }
                        byRaster[texture->raster] = out.texture;
                    }
                    if (out.texture >= 0 && TextureDecode::HasAlpha(model.textures[out.texture].rgba)) {
                        out.flags |= Protocol::kSkinMeshCutout;
                    }
                }
                materials.push_back(out);
            }

            SkinBuild::Geometry in;
            in.vertexCount = static_cast<std::uint32_t>(geometry->numVertices);
            in.positions = reinterpret_cast<const float*>(geometry->morphTarget[0].verts);
            in.normals = reinterpret_cast<const float*>(geometry->morphTarget[0].normals);
            in.uvs = geometry->numTexCoordSets > 0 ? reinterpret_cast<const float*>(geometry->texCoords[0]) : nullptr;
            in.boneIndices = RpSkinGetVertexBoneIndices(skin);
            in.weights = reinterpret_cast<const float*>(RpSkinGetVertexBoneWeights(skin));
            in.boneCount = bones;
            in.triangleCount = static_cast<std::uint32_t>(geometry->numTriangles);
            in.triangles = reinterpret_cast<const std::uint16_t*>(geometry->triangles);
            SkinBuild::Stats stats;
            model.meshes = SkinBuild::Meshes(in, materials, &stats);

            std::uint32_t vertices = 0, triangles = 0;
            for (const auto& mesh : model.meshes) {
                vertices += static_cast<std::uint32_t>(mesh.vertices.size());
                triangles += static_cast<std::uint32_t>(mesh.indices.size() / 3);
            }
            const auto blob = SkinModel::Serialize(model);
            if (blob.size() > Protocol::kModelSizeMax || !SkinModel::Parse(blob.data(), blob.size())) {
                Log(std::format("body: CJ's model ({} bytes, {} meshes) would not load in Skyrim, not sent", blob.size(), model.meshes.size()));
                return false;
            }
            state.model = static_cast<std::uint32_t>(::GetTickCount64()) | 1;  // new for every build, also across restarts
            state.boneCount = bones;
            auto       chunks = SkinModel::Chunks(state.model, blob);
            const auto chunkCount = chunks.size();
            ModelSender::Set(ModelSender::kBodyKey, state.model, std::move(chunks), "body: CJ's model");
            state.failed = false;
            const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            Log(std::format("body: CJ's model {:08X} read in {:.1f} ms: {} meshes, {} vertices, {} triangles ({} turned round, {} dropped, "
                            "{} vertices without weight, {} weights on bones past the skeleton), {} bones ({} space), {} textures:{}; {} bytes in {} chunks",
                state.model, ms, model.meshes.size(), vertices, triangles, stats.flipped, stats.dropped, stats.unweighted, stats.foreignBones, bones,
                (hierarchy->flags & rpHANIMHIERARCHYLOCALSPACEMATRICES) ? "local" : "world", model.textures.size(),
                textureLog.empty() ? " none" : textureLog, blob.size(), chunkCount));
            return true;
        }

        Pose::Vec3 Vec(const CVector& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        // Milliseconds since the first frame on the high-resolution clock (GetTickCount ticks in 16 ms steps, too coarse to
        // interpolate 30 frames a second by).
        std::uint32_t Clock()
        {
            static LARGE_INTEGER frequency{}, start{};
            if (!frequency.QuadPart) {
                ::QueryPerformanceFrequency(&frequency);
                ::QueryPerformanceCounter(&start);
            }
            LARGE_INTEGER now;
            ::QueryPerformanceCounter(&now);
            return static_cast<std::uint32_t>((now.QuadPart - start.QuadPart) * 1000 / frequency.QuadPart);
        }
    }

    std::uint32_t NowMs() { return Clock(); }

    bool BoneWorld(CPlayerPed* a_ped, int a_boneId, RwMatrixTag& a_out)
    {
        auto*             clump = a_ped->m_pRwClump;
        auto*             atomic = clump ? SkinnedAtomic(clump) : nullptr;
        RpHAnimHierarchy* hierarchy = clump ? GetAnimHierarchyFromSkinClump(clump) : nullptr;
        if (!atomic || !hierarchy) {
            return false;
        }
        const int index = RpHAnimIDGetIndex(hierarchy, a_boneId);
        const RwMatrix* matrices = RpHAnimHierarchyGetMatrixArray(hierarchy);
        if (index < 0 || !matrices) {
            return false;
        }
        a_out = matrices[index];
        if (hierarchy->flags & rpHANIMHIERARCHYLOCALSPACEMATRICES) {
            const RwMatrix* atomicLtm = RwFrameGetLTM(FrameOf(atomic));
            if (!atomicLtm) {
                return false;
            }
            RwMatrix world;
            RwMatrixMultiply(&world, &a_out, atomicLtm);
            a_out = world;
        }
        return true;
    }

    bool Pelvis(CVector& a_offset)
    {
        a_offset = state.pelvis;
        return state.havePelvis;
    }

    void Install()
    {
        g_updateAnim = safetyhook::create_inline(reinterpret_cast<void*>(kUpdateAnim), reinterpret_cast<void*>(&UpdateAnimHook));
        Log(g_updateAnim ? "body: CEntity::UpdateAnim hooked, CJ animates in full while hidden" : "body: could not hook CEntity::UpdateAnim");
    }

    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame)
    {
        auto* clump = a_ped->m_pRwClump;
        auto* atomic = clump ? SkinnedAtomic(clump) : nullptr;
        if (atomic && (atomic->geometry != state.geometry || Signature(atomic->geometry) != state.signature)) {
            Build(a_ped);  // ModelSender sends it
        }
        if (!atomic || state.failed) {
            return;
        }
        RpHAnimHierarchy* hierarchy = GetAnimHierarchyFromSkinClump(clump);
        RpSkin*           skin = RpSkinGeometryGetSkin(atomic->geometry);
        if (!hierarchy || !skin) {
            return;
        }

        // Nothing drew CJ, so nothing posed his bones from this frame's animation.
        a_ped->bDontUpdateHierarchy = false;
        a_ped->UpdateRwMatrix();  // the clump's frame where CJ is now (also after the origin moved)
        a_ped->UpdateRwFrame();
        a_ped->UpdateRpHAnim();
        // CPed::PreRender then sets the breast bones (between clavicle and upper arm) from the posed
        // upper arms; hidden it never runs, and the shoulders stretched back to wherever those bones
        // were left.
        // His arm aimed first, so the shoulder follows it.
        if ((hierarchy->flags & rpHANIMHIERARCHYLOCALSPACEMATRICES) == 0 && RpHAnimHierarchyGetMatrixArray(hierarchy)) {
            AimArms(a_ped, hierarchy, RpHAnimHierarchyGetMatrixArray(hierarchy));
        }
        ShoulderBoneRotation(clump);
        const RwMatrix* matrices = RpHAnimHierarchyGetMatrixArray(hierarchy);
        const RwMatrix* skinToBone = RpSkinGetSkinToBoneMatrices(skin);
        const bool      local = (hierarchy->flags & rpHANIMHIERARCHYLOCALSPACEMATRICES) != 0;
        const RwMatrix* atomicLtm = RwFrameGetLTM(FrameOf(atomic));
        if (!matrices || !skinToBone || !atomicLtm) {
            return;
        }
        if (!state.poseLogged) {
            state.poseLogged = true;
            Log(std::format("body: posing CJ's {} bones every frame (hierarchy flags 0x{:X})", state.boneCount, hierarchy->flags));
        }

        const float     ground = a_ped->GetDistanceFromCentreOfMassToBaseOfModel();
        const CVector   feet = a_ped->GetPosition() - CVector(0.0f, 0.0f, ground);
        const CVector   spawnFeet = a_spawn - CVector(0.0f, 0.0f, ground + a_lift);
        const auto      atomicFrame = ToFrame(*atomicLtm);
        Protocol::BodyState body{};
        body.frame = a_frame;
        body.flags = Protocol::kBodyValid;
        body.handover = a_handover;
        body.model = state.model;
        body.boneCount = state.boneCount;
        body.timeMs = Clock();
        for (std::uint32_t i = 0; i < state.boneCount; i++) {
            const auto world = Body::SkinToWorld(ToFrame(skinToBone[i]), ToFrame(matrices[i]), atomicFrame, local);
            body.bones[i] = Body::ToBone(world, Vec(feet));
        }
        // His pelvis joint, for seating him where Skyrim's player sits (Scenes).
        state.havePelvis = false;
        for (int i = 0; i < hierarchy->numNodes; i++) {
            if (hierarchy->pNodeInfo[i].nodeID == kBonePelvis) {
                const Body::Vec3 at{ matrices[i].pos.x, matrices[i].pos.y, matrices[i].pos.z };
                const auto       joint = local ? Body::Apply(atomicFrame, at) : at;
                const auto&      position = a_ped->GetPosition();
                state.pelvis = CVector(joint.x - position.x, joint.y - position.y, joint.z - position.z);
                state.havePelvis = true;
                break;
            }
        }
        const auto toSkyrim = [&](const CVector& a_point, float (&a_out)[3]) {
            const CVector d = a_point - spawnFeet;
            a_out[0] = d.x * Pose::kUnitsPerMeter;
            a_out[1] = d.y * Pose::kUnitsPerMeter;
            a_out[2] = d.z * Pose::kUnitsPerMeter;
        };
        toSkyrim(feet, body.feet);
        const auto& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
        toSkyrim(cam.m_vecSource, body.cameraPosition);
        const auto forward = Body::Normalize(Vec(cam.m_vecFront), { 0.0f, 1.0f, 0.0f });
        body.cameraForward[0] = forward.x;
        body.cameraForward[1] = forward.y;
        body.cameraForward[2] = forward.z;
        body.cameraFov = cam.m_fFOV;
        if (Body::Finite(body)) {
            GetLink().WriteBody(body);
        }
    }
}
