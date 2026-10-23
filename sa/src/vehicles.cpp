#include "vehicles.h"

#include "cj_cheats.h"
#include "log.h"
#include "position_guard.h"
#include "model_sender.h"
#include "rw_texture.h"
#include "skin_build.h"
#include "skin_model.h"
#include "texture_decode.h"
#include "vehicle.h"
#include "vehicle_damage.h"
#include "vehicle_kinds.h"

#include <plugin.h>

#include <CAutomobile.h>
#include <CBike.h>
#include <CBmx.h>
#include <CCamera.h>
#include <CColPoint.h>
#include <CHeli.h>
#include <CModelInfo.h>
#include <CMonsterTruck.h>
#include <CPedIntelligence.h>
#include <CPlane.h>
#include <CPlayerPed.h>
#include <CPools.h>
#include <CPad.h>
#include <CQuadBike.h>
#include <CStats.h>
#include <CStreaming.h>
#include <CTaskSimpleCarSetPedInAsDriver.h>
#include <CTaskSimplePlayerOnFoot.h>
#include <CTimer.h>
#include <CVehicleModelInfo.h>
#include <CWorld.h>
#include <NodeName.h>
#include <common.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
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
            bool                  scorched = false;  // a wreck: its model was built again, scorched
            std::vector<RwFrame*> parts;  // each part's frame, in bone order
            std::vector<RpAtomic*> atomics;  // and its atomic: whether San Andreas draws it (a damaged or whole version) is read every frame
            std::vector<VehicleKinds::Spinner> spinners;  // a rotor's or a propeller's standing or spinning version
            std::vector<VehicleKinds::ExtraWheel> extras;  // the Rhino's road wheels between its front and back ones
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
        bool FindSpot(CPlayerPed* a_ped, const Vehicle::Footprint& a_size, float a_maxUneven, Vehicle::Spot& a_spot, float& a_groundZ, int& a_tried)
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
                const auto z = Vehicle::GroundHeight(ground, feet.z, a_maxUneven);
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

        VehicleKinds::Kind KindOf(CVehicleModelInfo* a_info, int a_model)
        {
            return VehicleKinds::FromSaType(a_info->m_nVehicleType, static_cast<std::uint16_t>(a_model));
        }

        // What CCheat::VehicleCheat makes for a model, by its type; nullptr for a kind that can't spawn
        // (VehicleKinds::Traits: boats, trains, trailers, the radio-controlled ones).
        CVehicle* Create(CVehicleModelInfo* a_info, int a_model)
        {
            if (!VehicleKinds::TraitsOf(KindOf(a_info, a_model)).spawnable) {
                return nullptr;
            }
            switch (a_info->m_nVehicleType) {
            case VEHICLE_AUTOMOBILE: return new CAutomobile(a_model, MISSION_VEHICLE, true);
            case VEHICLE_MTRUCK: return new CMonsterTruck(a_model, MISSION_VEHICLE);
            case VEHICLE_QUAD: return new CQuadBike(a_model, MISSION_VEHICLE);
            case VEHICLE_HELI: return new CHeli(a_model, MISSION_VEHICLE);
            case VEHICLE_PLANE: return new CPlane(a_model, MISSION_VEHICLE);
            case VEHICLE_BIKE: return new CBike(a_model, MISSION_VEHICLE);
            case VEHICLE_BMX: return new CBmx(a_model, MISSION_VEHICLE);
            default: return nullptr;
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

        std::string_view FrameName(RpAtomic* a_atomic)
        {
            const char* raw = GetFrameNodeName(RpAtomicGetFrame(a_atomic));
            return raw ? raw : "";
        }

        // A part Skyrim gets: any atomic with a geometry but the low detail chassis, which isn't drawn up
        // close. A part's damaged and whole versions both go, and whether San Andreas draws one now
        // (rpATOMICRENDER, CVehicle::SetComponentVisibility) is read every frame (hiddenParts, v19).
        bool Usable(RpAtomic* a_atomic) { return a_atomic->geometry && FrameName(a_atomic).find("_vlo") == std::string_view::npos; }  // also the Stuntplane's chassis_vlo2

        bool Drawn(RpAtomic* a_atomic) { return (RpAtomicGetFlags(a_atomic) & rpATOMICRENDER) != 0; }

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
            // The ones drawn now first, so a model with more parts than bones keeps those.
            std::stable_partition(atomics.begin(), atomics.end(), [](RpAtomic* a_atomic) { return Drawn(a_atomic); });

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
            std::vector<SkinModel::MeshIn>    wheelMeshes[2];  // the left and right front wheels', for the Rhino's road wheels
            int                               wheelParts[4] = { -1, -1, -1, -1 };  // its lf, lb, rf, rb wheels' parts
            slot.parts.clear();
            slot.extras.clear();
            slot.atomics.clear();
            slot.spinners.clear();
            for (RpAtomic* atomic : atomics) {
                RpGeometry* geometry = atomic->geometry;
                if (!Usable(atomic)) {
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
                    auto                rgb = Vehicle::MaterialColour({ material->color.red, material->color.green, material->color.blue },
                                 texture && texture == CVehicleModelInfo::ms_pLightsTexture, colours);
                    if (slot.scorched) {
                        rgb = VehicleDamage::Scorch(rgb);
                    }
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
                    // A spinning rotor's or propeller's disc: San Andreas fades it in by the material's alpha when it draws it
                    // (SetComponentAtomicAlpha), so it is see-through, never solid.
                    if (VehicleKinds::SpinnerOf(FrameName(atomic)) == VehicleKinds::Spinner::Moving) {
                        out.color[3] = (std::min)(out.color[3], VehicleKinds::kSpinnerAlpha);
                    }
                    if (out.color[3] < 255) {
                        out.flags |= Protocol::kSkinMeshBlend;  // glass, a spinning disc
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
                if (const int wheel = VehicleKinds::RoadWheelOf(FrameName(atomic)); wheel >= 0 && VehicleKinds::HasRoadWheels(a_vehicle->m_nModelIndex)) {
                    wheelParts[wheel] = static_cast<int>(part);
                    if (wheel == 0 || wheel == 2) {
                        wheelMeshes[wheel / 2] = partMeshes;
                    }
                }
                for (auto& mesh : partMeshes) {
                    meshes.push_back(std::move(mesh));
                }
                slot.parts.push_back(frame);
                slot.atomics.push_back(atomic);
                slot.spinners.push_back(VehicleKinds::SpinnerOf(FrameName(atomic)));
                model.boneIds.push_back(part);
            }
            if (slot.parts.empty()) {
                Log(std::format("vehicle: the {} has no part to draw ({} atomics, {} hidden, {} skipped)", slot.name, atomics.size(), hidden, skipped));
                return false;
            }
            // The Rhino's road wheels: San Andreas hides the middle wheel parts and draws its front wheel again along each
            // side when it renders the tank (CAutomobile::Render), which never runs hidden. Each is a copy of that side's
            // front wheel, a part of its own, put between the front and back wheels every frame.
            for (const auto& extra : VehicleKinds::RoadWheels(wheelParts, static_cast<int>(slot.parts.size()))) {
                if (extra.part >= static_cast<int>(Protocol::kBodyBoneMax)) {
                    break;
                }
                for (auto mesh : wheelMeshes[extra.front == wheelParts[0] ? 0 : 1]) {
                    for (auto& v : mesh.vertices) {
                        v.bones[0] = static_cast<std::uint8_t>(extra.part);
                    }
                    meshes.push_back(std::move(mesh));
                }
                slot.parts.push_back(slot.parts[extra.front]);
                slot.atomics.push_back(slot.atomics[extra.front]);
                slot.spinners.push_back(VehicleKinds::Spinner::None);
                model.boneIds.push_back(static_cast<std::uint32_t>(extra.part));
                slot.extras.push_back(extra);
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
            Log(std::format("vehicle: the {}'s {}model {:08X} read in {:.1f} ms: {} parts ({} with a scale or mirror baked in; {} atomics left out, {} skipped), "
                            "{} meshes, {} vertices, {} triangles ({} turned round), {} textures:{}; {} bytes in {} chunks",
                slot.name, slot.scorched ? "scorched " : "", slot.model, ms, slot.parts.size(), baked, hidden, skipped, model.meshes.size(), vertices, triangles, flipped,
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

        bool Wrecked(CVehicle* a_vehicle) { return a_vehicle->m_nStatus == STATUS_WRECKED; }

        // Where the engine smokes and burns from, in the world: San Andreas's engine dummy, where it puts its
        // overheat_car and fire_car, or the bonnet for a model without one.
        CVector EngineAt(CVehicle* a_vehicle)
        {
            auto* info = InfoOf(a_vehicle->m_nModelIndex);
            if (!info || !info->m_pVehicleStruct || !info->m_pColModel) {
                return a_vehicle->GetPosition();
            }
            const CVector& dummy = info->m_pVehicleStruct->m_avDummyPos[7];  // DUMMY_ENGINE
            const auto&    box = info->m_pColModel->m_boundBox;
            const auto     p = VehicleDamage::EnginePoint(Vec(dummy), Vec(box.m_vecMin), Vec(box.m_vecMax));
            return a_vehicle->GetPosition() + a_vehicle->GetRight() * p.x + a_vehicle->GetForward() * p.y + a_vehicle->GetUp() * p.z;
        }

        // How fast its rotor or propeller turns (San Andreas's radians a step): a helicopter's m_fHeliRotorSpeed (the
        // second wheel speed in the union), a plane's m_fPropSpeed (0x9C4, which plugin-sdk leaves unnamed); 0 for the rest.
        float SpinOf(CVehicle* a_vehicle)
        {
            switch (a_vehicle->m_nVehicleSubClass) {
            case VEHICLE_HELI: return static_cast<CAutomobile*>(a_vehicle)->m_fWheelSpeed[1];
            case VEHICLE_PLANE: return *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(a_vehicle) + 0x9C4);
            default: return 0.0f;
            }
        }

        void Publish(int a_index, CVehicle* a_vehicle, bool a_driven, const CVector& a_spawnFeet, unsigned a_handover, unsigned a_frame)
        {
            const Slot&         slot = state.slots[a_index];
            const CVector&      at = a_vehicle->GetPosition();
            Protocol::BodyState s{};
            s.frame = a_frame;
            s.flags = Protocol::kBodyValid | (a_driven ? Protocol::kBodyDriven : 0);
            // Its damage stage (v18): the engine on fire, as ProcessCarOnFireAndExplode tells, or San
            // Andreas's own fire on the wreck.
            const bool wrecked = Wrecked(a_vehicle);
            if (wrecked) {
                s.flags |= Protocol::kBodyWrecked;
            }
            if (VehicleDamage::EngineBurns(a_vehicle->m_fHealth, wrecked, a_vehicle->bIsDrowning) || (wrecked && a_vehicle->m_pFire)) {
                s.flags |= Protocol::kBodyBurning;
            }
            s.health = a_vehicle->m_fHealth;
            const CVector engine = (EngineAt(a_vehicle) - at) * Pose::kUnitsPerMeter;
            s.engine[0] = engine.x;
            s.engine[1] = engine.y;
            s.engine[2] = engine.z;
            s.handover = a_handover;
            s.model = slot.model;
            s.boneCount = static_cast<std::uint32_t>(slot.parts.size());
            s.timeMs = NowMs();
            const CVector d = at - a_spawnFeet;
            s.feet[0] = d.x * Pose::kUnitsPerMeter;
            s.feet[1] = d.y * Pose::kUnitsPerMeter;
            s.feet[2] = d.z * Pose::kUnitsPerMeter;
            s.cameraForward[1] = 1.0f;  // unused, but a unit vector like everywhere else
            const float spin = SpinOf(a_vehicle);
            for (std::uint32_t i = 0; i < s.boneCount; i++) {
                s.bones[i] = Body::ToBone(ToFrame(*RwFrameGetLTM(slot.parts[i])), Vec(at));
                if (!Drawn(slot.atomics[i]) || !VehicleKinds::SpinnerShows(slot.spinners[i], spin)) {
                    s.hiddenParts |= 1ull << i;
                }
            }
            for (const auto& extra : slot.extras) {
                auto       rows = ToFrame(*RwFrameGetLTM(slot.parts[extra.front]));
                const auto back = ToFrame(*RwFrameGetLTM(slot.parts[extra.back]));
                rows.pos = VehicleKinds::Between(rows.pos, back.pos, extra.along);
                s.bones[extra.part] = Body::ToBone(rows, Vec(at));
            }
            if (Body::Finite(s)) {
                GetLink().WriteVehicle(static_cast<std::uint32_t>(a_index), s);
            }
        }

        // Steering in the air, stronger than San Andreas's (protocol/cpp/vehicle.h), for the vehicle CJ
        // drives with no wheel down, once a frame after San Andreas's own physics step: a car gets
        // San Andreas's own pushes again, as many more as ExtraAirPushes says, under its conditions (not
        // a helicopter or a plane); a motorbike yaws with left/right (BikeAirYaw), which San Andreas
        // leaves out (a bicycle yaws by its rules already). Not a hook on ProcessAI: hooked, San Andreas
        // stopped taking the keys in that vehicle altogether.
        void AirControl(CVehicle* a_vehicle, CPlayerPed* a_ped)
        {
            if (CTimer::m_UserPause || CTimer::m_CodePause || a_vehicle->m_nStatus != STATUS_PLAYER || a_vehicle->m_pDriver != a_ped ||
                a_ped->m_ePedState != PEDSTATE_DRIVING) {
                return;
            }
            auto* pad = a_ped->GetPadFromPlayer();
            if (!pad) {
                return;
            }
            const auto    type = a_vehicle->m_nVehicleSubClass;
            const CVector spin = a_vehicle->m_vecTurnSpeed;
            const CVector right = a_vehicle->GetRight(), forward = a_vehicle->GetForward(), up = a_vehicle->GetUp();
            const float   skill = 0.0007f * CStats::GetFatAndMuscleModifier(STAT_MOD_14);  // 14: driving skill
            if (a_vehicle->m_nVehicleClass == VEHICLE_BIKE) {
                if (type == VEHICLE_BMX || static_cast<CBike*>(a_vehicle)->m_nNumContactWheels != 0) {
                    return;
                }
                const float steer = pad->GetSteeringLeftRight() / 128.0f;
                if (const float push = Vehicle::BikeAirYaw(spin.Dot(up), steer); push != 0.0f) {
                    const float force = Vehicle::AirPushForce(skill, a_vehicle->m_fTurnMass) * CTimer::ms_fTimeStep;
                    a_vehicle->ApplyTurnForce(right * (steer * force * push), a_vehicle->m_vecCentreOfMass + forward);
                }
                return;
            }
            if (a_vehicle->m_nVehicleClass != VEHICLE_AUTOMOBILE || type == VEHICLE_HELI || type == VEHICLE_PLANE ||
                static_cast<CAutomobile*>(a_vehicle)->m_nNumContactWheels != 0 || CjCheats::Active(Cheats::SaCheat::CarsFly)) {
                return;  // flying cars (v20) take San Andreas's own flight controls in the air
            }
            const auto& mouse = CPad::NewMouseControllerState;
            const auto  steer = Vehicle::AirSteering(pad->GetSteeringLeftRight(), pad->GetSteeringUpDown(), CCamera::m_bUseMouse3rdPerson, mouse.x, mouse.y);
            const auto  push = Vehicle::ExtraAirPushes(spin.Dot(up), spin.Dot(forward), spin.Dot(right), steer, pad->GetHandBrake() != 0, pad->GetAccelerate() != 0);
            const float force = Vehicle::AirPushForce(type == VEHICLE_MTRUCK ? 0.0025f : skill, a_vehicle->m_fTurnMass) * CTimer::ms_fTimeStep;
            if (push.yaw != 0.0f) {
                a_vehicle->ApplyTurnForce(right * (steer.leftRight * force * push.yaw), a_vehicle->m_vecCentreOfMass + forward);
            }
            if (push.roll != 0.0f) {
                a_vehicle->ApplyTurnForce(right * (steer.leftRight * force * push.roll), a_vehicle->m_vecCentreOfMass + up);
            }
            if (push.pitch != 0.0f) {
                a_vehicle->ApplyTurnForce(up * (steer.upDown * force * push.pitch), a_vehicle->m_vecCentreOfMass + forward);
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
        const auto kind = KindOf(info, a_model);
        const auto traits = VehicleKinds::TraitsOf(kind);
        if (const auto listed = VehicleKinds::KindOf(static_cast<std::uint16_t>(a_model)); listed != kind) {
            Log(std::format("vehicle: the {} is a {} to San Andreas but a {} in AhShitRim's table", name, VehicleKinds::NameOf(kind), VehicleKinds::NameOf(listed)));
        }
        if (!traits.spawnable) {
            Log(std::format("vehicle: the {} is a {}, nothing spawned: {}", name, VehicleKinds::NameOf(kind), traits.refusal));
            return;
        }
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
        if (!FindSpot(a_ped, size, traits.maxUneven, spot, groundZ, tried)) {
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
            Log(std::format("vehicle: no way to make the {} (type {}), nothing spawned", name, static_cast<int>(info->m_nVehicleType)));
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
        Log(std::format("vehicle: {} ({}) spawned in slot {} at ({:.1f},{:.1f},{:.1f}), spot {} of {} tried, colours {}/{}", name, VehicleKinds::NameOf(kind),
            index, at.x, at.y, at.z, tried, Vehicle::kRings * std::size(Vehicle::kTurns), vehicle->m_nPrimaryColor, vehicle->m_nSecondaryColor));
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

    void Respray(CPlayerPed* a_ped)
    {
        CVehicle* vehicle = Driven(a_ped);
        if (!vehicle || Wrecked(vehicle)) {
            Log(vehicle ? "vehicle: PAINT, but CJ's vehicle is a wreck" : "vehicle: PAINT, but CJ is on foot");
            return;
        }
        auto* info = InfoOf(vehicle->m_nModelIndex);
        unsigned char colours[4] = { vehicle->m_nPrimaryColor, vehicle->m_nSecondaryColor, vehicle->m_nTertiaryColor, vehicle->m_nQuaternaryColor };
        if (info) {
            info->ChooseVehicleColour(colours[0], colours[1], colours[2], colours[3], 1);  // the model's next colour set
        }
        if (colours[0] == vehicle->m_nPrimaryColor && colours[1] == vehicle->m_nSecondaryColor) {
            colours[0] = static_cast<unsigned char>((colours[0] + 1 + std::rand() % 125) % 126);  // one set only: any colour of carcols' 126
            colours[1] = static_cast<unsigned char>(std::rand() % 126);
        }
        vehicle->m_nPrimaryColor = colours[0];
        vehicle->m_nSecondaryColor = colours[1];
        vehicle->m_nTertiaryColor = colours[2];
        vehicle->m_nQuaternaryColor = colours[3];
        vehicle->Fix();
        vehicle->m_fHealth = 1000.0f;
        vehicle->ExtinguishCarFire();
        for (int i = 0; i < static_cast<int>(Protocol::kVehicleMax); i++) {
            if (Live(i) == vehicle) {
                state.slots[i].model = 0;  // built again in its new colours
                state.slots[i].failed = false;
                Log(std::format("vehicle: the {} in slot {} resprayed {}/{}/{}/{} and mended", state.slots[i].name, i, colours[0], colours[1], colours[2], colours[3]));
            }
        }
    }

    void AddHydraulics(CPlayerPed* a_ped)
    {
        constexpr int kHydraulicsUpgrade = 1087;
        CVehicle*     vehicle = Driven(a_ped);
        if (!vehicle || vehicle->m_nVehicleClass != VEHICLE_AUTOMOBILE || vehicle->m_nVehicleSubClass == VEHICLE_HELI ||
            vehicle->m_nVehicleSubClass == VEHICLE_PLANE) {
            Log("vehicle: LOWRIDER, but CJ isn't in a car");
            return;
        }
        if (vehicle->m_nHandlingFlags.bHydraulicNone) {
            Log(std::format("vehicle: LOWRIDER, but the {} can't take hydraulics (its handling says so)", vehicle->m_nModelIndex));
            return;
        }
        if (!vehicle->m_nHandlingFlags.bHydraulicInst) {
            CStreaming::RequestModel(kHydraulicsUpgrade, 0x2 | 0x8);  // game required, keep in memory
            CStreaming::LoadAllRequestedModels(false);
            if (CStreaming::ms_aInfoForModel[kHydraulicsUpgrade].m_nLoadState == 1) {  // LOADSTATE_LOADED
                vehicle->AddVehicleUpgrade(kHydraulicsUpgrade);
            }
        }
        Log(vehicle->m_nHandlingFlags.bHydraulicInst ? std::format("vehicle: hydraulics on model {}", vehicle->m_nModelIndex) :
                                                       "vehicle: LOWRIDER: the hydraulics didn't take");
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

    void Recover(CPlayerPed* a_ped, const CVector& a_at)
    {
        GetOut(a_ped);
        for (int i = 0; i < static_cast<int>(Protocol::kVehicleMax); i++) {
            auto* vehicle = Live(i);
            if (!vehicle) {
                continue;
            }
            const auto& at = vehicle->GetPosition();
            if (PositionGuard::Sound({ at.x, at.y, at.z })) {
                continue;
            }
            if (vehicle == Driven(a_ped)) {
                vehicle->Teleport(a_at, true);  // also resets its rotation and suspension
                vehicle->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
                vehicle->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
                vehicle->m_vecFrictionMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
                vehicle->m_vecFrictionTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
                vehicle->m_vecForce = CVector(0.0f, 0.0f, 0.0f);
                vehicle->m_vecTorque = CVector(0.0f, 0.0f, 0.0f);
                Log(std::format("vehicle: CJ's {} was at a broken position, put back with him", state.slots[i].name));
            } else {
                Remove(i);
            }
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
            if (Wrecked(vehicle) && !slot.scorched) {
                // Blown up: San Andreas draws the wreck scorched, and has taken off what flew away (a wheel).
                Log(std::format("vehicle: the {} in slot {} blew up, its model built again scorched", slot.name, i));
                slot.scorched = true;
                slot.model = 0;
                slot.failed = false;
            }
            if (!slot.model && !slot.failed) {
                Build(i, vehicle);  // after its first PreRender, so the wheels sit where they belong
            }
            if (slot.model) {
                Publish(i, vehicle, vehicle == driven, spawnFeet, a_handover, a_frame);
            }
        }
        if (driven) {
            AirControl(driven, a_ped);
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
