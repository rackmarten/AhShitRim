// The coronas over a vehicle's burning lamps (protocol v36's lamps): what makes a lamp look lit in San Andreas is
// the glowing sprite it draws over it (CCoronas), not the lamp's texture, and Skyrim draws none. Made here: one model
// per vehicle with a quad per lamp, each on a bone of its own that sits just out from the lamp and turns to face the
// camera; added onto what's behind, white for the headlights and red for the tail lights. Each quad is a lamp's mesh
// (Protocol::kSkinMeshLamp), so ModelView lights it as it lights the lamp: shown while the lamp burns, its glow the
// lamp's (LampGlow, a tail lamp's twice as bright while braking), clear while it doesn't. Header-only and free of game
// types, so the protocol tests cover it; the SKSE plugin hands the blob to its ModelView and poses it every frame.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "body.h"
#include "skin_model.h"
#include "vehicle_hits.h"
#include "vehicle_lights.h"

namespace AhShitRim::CoronaModel
{
    inline constexpr std::uint32_t kTextureSize = 64;
    inline constexpr float         kOut = 4.0f;     // Skyrim units out from the lamp's dummy, clear of its glass
    inline constexpr float         kToward = 30.0f; // and toward the camera: a dummy sits inside the lamp (the ZR-350's pop-ups
                                                    // in the bonnet), and San Andreas draws its coronas over everything

    // Bone i is lamp VehicleLights::kLampBits[i]: front right, front left, rear right, rear left.
    inline constexpr std::uint32_t kLamps = 4;

    // A soft round glow: a bright core fading out to clear, in a_rgb (white in the very middle).
    inline std::vector<std::uint8_t> Texture(const std::uint8_t (&a_rgb)[3])
    {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kTextureSize) * kTextureSize * 4);
        for (std::uint32_t y = 0; y < kTextureSize; ++y) {
            for (std::uint32_t x = 0; x < kTextureSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / kTextureSize * 2.0f - 1.0f;
                const float v = (static_cast<float>(y) + 0.5f) / kTextureSize * 2.0f - 1.0f;
                const float r = std::sqrt(u * u + v * v);
                const float glow = std::pow(std::clamp(1.0f - r, 0.0f, 1.0f), 2.2f);         // the halo
                const float core = std::clamp(1.0f - r * 4.0f, 0.0f, 1.0f);                   // the lamp itself
                const float white = core * core;                                               // its middle whiter
                auto*       p = &rgba[(static_cast<std::size_t>(y) * kTextureSize + x) * 4];
                for (int c = 0; c < 3; ++c) {
                    p[c] = static_cast<std::uint8_t>(std::lround(static_cast<float>(a_rgb[c]) + (255.0f - static_cast<float>(a_rgb[c])) * white));
                }
                p[3] = static_cast<std::uint8_t>(std::lround(std::clamp(glow + core, 0.0f, 1.0f) * 255.0f));
            }
        }
        return rgba;
    }

    // The model: a quad per lamp in its bone's XZ plane (facing +Y), a_frontHalf and a_rearHalf units from its middle to
    // an edge. Its kind is a vehicle's, so ModelView treats its quads as lamp meshes.
    inline SkinModel::ModelIn Build(float a_frontHalf, float a_rearHalf)
    {
        SkinModel::ModelIn model;
        model.name = "Coronas";
        model.kind = Protocol::kSkinModelVehicle;
        model.boneIds = { 0, 1, 2, 3 };
        const std::uint8_t white[3] = { 255, 250, 225 };  // San Andreas's headlight corona, a warm white
        const std::uint8_t red[3] = { 255, 20, 10 };
        model.textures.push_back({ kTextureSize, kTextureSize, Texture(white) });
        model.textures.push_back({ kTextureSize, kTextureSize, Texture(red) });
        for (std::uint32_t lamp = 0; lamp < kLamps; ++lamp) {
            const bool          front = lamp < 2;
            const float         h = front ? a_frontHalf : a_rearHalf;
            SkinModel::MeshIn   mesh;
            mesh.texture = front ? 0 : 1;
            mesh.flags = Protocol::kSkinMeshBlend | Protocol::kSkinMeshAdditive | VehicleLights::MeshFlagsOf(VehicleLights::kLampBits[lamp]);
            mesh.color[3] = 0;  // clear while the lamp doesn't burn (ModelView makes a burning lamp's mesh solid)
            const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
            for (const auto& corner : corners) {
                Protocol::SkinVertex v{};
                v.position[0] = corner[0] * h;
                v.position[2] = corner[1] * h;
                v.normal[1] = 1.0f;
                v.uv[0] = corner[0] * 0.5f + 0.5f;
                v.uv[1] = 0.5f - corner[1] * 0.5f;
                v.bones[0] = static_cast<std::uint8_t>(lamp);
                v.weights[0] = 1.0f;
                mesh.vertices.push_back(v);
            }
            for (const int i : { 0, 1, 2, 0, 2, 3 }) {
                mesh.indices.push_back(static_cast<std::uint16_t>(i));
            }
            model.meshes.push_back(std::move(mesh));
        }
        return model;
    }

    // Where each lamp's corona goes this frame, for a vehicle drawn with a_frame (in the world, as a_camera): its lamps from
    // a_lights' dummies (the left ones mirrored, as DoLightEffectImpl places them), each kOut further out of its end,
    // turned to face the camera and pulled kToward closer to it, so the bodywork round a lamp doesn't hide it (San Andreas
    // draws coronas with no depth test). A bone is hidden while its lamp doesn't burn, while the camera is behind its end (a
    // headlight's corona seen from behind the car), or when the model has no such lamp. Translations relative to
    // a_origin (where ModelView shows the model). Returns how many show.
    inline std::uint32_t Pose(const Protocol::BodyLights& a_lights, const VehicleHits::Frame& a_frame, const Body::Vec3& a_camera,
        const Body::Vec3& a_origin, Protocol::BodyState& a_out)
    {
        a_out.boneCount = kLamps;
        a_out.hiddenParts = 0;
        std::uint32_t shown = 0;
        for (std::uint32_t lamp = 0; lamp < kLamps; ++lamp) {
            const bool          front = lamp < 2;
            const bool          left = lamp % 2 == 1;
            const std::int16_t* dummy = front ? a_lights.front : a_lights.rear;
            const float         out = front ? kOut : -kOut;
            const Body::Vec3    local{ static_cast<float>(left ? -dummy[0] : dummy[0]), static_cast<float>(dummy[1]) + out, static_cast<float>(dummy[2]) };
            const Body::Vec3    lampAt = VehicleHits::ToWorld(a_frame, local);
            const Body::Vec3    toCamera = Body::Sub(a_camera, lampAt);
            const Body::Vec3    facing = front ? a_frame.forward : Body::Scale(a_frame.forward, -1.0f);
            const bool          none = dummy[0] == 0 && dummy[1] == 0 && dummy[2] == 0;
            const bool          lit = (a_lights.lit & VehicleLights::kLampBits[lamp]) != 0;
            if (none || !lit || Body::Dot(toCamera, facing) <= 0.0f) {
                a_out.hiddenParts |= std::uint64_t{ 1 } << lamp;
            } else {
                ++shown;
            }
            // +Y (the quad's face) toward the camera, X level across it.
            const Body::Vec3 y = Body::Normalize(toCamera, facing);
            const Body::Vec3 at = Body::Add(lampAt, Body::Scale(y, (std::min)(kToward, std::sqrt(Body::Dot(toCamera, toCamera)) * 0.5f)));
            Body::Vec3       x = Body::Cross(y, Body::Vec3{ 0.0f, 0.0f, 1.0f });
            if (Body::Dot(x, x) < 1e-6f) {
                x = Body::Vec3{ 1.0f, 0.0f, 0.0f };
            }
            const Body::Quat q = Body::FromAxes(x, y);
            auto&            bone = a_out.bones[lamp];
            bone.rotation[0] = q.x;
            bone.rotation[1] = q.y;
            bone.rotation[2] = q.z;
            bone.rotation[3] = q.w;
            const Body::Vec3 t = Body::Sub(at, a_origin);
            bone.translation[0] = t.x;
            bone.translation[1] = t.y;
            bone.translation[2] = t.z;
        }
        return shown;
    }
}
