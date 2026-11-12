// Built only with AHSHITRIM_TEST_KIT (dev builds); TestKitOff.cpp stands in otherwise.
#include "debug/TestKit.h"

#include "Ini.h"
#include "link/LinkService.h"
#include "player/Arms.h"
#include "player/PlayerSync.h"
#include "vehicle_kinds.h"
#include "weapons.h"

#include <skse_test_kit/Kit.h>

#include <array>

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

        // AhShitRim's commands on the kit's command pipe (docs/DEV_SETUP.md, "Test sessions").
        void AddCommands()
        {
            SkseTestKit::AddTask("asr.cj", "[on]",
                "Hands the player to San Andreas (on=true, the default) or back to Skyrim, as F10 does; answers once it's done",
                [](Call& a_call) -> Step {
                    const bool want = a_call.Flag("on", true);
                    auto       wait = std::make_shared<int>(0);
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
                        // in=true: the slot CJ drives. Else a slot that was empty before, or failing that one whose model
                        // changed (San Andreas reuses the oldest slot when all are taken).
                        const auto now = ReadSlots();
                        std::optional<std::uint32_t> found;
                        for (int pass = 0; pass < 2 && !found; pass++) {
                            for (std::uint32_t slot = 0; slot < Protocol::kVehicleMax && !found; slot++) {
                                Protocol::BodyState vehicle{};
                                if (!now[slot] || !LinkService::Get().ReadVehicle(slot, vehicle) || vehicle.handover != handover) {
                                    continue;
                                }
                                const bool driven = (vehicle.flags & Protocol::kBodyDriven) != 0;
                                const bool pick = in ? driven : (pass == 0 ? before[slot] == 0 : now[slot] != before[slot]);
                                if (pick) {
                                    found = slot;
                                }
                            }
                        }
                        if (!found) {
                            // San Andreas answers in a few frames; when it finds no flat, clear spot in front of CJ (another
                            // vehicle in the way) it spawns nothing and only its own log says so.
                            if (!in && a_call.Seconds() > 5.0) {
                                throw Error("San Andreas spawned nothing in 5 s: no clear spot in front of CJ? (its AhShitRim.log says why)");
                            }
                            return std::nullopt;
                        }
                        Protocol::BodyState vehicle{};
                        LinkService::Get().ReadVehicle(*found, vehicle);
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
        if (SkseTestKit::Load({ .name = "AhShitRim", .ini = Ini::kPath, .hooks = { .arrived = &Arrived, .ready = &CjReady } })) {
            AddCommands();
        }
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_message)
    {
        SkseTestKit::OnMessage(a_message);
    }
}
