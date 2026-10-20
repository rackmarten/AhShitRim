// Stands in for the Skyrim plugin, to drive a running San Andreas without Skyrim: links up as the
// Skyrim side, hands the player over twice, sends flat ground (7 x 7 collision chunks) around the
// handover spot, holds W for a few seconds and prints CJ's poses. Also receives CJ's model, skins
// it with the skeleton San Andreas sends and checks he stands about as tall as a man, on his feet,
// and that his bones move while he walks. Built with the protocol tests;
// run it through scripts/fake-skyrim.ps1 while San Andreas runs (launch-sa.ps1).
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
#include "../cpp/link.h"
#include "../cpp/pose.h"
#include "../cpp/skin_model.h"

using namespace AhShitRim;
using namespace std::chrono;

// Somewhere in Skyrim's Tamriel, away from the chunk grid's lines.
constexpr float kAnchor[3] = { 10000.0f, -20000.0f, 3000.0f };

struct Received
{
    SkinModel::Assembler      assembler;
    std::vector<std::uint8_t> blob;
    std::uint32_t             id = 0;
    std::optional<SkinModel::Model> model;
};

void Drain(Link& a_link, Received& a_received)
{
    a_link.Receive([&](Protocol::MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
        if (a_type != Protocol::MessageType::ModelChunk || a_received.assembler.Add(a_data, a_size) != SkinModel::Assembler::Result::Complete) {
            return;
        }
        a_received.blob = a_received.assembler.Blob();
        a_received.id = a_received.assembler.Id();
        a_received.model = SkinModel::Parse(a_received.blob.data(), a_received.blob.size());
        if (!a_received.model) {
            std::printf("model %08X: malformed (%zu bytes)\n", a_received.id, a_received.blob.size());
            return;
        }
        std::uint32_t vertices = 0, triangles = 0;
        for (const auto& mesh : a_received.model->meshes) {
            vertices += mesh.vertexCount;
            triangles += mesh.indexCount / 3;
        }
        std::printf("model %08X %s: %zu meshes, %u vertices, %u triangles, %zu textures, %zu bones (%zu bytes)\n", a_received.id,
            a_received.model->name.c_str(), a_received.model->meshes.size(), vertices, triangles, a_received.model->textures.size(),
            a_received.model->boneIds.size(), a_received.blob.size());
    });
}

// CJ's skinned extent relative to his feet, units: lowest and highest vertex.
bool Extent(const Received& a_received, const Protocol::BodyState& a_body, float& a_low, float& a_high)
{
    if (!a_received.model || a_body.model != a_received.id || a_body.boneCount != a_received.model->boneIds.size()) {
        return false;
    }
    std::vector<Body::Matrix34> bones;
    for (std::uint32_t i = 0; i < a_body.boneCount; i++) {
        bones.push_back(Body::ToMatrix(a_body.bones[i]));
    }
    a_low = 1e9f;
    a_high = -1e9f;
    for (const auto& mesh : a_received.model->meshes) {
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
    const auto     publish = [&](bool a_owned) {
        auto s = input.Build(++frame, a_owned, true);
        s.handover = handoverId;
        s.heading = 0.0f;
        s.anchor[0] = anchor[0];
        s.anchor[1] = anchor[1];
        s.anchor[2] = anchor[2];
        link.WriteInput(s);
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
    return failed ? 2 : 0;
}
