#include "crew_puppets.h"

#include "aim.h"
#include "body.h"
#include "cj_body.h"
#include "cj_weapons.h"
#include "log.h"
#include "model_sender.h"
#include "passengers.h"
#include "pose.h"
#include "scene.h"
#include "skin_model.h"

#include <plugin.h>

#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CAnimManager.h>
#include <CModelInfo.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CPools.h>
#include <CPopulation.h>
#include <CStreaming.h>
#include <CTimer.h>
#include <CWeapon.h>
#include <CWeaponInfo.h>
#include <CWorld.h>
#include <common.h>

#include <Windows.h>

#include <array>
#include <cmath>
#include <format>
#include <map>
#include <random>

namespace AhShitRim::CrewPuppets
{
    namespace
    {
        constexpr int kTaskPrimary = 3;       // TASK_PRIMARY_PRIMARY, as Scenes sets it
        constexpr int kGroupDefault = 0;      // ANIM_GROUP_DEFAULT
        constexpr int kMoveWalk = 0;          // ANIM_ID_WALK
        constexpr int kMoveRun = 1;           // ANIM_ID_RUN
        constexpr int kMoveIdle = 3;          // ANIM_ID_IDLE
        constexpr int kSeatIdle = 150;        // ANIM_DEFAULT_SEAT_IDLE
        constexpr int kShotFront = 15;        // ANIM_DEFAULT_KO_SHOT_FRONT_0
        constexpr int kBoneRightHand = 24;
        constexpr int kBonePelvis = 2;        // BONE_PELVIS
        constexpr unsigned short kAnimLooped = 0x2;
        constexpr float kStepSeconds = 0.02f;  // San Andreas steps in 1/50 s

        struct Puppet
        {
            CPed*                  ped = nullptr;
            std::uint32_t          id = 0;
            std::uint16_t          model = 0;
            std::uint8_t           weapon = 0;
            Scene::MotionTracker   motion;
            Scene::SeatMatch       seat;  // seated: his pelvis's offset from his position, to sit it on the seat
            bool                   sat = false;     // he sat since he last stood (for the logs)
            bool                   satLogged = false;
            float                  stood = 0.0f;    // seconds since he stood up while his seat anim fades
            Crew::Facing           facing;
            float                  heading = 0.0f;  // as he faced last
            int                    playing = -1;  // the Scene::Motion played, -1 before any
            bool                   stepping = false;  // a follower walking or running last frame, for the slip measured
            float                  stepTime = 0.0f;   // his walk's or run's time in its cycle then
            CVector                stepFeet;          // and his feet
            bool                   fallen = false;
            float                  fireWait = 0.0f;
            std::uint32_t          shots = 0;
        };

        struct ModelEntry
        {
            std::uint32_t id = 0;     // the id sent to Skyrim
            std::uint32_t bones = 0;  // a body's bone count, a weapon's parts
            bool          failed = false;
        };

        struct State
        {
            std::array<Puppet, Protocol::kCrewMax> puppets{};
            std::map<std::uint16_t, ModelEntry>    bodies;   // by ped model
            std::map<std::uint8_t, ModelEntry>     weapons;  // by weapon type
            unsigned                               handover = 0;
            bool                                   loggedFirst = false;
            std::uint32_t                          inputFrame = 0;   // Skyrim's frame the gaits and facings last moved on
            float                                  sinceInput = 0.0f;  // seconds of ours since
            std::mt19937                           rng{ 0x5EED };
            std::uint64_t                          statMs = 0;
            unsigned                               posed = 0, fired = 0, kept = 0;
            unsigned                               facedSamples = 0;  // followers on the move: how far off their way they face
            float                                  facedSum = 0.0f, facedMax = 0.0f;
            unsigned                               paced = 0, gaitChanges = 0;  // followers' walking or running frames; their gait changes
            float                                  groundSum = 0.0f, stepSum = 0.0f;  // metres they went, and their steps covered
        } state;

        Pose::Vec3 Vec(const CVector& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        bool LoadModel(int a_model)
        {
            if (a_model < 0) {
                return false;
            }
            if (CStreaming::ms_aInfoForModel[a_model].m_nLoadState != 1) {  // LOADSTATE_LOADED
                CStreaming::RequestModel(a_model, 0x2 | 0x8);               // game required, keep in memory
                CStreaming::LoadAllRequestedModels(false);
            }
            return CStreaming::ms_aInfoForModel[a_model].m_nLoadState == 1;
        }

        int WeaponModelOf(std::uint8_t a_weapon)
        {
            const auto* info = CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(a_weapon), 1);
            return info ? info->m_nModelId : -1;
        }

        void Remove(Puppet& a_puppet, const char* a_why)
        {
            if (a_puppet.ped) {
                if (CPools::ms_pPedPool->IsObjectValid(a_puppet.ped)) {
                    CPopulation::RemovePed(a_puppet.ped);  // as San Andreas takes its own peds out (groups, the world, the pool)
                }
                Log(std::format("crew: homie {:08X}'s puppet removed ({})", a_puppet.id, a_why));
            }
            a_puppet = Puppet{};
        }

        // The Families ped as a puppet: a mission ped San Andreas's population never takes, empty of decisions (no event
        // makes him act), no collision, no gravity, nothing of San Andreas's hurts him.
        CPed* Create(std::uint16_t a_model, std::uint8_t a_weapon, const CVector& a_at)
        {
            if (!LoadModel(a_model)) {
                Log(std::format("crew: ped model {} didn't load", a_model));
                return nullptr;
            }
            const int weaponModel = WeaponModelOf(a_weapon);
            if (weaponModel >= 0) {
                LoadModel(weaponModel);
            }
            CPed* ped = CPopulation::AddPed(PED_TYPE_GANG2, a_model, a_at, false);
            if (!ped) {
                Log(std::format("crew: San Andreas made no ped of model {}", a_model));
                return nullptr;
            }
            ped->SetCharCreatedBy(2);  // PED_MISSION
            if (ped->m_pIntelligence) {
                ped->m_pIntelligence->SetPedDecisionMakerType(eDecisionMakerType::PED_EMPTY);
                ped->m_pIntelligence->ClearTasks(true, true);
            }
            ped->bApplyGravity = false;
            ped->bBulletProof = true;
            ped->bFireProof = true;
            ped->bCollisionProof = true;
            ped->bMeleeProof = true;
            ped->bExplosionProof = true;
            ped->bInvulnerable = true;
            ped->bUsesCollision = false;
            ped->bDontAcceptIKLookAts = true;
            if (a_weapon) {
                ped->GiveWeapon(static_cast<eWeaponType>(a_weapon), 30000, true);
                ped->SetCurrentWeapon(static_cast<eWeaponType>(a_weapon));
            }
            return ped;
        }

        // His model, once per ped model, for Skyrim to build once and copy for every homie wearing it.
        ModelEntry* BodyModel(CPed* a_ped, std::uint16_t a_model)
        {
            auto& entry = state.bodies[a_model];
            if (entry.id || entry.failed) {
                return entry.failed ? nullptr : &entry;
            }
            std::uint32_t bones = 0;
            const auto    blob = CjBody::ReadSkinnedModel(a_ped->m_pRwClump, std::format("fam{}", a_model - 104), Protocol::kSkinModelBody, bones);
            if (blob.empty()) {
                entry.failed = true;
                return nullptr;
            }
            entry.id = ModelSender::NewId();
            entry.bones = bones;
            ModelSender::Set(ModelSender::kCrewBodyKey + (a_model - 105), entry.id, SkinModel::Chunks(entry.id, blob),
                std::format("crew: the fam{} model", a_model - 104));
            return &entry;
        }

        ModelEntry* WeaponModel(CPed* a_ped, std::uint8_t a_weapon)
        {
            auto* clump = reinterpret_cast<RpClump*>(a_ped->m_pWeaponObject);
            if (!a_weapon || !clump) {
                return nullptr;
            }
            auto& entry = state.weapons[a_weapon];
            if (entry.id || entry.failed) {
                return entry.failed ? nullptr : &entry;
            }
            std::vector<RpAtomic*> parts;
            const auto blob = CjWeapons::BuildModel(reinterpret_cast<RwObject*>(clump), a_weapon, parts);
            if (blob.empty()) {
                entry.failed = true;
                return nullptr;
            }
            entry.id = ModelSender::NewId();
            entry.bones = static_cast<std::uint32_t>(parts.size());
            ModelSender::Set(ModelSender::kCrewWeaponKey + (a_weapon % 16), entry.id, SkinModel::Chunks(entry.id, blob),
                std::format("crew: weapon {}'s model", a_weapon));
            return &entry;
        }

        // His seat fades out. It's a partial anim, and BlendAnimation only fades out those as partial as the one it
        // blends in, so his walk, run, idle or fall never would: he'd sit on top of them for good.
        void StandUp(CPed* a_ped)
        {
            if (auto* seat = RpAnimBlendClumpGetAssociation(a_ped->m_pRwClump, kSeatIdle)) {
                seat->m_fBlendDelta = -4.0f;  // SEAT_idle is blend-auto-remove: gone once faded
            }
        }

        // How far through its cycle a_assoc is, 0 to 1 (-1 for none).
        float PhaseOf(const CAnimBlendAssociation* a_assoc)
        {
            if (!a_assoc || !a_assoc->m_pHierarchy || a_assoc->m_pHierarchy->m_fTotalTime <= 0.0f) {
                return -1.0f;
            }
            return a_assoc->m_fCurrentTime / a_assoc->m_pHierarchy->m_fTotalTime;
        }

        // His walk, run, idle or seat, blended straight onto his clump the moment the gait changes. Not through a task:
        // a task's anim fades out (and is deleted) as the task goes, while the next task's anim only starts once San
        // Andreas processes him, and a clump left with nothing crashed San Andreas's anim update. From a walk to a run
        // or back the new one starts where the old one was in its stride, as San Andreas keeps its peds' in step, so the
        // feet don't jump as one fades into the other.
        void PlayMotion(CPed* a_ped, Scene::Motion a_motion)
        {
            a_ped->m_pIntelligence->m_TaskMgr.SetTask(nullptr, kTaskPrimary, false);
            auto*       clump = a_ped->m_pRwClump;
            if (a_motion != Scene::Motion::Seated) {
                StandUp(a_ped);
            }
            const bool  walk = a_motion == Scene::Motion::Walk;
            const float phase = walk || a_motion == Scene::Motion::Run ? PhaseOf(RpAnimBlendClumpGetAssociation(clump, walk ? kMoveRun : kMoveWalk)) : -1.0f;
            CAnimBlendAssociation* assoc = nullptr;
            switch (a_motion) {
            case Scene::Motion::Seated: CAnimManager::BlendAnimation(clump, kGroupDefault, kSeatIdle, 4.0f); break;
            case Scene::Motion::Walk: assoc = CAnimManager::BlendAnimation(clump, a_ped->m_nAnimGroup, kMoveWalk, 4.0f); break;
            case Scene::Motion::Run: assoc = CAnimManager::BlendAnimation(clump, a_ped->m_nAnimGroup, kMoveRun, 4.0f); break;
            default: CAnimManager::BlendAnimation(clump, a_ped->m_nAnimGroup, kMoveIdle, 4.0f); break;
            }
            if (assoc && phase >= 0.0f && assoc->m_pHierarchy) {
                assoc->SetCurrentTime(phase * assoc->m_pHierarchy->m_fTotalTime);
            }
        }

        // His walk or run, played at the rate that keeps his feet on the ground at a_speed m/s (Crew::Stride); null
        // standing.
        CAnimBlendAssociation* PaceMotion(CPed* a_ped, std::uint16_t a_model, Scene::Motion a_motion, float a_speed)
        {
            if (a_motion != Scene::Motion::Walk && a_motion != Scene::Motion::Run) {
                return nullptr;
            }
            auto* assoc = RpAnimBlendClumpGetAssociation(a_ped->m_pRwClump, a_motion == Scene::Motion::Walk ? kMoveWalk : kMoveRun);
            if (assoc) {
                assoc->m_fSpeed = Crew::Stride::Rate(a_model, a_motion, a_speed);
            }
            return assoc;
        }

        // His fall as the actor dies (San Andreas's shot-in-front knockout), held on its last frame.
        void Fall(Puppet& a_puppet)
        {
            a_puppet.fallen = true;
            a_puppet.ped->m_pIntelligence->m_TaskMgr.SetTask(nullptr, kTaskPrimary, false);
            StandUp(a_puppet.ped);
            if (auto* assoc = CAnimManager::BlendAnimation(a_puppet.ped->m_pRwClump, kGroupDefault, kShotFront, 8.0f)) {
                assoc->m_nFlags = static_cast<unsigned short>(assoc->m_nFlags & ~kAnimLooped);
                assoc->Start(0.0f);
            }
        }

        // Seconds between his shots (the weapon's own rate, slower as San Andreas's gang peds fire).
        float FireEvery(std::uint8_t a_weapon)
        {
            switch (a_weapon) {
            case Crew::kMicroSmg:
            case Crew::kTec9: return 0.12f;
            case Crew::kAk47: return 0.16f;
            default: return 0.4f;
            }
        }

        bool HandMatrix(CPed* a_ped, RwMatrix& a_out) { return CjBody::BoneWorld(static_cast<CPlayerPed*>(a_ped), kBoneRightHand, a_out); }

        // His gun on his right hand, as San Andreas puts it there when it draws a ped (RenderWeaponPedsForPC).
        bool PlaceGun(CPed* a_ped)
        {
            auto*    clump = reinterpret_cast<RpClump*>(a_ped->m_pWeaponObject);
            RwMatrix hand;
            if (!clump || !HandMatrix(a_ped, hand)) {
                return false;
            }
            RwFrame*  frame = RpClumpGetFrame(clump);
            RwMatrix* matrix = RwFrameGetMatrix(frame);
            std::memcpy(matrix, &hand, sizeof(RwMatrix));
            RwFrameUpdateObjects(frame);
            return true;
        }

        CVector Muzzle(CPed* a_ped)
        {
            RwMatrix hand;
            if (!HandMatrix(a_ped, hand)) {
                return a_ped->GetPosition() + CVector(0.0f, 0.0f, 0.5f);
            }
            const auto* info = CWeaponInfo::GetWeaponInfo(a_ped->m_aWeapons[a_ped->m_nSelectedWepSlot].m_eWeaponType, 1);
            const CVector offset = info ? CVector(info->m_vecFireOffset.x, info->m_vecFireOffset.y, info->m_vecFireOffset.z) : CVector();
            return CVector(hand.pos.x, hand.pos.y, hand.pos.z) + CVector(hand.right.x, hand.right.y, hand.right.z) * offset.x +
                   CVector(hand.up.x, hand.up.y, hand.up.z) * offset.y + CVector(hand.at.x, hand.at.y, hand.at.z) * offset.z;
        }

        // One shot at a_target: San Andreas's own fire (its sound, its ammo), whose bullet CjShots sends to Skyrim
        // with this homie as its shooter instead of testing it in San Andreas's world.
        void Shoot(Puppet& a_puppet, const CVector& a_target)
        {
            CPed* ped = a_puppet.ped;
            auto& weapon = ped->m_aWeapons[ped->m_nSelectedWepSlot];
            if (weapon.m_eWeaponType == WEAPONTYPE_UNARMED) {
                return;
            }
            weapon.m_nAmmoTotal = 30000;
            if (weapon.m_nAmmoInClip <= 1) {
                weapon.m_nAmmoInClip = 50;
            }
            weapon.m_nState = WEAPONSTATE_READY;
            weapon.m_nTimeForNextShot = 0;
            CVector muzzle = Muzzle(ped);
            CVector target = a_target;
            weapon.Fire(ped, &muzzle, &muzzle, nullptr, &target, nullptr);
            a_puppet.shots++;
            state.fired++;
        }
    }

    bool IsPuppet(const void* a_entity) { return SlotOf(a_entity) >= 0; }

    int SlotOf(const void* a_entity)
    {
        if (!a_entity) {
            return -1;
        }
        for (std::uint32_t i = 0; i < Protocol::kCrewMax; i++) {
            if (state.puppets[i].ped == a_entity) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    float SpreadRadians(std::uint32_t a_seed)
    {
        // gang peds are poor shots: up to 2.5 degrees off, deterministic per shot
        const float t = static_cast<float>((a_seed * 2654435761u) >> 16 & 0xFFFF) / 65535.0f;
        return (t - 0.5f) * 2.0f * 2.5f * Pose::kPi / 180.0f;
    }

    void KeepAnimated(void* a_ped, float a_step)
    {
        auto*                       ped = static_cast<CPed*>(a_ped);
        std::array<Crew::Blend, 16> blends{};
        std::size_t                 count = 0;
        for (auto* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc && count < blends.size();
             assoc = RpAnimBlendGetNextAssociation(assoc)) {
            blends[count++] = { assoc->m_fBlendAmount, assoc->m_fBlendDelta, (assoc->m_nFlags & Crew::kBlendDeleteFaded) != 0,
                assoc->m_pHierarchy && assoc->m_pHierarchy->m_pSequences };
        }
        if (count == blends.size() || Crew::LeavesANode(blends.data(), count, a_step)) {
            return;  // as many as that: some outlive the update
        }
        CAnimManager::AddAnimation(ped->m_pRwClump, ped->m_nAnimGroup, kMoveIdle);
        if (state.kept++ == 0) {
            Log(std::format("crew: the puppet in slot {} had no animation to outlive its update ({} fading out); his idle put on", SlotOf(ped), count));
        }
    }

    void Clear()
    {
        for (auto& puppet : state.puppets) {
            Remove(puppet, "the crew is cleared");
        }
    }

    void Update(CPlayerPed* a_cj, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame, bool a_owned)
    {
        Protocol::CrewInput input{};
        const bool have = a_owned && GetLink().ReadCrewInput(input) && input.handover == a_handover;
        if (a_handover != state.handover) {
            state.handover = a_handover;
            Clear();  // a re-anchor: Skyrim sends them again for the new anchor
        }
        if (!a_owned) {
            Clear();
            return;
        }
        const float  ground = a_cj->GetDistanceFromCentreOfMassToBaseOfModel();
        const CVector spawnFeet = a_spawn - CVector(0.0f, 0.0f, ground + a_lift);
        const float  seconds = CTimer::ms_fTimeStep * kStepSeconds;
        // Gaits and facings move only on a frame of Skyrim's they haven't seen, over the time since the last one: while
        // Skyrim stalls (an F11 screenshot holds its frame a fifth of a second) the homies would seem to stop and then
        // leap, and their gait flipped from run to walk and back.
        const bool  freshInput = have && input.frame != state.inputFrame;
        state.sinceInput += seconds;
        const float tracked = state.sinceInput;
        if (freshInput) {
            state.inputFrame = input.frame;
            state.sinceInput = 0.0f;
        }
        const auto   toSa = [&](const float (&a_rel)[3]) {
            return spawnFeet + CVector(a_rel[0] * Pose::kMetersPerUnit, a_rel[1] * Pose::kMetersPerUnit, a_rel[2] * Pose::kMetersPerUnit);
        };

        for (std::uint32_t i = 0; i < Protocol::kCrewMax; i++) {
            auto&       puppet = state.puppets[i];
            const auto& member = input.members[i];
            const bool  present = have && (member.flags & Protocol::kCrewPresent) != 0 && member.id != 0;
            if (!present) {
                if (puppet.ped && have) {
                    Remove(puppet, "Skyrim let him go");
                }
                if (!have) {
                    continue;  // a torn read: as they were
                }
                Protocol::CrewBody none{};
                GetLink().WriteCrew(i, none);
                continue;
            }
            if (puppet.ped && (puppet.id != member.id || puppet.model != member.model || !CPools::ms_pPedPool->IsObjectValid(puppet.ped))) {
                Remove(puppet, "another homie took the slot");
            }
            const CVector feet = toSa(member.feet);
            if (!puppet.ped) {
                puppet.ped = Create(member.model, member.weapon, feet + CVector(0.0f, 0.0f, 1.0f));
                if (!puppet.ped) {
                    continue;
                }
                puppet.id = member.id;
                puppet.model = member.model;
                puppet.weapon = member.weapon;
                if (!state.loggedFirst) {
                    state.loggedFirst = true;
                    Log(std::format("crew: the first homie's puppet ({:08X}, fam{}, weapon {}) made at ({:.1f},{:.1f},{:.1f})", member.id, member.model - 104,
                        member.weapon, feet.x, feet.y, feet.z));
                }
            }
            CPed* ped = puppet.ped;
            if (member.weapon != puppet.weapon && member.weapon) {
                LoadModel(WeaponModelOf(member.weapon));
                ped->GiveWeapon(static_cast<eWeaponType>(member.weapon), 30000, true);
                ped->SetCurrentWeapon(static_cast<eWeaponType>(member.weapon));
                puppet.weapon = member.weapon;
            }

            const bool dead = (member.flags & Protocol::kCrewDead) != 0;
            const bool seated = (member.flags & Protocol::kCrewSeated) != 0;
            const bool aiming = !dead && (member.flags & (Protocol::kCrewAiming | Protocol::kCrewFiring)) != 0;
            const bool track = freshInput || puppet.playing < 0;
            auto       motion = puppet.playing >= 0 ? static_cast<Scene::Motion>(puppet.playing) : Scene::Motion::Idle;
            if (!dead && track) {
                // San Andreas's gaits at his pace, changed where its paces meet (Crew::Stride), not the scenes' 3.5 m/s.
                const auto was = puppet.playing >= 0 ? static_cast<Scene::Motion>(puppet.playing) : Scene::Motion::Idle;
                motion = puppet.motion.Update(seated, Vec(feet), tracked);
                if (motion != Scene::Motion::Seated) {
                    motion = Crew::Stride::Next(was == Scene::Motion::Seated ? Scene::Motion::Idle : was, puppet.motion.Speed());
                }
            }
            const bool moving = motion == Scene::Motion::Walk || motion == Scene::Motion::Run;

            // Where Skyrim says, facing the way he goes while he walks or runs, else as Skyrim says (his aim, CJ's
            // heading for a follower, his own at a spot; seated or dead at once).
            const float still = Pose::SanAndreasHeading(member.heading);
            if (track) {
                puppet.heading = puppet.facing.Update(Vec(feet), tracked, still, moving, aiming);
            }
            float heading = puppet.heading;
            if (seated || dead) {
                heading = still;
            } else if (track && moving && !aiming && (member.flags & Protocol::kCrewFollower) && puppet.facing.Speed() >= Crew::Facing::kMinSpeed) {
                const float off = std::fabs(Scene::TurnToward(0.0f, heading - puppet.facing.Travel(), Pose::kPi));
                state.facedSum += off;
                state.facedMax = (std::max)(state.facedMax, off);
                state.facedSamples++;
            }
            // Seated, his pelvis on the seat, not his feet on his actor's: San Andreas's sitting anim lowers him backwards
            // from where he stands onto a chair behind him, which put him a head too high (through a car's roof) and
            // behind his seat (beside a bike). His actor's feet are kSeatDrop under the seat, where the hips go.
            const bool  sits = !dead && motion == Scene::Motion::Seated;
            const float lift = ped->GetDistanceFromCentreOfMassToBaseOfModel();
            CVector     at = feet + CVector(0.0f, 0.0f, lift);
            if (!sits) {
                puppet.seat.Reset();
            } else if (puppet.seat.Have()) {
                const float surface = Passengers::kSeatDrop * Pose::kMetersPerUnit - Scene::SeatMatch::kPelvisOverSeat;
                const auto  place = puppet.seat.Place(Vec(feet + CVector(0.0f, 0.0f, surface)), lift);
                at = CVector(place.x, place.y, place.z);
            }
            ped->SetPosn(at);
            ped->SetHeading(heading);
            ped->m_fHeadingCurrent = heading;
            ped->m_fHeadingGoal = heading;
            ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
            ped->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
            ped->m_fHealth = 100.0f;

            if (dead && !puppet.fallen) {
                Fall(puppet);
            } else if (!dead) {
                puppet.fallen = false;
                const bool follower = (member.flags & Protocol::kCrewFollower) != 0;
                const bool changed = static_cast<int>(motion) != puppet.playing;
                if (changed) {
                    state.gaitChanges += follower && puppet.playing >= 0 ? 1 : 0;
                    puppet.playing = static_cast<int>(motion);
                    PlayMotion(ped, motion);
                }
                // The slip measured, not assumed: the ground he went this frame against what his steps covered, his
                // walk's or run's time through its cycle (as San Andreas played it, at whatever rate) times its pace.
                auto* step = PaceMotion(ped, member.model, motion, puppet.motion.Speed());
                if (step && step->m_pHierarchy && follower && puppet.stepping && !changed) {
                    const float cycle = step->m_pHierarchy->m_fTotalTime;
                    float       played = step->m_fCurrentTime - puppet.stepTime;
                    played += played < 0.0f ? cycle : 0.0f;
                    const float went = std::hypot(feet.x - puppet.stepFeet.x, feet.y - puppet.stepFeet.y);
                    if (went < 1.0f && played < cycle * 0.5f) {  // not a jump or a load
                        state.groundSum += went;
                        state.stepSum += played * Crew::Stride::Natural(member.model, motion);
                        state.paced++;
                    }
                }
                puppet.stepping = step != nullptr && follower;
                puppet.stepTime = step ? step->m_fCurrentTime : 0.0f;
                puppet.stepFeet = feet;
            }

            // His bones, posed as CJ's are (hidden, San Andreas poses nobody), his arm on his aim, his gun on his hand.
            ped->bDontUpdateHierarchy = false;
            ped->UpdateRwMatrix();
            ped->UpdateRwFrame();
            ped->UpdateRpHAnim();
            if (sits) {
                RwMatrix   pelvis;
                const bool posed = CjBody::BoneWorld(static_cast<CPlayerPed*>(ped), kBonePelvis, pelvis);
                const auto from = ped->GetPosition();
                puppet.seat.Update(posed, pelvis.pos.x - from.x, pelvis.pos.y - from.y, pelvis.pos.z - from.z, seconds);
                // Once, sat down: how far his pelvis is from the seat (the ridecrew hit test reads it).
                const auto* anim = RpAnimBlendClumpGetAssociation(ped->m_pRwClump, kSeatIdle);
                puppet.sat = true;
                puppet.stood = 0.0f;
                if (!puppet.satLogged && posed && anim && anim->m_fBlendAmount >= 0.99f) {
                    puppet.satLogged = true;
                    const CVector off = CVector(pelvis.pos.x, pelvis.pos.y, pelvis.pos.z) - feet -
                                        CVector(0.0f, 0.0f, Passengers::kSeatDrop * Pose::kMetersPerUnit);
                    Log(std::format("crew: homie {:08X} sits with his pelvis ({:.2f}, {:.2f}, {:.2f}) m from his seat{}", member.id, off.x, off.y, off.z,
                        std::hypot(off.x, off.y, off.z) <= 0.1f ? ": on it" : ": OFF HIS SEAT"));
                }
            } else if (puppet.sat) {
                // Stood up (or fell): his seat anim must be gone within its fade.
                const auto* anim = RpAnimBlendClumpGetAssociation(ped->m_pRwClump, kSeatIdle);
                puppet.stood += seconds;
                if (!anim || anim->m_fBlendAmount <= 0.0f || puppet.stood >= 2.0f) {
                    Log(anim && anim->m_fBlendAmount > 0.0f
                            ? std::format("crew: homie {:08X} STILL SITS {:.1f} s after he stood up (seat blend {:.2f})", member.id, puppet.stood, anim->m_fBlendAmount)
                            : std::format("crew: homie {:08X} stood up, his seat anim gone in {:.2f} s", member.id, puppet.stood));
                    puppet.sat = false;
                    puppet.satLogged = false;
                }
            }
            const CVector aim = toSa(member.aim);
            if (aiming) {
                CjBody::AimRightArm(ped->m_pRwClump, ped->GetForward(), aim);
            }
            PlaceGun(ped);

            if (aiming && (member.flags & Protocol::kCrewFiring) != 0) {
                puppet.fireWait -= seconds;
                if (puppet.fireWait <= 0.0f) {
                    puppet.fireWait = FireEvery(puppet.weapon);
                    Shoot(puppet, aim);
                }
            } else {
                puppet.fireWait = 0.0f;
            }

            Protocol::CrewBody out{};
            out.id = member.id;
            auto* body = BodyModel(ped, member.model);
            const CVector pedFeet = feet;  // his bones go from his actor's feet, where Skyrim draws them (seated, not his own)
            if (body && CjBody::PoseSkinned(ped->m_pRwClump, body->bones, pedFeet, out.body)) {
                out.body.frame = a_frame;
                out.body.flags = Protocol::kBodyValid;
                out.body.handover = a_handover;
                out.body.model = body->id;
                out.body.timeMs = CjBody::NowMs();
                const CVector d = pedFeet - spawnFeet;
                out.body.feet[0] = d.x * Pose::kUnitsPerMeter;
                out.body.feet[1] = d.y * Pose::kUnitsPerMeter;
                out.body.feet[2] = d.z * Pose::kUnitsPerMeter;
                out.body.cameraForward[1] = 1.0f;
                if (auto* gun = WeaponModel(ped, puppet.weapon)) {
                    auto*      clump = reinterpret_cast<RpClump*>(ped->m_pWeaponObject);
                    const auto parts = CjWeapons::PartsOf(clump);
                    if (parts.size() == gun->bones) {
                        auto& w = out.weapon;
                        w = {};
                        w.frame = a_frame;
                        w.flags = Protocol::kBodyValid;
                        w.handover = a_handover;
                        w.model = gun->id;
                        w.boneCount = gun->bones;
                        w.timeMs = out.body.timeMs;
                        std::copy_n(out.body.feet, 3, w.feet);
                        w.cameraForward[1] = 1.0f;
                        for (std::uint32_t b = 0; b < gun->bones; b++) {
                            w.bones[b] = Body::ToBone(CjBody::Rows(*RwFrameGetLTM(RpAtomicGetFrame(parts[b]))), Vec(pedFeet));
                        }
                    }
                }
                if (Body::Finite(out.body) && (out.weapon.flags == 0 || Body::Finite(out.weapon))) {
                    GetLink().WriteCrew(i, out);
                    state.posed++;
                }
            }
        }

        const std::uint64_t now = ::GetTickCount64();
        if (now - state.statMs >= 5000) {
            if (state.statMs && state.posed) {
                int puppets = 0;
                int gaits[4]{};
                for (const auto& p : state.puppets) {
                    puppets += p.ped ? 1 : 0;
                    if (p.ped && p.playing >= 0 && p.playing < 4) {
                        gaits[p.playing]++;
                    }
                }
                Log(std::format("crew: {} puppets ({} idle, {} walking, {} running, {} seated), {} poses sent, {} shots fired and {} empty anim "
                                "updates kept in the last 5 s",
                    puppets, gaits[0], gaits[1], gaits[2], gaits[3], state.posed, state.fired, state.kept));
                if (state.facedSamples) {
                    Log(std::format("crew: moving followers faced {:.0f} degrees off their way on average, {:.0f} at most ({} frames)",
                        state.facedSum / state.facedSamples * 180.0f / Pose::kPi, state.facedMax * 180.0f / Pose::kPi, state.facedSamples));
                }
                if (state.paced) {
                    Log(std::format("crew: moving followers went {:.1f} m on the ground and their steps covered {:.1f} m ({} frames, {} gait changes)",
                        state.groundSum, state.stepSum, state.paced, state.gaitChanges));
                }
            }
            state.statMs = now;
            state.posed = state.fired = state.kept = 0;
            state.facedSamples = 0;
            state.facedSum = state.facedMax = 0.0f;
            state.paced = state.gaitChanges = 0;
            state.groundSum = state.stepSum = 0.0f;
        }
    }
}
