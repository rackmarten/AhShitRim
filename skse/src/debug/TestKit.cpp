// Built only with AHSHITRIM_TEST_KIT (dev builds); TestKitOff.cpp stands in otherwise.
#include "debug/TestKit.h"

#include "Ini.h"
#include "body/ModelView.h"
#include "body/MuzzleFlash.h"
#include "debug/HitTest.h"
#include "debug/LoadTest.h"
#include "debug/StreamTest.h"
#include "hud/Overlay.h"
#include "link/LinkService.h"
#include "player/Arms.h"
#include "player/Missions.h"
#include "player/PlayerSync.h"
#include "ride.h"
#include "vehicle_hits.h"
#include "vehicle_kinds.h"
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
            bool               lamps = false;  // the slot carries lamps (kHasLamps)
            std::uint8_t       lit = 0;        // the lamps burning as San Andreas sent them (asr.lamps off draws none of them)
            std::uint8_t       lampState = 0;
            Body::Vec3         front{}, rear{};  // the headlight and tail light dummies, in the vehicle's frame
        };
        std::array<Seen, Protocol::kVehicleMax> g_seen;
        bool                                    g_lampsOff = false;  // asr.lamps off: drawn and lit as if every lamp were out

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
            if constexpr (kHasLamps<T>) {
                seen.lamps = true;
                seen.lit = a_pose.lights.lit;
                seen.lampState = a_pose.lights.state;
                seen.front = { static_cast<float>(a_pose.lights.front[0]), static_cast<float>(a_pose.lights.front[1]), static_cast<float>(a_pose.lights.front[2]) };
                seen.rear = { static_cast<float>(a_pose.lights.rear[0]), static_cast<float>(a_pose.lights.rear[1]), static_cast<float>(a_pose.lights.rear[2]) };
                if (g_lampsOff) {
                    a_pose.lights.lit = 0;
                }
            }
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
                                       { "forcedOff", g_lampsOff }, { "frontDummy", Point(seen.front) }, { "rearDummy", Point(seen.rear) } });
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
                                { "heading", Round(barrel.heading * 180.0 / std::numbers::pi, 0.1) }, { "pitch", Round(barrel.pitch, 0.1) } } } };
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
                "scene lights), on gives San Andreas's back. Needs a build whose vehicles carry their lamps (protocol v36)",
                [](Call& a_call) -> Json {
                    if constexpr (!kHasLamps<Protocol::BodyState>) {
                        throw Error("this build's vehicles carry no lamps (the headlights, protocol v36, aren't in it)");
                    } else {
                        const auto which = a_call.Text("state");
                        if (which != "on" && which != "off") {
                            throw Error("asr.lamps on, or asr.lamps off");
                        }
                        g_lampsOff = which == "off";
                        return JsonObject{ { "lampsOff", g_lampsOff } };
                    }
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

            SkseTestKit::AddTask("asr.walk", "<points> [gait=<walk|run|sprint>] [arrive=<units>] [stuck=<seconds>]",
                "Walks CJ (San Andreas's stick, from its camera's view) through waypoints x,y; x,y; ... at a gait (run, the default, is San "
                "Andreas's jog), answering once he's within arrive (100) of the last; fails when he gets no closer to the next for stuck (5) s",
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
                                return JsonObject{ { "points", points.size() }, { "seconds", Round(a_call.Seconds()) }, { "path", Round(walk->path, 0.1) },
                                    { "at", Point(at) }, { "gait", gait } };
                            }
                            return std::nullopt;
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
                "muzzle's end) and across (off the barrel's line). release=true lets go of a flash asr.fire hold=true kept, after measuring",
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
    }

    void Load()
    {
        // Shots without the HUD hide San Andreas's too (Overlay draws it in Present, before the kit's capture).
        const SkseTestKit::Hooks hooks{ .arrived = &Arrived, .ready = &CjReady, .hud = [](bool a_visible) { Overlay::SetHidden(!a_visible); } };
        if (SkseTestKit::Load({ .name = "AhShitRim", .ini = Ini::kPath, .hooks = hooks })) {
            AddCommands();
            AddTests();
            AddFeatureChecks();
        }
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_message)
    {
        SkseTestKit::OnMessage(a_message);
    }
}
