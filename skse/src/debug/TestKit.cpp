// Built only with AHSHITRIM_TEST_KIT (dev builds); TestKitOff.cpp stands in otherwise.
#include "debug/TestKit.h"

#include "Ini.h"
#include "aim.h"
#include "body/ModelView.h"
#include "body/MuzzleFlash.h"
#include "debug/HitTest.h"
#include "debug/LoadTest.h"
#include "debug/StreamTest.h"
#include "hud/Overlay.h"
#include "hud/SaHud.h"
#include "hud/SkyrimMessages.h"
#include "link/LinkService.h"
#include "player/Arms.h"
#include "player/Headlights.h"
#include "player/Missions.h"
#include "player/ObjectHits.h"
#include "player/PlayerSync.h"
#include "ride.h"
#include "player/Shots.h"
#include "player/StealthKills.h"
#include "player/Respawns.h"
#include "vehicle_hits.h"
#include "vehicle_kinds.h"
#include "wanted.h"
#include "weapons.h"

// The Grove Street crew's command builds once the crew is in (player/Homies.h, protocol v39); before, asr.crew isn't
// there and scenarios/crew-follow.json fails at its first asr.crew step.
#if __has_include("player/Homies.h")
#    include "player/Homies.h"
#    define AHSHITRIM_TEST_CREW 1
#endif

#include <skse_test_kit/Kit.h>

#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

namespace AhShitRim::TestKit
{
    namespace
    {
        using SkseTestKit::Call;
        using SkseTestKit::Error;
        using SkseTestKit::Json;
        using SkseTestKit::JsonArray;
        using SkseTestKit::JsonObject;
        using SkseTestKit::Step;

        bool g_cj = false;
        int  g_wait = 0;  // frames until the next try at the handover
        std::optional<std::uint32_t> g_spawned;  // the slot asr.spawn found last, for the asr.vehicle target

        void RunConsole(const std::string& a_command)
        {
            auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
            auto* script = factory ? factory->Create() : nullptr;
            if (!script) {
                throw Error("no console script to run it with");
            }
            script->SetCommand(a_command);
            script->CompileAndRun(nullptr);
            delete script;
            SKSE::log::info("test kit: asr ran '{}'", a_command);
        }

        void Arrived(const SkseTestKit::Settings& a_settings)
        {
            const auto cj = a_settings.values.find("CJ");
            g_cj = cj != a_settings.values.end() && cj->second == "1";
        }

        // [Test] CJ=1: San Andreas takes the player as F10 hands it over, once the link is up (San Andreas may still be
        // starting), tried again every 2 s until it has it.
        bool CjReady()
        {
            if (!g_cj || PlayerSync::DebugOwned()) {
                return true;
            }
            if (g_wait > 0) {
                --g_wait;
                return false;
            }
            g_wait = 120;
            if (LinkService::Get().IsUp()) {
                SKSE::log::info("test kit: handing the player to San Andreas ([Test] CJ=1)");
                PlayerSync::DebugToggle();
            }
            return PlayerSync::DebugOwned();
        }

        Json Point(const float (&a_point)[3])
        {
            return JsonObject{ { "x", a_point[0] }, { "y", a_point[1] }, { "z", a_point[2] } };
        }

        // What each vehicle slot holds: its streamed model (BodyState.model, not San Andreas's model id), 0 when empty.
        using Slots = std::array<std::uint32_t, Protocol::kVehicleMax>;

        Slots ReadSlots()
        {
            Slots slots{};
            for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
                Protocol::BodyState vehicle{};
                if (LinkService::Get().ReadVehicle(i, vehicle) && (vehicle.flags & Protocol::kBodyValid)) {
                    slots[i] = vehicle.model;
                }
            }
            return slots;
        }

        void RequireCj()
        {
            if (!PlayerSync::DebugOwned()) {
                throw Error("San Andreas doesn't have the player: asr.cj first");
            }
        }

        const char* CueName(std::uint8_t a_cue)
        {
            switch (a_cue) {
            case Protocol::kCueMissionPassed: return "mission passed tune";
            case Protocol::kCueIntroLine: return "intro line";
            default: return "";
            }
        }

        Json Percent(int a_part, int a_whole) { return a_whole ? std::round(a_part * 1000.0 / a_whole) / 10.0 : 0.0; }

        Json DescribeSecond(const StreamTest::Second& a_s)
        {
            return JsonObject{ { "at", a_s.at }, { "frames", a_s.frames }, { "fps", a_s.window > 0.0f ? a_s.frames / a_s.window : 0.0f },
                { "worstMs", a_s.worst * 1000.0f }, { "over50", a_s.over50 }, { "over100", a_s.over100 },
                { "underAttached", Percent(a_s.underAttached, a_s.samples) }, { "underLand", Percent(a_s.underLand, a_s.samples) },
                { "ahead1Attached", Percent(a_s.ahead1Attached, a_s.samples) }, { "ahead1Land", Percent(a_s.ahead1Land, a_s.samples) },
                { "ahead2Attached", Percent(a_s.ahead2Attached, a_s.samples) }, { "ahead2Land", Percent(a_s.ahead2Land, a_s.samples) } };
        }

        // ---- The feature checks (scenarios/vehicle-lights.json, muzzle-flash.json, crew-follow.json) ----

        // The vehicle slot CJ drives in this handover, if any.
        std::optional<std::uint32_t> DrivenSlot()
        {
            for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
                Protocol::BodyState vehicle{};
                if (LinkService::Get().ReadVehicle(i, vehicle) && (vehicle.flags & Protocol::kBodyValid) && (vehicle.flags & Protocol::kBodyDriven) &&
                    vehicle.handover == PlayerSync::DebugHandover()) {
                    return i;
                }
            }
            return std::nullopt;
        }

        Json Point(const RE::NiPoint3& a_p) { return JsonObject{ { "x", a_p.x }, { "y", a_p.y }, { "z", a_p.z } }; }
        Json Point(const Body::Vec3& a_p) { return JsonObject{ { "x", a_p.x }, { "y", a_p.y }, { "z", a_p.z } }; }
        double Round(double a_v, double a_step = 0.01) { return std::round(a_v / a_step) * a_step; }

        // Whether this build's vehicle slots carry their lamps (BodyState.lights, protocol v36): asr.lamps and the lamp
        // details of asr.vehicle work once they do. A variable template, so the check compiles either way.
        template <class T>
        constexpr bool kHasLamps = requires(T& a_pose) {
            a_pose.lights.lit;
            a_pose.lights.state;
            a_pose.lights.front[0];
            a_pose.lights.rear[0];
        };

        // What each vehicle slot drew in the last frame, seen through PlayerSync's vehicle hook.
        struct Seen
        {
            bool               on = false;
            VehicleHits::Frame frame;
            std::uint32_t      flags = 0;
            float              health = 0.0f;       // San Andreas's vehicle health (1000 new)
            float              boxBottom = 0.0f;    // the collision box's bottom, in the vehicle's frame, Skyrim units
            bool               lamps = false;  // the slot carries lamps (kHasLamps)
            std::uint8_t       lit = 0;        // the lamps burning as San Andreas sent them (asr.lamps off draws none of them)
            std::uint8_t       lampState = 0;
            Body::Vec3         front{}, rear{};  // the headlight and tail light dummies, in the vehicle's frame
        };
        std::array<Seen, Protocol::kVehicleMax> g_seen;
        bool                                    g_lampsOff = false;  // asr.lamps off: drawn and lit as if every lamp were out
        std::optional<bool>                     g_braking;            // asr.lamps brake / nobrake: drawn and lit as if braking, or not

        template <class T>
        void SeeVehicle(std::uint32_t a_slot, T& a_pose, const RE::NiPoint3& a_feet)
        {
            if (a_slot >= g_seen.size()) {
                return;
            }
            auto& seen = g_seen[a_slot];
            seen.on = true;
            seen.frame = VehicleHits::FrameOf(a_pose.vehicleFrame, { a_feet.x, a_feet.y, a_feet.z });
            seen.flags = a_pose.flags;
            seen.health = a_pose.health;
            seen.boxBottom = a_pose.boxMin[2];
            if constexpr (kHasLamps<T>) {
                seen.lamps = true;
                seen.lit = a_pose.lights.lit;
                seen.lampState = a_pose.lights.state;
                seen.front = { static_cast<float>(a_pose.lights.front[0]), static_cast<float>(a_pose.lights.front[1]), static_cast<float>(a_pose.lights.front[2]) };
                seen.rear = { static_cast<float>(a_pose.lights.rear[0]), static_cast<float>(a_pose.lights.rear[1]), static_cast<float>(a_pose.lights.rear[2]) };
                if (g_lampsOff) {
                    a_pose.lights.lit = 0;
                }
                if (g_braking) {
                    a_pose.lights.state = static_cast<std::uint8_t>(*g_braking ? (a_pose.lights.state | Protocol::kLightBraking) : (a_pose.lights.state & ~Protocol::kLightBraking));
                    // The tail lamps burn for the brake by day too (VehicleLights::Lit). Both bits: a bike's one tail light
                    // sits on its middle line, where the mirrored one falls too.
                    const bool rear = a_pose.lights.rear[0] != 0 || a_pose.lights.rear[1] != 0 || a_pose.lights.rear[2] != 0;
                    if (*g_braking && rear && !g_lampsOff && (a_pose.flags & Protocol::kBodyWrecked) == 0) {
                        a_pose.lights.lit |= static_cast<std::uint8_t>(Protocol::kLightRearRight | Protocol::kLightRearLeft);
                    }
                }
            }
        }

        // Skyrim's ground under a point: what a ray down through the line-of-sight layer hits first, from a_above units above
        // it to a_below below: the land and the statics on it (the cobbles of a village), not what grows or stands on it
        // (trees, bushes, clutter, people), which the ray goes on through.
        std::optional<float> GroundUnder(const RE::NiPoint3& a_at, float a_above = 20.0f, float a_below = 600.0f)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            auto* bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto* world = bhk ? bhk->GetWorld1() : nullptr;
            if (!world) {
                return std::nullopt;
            }
            RE::BSReadLockGuard lock(bhk->worldLock);
            const float         scale = RE::bhkWorld::GetWorldScale();
            const float         bottom = a_at.z - a_below;
            float               top = a_at.z + a_above;
            for (int pass = 0; pass < 8 && top > bottom; pass++) {
                RE::hkpWorldRayCastInput input;
                input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
                input.from = RE::hkVector4(a_at.x * scale, a_at.y * scale, top * scale, 0.0f);
                input.to = RE::hkVector4(a_at.x * scale, a_at.y * scale, bottom * scale, 0.0f);
                RE::hkpWorldRayCastOutput output;
                world->CastRay(input, output);
                if (!output.HasHit()) {
                    return std::nullopt;
                }
                const float z = top + (bottom - top) * output.hitFraction;
                switch (output.rootCollidable ? output.rootCollidable->GetCollisionLayer() : RE::COL_LAYER::kUnidentified) {
                case RE::COL_LAYER::kTrees:
                case RE::COL_LAYER::kClutter:
                case RE::COL_LAYER::kProps:
                case RE::COL_LAYER::kDebrisSmall:
                case RE::COL_LAYER::kDebrisLarge:
                case RE::COL_LAYER::kBiped:
                case RE::COL_LAYER::kDeadBip:
                case RE::COL_LAYER::kCharController:
                case RE::COL_LAYER::kTransparent:
                case RE::COL_LAYER::kTransparentSmall:
                    top = z - 1.0f;
                    continue;
                default: return z;
                }
            }
            return std::nullopt;
        }

        RE::NiNode* WorldScene()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* root = player ? player->Get3D() : nullptr;
            return root ? root->parent : nullptr;
        }

        // How well a lamp light at a_world fits a vehicle drawn with a_frame: player/Headlights.cpp puts each on the
        // vehicle's middle line (x 0), ahead of the headlights or behind the tail lights at about their height. Lower fits
        // better.
        double LampFit(const VehicleHits::Frame& a_frame, const RE::NiPoint3& a_world)
        {
            const auto local = VehicleHits::ToLocal(a_frame, { a_world.x, a_world.y, a_world.z });
            return std::abs(local.x) + std::abs(local.z) + (std::max)(0.0, std::abs(static_cast<double>(local.y)) - 1200.0);
        }

        // Vehicle slot a_slot's scene lights: AhShitRim's headlight and tail light point lights (named so by
        // player/Headlights.cpp), which hang in the world's scene graph, not under the vehicle's node. A slot has one of
        // each; the lights of other slots stay where they were last put (out, once their vehicle goes), so of each name
        // the one that fits this vehicle best, and only one no other drawn vehicle fits better.
        std::vector<RE::NiPointLight*> LampLightsOf(std::uint32_t a_slot)
        {
            std::vector<RE::NiPointLight*> out;
            auto*                          scene = WorldScene();
            if (!scene || !g_seen[a_slot].on) {
                return out;
            }
            for (const std::string_view wanted : { std::string_view("AhShitRimHeadlight"), std::string_view("AhShitRimTailLight") }) {
                RE::NiPointLight* best = nullptr;
                double            bestFit = 400.0;
                for (auto& child : scene->GetChildren()) {
                    auto* light = child ? netimmerse_cast<RE::NiPointLight*>(child.get()) : nullptr;
                    if (!light || std::string_view(light->name.c_str()) != wanted) {
                        continue;
                    }
                    const double fit = LampFit(g_seen[a_slot].frame, light->world.translate);
                    if (fit >= bestFit) {
                        continue;
                    }
                    bool theirs = false;
                    for (std::uint32_t other = 0; !theirs && other < Protocol::kVehicleMax; other++) {
                        RE::NiPoint3 position;
                        float        heading = 0.0f;
                        theirs = other != a_slot && g_seen[other].on && PlayerSync::DebugVehicle(other, position, heading) &&
                                 LampFit(g_seen[other].frame, light->world.translate) < fit;
                    }
                    if (!theirs) {
                        best = light;
                        bestFit = fit;
                    }
                }
                if (best) {
                    out.push_back(best);
                }
            }
            return out;
        }

        // A whole number from a target's argument text ("" or anything else after the number refuses).
        std::optional<std::uint32_t> WholeNumber(std::string_view a_text)
        {
            std::uint32_t n = 0;
            const auto [end, error] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), n);
            if (a_text.empty() || error != std::errc{} || end != a_text.data() + a_text.size()) {
                return std::nullopt;
            }
            return n;
        }

        // A hand from a target's argument: "" or 0 the right, 1 the left.
        std::uint32_t HandOfTarget(std::string_view a_text)
        {
            if (a_text.empty()) {
                return 0;
            }
            const auto hand = WholeNumber(a_text);
            if (!hand || *hand > 1) {
                throw Error("the hand is 0 (right) or 1 (left)");
            }
            return *hand;
        }

        std::array<RE::FormID, 8> g_homies{};  // asr.homie:<n>'s follower, pinned the first time it's found (asr.crew lets go)

        Json LampNames(std::uint8_t a_bits)
        {
            JsonArray                                          out;
            const std::pair<std::uint8_t, const char*> names[] = { { 0x1, "frontRight" }, { 0x2, "frontLeft" }, { 0x4, "rearRight" }, { 0x8, "rearLeft" } };
            for (const auto& [bit, name] : names) {
                if (a_bits & bit) {
                    out.emplace_back(name);
                }
            }
            return out;
        }

        // asr.vehicle's details beyond slot and model: wrecked, its lamps (which burn, the dummies in its frame: x right,
        // y forward, z up) and its scene lights, each with where it is in the vehicle's frame.
        void DescribeLamps(std::uint32_t a_slot, JsonObject& a_details, std::vector<RE::NiLight*>& a_lights)
        {
            const auto& seen = g_seen[a_slot];
            if (!seen.on) {
                return;
            }
            a_details.Set("wrecked", (seen.flags & Protocol::kBodyWrecked) != 0);
            if (!seen.lamps) {
                return;
            }
            a_details.Set("lamps", JsonObject{ { "lit", LampNames(seen.lit) }, { "litCount", std::popcount(seen.lit) }, { "braking", (seen.lampState & 0x1) != 0 },
                                       { "forcedOff", g_lampsOff }, { "forcedBraking", g_braking ? Json(*g_braking) : Json() }, { "frontDummy", Point(seen.front) }, { "rearDummy", Point(seen.rear) } });
            JsonArray lights;
            for (auto* light : LampLightsOf(a_slot)) {
                const auto& data = light->GetLightRuntimeData();
                const auto& w = light->world.translate;
                const auto  local = VehicleHits::ToLocal(seen.frame, { w.x, w.y, w.z });
                lights.emplace_back(JsonObject{ { "name", light->name.c_str() }, { "fade", Round(data.fade) }, { "radius", Round(data.radius.x, 0.1) },
                    { "color", JsonObject{ { "r", Round(data.diffuse.red) }, { "g", Round(data.diffuse.green) }, { "b", Round(data.diffuse.blue) } } },
                    { "world", Point(w) }, { "local", Point(local) } });
                a_lights.push_back(light);
            }
            a_details.Set("lights", std::move(lights));
        }

        // CJ's pose for this handover, or nothing.
        std::optional<Protocol::PlayerState> CjPose()
        {
            Protocol::PlayerState pose{};
            if (!LinkService::Get().ReadPlayer(pose) || pose.handover != PlayerSync::DebugHandover() || (pose.flags & Protocol::kPlayerValid) == 0) {
                return std::nullopt;
            }
            return pose;
        }

        // How far the camera sees along San Andreas's camera's forward before something solid (land, a tree, a building;
        // not people, the hidden player among them), from the camera the frame is rendered with; a_max when nothing is.
        float ClearAhead(const float a_forward[3], float a_max)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* camera = RE::Main::WorldRootCamera();
            auto* cell = player ? player->GetParentCell() : nullptr;
            auto* bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto* world = bhk ? bhk->GetWorld1() : nullptr;
            if (!camera || !world) {
                return -1.0f;
            }
            const auto&              from = camera->world.translate;
            const RE::NiPoint3       to = from + RE::NiPoint3(a_forward[0], a_forward[1], a_forward[2]) * a_max;
            const float              scale = RE::bhkWorld::GetWorldScale();
            RE::hkpWorldRayCastInput input;
            input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
            RE::BSReadLockGuard lock(bhk->worldLock);
            float               start = 0.0f;
            for (int i = 0; i < 4 && start < 1.0f; i++) {
                const RE::NiPoint3 a = from + (to - from) * start;
                input.from = RE::hkVector4(a.x * scale, a.y * scale, a.z * scale, 0.0f);
                input.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0f);
                RE::hkpWorldRayCastOutput output;
                world->CastRay(input, output);
                if (!output.HasHit()) {
                    return a_max;
                }
                const float at = start + (1.0f - start) * output.hitFraction;
                auto*       hit = output.rootCollidable ? RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable) : nullptr;
                if (!hit || !hit->IsActor()) {
                    return at * a_max;
                }
                start = at + 1.0f / a_max;
            }
            return a_max;
        }

        // asr.camera: San Andreas's camera against CJ and his vehicle, from the pose and the body (both relative to the
        // handover's anchor, Skyrim units).
        Json CameraNow()
        {
            const auto pose = CjPose();
            Protocol::BodyState body{};
            if (!pose || !LinkService::Get().ReadBody(body) || (body.flags & Protocol::kBodyValid) == 0) {
                throw Error("San Andreas has no pose or camera for CJ (asr.cj first)");
            }
            const float cam[3] = { body.cameraPosition[0] - pose->offset[0], body.cameraPosition[1] - pose->offset[1],
                body.cameraPosition[2] - pose->offset[2] };  // from CJ's feet
            JsonObject out{ { "mode", static_cast<int>(pose->cameraMode) }, { "firstPerson", pose->cameraMode == Weapons::kCamFirstPerson },
                { "fromFeet", Round(std::hypot(cam[0], cam[1]), 0.1) }, { "height", Round(cam[2], 0.1) },
                { "forward3", JsonObject{ { "x", Round(body.cameraForward[0]) }, { "y", Round(body.cameraForward[1]) }, { "z", Round(body.cameraForward[2]) } } },
                { "inVehicle", (pose->flags & Protocol::kPlayerInVehicle) != 0 }, { "model", static_cast<int>(pose->vehicleModel) },
                { "ahead", Round(ClearAhead(body.cameraForward, 3000.0f), 1.0) } };
            const auto scope = Weapons::ScopeOf(pose->cameraMode, (pose->flags & Protocol::kPlayerInVehicle) ? pose->vehicleModel : 0);
            constexpr const char* kScopes[] = { "none", "sniper", "rocket", "rifle", "camera", "aim" };
            out.Set("scope", static_cast<std::size_t>(scope) < std::size(kScopes) ? kScopes[static_cast<std::size_t>(scope)] : "?");
            if ((pose->flags & Protocol::kPlayerInVehicle) && pose->vehicleModel) {
                const float h = pose->vehicleHeading;
                const float rel[3] = { cam[0] - pose->vehicleOffset[0], cam[1] - pose->vehicleOffset[1], cam[2] - pose->vehicleOffset[2] };
                out.Set("forward", Round(rel[0] * std::sin(h) + rel[1] * std::cos(h), 0.1));
                out.Set("right", Round(rel[0] * std::cos(h) - rel[1] * std::sin(h), 0.1));
                out.Set("up", Round(rel[2], 0.1));
                out.Set("looksAhead", Round(body.cameraForward[0] * std::sin(h) + body.cameraForward[1] * std::cos(h)));
            }
            return out;
        }

        std::uint32_t HandOf(Call& a_call)
        {
            const double hand = a_call.Number("hand", 0.0);
            if (hand != 0.0 && hand != 1.0) {
                throw Error("hand is 0 (right) or 1 (left, twin guns)");
            }
            return static_cast<std::uint32_t>(hand);
        }

        // The weapon in a hand as drawn, in its own frame (the barrel along its X: San Andreas fires along the hand's X,
        // and the gun's model sits on it as it is): the muzzle's end is the farthest vertex along X, at the middle of the
        // ring of vertices within 1 unit of it.
        struct Barrel
        {
            Protocol::BodyState pose{};
            VehicleHits::Frame  frame;
            Body::Vec3          tipLocal{};
            Body::Vec3          tip{};  // world
            std::size_t         vertices = 0;
            double              heading = 0.0;  // the barrel's, radians as a reference's angle z
            double              pitch = 0.0;    // degrees above level
            double              roll = 0.0;     // degrees the gun's top leans right of up about its barrel (Aim::RollOf)
            bool                turned = false; // its first part faces back along the barrel, read turned round
        };

        Barrel BarrelOf(std::uint32_t a_hand)
        {
            Barrel       out;
            RE::NiPoint3 feet;
            if (!Arms::DebugWeapon(a_hand, out.pose, feet)) {
                throw Error(std::format("no weapon is drawn in hand {} (asr.give, asr.fire weapon=)", a_hand));
            }
            const auto& t = out.pose.bones[0].translation;
            out.frame = VehicleHits::FrameOf(out.pose.bones[0], { feet.x + t[0], feet.y + t[1], feet.z + t[2] });
            // A model whose first part faces back along the barrel (the sawn-off's) is turned round about its Z, so its
            // X points where San Andreas's camera looks, the way the gun fires: otherwise its stock would read as the muzzle.
            // Only while he aims: then the gun points where the camera looks, hanging down or from the hip it needn't.
            if (const auto pose = CjPose(); pose && (pose->weaponFlags & Protocol::kWeaponAiming) != 0) {
                const auto& b = out.frame.right;
                if (b.x * std::sin(pose->viewHeading) + b.y * std::cos(pose->viewHeading) < 0.0f) {
                    out.frame.right = Body::Scale(out.frame.right, -1.0f);
                    out.frame.forward = Body::Scale(out.frame.forward, -1.0f);
                    out.turned = true;
                }
            }
            std::vector<RE::NiPoint3> vertices;
            if (!ModelView::DebugPosed(out.pose.model, vertices) || vertices.empty()) {
                throw Error("the weapon's model isn't drawn");
            }
            std::vector<Body::Vec3> local;
            local.reserve(vertices.size());
            float maxX = -(std::numeric_limits<float>::max)();
            for (const auto& v : vertices) {
                local.push_back(VehicleHits::ToLocal(out.frame, { feet.x + v.x, feet.y + v.y, feet.z + v.z }));
                maxX = (std::max)(maxX, local.back().x);
            }
            double y = 0.0, z = 0.0;
            int    ring = 0;
            for (const auto& l : local) {
                if (l.x >= maxX - 1.0f) {
                    y += l.y;
                    z += l.z;
                    ring++;
                }
            }
            out.tipLocal = { maxX, static_cast<float>(y / ring), static_cast<float>(z / ring) };
            out.tip = VehicleHits::ToWorld(out.frame, out.tipLocal);
            out.vertices = vertices.size();
            const auto& b = out.frame.right;  // its X
            out.heading = std::atan2(b.x, b.y);
            out.pitch = std::atan2(b.z, std::hypot(b.x, b.y)) * 180.0 / std::numbers::pi;
            // Its top is its Z, the left hand's gun included: the drawn gun's frame is already turned over for it.
            out.roll = Aim::RollOf(out.frame.right, out.frame.up) * 180.0 / std::numbers::pi;
            return out;
        }

        // asr.muzzle's answer: the weapon's barrel, the flash, and the flash against the muzzle's end (units; 1 unit = 1.43 cm).
        Json MuzzleCheck(std::uint32_t a_hand)
        {
            const auto barrel = BarrelOf(a_hand);
            const auto flash = MuzzleFlash::Debug(a_hand);
            JsonObject out{ { "hand", a_hand },
                { "weapon", JsonObject{ { "model", std::format("{:08X}", barrel.pose.model) }, { "vertices", barrel.vertices },
                                { "origin", Point(barrel.frame.pos) }, { "tip", Point(barrel.tip) }, { "tipLocal", Point(barrel.tipLocal) },
                                { "heading", Round(barrel.heading * 180.0 / std::numbers::pi, 0.1) }, { "pitch", Round(barrel.pitch, 0.1) }, { "roll", Round(barrel.roll, 0.1) }, { "turned", barrel.turned } } } };
            JsonObject flashJson{ { "shown", flash.shown }, { "fired", flash.fired }, { "left", Round(flash.left, 0.001) }, { "strength", Round(flash.strength) } };
            if (flash.fired > 0) {
                flashJson.Set("at", Point(flash.at));
            }
            // The offsets only for a flash shown in the last frame: an older one's place says nothing of the gun now.
            if (flash.shown) {
                const auto local = VehicleHits::ToLocal(barrel.frame, { flash.at.x, flash.at.y, flash.at.z });
                flashJson.Set("local", Point(local));
                // along: past the muzzle's end (+) or short of it (-); across: off the barrel's line through the muzzle.
                const double along = local.x - barrel.tipLocal.x;
                const double across = std::hypot(local.y - barrel.tipLocal.y, local.z - barrel.tipLocal.z);
                out.Set("along", Round(along));
                out.Set("across", Round(across));
                out.Set("distance", Round(std::hypot(along, across)));
            }
            if (flash.light) {
                const auto& data = flash.light->GetLightRuntimeData();
                flashJson.Set("light", JsonObject{ { "fade", Round(data.fade) }, { "at", Point(flash.light->world.translate) },
                                           { "fromFlash", Round(flash.light->world.translate.GetDistance(flash.at)) } });
            }
            out.Set("flash", std::move(flashJson));
            return out;
        }

        struct Waypoint
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        float ParseFloat(std::string_view a_s)
        {
            while (!a_s.empty() && a_s.front() == ' ') {
                a_s.remove_prefix(1);
            }
            while (!a_s.empty() && a_s.back() == ' ') {
                a_s.remove_suffix(1);
            }
            float      value = 0.0f;
            const auto [end, error] = std::from_chars(a_s.data(), a_s.data() + a_s.size(), value);
            if (a_s.empty() || error != std::errc{} || end != a_s.data() + a_s.size()) {
                throw Error(std::format("'{}' isn't a number", a_s));
            }
            return value;
        }

        // "x,y; x,y; ..."
        std::vector<Waypoint> ParseWaypoints(std::string_view a_text)
        {
            std::vector<Waypoint> out;
            while (!a_text.empty()) {
                const auto end = a_text.find(';');
                const auto point = a_text.substr(0, end);
                a_text = end == std::string_view::npos ? std::string_view{} : a_text.substr(end + 1);
                if (point.find_first_not_of(' ') == std::string_view::npos) {
                    continue;  // a trailing ';'
                }
                const auto comma = point.find(',');
                if (comma == std::string_view::npos) {
                    throw Error("points are x,y; x,y; ...");
                }
                out.push_back({ ParseFloat(point.substr(0, comma)), ParseFloat(point.substr(comma + 1)) });
            }
            if (out.empty()) {
                throw Error("no points");
            }
            return out;
        }

        void AddFeatureChecks()
        {
            PlayerSync::SetDebugVehicleHook([](std::uint32_t a_slot, Protocol::BodyState& a_pose, const RE::NiPoint3& a_feet) { SeeVehicle(a_slot, a_pose, a_feet); });

            SkseTestKit::AddCommand("asr.lamps", "<state>",
                "Vehicle lamps for an A/B pair: off draws and lights every vehicle as if none of its lamps burned (their glow and their "
                "scene lights), brake as if its driver braked (the tail lamps' brake glow and light), nobrake as if he didn't; on gives "
                "San Andreas's back. Needs a build whose vehicles carry their lamps (protocol v36)",
                [](Call& a_call) -> Json {
                    if constexpr (!kHasLamps<Protocol::BodyState>) {
                        throw Error("this build's vehicles carry no lamps (the headlights, protocol v36, aren't in it)");
                    } else {
                        const auto which = a_call.Text("state");
                        if (which == "on") {
                            g_lampsOff = false;
                            g_braking.reset();
                        } else if (which == "off") {
                            g_lampsOff = true;
                        } else if (which == "brake" || which == "nobrake") {
                            g_braking = which == "brake";
                        } else {
                            throw Error("asr.lamps on, off, brake or nobrake");
                        }
                        return JsonObject{ { "lampsOff", g_lampsOff }, { "braking", g_braking ? Json(*g_braking) : Json() } };
                    }
                });

            SkseTestKit::AddTask("asr.oil", "[seconds=<s>]",
                "Oil burning GTA style: places three fresh oil pools (TrapOilPool01) at three of the pools on Bleak Falls Barrow's "
                "first floor (the player there first: coc BleakFallsBarrow01, player.moveto 0009B0E6), lights the first by a "
                "bullet landing, the second by a Molotov's burst and the third by a rocket's blast, through what CJ's own "
                "shots and projectiles call (ObjectHits), and answers once all three burnt out, or after seconds (30): per "
                "pool its lowest health (of 100; it bursts into flame at 94 and is gone at 7) and whether it burned",
                [](Call& a_call) -> Step {
                    constexpr std::uint32_t kOilPool = 0x000862CC;
                    constexpr std::uint32_t kSpots[3] = { 0x0009B0E6, 0x0009B0E0, 0x0009B0DE };
                    const char*             by[3] = { "a bullet", "a Molotov", "a rocket" };
                    auto*                   base = RE::TESForm::LookupByID<RE::TESBoundObject>(kOilPool);
                    if (!base) {
                        throw Error("no TrapOilPool01 (000862CC)");
                    }
                    struct Pool
                    {
                        RE::ObjectRefHandle ref;
                        const char*         by = "";
                        float               lowest = 100.0f;
                        bool                burned = false;
                        bool                gone = false;
                    };
                    auto pools = std::make_shared<std::vector<Pool>>();
                    for (int i = 0; i < 3; i++) {
                        auto* at = RE::TESForm::LookupByID<RE::TESObjectREFR>(kSpots[i]);
                        auto  made = at ? at->PlaceObjectAtMe(base, false) : nullptr;
                        if (!made) {
                            throw Error(std::format("couldn't place oil at {:08X}: not in Bleak Falls Barrow (coc BleakFallsBarrow01)?", kSpots[i]));
                        }
                        pools->push_back({ made->GetHandle(), by[i] });
                    }
                    const double seconds = a_call.Number("seconds", 30.0);
                    a_call.DefaultTimeout(seconds + 15.0);
                    auto lit = std::make_shared<bool>(false);
                    const auto health = [](RE::TESObjectREFR* a_ref) {
                        const auto* extra = a_ref->extraList.GetByType<RE::ExtraObjectHealth>();
                        return extra ? extra->health : 100.0f;
                    };
                    return [=](Call& a_call) -> std::optional<Json> {
                        auto* player = RE::PlayerCharacter::GetSingleton();
                        if (!*lit) {
                            if (a_call.Seconds() < 4.0) {
                                return std::nullopt;  // the pools' 3D loads
                            }
                            for (std::size_t i = 0; i < pools->size(); i++) {
                                auto  ref = (*pools)[i].ref.get();
                                auto* root = ref ? ref->Get3D() : nullptr;
                                if (!root) {
                                    throw Error(std::format("the oil for {} has no 3D", (*pools)[i].by));
                                }
                                const auto at = root->worldBound.center;
                                if (i == 0) {
                                    ObjectHits::BulletLanded(player, at, Weapons::Pistol);
                                } else {
                                    ObjectHits::Around(player, at, 5.0f, i == 1 ? Traps::Hit::Fire : Traps::Hit::Blast, 0.0f);  // just this pool
                                }
                            }
                            *lit = true;
                            return std::nullopt;
                        }
                        bool all = true;
                        for (auto& pool : *pools) {
                            const auto ref = pool.ref.get();
                            if (!ref || ref->IsDisabled()) {
                                pool.gone = true;
                                continue;
                            }
                            const float h = health(ref.get());
                            pool.lowest = (std::min)(pool.lowest, h);
                            pool.burned = pool.burned || (h < 94.0f && h > 7.0f);
                            all = false;
                        }
                        if (!all && a_call.Seconds() < seconds + 4.0) {
                            return std::nullopt;
                        }
                        JsonArray out;
                        int       burned = 0;
                        for (const auto& pool : *pools) {
                            burned += pool.burned ? 1 : 0;
                            out.push_back(JsonObject{ { "by", pool.by }, { "lowest", Round(pool.lowest, 0.1) }, { "burned", pool.burned }, { "gone", pool.gone } });
                        }
                        return JsonObject{ { "pools", std::move(out) }, { "burned", burned }, { "seconds", Round(a_call.Seconds()) } };
                    };
                });

            SkseTestKit::AddCommand("asr.lamptune", "[key] [value]",
                "Sets one of AhShitRim.ini's [Headlights] values (Intensity, Radius, Ahead, Raise, TailRadius, TailIntensity, TailDrop, TailBack, "
                "BrakeRadius, BrakeIntensity, LampGlow, BrakeGlow; metres for the distances) until the game closes, for trying values; "
                "answers every value as it is now",
                [](Call& a_call) -> Json {
                    if (a_call.Has("key")) {
                        const auto key = a_call.Text("key");
                        if (!Headlights::DebugTune(key, static_cast<float>(a_call.Number("value")))) {
                            throw Error(std::format("no [Headlights] key {} (or a negative value)", key));
                        }
                    }
                    JsonObject out;
                    for (const auto& [key, value] : Headlights::DebugTuning()) {
                        out.Set(key, Round(value, 0.001));
                    }
                    return out;
                });

            SkseTestKit::AddCommand("asr.pedals", "[gas=<bool>] [brake=<bool>] [handbrake=<bool>] [steer=<-1..1>] [seconds=<s>]",
                "Holds CJ's vehicle controls (W, S, Space, the stick sideways) for seconds (10, at most 30) or until the next asr.pedals; "
                "with none of them it lets go",
                [](Call& a_call) -> Json {
                    RequireCj();
                    std::uint32_t buttons = 0;
                    buttons |= a_call.Flag("gas", false) ? Protocol::kButtonAccelerate : 0;
                    buttons |= a_call.Flag("brake", false) ? Protocol::kButtonBrake : 0;
                    buttons |= a_call.Flag("handbrake", false) ? Protocol::kButtonHandbrake : 0;
                    const float steer = std::clamp(static_cast<float>(a_call.Number("steer", 0.0)), -1.0f, 1.0f);
                    const auto  hold = std::chrono::milliseconds(static_cast<long long>(std::clamp(a_call.Number("seconds", 10.0), 0.1, 30.0) * 1000.0));
                    // moveY with W and S as Skyrim's keys give it: forward for W, back for S, nothing for both.
                    const float move = ((buttons & Protocol::kButtonAccelerate) ? 1.0f : 0.0f) - ((buttons & Protocol::kButtonBrake) ? 1.0f : 0.0f);
                    if (buttons == 0 && steer == 0.0f) {
                        PlayerSync::DebugHold(std::nullopt);
                    } else {
                        PlayerSync::DebugHold(PlayerSync::DebugInput{ .moveX = steer, .moveY = move, .buttons = buttons }, hold);
                    }
                    return JsonObject{ { "gas", (buttons & Protocol::kButtonAccelerate) != 0 }, { "brake", (buttons & Protocol::kButtonBrake) != 0 },
                        { "handbrake", (buttons & Protocol::kButtonHandbrake) != 0 }, { "steer", steer } };
                });

            SkseTestKit::AddCommand("asr.cheat", "<code>", "Types a cheat code for CJ, as its letters (CPKTNWT blows up every vehicle)", [](Call& a_call) -> Json {
                RequireCj();
                std::string code = a_call.Text("code");
                for (auto& c : code) {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                PlayerSync::DebugType(code.c_str());
                return JsonObject{ { "typed", code } };
            });

            SkseTestKit::AddTask("asr.exit", "", "CJ gets out of the vehicle he's in (the enter/exit key pressed); answers once he's on foot",
                [](Call& a_call) -> Step {
                    RequireCj();
                    a_call.DefaultTimeout(15.0);
                    return [](Call& a_call) -> std::optional<Json> {
                        const auto pose = CjPose();
                        if (pose && (pose->flags & Protocol::kPlayerInVehicle) == 0) {
                            PlayerSync::DebugHold(std::nullopt);
                            return JsonObject{ { "onFoot", true }, { "seconds", Round(a_call.Seconds()) } };
                        }
                        // Held 0.2 s, again every 5 s should he still sit in it: getting out takes about 2 s, and a press
                        // once he's out would put him back in.
                        const bool press = std::fmod(a_call.Seconds(), 5.0) < 0.2;
                        PlayerSync::DebugHold(press ? std::optional(PlayerSync::DebugInput{ .buttons = Protocol::kButtonEnterExit }) : std::nullopt);
                        return std::nullopt;
                    };
                });

            SkseTestKit::AddTask("asr.walk", "<points> [gait=<walk|run|sprint>] [arrive=<units>] [stuck=<seconds>] [untilZone=<bool>]",
                "Walks CJ (San Andreas's stick, from its camera's view) through waypoints x,y; x,y; ... at a gait (run, the default, is San "
                "Andreas's jog), answering once he's within arrive (100) of the last; fails when he gets no closer to the next for stuck (5) s. "
                "untilZone=true stops him and answers as soon as Skyrim names a location he walks into (San Andreas's zone name, zone: its "
                "text), and fails when he reaches the last point without one",
                [](Call& a_call) -> Step {
                    RequireCj();
                    const auto    points = ParseWaypoints(a_call.Text("points"));
                    const auto    gait = a_call.Text("gait", "run");
                    std::uint32_t buttons = 0;
                    if (gait == "walk") {
                        buttons = Protocol::kButtonWalk;
                    } else if (gait == "sprint") {
                        buttons = Protocol::kButtonSprint;
                    } else if (gait != "run") {
                        throw Error("gait is walk, run or sprint");
                    }
                    const float  arrive = static_cast<float>(a_call.Number("arrive", 100.0));
                    const double stuck = a_call.Number("stuck", 5.0);
                    const bool   untilZone = a_call.Flag("untilZone", false);
                    const auto   zonesBefore = SkyrimMessages::DebugCounted().zones;
                    double       length = 0.0;
                    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                        Waypoint from{ player->GetPosition().x, player->GetPosition().y };
                        for (const auto& p : points) {
                            length += std::hypot(p.x - from.x, p.y - from.y);
                            from = p;
                        }
                    }
                    a_call.DefaultTimeout(length / 80.0 + 30.0);  // CJ walks about 100 units/s
                    struct Walk
                    {
                        std::size_t  next = 0;
                        float        best = (std::numeric_limits<float>::max)();
                        double       bestAt = 0.0;
                        double       path = 0.0;
                        RE::NiPoint3 last;
                        bool         haveLast = false;
                    };
                    auto walk = std::make_shared<Walk>();
                    return [=](Call& a_call) -> std::optional<Json> {
                        auto*      player = RE::PlayerCharacter::GetSingleton();
                        const auto pose = CjPose();
                        if (!player || !pose || SkseTestKit::Paused()) {
                            walk->bestAt = a_call.Seconds();  // no time counts towards stuck while nothing can move
                            return std::nullopt;
                        }
                        if (pose->flags & Protocol::kPlayerInVehicle) {
                            PlayerSync::DebugHold(std::nullopt);
                            throw Error("CJ is in a vehicle (asr.exit first)");
                        }
                        const auto at = player->GetPosition();
                        if (walk->haveLast) {
                            walk->path += std::hypot(at.x - walk->last.x, at.y - walk->last.y);
                        }
                        walk->last = at;
                        walk->haveLast = true;
                        const auto& p = points[walk->next];
                        const float d = std::hypot(p.x - at.x, p.y - at.y);
                        if (d <= arrive) {
                            walk->next++;
                            walk->best = (std::numeric_limits<float>::max)();
                            walk->bestAt = a_call.Seconds();
                            if (walk->next == points.size()) {
                                PlayerSync::DebugHold(std::nullopt);
                                if (untilZone) {
                                    throw Error("CJ reached the last point and Skyrim named no location on the way");
                                }
                                return JsonObject{ { "points", points.size() }, { "seconds", Round(a_call.Seconds()) }, { "path", Round(walk->path, 0.1) },
                                    { "at", Point(at) }, { "gait", gait } };
                            }
                            return std::nullopt;
                        }
                        if (untilZone) {
                            if (const auto counts = SkyrimMessages::DebugCounted(); counts.zones != zonesBefore) {
                                PlayerSync::DebugHold(std::nullopt);
                                return JsonObject{ { "zone", counts.lastZone }, { "rule", counts.lastRule }, { "seconds", Round(a_call.Seconds()) },
                                    { "at", Point(at) } };
                            }
                        }
                        if (d < walk->best - 10.0f) {
                            walk->best = d;
                            walk->bestAt = a_call.Seconds();
                        } else if (a_call.Seconds() - walk->bestAt > stuck) {
                            PlayerSync::DebugHold(std::nullopt);
                            throw Error(std::format("CJ got no closer to point {} ({:.0f}, {:.0f}) for {:.0f} s: {:.0f} units from it, at {:.0f}, {:.0f}", walk->next + 1,
                                p.x, p.y, stuck, d, at.x, at.y));
                        }
                        // The stick from San Andreas's camera: forward is where it looks.
                        const float rel = std::atan2(p.x - at.x, p.y - at.y) - pose->viewHeading;
                        PlayerSync::DebugHold(PlayerSync::DebugInput{ .moveX = std::sin(rel), .moveY = std::cos(rel), .buttons = buttons });
                        return std::nullopt;
                    };
                });

            SkseTestKit::AddCommand("asr.muzzle", "[hand] [release=<bool>]",
                "The weapon drawn in a hand (0 right, 1 left) against its muzzle flash: the barrel's end (its farthest vertex along the "
                "barrel), the flash as last shown (shown in the last frame, seconds left, where), along (units the flash is past the "
                "muzzle's end) and across (off the barrel's line), and weapon.roll (degrees its top leans right of up about the barrel; weapon.turned: a model whose first part faces back, read turned round while he aims). release=true lets go of a flash asr.fire hold=true kept, after measuring",
                [](Call& a_call) -> Json {
                    auto out = MuzzleCheck(HandOf(a_call));
                    if (a_call.Flag("release", false)) {
                        MuzzleFlash::DebugHold(false);
                    }
                    return out;
                });

            SkseTestKit::AddTask("asr.fire", "[weapon] [hand=<0|1>] [aim=<bool>] [shots=<n>] [settle=<seconds>] [hold=<bool>]",
                "CJ fires: takes the weapon (a type he carries: asr.give first) if given, aims (the right button, aim=true) for settle (1) s, "
                "then holds fire until shots (1) shots flashed, and answers in the frame of the last flash with asr.muzzle's numbers. "
                "hold=true keeps that flash from running out (for a pause and shots of it) until asr.muzzle release=true or the next asr.fire",
                [](Call& a_call) -> Step {
                    RequireCj();
                    const auto   hand = HandOf(a_call);
                    const bool   aim = a_call.Flag("aim", true);
                    const auto   shots = static_cast<std::uint32_t>((std::max)(1.0, a_call.Number("shots", 1.0)));
                    const double settle = a_call.Number("settle", 1.0);
                    const bool   hold = a_call.Flag("hold", false);
                    MuzzleFlash::DebugHold(false);
                    std::uint8_t weapon = 0;
                    if (a_call.Has("weapon")) {
                        const double number = a_call.Number("weapon");
                        if (number < 1 || number > 46 || number != std::floor(number)) {
                            throw Error("weapon is a San Andreas weapon type, 1 to 46");
                        }
                        weapon = static_cast<std::uint8_t>(number);
                        if (!Arms::DebugHoldWeapon(weapon)) {
                            throw Error(std::format("CJ doesn't carry weapon {} (asr.give {} first)", weapon, weapon));
                        }
                    }
                    a_call.DefaultTimeout(settle + 15.0);
                    struct Fire
                    {
                        bool          held = false;  // the weapon's in his hands
                        double        heldAt = 0.0;
                        std::uint32_t firedBefore = 0;
                    };
                    auto fire = std::make_shared<Fire>();
                    return [=](Call& a_call) -> std::optional<Json> {
                        const auto pose = CjPose();
                        if (!pose) {
                            return std::nullopt;
                        }
                        if (!fire->held) {
                            if (weapon != 0 && pose->weaponType != weapon) {
                                return std::nullopt;  // San Andreas hasn't put it in his hands yet
                            }
                            fire->held = true;
                            fire->heldAt = a_call.Seconds();
                            fire->firedBefore = MuzzleFlash::Debug(hand).fired;
                        }
                        const std::uint32_t aimBit = aim ? Protocol::kButtonAim : 0u;
                        if (a_call.Seconds() - fire->heldAt < settle) {
                            PlayerSync::DebugHold(PlayerSync::DebugInput{ .buttons = aimBit });
                            return std::nullopt;
                        }
                        const auto flash = MuzzleFlash::Debug(hand);
                        if (flash.fired - fire->firedBefore >= shots && flash.shown) {
                            PlayerSync::DebugHold(std::nullopt);
                            MuzzleFlash::DebugHold(hold);
                            auto out = MuzzleCheck(hand);
                            out["weaponType"] = static_cast<int>(pose->weaponType);
                            out["aimed"] = (pose->weaponFlags & Protocol::kWeaponAiming) != 0;
                            out["twin"] = (pose->weaponFlags & Protocol::kWeaponTwin) != 0;
                            out["shots"] = flash.fired - fire->firedBefore;
                            return out;
                        }
                        PlayerSync::DebugHold(PlayerSync::DebugInput{ .buttons = aimBit | Protocol::kButtonFire });
                        return std::nullopt;
                    };
                });

            // A lamp's dummy in the vehicle's whole frame (its slope too, which a target's offset= leaves out), for regions round
            // the lamps: asr.lamp:rearLeft is the tail light on the left of the vehicle asr.spawn made last, @<slot> another's.
            SkseTestKit::AddTarget("asr.lamp", "<frontRight|frontLeft|rearRight|rearLeft>[@<slot>]",
                "A vehicle's lamp where San Andreas's dummy puts it (a pair mirrored across its middle), facing the vehicle's way",
                [](std::string_view a_spec) -> SkseTestKit::Place {
                    const auto  at = a_spec.find('@');
                    const auto  lamp = a_spec.substr(0, at);
                    std::uint32_t slot = 0;
                    if (at != std::string_view::npos) {
                        const auto number = a_spec.substr(at + 1);
                        const auto whole = WholeNumber(number);
                        slot = whole.value_or(Protocol::kVehicleMax);
                        if (slot >= Protocol::kVehicleMax) {
                            throw Error("asr.lamp:<lamp>@<slot>, the slot 0 to " + std::to_string(Protocol::kVehicleMax - 1));
                        }
                    } else if (g_spawned) {
                        slot = *g_spawned;
                    } else {
                        throw Error("asr.spawn hasn't made a vehicle yet: name a slot (asr.lamp:rearLeft@0)");
                    }
                    const bool front = lamp == "frontRight" || lamp == "frontLeft";
                    const bool left = lamp == "frontLeft" || lamp == "rearLeft";
                    if (!front && lamp != "rearRight" && !left) {
                        throw Error("the lamp is frontRight, frontLeft, rearRight or rearLeft");
                    }
                    RE::NiPoint3 position;
                    float        heading = 0.0f;
                    if (!PlayerSync::DebugVehicle(slot, position, heading) || !g_seen[slot].on) {
                        throw Error(std::format("vehicle slot {} isn't drawn", slot));
                    }
                    const auto& seen = g_seen[slot];
                    if (!seen.lamps) {
                        throw Error("this build's vehicles carry no lamps (the headlights, protocol v36, aren't in it)");
                    }
                    auto dummy = front ? seen.front : seen.rear;
                    if (dummy.x == 0.0f && dummy.y == 0.0f && dummy.z == 0.0f) {
                        throw Error(std::format("the vehicle has no {} dummy", front ? "headlight" : "tail light"));
                    }
                    dummy.x = left ? -std::abs(dummy.x) : std::abs(dummy.x);  // the dummy is one of the pair; X runs to the right
                    const auto world = VehicleHits::ToWorld(seen.frame, dummy);
                    return SkseTestKit::Place{ .x = world.x, .y = world.y, .z = world.z, .heading = heading,
                        .details = JsonObject{ { "slot", slot }, { "lamp", std::string(lamp) }, { "local", Point(dummy) },
                            { "lit", (seen.lit & (front ? (left ? 0x2 : 0x1) : (left ? 0x8 : 0x4))) != 0 } } };
                });

            // Cameras and probes at the flash and at the gun's muzzle.
            SkseTestKit::AddTarget("asr.flash", "[hand]", "A hand's muzzle flash where it was last shown, facing along the barrel (its model and light inside)",
                [](std::string_view a_hand) -> SkseTestKit::Place {
                    const std::uint32_t hand = HandOfTarget(a_hand);
                    const auto          flash = MuzzleFlash::Debug(hand);
                    if (flash.fired == 0) {
                        throw Error("CJ hasn't fired yet");
                    }
                    double heading = 0.0;
                    try {
                        heading = BarrelOf(hand).heading;
                    } catch (const Error&) {
                        // no weapon drawn now: facing north
                    }
                    SkseTestKit::Place place{ .x = flash.at.x, .y = flash.at.y, .z = flash.at.z, .heading = heading, .root = ModelView::Node(flash.model),
                        .details = JsonObject{ { "shown", flash.shown }, { "left", Round(flash.left, 0.001) }, { "fired", flash.fired } } };
                    if (flash.light) {
                        place.lights.push_back(flash.light);
                    }
                    return place;
                });
            SkseTestKit::AddTarget("asr.weapon", "[hand]", "The muzzle of the weapon drawn in a hand, facing along its barrel (the gun's model inside)",
                [](std::string_view a_hand) -> SkseTestKit::Place {
                    const auto barrel = BarrelOf(HandOfTarget(a_hand));
                    return SkseTestKit::Place{ .x = barrel.tip.x, .y = barrel.tip.y, .z = barrel.tip.z, .heading = barrel.heading,
                        .root = ModelView::Node(barrel.pose.model), .details = JsonObject{ { "model", std::format("{:08X}", barrel.pose.model) } } };
                });

#ifdef AHSHITRIM_TEST_CREW
            SkseTestKit::AddTask("asr.crew", "[recruit=<n>] [thanes=<n>]",
                "The Grove Street crew: plays as a Thane of thanes (1) holds (-1: the titles the game has), waits for the spots round CJ to fill, has the recruit (2) nearest "
                "homies join and answers once they follow, with each follower's reference (refs, for record start)",
                [](Call& a_call) -> Step {
                    RequireCj();
                    const int recruit = static_cast<int>(a_call.Number("recruit", 2.0));
                    Homies::DebugThanes(static_cast<int>(a_call.Number("thanes", 1.0)));
                    g_homies = {};
                    a_call.DefaultTimeout(40.0);
                    auto joined = std::make_shared<std::optional<int>>();
                    return [recruit, joined](Call& a_call) -> std::optional<Json> {
                        auto* player = RE::PlayerCharacter::GetSingleton();
                        if (!*joined) {
                            if (Homies::DebugCount(false) < recruit && a_call.Seconds() < 25.0) {
                                return std::nullopt;  // the spots fill out of view, a few seconds after the Thane read
                            }
                            *joined = Homies::DebugRecruit(player, recruit);
                        }
                        if (Homies::DebugCount(true) < **joined && a_call.Seconds() < 35.0) {
                            return std::nullopt;
                        }
                        JsonArray   followers;
                        std::string refs;
                        // The followers: the loaded homies on the player's team (Homies' Join makes them teammates).
                        if (auto* processes = RE::ProcessLists::GetSingleton()) {
                            for (auto& handle : processes->highActorHandles) {
                                auto actor = handle.get();
                                if (actor && Homies::IsHomie(actor.get()) && actor->IsPlayerTeammate() && !actor->IsDead()) {
                                    const auto ref = SkseTestKit::FormId(actor->GetFormID());
                                    refs += (refs.empty() ? "" : ",") + ref;
                                    followers.emplace_back(
                                        JsonObject{ { "ref", ref }, { "distance", Round(actor->GetPosition().GetDistance(player->GetPosition()), 1.0) } });
                                }
                            }
                        }
                        return JsonObject{ { "about", Homies::DebugCount(false) }, { "joined", **joined }, { "following", Homies::DebugCount(true) },
                            { "followers", std::move(followers) }, { "refs", refs } };
                    };
                });

            // The followers as targets, so a scenario file can record them (their references are made at run time):
            // asr.homie:0 is the follower with the lowest form id. A target is sampled for its place alone, so its details
            // carry what the recorder reads of a reference's character controller (want, got, obstacle: asked for over 30
            // units/s and moving at under half of it), its AI package and whether it fights.
            SkseTestKit::AddTarget("asr.homie", "<n>",
                "The nth (from 0) of CJ's crew following him, by form id when first asked for (the same homie after that, until asr.crew): "
                "where he is, facing his way",
                [](std::string_view a_index) -> SkseTestKit::Place {
                    const auto n = WholeNumber(a_index);
                    if (!n || *n >= g_homies.size()) {
                        throw Error(std::format("asr.homie:<n>, n from 0 to {}", g_homies.size() - 1));
                    }
                    auto& pinned = g_homies[*n];
                    if (!pinned) {
                        std::vector<RE::Actor*> crew;
                        if (auto* processes = RE::ProcessLists::GetSingleton()) {
                            for (auto& handle : processes->highActorHandles) {
                                auto actor = handle.get();
                                if (actor && Homies::IsHomie(actor.get()) && actor->IsPlayerTeammate() && !actor->IsDead()) {
                                    crew.push_back(actor.get());
                                }
                            }
                        }
                        std::sort(crew.begin(), crew.end(), [](const RE::Actor* a, const RE::Actor* b) { return a->GetFormID() < b->GetFormID(); });
                        if (*n >= crew.size()) {
                            throw Error(std::format("{} follow CJ: no asr.homie:{}", crew.size(), *n));
                        }
                        pinned = crew[*n]->GetFormID();
                    }
                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(pinned);
                    if (!actor || !actor->Is3DLoaded()) {
                        throw Error(std::format("asr.homie:{} ({}) isn't loaded any more", *n, SkseTestKit::FormId(pinned)));
                    }
                    const auto at = actor->GetPosition();
                    JsonObject details{ { "ref", SkseTestKit::FormId(actor->GetFormID()) }, { "following", actor->IsPlayerTeammate() },
                        { "dead", actor->IsDead() }, { "combat", actor->IsInCombat() } };
                    if (auto* package = actor->GetCurrentPackage()) {
                        details.Set("package", static_cast<int>(package->packData.packType.get()));
                    }
                    if (auto* controller = actor->GetCharController()) {
                        RE::NiPoint3 velocity;
                        actor->GetLinearVelocity(velocity);
                        const auto*  out = reinterpret_cast<const float*>(&controller->outVelocity);
                        const double want = std::hypot(out[0], out[1]) * RE::bhkWorld::GetWorldScaleInverse();
                        const double got = std::hypot(velocity.x, velocity.y);
                        details.Set("want", Round(want, 0.1));
                        details.Set("got", Round(got, 0.1));
                        details.Set("obstacle", want > 30.0 && got < 0.5 * want);
                    }
                    return SkseTestKit::Place{ .x = at.x, .y = at.y, .z = at.z, .heading = actor->GetAngleZ(), .root = actor->Get3D(), .details = std::move(details) };
                });
#endif
        }

        // AhShitRim's in-game tests (scenarios/README.md): each a scenario whose rows go to
        // skytest run's CSV; the game logic is in skse/src/debug/ (HitTest, LoadTest, StreamTest) and Missions.
        void AddTests()
        {
            SkseTestKit::AddScenario("asr.hits", "[bust=<bool>] [seconds=<s>]",
                "The hit test: with CJ in a vehicle (asr.spawn 495 in=true), a giant and two guards set on him (or two stars with bust=true, "
                "so a guard runs to the door to bust him) for seconds (90) of game time; a row every 2 s (car and player health, in the car, "
                "wasted, busted), then the attackers go and the stars are cleared",
                [](Call& a_call) -> Step {
                    const double seconds = a_call.Number("seconds", 90.0);
                    if (seconds < 1.0 || seconds > 1800.0) {
                        throw Error("seconds is 1 to 1800");
                    }
                    if (const auto refusal = HitTest::Begin(a_call.Flag("bust", false), static_cast<float>(seconds)); !refusal.empty()) {
                        throw Error(refusal);
                    }
                    a_call.DefaultTimeout(seconds * 2.0 + 60.0);  // game seconds: a pausing menu stops them
                    return [](Call& a_call) -> std::optional<Json> {
                        while (const auto s = HitTest::NextSample()) {
                            a_call.Row(JsonObject{ { "seconds", s->seconds }, { "carHealth", s->carHealth }, { "playerHealth", s->playerHealth },
                                { "inCar", s->inCar }, { "wasted", s->wasted }, { "busted", s->busted } });
                        }
                        if (HitTest::Running()) {
                            return std::nullopt;
                        }
                        const auto& r = HitTest::Last();
                        if (!r.done) {
                            throw Error(r.stopped.empty() ? "the hit test was stopped before its end" : r.stopped);
                        }
                        return JsonObject{ { "seconds", r.seconds }, { "attackers", r.attackers }, { "carStart", r.carStart }, { "carMin", r.carMin },
                            { "carEnd", r.carEnd }, { "carLost", r.carStart - r.carMin },
                            { "playerMin", r.playerMin }, { "inCarAtEnd", r.inCarAtEnd }, { "wasted", r.wasted }, { "busted", r.busted } };
                    };
                });

            SkseTestKit::AddScenario("asr.load", "<save> <scenario> [saveAs=<name>]",
                "One cycle of the load test: loads the save (Saves/<save>.ess) through the console's load, lets it settle, hands the player to "
                "San Andreas, then the scenario (" + LoadTest::Scenarios() + "); a row per step. saveAs saves the settled game under that name "
                "first. scripts/load-test.ps1 runs hundreds",
                [](Call& a_call) -> Step {
                    LoadTest::Stop();  // a cycle whose request went away
                    if (const auto refusal = LoadTest::Begin(a_call.Text("save"), a_call.Text("scenario"), a_call.Text("saveAs")); !refusal.empty()) {
                        throw Error(refusal);
                    }
                    a_call.DefaultTimeout(180.0);
                    return [save = a_call.Text("save"), scenario = a_call.Text("scenario")](Call& a_call) -> std::optional<Json> {
                        while (const auto note = LoadTest::NextNote()) {
                            a_call.Row(JsonObject{ { "time", note->time }, { "seconds", note->seconds }, { "what", note->what } });
                        }
                        if (LoadTest::Done()) {
                            return JsonObject{ { "save", save }, { "scenario", scenario }, { "cj", PlayerSync::DebugOwned() } };
                        }
                        if (!LoadTest::Running()) {
                            throw Error("the cycle was stopped");
                        }
                        // The player died and Skyrim had no save to load: the cycle would wait for nothing.
                        if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME) && a_call.Seconds() > 5.0) {
                            LoadTest::Stop();
                            throw Error("Skyrim is back at its main menu (the player died?)");
                        }
                        return std::nullopt;
                    };
                });

            SkseTestKit::AddScenario("asr.stream", "<speed> [heading=<degrees>] [height=<metres>] [seconds=<s>]",
                "The stream test: carries the player (Skyrim's: asr.cj on=false first) in a straight line at speed m/s, heading degrees from "
                "north (0), height metres up (30), for seconds (60); a row a second (frames, the worst frame, the land loaded under and ahead), "
                "the whole flight as the answer",
                [](Call& a_call) -> Step {
                    const double seconds = a_call.Number("seconds", 60.0);
                    if (const auto refusal = StreamTest::Begin(static_cast<float>(a_call.Number("speed")), static_cast<float>(a_call.Number("heading", 0.0)),
                            static_cast<float>(a_call.Number("height", 30.0)), static_cast<float>(seconds));
                        !refusal.empty()) {
                        throw Error(refusal);
                    }
                    a_call.DefaultTimeout(seconds * 2.0 + 60.0);
                    return [](Call& a_call) -> std::optional<Json> {
                        while (const auto s = StreamTest::NextSecond()) {
                            a_call.Row(DescribeSecond(*s));
                        }
                        if (StreamTest::Running()) {
                            return std::nullopt;
                        }
                        const auto total = StreamTest::Total();
                        if (!total) {
                            throw Error("the stream test was stopped before its end");
                        }
                        return DescribeSecond(*total);
                    };
                });

            SkseTestKit::AddTask("asr.intro", "",
                "CJ's intro line, as if Ralof's line in the cart had just ended; answers once San Andreas was given the cue to play it",
                [](Call& a_call) -> Step {
                    const auto before = Missions::DebugShown().cues;
                    Missions::TestIntro();
                    a_call.DefaultTimeout(10.0);
                    return [before](Call&) -> std::optional<Json> {
                        const auto& shown = Missions::DebugShown();
                        if (shown.cues == before) {
                            return std::nullopt;
                        }
                        return JsonObject{ { "cue", CueName(shown.cue) }, { "cj", PlayerSync::DebugOwned() } };
                    };
                });

            SkseTestKit::AddTask("asr.quest", "<command...>",
                "Runs a console command (setstage MQ103 10, completequest MQ103) and answers once San Andreas's HUD shows what Skyrim announced: "
                "the mission title or mission passed, with the quest's name and the cue it started",
                [](Call& a_call) -> Step {
                    const auto before = Missions::DebugShown();
                    RunConsole(a_call.Text("command"));
                    a_call.DefaultTimeout(20.0);
                    return [before](Call&) -> std::optional<Json> {
                        const auto& shown = Missions::DebugShown();
                        if (shown.steps == before.steps) {
                            return std::nullopt;
                        }
                        return JsonObject{ { "shown", shown.last }, { "name", shown.name },
                            { "cue", shown.cues != before.cues ? Json(CueName(shown.cue)) : Json() }, { "cj", PlayerSync::DebugOwned() } };
                    };
                });
        }

        // AhShitRim's commands on the kit's command pipe (docs/DEV_SETUP.md, "Test sessions").
        void AddCommands()
        {
            SkseTestKit::AddTask("asr.cj", "[on]",
                "Hands the player to San Andreas (on=true, the default) or back to Skyrim, as F10 does; answers once it's done",
                [](Call& a_call) -> Step {
                    const bool want = a_call.Flag("on", true);
                    g_lampsOff = false;  // a run that stopped between asr.lamps off and on leaves nothing behind a new handover
                    g_braking.reset();
                    PlayerSync::DebugHold(std::nullopt);  // nor held pedals
                    if (want && !PlayerSync::DebugOwned() && !LinkService::Get().IsUp()) {
                        throw Error("San Andreas isn't linked (not running, or still starting): nothing to hand the player to");
                    }
                    auto wait = std::make_shared<int>(0);
                    // Done when the player is where asked and, for CJ, San Andreas has answered the handover with a pose.
                    auto done = [want]() {
                        if (PlayerSync::DebugOwned() != want) {
                            return false;
                        }
                        Protocol::PlayerState pose{};
                        return !want || (LinkService::Get().ReadPlayer(pose) && pose.handover == PlayerSync::DebugHandover());
                    };
                    return [want, wait, done](Call&) -> std::optional<Json> {
                        if (done()) {
                            return JsonObject{ { "cj", want } };
                        }
                        if (PlayerSync::DebugOwned() == want) {
                            return std::nullopt;  // handed over, San Andreas's first pose still to come
                        }
                        if ((*wait)-- > 0) {
                            return std::nullopt;
                        }
                        *wait = 120;  // a handover takes a moment: again in 2 s
                        if (!want || LinkService::Get().IsUp()) {
                            PlayerSync::DebugToggle();
                        }
                        return std::nullopt;
                    };
                });

            SkseTestKit::AddTask("asr.spawn", "<model> [in=<bool>]",
                "Spawns a San Andreas vehicle by model id (400 to 611) where San Andreas puts a cheat's vehicle, in front of CJ; in=true puts CJ "
                "in a new one where he stands (handing him the player if he hasn't it). Answers with its slot and where it is from the player",
                [](Call& a_call) -> Step {
                    const double number = a_call.Number("model");
                    if (number < 400 || number > 611 || number != std::floor(number)) {
                        throw Error("model is a San Andreas vehicle model id, 400 to 611");
                    }
                    const auto model = static_cast<std::uint16_t>(number);
                    if (!VehicleKinds::Spawnable(model) || VehicleKinds::IsGear(model)) {
                        throw Error(std::format("{} can't spawn: {}", VehicleKinds::Label(model), VehicleKinds::TraitsOf(VehicleKinds::KindOf(model)).refusal));
                    }
                    const bool in = a_call.Flag("in", false);
                    if (!in) {
                        RequireCj();
                    } else if (auto* player = RE::PlayerCharacter::GetSingleton(); !player || !player->GetParentCell() || !LinkService::Get().IsUp()) {
                        throw Error("in=true needs the player in the world and San Andreas linked");
                    }
                    const auto before = ReadSlots();
                    if (in) {
                        PlayerSync::TakeOverInVehicle(model);  // a new handover: San Andreas starts over with CJ in one
                    } else if (const auto refusal = PlayerSync::DebugSpawn(model); !refusal.empty()) {
                        throw Error(refusal);
                    }
                    const auto handover = PlayerSync::DebugHandover();
                    a_call.DefaultTimeout(20.0);
                    return [model, before, in, handover](Call& a_call) -> std::optional<Json> {
                        if (PlayerSync::DebugHandover() != handover) {
                            throw Error("the handover changed while waiting (a load door, fast travel, F10): its vehicles are gone");
                        }
                        // Only San Andreas's answers to this handover: older poses and slots are another anchor's.
                        Protocol::PlayerState pose{};
                        if (!LinkService::Get().ReadPlayer(pose) || pose.handover != handover) {
                            return std::nullopt;
                        }
                        if (in && (!(pose.flags & Protocol::kPlayerInVehicle) || pose.vehicleModel != model)) {
                            return std::nullopt;
                        }
                        // This handover's vehicles, each slot read once.
                        std::array<std::optional<Protocol::BodyState>, Protocol::kVehicleMax> slots;
                        for (std::uint32_t slot = 0; slot < Protocol::kVehicleMax; slot++) {
                            Protocol::BodyState vehicle{};
                            if (LinkService::Get().ReadVehicle(slot, vehicle) && (vehicle.flags & Protocol::kBodyValid) && vehicle.handover == handover) {
                                slots[slot] = vehicle;
                            }
                        }
                        auto first = [&](auto a_rule) -> std::optional<std::uint32_t> {
                            for (std::uint32_t slot = 0; slot < Protocol::kVehicleMax; slot++) {
                                if (slots[slot] && a_rule(slot, *slots[slot])) {
                                    return slot;
                                }
                            }
                            return std::nullopt;
                        };
                        // in=true: the one CJ drives. Else one in a slot that was empty before, or failing that one whose
                        // model changed (San Andreas reuses the oldest slot when all are taken).
                        auto found = in ? first([](std::uint32_t, const Protocol::BodyState& a_v) { return (a_v.flags & Protocol::kBodyDriven) != 0; })
                                        : first([&](std::uint32_t a_slot, const Protocol::BodyState&) { return before[a_slot] == 0; });
                        if (!found && !in) {
                            found = first([&](std::uint32_t a_slot, const Protocol::BodyState& a_v) { return a_v.model != before[a_slot]; });
                        }
                        if (!found) {
                            // San Andreas answers in a few frames; when it finds no flat, clear spot in front of CJ (another
                            // vehicle in the way) it spawns nothing and only its own log says so.
                            if (!in && a_call.Seconds() > 5.0) {
                                throw Error("San Andreas spawned nothing in 5 s: no clear spot in front of CJ? (its AhShitRim.log says why)");
                            }
                            return std::nullopt;
                        }
                        const auto& vehicle = *slots[*found];
                        g_spawned = *found;
                        // Both relative to the handover's anchor: the difference is the vehicle from CJ, whom the player follows.
                        const float offset[3] = { vehicle.feet[0] - pose.offset[0], vehicle.feet[1] - pose.offset[1], vehicle.feet[2] - pose.offset[2] };
                        return JsonObject{ { "model", model }, { "label", VehicleKinds::Label(model) }, { "slot", *found },
                            { "driven", (vehicle.flags & Protocol::kBodyDriven) != 0 }, { "offset", Point(offset) },
                            { "distance", std::sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]) }, { "health", vehicle.health } };
                    };
                });

            SkseTestKit::AddTask("asr.give", "<weapon> [ammo=<n>]",
                "Gives CJ a weapon by San Andreas's weapon type (22 pistol, 25 shotgun, 31 M4...) with ammo rounds (default 500); answers with "
                "his loadout once San Andreas has the gift",
                [](Call& a_call) -> Step {
                    RequireCj();
                    const double number = a_call.Number("weapon");
                    if (number < 1 || number > 46 || number != std::floor(number) || !Weapons::Carried(static_cast<std::uint8_t>(number))) {
                        throw Error("weapon is a San Andreas weapon type CJ can carry");
                    }
                    const auto type = static_cast<std::uint8_t>(number);
                    const auto rounds = static_cast<std::uint32_t>((std::max)(0.0, a_call.Number("ammo", 500.0)));
                    // What he has of it now: the answer waits for the gift on top (or the weapon itself, when he hasn't it).
                    std::optional<std::uint32_t> had;
                    for (const auto& entry : Arms::Loadout()) {
                        if (entry.type == type) {
                            had = entry.ammo;
                        }
                    }
                    Arms::Give(type, rounds);
                    a_call.DefaultTimeout(10.0);
                    return [type, had, rounds](Call&) -> std::optional<Json> {
                        JsonArray                    loadout;
                        std::optional<std::uint32_t> has;
                        for (const auto& entry : Arms::Loadout()) {
                            if (entry.type == 0) {
                                continue;
                            }
                            if (entry.type == type) {
                                has = entry.ammo;
                            }
                            loadout.emplace_back(JsonObject{ { "type", entry.type }, { "clip", entry.clip }, { "ammo", entry.ammo } });
                        }
                        const bool arrived = has && (!had || rounds == 0 || *has > *had || *has >= 99999);
                        if (!arrived) {
                            return std::nullopt;
                        }
                        return JsonObject{ { "weapon", type }, { "loadout", std::move(loadout) } };
                    };
                });

            SkseTestKit::AddCommand("asr.ground", "<x> <y> [radius] [step]",
                "Skyrim's ground height (a ray down from 400 above the player to 800 below) on a square of sample points round x, y: rows of "
                "heights, and the lowest, the highest and the steepest slope between neighbours (degrees)",
                [](Call& a_call) -> Json {
                    auto*      player = RE::PlayerCharacter::GetSingleton();
                    const auto from = player ? player->GetPosition() : RE::NiPoint3{};
                    const float x = static_cast<float>(a_call.Number("x", from.x)), y = static_cast<float>(a_call.Number("y", from.y));
                    const float radius = static_cast<float>(a_call.Number("radius", 200.0));
                    const float step = (std::max)(10.0f, static_cast<float>(a_call.Number("step", 50.0)));
                    const int   n = static_cast<int>(radius / step);
                    JsonArray   rows;
                    float       low = 1e9f, high = -1e9f, steepest = 0.0f;
                    std::vector<std::vector<float>> grid;
                    for (int j = -n; j <= n; j++) {
                        JsonArray   row;
                        std::vector<float> line;
                        for (int i = -n; i <= n; i++) {
                            const auto ground = GroundUnder({ x + i * step, y + j * step, from.z }, 400.0f, 800.0f);
                            line.push_back(ground ? *ground : std::nanf(""));
                            row.emplace_back(ground ? Json(Round(*ground, 0.1)) : Json());
                            if (ground) {
                                low = (std::min)(low, *ground);
                                high = (std::max)(high, *ground);
                            }
                        }
                        rows.emplace_back(std::move(row));
                        grid.push_back(std::move(line));
                    }
                    for (std::size_t j = 0; j < grid.size(); j++) {
                        for (std::size_t i = 0; i < grid[j].size(); i++) {
                            for (const auto& [dj, di] : { std::pair{ 0, 1 }, std::pair{ 1, 0 } }) {
                                if (j + dj < grid.size() && i + di < grid[j].size() && !std::isnan(grid[j][i]) && !std::isnan(grid[j + dj][i + di])) {
                                    const float rise = std::fabs(grid[j + dj][i + di] - grid[j][i]);
                                    steepest = (std::max)(steepest, static_cast<float>(std::atan2(rise, step) * 57.29578));
                                }
                            }
                        }
                    }
                    return JsonObject{ { "step", step }, { "low", low }, { "high", high }, { "steepest", Round(steepest, 0.1) }, { "rows", std::move(rows) } };
                });

            SkseTestKit::AddCommand("asr.night", "[hour]", "Sets the game's hour (default 23, night for the lights and the HUD clock)", [](Call& a_call) -> Json {
                const double hour = a_call.Number("hour", 23.0);
                if (hour < 0.0 || hour >= 24.0) {
                    throw Error("hour is 0 to 24");
                }
                auto* calendar = RE::Calendar::GetSingleton();
                if (!calendar || !calendar->gameHour) {
                    throw Error("no calendar yet");
                }
                calendar->gameHour->value = static_cast<float>(hour);
                return JsonObject{ { "hour", calendar->GetHour() } };
            });

            SkseTestKit::AddCommand("asr.camera", "",
                "San Andreas's camera now: its mode (eCamMode), where it is from CJ's feet (distance on the ground, height) and, in a "
                "vehicle, in the vehicle's frame from its middle (forward, right, up: forward > 0 is in front of it); its scope; and ahead: how far "
                "the rendered camera sees that way before land, a tree or a building (3000 for nothing within 3000 units)",
                [](Call&) -> Json { return CameraNow(); });

            SkseTestKit::AddTask("asr.view", "[settle=<seconds>] [untilFirstPerson=<bool>] [presses=<n>]",
                "Presses San Andreas's camera key (V, select on its pad) once, as the player would, and answers settle seconds (1.5) later "
                "with asr.camera's numbers, before (fromFeet's before the press) and changed (the camera moved more than 20 units). "
                "untilFirstPerson=true presses again every settle seconds, up to presses (6) times, until the first-person view is up "
                "(presses: how many it took), and fails when it never comes",
                [](Call& a_call) -> Step {
                    RequireCj();
                    const double settle = a_call.Number("settle", 1.5);
                    const bool   untilFirst = a_call.Flag("untilFirstPerson", false);
                    const int    most = static_cast<int>(a_call.Number("presses", 6.0));
                    a_call.DefaultTimeout(settle * (untilFirst ? most : 1) + 10.0);
                    struct View
                    {
                        Json   before;
                        int    pressed = 0;
                        double pressedAt = -1.0;
                    };
                    auto view = std::make_shared<View>();
                    view->before = CameraNow();
                    return [=](Call& a_call) -> std::optional<Json> {
                        const double now = a_call.Seconds();
                        if (view->pressedAt < 0.0 || (now - view->pressedAt >= settle)) {
                            if (view->pressedAt >= 0.0) {
                                auto after = CameraNow();
                                const auto* first = after.AsObject().Find("firstPerson");
                                const bool  done = !untilFirst || (first && first->AsBool());
                                if (done) {
                                    PlayerSync::DebugHold(std::nullopt);
                                    const auto num = [](const Json& a_json, std::string_view a_key) {
                                        const auto* v = a_json.AsObject().Find(a_key);
                                        return v && v->IsNumber() ? v->AsNumber() : 0.0;
                                    };
                                    const double moved = std::hypot(num(after, "fromFeet") - num(view->before, "fromFeet"), num(after, "height") - num(view->before, "height"),
                                        num(after, "forward") - num(view->before, "forward"));
                                    auto& out = after.AsObject();
                                    out.Set("before", num(view->before, "fromFeet"));
                                    out.Set("changed", moved > 20.0);
                                    out.Set("presses", view->pressed);
                                    return after;
                                }
                                if (view->pressed >= most) {
                                    PlayerSync::DebugHold(std::nullopt);
                                    throw Error(std::format("no first-person view after {} presses", view->pressed));
                                }
                            }
                            view->pressed++;
                            view->pressedAt = now;
                        }
                        const bool press = now - view->pressedAt < 0.15;
                        PlayerSync::DebugHold(press ? std::optional(PlayerSync::DebugInput{ .buttons = Protocol::kButtonCameraView }) : std::nullopt);
                        return std::nullopt;
                    };
                });

            // San Andreas's zone and vehicle names and help box, and what became of Skyrim's HUD messages
            // (hud/SkyrimMessages: counts since the game started, the last of each).
            SkseTestKit::AddCommand("asr.hud", "",
                "San Andreas's zone name, vehicle name and help box as drawn last frame (text, alpha 0 to 1, phase), and Skyrim's HUD messages "
                "restyled or hidden so far (zones, helps, hidden, the last of each and the rule it went by), and the player's Skyrim location",
                [](Call&) -> Json {
                    const auto text = SaHud::DebugTexts();
                    const auto counts = SkyrimMessages::DebugCounted();
                    auto*      player = RE::PlayerCharacter::GetSingleton();
                    auto*      location = player ? player->GetCurrentLocation() : nullptr;
                    std::string where = location && location->GetName() ? location->GetName() : "";
                    return JsonObject{ { "zone", text.zone }, { "zoneAlpha", Round(text.zoneAlpha) }, { "zonePhase", text.zonePhase },
                        { "vehicle", text.vehicle }, { "vehicleAlpha", Round(text.vehicleAlpha) }, { "vehiclePhase", text.vehiclePhase },
                        { "help", text.help }, { "helpAlpha", Round(text.helpAlpha) }, { "zones", counts.zones }, { "helps", counts.helps },
                        { "hidden", counts.hidden }, { "lastZone", counts.lastZone }, { "lastHelp", counts.lastHelp },
                        { "lastHidden", counts.lastHidden }, { "lastRule", counts.lastRule }, { "location", where }, { "cj", PlayerSync::DebugOwned() } };
                });

            SkseTestKit::AddTask("asr.notify", "<text...>",
                "Shows a Skyrim notification (Debug.Notification's way: SendHUDMessage::ShowHUDMessage, through Skyrim's HUD) and answers with what became of it: "
                "hidden, help (San Andreas's help box) or kept (Skyrim's, when nothing took it within 3 s), and the rule it went by",
                [](Call& a_call) -> Step {
                    const auto text = a_call.Text("text");
                    if (text.empty()) {
                        throw Error("no text");
                    }
                    const auto before = SkyrimMessages::DebugCounted();
                    RE::SendHUDMessage::ShowHUDMessage(text.c_str());
                    a_call.DefaultTimeout(10.0);
                    return [before](Call& a_call) -> std::optional<Json> {
                        const auto now = SkyrimMessages::DebugCounted();
                        if (now.hidden != before.hidden) {
                            return JsonObject{ { "action", "hidden" }, { "text", now.lastHidden }, { "rule", now.lastRule } };
                        }
                        if (now.helps != before.helps) {
                            return JsonObject{ { "action", "help" }, { "text", now.lastHelp }, { "rule", now.lastRule } };
                        }
                        if (a_call.Seconds() > 3.0) {
                            return JsonObject{ { "action", "kept" } };
                        }
                        return std::nullopt;
                    };
                });

            // A camera target (asr.vehicle:<slot>, asr.vehicle:driven, asr.vehicle: for the one asr.spawn made last): San Andreas's
            // vehicles are drawn by ModelView, not references, and their node isn't turned (the parts are), so the kit
            // can't find them or their heading itself. Its root is the node ModelView draws it under, so the kit's node
            // and probe commands see its parts and shapes.
            SkseTestKit::AddTarget("asr.vehicle", "[slot|driven|last]",
                "A San Andreas vehicle where it's drawn, facing its way: a slot (asr.spawn answers it), the one CJ drives, or (the default) "
                "the one asr.spawn made last",
                [](std::string_view a_which) -> SkseTestKit::Place {
                    std::optional<std::uint32_t> slot;
                    if (a_which.empty() || a_which == "last") {
                        if (!g_spawned) {
                            throw Error("asr.spawn hasn't made a vehicle yet: name a slot");
                        }
                        slot = g_spawned;
                    } else if (a_which == "driven") {
                        slot = DrivenSlot();
                        if (!slot) {
                            throw Error("CJ isn't driving");
                        }
                    } else {
                        std::uint32_t number = 0;
                        const auto [end, error] = std::from_chars(a_which.data(), a_which.data() + a_which.size(), number);
                        if (error != std::errc{} || end != a_which.data() + a_which.size() || number >= Protocol::kVehicleMax) {
                            throw Error(std::format("asr.vehicle takes a slot from 0 to {}, driven or last", Protocol::kVehicleMax - 1));
                        }
                        slot = number;
                    }
                    RE::NiPoint3  position;
                    float         heading = 0.0f;
                    std::uint32_t model = 0;
                    if (!PlayerSync::DebugVehicle(*slot, position, heading, &model)) {
                        throw Error(std::format("vehicle slot {} isn't drawn (asr.cj and asr.spawn first)", *slot));
                    }
                    JsonObject                details{ { "slot", *slot }, { "model", std::format("{:08X}", model) } };
                    std::vector<RE::NiLight*> lights;
                    DescribeLamps(*slot, details, lights);
                    return SkseTestKit::Place{ .x = position.x, .y = position.y, .z = position.z, .heading = heading, .root = ModelView::Node(model),
                        .lights = std::move(lights), .details = std::move(details) };
                });

            // A scenario: CJ drives his vehicle with keys held, a row a frame, and how it rode (protocol/cpp/ride.h).
            SkseTestKit::AddScenario("asr.drive", "[steer=<-1..1>] [gas=<forward|back|none>] [handbrake=<bool>] [seconds=<s>] [minSpeed=<m/s>]",
                "Holds the driving keys for seconds (3): steer (-1 full left, A; 1 full right, D; 0), gas (forward W, back S, none) and the "
                "handbrake (Space). A row a frame of the driven vehicle where it's drawn (its position, heading, pitch, roll, tilt, speed and "
                "slip off its heading); answers with the leg: turned (degrees, right positive), path, moved, radius (units), top speed, mean, median "
                "and max slip (degrees, counted while faster than minSpeed, 2 m/s), impacts (stopped short by something), max tilt, roll and pitch, the height range and the fastest rise",
                [](Call& a_call) -> Step {
                    RequireCj();
                    const float steer = static_cast<float>(a_call.Number("steer", 0.0));
                    if (!(steer >= -1.0f && steer <= 1.0f)) {
                        throw Error("steer is -1 to 1");
                    }
                    const auto    gas = a_call.Text("gas", "forward");
                    float         moveY = 0.0f;
                    std::uint32_t buttons = 0;
                    if (gas == "forward") {
                        moveY = 1.0f;
                        buttons = Protocol::kButtonAccelerate;
                    } else if (gas == "back") {
                        moveY = -1.0f;
                        buttons = Protocol::kButtonBrake;
                    } else if (gas != "none") {
                        throw Error("gas is forward, back or none");
                    }
                    if (a_call.Flag("handbrake", false)) {
                        buttons |= Protocol::kButtonHandbrake;
                    }
                    const double seconds = a_call.Number("seconds", 3.0);
                    if (!(seconds > 0.0 && seconds <= 120.0)) {
                        throw Error("seconds is over 0, up to 120");
                    }
                    const auto slot = DrivenSlot();
                    if (!slot) {
                        throw Error("CJ isn't driving (asr.spawn <model> in=true first)");
                    }
                    a_call.DefaultTimeout(seconds * 3.0 + 15.0);
                    const PlayerSync::DebugInput input{ .moveX = steer, .moveY = moveY, .buttons = buttons };
                    const double minSpeed = a_call.Number("minSpeed", 2.0);
                    if (!(minSpeed > 0.0)) {
                        throw Error("minSpeed is over 0 (m/s)");
                    }
                    auto leg = std::make_shared<Ride::Leg>(static_cast<float>(minSpeed) * Pose::kUnitsPerMeter);
                    auto held = std::make_shared<double>(0.0);  // seconds held while the world ran
                    auto last = std::make_shared<double>(a_call.Seconds());
                    return [=](Call& a_call) -> std::optional<Json> {
                        const double now = a_call.Seconds();
                        const double step = now - *last;
                        *last = now;
                        if (SkseTestKit::Paused()) {
                            PlayerSync::DebugHold(std::nullopt);
                            return std::nullopt;
                        }
                        if (a_call.Frames() > 0) {
                            *held += step;  // the time since the last sample: the first one is this one's own frame
                        }
                        RE::NiPoint3 at;
                        float        heading = 0.0f;
                        float        rotation[4]{};
                        if (!PlayerSync::DebugVehicle(*slot, at, heading, nullptr, &rotation)) {
                            PlayerSync::DebugHold(std::nullopt);
                            throw Error(std::format("vehicle slot {} isn't drawn any more", *slot));
                        }
                        Protocol::BodyBone frame{};
                        std::copy(std::begin(rotation), std::end(rotation), std::begin(frame.rotation));
                        const auto attitude = Ride::AttitudeOf(Body::ToMatrix(frame));
                        leg->Add(*held, at.x, at.y, at.z, heading, attitude);
                        a_call.Row(JsonObject{ { "t", Round(*held, 0.001) }, { "frame", a_call.Frames() }, { "x", Round(at.x, 0.1) }, { "y", Round(at.y, 0.1) },
                            { "z", Round(at.z, 0.1) }, { "heading", Round(heading * Ride::kDegrees, 0.1) }, { "pitch", Round(attitude.pitch, 0.1) },
                            { "roll", Round(attitude.roll, 0.1) }, { "tilt", Round(attitude.tilt, 0.1) }, { "speed", Round(leg->LastSpeed() / Pose::kUnitsPerMeter, 0.01) },
                            { "slip", Round(leg->LastSlip(), 0.1) } });
                        if (*held >= seconds) {
                            PlayerSync::DebugHold(std::nullopt);
                            const float m = Pose::kUnitsPerMeter;
                            return JsonObject{ { "slot", *slot }, { "samples", leg->Samples() }, { "turned", Round(leg->Turned(), 0.1) },
                                { "path", Round(leg->Path(), 0.1) }, { "moved", Round(leg->Moved(), 0.1) }, { "radius", Round(leg->Radius(), 0.1) },
                                { "radiusM", Round(leg->Radius() / m, 0.01) }, { "topSpeed", Round(leg->TopSpeed() / m, 0.01) },
                                { "slip", Round(leg->MeanSlip(), 0.1) }, { "medianSlip", Round(leg->MedianSlip(), 0.1) }, { "maxSlip", Round(leg->MaxSlip(), 0.1) }, { "impacts", leg->Impacts() }, { "slipSamples", leg->SlipSamples() },
                                { "maxTilt", Round(leg->MaxTilt(), 0.1) }, { "maxRoll", Round(leg->MaxRoll(), 0.1) }, { "maxPitch", Round(leg->MaxPitch(), 0.1) },
                                { "heightRange", Round(leg->HeightRange() / m, 0.01) }, { "maxRise", Round(leg->MaxRise() / m, 0.01) },
                                { "at", Point(at) }, { "heading", Round(heading * Ride::kDegrees, 0.1) } };
                        }
                        PlayerSync::DebugHold(input);
                        return std::nullopt;
                    };
                });

            SkseTestKit::AddCommand("asr.vehicles", "[within=<units>]",
                "San Andreas's vehicles drawn now (every slot, a parked one too): count, and each one's slot, model (ModelView's id), where "
                "and how far from the player, its health (1000 new), wrecked, burning, smoking (health under 650), up (1 upright: its frame's Z) "
                "and above (how far its box's bottom is over Skyrim's ground there, units; missing with no ground); within (default everywhere) "
                "counts only those that close",
                [](Call& a_call) -> Json {
                    RequireCj();
                    const double within = a_call.Number("within", (std::numeric_limits<double>::max)());
                    auto*        player = RE::PlayerCharacter::GetSingleton();
                    const auto   from = player ? player->GetPosition() : RE::NiPoint3{};
                    JsonArray    vehicles;
                    for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
                        RE::NiPoint3  at;
                        float         heading = 0.0f;
                        std::uint32_t model = 0;
                        if (!PlayerSync::DebugVehicle(i, at, heading, &model)) {
                            continue;
                        }
                        const double distance = std::hypot(at.x - from.x, at.y - from.y);
                        if (distance <= within) {
                            const auto& seen = g_seen[i];
                            const bool  wrecked = seen.on && (seen.flags & Protocol::kBodyWrecked) != 0;
                            JsonObject  one{ { "slot", i }, { "model", std::format("{:08X}", model) }, { "at", Point(at) }, { "distance", Round(distance, 1.0) },
                                { "health", seen.health }, { "wrecked", wrecked }, { "burning", seen.on && (seen.flags & Protocol::kBodyBurning) != 0 },
                                { "smoking", seen.on && !wrecked && seen.health < 650.0f }, { "up", seen.frame.up.z } };
                            if (const auto ground = GroundUnder(at)) {
                                one.Set("above", at.z + seen.boxBottom - *ground);
                            }
                            vehicles.emplace_back(std::move(one));
                        }
                    }
                    const auto count = vehicles.size();
                    return JsonObject{ { "count", count }, { "vehicles", std::move(vehicles) } };
                });

            // A scenario: a row a frame, for skytest run's CSV.
            SkseTestKit::AddScenario("asr.track", "[frames]",
                "Records CJ for frames frames (default 60), a row a frame: where San Andreas has him from the handover's anchor, and his vehicle",
                [](Call& a_call) -> Step {
                    RequireCj();
                    const double frames = a_call.Number("frames", 60.0);
                    if (frames < 1.0 || frames > 36000.0) {
                        throw Error("frames is 1 to 36000");
                    }
                    a_call.DefaultTimeout(frames / 30.0 + 30.0);
                    return [count = static_cast<std::uint64_t>(frames)](Call& a_call) -> std::optional<Json> {
                        Protocol::PlayerState pose{};
                        if (LinkService::Get().ReadPlayer(pose) && pose.handover == PlayerSync::DebugHandover()) {
                            const bool inVehicle = (pose.flags & Protocol::kPlayerInVehicle) != 0;
                            a_call.Row(JsonObject{ { "frame", a_call.Frames() }, { "saFrame", pose.frame }, { "cj", Point(pose.offset) },
                                { "inVehicle", inVehicle }, { "vehicleModel", inVehicle ? Json(pose.vehicleModel) : Json() } });
                        }
                        if (a_call.Frames() + 1 < count) {
                            return std::nullopt;
                        }
                        return JsonObject{ { "frames", count } };
                    };
                });
        }

        // ---- The knife stealth kill (v36): scenarios/stealth-kill.json ----

        struct Kill
        {
            RE::FormID   victim = 0;
            RE::NiPoint3 victimAt;  // where it stood at the grab
            RE::NiPoint3 cjAt;      // CJ's feet then
        } g_kill;
        float      g_lastPerLanded = 0.0f;  // asr.swing's last health drop per landed (Damage::Blows), for vsLast
        RE::FormID g_victim = 0;            // the last asr.victim placed or asr.seated found: asr.victim:, and ref's default
        RE::FormID g_placed = 0;            // the last guard asr.victim placed, taken out by the next

        RE::NiPoint3 CjFeet()
        {
            RE::NiPoint3 feet;
            float        heading = 0.0f;
            if (!PlayerSync::DebugCj(feet, heading)) {
                throw Error("San Andreas doesn't have the player: asr.cj first");
            }
            return feet;
        }

        float Flat(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b) { return std::hypot(a_a.x - a_b.x, a_a.y - a_b.y); }

        Json Spot(const RE::NiPoint3& a_p) { return JsonObject{ { "x", Round(a_p.x, 0.1) }, { "y", Round(a_p.y, 0.1) }, { "z", Round(a_p.z, 0.1) } }; }

        RE::Actor* ActorArg(Call& a_call)
        {
            if (!a_call.Has("ref") || a_call.Text("ref") == "victim") {
                auto* victim = g_victim ? RE::TESForm::LookupByID<RE::Actor>(g_victim) : nullptr;
                if (!victim) {
                    throw Error("no victim: asr.victim or asr.seated first, or ref=");
                }
                return victim;
            }
            auto* ref = SkseTestKit::FindRef(a_call.Text("ref"));
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            if (!actor) {
                throw Error("ref isn't an actor");
            }
            return actor;
        }

        // Whether there's room to stand behind a_actor, from half a metre (its seat's back) to a_meters + 0.35, at
        // waist height: nothing in the way on Skyrim's line-of-sight layer (walls, tables, doors).
        bool RoomBehind(RE::Actor* a_actor, float a_meters)
        {
            auto* cell = a_actor->GetParentCell();
            auto* bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto* world = bhk ? bhk->GetWorld1() : nullptr;
            if (!world) {
                return false;
            }
            const float        heading = a_actor->GetAngleZ();
            const RE::NiPoint3 back(-std::sin(heading), -std::cos(heading), 0.0f);
            const RE::NiPoint3 waist = a_actor->GetPosition() + RE::NiPoint3(0.0f, 0.0f, 60.0f);
            const auto         from = waist + back * (0.5f * Pose::kUnitsPerMeter);
            const auto         to = waist + back * ((a_meters + 0.35f) * Pose::kUnitsPerMeter);
            const float        scale = RE::bhkWorld::GetWorldScale();
            RE::hkpWorldRayCastInput input;
            input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
            input.from = RE::hkVector4(from.x * scale, from.y * scale, from.z * scale, 0.0f);
            input.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0f);
            RE::hkpWorldRayCastOutput output;
            RE::BSReadLockGuard       lock(bhk->worldLock);
            world->CastRay(input, output);
            return !output.HasHit();
        }

        // Whether the furniture a_actor uses is a seat (a chair, a bench), not something to lean on.
        bool OnASeat(RE::Actor* a_actor)
        {
            const auto furniture = a_actor->GetOccupiedFurniture().get();
            auto*      base = furniture ? furniture->GetBaseObject() : nullptr;
            auto*      seat = base ? base->As<RE::TESFurniture>() : nullptr;
            return seat && seat->furnFlags.any(RE::TESFurniture::ActiveMarker::kCanSit) && seat->furnFlags.none(RE::TESFurniture::ActiveMarker::kCanLean);
        }

        float HealthOf(RE::Actor* a_actor) { return a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth); }

        // CJ crouched (San Andreas toggles it on the press) or standing, as Skyrim's player sneaks: presses crouch every
        // 1.5 s until Skyrim's player is as asked. True once it is.
        bool Crouch(Call& a_call, bool a_crouch)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && player->IsSneaking() == a_crouch) {
                return true;
            }
            const bool press = std::fmod(a_call.Seconds(), 1.5) < 0.15;
            PlayerSync::DebugHold(press ? std::optional(PlayerSync::DebugInput{ .buttons = Protocol::kButtonCrouch }) : std::nullopt);
            return false;
        }

        // The knife in CJ's hands (asr.give 4 first): asks San Andreas for it; true once his pose holds it.
        bool KnifeInHands()
        {
            const auto pose = CjPose();
            if (pose && pose->weaponType == Weapons::Knife) {
                return true;
            }
            if (!Arms::DebugHoldWeapon(Weapons::Knife)) {
                throw Error("CJ doesn't carry the knife: asr.give 4 first");
            }
            return false;
        }

        // The victim and CJ against where they stood at the grab, flat, in units.
        JsonObject KillState(RE::Actor* a_victim)
        {
            auto*      player = RE::PlayerCharacter::GetSingleton();
            const auto cj = CjFeet();
            const auto debug = StealthKills::DebugState();
            JsonObject out{ { "victim", SkseTestKit::FormId(g_kill.victim) }, { "sneaking", player->IsSneaking() }, { "cj", Spot(cj) },
                { "cjMoved", Round(Flat(cj, g_kill.cjAt), 0.1) }, { "playerFromCj", Round(Flat(player->GetPosition(), cj), 0.1) },
                { "grabs", debug.grabs }, { "stabs", debug.stabs }, { "held", debug.held != 0 }, { "killMove", debug.killMove }, { "idle", std::string(debug.idle) },
                { "moveSeconds", Round(debug.moveSeconds) } };
            if (a_victim) {
                const auto at = a_victim->GetPosition();
                out.Set("at", Spot(at));
                out.Set("victimMoved", Round(Flat(at, g_kill.victimAt), 0.1));
                out.Set("cjToVictim", Round(Flat(cj, at) * Pose::kMetersPerUnit, 0.01));
                out.Set("victimDead", a_victim->IsDead());
                out.Set("health", Round(HealthOf(a_victim), 0.1));
                out.Set("inKillMove", a_victim->IsInKillMove());
                out.Set("ragdoll", a_victim->IsInRagdollState());
            }
            return out;
        }

        void AddStealthKills()
        {
            SkseTestKit::AddTarget("asr.cj", "", "CJ's feet in Skyrim's world, where San Andreas's last pose puts him, facing his way", [](std::string_view) {
                RE::NiPoint3 feet;
                float        heading = 0.0f;
                if (!PlayerSync::DebugCj(feet, heading)) {
                    throw Error("San Andreas doesn't have the player");
                }
                return SkseTestKit::Place{ .x = feet.x, .y = feet.y, .z = feet.z, .heading = heading };
            });

            SkseTestKit::AddTarget("asr.victim", "", "The last guard asr.victim placed or person asr.seated found: its feet, facing its way",
                [](std::string_view) {
                    auto* victim = g_victim ? RE::TESForm::LookupByID<RE::Actor>(g_victim) : nullptr;
                    if (!victim) {
                        throw Error("no victim: asr.victim or asr.seated first");
                    }
                    const auto at = victim->GetPosition();
                    return SkseTestKit::Place{ .x = at.x, .y = at.y, .z = at.z, .heading = victim->GetAngleZ(),
                        .details = JsonObject{ { "dead", victim->IsDead() }, { "inKillMove", victim->IsInKillMove() }, { "ragdoll", victim->IsInRagdollState() } } };
                });

            SkseTestKit::AddCommand("asr.victim", "[meters] [toward=<bool>]",
                "Places a Whiterun guard meters (1.05) ahead of CJ, his back to him (toward=true: facing him), and takes out the one it placed "
                "before (dead or alive); answers with the guard",
                [](Call& a_call) -> Json {
                    RE::NiPoint3 feet;
                    float        heading = 0.0f;
                    if (!PlayerSync::DebugCj(feet, heading)) {
                        throw Error("San Andreas doesn't have the player: asr.cj first");
                    }
                    if (auto* before = g_placed ? RE::TESForm::LookupByID<RE::Actor>(g_placed) : nullptr; before && !before->IsDisabled()) {
                        before->Disable();
                        before->SetDelete(true);
                    }
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(Wanted::kHolds[0].guard);
                    auto  placed = player && base ? player->PlaceObjectAtMe(base, false) : nullptr;
                    auto* ref = placed ? placed->As<RE::Actor>() : nullptr;
                    if (!ref) {
                        throw Error("couldn't place a Whiterun guard");
                    }
                    const float meters = static_cast<float>(a_call.Number("meters", 1.05));
                    const auto  at = feet + RE::NiPoint3(std::sin(heading), std::cos(heading), 0.0f) * (meters * Pose::kUnitsPerMeter);
                    ref->SetPosition(at, true);
                    ref->SetHeading(a_call.Flag("toward", false) ? heading + std::numbers::pi_v<float> : heading);
                    // Calm: a guard placed while the last kill's alarm still runs would come out fighting CJ (whom a stealth
                    // kill rightly refuses, as San Andreas does someone attacking him).
                    ref->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, 0.0f);
                    ref->StopCombat();
                    g_victim = ref->GetFormID();
                    g_placed = g_victim;
                    return JsonObject{ { "ref", SkseTestKit::FormId(ref->GetFormID()) }, { "name", ref->GetName() }, { "at", Spot(at) },
                        { "distance", Round(Flat(at, feet) * Pose::kMetersPerUnit) } };
                });

            SkseTestKit::AddCommand("asr.seated", "[radius] [lean=<bool>]",
                "The nearest loaded person sitting on furniture within radius (4000) of the player that a stealth kill may take (not a "
                "child, not essential; not a guard, who leans on a wall; on a chair or a bench, or with lean=true leaning on a counter; with "
                "room to stand behind them), with its sit state",
                [](Call& a_call) -> Json {
                    auto*       player = RE::PlayerCharacter::GetSingleton();
                    auto*       lists = RE::ProcessLists::GetSingleton();
                    const float radius = static_cast<float>(a_call.Number("radius", 4000.0));
                    const bool  lean = a_call.Flag("lean", false);  // also someone leaning on a counter (the same sit state)
                    if (!player || !lists) {
                        throw Error("no player yet");
                    }
                    RE::Actor*  best = nullptr;
                    float       bestDistance = radius;
                    std::string others;  // the seated it can't take, and why
                    for (auto& handle : lists->highActorHandles) {
                        auto  actor = handle.get();
                        auto* state = actor ? actor->AsActorState() : nullptr;
                        if (!actor || actor.get() == player || !state || !StealthKill::Seated(static_cast<std::uint32_t>(state->GetSitSleepState()))) {
                            continue;
                        }
                        if (std::string_view(actor->GetName()).find("Guard") != std::string_view::npos) {
                            continue;  // guards lean on walls, where nobody stands behind them
                        }
                        // On a chair or a bench, not leaning on a counter or a wall (the same sit state): its furniture
                        // a seat, not a lean.
                        if (!lean && !OnASeat(actor.get())) {
                            others += std::format("{}{} (leaning, not on a seat)", others.empty() ? "" : ", ", actor->GetName());
                            continue;
                        }
                        if (!RoomBehind(actor.get(), 1.05f)) {
                            others += std::format("{}{} (no room behind)", others.empty() ? "" : ", ", actor->GetName());
                            continue;
                        }
                        if (const auto why = StealthKill::Excluded(Weapons::Knife, StealthKills::TargetOf(actor.get(), player)); why != StealthKill::Verdict::Yes) {
                            others += std::format("{}{} ({})", others.empty() ? "" : ", ", actor->GetName(), StealthKill::Why(why));
                            continue;
                        }
                        const float d = actor->GetPosition().GetDistance(player->GetPosition());
                        if (d < bestDistance) {
                            best = actor.get();
                            bestDistance = d;
                        }
                    }
                    if (!best) {
                        throw Error(std::format("nobody a stealth kill may take sits within {:.0f} of the player (seated: {})", radius, others.empty() ? "none" : others));
                    }
                    g_victim = best->GetFormID();
                    return JsonObject{ { "ref", SkseTestKit::FormId(best->GetFormID()) }, { "name", best->GetName() }, { "at", Spot(best->GetPosition()) },
                        { "heading", Round(best->GetAngleZ()) }, { "sitState", static_cast<int>(best->AsActorState()->GetSitSleepState()) },

                        { "distance", Round(bestDistance, 1.0) } };
                });

            SkseTestKit::AddCommand("asr.behind", "[ref] [meters]",
                "Puts Skyrim's player (asr.cj on=false first) meters (1.05) behind ref (the last asr.victim or asr.seated), facing the way it faces, for a handover there",
                [](Call& a_call) -> Json {
                    if (PlayerSync::DebugOwned()) {
                        throw Error("San Andreas has the player: asr.cj on=false first");
                    }
                    auto*       player = RE::PlayerCharacter::GetSingleton();
                    auto*       victim = ActorArg(a_call);
                    const float meters = static_cast<float>(a_call.Number("meters", 1.05));
                    const float heading = victim->GetAngleZ();
                    const auto  at = victim->GetPosition() - RE::NiPoint3(std::sin(heading), std::cos(heading), 0.0f) * (meters * Pose::kUnitsPerMeter);
                    player->SetPosition(at, true);
                    player->SetHeading(heading);
                    return JsonObject{ { "at", Spot(at) }, { "heading", Round(heading) } };
                });

            SkseTestKit::AddTask("asr.stealth", "[until] [crouch=<bool>] [aim=<seconds>]",
                "CJ's knife stealth kill on his lock-on target, as a player does it. until=grab (crouched first with crouch=true, the right "
                "button held aim (1.2) s, then the left one clicked) answers in the frame Skyrim takes the grab; until=stab in the frame of "
                "the stab; until=over once the victim is dead and let go. Each answers with the victim and CJ (cjMoved, victimMoved: flat "
                "units from where they stood at the grab; playerFromCj: Skyrim's hidden player from CJ)",
                [](Call& a_call) -> Step {
                    RequireCj();
                    const auto   until = a_call.Text("until", "grab");
                    const bool   crouch = a_call.Flag("crouch", false);
                    const double aim = a_call.Number("aim", 1.2);
                    if (until != "grab" && until != "stab" && until != "over") {
                        throw Error("until is grab, stab or over");
                    }
                    if (until != "grab" && !g_kill.victim) {
                        throw Error("no grab yet: asr.stealth until=grab first");
                    }
                    a_call.DefaultTimeout(15.0);
                    struct Run
                    {
                        int    grabs = 0;
                        int    stabs = 0;
                        bool   ready = false;
                        double readyAt = 0.0;
                    };
                    const auto before = StealthKills::DebugState();
                    auto       run = std::make_shared<Run>(Run{ .grabs = before.grabs, .stabs = before.stabs });
                    return [=](Call& a_call) -> std::optional<Json> {
                        if (SkseTestKit::Paused()) {
                            return std::nullopt;
                        }
                        const auto debug = StealthKills::DebugState();
                        if (until == "grab") {
                            if (debug.grabs > run->grabs) {
                                PlayerSync::DebugHold(std::nullopt);
                                g_kill = { debug.victim, debug.grabbedAt, CjFeet() };
                                return KillState(RE::TESForm::LookupByID<RE::Actor>(debug.victim));
                            }
                            if (!run->ready) {
                                if (!KnifeInHands() || (crouch && !Crouch(a_call, true))) {
                                    return std::nullopt;
                                }
                                run->ready = true;
                                run->readyAt = a_call.Seconds();
                            }
                            const double held = a_call.Seconds() - run->readyAt;
                            if (held > aim + 3.0) {
                                PlayerSync::DebugHold(std::nullopt);
                                std::string why = "no victim placed";
                                if (auto* victim = g_victim ? RE::TESForm::LookupByID<RE::Actor>(g_victim) : nullptr) {
                                    const auto feet = CjFeet();
                                    const auto target = StealthKills::TargetOf(victim, RE::PlayerCharacter::GetSingleton());
                                    const auto verdict = StealthKill::Check({ feet.x, feet.y, feet.z }, Weapons::Knife, target);
                                    why = std::format("{} is {} ({}{:.2f} m, {})", victim->GetName(), StealthKill::Why(verdict),
                                        verdict == StealthKill::Verdict::NotStanding ? StealthKills::OffItsFeet(victim) + ", " : std::string(),
                                        StealthKill::Distance({ feet.x, feet.y, feet.z }, target.feet), victim->IsInCombat() ? "in combat" : "not in combat");
                                }
                                throw Error("no grab: " + why);
                            }
                            const bool click = held >= aim && held < aim + 0.25;
                            PlayerSync::DebugHold(PlayerSync::DebugInput{ .buttons = Protocol::kButtonAim | (click ? Protocol::kButtonFire : 0u) });
                            return std::nullopt;
                        }
                        auto* victim = RE::TESForm::LookupByID<RE::Actor>(g_kill.victim);
                        if (until == "stab") {
                            return debug.stabs > run->stabs ? std::optional<Json>(KillState(victim)) : std::nullopt;
                        }
                        if (victim && victim->IsDead() && debug.held == 0 && !victim->IsInKillMove() && !debug.killMove) {
                            return KillState(victim);
                        }
                        return std::nullopt;
                    };
                });

            SkseTestKit::AddTask("asr.swing", "[ref] [crouch=<bool>]",
                "CJ swings the knife once (the left button) at ref (the last asr.victim), crouched or standing (crouch, false) as Skyrim's player "
                "sneaks; answers once the blow lands with ref's health before and after, the drop, the blow's sneak attack multiplier, how "
                "well it landed (Damage::Blows), and vsLast: this drop per landed against the last asr.swing's",
                [](Call& a_call) -> Step {
                    RequireCj();
                    const auto id = ActorArg(a_call)->GetFormID();
                    const bool crouch = a_call.Flag("crouch", false);
                    a_call.DefaultTimeout(12.0);
                    struct Swing
                    {
                        bool             ready = false;
                        double           readyAt = 0.0;
                        int              blows = 0;
                        float            before = 0.0f;
                        int              after = -1;  // frames since it landed
                        Shots::BlowDebug blow;
                    };
                    auto swing = std::make_shared<Swing>();
                    return [=](Call& a_call) -> std::optional<Json> {
                        if (SkseTestKit::Paused()) {
                            return std::nullopt;
                        }
                        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
                        auto* player = RE::PlayerCharacter::GetSingleton();
                        if (!actor || !player) {
                            throw Error("ref is gone");
                        }
                        if (!swing->ready) {
                            if (!KnifeInHands() || !Crouch(a_call, crouch)) {
                                return std::nullopt;
                            }
                            swing->ready = true;
                            swing->readyAt = a_call.Seconds();
                            swing->blows = Shots::DebugLastBlow().blows;
                            swing->before = HealthOf(actor);
                        }
                        if (swing->after < 0) {
                            const auto blow = Shots::DebugLastBlow();
                            if (blow.blows > swing->blows && blow.victim == id) {
                                PlayerSync::DebugHold(std::nullopt);
                                swing->blow = blow;
                                swing->after = 0;
                                return std::nullopt;
                            }
                            const double held = a_call.Seconds() - swing->readyAt;
                            if (held > 4.0) {
                                PlayerSync::DebugHold(std::nullopt);
                                throw Error("CJ's blow never landed on ref");
                            }
                            const bool press = held > 0.5 && held < 0.7;  // settled in the stance first
                            PlayerSync::DebugHold(press ? std::optional(PlayerSync::DebugInput{ .buttons = Protocol::kButtonFire }) : std::nullopt);
                            return std::nullopt;
                        }
                        if (++swing->after < 3) {
                            return std::nullopt;  // Skyrim's hit applied
                        }
                        const float now = HealthOf(actor);
                        const float drop = swing->before - now;
                        const float perLanded = swing->blow.landed > 0.0f ? drop / swing->blow.landed : 0.0f;
                        JsonObject  out{ { "before", Round(swing->before, 0.1) }, { "after", Round(now, 0.1) }, { "drop", Round(drop, 0.01) },
                            { "sneak", Round(swing->blow.sneak) }, { "landed", Round(swing->blow.landed, 0.001) }, { "saDamage", Round(swing->blow.saDamage) },
                            { "weapon", static_cast<int>(swing->blow.weapon) }, { "sneaking", player->IsSneaking() },
                            { "detection", actor->RequestDetectionLevel(player) } };
                        if (g_lastPerLanded > 0.0f) {
                            out.Set("vsLast", Round(perLanded / g_lastPerLanded, 0.01));
                        }
                        g_lastPerLanded = perLanded;
                        return out;
                    };
                });
        }

        // ---- Coming back (player/Respawns.cpp) ----

        struct Victim
        {
            std::uint32_t id = 0;
            std::string   name;
            std::string   verdict;  // Respawns' own words for what the kill does
        };
        std::vector<Victim> g_victims;  // asr.kill's, for asr.killed

        // How far a_actor stands from its editor spot, when both are in the same interior or worldspace.
        std::optional<double> FromSpot(RE::Actor* a_actor)
        {
            RE::NiPoint3 position;
            RE::NiPoint3 angle;
            RE::TESForm* where = nullptr;
            const auto*  cell = a_actor->GetParentCell();
            if (!cell || !a_actor->GetEditorLocation(position, angle, where, nullptr) || !where) {
                return std::nullopt;
            }
            const bool same = where->Is(RE::FormType::Cell) ? where == cell : !cell->IsInteriorCell() && where == a_actor->GetWorldspace();
            if (!same) {
                return std::nullopt;
            }
            return static_cast<double>(a_actor->GetPosition().GetDistance(position));
        }

        void AddRespawnChecks()
        {
            SkseTestKit::AddTask("asr.kill", "<ref> [all=<radius>]",
                "The player kills an actor, as CJ's blow would (the player's kill, Skyrim's death event); all=<radius> kills every living actor of "
                "the same name within that many units of the player too. The kills run from SKSE's task queue (a kill from the kit's point in "
                "the frame once waited on the engine for good); answers once all are dead, with each one's verdict (what Respawns says the "
                "death does), and keeps them for asr.killed",
                [](Call& a_call) -> Step {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    auto* first = SkseTestKit::FindRef(a_call.Text("ref"))->As<RE::Actor>();
                    if (!first || !player) {
                        throw Error("not an actor");
                    }
                    std::vector<RE::Actor*> targets{ first };
                    if (const double radius = a_call.Number("all", 0.0); radius > 0.0) {
                        const std::string name = first->GetDisplayFullName();
                        if (auto* lists = RE::ProcessLists::GetSingleton()) {
                            for (auto& handle : lists->highActorHandles) {
                                auto  held = handle.get();
                                auto* other = held.get();
                                if (other && other != first && !other->IsDead() && name == other->GetDisplayFullName() &&
                                    other->GetPosition().GetDistance(player->GetPosition()) <= radius) {
                                    targets.push_back(other);
                                }
                            }
                        }
                    }
                    auto killed = std::make_shared<JsonArray>();
                    std::vector<std::uint32_t> ids;
                    for (auto* actor : targets) {
                        if (actor->IsDead()) {
                            throw Error(std::format("{} ({}) is dead already", actor->GetDisplayFullName(), SkseTestKit::FormId(actor->GetFormID())));
                        }
                        Victim victim{ actor->GetFormID(), actor->GetDisplayFullName(), Respawns::DryRun(actor, player) };
                        killed->push_back(JsonObject{ { "id", SkseTestKit::FormId(victim.id) }, { "name", victim.name }, { "verdict", victim.verdict } });
                        ids.push_back(victim.id);
                        std::erase_if(g_victims, [&](const Victim& a_v) { return a_v.id == victim.id; });
                        g_victims.push_back(std::move(victim));
                    }
                    SKSE::GetTaskInterface()->AddTask([ids] {
                        auto* killer = RE::PlayerCharacter::GetSingleton();
                        for (const auto id : ids) {
                            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id); actor && killer && !actor->IsDead()) {
                                actor->KillImpl(killer, 100000.0f, true, true);
                            }
                        }
                    });
                    a_call.DefaultTimeout(10.0);
                    return [ids, killed](Call&) -> std::optional<Json> {
                        for (const auto id : ids) {
                            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
                            if (actor && !actor->IsDead()) {
                                return std::nullopt;
                            }
                        }
                        return JsonObject{ { "count", killed->size() }, { "killed", *killed } };
                    };
                });

            SkseTestKit::AddCommand("asr.judge", "<ref>", "What a kill of an actor by CJ would do, as Respawns judges it (nobody killed)", [](Call& a_call) -> Json {
                auto* actor = SkseTestKit::FindRef(a_call.Text("ref"))->As<RE::Actor>();
                if (!actor) {
                    throw Error("not an actor");
                }
                auto*      player = RE::PlayerCharacter::GetSingleton();
                const auto read = Respawns::DebugJudge(actor, player);
                return JsonObject{ { "id", SkseTestKit::FormId(actor->GetFormID()) }, { "name", actor->GetDisplayFullName() },
                    { "verdict", Respawns::DryRun(actor, player) }, { "baseAggression", read.baseAggression }, { "aggression", read.aggression },
                    { "reaction", read.reaction }, { "hostile", read.hostile }, { "lawful", read.lawful } };
            });

            SkseTestKit::AddCommand("asr.killed", "[forget=<bool>]",
                "Everyone asr.kill killed, in that order: dead or up, waiting to come back (and held in view), how far from their editor spot; "
                "with the counts. forget=true clears the list after answering",
                [](Call& a_call) -> Json {
                    JsonArray victims;
                    int       alive = 0;
                    int       waiting = 0;
                    for (const auto& victim : g_victims) {
                        auto*      actor = RE::TESForm::LookupByID<RE::Actor>(victim.id);
                        const bool dead = !actor || actor->IsDead();
                        const bool waits = Respawns::DebugWaiting(victim.id);
                        alive += dead ? 0 : 1;
                        waiting += waits ? 1 : 0;
                        const auto from = actor ? FromSpot(actor) : std::nullopt;
                        victims.push_back(JsonObject{ { "id", SkseTestKit::FormId(victim.id) }, { "name", victim.name }, { "verdict", victim.verdict },
                            { "dead", dead }, { "waiting", waits }, { "held", Respawns::DebugHeld(victim.id) }, { "fromSpot", from ? Json(*from) : Json() },
                            { "loaded", actor && actor->Is3DLoaded() } });
                    }
                    const int  count = static_cast<int>(g_victims.size());
                    JsonObject out{ { "count", count }, { "alive", alive }, { "dead", count - alive }, { "waiting", waiting }, { "victims", std::move(victims) } };
                    if (a_call.Flag("forget", false)) {
                        g_victims.clear();
                    }
                    return out;
                });

            SkseTestKit::AddCommand("asr.later", "<hours>",
                "Moves Skyrim's clock on by that many game hours (days passed and the hour), as a wait would, without the wait menu",
                [](Call& a_call) -> Json {
                    const double hours = a_call.Number("hours");
                    if (hours <= 0.0 || hours > 48.0) {
                        throw Error("hours is over 0, up to 48");
                    }
                    auto* calendar = RE::Calendar::GetSingleton();
                    if (!calendar || !calendar->gameDaysPassed || !calendar->gameHour) {
                        throw Error("no calendar yet");
                    }
                    calendar->gameDaysPassed->value += static_cast<float>(hours / 24.0);
                    calendar->gameHour->value = static_cast<float>(std::fmod(calendar->gameHour->value + hours, 24.0));
                    return JsonObject{ { "daysPassed", calendar->GetDaysPassed() }, { "hour", calendar->GetHour() } };
                });

            SkseTestKit::AddCommand("asr.objectives", "<quest>",
                "A quest (editor id) as Respawns reads it: running, its stage, and each objective's index, state and the references its "
                "target aliases hold",
                [](Call& a_call) -> Json {
                    const auto id = a_call.Text("quest");
                    auto*      quest = RE::TESForm::LookupByEditorID<RE::TESQuest>(id);
                    if (!quest) {
                        throw Error(std::format("no quest {}", id));
                    }
                    JsonArray objectives;
                    for (auto* objective : quest->objectives) {
                        if (!objective) {
                            continue;
                        }
                        JsonArray targets;
                        for (std::uint32_t i = 0; i < objective->numTargets; i++) {
                            const auto* target = objective->targets[i];
                            if (!target) {
                                continue;
                            }
                            JsonObject row{ { "alias", target->alias } };
                            for (auto* alias : quest->aliases) {
                                if (!alias || alias->aliasID != target->alias) {
                                    continue;
                                }
                                row["aliasName"] = alias->aliasName.c_str();
                                if (alias->GetVMTypeID() == RE::BGSRefAlias::VMTYPEID) {
                                    if (auto* held = static_cast<RE::BGSRefAlias*>(alias)->GetReference()) {
                                        row["ref"] = SkseTestKit::FormId(held->GetFormID());
                                        row["refName"] = held->GetDisplayFullName();
                                    }
                                }
                            }
                            targets.push_back(std::move(row));
                        }
                        objectives.push_back(JsonObject{ { "index", objective->index }, { "state", static_cast<int>(objective->state.get()) },
                            { "text", objective->displayText.c_str() }, { "targets", std::move(targets) } });
                    }
                    return JsonObject{ { "quest", id }, { "running", quest->IsRunning() }, { "stage", quest->GetCurrentStageID() },
                        { "objectives", std::move(objectives) } };
                });
        }
    }

    void Load()
    {
        // Shots without the HUD hide San Andreas's too (Overlay draws it in Present, before the kit's capture).
        const SkseTestKit::Hooks hooks{ .arrived = &Arrived, .ready = &CjReady, .hud = [](bool a_visible) { Overlay::SetHidden(!a_visible); } };
        if (SkseTestKit::Load({ .name = "AhShitRim", .ini = Ini::kPath, .hooks = hooks })) {
            AddCommands();
            AddTests();
            AddFeatureChecks();
            AddStealthKills();
            AddRespawnChecks();
        }
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_message)
    {
        SkseTestKit::OnMessage(a_message);
    }
}
