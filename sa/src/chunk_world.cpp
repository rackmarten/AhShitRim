#include "chunk_world.h"

#include "cj_pickups.h"
#include "cj_cheats.h"
#include "cj_projectiles.h"
#include "cj_weapons.h"

#include "fall_rescue.h"
#include "ground.h"
#include "log.h"
#include "position_guard.h"
#include "vehicles.h"

#include <plugin.h>

#include <CAutomobile.h>
#include <CBike.h>
#include <CBuilding.h>
#include <CCamera.h>
#include <CColModel.h>
#include <CColPoint.h>
#include <CModelInfo.h>
#include <CPad.h>
#include <CPlayerPed.h>
#include <CPools.h>
#include <CTimer.h>
#include <CWorld.h>

#include <Windows.h>

#include <cmath>
#include <deque>
#include <format>
#include <string>
#include <vector>

namespace AhShitRim::ChunkWorld
{
    namespace
    {
        // One collision object per chunk San Andreas keeps.
        constexpr int kSlots = (2 * Ground::kKeepRadius + 1) * (2 * Ground::kKeepRadius + 1);

        // A building takes its collision from its model info, so each slot needs a model of its
        // own. San Andreas's store of atomic model infos is full (CModelInfo::AddAtomicModel
        // crashed), so each slot borrows one of the map's atomic models for good: the map is
        // emptied, nothing else uses them. From the top of the map's ids down, clear of vehicle
        // upgrades and pickups (below 2000).
        constexpr int kFirstBorrowedModel = 2000;
        constexpr int kLastBorrowedModel = 18630;

        constexpr std::size_t kMaxWaiting = 64;  // chunks of a handover San Andreas hasn't read yet

        // Every chunk triangle's surface: collision chunks carry no material yet, and on tarmac cars
        // and bikes grip like on a road everywhere (the bumps still come from the ground's shape).
        constexpr unsigned char kSurface = 1;  // SURFACE_TARMAC

        struct Slot
        {
            int        model = -1;
            CBuilding* building = nullptr;
            CColModel* col = nullptr;  // the borrowed model's own collision model, filled with each chunk the slot gets
            bool       inWorld = false;
        };

        struct State
        {
            Ground::Origin    origin;
            Ground::ChunkBook book{ kSlots };
            Slot              slots[kSlots];
            int               nextModel = kLastBorrowedModel;
            std::deque<std::vector<std::uint8_t>> waiting;

            bool                  ready = false;  // the chunk under the handover spot is in
            std::uint64_t         beganMs = 0;
            FallRescue::Watch     fall;
            PositionGuard::Watch  guard;
            CVehicle*             falling = nullptr;  // what fall watches: CJ's vehicle, or CJ himself (nullptr)

            // Stats, logged every 5 s while chunks come in.
            int           built = 0, replaced = 0, triangles = 0, dropped = 0, stale = 0, distant = 0;
            std::uint64_t statMs = 0;
        } state;

        // A plain map model: atomic (not timed, LOD, a weapon or a vehicle part), no special type
        // (glass, breakable), no 2D effects (lights, particles).
        bool Borrowable(CBaseModelInfo* a_info)
        {
            return a_info && a_info->GetModelType() == MODEL_INFO_ATOMIC && a_info->nSpecialType == 0 && a_info->m_nNum2dEffects == 0;
        }

        int BorrowModel()
        {
            while (state.nextModel >= kFirstBorrowedModel && !Borrowable(CModelInfo::GetModelInfo(state.nextModel))) {
                --state.nextModel;
            }
            return state.nextModel >= kFirstBorrowedModel ? state.nextModel-- : -1;
        }

        // Out of the world, but the building keeps its collision until the slot gets a new chunk:
        // CJ still refers to the building he last stood on, and San Andreas crashed on the next
        // physics step when its collision was gone (the second handover dropped them all).
        void TakeOut(Slot& a_slot)
        {
            if (a_slot.inWorld) {
                CWorld::Remove(a_slot.building);
                a_slot.inWorld = false;
            }
        }

        // a_col filled with a chunk's mesh, whatever it held before (its volumes freed first).
        void FillColModel(CColModel* a_col, const Ground::Mesh& a_mesh)
        {
            const int vertices = static_cast<int>(a_mesh.vertices.size() / 3);
            const int triangles = static_cast<int>(a_mesh.triangles.size() / 3);
            auto*     col = a_col;
            col->RemoveCollisionVolumes();
            col->AllocateData(0, 0, 0, vertices, triangles, false);
            auto* data = col->m_pColData;
            for (int v = 0; v < vertices; ++v) {
                data->m_pVertices[v].x = a_mesh.vertices[v * 3];
                data->m_pVertices[v].y = a_mesh.vertices[v * 3 + 1];
                data->m_pVertices[v].z = a_mesh.vertices[v * 3 + 2];
            }
            for (int t = 0; t < triangles; ++t) {
                auto& tri = data->m_pTriangles[t];
                tri.m_nVertA = a_mesh.triangles[t * 3];
                tri.m_nVertB = a_mesh.triangles[t * 3 + 1];
                tri.m_nVertC = a_mesh.triangles[t * 3 + 2];
                tri.m_nMaterial = kSurface;
                tri.m_nLight = 0;
            }
            const CVector min(a_mesh.min.x, a_mesh.min.y, a_mesh.min.z);
            const CVector max(a_mesh.max.x, a_mesh.max.y, a_mesh.max.z);
            col->m_boundBox.m_vecMin = min;
            col->m_boundBox.m_vecMax = max;
            const CVector center = (min + max) * 0.5f;
            col->m_boundSphere.m_vecCenter = center;
            col->m_boundSphere.m_fRadius = (max - center).Magnitude();
            col->m_nColSlot = 0;
        }

        void Build(const Ground::Chunk& a_chunk)
        {
            const auto&     h = a_chunk.header;
            const Ground::Key key{ h.chunkX, h.chunkY };
            const auto      mesh = Ground::BuildMesh(a_chunk, state.origin);
            state.dropped += mesh.dropped;
            if (mesh.triangles.empty()) {
                return;  // nothing to stand on (yet): Skyrim sends it again once it has more
            }
            bool      replaced = false;
            const int index = state.book.Place(key, replaced);
            if (index < 0) {
                Log(std::format("ground: no slot left for chunk ({},{})", key.x, key.y));
                return;
            }
            Slot& slot = state.slots[index];
            if (slot.model < 0) {
                slot.model = BorrowModel();
                if (slot.model < 0) {
                    Log("ground: no model left to borrow for a chunk");
                    return;
                }
                slot.building = new CBuilding();
                slot.building->SetModelIndexNoCreate(slot.model);
                slot.building->bUsesCollision = true;
                slot.building->bIsVisible = false;  // nothing to draw, and invisible entities are never streamed
                slot.building->bDontStream = true;
                slot.building->m_nAreaCode = 0;
            }
            // San Andreas's pool of collision models (10150) is all but full of the emptied map's own, a few left: a
            // slot fills its borrowed model's own collision model with each chunk rather than taking one from the pool
            // per chunk (past about 90 chunks the pool ran out and San Andreas crashed on a long flight).
            auto* info = CModelInfo::GetModelInfo(slot.model);
            if (!slot.col) {
                slot.col = info->m_pColModel;
                if (!slot.col) {
                    slot.col = new CColModel();
                    if (!slot.col) {
                        Log(std::format("ground: San Andreas's collision models are all taken, chunk ({},{}) not built", key.x, key.y));
                        return;
                    }
                    info->SetColModel(slot.col, true);
                }
            }
            TakeOut(slot);
            FillColModel(slot.col, mesh);  // its triangle planes are freed with its volumes
            slot.building->SetPosn(CVector(mesh.position.x, mesh.position.y, mesh.position.z));
            CWorld::Add(slot.building);
            slot.inWorld = true;
            state.built++;
            state.replaced += replaced;
            state.triangles += static_cast<int>(mesh.triangles.size() / 3);
        }

        void Handle(const std::uint8_t* a_data, std::uint32_t a_size)
        {
            Ground::Chunk chunk;
            if (!Ground::ParseChunk(a_data, a_size, chunk)) {
                Log(std::format("ground: malformed collision chunk ({} bytes), dropped", a_size));
                return;
            }
            switch (state.book.Classify(chunk.header.generation)) {
            case Ground::ChunkBook::Verdict::Current:
                Build(chunk);
                break;
            case Ground::ChunkBook::Verdict::Future:
                if (state.waiting.size() >= kMaxWaiting) {
                    state.waiting.pop_front();
                }
                state.waiting.emplace_back(a_data, a_data + a_size);
                break;
            case Ground::ChunkBook::Verdict::Stale:
                state.stale++;
                break;
            }
        }

        void Shift(CPlayerPed* a_ped, CVector& a_spawn, const Ground::Vec3& a_shift)
        {
            const CVector shift(a_shift.x, a_shift.y, a_shift.z);
            const CVector speed = a_ped->m_vecMoveSpeed;
            a_ped->SetPosn(a_ped->GetPosition() - shift);
            a_ped->m_vecMoveSpeed = speed;
            a_spawn -= shift;
            for (auto& slot : state.slots) {
                if (slot.inWorld) {
                    CWorld::Remove(slot.building);
                    slot.building->SetPosn(slot.building->GetPosition() - shift);
                    CWorld::Add(slot.building);
                }
            }
            // The camera follows CJ from where it was: move it with him, so it doesn't swing round.
            for (auto& cam : TheCamera.m_aCams) {
                cam.m_vecSource -= shift;
                cam.m_vecSourceBeforeLookBehind -= shift;
                cam.m_vecTargetCoorsForFudgeInter -= shift;
            }
            TheCamera.m_vecGameCamPos -= shift;
            TheCamera.GetPosition() -= shift;
            Vehicles::Shift(shift);
            state.fall.Shift(a_shift);
            state.guard.Shift(a_shift);
            Log(std::format("ground: origin moved by ({:.0f},{:.0f},{:.0f}) m with CJ", a_shift.x, a_shift.y, a_shift.z));
        }

        Ground::Vec3 ToVec(const CVector& a_v)
        {
            return { a_v.x, a_v.y, a_v.z };
        }

        bool Finite(const CVector& a_v) { return PositionGuard::Finite(ToVec(a_v)); }

        void Mend(CVector& a_v, const CVector& a_to)
        {
            if (!Finite(a_v)) {
                a_v = a_to;
            }
        }

        void Mend(float& a_f, float a_to)
        {
            if (!std::isfinite(a_f)) {
                a_f = a_to;
            }
        }

        // Whatever of San Andreas's camera isn't a number any more, from round a_at: the origin moves
        // took the camera along, and a NaN one stayed NaN (CJ walks where the camera faces).
        void MendCamera(const CVector& a_at)
        {
            const CVector behind = a_at + CVector(0.0f, -4.0f, 1.0f);
            const CVector front(0.0f, 1.0f, 0.0f), up(0.0f, 0.0f, 1.0f), none(0.0f, 0.0f, 0.0f);
            for (auto& cam : TheCamera.m_aCams) {
                Mend(cam.m_vecSource, behind);
                Mend(cam.m_vecSourceBeforeLookBehind, behind);
                Mend(cam.m_vecTargetCoorsForFudgeInter, a_at);
                Mend(cam.m_vecFront, front);
                Mend(cam.m_vecUp, up);
                Mend(cam.m_vecSourceSpeedOverOneFrame, none);
                Mend(cam.m_vecTargetSpeedOverOneFrame, none);
                Mend(cam.m_vecUpOverOneFrame, none);
                Mend(cam.m_vecBufferedPlayerBodyOffset, none);
                Mend(cam.m_vecLastAboveWaterCamPosition, behind);
                for (auto& v : cam.m_avecPreviousVectors) {
                    Mend(v, none);
                }
                for (auto& v : cam.m_avecTargetHistoryPos) {
                    Mend(v, a_at);
                }
                Mend(cam.m_fHorizontalAngle, 0.0f);
                Mend(cam.m_fVerticalAngle, 0.0f);
                Mend(cam.m_fAlphaSpeed, 0.0f);
                Mend(cam.m_fBetaSpeed, 0.0f);
            }
            Mend(TheCamera.m_vecGameCamPos, behind);
            Mend(TheCamera.GetPosition(), behind);
        }

        // CJ's position broke (PositionGuard): out of a broken vehicle and back to a sound spot.
        void Recover(CPlayerPed* a_ped, CVector& a_spawn, const PositionGuard::Recovery& a_recovery)
        {
            const CVector from = a_ped->GetPosition();
            const CVector at(a_recovery.at.x, a_recovery.at.y, a_recovery.at.z);
            Vehicles::Recover(a_ped, at);
            if (!PositionGuard::Sound(ToVec(a_spawn))) {
                a_spawn = at;
            }
            const float heading = a_ped->m_fHeadingCurrent;
            Place(a_ped, at, std::isfinite(heading) ? heading : 0.0f);
            TheCamera.SetCameraDirectlyBehindForFollowPed_CamOnAString();
            state.fall.Reset();
            Log(std::format("ground: CJ was at ({:.1f},{:.1f},{:.1f}), a broken position; put back at {} ({:.1f},{:.1f},{:.1f})",
                from.x, from.y, from.z, PositionGuard::NameOf(a_recovery.to), at.x, at.y, at.z));
        }

        // Whether a vertical line from a_from to height a_toZ hits a building (a chunk), and where.
        bool Line(const CVector& a_from, float a_toZ, float& a_hitZ)
        {
            CColPoint point;
            CEntity*  entity = nullptr;
            if (!CWorld::ProcessVerticalLine(a_from, a_toZ, point, entity, true, false, false, false, false, false, nullptr)) {
                return false;
            }
            a_hitZ = point.m_vecPoint.z;
            return true;
        }

        int WheelsOnGround(CVehicle* a_vehicle)
        {
            return a_vehicle->m_nVehicleClass == VEHICLE_BIKE ? static_cast<CBike*>(a_vehicle)->m_nNumContactWheels :
                                                               static_cast<CAutomobile*>(a_vehicle)->m_nNumContactWheels;
        }

        // CJ, or the vehicle he is in, below the ground: put back (FallRescue). A vehicle is watched by
        // its middle (the bottom of its collision box hangs below its tyres, under the road), and it
        // stands while a wheel touches.
        void Catch(CPlayerPed* a_ped)
        {
            CVehicle* vehicle = Vehicles::Driven(a_ped);
            if (vehicle != state.falling) {
                state.falling = vehicle;
                state.fall.Reset();  // the spot it remembered was the other one's
            }
            auto*           col = vehicle ? CModelInfo::GetModelInfo(vehicle->m_nModelIndex)->m_pColModel : nullptr;
            const float     lift = col ? -col->m_boundBox.m_vecMin.z : a_ped->GetDistanceFromCentreOfMassToBaseOfModel();
            const CVector&  pos = vehicle ? vehicle->GetPosition() : a_ped->GetPosition();
            const CVector   feet(pos.x, pos.y, vehicle ? pos.z : pos.z - lift);
            const CVector   from(feet.x, feet.y, feet.z + 0.1f);
            FallRescue::Around around;
            around.feet = ToVec(feet);
            around.standing = vehicle ? WheelsOnGround(vehicle) > 0 : a_ped->bIsStanding;
            float below = 0.0f;
            around.surfaceBelow = Line(from, from.z - FallRescue::kBelowReach, below);
            around.surfaceAbove = Line(from, from.z + FallRescue::kAboveReach, around.aboveZ);
            const auto to = state.fall.Update(CTimer::ms_fTimeStep / 50.0f, around);
            if (!to) {
                return;
            }
            if (vehicle) {
                // Back where its middle was, or onto the surface over it, its wheels on it.
                const bool onto = around.surfaceAbove && std::abs(to->z - around.aboveZ) < 0.001f;
                vehicle->Teleport(CVector(to->x, to->y, to->z + (onto ? lift : 0.0f) + 0.05f), false);  // also stops it and resets its suspension
            } else {
                a_ped->SetPosn(CVector(to->x, to->y, to->z + lift + 0.05f));
                a_ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
            }
            Log(std::format("ground: {} was below the ground at ({:.1f},{:.1f},{:.1f}), put back at ({:.1f},{:.1f},{:.1f})",
                vehicle ? "CJ's vehicle" : "CJ", feet.x, feet.y, feet.z, to->x, to->y, to->z));
        }
    }

    void Begin(std::uint32_t a_handover, const float (&a_anchor)[3])
    {
        for (int slot : state.book.Begin(a_handover)) {
            TakeOut(state.slots[slot]);
        }
        if (!state.origin.Reset(a_anchor)) {
            Log("ground: the handover's anchor isn't a number, the origin stays where it was");
        }
        state.ready = false;
        state.beganMs = ::GetTickCount64();
        state.fall.Reset();
        state.guard.Reset();
        Log(std::format("ground: handover {} at Skyrim ({:.0f},{:.0f},{:.0f}), waiting for the ground under CJ",
            a_handover, a_anchor[0], a_anchor[1], a_anchor[2]));

        // Chunks that came in before this handover's input did.
        std::deque<std::vector<std::uint8_t>> waiting;
        waiting.swap(state.waiting);
        for (const auto& payload : waiting) {
            Handle(payload.data(), static_cast<std::uint32_t>(payload.size()));
        }
    }

    void Receive()
    {
        // Skyrim writes a new handover's input before it sends that handover's chunks. Until the
        // runner has read it (next frame), the ring stays as it is, so no chunk is taken for an
        // older handover's and dropped.
        Protocol::InputState input{};
        if (GetLink().ReadInput(input) && input.handover != state.book.Generation()) {
            return;
        }
        GetLink().Receive([](Protocol::MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
            if (a_type == Protocol::MessageType::CollisionChunk) {
                Handle(a_data, a_size);
            } else if (a_type == Protocol::MessageType::WeaponCommand) {
                CjWeapons::Queue(a_data, a_size);
            } else if (a_type == Protocol::MessageType::Effect) {
                CjProjectiles::Queue(a_data, a_size);
            } else if (a_type == Protocol::MessageType::Cheat) {
                CjCheats::Queue(a_data, a_size);
            } else if (a_type == Protocol::MessageType::PickupCommand) {
                CjPickups::Queue(a_data, a_size);
            }
        });
    }

    void Update(CPlayerPed* a_ped, CVector& a_spawn, bool a_following)
    {
        // First, so nothing broken reaches the origin, the ground or the fall rescue.
        if (const auto recovery = state.guard.Update(CTimer::ms_fTimeStep / 50.0f, ToVec(a_ped->GetPosition()), ToVec(a_spawn))) {
            Recover(a_ped, a_spawn, *recovery);
        }

        Ground::Vec3 shift;
        if (state.ready && state.origin.Recenter(ToVec(a_ped->GetPosition()), shift)) {
            Shift(a_ped, a_spawn, shift);
        }

        const auto cj = state.origin.ToSkyrim(ToVec(a_ped->GetPosition()));
        const auto distant = state.book.DropFar(Ground::KeyOf(cj[0], cj[1]));
        for (int slot : distant) {
            TakeOut(state.slots[slot]);
        }
        state.distant += static_cast<int>(distant.size());

        if (!state.ready) {
            const auto spawn = state.origin.ToSkyrim(ToVec(a_spawn));
            if (state.book.Has(Ground::KeyOf(spawn[0], spawn[1]))) {
                state.ready = true;
                Log(std::format("ground: the ground under CJ is in after {:.1f} s, letting him go", (::GetTickCount64() - state.beganMs) / 1000.0));
            } else {
                // Held where the handover put him until there is something to stand on.
                a_ped->SetPosn(a_spawn);
                a_ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
                return;
            }
        }
        if (!CTimer::m_UserPause && !a_following) {
            Catch(a_ped);
        }

        const std::uint64_t now = ::GetTickCount64();
        if (now - state.statMs >= 5000) {
            if (state.built || state.distant || state.stale) {
                // San Andreas's pools the chunks draw on: a full one returns nothing, and its users crash.
                const auto used = [](auto* a_pool) { return a_pool ? std::format("{}/{}", a_pool->GetNoOfUsedSpaces(), a_pool->m_nSize) : std::string("?"); };
                Log(std::format("ground: {} chunks built ({} resent, {} triangles, {} left out), {} far ones dropped, {} stale; {} chunks around CJ; "
                                "pools: col models {}, buildings {}",
                    state.built, state.replaced, state.triangles, state.dropped, state.distant, state.stale, state.book.Count(), used(CPools::ms_pColModelPool),
                    used(CPools::ms_pBuildingPool)));
            }
            state.statMs = now;
            state.built = state.replaced = state.triangles = state.dropped = state.stale = state.distant = 0;
        }
    }

    void Place(CPlayerPed* a_ped, const CVector& a_at, float a_rotation)
    {
        a_ped->SetPosn(a_at);
        a_ped->SetHeading(a_rotation);  // the whole rotation, whatever was in it
        a_ped->m_fHeadingCurrent = a_rotation;
        a_ped->m_fHeadingGoal = a_rotation;
        a_ped->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        a_ped->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
        a_ped->m_vecFrictionMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        a_ped->m_vecFrictionTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
        a_ped->m_vecForce = CVector(0.0f, 0.0f, 0.0f);
        a_ped->m_vecTorque = CVector(0.0f, 0.0f, 0.0f);
        MendCamera(a_at);
        // What San Andreas's own Wasted left behind (it can't run any more, but a game that ran it
        // kept CJ standing still for good): its player controls off, its screen faded out.
        CPad::GetPad(0)->DisablePlayerControls = 0;
        TheCamera.Fade(0.0f, 1);  // fade in, at once
    }

    bool Ready() { return state.ready; }
}
