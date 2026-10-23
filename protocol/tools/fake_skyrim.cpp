// Stands in for the Skyrim plugin, to drive a running San Andreas without Skyrim: links up as the
// Skyrim side, hands the player over twice, sends flat ground (7 x 7 collision chunks) around the
// handover spot, holds W for a few seconds and prints CJ's poses. Also receives CJ's model, skins
// it with the skeleton San Andreas sends and checks he stands about as tall as a man, on his feet,
// and that his bones move while he walks. A third handover spawns a Sandking, gets in with F,
// drives it with W and gets out again, then spawns a Sanchez, rides it a little and spawns a ZR-350,
// and checks the vehicles' models arrive and stand on the ground, and that the Sandking moved with
// CJ in it and turned its wheels. A re-anchor in the ZR-350 (fast travel, far off and far below)
// takes CJ out and removes the vehicles, holds him until ground comes there, and he walks on it.
// Scenes (v9) there: CJ turns to a speaker, gestures at a shout and a swear (v16), follows a walking Skyrim player,
// sits where it sits and walks on when it lets go. Built with the protocol tests; run it through scripts/fake-skyrim.ps1 while San Andreas runs
// (launch-sa.ps1).
//
// Usage: fake_skyrim [seconds of W, default 3]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>
#include <vector>

#include "../cpp/body.h"
#include "../cpp/collision.h"
#include "../cpp/input.h"
#include "../cpp/radio.h"
#include "../cpp/link.h"
#include "../cpp/pose.h"
#include "../cpp/skin_model.h"

#include <map>
#include <set>

using namespace AhShitRim;
using namespace std::chrono;

// Somewhere in Skyrim's Tamriel, away from the chunk grid's lines.
constexpr float kAnchor[3] = { 10000.0f, -20000.0f, 3000.0f };

// Every model San Andreas sent, by id (CJ's and the vehicles').
struct Received
{
    SkinModel::Assembler                                assembler;
    std::map<std::uint32_t, std::vector<std::uint8_t>> blobs;

    std::optional<SkinModel::Model> Model(std::uint32_t a_id) const
    {
        const auto found = blobs.find(a_id);
        return found != blobs.end() ? SkinModel::Parse(found->second.data(), found->second.size()) : std::nullopt;
    }
};

void Drain(Link& a_link, Received& a_received)
{
    a_link.Receive([&](Protocol::MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
        if (a_type != Protocol::MessageType::ModelChunk || a_received.assembler.Add(a_data, a_size) != SkinModel::Assembler::Result::Complete) {
            return;
        }
        const auto id = a_received.assembler.Id();
        const auto& blob = a_received.blobs[id] = a_received.assembler.Blob();
        const auto model = SkinModel::Parse(blob.data(), blob.size());
        if (!model) {
            std::printf("model %08X: malformed (%zu bytes)\n", id, blob.size());
            return;
        }
        std::uint32_t vertices = 0, triangles = 0;
        for (const auto& mesh : model->meshes) {
            vertices += mesh.vertexCount;
            triangles += mesh.indexCount / 3;
        }
        std::printf("model %08X %s (%s): %zu meshes, %u vertices, %u triangles, %zu textures, %zu bones (%zu bytes)\n", id, model->name.c_str(),
            model->kind == Protocol::kSkinModelVehicle ? "vehicle" : "body", model->meshes.size(), vertices, triangles, model->textures.size(),
            model->boneIds.size(), blob.size());
    });
}

// A model's skinned extent relative to its origin (CJ's feet, a vehicle's position), units: lowest
// and highest vertex.
bool Extent(const Received& a_received, const Protocol::BodyState& a_body, float& a_low, float& a_high)
{
    const auto model = a_received.Model(a_body.model);
    if (!model || a_body.boneCount != model->boneIds.size()) {
        return false;
    }
    std::vector<Body::Matrix34> bones;
    for (std::uint32_t i = 0; i < a_body.boneCount; i++) {
        bones.push_back(Body::ToMatrix(a_body.bones[i]));
    }
    a_low = 1e9f;
    a_high = -1e9f;
    for (const auto& mesh : model->meshes) {
        for (std::uint32_t i = 0; i < mesh.vertexCount; i++) {
            Body::Vec3 p, n;
            Body::Skin(bones.data(), a_body.boneCount, mesh.Vertex(i), p, n);
            a_low = (std::min)(a_low, p.z);
            a_high = (std::max)(a_high, p.z);
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    const double walkSeconds = argc > 1 ? std::atof(argv[1]) : 3.0;
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
    std::uint32_t  handoverId = 0;
    float          anchor[3] = { kAnchor[0], kAnchor[1], kAnchor[2] };
    Protocol::InputState scene{};  // v9: the scene flags, offset, heading and shout count to send
    const auto     publish = [&](bool a_owned) {
        auto s = input.Build(++frame, a_owned, true);
        s.flags |= scene.flags;
        for (int i = 0; i < 3; ++i) {
            s.sceneOffset[i] = scene.sceneOffset[i];
        }
        s.sceneHeading = scene.sceneHeading;
        s.shoutCount = scene.shoutCount;
        s.handover = handoverId;
        s.heading = 0.0f;
        s.anchor[0] = anchor[0];
        s.anchor[1] = anchor[1];
        s.anchor[2] = anchor[2];
        link.WriteInput(s);
        Protocol::AudioState audio{};  // v7: half volume, as from Skyrim's sliders
        audio.effects = a_owned ? 0.5f : 0.0f;
        audio.music = a_owned ? 0.5f : 0.0f;
        link.WriteAudio(audio);
    };

    // Two handovers, the second elsewhere and higher up: San Andreas drops the first one's ground
    // while CJ still stands on it (that once crashed it).
    int failed = 0;
    for (int round = 0; round < 2; ++round) {
        handoverId = static_cast<std::uint32_t>(round + 1);
        anchor[1] = kAnchor[1] + round * 4000.0f;
        anchor[2] = kAnchor[2] + round * 700.0f;

        // Hand over, and send flat ground at the handover spot's height around it.
        publish(true);
        const auto cx = Collision::ChunkIndex(anchor[0]), cy = Collision::ChunkIndex(anchor[1]);
        int        chunks = 0;
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                Collision::Heightfield f;
                f.Reset(cx + dx, cy + dy, 32);
                std::fill(f.heights.begin(), f.heights.end(), anchor[2]);
                std::fill(f.hit.begin(), f.hit.end(), std::uint8_t{ 1 });
                const auto payload = Collision::BuildChunkPayload(f, 0x3C, handoverId);
                chunks += link.Send(Protocol::MessageType::CollisionChunk, payload.data(), static_cast<std::uint32_t>(payload.size()));
            }
        }
        std::printf("handover %u: sent %d flat collision chunks\n", handoverId, chunks);

        // Stand for a second, walk forwards, then stand again and hand back.
        const std::uint64_t handover = ms();
        Protocol::PlayerState pose{};
        Protocol::BodyState   body{}, standing{};
        bool                  haveStanding = false;
        float                 boneMoved = 0.0f;  // the most any bone moved from standing while walking, units
        std::uint64_t         lastPrint = 0;
        bool                  walking = false;
        while (ms() - handover < static_cast<std::uint64_t>((walkSeconds + 2.0) * 1000)) {
            const double t = (ms() - handover) / 1000.0;
            const bool   walk = t >= 1.0 && t < 1.0 + walkSeconds;
            if (walk != walking) {
                walking = walk;
                input.SetKey(Input::kDikW, walk);
                std::printf("%.1f s: %s W\n", t, walk ? "holding" : "letting go of");
            }
            publish(true);
            link.Tick(ms());
            Drain(link, received);
            if (link.ReadBody(body) && body.handover == handoverId && (body.flags & Protocol::kBodyValid) != 0) {
                if (!walk && t < 1.0) {
                    standing = body;
                    haveStanding = true;
                } else if (walk && haveStanding && body.boneCount == standing.boneCount) {
                    for (std::uint32_t i = 0; i < body.boneCount; i++) {
                        for (int k = 0; k < 3; k++) {
                            boneMoved = (std::max)(boneMoved, std::abs(body.bones[i].translation[k] - standing.bones[i].translation[k]));
                        }
                    }
                }
            }
            if (ms() - lastPrint >= 500 && link.ReadPlayer(pose)) {
                lastPrint = ms();
                std::printf("%.1f s: pose frame %u handover %u%s offset (%.0f, %.0f, %.0f) units, speed %.1f m/s, heading %.2f, view %.2f/%.2f\n",
                    t, pose.frame, pose.handover, (pose.flags & Protocol::kPlayerGrounded) ? " grounded" : " in the air",
                    pose.offset[0], pose.offset[1], pose.offset[2],
                    std::hypot(pose.velocity[0], pose.velocity[1]) * Pose::kMetersPerUnit, pose.heading, pose.viewHeading, pose.viewPitch);
            }
            std::this_thread::sleep_for(16ms);
        }
        publish(false);
        link.Tick(ms());
        const float metres = std::hypot(pose.offset[0], pose.offset[1]) * Pose::kMetersPerUnit;
        const float height = pose.offset[2] * Pose::kMetersPerUnit;
        std::printf("handover %u: CJ ended %.1f m from the handover spot, %.2f m above it%s\n", handoverId, metres, height,
            (pose.flags & Protocol::kPlayerGrounded) ? ", standing" : ", in the air");
        failed += !(pose.handover == handoverId && metres > 1.0f && std::abs(height) < 0.5f && link.IsUp());

        // CJ's body: about as tall as a man, his feet at his feet, and his limbs moved while he walked.
        float low = 0.0f, high = 0.0f;
        const bool  skinned = haveStanding && Extent(received, standing, low, high);
        const float tall = (high - low) * Pose::kMetersPerUnit;
        std::printf("handover %u: CJ's body %s: standing %.2f m tall, lowest vertex %.2f m from his feet; bones moved up to %.2f m walking; "
                    "camera %.1f m from his feet, fov %.0f\n",
            handoverId, skinned ? "skinned" : "missing", tall, low * Pose::kMetersPerUnit, boneMoved * Pose::kMetersPerUnit,
            std::hypot(body.cameraPosition[0] - body.feet[0], body.cameraPosition[1] - body.feet[1], body.cameraPosition[2] - body.feet[2]) *
                Pose::kMetersPerUnit,
            body.cameraFov);
        failed += !(skinned && tall > 1.4f && tall < 2.2f && std::abs(low * Pose::kMetersPerUnit) < 0.3f && boneMoved * Pose::kMetersPerUnit > 0.05f);
    }

    // Vehicles: a third handover; spawn a Sandking, get in, drive, get out; then spawn a Sanchez.
    handoverId = 3;
    anchor[0] = kAnchor[0] + 8000.0f;
    anchor[1] = kAnchor[1];
    anchor[2] = kAnchor[2];
    publish(true);
    {
        const auto cx = Collision::ChunkIndex(anchor[0]), cy = Collision::ChunkIndex(anchor[1]);
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                Collision::Heightfield f;
                f.Reset(cx + dx, cy + dy, 32);
                std::fill(f.heights.begin(), f.heights.end(), anchor[2]);
                std::fill(f.hit.begin(), f.hit.end(), std::uint8_t{ 1 });
                const auto payload = Collision::BuildChunkPayload(f, 0x3C, handoverId);
                link.Send(Protocol::MessageType::CollisionChunk, payload.data(), static_cast<std::uint32_t>(payload.size()));
            }
        }
    }
    const auto run = [&](double a_seconds, const auto& a_each) {
        const std::uint64_t until = ms() + static_cast<std::uint64_t>(a_seconds * 1000);
        while (ms() < until) {
            publish(true);
            link.Tick(ms());
            Drain(link, received);
            if (a_each()) {
                return true;
            }
            std::this_thread::sleep_for(16ms);
        }
        return false;
    };
    const auto press = [&](std::uint32_t a_dik) {
        input.SetKey(a_dik, true);
        run(0.3, [] { return false; });
        input.SetKey(a_dik, false);
    };
    const auto vehicleIn = [&](std::uint32_t a_slot, Protocol::BodyState& a_out) {
        return link.ReadVehicle(a_slot, a_out) && (a_out.flags & Protocol::kBodyValid) && a_out.handover == handoverId && received.Model(a_out.model);
    };
    const auto inVehicle = [&] {
        Protocol::PlayerState p{};
        return link.ReadPlayer(p) && p.handover == handoverId && (p.flags & Protocol::kPlayerInVehicle);
    };

    run(1.5, [] { return false; });  // the ground comes in
    press(Input::kDikRightBracket);
    Protocol::BodyState car{}, parked{};
    const bool spawned = run(10.0, [&] { return vehicleIn(0, car); });
    float carLow = 0.0f, carHigh = 0.0f;
    const bool carSkinned = spawned && Extent(received, car, carLow, carHigh);
    // The lowest vertex (a tyre) on the ground: the vehicle's position plus its lowest vertex at the
    // handover spot's height.
    const float carGround = (car.feet[2] + carLow) * Pose::kMetersPerUnit;
    std::printf("vehicles: Sandking %s, %u parts, %.1f m from the handover spot, lowest vertex %.2f m above the ground, %.2f m tall\n",
        spawned ? "spawned" : "missing", car.boneCount, std::hypot(car.feet[0], car.feet[1]) * Pose::kMetersPerUnit, carGround,
        (carHigh - carLow) * Pose::kMetersPerUnit);
    failed += !(carSkinned && std::abs(carGround) < 0.3f && car.boneCount >= 5);
    run(1.0, [&] { return !vehicleIn(0, parked); });  // settled

    press(Input::kDikF);
    const bool gotIn = run(6.0, [&] { return inVehicle(); });
    std::printf("vehicles: CJ %s the Sandking\n", gotIn ? "got into" : "did not get into");
    failed += !gotIn;
    run(2.0, [] { return false; });  // CJ shuts the door before he can drive

    // The radio (v10): the Sandking's own station, then the wheel dials through every station,
    // Radio Sovngarde, Sweetroll FM and off (14 stops), round to where it started.
    {
        Protocol::PlayerState p{};
        link.ReadPlayer(p);
        std::int8_t at = p.radioStation;
        std::printf("radio: the Sandking plays %s (%d)\n", at == Protocol::kRadioNone ? "nothing" : Radio::StationName(at).data(), at);
        int dialed = 0;
        for (int step = 0; step < Radio::kRadioOff && at != Protocol::kRadioNone; ++step) {
            const std::int8_t want = Radio::Dial(at, 1);
            input.AddWheel(true);
            const bool tuned = run(6.0, [&] {
                Protocol::PlayerState q{};
                return link.ReadPlayer(q) && q.radioStation == want && !(q.flags & Protocol::kPlayerRadioRetuning);
            });
            std::printf("radio: wheel up to %s %s\n", Radio::StationName(want).data(), tuned ? "tuned" : "NOT TUNED");
            dialed += tuned;
            at = want;
        }
        failed += dialed != Radio::kRadioOff;
    }

    // HUD sounds (v24): each one San Andreas may play, then one it drops (34, the radio retune's loop). Its log
    // shows them ("audio: HUD sound", "audio: no HUD sound 34").
    {
        int sent = 0;
        for (const auto event : { 6, 7, 8, 10, 11, 32, 40, 34 }) {
            Protocol::FrontendSoundCommand command{};
            command.event = static_cast<std::uint8_t>(event);
            sent += link.Send(Protocol::MessageType::FrontendSound, &command, sizeof(command));
            run(0.5, [] { return false; });
        }
        std::printf("hud sounds: %d of 8 sent\n", sent);
        failed += sent != 8;
    }

    Protocol::BodyState before{}, driving{};
    vehicleIn(0, before);
    float wheelTurn = 0.0f;  // the most any part turned while driving, quaternion distance
    input.SetKey(Input::kDikW, true);
    run(3.0, [&] {  // the flat ground ends about 85 m out; a Sandking does 30 m/s after 4 s
        if (vehicleIn(0, driving) && driving.boneCount == before.boneCount) {
            for (std::uint32_t i = 0; i < driving.boneCount; i++) {
                float dot = 0.0f;
                for (int k = 0; k < 4; k++) {
                    dot += driving.bones[i].rotation[k] * before.bones[i].rotation[k];
                }
                wheelTurn = (std::max)(wheelTurn, 1.0f - std::abs(dot));
            }
        }
        return false;
    });
    input.SetKey(Input::kDikW, false);
    const float driven = std::hypot(driving.feet[0] - before.feet[0], driving.feet[1] - before.feet[1]) * Pose::kMetersPerUnit;
    Protocol::PlayerState cj{};
    link.ReadPlayer(cj);
    const float cjToCar = std::hypot(cj.offset[0] - driving.feet[0], cj.offset[1] - driving.feet[1]) * Pose::kMetersPerUnit;
    std::printf("vehicles: drove %.1f m in 3 s (%s), a part turned by %.3f, CJ %.1f m from the car's middle, speed %.1f m/s\n", driven,
        (driving.flags & Protocol::kBodyDriven) ? "driven" : "not driven", wheelTurn, cjToCar,
        std::hypot(cj.velocity[0], cj.velocity[1]) * Pose::kMetersPerUnit);
    failed += !(driven > 10.0f && (driving.flags & Protocol::kBodyDriven) && wheelTurn > 0.1f && cjToCar < 2.0f);

    input.SetKey(Input::kDikSpace, true);  // handbrake
    run(3.0, [] { return false; });
    input.SetKey(Input::kDikSpace, false);
    press(Input::kDikF);
    const bool gotOut = run(6.0, [&] { return !inVehicle(); });
    std::printf("vehicles: CJ %s the Sandking\n", gotOut ? "got out of" : "did not get out of");
    failed += !gotOut;

    run(1.5, [] { return false; });
    press(Input::kDikBackslash);
    Protocol::BodyState bike{};
    const bool bikeSpawned = run(10.0, [&] { return vehicleIn(1, bike); });
    float bikeLow = 0.0f, bikeHigh = 0.0f;
    const bool  bikeSkinned = bikeSpawned && Extent(received, bike, bikeLow, bikeHigh);
    const float bikeGround = (bike.feet[2] + bikeLow) * Pose::kMetersPerUnit;
    std::printf("vehicles: Sanchez %s, %u parts, lowest vertex %.2f m above the ground, %.2f m tall\n", bikeSpawned ? "spawned" : "missing",
        bike.boneCount, bikeGround, (bikeHigh - bikeLow) * Pose::kMetersPerUnit);
    const auto bikeModel = bikeSpawned ? received.Model(bike.model) : std::nullopt;
    failed += !(bikeSkinned && std::abs(bikeGround) < 0.4f && bikeModel && bikeModel->name.starts_with("SANCHEZ"));

    // On the Sanchez (San Andreas streams the bike animations in), a short ride turning right, off.
    press(Input::kDikF);
    const bool onBike = run(8.0, [&] { return inVehicle(); });
    run(2.0, [] { return false; });
    Protocol::BodyState bikeBefore{}, riding{};
    vehicleIn(1, bikeBefore);
    input.SetKey(Input::kDikW, true);
    input.SetKey(Input::kDikD, true);
    run(2.0, [&] { return vehicleIn(1, riding) && false; });
    input.SetKey(Input::kDikD, false);
    input.SetKey(Input::kDikW, false);
    const float rode = std::hypot(riding.feet[0] - bikeBefore.feet[0], riding.feet[1] - bikeBefore.feet[1]) * Pose::kMetersPerUnit;
    input.SetKey(Input::kDikS, true);  // San Andreas ignores the exit key on a fast bike
    run(3.0, [] { return false; });
    input.SetKey(Input::kDikS, false);
    press(Input::kDikF);
    const bool offBike = run(6.0, [&] { return !inVehicle(); });
    std::printf("vehicles: CJ %s the Sanchez, rode %.1f m in 2 s (%s), %s\n", onBike ? "got on" : "did not get on", rode,
        (riding.flags & Protocol::kBodyDriven) ? "ridden" : "not ridden", offBike ? "got off" : "did not get off");
    failed += !(onBike && rode > 5.0f && offBike);

    run(1.5, [] { return false; });
    press(Input::kDikLeftBracket);
    Protocol::BodyState zr{};
    const bool zrSpawned = run(10.0, [&] { return vehicleIn(2, zr); });
    float zrLow = 0.0f, zrHigh = 0.0f;
    const bool  zrSkinned = zrSpawned && Extent(received, zr, zrLow, zrHigh);
    const float zrGround = (zr.feet[2] + zrLow) * Pose::kMetersPerUnit;
    std::printf("vehicles: ZR-350 %s, %u parts, lowest vertex %.2f m above the ground, %.2f m tall\n", zrSpawned ? "spawned" : "missing", zr.boneCount,
        zrGround, (zrHigh - zrLow) * Pose::kMetersPerUnit);
    const auto zrModel = zrSpawned ? received.Model(zr.model) : std::nullopt;
    failed += !(zrSkinned && std::abs(zrGround) < 0.3f && zrModel && zrModel->name.starts_with("ZR350"));

    // In the ZR-350, a re-anchor, as Skyrim sends for fast travel or a load door (the handover goes
    // on with a new anchor), far off and far below, like an interior: CJ comes out of the car, every
    // vehicle goes, and he waits for the ground there.
    press(Input::kDikF);
    const bool inZr = run(8.0, [&] { return inVehicle(); });
    handoverId = 4;
    anchor[0] = kAnchor[0] + 60000.0f;
    anchor[2] = kAnchor[2] - 10000.0f;  // no ground sent yet: CJ just waits for it
    publish(true);
    const bool cleared = run(5.0, [&] {
        Protocol::PlayerState p{};
        Protocol::BodyState   v{};
        bool                  any = false;
        for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
            any |= link.ReadVehicle(i, v) && (v.flags & Protocol::kBodyValid) != 0;
        }
        return link.ReadPlayer(p) && p.handover == handoverId && !(p.flags & Protocol::kPlayerInVehicle) && !any;
    });
    run(1.0, [] { return false; });
    Protocol::PlayerState held{};
    link.ReadPlayer(held);
    const float heldAt = std::hypot(std::hypot(held.offset[0], held.offset[1]), held.offset[2]) * Pose::kMetersPerUnit;
    std::printf("vehicles: CJ %s the ZR-350; a re-anchor %s, and held him %.2f m from the new anchor while its ground was missing\n",
        inZr ? "got into" : "did not get into", cleared ? "took him out and removed the vehicles" : "left him in or left vehicles behind", heldAt);
    failed += !(inZr && cleared && link.IsUp() && held.handover == handoverId && heldAt < 0.5f);

    // And CJ still walks after that.
    {
        const auto cx = Collision::ChunkIndex(anchor[0]), cy = Collision::ChunkIndex(anchor[1]);
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                Collision::Heightfield f;
                f.Reset(cx + dx, cy + dy, 32);
                std::fill(f.heights.begin(), f.heights.end(), anchor[2]);
                std::fill(f.hit.begin(), f.hit.end(), std::uint8_t{ 1 });
                const auto payload = Collision::BuildChunkPayload(f, 0x3C, handoverId);
                link.Send(Protocol::MessageType::CollisionChunk, payload.data(), static_cast<std::uint32_t>(payload.size()));
            }
        }
    }
    run(1.5, [] { return false; });
    input.SetKey(Input::kDikW, true);
    run(2.0, [] { return false; });
    input.SetKey(Input::kDikW, false);
    Protocol::PlayerState after{};
    link.ReadPlayer(after);
    const float walked = std::hypot(after.offset[0], after.offset[1]) * Pose::kMetersPerUnit;
    std::printf("vehicles: after the re-anchor CJ walked %.1f m on foot\n", walked);
    failed += !(after.handover == handoverId && walked > 3.0f);

    // Scenes (v9). Talking: CJ turns to the speaker due east.
    run(1.0, [] { return false; });
    scene.flags = Protocol::kInputFace;
    scene.sceneHeading = Pose::kPi / 2.0f;
    run(1.5, [] { return false; });
    Protocol::PlayerState facing{};
    link.ReadPlayer(facing);
    std::printf("scenes: talking, CJ turned to heading %.2f (the speaker at %.2f)\n", facing.heading, scene.sceneHeading);
    failed += !(std::abs(facing.heading - scene.sceneHeading) < 0.1f);
    scene.flags = 0;

    // Shouting: CJ's gesture moves his arms while he stands.
    Protocol::BodyState still{}, gesture{};
    run(1.0, [&] { return false; });
    link.ReadBody(still);
    scene.shoutCount++;
    float gestured = 0.0f;
    run(1.5, [&] {
        if (link.ReadBody(gesture) && gesture.boneCount == still.boneCount) {
            for (std::uint32_t i = 0; i < gesture.boneCount; i++) {
                for (int k = 0; k < 3; k++) {
                    gestured = (std::max)(gestured, std::abs(gesture.bones[i].translation[k] - still.bones[i].translation[k]));
                }
            }
        }
        return false;
    });
    std::printf("scenes: shouting, a bone moved up to %.2f m from standing\n", gestured * Pose::kMetersPerUnit);
    failed += !(gestured * Pose::kMetersPerUnit > 0.05f);

    // Swearing (v16): the finger moves his arm too.
    run(1.5, [&] { return false; });
    link.ReadBody(still);
    input.AddInsult();
    float fingered = 0.0f;
    run(1.5, [&] {
        if (link.ReadBody(gesture) && gesture.boneCount == still.boneCount) {
            for (std::uint32_t i = 0; i < gesture.boneCount; i++) {
                for (int k = 0; k < 3; k++) {
                    fingered = (std::max)(fingered, std::abs(gesture.bones[i].translation[k] - still.bones[i].translation[k]));
                }
            }
        }
        return false;
    });
    std::printf("scenes: swearing, a bone moved up to %.2f m from standing\n", fingered * Pose::kMetersPerUnit);
    failed += !(fingered * Pose::kMetersPerUnit > 0.05f);

    // Following Skyrim's player in a scene: it walks east at 1.5 m/s, then sits, then CJ has it back.
    Protocol::PlayerState from{};
    link.ReadPlayer(from);
    scene.flags = Protocol::kInputScripted;
    scene.sceneHeading = Pose::kPi / 2.0f;
    for (int i = 0; i < 3; ++i) {
        scene.sceneOffset[i] = from.offset[i];
    }
    const std::uint64_t walkFrom = ms();
    run(3.0, [&] {
        scene.sceneOffset[0] = from.offset[0] + 1.5f * Pose::kUnitsPerMeter * static_cast<float>(ms() - walkFrom) / 1000.0f;
        return false;
    });
    Protocol::PlayerState led{};
    link.ReadPlayer(led);
    const float behind = std::hypot(led.offset[0] - scene.sceneOffset[0], led.offset[1] - scene.sceneOffset[1]) * Pose::kMetersPerUnit;
    std::printf("scenes: following, CJ %.2f m from Skyrim's player after %.1f m, heading %.2f\n", behind,
        (scene.sceneOffset[0] - from.offset[0]) * Pose::kMetersPerUnit, led.heading);
    failed += !(behind < 0.3f && std::abs(led.heading - scene.sceneHeading) < 0.1f);

    scene.flags = Protocol::kInputScripted | Protocol::kInputSeated;
    const float floorZ = scene.sceneOffset[2];
    scene.sceneOffset[2] = floorZ + 0.45f * Pose::kUnitsPerMeter;  // a chair's seat
    run(2.0, [] { return false; });
    Protocol::BodyState seated{};
    float seatLow = 0.0f, seatHigh = 0.0f;
    const bool seatSkinned = link.ReadBody(seated) && Extent(received, seated, seatLow, seatHigh);
    // And over the seat: his body's middle (where his hips are), not half a metre behind it.
    float seatOff = 1e9f;
    if (const auto model = seatSkinned ? received.Model(seated.model) : std::nullopt) {
        std::vector<Body::Matrix34> bones;
        for (std::uint32_t i = 0; i < seated.boneCount; i++) {
            bones.push_back(Body::ToMatrix(seated.bones[i]));
        }
        double sx = 0.0, sy = 0.0;
        std::size_t n = 0;
        for (const auto& mesh : model->meshes) {
            for (std::uint32_t i = 0; i < mesh.vertexCount; i++, n++) {
                Body::Vec3 p, nrm;
                Body::Skin(bones.data(), seated.boneCount, mesh.Vertex(i), p, nrm);
                sx += p.x;
                sy += p.y;
            }
        }
        if (n) {
            seatOff = std::hypot(seated.feet[0] + static_cast<float>(sx / n) - scene.sceneOffset[0], seated.feet[1] + static_cast<float>(sy / n) - scene.sceneOffset[1]) *
                      Pose::kMetersPerUnit;
        }
    }
    const float soles = (seated.feet[2] + seatLow - floorZ) * Pose::kMetersPerUnit;
    std::printf("scenes: seated, CJ %.2f m tall, his middle %.2f m from the seat, his soles %.2f m over the floor\n", (seatHigh - seatLow) * Pose::kMetersPerUnit,
        seatOff, soles);
    failed += !(seatSkinned && (seatHigh - seatLow) * Pose::kMetersPerUnit < 1.5f && seatOff < 0.25f && std::abs(soles) < 0.25f);

    scene.flags = 0;
    Protocol::PlayerState freed{}, walkedOn{};
    run(1.0, [] { return false; });
    link.ReadPlayer(freed);
    input.SetKey(Input::kDikW, true);
    run(1.5, [] { return false; });
    input.SetKey(Input::kDikW, false);
    link.ReadPlayer(walkedOn);
    const float onFoot = std::hypot(walkedOn.offset[0] - freed.offset[0], walkedOn.offset[1] - freed.offset[1]) * Pose::kMetersPerUnit;
    std::printf("scenes: CJ has the player back and walked %.1f m%s\n", onFoot, (walkedOn.flags & Protocol::kPlayerGrounded) ? ", standing" : ", in the air");
    failed += !(onFoot > 1.0f && (walkedOn.flags & Protocol::kPlayerGrounded));

    // Past 1 km in the Sandking: San Andreas moves its floating origin under CJ, and he must stay
    // in the car, on the ground, without a jump in where Skyrim puts him. Flat ground is streamed
    // around him as he goes, as Skyrim's sampler does.
    handoverId = 5;
    anchor[0] = kAnchor[0] - 40000.0f;
    anchor[1] = kAnchor[1];
    anchor[2] = kAnchor[2];
    publish(true);
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
    run(1.5, [&] { stream(); return false; });
    press(Input::kDikRightBracket);
    Protocol::BodyState far{};
    const bool farSpawned = run(10.0, [&] { stream(); return vehicleIn(0, far); });
    run(1.0, [&] { stream(); return false; });
    press(Input::kDikF);
    const bool farIn = farSpawned && run(8.0, [&] { stream(); return inVehicle(); });
    run(2.0, [&] { stream(); return false; });
    Protocol::PlayerState last{};
    float                 biggestJump = 0.0f, lowest = 0.0f, highest = 0.0f, reached = 0.0f;
    bool                  leftCar = false;
    input.SetKey(Input::kDikW, true);
    run(90.0, [&] {
        stream();
        Protocol::PlayerState p{};
        if (!link.ReadPlayer(p) || p.handover != handoverId || p.frame == last.frame) {
            return false;
        }
        if (last.frame) {
            biggestJump = (std::max)(biggestJump, std::hypot(p.offset[0] - last.offset[0], p.offset[1] - last.offset[1], p.offset[2] - last.offset[2]));
        }
        last = p;
        lowest = (std::min)(lowest, p.offset[2]);
        highest = (std::max)(highest, p.offset[2]);
        leftCar |= !(p.flags & Protocol::kPlayerInVehicle);
        reached = std::hypot(p.offset[0], p.offset[1]) * Pose::kMetersPerUnit;
        return reached > 1300.0f || leftCar;
    });
    input.SetKey(Input::kDikW, false);
    std::printf("vehicles: drove %.0f m from the handover spot (the origin moves at 1 km), %s, height %.1f..%.1f m, biggest step %.1f m\n", reached,
        leftCar ? "CJ LEFT THE CAR" : "CJ stayed in", lowest * Pose::kMetersPerUnit, highest * Pose::kMetersPerUnit, biggestJump * Pose::kMetersPerUnit);
    failed += !(farIn && reached > 1300.0f && !leftCar && lowest * Pose::kMetersPerUnit > -3.0f && biggestJump * Pose::kMetersPerUnit < 5.0f);

    publish(false);
    link.Tick(ms());
    return failed ? 2 : 0;
}
