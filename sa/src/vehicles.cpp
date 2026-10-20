#include "vehicles.h"

#include "log.h"
#include "model_sender.h"
#include "rw_texture.h"
#include "skin_build.h"
#include "skin_model.h"
#include "texture_decode.h"
#include "vehicle.h"

#include <plugin.h>

#include <CAutomobile.h>
#include <CBike.h>
#include <CBmx.h>
#include <CColPoint.h>
#include <CModelInfo.h>
#include <CMonsterTruck.h>
#include <CPedIntelligence.h>
#include <CPlayerPed.h>
#include <CPools.h>
#include <CQuadBike.h>
#include <CStreaming.h>
#include <CTaskSimpleCarSetPedInAsDriver.h>
#include <CTaskSimplePlayerOnFoot.h>
#include <CVehicleModelInfo.h>
#include <CWorld.h>
#include <NodeName.h>
#include <common.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace AhShitRim::Vehicles
{
    namespace
    {
        constexpr float kGroundAbove = Vehicle::kMaxStep + 2.0f;  // ground samples start this far above CJ's feet...
        constexpr float kGroundBelow = Vehicle::kMaxStep + 1.0f;  // ... and look this far below them
        constexpr float kClearHeight = 0.8f;                       // the in-the-way lines run this far above the ground
        constexpr float kDropHeight = 0.3f;                        // a new vehicle is put this far above the ground first

        struct Slot
        {
            int                   ref = -1;  // CPools vehicle handle, -1 = empty
            std::uint64_t         spawned = 0;  // spawn order: the oldest goes first
            std::uint32_t         model = 0;    // the model's id once built
            bool                  failed = false;
            std::vector<RwFrame*> parts;  // each part's frame, in bone order
            std::string           name;
        };

        struct State
        {
            Slot          slots[Protocol::kVehicleMax];
            std::uint64_t spawns = 0;
            std::uint32_t builds = 0;
            std::uint64_t drivenLogMs = 0;
        } state;

        Pose::Vec3 Vec(const CVector& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        Body::RwRows ToFrame(const RwMatrix& a_m)
        {
            return { { a_m.right.x, a_m.right.y, a_m.right.z }, { a_m.up.x, a_m.up.y, a_m.up.z }, { a_m.at.x, a_m.at.y, a_m.at.z },
                { a_m.pos.x, a_m.pos.y, a_m.pos.z } };
        }

        std::uint32_t NowMs()
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

        CVehicleModelInfo* InfoOf(int a_model)
        {
            auto* info = a_model >= 0 && a_model < 20000 ? CModelInfo::GetModelInfo(a_model) : nullptr;
            return info && info->GetModelType() == MODEL_INFO_VEHICLE ? static_cast<CVehicleModelInfo*>(info) : nullptr;
        }

        std::string NameOf(CVehicleModelInfo* a_info, int a_model)
        {
            const std::string_view name(a_info->m_szGameName, strnlen(a_info->m_szGameName, sizeof(a_info->m_szGameName)));
            return name.empty() ? std::format("model {}", a_model) : std::format("{} ({})", name, a_model);
        }

        // The slot's vehicle, or nullptr once San Andreas has deleted it (the slot is emptied then).
        CVehicle* Live(int a_index)
        {
            Slot& slot = state.slots[a_index];
            if (slot.ref < 0) {
                return nullptr;
            }
            if (auto* vehicle = CPools::GetVehicle(slot.ref)) {
                return vehicle;
            }
            Log(std::format("vehicle: the {} in slot {} is gone", slot.name, a_index));
            ModelSender::Remove(ModelSender::kVehicleKey + a_index);
            GetLink().WriteVehicle(static_cast<std::uint32_t>(a_index), Protocol::BodyState{});
            slot = Slot{};
            return nullptr;
        }

        void Remove(int a_index)
        {
            if (auto* vehicle = Live(a_index)) {
                CWorld::Remove(vehicle);
                delete vehicle;
                Log(std::format("vehicle: the {} in slot {} removed", state.slots[a_index].name, a_index));
            }
            ModelSender::Remove(ModelSender::kVehicleKey + a_index);
            GetLink().WriteVehicle(static_cast<std::uint32_t>(a_index), Protocol::BodyState{});  // flags 0: Skyrim hides it
            state.slots[a_index] = Slot{};
        }

        // ---- Spawning ----

        Vehicle::Footprint FootprintOf(CVehicleModelInfo* a_info)
        {
            const auto& box = a_info->m_pColModel->m_boundBox;
            return { (std::max)(std::abs(box.m_vecMin.x), std::abs(box.m_vecMax.x)), (std::max)(std::abs(box.m_vecMin.y), std::abs(box.m_vecMax.y)) };
        }

        bool GroundAt(const Pose::Vec3& a_point, float a_feetZ, float& a_z)
        {
            CColPoint point;
            CEntity*  entity = nullptr;
            if (!CWorld::ProcessVerticalLine(CVector(a_point.x, a_point.y, a_feetZ + kGroundAbove), a_feetZ - kGroundBelow, point, entity, true, false,
                    false, true, false, false, nullptr)) {
                return false;
            }
            a_z = point.m_vecPoint.z;
            return true;
        }

        bool Clear(const CVector& a_from, const CVector& a_to)
        {
            return CWorld::GetIsLineOfSightClear(a_from, a_to, true, true, false, true, false, false, false);
        }

        // The first spot in front of CJ that will do, and the ground's height there.
        bool FindSpot(CPlayerPed* a_ped, const Vehicle::Footprint& a_size, Vehicle::Spot& a_spot, float& a_groundZ, int& a_tried)
        {
            const CVector feet = a_ped->GetPosition() - CVector(0.0f, 0.0f, a_ped->GetDistanceFromCentreOfMassToBaseOfModel());
            const CVector chest = feet + CVector(0.0f, 0.0f, 1.0f);
            a_tried = 0;
            for (const auto& spot : Vehicle::Candidates(Vec(feet), Vec(a_ped->GetForward()), a_size)) {
                a_tried++;
                const auto      points = Vehicle::SamplePoints(spot, a_size);
                Vehicle::Ground ground;
                for (int i = 0; i < Vehicle::kSamples; i++) {
                    ground.hit[i] = GroundAt(points[i], feet.z, ground.z[i]);
                }
                const auto z = Vehicle::GroundHeight(ground, feet.z);
                if (!z) {
                    continue;
                }
                const float h = *z + kClearHeight;
                const auto  at = [&](int i) { return CVector(points[i].x, points[i].y, h); };
                if (!Clear(chest, CVector(spot.center.x, spot.center.y, h)) || !Clear(at(0), at(3)) || !Clear(at(1), at(2))) {
                    continue;
                }
                a_spot = spot;
                a_groundZ = *z;
                return true;
            }
            return false;
        }

        CVehicle* Create(CVehicleModelInfo* a_info, int a_model)
        {
            switch (a_info->m_nVehicleType) {
            case VEHICLE_AUTOMOBILE: return new CAutomobile(a_model, MISSION_VEHICLE, true);
            case VEHICLE_MTRUCK: return new CMonsterTruck(a_model, MISSION_VEHICLE);
            case VEHICLE_QUAD: return new CQuadBike(a_model, MISSION_VEHICLE);
            case VEHICLE_BIKE: return new CBike(a_model, MISSION_VEHICLE);
            case VEHICLE_BMX: return new CBmx(a_model, MISSION_VEHICLE);
            default: return nullptr;  // boats, aircraft and trains come later
            }
        }

        int FreeSlot()
        {
            for (int i = 0; i < static_cast<int>(Protocol::kVehicleMax); i++) {
                if (!Live(i)) {
                    return i;
                }
            }
            // All taken: the oldest one CJ isn't in goes.
            auto* ped = FindPlayerPed(0);
            int   oldest = -1;
            for (int i = 0; i < static_cast<int>(Protocol::kVehicleMax); i++) {
                if (ped && (Live(i) == Driven(ped) || Live(i) == ped->m_pVehicle)) {
                    continue;  // in it, or getting in or out of it
                }
                if (oldest < 0 || state.slots[i].spawned < state.slots[oldest].spawned) {
                    oldest = i;
                }
            }
            if (oldest >= 0) {
                Remove(oldest);
            }
            return oldest;
        }

        // ---- The model ----

        bool Drawn(RpAtomic* a_atomic)
        {
            if ((RpAtomicGetFlags(a_atomic) & rpATOMICRENDER) == 0 || !a_atomic->geometry) {
                return false;
            }
            const char*            raw = GetFrameNodeName(RpAtomicGetFrame(a_atomic));
            const std::string_view name = raw ? raw : "";
            // The low detail chassis and the damaged versions of parts aren't drawn up close.
            return !name.ends_with("_vlo") && !name.ends_with("_dam");
        }

        // The vehicle's drawn parts and their frames; its model blob in a_out.
        bool Build(int a_index, CVehicle* a_vehicle)
        {
            Slot&      slot = state.slots[a_index];
            const auto start = std::chrono::steady_clock::now();
            slot.failed = true;
            auto* info = InfoOf(a_vehicle->m_nModelIndex);
            auto* clump = a_vehicle->m_pRwClump;
            if (!info || !clump || a_vehicle->m_pRwObject->type != rpCLUMP) {
                Log(std::format("vehicle: the {} has no clump, nothing to draw in Skyrim", slot.name));
                return false;
            }
            std::vector<RpAtomic*> atomics;
            RpClumpForAllAtomics(
                clump,
                [](RpAtomic* a_atomic, void* a_data) -> RpAtomic* {
                    static_cast<std::vector<RpAtomic*>*>(a_data)->push_back(a_atomic);
                    return a_atomic;
                },
                &atomics);

            const CRGBA*                    table = CVehicleModelInfo::ms_vehicleColourTable;
            const auto                      colour = [&](unsigned char a_i) { return Vehicle::Rgb{ table[a_i].r, table[a_i].g, table[a_i].b }; };
            const std::array<Vehicle::Rgb, 4> colours = { colour(a_vehicle->m_nPrimaryColor), colour(a_vehicle->m_nSecondaryColor),
                colour(a_vehicle->m_nTertiaryColor), colour(a_vehicle->m_nQuaternaryColor) };

            SkinModel::ModelIn               model;
            model.name = slot.name.substr(0, 31);
            model.kind = Protocol::kSkinModelVehicle;
            std::map<RwRaster*, std::int32_t> byRaster;
            std::vector<SkinModel::MeshIn>    meshes;
            std::string                       textureLog;
            int                               skipped = 0, hidden = 0, baked = 0;
            std::uint32_t                     flipped = 0;
            slot.parts.clear();
            for (RpAtomic* atomic : atomics) {
                RpGeometry* geometry = atomic->geometry;
                if (!Drawn(atomic)) {
                    hidden++;
                    continue;
                }
                if ((geometry->flags & rpGEOMETRYNATIVE) != 0 || !geometry->morphTarget || !geometry->morphTarget[0].verts || !geometry->triangles ||
                    slot.parts.size() >= Protocol::kBodyBoneMax) {
                    skipped++;
                    continue;
                }
                const auto part = static_cast<std::uint32_t>(slot.parts.size());
                RwFrame*   frame = RpAtomicGetFrame(atomic);

                std::vector<SkinBuild::Material> materials;
                for (int m = 0; m < geometry->matList.numMaterials; m++) {
                    const RpMaterial*   material = geometry->matList.materials[m];
                    RwTexture*          texture = material->texture;
                    SkinBuild::Material out;
                    const auto          rgb = Vehicle::MaterialColour({ material->color.red, material->color.green, material->color.blue },
                                 texture && texture == CVehicleModelInfo::ms_pLightsTexture, colours);
                    out.color[0] = rgb.r;
                    out.color[1] = rgb.g;
                    out.color[2] = rgb.b;
                    out.color[3] = material->color.alpha;
                    if (texture && texture->raster) {
                        const auto found = byRaster.find(texture->raster);
                        if (found != byRaster.end()) {
                            out.texture = found->second;
                        } else {
                            std::string why;
                            auto        read = RwTextures::Read(texture, why);
                            if (read.rgba.empty()) {
                                Log(std::format("vehicle: texture {} of the {} left white: {}", static_cast<const char*>(texture->name), slot.name, why));
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
                        out.flags |= Protocol::kSkinMeshBlend;  // glass
                    } else if (out.texture >= 0 && TextureDecode::HasAlpha(model.textures[out.texture].rgba)) {
                        out.flags |= Protocol::kSkinMeshCutout;
                    }
                    materials.push_back(out);
                }

                // Every vertex on this part's bone alone.
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
                const auto bake = Vehicle::Bake(ToFrame(*RwFrameGetLTM(frame)));
                baked += !Vehicle::IsIdentity(bake);
                Vehicle::BakeMeshes(partMeshes, bake);
                for (auto& mesh : partMeshes) {
                    meshes.push_back(std::move(mesh));
                }
                slot.parts.push_back(frame);
                model.boneIds.push_back(part);
            }
            if (slot.parts.empty()) {
                Log(std::format("vehicle: the {} has no part to draw ({} atomics, {} hidden, {} skipped)", slot.name, atomics.size(), hidden, skipped));
                return false;
            }
            model.meshes = Vehicle::Merge(std::move(meshes));
            std::uint32_t vertices = 0, triangles = 0;
            for (const auto& mesh : model.meshes) {
                vertices += static_cast<std::uint32_t>(mesh.vertices.size());
                triangles += static_cast<std::uint32_t>(mesh.indices.size() / 3);
            }
            const auto blob = SkinModel::Serialize(model);
            if (blob.size() > Protocol::kModelSizeMax || !SkinModel::Parse(blob.data(), blob.size())) {
                Log(std::format("vehicle: the {}'s model ({} bytes, {} meshes) would not load in Skyrim, not sent", slot.name, blob.size(), model.meshes.size()));
                return false;
            }
            slot.model = (static_cast<std::uint32_t>(::GetTickCount64()) << 4 | (++state.builds & 0xF)) | 1;  // new for every build
            auto       chunks = SkinModel::Chunks(slot.model, blob);
            const auto chunkCount = chunks.size();
            ModelSender::Set(ModelSender::kVehicleKey + a_index, slot.model, std::move(chunks), std::format("vehicle: the {}'s model", slot.name));
            slot.failed = false;
            const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            Log(std::format("vehicle: the {}'s model {:08X} read in {:.1f} ms: {} parts ({} with a scale or mirror baked in; {} atomics hidden, {} skipped), "
                            "{} meshes, {} vertices, {} triangles ({} turned round), {} textures:{}; {} bytes in {} chunks",
                slot.name, slot.model, ms, slot.parts.size(), baked, hidden, skipped, model.meshes.size(), vertices, triangles, flipped,
                model.textures.size(), textureLog.empty() ? " none" : textureLog, blob.size(), chunkCount));
            return true;
        }

        // ---- The pose ----

        // What drawing the vehicle would have done to its frames: its matrix into the clump's frame,
        // then PreRender (wheels turned, steered and on their suspension; a bike's forks and
        // handlebars, and its lean, which PreRender puts into the parts under the upright root: the
        // lean also put on the root leaned the bike twice as far as CJ, 96 degrees to his 48).
        // A bike's lean matrix is worked out fresh too, for seating CJ (SetPedPositionInCar).
        void Pose(CVehicle* a_vehicle)
        {
            a_vehicle->bOffscreen = false;
            a_vehicle->UpdateRwMatrix();
            a_vehicle->UpdateRwFrame();
            a_vehicle->PreRender();
            if (a_vehicle->m_nVehicleClass == VEHICLE_BIKE) {
                auto* bike = static_cast<CBike*>(a_vehicle);
                bike->m_bLeanMatrixCalculated = false;
                bike->CalculateLeanMatrix();
            }
        }

        void Publish(int a_index, CVehicle* a_vehicle, bool a_driven, const CVector& a_spawnFeet, unsigned a_handover, unsigned a_frame)
        {
            const Slot&         slot = state.slots[a_index];
            const CVector&      at = a_vehicle->GetPosition();
            Protocol::BodyState s{};
            s.frame = a_frame;
            s.flags = Protocol::kBodyValid | (a_driven ? Protocol::kBodyDriven : 0);
            s.handover = a_handover;
            s.model = slot.model;
            s.boneCount = static_cast<std::uint32_t>(slot.parts.size());
            s.timeMs = NowMs();
            const CVector d = at - a_spawnFeet;
            s.feet[0] = d.x * Pose::kUnitsPerMeter;
            s.feet[1] = d.y * Pose::kUnitsPerMeter;
            s.feet[2] = d.z * Pose::kUnitsPerMeter;
            s.cameraForward[1] = 1.0f;  // unused, but a unit vector like everywhere else
            for (std::uint32_t i = 0; i < s.boneCount; i++) {
                s.bones[i] = Body::ToBone(ToFrame(*RwFrameGetLTM(slot.parts[i])), Vec(at));
            }
            if (Body::Finite(s)) {
                GetLink().WriteVehicle(static_cast<std::uint32_t>(a_index), s);
            }
        }
    }

    CVehicle* Driven(CPlayerPed* a_ped)
    {
        return a_ped && a_ped->bInVehicle ? a_ped->m_pVehicle : nullptr;
    }

    void Spawn(CPlayerPed* a_ped, int a_model)
    {
        auto* info = InfoOf(a_model);
        if (!info) {
            Log(std::format("vehicle: {} is not a vehicle model, nothing spawned", a_model));
            return;
        }
        const auto name = NameOf(info, a_model);
        if (Driven(a_ped)) {
            Log(std::format("vehicle: CJ is in a vehicle, no {} spawned", name));
            return;
        }
        CStreaming::RequestModel(a_model, 0x2 | 0x8);  // game required, keep in memory
        CStreaming::LoadAllRequestedModels(false);
        if (CStreaming::ms_aInfoForModel[a_model].m_nLoadState != 1 || !info->m_pColModel) {  // LOADSTATE_LOADED
            Log(std::format("vehicle: the {} did not load, nothing spawned", name));
            return;
        }
        const auto     size = FootprintOf(info);
        Vehicle::Spot  spot;
        float          groundZ = 0.0f;
        int            tried = 0;
        if (!FindSpot(a_ped, size, spot, groundZ, tried)) {
            Log(std::format("vehicle: no flat, clear spot for the {} in front of CJ ({} tried), nothing spawned", name, tried));
            return;
        }
        const int index = FreeSlot();
        if (index < 0) {
            Log(std::format("vehicle: no slot for the {}, nothing spawned", name));
            return;
        }
        CVehicle* vehicle = Create(info, a_model);
        if (!vehicle) {
            Log(std::format("vehicle: the {} is not a car or a bike, nothing spawned", name));
            return;
        }
        vehicle->SetPosn(CVector(spot.center.x, spot.center.y, groundZ - info->m_pColModel->m_boundBox.m_vecMin.z + kDropHeight));
        vehicle->SetHeading(spot.heading);
        vehicle->m_nStatus = STATUS_ABANDONED;
        vehicle->m_eDoorLock = DOORLOCK_UNLOCKED;
        CWorld::Add(vehicle);
        if (vehicle->m_nVehicleClass == VEHICLE_BIKE) {
            static_cast<CBike*>(vehicle)->bOnSideStand = true;  // parked leaning on it, as San Andreas parks bikes, rather than falling over
            static_cast<CBike*>(vehicle)->PlaceOnRoadProperly();
        } else {
            static_cast<CAutomobile*>(vehicle)->PlaceOnRoadProperly();
        }
        Slot& slot = state.slots[index];
        slot = Slot{};
        slot.ref = CPools::GetVehicleRef(vehicle);
        slot.spawned = ++state.spawns;
        slot.name = name;
        const CVector& at = vehicle->GetPosition();
        Log(std::format("vehicle: {} spawned in slot {} at ({:.1f},{:.1f},{:.1f}), spot {} of {} tried, colours {}/{}", name, index, at.x, at.y, at.z,
            tried, Vehicle::kRings * std::size(Vehicle::kTurns), vehicle->m_nPrimaryColor, vehicle->m_nSecondaryColor));
    }

    bool Restore(CPlayerPed* a_ped, const CVector& a_cj, const Restoration& a_car)
    {
        auto* info = InfoOf(a_car.model);
        if (!info) {
            Log(std::format("vehicle: {} from the save is not a vehicle model, nothing restored", a_car.model));
            return true;
        }
        const auto name = NameOf(info, a_car.model);
        if (Driven(a_ped)) {
            return true;
        }
        CStreaming::RequestModel(a_car.model, 0x2 | 0x8);  // game required, keep in memory
        CStreaming::LoadAllRequestedModels(false);
        if (CStreaming::ms_aInfoForModel[a_car.model].m_nLoadState != 1 || !info->m_pColModel) {  // LOADSTATE_LOADED
            Log(std::format("vehicle: the {} from the save did not load, nothing restored", name));
            return true;
        }
        const int index = FreeSlot();
        CVehicle* vehicle = index >= 0 ? Create(info, a_car.model) : nullptr;
        if (!vehicle) {
            Log(std::format("vehicle: no slot or no way to make the {} from the save, nothing restored", name));
            return true;
        }
        const CVector feet = a_cj - CVector(0.0f, 0.0f, a_ped->GetDistanceFromCentreOfMassToBaseOfModel());
        const CVector at = feet + CVector(a_car.offset[0], a_car.offset[1], a_car.offset[2]);
        float         groundZ = at.z;
        if (!GroundAt({ at.x, at.y, at.z }, feet.z, groundZ)) {
            groundZ = at.z;  // the middle of it isn't on the ground: where it was, then put on its wheels
        }
        vehicle->SetPosn(CVector(at.x, at.y, groundZ - info->m_pColModel->m_boundBox.m_vecMin.z + kDropHeight));
        vehicle->SetHeading(a_car.heading);
        vehicle->m_nPrimaryColor = a_car.colours[0];
        vehicle->m_nSecondaryColor = a_car.colours[1];
        vehicle->m_nTertiaryColor = a_car.colours[2];
        vehicle->m_nQuaternaryColor = a_car.colours[3];
        vehicle->m_nStatus = STATUS_ABANDONED;
        vehicle->m_eDoorLock = DOORLOCK_UNLOCKED;
        CWorld::Add(vehicle);
        if (vehicle->m_nVehicleClass == VEHICLE_BIKE) {
            static_cast<CBike*>(vehicle)->PlaceOnRoadProperly();
        } else {
            static_cast<CAutomobile*>(vehicle)->PlaceOnRoadProperly();
        }
        Slot& slot = state.slots[index];
        slot = Slot{};
        slot.ref = CPools::GetVehicleRef(vehicle);
        slot.spawned = ++state.spawns;
        slot.name = name;
        // What WARP_CHAR_INTO_CAR does: CJ in the driver's seat at once.
        auto* task = new CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
        task->m_bWarpingInToCar = true;
        task->ProcessPed(a_ped);
        delete task;
        vehicle->bEngineOn = true;
        const CVector& now = vehicle->GetPosition();
        Log(std::format("vehicle: the {} from the save restored in slot {} at ({:.1f},{:.1f},{:.1f}), colours {}/{}/{}/{}, CJ {}", name, index, now.x, now.y, now.z,
            a_car.colours[0], a_car.colours[1], a_car.colours[2], a_car.colours[3], Driven(a_ped) ? "at the wheel" : "could not be seated"));
        return true;
    }

    void GetOut(CPlayerPed* a_ped)
    {
        if (Driven(a_ped) || a_ped->m_pVehicle) {
            // What WARP_CHAR_FROM_CAR_TO_COORD does (also while he gets in or out); the caller puts CJ
            // where he goes.
            a_ped->m_pIntelligence->FlushImmediately(true);
            a_ped->m_pIntelligence->m_TaskMgr.SetTask(new CTaskSimplePlayerOnFoot(), 4, false);  // TASK_PRIMARY_DEFAULT, as at his creation
            Log(Driven(a_ped) ? "vehicle: could not get CJ out of his vehicle" : "vehicle: CJ taken out of his vehicle");
        }
    }

    void Clear(CPlayerPed* a_ped)
    {
        GetOut(a_ped);
        int removed = 0;
        for (int i = 0; i < static_cast<int>(Protocol::kVehicleMax); i++) {
            if (Live(i) && Live(i) != Driven(a_ped)) {
                Remove(i);
                removed++;
            }
        }
        if (removed) {
            Log(std::format("vehicle: {} vehicles removed for the new handover or re-anchor", removed));
        }
    }

    void Shift(const CVector& a_shift)
    {
        for (int i = 0; i < static_cast<int>(Protocol::kVehicleMax); i++) {
            if (auto* vehicle = Live(i)) {
                const CVector speed = vehicle->m_vecMoveSpeed;
                CWorld::Remove(vehicle);
                vehicle->SetPosn(vehicle->GetPosition() - a_shift);
                CWorld::Add(vehicle);
                vehicle->m_vecMoveSpeed = speed;
                // Where the wheels touched in the last collision pass, in world space: the next
                // physics step takes the tyres' speed at those points, and left a kilometre behind
                // they stopped a car at 49 m/s dead and rolled it.
                if (vehicle->m_nVehicleClass == VEHICLE_BIKE) {
                    for (auto& point : static_cast<CBike*>(vehicle)->m_anWheelColPoint) {
                        point.m_vecPoint -= a_shift;
                    }
                } else {
                    for (auto& point : static_cast<CAutomobile*>(vehicle)->m_wheelColPoint) {
                        point.m_vecPoint -= a_shift;
                    }
                }
                vehicle->m_vecLastCollisionPosn -= a_shift;
            }
        }
    }

    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame)
    {
        CVehicle*     driven = Driven(a_ped);
        const CVector spawnFeet = a_spawn - CVector(0.0f, 0.0f, a_ped->GetDistanceFromCentreOfMassToBaseOfModel() + a_lift);
        for (int i = 0; i < static_cast<int>(Protocol::kVehicleMax); i++) {
            auto* vehicle = Live(i);
            if (!vehicle) {
                continue;
            }
            Pose(vehicle);
            Slot& slot = state.slots[i];
            if (!slot.model && !slot.failed) {
                Build(i, vehicle);  // after its first PreRender, so the wheels sit where they belong
            }
            if (slot.model) {
                Publish(i, vehicle, vehicle == driven, spawnFeet, a_handover, a_frame);
            }
        }
        if (driven) {
            a_ped->SetPedPositionInCar();  // seated where the (leaning) vehicle is now
            // What the vehicle makes of the input and the ground, every 5 s while CJ drives.
            if (const std::uint64_t now = ::GetTickCount64(); now - state.drivenLogMs >= 5000) {
                state.drivenLogMs = now;
                const int wheels = driven->m_nVehicleClass == VEHICLE_BIKE ? static_cast<CBike*>(driven)->m_nNumContactWheels :
                                                                             static_cast<CAutomobile*>(driven)->m_nNumContactWheels;
                const CVector& speed = driven->m_vecMoveSpeed;
                Log(std::format("vehicle: driving at {:.1f} m/s, {} wheels on the ground, gas {:.2f}, brake {:.2f}{}, gear {}, engine {}, status {}",
                    std::hypot(speed.x, speed.y) * 50.0f, wheels, driven->m_fGasPedal, driven->m_fBreakPedal, driven->bIsHandbrakeOn ? ", handbrake" : "",
                    driven->m_nCurrentGear, driven->bEngineOn ? "on" : "off", static_cast<int>(driven->m_nStatus)));
            }
        }
    }
}
