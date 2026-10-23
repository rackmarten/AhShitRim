#include "cj_gear.h"

#include "body.h"
#include "cj_body.h"
#include "cj_weapons.h"
#include "log.h"
#include "model_sender.h"
#include "parachute.h"
#include "pose.h"
#include "skin_model.h"
#include "vehicle_kinds.h"

#include <plugin.h>

#include <CAnimBlock.h>
#include <CAnimManager.h>
#include <CClumpModelInfo.h>
#include <CModelInfo.h>
#include <CPad.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CStreaming.h>
#include <CTaskSimpleJetpack.h>
#include <CTimer.h>
#include <CWorld.h>
#include <common.h>

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace AhShitRim::CjGear
{
    namespace
    {
        constexpr int kMoveIdle = 3;               // ANIM_ID_IDLE
        constexpr int kTaskPrimary = 3;            // TASK_PRIMARY_PRIMARY, over CJ's own on-foot task (where the jetpack cheat puts its task)
        constexpr int kJetpackModel = 370;         // MODEL_JETPACK
        constexpr std::uintptr_t kJetpackCtor = 0x67B4E0;  // CTaskSimpleJetPack::CTaskSimpleJetPack(const CVector*, float, int, CEntity*) (thiscall)
        constexpr int kIfpBase = 25575;            // RESOURCE_ID_IFP: an animation file's streaming id is this plus its block's index
        constexpr int kJetpackNode = 3;            // RenderJetPack's matrix: index 3 of his skeleton's (PED_NODE_LEFT_ARM, the spine in CJ's)
        constexpr float kHeightReach = 300.0f;     // metres looked down for the ground under a skydive
        const RwV3d     kJetpackNudge{ 0.1f, 0.08f, 0.0f };  // JETPACK_POS_OFFSET
        const RwV3d     kXAxis{ 1.0f, 0.0f, 0.0f };
        const RwV3d     kYAxis{ 0.0f, 1.0f, 0.0f };

        struct Model
        {
            std::uint32_t          id = 0;
            std::uint32_t          boneCount = 0;
            bool                   failed = false;
            std::vector<RpAtomic*> parts;  // the jetpack's, in bone order
            RpClump*               clump = nullptr;  // the instance the parts belong to
        };

        struct State
        {
            Parachute::State chute;
            std::string      playing;          // CJ's parachute.ifp animation now, empty for none (HoldCjAnim)
            bool             loop = false;
            CAnimBlendHierarchy* hierarchy = nullptr;  // its hierarchy, once found
            std::string      found;            // the animation hierarchy is
            RpClump*         canopy = nullptr; // the PARACHUTE model's instance, animated here
            std::string      canopyPlaying;
            int              canopyModel = -1;
            bool             blockLoaded = false;
            Model            jetpack;
            Model            chuteModel;
            std::uint32_t    builds = 0;
            bool             shown = false;    // the gear slot holds something
        } state;

        std::uint32_t NewId() { return (static_cast<std::uint32_t>(::GetTickCount64()) << 4 | (++state.builds & 0xF)) | 1; }

        Body::RwRows ToFrame(const RwMatrix& a_m)
        {
            return { { a_m.right.x, a_m.right.y, a_m.right.z }, { a_m.up.x, a_m.up.y, a_m.up.z }, { a_m.at.x, a_m.at.y, a_m.at.z },
                { a_m.pos.x, a_m.pos.y, a_m.pos.z } };
        }

        Pose::Vec3 Vec(const CVector& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        bool LoadModel(int a_model)
        {
            if (CStreaming::ms_aInfoForModel[a_model].m_nLoadState != 1) {  // LOADSTATE_LOADED
                CStreaming::RequestModel(a_model, 0x2 | 0x8);               // game required, keep in memory
                CStreaming::LoadAllRequestedModels(false);
            }
            return CStreaming::ms_aInfoForModel[a_model].m_nLoadState == 1;
        }

        // parachute.ifp, kept in memory once loaded (the script's REQUEST_ANIMATION "PARACHUTE").
        CAnimBlock* ParachuteBlock()
        {
            const int index = CAnimManager::GetAnimationBlockIndex("parachute");
            if (index < 0) {
                return nullptr;
            }
            CAnimBlock* block = &CAnimManager::ms_aAnimBlocks[index];
            if (!block->bLoaded) {
                CStreaming::RequestModel(kIfpBase + index, 0x2 | 0x8);
                CStreaming::LoadAllRequestedModels(false);
            }
            if (block->bLoaded && !state.blockLoaded) {
                state.blockLoaded = true;
                CAnimManager::AddAnimBlockRef(index);
                Log("gear: parachute.ifp loaded");
            }
            return block->bLoaded ? block : nullptr;
        }

        void Clear()
        {
            if (state.shown) {
                state.shown = false;
                GetLink().WriteGear(Protocol::BodyState{});
            }
        }

        void Write(const Protocol::BodyState& a_slot)
        {
            if (Body::Finite(a_slot)) {
                GetLink().WriteGear(a_slot);
                state.shown = true;
            }
        }

        Protocol::BodyState Slot(std::uint32_t a_flags, const Model& a_model, const CVector& a_feet, const CVector& a_spawnFeet, unsigned a_handover, unsigned a_frame)
        {
            Protocol::BodyState s{};
            s.frame = a_frame;
            s.flags = Protocol::kBodyValid | a_flags;
            s.handover = a_handover;
            s.model = a_model.id;
            s.boneCount = a_model.boneCount;
            s.timeMs = CjBody::NowMs();
            const CVector d = a_feet - a_spawnFeet;
            s.feet[0] = d.x * Pose::kUnitsPerMeter;
            s.feet[1] = d.y * Pose::kUnitsPerMeter;
            s.feet[2] = d.z * Pose::kUnitsPerMeter;
            s.cameraForward[1] = 1.0f;
            return s;
        }

        // ---- The jetpack ----

        CTaskSimpleJetPack* Jetpack(CPlayerPed* a_ped)
        {
            auto* task = a_ped->m_pIntelligence ? a_ped->m_pIntelligence->GetTaskJetPack() : nullptr;
            return task && !task->m_bIsFinished ? task : nullptr;
        }

        // What RenderJetPack does, short of drawing it: the clump on his back, the jet balls turned with the thrust
        // (wobbling while he stops), and the heading it turns them by kept for the next frame.
        void PlaceJetpack(CPlayerPed* a_ped, CTaskSimpleJetPack* a_task)
        {
            RpClump* clump = a_task->m_pJetPackClump;
            auto*    hierarchy = GetAnimHierarchyFromSkinClump(a_ped->m_pRwClump);
            RwMatrix back;
            if (!hierarchy || hierarchy->numNodes <= kJetpackNode || !CjBody::BoneWorld(a_ped, hierarchy->pNodeInfo[kJetpackNode].nodeID, back)) {
                return;
            }
            RwFrame*  root = RpClumpGetFrame(clump);
            RwMatrix* matrix = RwFrameGetMatrix(root);
            std::memcpy(matrix, &back, sizeof(RwMatrix));
            RwMatrixTranslate(matrix, &kJetpackNudge, rwCOMBINEPRECONCAT);
            RwMatrixRotate(matrix, &kYAxis, 90.0f, rwCOMBINEPRECONCAT);

            float heading = a_ped->m_fHeadingGoal;
            if (heading >= a_task->m_fOldHeading + 3.14159265f) {
                heading -= 6.28318531f;
            } else if (heading <= a_task->m_fOldHeading - 3.14159265f) {
                heading += 6.28318531f;
            }
            const float turn = std::clamp(heading - a_task->m_fOldHeading, -0.2f, 0.2f);
            const float pitch = std::clamp((a_task->m_fThrustAngle - turn * 10.0f) * 57.2957795f, -90.0f, 90.0f);
            for (const bool left : { true, false }) {
                RwFrame* ball = CClumpModelInfo::GetFrameFromName(clump, left ? "jetball1" : "jetball2");
                if (!ball) {
                    continue;
                }
                RwMatrix*   m = RwFrameGetMatrix(ball);
                const RwV3d at = m->pos;
                float       x = 0.0f, y = 0.0f;
                if (a_task->m_nThrustStop) {
                    const float t = static_cast<float>(CTimer::m_snTimeInMilliseconds) / 79.577469f;
                    x = std::sin(t) * (left ? 45.0f : -45.0f);
                    y = std::cos(t) * 45.0f;
                } else {
                    const float strafe = a_task->m_fThrustStrafe * 45.0f;
                    x = pitch;
                    y = left ? (std::max)(0.0f, strafe) : (std::min)(0.0f, strafe);
                }
                RwMatrixRotate(m, &kXAxis, x, rwCOMBINEREPLACE);
                RwMatrixRotate(m, &kYAxis, y, rwCOMBINEPRECONCAT);
                m->pos = at;
            }
            RwFrameUpdateObjects(root);
            a_task->m_fOldHeading = a_ped->m_fHeadingCurrent;
        }

        bool BuildJetpack(RpClump* a_clump)
        {
            Model& model = state.jetpack;
            model.clump = a_clump;
            if (model.id) {
                model.parts = CjWeapons::PartsOf(a_clump);  // a new instance of the model already sent
                return model.parts.size() >= model.boneCount;
            }
            const auto blob = CjWeapons::BuildModel(reinterpret_cast<RwObject*>(a_clump), 0, model.parts, "jetpack");
            if (blob.empty()) {
                model.failed = true;
                return false;
            }
            model.id = NewId();
            model.boneCount = static_cast<std::uint32_t>((std::min)(model.parts.size(), static_cast<std::size_t>(Protocol::kBodyBoneMax)));
            ModelSender::Set(ModelSender::kGearKey, model.id, SkinModel::Chunks(model.id, blob), "gear: the jetpack's model");
            return true;
        }

        bool UpdateJetpack(CPlayerPed* a_ped, const CVector& a_feet, const CVector& a_spawnFeet, unsigned a_handover, unsigned a_frame)
        {
            auto* task = Jetpack(a_ped);
            if (!task || !task->m_pJetPackClump || state.jetpack.failed) {
                return false;
            }
            if (task->m_pJetPackClump != state.jetpack.clump && !BuildJetpack(task->m_pJetPackClump)) {
                return false;
            }
            PlaceJetpack(a_ped, task);
            auto s = Slot(Protocol::kBodyJetpack, state.jetpack, a_feet, a_spawnFeet, a_handover, a_frame);
            for (std::uint32_t i = 0; i < s.boneCount; i++) {
                s.bones[i] = Body::ToBone(ToFrame(*RwFrameGetLTM(RpAtomicGetFrame(state.jetpack.parts[i]))), Vec(a_feet));
            }
            Write(s);
            return true;
        }

        // ---- The parachute ----

        bool HoldsChute(CPlayerPed* a_ped) { return a_ped->m_aWeapons[a_ped->m_nSelectedWepSlot].m_eWeaponType == WEAPONTYPE_PARACHUTE; }

        // Whether he carries it at all: San Andreas's script takes it out for him when he falls with it.
        bool CarriesChute(CPlayerPed* a_ped)
        {
            for (const auto& weapon : a_ped->m_aWeapons) {
                if (weapon.m_eWeaponType == WEAPONTYPE_PARACHUTE) {
                    return true;
                }
            }
            return false;
        }

        float HeightAboveGround(const CVector& a_feet)
        {
            CColPoint point;
            CEntity*  entity = nullptr;
            if (!CWorld::ProcessVerticalLine(a_feet + CVector(0.0f, 0.0f, 0.1f), a_feet.z - kHeightReach, point, entity, true, false, false, true, false, false, nullptr)) {
                return 1e9f;
            }
            return a_feet.z - point.m_vecPoint.z;
        }

        // CJ's animation for the phase (parachute.ifp's). The script played it as a task, but hidden San Andreas's own fall
        // (its in-air reaction) kept blending its flailing fall over a task's: so it is only asked for here, and HoldCjAnim
        // gives it all of his pose right before his animations update, every other animation none.
        void PlayOnCj(CPlayerPed* a_ped, std::string_view a_anim, bool a_loop)
        {
            if (a_anim.empty() && !state.playing.empty() && a_ped && a_ped->m_pRwClump) {
                // Over: the parachute's animation fades out and his own idle in; San Andreas's tasks take it from there.
                for (auto* assoc = RpAnimBlendClumpGetFirstAssociation(a_ped->m_pRwClump); assoc; assoc = RpAnimBlendGetNextAssociation(assoc)) {
                    if (assoc->m_pHierarchy == state.hierarchy) {
                        assoc->m_fBlendDelta = -8.0f;
                    }
                }
                CAnimManager::BlendAnimation(a_ped->m_pRwClump, a_ped->m_nAnimGroup, kMoveIdle, 8.0f);
            }
            state.playing = std::string(a_anim);
            state.loop = a_loop;
        }

        // The canopy's instance, its animation blending set up.
        RpClump* Canopy()
        {
            if (state.canopy) {
                return state.canopy;
            }
            if (state.canopyModel < 0) {
                CModelInfo::GetModelInfo("PARACHUTE", &state.canopyModel);
            }
            if (state.canopyModel < 0 || !LoadModel(state.canopyModel)) {
                return nullptr;
            }
            auto* info = CModelInfo::GetModelInfo(state.canopyModel);
            auto* object = info ? info->CreateInstance() : nullptr;
            if (!object || RwObjectGetType(object) != rpCLUMP) {
                Log(std::format("gear: model {} (PARACHUTE) made no clump", state.canopyModel));
                return nullptr;
            }
            state.canopy = reinterpret_cast<RpClump*>(object);
            if (!RpAnimBlendClumpIsInitialized(state.canopy)) {
                RpAnimBlendClumpInit(state.canopy);
            }
            Log(std::format("gear: the parachute's canopy made from model {}", state.canopyModel));
            return state.canopy;
        }

        void DropCanopy()
        {
            if (state.canopy) {
                RpAnimBlendClumpRemoveAllAssociations(state.canopy);
                RpClumpDestroy(state.canopy);
                state.canopy = nullptr;
            }
            state.canopyPlaying.clear();
        }

        // The canopy at CJ, as the script attached it to him, playing a_anim's _O twin; its skeleton posed.
        bool PoseCanopy(CPlayerPed* a_ped, std::string_view a_anim, bool a_loop, CAnimBlock* a_block)
        {
            RpClump* clump = Canopy();
            if (!clump) {
                return false;
            }
            if (state.canopyPlaying != a_anim) {
                const std::string name = std::string(a_anim) + "_O";
                if (auto* anim = CAnimManager::GetAnimation(name.c_str(), a_block)) {
                    CAnimManager::BlendAnimation(clump, anim, 0x1 | (a_loop ? 0x2 : 0x4), 4.0f);
                } else {
                    Log(std::format("gear: parachute.ifp has no {}", name));
                }
                state.canopyPlaying = std::string(a_anim);
            }
            RwFrame*  root = RpClumpGetFrame(clump);
            RwMatrix* matrix = RwFrameGetMatrix(root);
            const CMatrix& ped = a_ped->GetMatrix();
            matrix->right = { ped.right.x, ped.right.y, ped.right.z };
            matrix->up = { ped.up.x, ped.up.y, ped.up.z };
            matrix->at = { ped.at.x, ped.at.y, ped.at.z };
            matrix->pos = { ped.pos.x, ped.pos.y, ped.pos.z };
            RwMatrixUpdate(matrix);
            RwFrameUpdateObjects(root);
            if (!CTimer::m_UserPause) {
                RpAnimBlendClumpUpdateAnimations(clump, CTimer::ms_fTimeStep / 50.0f, true);
            }
            if (auto* hierarchy = GetAnimHierarchyFromSkinClump(clump)) {
                RpHAnimHierarchyUpdateMatrices(hierarchy);
            }
            return true;
        }

        bool UpdateParachute(CPlayerPed* a_ped, bool a_free, const CVector& a_feet, const CVector& a_spawnFeet, unsigned a_handover, unsigned a_frame)
        {
            auto&       pad = CPad::GetPad(0)->NewState;
            const auto& old = CPad::GetPad(0)->OldState;
            Parachute::Keys keys;
            keys.steer = pad.LeftStickX / 128.0f;
            keys.pitch = -pad.LeftStickY / 128.0f;
            keys.open = pad.ButtonCircle && !old.ButtonCircle;
            Parachute::Around around;
            around.holdsChute = CarriesChute(a_ped);
            around.standing = a_ped->bIsStanding;
            // In a vehicle is bInVehicle alone: m_pVehicle stays on the last one he got out of.
            around.free = a_free && !a_ped->bInVehicle && !Jetpack(a_ped);
            around.fall = -a_ped->m_vecMoveSpeed.z * 50.0f;
            // The line down is only tested when it can matter: he could start a skydive, or he is in one.
            const bool mayStart = around.holdsChute && !around.standing && around.fall >= Parachute::kStartFall;
            around.height = state.chute.phase != Parachute::Phase::Off || mayStart ? HeightAboveGround(a_feet) : 0.0f;
            const auto before = state.chute.phase;
            const float dt = CTimer::m_UserPause ? 0.0f : CTimer::ms_fTimeStep / 50.0f;
            state.chute = Parachute::Step(state.chute, around, keys, dt);
            const auto phase = state.chute.phase;
            if (before == Parachute::Phase::Off && phase == Parachute::Phase::Freefall && !HoldsChute(a_ped)) {
                a_ped->SetCurrentWeapon(WEAPONTYPE_PARACHUTE);  // into his hands for the skydive, as the script does
            }
            if (phase != before) {
                Log(std::format("gear: parachute {} -> {} ({:.0f} m up, falling {:.1f} m/s)", static_cast<int>(before), static_cast<int>(phase), around.height, around.fall));
            }
            if (state.chute.usedUp) {
                a_ped->ClearWeapon(WEAPONTYPE_PARACHUTE);  // as the script's REMOVE_WEAPON_FROM_CHAR once he has landed
                Log("gear: CJ landed, the parachute is used up");
            }
            CAnimBlock* block = phase != Parachute::Phase::Off ? ParachuteBlock() : nullptr;
            if (phase == Parachute::Phase::Off || !block) {
                if (before != Parachute::Phase::Off) {
                    PlayOnCj(a_ped, {}, false);
                    DropCanopy();
                }
                return false;
            }
            const auto anim = Parachute::AnimOf(phase, keys);
            const bool loop = Parachute::Loops(phase);
            PlayOnCj(a_ped, anim, loop);
            if (Parachute::Flying(phase)) {
                // He flies as the rules say: his heading turned, his speed set for the next physics step.
                const float heading = a_ped->m_fHeadingCurrent - state.chute.turn * dt;  // San Andreas's headings turn counter-clockwise
                a_ped->m_fHeadingCurrent = a_ped->m_fHeadingGoal = heading;
                a_ped->SetHeading(heading);
                const CVector forward(-std::sin(heading), std::cos(heading), 0.0f);
                const CVector speed = forward * state.chute.forward - CVector(0.0f, 0.0f, state.chute.fall);
                a_ped->m_vecMoveSpeed = speed / 50.0f;
            }
            if (!Parachute::CanopyOut(phase)) {
                return false;
            }
            if (!PoseCanopy(a_ped, anim, loop, block) || state.chuteModel.failed) {
                return false;
            }
            if (!state.chuteModel.id) {
                const auto blob = CjBody::ReadSkinnedModel(state.canopy, "parachute", Protocol::kSkinModelBody, state.chuteModel.boneCount, true);
                if (blob.empty()) {
                    state.chuteModel.failed = true;
                    return false;
                }
                state.chuteModel.id = NewId();
                ModelSender::Set(ModelSender::kGearKey + 1, state.chuteModel.id, SkinModel::Chunks(state.chuteModel.id, blob), "gear: the parachute's model");
            }
            auto s = Slot(Protocol::kBodyParachute, state.chuteModel, a_feet, a_spawnFeet, a_handover, a_frame);
            if (!CjBody::PoseSkinned(state.canopy, state.chuteModel.boneCount, a_feet, s)) {
                return false;
            }
            Write(s);
            return true;
        }
    }

    void Give(CPlayerPed* a_ped, std::uint16_t a_model)
    {
        if (a_model == VehicleKinds::kJetpack) {
            if (a_ped->bInVehicle) {
                Log("gear: CJ is in a vehicle, no jetpack");
                return;
            }
            if (Jetpack(a_ped)) {
                Log("gear: CJ has the jetpack on already");
                return;
            }
            if (!LoadModel(kJetpackModel)) {
                Log("gear: the jetpack's model did not load");
                return;
            }
            PlayOnCj(a_ped, {}, false);
            state.chute = {};
            DropCanopy();
            // What CCheat::JetpackCheat hands him, in the slot its script command event puts it in. San Andreas's
            // constructor takes a target entity too, which plugin-sdk's leaves out (it came in as garbage and crashed).
            auto* task = static_cast<CTaskSimpleJetPack*>(CTask::operator new(sizeof(CTaskSimpleJetPack)));
            reinterpret_cast<void(__thiscall*)(CTaskSimpleJetPack*, const CVector*, float, int, CEntity*)>(kJetpackCtor)(task, nullptr, 10.0f, 0, nullptr);
            a_ped->m_pIntelligence->m_TaskMgr.SetTask(task, kTaskPrimary, false);
            Log("gear: the jetpack on CJ");
        } else if (a_model == VehicleKinds::kParachute) {
            // What CCheat::ParachuteCheat does, in a vehicle too (it comes out when he bails out and falls); on the jetpack
            // he keeps flying, as in San Andreas.
            LoadModel(371);  // MODEL_GUN_PARA
            a_ped->GiveWeapon(WEAPONTYPE_PARACHUTE, 0, true);
            if (!a_ped->bInVehicle) {
                a_ped->SetCurrentWeapon(WEAPONTYPE_PARACHUTE);
            }
            ParachuteBlock();
            Log(a_ped->bInVehicle ? "gear: the parachute given to CJ in his vehicle" : "gear: the parachute in CJ's hands");
        }
    }

    void Update(CPlayerPed* a_ped, bool a_free, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame)
    {
        const float   ground = a_ped->GetDistanceFromCentreOfMassToBaseOfModel();
        const CVector feet = a_ped->GetPosition() - CVector(0.0f, 0.0f, ground);
        const CVector spawnFeet = a_spawn - CVector(0.0f, 0.0f, ground + a_lift);
        const bool    shown = UpdateParachute(a_ped, a_free, feet, spawnFeet, a_handover, a_frame) || UpdateJetpack(a_ped, feet, spawnFeet, a_handover, a_frame);
        if (!shown) {
            Clear();
        }
    }

    void HoldCjAnim(CPlayerPed* a_ped)
    {
        RpClump* clump = a_ped ? a_ped->m_pRwClump : nullptr;
        if (state.playing.empty() || !clump) {
            return;
        }
        CAnimBlock* block = ParachuteBlock();
        if (!block) {
            return;
        }
        if (state.found != state.playing) {
            state.found = state.playing;
            state.hierarchy = CAnimManager::GetAnimation(state.playing.c_str(), block);
            if (!state.hierarchy) {
                Log(std::format("gear: parachute.ifp has no {}", state.playing));
            }
        }
        if (!state.hierarchy) {
            return;
        }
        CAnimBlendAssociation* ours = nullptr;
        for (auto* assoc = RpAnimBlendClumpGetFirstAssociation(clump); assoc; assoc = RpAnimBlendGetNextAssociation(assoc)) {
            if (assoc->m_pHierarchy == state.hierarchy) {
                ours = assoc;
            }
        }
        if (!ours) {
            ours = CAnimManager::BlendAnimation(clump, state.hierarchy, 0x1 | (state.loop ? 0x2 : 0x4), 1000.0f);  // started, looped or held at its end
        }
        // Held as Parachute::HoldOf says: the parachute's at full blend, his face's as they are, his idle at a sliver (it
        // alone drives his face, which parachute.ifp doesn't), San Andreas's fall and everything else at none.
        // Which animations he has each time the parachute's changes (the grin on steering right was one of them).
        if (static std::string logged; logged != state.playing) {
            logged = state.playing;
            std::string list;
            for (auto* assoc = RpAnimBlendClumpGetFirstAssociation(clump); assoc; assoc = RpAnimBlendGetNextAssociation(assoc)) {
                list += std::format(" {}/{} 0x{:X} {:.2f}{}", assoc->m_nAnimGroup, assoc->m_nAnimId, assoc->m_nFlags, assoc->m_fBlendAmount, assoc == ours ? " ours" : "");
            }
            Log(std::format("gear: {} over CJ's animations (group/id flags blend):{}", state.playing, list));
        }
        bool haveIdle = false;
        for (auto* assoc = RpAnimBlendClumpGetFirstAssociation(clump); assoc; assoc = RpAnimBlendGetNextAssociation(assoc)) {
            haveIdle = haveIdle || Parachute::HoldOf(false, assoc->m_nAnimGroup, assoc->m_nAnimId, (assoc->m_nFlags & 0x10) != 0, a_ped->m_nAnimGroup) ==
                                       Parachute::Hold::Idle;
        }
        if (!haveIdle) {
            CAnimManager::AddAnimation(clump, a_ped->m_nAnimGroup, kMoveIdle);
        }
        for (auto* assoc = RpAnimBlendClumpGetFirstAssociation(clump); assoc; assoc = RpAnimBlendGetNextAssociation(assoc)) {
            switch (Parachute::HoldOf(assoc == ours, assoc->m_nAnimGroup, assoc->m_nAnimId, (assoc->m_nFlags & 0x10) != 0, a_ped->m_nAnimGroup)) {
            case Parachute::Hold::Full: assoc->m_fBlendAmount = 1.0f; break;
            case Parachute::Hold::Idle: assoc->m_fBlendAmount = Parachute::kIdleBlend; break;
            case Parachute::Hold::Keep: continue;
            case Parachute::Hold::None: assoc->m_fBlendAmount = 0.0f; break;
            }
            assoc->m_fBlendDelta = 0.0f;
        }
    }

    void Reset(CPlayerPed* a_ped)
    {
        if (state.chute.phase != Parachute::Phase::Off) {
            Log("gear: the skydive ends for the new handover");
        }
        state.chute = {};
        PlayOnCj(a_ped, {}, false);
        DropCanopy();
    }
}
