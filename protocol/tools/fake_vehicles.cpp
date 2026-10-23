// Checks every kind of vehicle and CJ's gear against a running San Andreas without Skyrim (protocol v19): links up as
// the Skyrim side, hands the player over on flat ground streamed around CJ as Skyrim's sampler does, and through the dev
// key (' with a pick) spawns, boards and drives or flies an NRG-500, a BMX, a Hunter (its rotor, its guns), a Hydra
// (hover, then forward flight on the nozzle keys, its missiles) and a Rhino (its shell), refuses a boat, then puts the
// jetpack on, flies up, swaps it for the parachute, skydives, opens the chute and lands. Prints what it sees and exits
// non-zero when a check fails. Built with the protocol tests; run it through scripts/fake-vehicles.ps1 while San
// Andreas runs (launch-sa.ps1), with Skyrim closed.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../cpp/body.h"
#include "../cpp/collision.h"
#include "../cpp/input.h"
#include "../cpp/link.h"
#include "../cpp/pose.h"
#include "../cpp/skin_model.h"
#include "../cpp/vehicle_kinds.h"

#include <map>

using namespace AhShitRim;
using namespace std::chrono;

namespace
{
    constexpr float kAnchor[3] = { -30000.0f, 40000.0f, 2000.0f };

    struct Received
    {
        SkinModel::Assembler                                assembler;
        std::map<std::uint32_t, std::vector<std::uint8_t>> blobs;
        std::vector<Protocol::ProjectileEvent>             projectiles;
        int                                                 shots = 0;

        std::optional<SkinModel::Model> Model(std::uint32_t a_id) const
        {
            const auto found = blobs.find(a_id);
            return found != blobs.end() ? SkinModel::Parse(found->second.data(), found->second.size()) : std::nullopt;
        }
    };

    void Drain(Link& a_link, Received& a_received)
    {
        a_link.Receive([&](Protocol::MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
            if (a_type == Protocol::MessageType::Projectile && a_size == sizeof(Protocol::ProjectileEvent)) {
                Protocol::ProjectileEvent e{};
                std::memcpy(&e, a_data, sizeof(e));
                a_received.projectiles.push_back(e);
                return;
            }
            if (a_type == Protocol::MessageType::Shot) {
                a_received.shots++;
                return;
            }
            if (a_type != Protocol::MessageType::ModelChunk || a_received.assembler.Add(a_data, a_size) != SkinModel::Assembler::Result::Complete) {
                return;
            }
            const auto id = a_received.assembler.Id();
            const auto& blob = a_received.blobs[id] = a_received.assembler.Blob();
            if (const auto model = SkinModel::Parse(blob.data(), blob.size())) {
                std::printf("model %08X %s: %zu meshes, %zu bones\n", id, model->name.c_str(), model->meshes.size(), model->boneIds.size());
            }
        });
    }

    // AHSHITRIM_DUMP=<folder>: a model as Skyrim would draw it (skinned by a_body, hidden parts left out) into
    // <folder>/<a_name>.obj, Skyrim units relative to the handover spot, to look at what a pose does.
    void Dump(const Received& a_received, const Protocol::BodyState& a_body, const char* a_name)
    {
        const char* folder = std::getenv("AHSHITRIM_DUMP");
        const auto  model = folder ? a_received.Model(a_body.model) : std::nullopt;
        if (!model || a_body.boneCount != model->boneIds.size()) {
            return;
        }
        std::vector<Body::Matrix34> bones;
        for (std::uint32_t i = 0; i < a_body.boneCount; i++) {
            bones.push_back(Body::Hidden(a_body.hiddenParts, i) ? Body::Collapsed(a_body.bones[i]) : Body::ToMatrix(a_body.bones[i]));
        }
        const std::string path = std::string(folder) + "/" + a_name + ".obj";
        FILE*             out = std::fopen(path.c_str(), "w");
        if (!out) {
            return;
        }
        std::uint32_t base = 1;
        for (const auto& mesh : model->meshes) {
            for (std::uint32_t i = 0; i < mesh.vertexCount; i++) {
                Body::Vec3 v, n;
                Body::Skin(bones.data(), a_body.boneCount, mesh.Vertex(i), v, n);
                std::fprintf(out, "v %.1f %.1f %.1f\n", v.x + a_body.feet[0], v.y + a_body.feet[1], v.z + a_body.feet[2]);
            }
            for (std::uint32_t t = 0; t + 2 < mesh.indexCount; t += 3) {
                std::fprintf(out, "f %u %u %u\n", base + mesh.Index(t), base + mesh.Index(t + 1), base + mesh.Index(t + 2));
            }
            base += mesh.vertexCount;
        }
        std::fclose(out);
        std::printf("dumped %s\n", path.c_str());
    }

    int Bits(std::uint64_t a_mask)
    {
        int n = 0;
        for (; a_mask; a_mask &= a_mask - 1) {
            n++;
        }
        return n;
    }
}

int main()
{
    Link link(Protocol::Side::Skyrim, [](std::string_view a_line) { std::printf("link: %.*s\n", static_cast<int>(a_line.size()), a_line.data()); });
    const auto start = steady_clock::now();
    const auto ms = [&] { return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now() - start).count()); };
    while (!link.IsUp()) {
        if (ms() > 10000) {
            std::printf("no San Andreas plugin on the link after 10 s (is San Andreas running?)\n");
            return 1;
        }
        link.Tick(ms());
        std::this_thread::sleep_for(50ms);
    }

    Received       received;
    Input::Builder input;
    std::uint32_t  frame = 0;
    std::uint32_t  handoverId = 100;
    float          anchor[3] = { kAnchor[0], kAnchor[1], kAnchor[2] };
    const auto     publish = [&](bool a_owned) {
        auto s = input.Build(++frame, a_owned, true);
        s.handover = handoverId;
        s.anchor[0] = anchor[0];
        s.anchor[1] = anchor[1];
        s.anchor[2] = anchor[2];
        link.WriteInput(s);
    };
    std::set<std::pair<std::int32_t, std::int32_t>> sent;
    const auto stream = [&] {
        Protocol::PlayerState p{};
        const bool  have = link.ReadPlayer(p) && p.handover == handoverId;
        const float x = anchor[0] + (have ? p.offset[0] : 0.0f), y = anchor[1] + (have ? p.offset[1] : 0.0f);
        const auto  cx = Collision::ChunkIndex(x), cy = Collision::ChunkIndex(y);
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                if (sent.contains({ cx + dx, cy + dy })) {
                    continue;
                }
                Collision::Heightfield f;
                f.Reset(cx + dx, cy + dy, 32);
                std::fill(f.heights.begin(), f.heights.end(), anchor[2]);
                std::fill(f.hit.begin(), f.hit.end(), std::uint8_t{ 1 });
                const auto payload = Collision::BuildChunkPayload(f, 0x3C, handoverId);
                if (link.Send(Protocol::MessageType::CollisionChunk, payload.data(), static_cast<std::uint32_t>(payload.size()))) {
                    sent.insert({ cx + dx, cy + dy });
                }
            }
        }
    };
    const auto run = [&](double a_seconds, const auto& a_each) {
        const std::uint64_t until = ms() + static_cast<std::uint64_t>(a_seconds * 1000);
        while (ms() < until) {
            publish(true);
            link.Tick(ms());
            Drain(link, received);
            stream();
            if (a_each()) {
                return true;
            }
            std::this_thread::sleep_for(16ms);
        }
        return false;
    };
    const auto wait = [&](double a_seconds) { run(a_seconds, [] { return false; }); };
    const auto press = [&](std::uint32_t a_dik) {
        input.SetKey(a_dik, true);
        wait(0.3);
        input.SetKey(a_dik, false);
    };
    const auto player = [&] {
        Protocol::PlayerState p{};
        link.ReadPlayer(p);
        return p;
    };
    const auto inVehicle = [&] { return (player().flags & Protocol::kPlayerInVehicle) != 0; };
    const auto height = [&] { return player().offset[2] * Pose::kMetersPerUnit; };
    const auto speed = [&] {
        const auto p = player();
        return std::hypot(p.velocity[0], p.velocity[1]) * Pose::kMetersPerUnit;
    };
    // A fresh handover a little way off: San Andreas takes CJ out and removes every vehicle.
    const auto fresh = [&] {
        handoverId++;
        anchor[0] += 20000.0f;
        sent.clear();
        input.ReleaseAll();
        publish(true);
        run(5.0, [&] { return player().handover == handoverId && (player().flags & Protocol::kPlayerGrounded); });
        wait(1.0);
    };
    // The vehicle slot whose model's name starts with a_name, once it is there.
    const auto find = [&](const char* a_name, Protocol::BodyState& a_out) {
        for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
            Protocol::BodyState v{};
            if (link.ReadVehicle(i, v) && (v.flags & Protocol::kBodyValid) && v.handover == handoverId) {
                if (const auto model = received.Model(v.model); model && model->name.starts_with(a_name)) {
                    a_out = v;
                    return static_cast<int>(i);
                }
            }
        }
        return -1;
    };
    const auto spawn = [&](std::uint16_t a_model, const char* a_name, Protocol::BodyState& a_out) {
        input.SetDevModel(a_model);
        press(Input::kDikApostrophe);
        int slot = -1;
        run(10.0, [&] { return (slot = find(a_name, a_out)) >= 0; });
        std::printf("%s: %s, %u parts, %d hidden\n", a_name, slot >= 0 ? "spawned" : "MISSING", a_out.boneCount, Bits(a_out.hiddenParts));
        return slot;
    };
    const auto board = [&](const char* a_name) {
        press(Input::kDikF);
        const bool in = run(10.0, inVehicle);
        std::printf("%s: CJ %s\n", a_name, in ? "got in" : "DID NOT GET IN");
        wait(2.0);
        return in;
    };

    int failed = 0;
    publish(true);
    run(5.0, [&] { return player().handover == handoverId && (player().flags & Protocol::kPlayerGrounded); });
    wait(1.5);

    // Land: the NRG-500 and the BMX ride.
    for (const auto& [model, name, metres] : { std::tuple{ std::uint16_t{ 522 }, "NRG500", 10.0f }, std::tuple{ std::uint16_t{ 481 }, "BMX", 3.0f } }) {
        Protocol::BodyState bike{}, moved{};
        const int slot = spawn(model, name, bike);
        const bool in = slot >= 0 && board(name);
        find(name, bike);
        input.SetKey(Input::kDikW, true);
        wait(3.0);
        input.SetKey(Input::kDikW, false);
        find(name, moved);
        const float rode = std::hypot(moved.feet[0] - bike.feet[0], moved.feet[1] - bike.feet[1]) * Pose::kMetersPerUnit;
        std::printf("%s: rode %.1f m in 3 s, %s\n", name, rode, (moved.flags & Protocol::kBodyDriven) ? "ridden" : "NOT RIDDEN");
        failed += !(in && rode > metres && (moved.flags & Protocol::kBodyDriven));
        fresh();
    }

    // A boat never spawns.
    {
        Protocol::BodyState boat{};
        input.SetDevModel(430);
        press(Input::kDikApostrophe);
        const bool none = !run(3.0, [&] { return find("PREDATOR", boat) >= 0; });
        std::printf("Predator: %s\n", none ? "refused" : "SPAWNED");
        failed += !none;
    }

    // The Hunter: rotor up to speed (the standing rotor hidden for the spinning one), lift, its guns and rockets.
    {
        Protocol::BodyState heli{}, flying{};
        const int  slot = spawn(425, "HUNTER", heli);
        const bool in = slot >= 0 && board("HUNTER");
        const auto parked = heli.hiddenParts;
        const float ground = height();
        input.SetKey(Input::kDikW, true);
        const bool lifted = run(15.0, [&] { return height() - ground > 8.0f; });
        find("HUNTER", flying);
        std::printf("HUNTER: %s, %.1f m up; hidden parts %d parked, %d flying (%s)\n", lifted ? "lifted off" : "DID NOT LIFT", height() - ground,
            Bits(parked), Bits(flying.hiddenParts), parked != flying.hiddenParts ? "the rotor changed" : "THE ROTOR DID NOT CHANGE");
        input.SetKey(Input::kDikW, false);
        received.projectiles.clear();
        received.shots = 0;
        input.SetFire(true);
        wait(1.5);
        input.SetFire(false);
        input.SetAim(true);
        wait(1.5);
        input.SetAim(false);
        std::printf("HUNTER: firing sent %zu projectiles and %d shots\n", received.projectiles.size(), received.shots);
        failed += !(in && lifted && parked != flying.hiddenParts && (received.projectiles.size() + received.shots) > 0);
        fresh();
    }

    // The Hydra: it hovers up, then the nozzle keys take it to forward flight; its missiles.
    {
        Protocol::BodyState jet{};
        const int   slot = spawn(520, "HYDRA", jet);
        const bool  in = slot >= 0 && board("HYDRA");
        const float ground = height();
        input.SetKey(Input::kDikW, true);
        const bool hovered = run(15.0, [&] { return height() - ground > 15.0f; });
        std::printf("HYDRA: %s, %.1f m up at %.1f m/s\n", hovered ? "hovered up" : "DID NOT HOVER UP", height() - ground, speed());
        float fastest = 0.0f;
        const char* forwardKey = "none";
        for (const auto& [dik, keyName] : { std::pair{ Input::kDikNumpad8, "numpad 8" }, std::pair{ Input::kDikNumpad2, "numpad 2" } }) {
            input.SetKey(dik, true);
            float top = 0.0f;
            run(8.0, [&] {
                top = (std::max)(top, speed());
                return top > 30.0f;
            });
            input.SetKey(dik, false);
            std::printf("HYDRA: holding %s, up to %.1f m/s, %.1f m up\n", keyName, top, height() - ground);
            if (top > fastest) {
                fastest = top;
                forwardKey = keyName;
            }
            if (top > 30.0f) {
                break;
            }
        }
        received.projectiles.clear();
        input.SetFire(true);
        wait(1.0);
        input.SetFire(false);
        input.SetAim(true);
        wait(1.0);
        input.SetAim(false);
        input.SetKey(Input::kDikW, false);
        std::printf("HYDRA: forward flight at %.1f m/s on %s; firing sent %zu projectiles\n", fastest, forwardKey, received.projectiles.size());
        failed += !(in && hovered && fastest > 30.0f && !received.projectiles.empty());
        // AHSHITRIM_LONG_FLIGHT=<seconds>: fly on that long (nozzles forward, a slow turn) to load San Andreas's ground
        // and its pools, as a long flight in game does; the log's ground lines show the pools.
        if (const char* longFlight = std::getenv("AHSHITRIM_LONG_FLIGHT"); longFlight && std::atof(longFlight) > 0.0) {
            input.SetKey(Input::kDikW, true);
            input.SetKey(Input::kDikNumpad8, true);
            input.SetKey(Input::kDikA, true);
            float far = 0.0f;
            run(std::atof(longFlight), [&] {
                const auto p = player();
                far = (std::max)(far, std::hypot(p.offset[0], p.offset[1]) * Pose::kMetersPerUnit);
                return !link.IsUp();
            });
            input.ReleaseAll();
            std::printf("HYDRA: long flight of %s s, up to %.0f m out, link %s\n", longFlight, far, link.IsUp() ? "up" : "DOWN");
            failed += !link.IsUp();
        }
        fresh();
    }

    // The Rhino: its shell goes to Skyrim as a fast rocket without a model.
    {
        Protocol::BodyState tank{};
        const int  slot = spawn(432, "RHINO", tank);
        const bool in = slot >= 0 && board("RHINO");
        received.projectiles.clear();
        input.SetFire(true);
        wait(0.5);
        input.SetFire(false);
        wait(1.0);
        const bool shell = std::any_of(received.projectiles.begin(), received.projectiles.end(), [](const Protocol::ProjectileEvent& e) {
            return e.weaponType == 19 && e.model == 0 && e.fuseMs > 0 && e.fuseMs < 1000;
        });
        std::printf("RHINO: %s (%zu projectiles)\n", shell ? "fired a shell" : "NO SHELL", received.projectiles.size());
        failed += !(in && shell);
        fresh();
    }

    // The jetpack: up on it (the sprint key is San Andreas's accelerate), drawn in the gear slot. First on and off a BMX that
    // stays parked: San Andreas keeps it as CJ's last vehicle, which once kept the parachute from ever opening.
    {
        Protocol::BodyState parked{};
        if (spawn(481, "BMX", parked) >= 0 && board("BMX")) {
            press(Input::kDikF);
            run(6.0, [&] { return !inVehicle(); });
            wait(1.0);
        }
        input.SetDevModel(VehicleKinds::kJetpack);
        press(Input::kDikApostrophe);
        wait(1.0);
        const float ground = height();
        input.SetKey(Input::kDikLeftShift, true);
        const bool up = run(30.0, [&] { return height() - ground > 90.0f; });
        input.SetKey(Input::kDikLeftShift, false);
        Protocol::BodyState gear{};
        const bool drawn = link.ReadGear(gear) && (gear.flags & Protocol::kBodyJetpack) && received.Model(gear.model);
        std::printf("jetpack: %s, %.1f m up, %s\n", up ? "flew up" : "DID NOT FLY UP", height() - ground, drawn ? "drawn" : "NOT DRAWN");
        failed += !(up && drawn);

        // Back down (the jump key is San Andreas's brake: down), and off it with F standing still, as San Andreas allows.
        input.SetKey(Input::kDikSpace, true);
        run(40.0, [&] { return (player().flags & Protocol::kPlayerGrounded) != 0; });
        input.SetKey(Input::kDikSpace, false);
        wait(1.0);
        press(Input::kDikF);
        wait(1.5);
        Protocol::BodyState off{};
        const bool takenOff = !(link.ReadGear(off) && (off.flags & Protocol::kBodyJetpack));
        std::printf("jetpack: landed and %s\n", takenOff ? "taken off with F" : "STILL ON");
        failed += !takenOff;

        // The parachute on board a Hunter, up 60 m, and a bail-out (San Andreas's fall from a flying vehicle): it comes out,
        // he skydives, opens it with the fire key and lands.
        fresh();  // the F that took the jetpack off may have sat him on the BMX next to him
        Protocol::BodyState heli{};
        const bool inHeli = spawn(425, "HUNTER", heli) >= 0 && board("HUNTER");
        input.SetDevModel(VehicleKinds::kParachute);
        press(Input::kDikApostrophe);
        const float heliGround = height();
        input.SetKey(Input::kDikW, true);
        const bool climbed = run(150.0, [&] { return height() - heliGround > 100.0f; });
        input.SetKey(Input::kDikW, false);
        wait(3.0);  // San Andreas lets him out once the climb has eased
        input.SetKey(Input::kDikF, true);
        const bool bailed = run(8.0, [&] { return !inVehicle(); });
        input.SetKey(Input::kDikF, false);
        std::printf("parachute: %s the Hunter, %.0f m up, %s\n", inHeli ? "in" : "NOT IN", height() - heliGround, bailed ? "bailed out" : "DID NOT BAIL OUT");
        failed += !(inHeli && climbed && bailed);
        float fastest = 0.0f;
        run(1.5, [&] {
            fastest = (std::max)(fastest, -player().velocity[2] * Pose::kMetersPerUnit);
            return false;
        });
        {
            Protocol::BodyState cjBody{};
            link.ReadBody(cjBody);
            Dump(received, cjBody, "skydive_cj");
        }
        input.SetFire(true);
        wait(0.3);
        input.SetFire(false);
        bool  canopy = false;
        float floatFall = 0.0f;
        bool  dumped = false;
        const auto opened = ms();
        const bool landed = run(60.0, [&] {
            Protocol::BodyState g{};
            if (link.ReadGear(g) && (g.flags & Protocol::kBodyParachute) && received.Model(g.model)) {
                canopy = true;
            }
            const auto p = player();
            if (canopy && !(p.flags & Protocol::kPlayerGrounded)) {
                floatFall = -p.velocity[2] * Pose::kMetersPerUnit;
            }
            if (canopy && !dumped && ms() - opened > 2500) {
                dumped = true;
                Protocol::BodyState cjBody{};
                link.ReadBody(cjBody);
                Dump(received, cjBody, "float_cj");
                Dump(received, g, "float_canopy");
            }
            return canopy && (p.flags & Protocol::kPlayerGrounded) != 0;
        });
        wait(2.0);
        const auto p = player();
        const bool kept = std::any_of(std::begin(p.loadout), std::end(p.loadout), [](const Protocol::WeaponEntry& w) { return w.type == 46; });
        std::printf("parachute: fell up to %.1f m/s, canopy %s, floating down at %.1f m/s, %s, parachute %s, damage so far %.0f\n", fastest,
            canopy ? "out" : "NEVER OUT", floatFall, landed ? "landed" : "DID NOT LAND", kept ? "STILL CARRIED" : "used up", p.damage);
        failed += !(fastest > 10.0f && canopy && floatFall < 8.0f && landed && !kept);
    }

    publish(false);
    link.Tick(ms());
    std::printf("%s\n", failed ? "SOME CHECKS FAILED" : "all checks passed");
    return failed ? 2 : 0;
}
