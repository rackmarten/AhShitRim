#include <doctest/doctest.h>

#include "../../cpp/fx_model.h"
#include "../../cpp/txd.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace AhShitRim;
using namespace AhShitRim::FxModel;

namespace
{
    int Shown(const Protocol::BodyState& a_pose)
    {
        int shown = 0;
        for (int b = 0; b < kBones; ++b) {
            shown += a_pose.bones[b].translation[2] > kHidden / 2.0f ? 1 : 0;
        }
        return shown;
    }
}

TEST_CASE("fx model: curves of up to six keys, flat past the ends")
{
    constexpr Curve flat = 0.7f;
    CHECK(flat.At(-1.0f) == doctest::Approx(0.7f));
    CHECK(flat.At(5.0f) == doctest::Approx(0.7f));
    CHECK(kJetFlame.rate.At(0.0f) == doctest::Approx(100.0f));
    CHECK(kJetFlame.rate.At(0.1f) == doctest::Approx(75.0f));
    CHECK(kJetFlame.rate.At(0.35f) == doctest::Approx(50.0f));
    CHECK(kJetFlame.rate.At(0.7f) == doctest::Approx(0.0f));
    CHECK(kJetFlame.rate.At(1.0f) == doctest::Approx(0.0f));
    CHECK(Curve{}.At(0.5f) == 0.0f);
}

TEST_CASE("fx model: the jetpack's flames grow with thrust, orange turning blue")
{
    // At rest only the orange flame; at full thrust only the blue, at twice the rate.
    CHECK(kJetFlame.rate.At(0.0f) > 0.0f);
    CHECK(kJetGas.rate.At(0.0f) == 0.0f);
    CHECK(kJetFlame.rate.At(1.0f) == 0.0f);
    CHECK(kJetGas.rate.At(1.0f) == doctest::Approx(100.0f));
    CHECK(kJetGas.speed.At(1.0f) > kJetGas.speed.At(0.0f));
    // A tenth a frame each way, held in 0..1.
    float level = 0.0f;
    for (int frame = 0; frame < 5; ++frame) {
        level = JetpackLevel(level, true);
    }
    CHECK(level == doctest::Approx(0.5f));
    for (int frame = 0; frame < 20; ++frame) {
        level = JetpackLevel(level, true);
    }
    CHECK(level == doctest::Approx(1.0f));
    CHECK(JetpackLevel(0.05f, false) == 0.0f);
    CHECK(FromByte(ToByte(level)) == doctest::Approx(1.0f));
    CHECK(ToByte(2.0f) == 255);
    CHECK(ToByte(-1.0f) == 0);
}

TEST_CASE("fx model: a flame is seen in the first fifth of its life only")
{
    CHECK(VisibleFrom(kJetFlame) == 0.0f);
    CHECK(VisibleTo(kJetFlame) == doctest::Approx(0.2f));
    CHECK(VisibleTo(kNitroFlame) == doctest::Approx(1.0f));
    // The dust can't be seen for its first half.
    CHECK(VisibleFrom(kRotorDust) == doctest::Approx(0.5f));
    CHECK(VisibleTo(kRotorDust) == doctest::Approx(1.0f));
    CHECK(AgeBucket(kRotorDust, 0.3f) == -1);
    CHECK(AgeBucket(kRotorDust, 0.5f) == 0);
    CHECK(AgeBucket(kRotorDust, 0.99f) == kAges - 1);
    CHECK(AgeBucket(kJetFlame, 0.19f) == kAges - 1);
    CHECK(BucketMiddle(kRotorDust, 0) > 0.5f);
}

TEST_CASE("fx model: each age a mesh of its own colour, alpha and size, San Andreas's multipliers baked in")
{
    const SkinModel::TextureIn sprite{ 4, 4, std::vector<std::uint8_t>(4 * 4 * 4, 255) };
    const auto                 gas = Build(kJetGas, sprite);
    REQUIRE(gas.meshes.size() == static_cast<std::size_t>(kAges));
    CHECK(gas.boneIds.size() == static_cast<std::size_t>(kBones));
    CHECK(gas.meshes[kAges - 1].color[2] == 255);  // blue
    CHECK(gas.meshes[0].color[3] > gas.meshes[kAges - 1].color[3]);
    CHECK(gas.meshes[0].vertices[1].position[0] < gas.meshes[kAges - 1].vertices[1].position[0]);

    const auto smoke = Build(kExhaust, sprite, kExhaustMult);
    CHECK((smoke.meshes[0].flags & Protocol::kSkinMeshAdditive) == 0);
    CHECK(smoke.meshes[1].color[0] == 115);  // 128 tinted 0.9
    CHECK(smoke.meshes[1].color[2] == 128);
    int brightest = 0;
    for (const auto& mesh : smoke.meshes) {
        brightest = (std::max)(brightest, static_cast<int>(mesh.color[3]));
    }
    CHECK(brightest <= 64);  // a quarter of 255
    CHECK(SizeOf(kExhaust, kAges - 1, kExhaustMult) < SizeOf(kExhaust, kAges - 1));

    CHECK((Build(kNitroFlame, sprite).meshes[0].flags & Protocol::kSkinMeshAdditive) != 0);
    CHECK(SizeOf(kTankFlash, 3, kFlashMult) == doctest::Approx(0.5f * 5.0f * kFlashRect * kMeter));
}

TEST_CASE("fx model: a sprite's way strays between the prim's angles")
{
    const Vec3 down{ 0.0f, 0.0f, -1.0f };
    for (int i = 0; i < 40; ++i) {
        const Vec3 dir = Spread(down, 10.0f, 10.0f, SmokeModel::Hash(i * 2u), SmokeModel::Hash(i * 2u + 1u));
        CHECK(Body::Dot(dir, down) == doctest::Approx(std::cos(10.0f * 0.0174532925f)).epsilon(1e-3));
        const Vec3 flat = Spread({ 0.0f, 0.0f, 1.0f }, 90.0f, 90.0f, SmokeModel::Hash(i * 2u), SmokeModel::Hash(i * 2u + 1u));
        CHECK(std::fabs(flat.z) < 1e-3f);  // the rotor dust goes out along the ground
    }
}

TEST_CASE("fx model: the jetpack's flames come out of both jets, downwards, and go along with them")
{
    Emitter      flame(kJetFlame);
    const Source jets[2] = { { { -10.0f, 0.0f, 100.0f }, { 0.0f, 0.0f, -1.0f } }, { { 10.0f, 0.0f, 100.0f }, { 0.0f, 0.0f, -1.0f } } };
    for (int i = 0; i < 6; ++i) {
        flame.Move(0.02f);
        flame.Emit(0.02f, 0.0f, jets);
    }
    // 0.12 s at 100 a second, the first at once, none older than the 0.14 s a flame is seen for.
    CHECK(flame.Sprites().size() >= 12);
    CHECK(flame.Sprites().size() <= 14);
    int left = 0;
    for (const auto& sprite : flame.Sprites()) {
        CHECK(sprite.at.z <= 100.0f);  // below the jet ball
        left += sprite.at.x < 0.0f ? 1 : 0;
    }
    CHECK(left * 2 == doctest::Approx(static_cast<double>(flame.Sprites().size())).epsilon(0.15));
    // Moving with CJ: every sprite shifts as far as the jets did.
    const float before = flame.Sprites().back().at.y;
    flame.Move(0.0f, { 0.0f, 50.0f, 0.0f });
    CHECK(flame.Sprites().back().at.y == doctest::Approx(before + 50.0f));
    // Gone 0.14 s after the last one: past the fifth of its 0.7 s it can be seen in.
    for (int i = 0; i < 8; ++i) {
        flame.Move(0.02f);
    }
    CHECK(flame.Empty());
}

TEST_CASE("fx model: the pose draws the newest of each age and hides the rest")
{
    Emitter      dust(kRotorDust);
    const Source ground[1] = { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };
    for (int i = 0; i < 100; ++i) {
        dust.Move(0.02f);
        dust.Emit(0.02f, 1.0f, ground);
    }
    Protocol::BodyState pose{};
    dust.Pose({ 0.0f, 0.0f, 0.0f }, { 0.0f, -2000.0f, 300.0f }, pose);
    CHECK(pose.boneCount == static_cast<std::uint32_t>(kBones));
    const int shown = Shown(pose);
    CHECK(shown > 0);
    CHECK(shown <= kBones);
    // The young sprites, unseen for half their life, aren't drawn.
    int young = 0;
    for (const auto& sprite : dust.Sprites()) {
        young += sprite.age / sprite.life < 0.5f ? 1 : 0;
        CHECK(std::fabs(sprite.at.z) < 1.0f);  // flat along the ground
    }
    CHECK(young > 0);
    CHECK(shown <= static_cast<int>(dust.Sprites().size()) - young);
    // Friction slowed the oldest from 25 m/s.
    const auto& oldest = dust.Sprites().front().speed;
    CHECK(std::sqrt(Body::Dot(oldest, oldest)) < 15.0f * kMeter);
}

TEST_CASE("fx model: the nitro burns half to full with the gas, then its cooler puffs as it charges")
{
    CHECK(NitroLevel(1.0f, 1.0f) == 0.0f);               // ready, not burning
    CHECK(NitroLevel(-0.5f, 0.0f) == doctest::Approx(0.5f));
    CHECK(NitroLevel(-0.5f, 1.0f) == doctest::Approx(1.0f));
    CHECK(NitroLevel(-0.5f, -1.0f) == doctest::Approx(0.5f));  // braking
    CHECK(NitroLevel(0.0f, 1.0f) == doctest::Approx(0.5f));    // just burnt out
    CHECK(NitroLevel(0.2f, 1.0f) == doctest::Approx(0.4f));    // charging: the cooler's level
    CHECK(kNitroCooler.rate.At(NitroLevel(0.2f, 0.0f)) == doctest::Approx(30.0f));
    CHECK(kNitroFlame.rate.At(NitroLevel(0.2f, 0.0f)) == 0.0f);
    CHECK(kNitroFlame.rate.At(NitroLevel(-0.5f, 1.0f)) == doctest::Approx(100.0f));
    CHECK(kNitroCore.rate.At(NitroLevel(-0.5f, 0.0f)) < kNitroCore.rate.At(NitroLevel(-0.5f, 1.0f)));
    CHECK(GasFromByte(GasToByte(-1.0f)) == doctest::Approx(-1.0f));
    CHECK(GasFromByte(GasToByte(0.5f)) == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("fx model: exhaust smoke as AddExhaustParticles puffs it")
{
    // A random 1..3 times (gas + 1.1) over 2.5.
    CHECK(ExhaustChance(0.0f) == doctest::Approx((3.0f - 2.5f / 1.1f) / 2.0f));
    CHECK(ExhaustChance(1.0f) == doctest::Approx((3.0f - 2.5f / 2.1f) / 2.0f));
    CHECK(ExhaustChance(-1.0f) == 0.0f);
    CHECK(ExhaustChance(-2.0f) == 0.0f);
    // An idling car puffs about 22 sprites a second from each exhaust; revving in first gear more.
    CHECK(ExhaustRate(0.0f, false) == doctest::Approx(21.8f).epsilon(0.01));
    CHECK(ExhaustRate(1.0f, true) > ExhaustRate(1.0f, false));
    CHECK(ExhaustRate(0.3f, true) == doctest::Approx(ExhaustRate(0.3f, false)));
    // Fainter and shorter with speed, gone at 25 and 20 m/s.
    CHECK(ExhaustAlpha(0.0f) == doctest::Approx(0.25f));
    CHECK(ExhaustScale(0.0f) == doctest::Approx(1.0f));
    CHECK(ExhaustScale(12.5f) == doctest::Approx(0.5f));
    CHECK(ExhaustAlpha(30.0f) == 0.0f);
    CHECK(ExhaustLife(0.0f) == doctest::Approx(0.2f));
    CHECK(ExhaustLife(20.0f) == 0.0f);
    // At rest out of the back; driving forward along with the car at 0.6 of its speed.
    const Vec3 north{ 0.0f, 1.0f, 0.0f };
    const Vec3 still = ExhaustVelocity({}, north);
    CHECK(still.y == doctest::Approx(-1.35f * kMeter));
    const Vec3 slow = ExhaustVelocity({ 0.0f, 2.0f * kMeter, 0.0f }, north);
    CHECK(slow.y < 0.0f);
    const Vec3 driving = ExhaustVelocity({ 0.0f, 10.0f * kMeter, 0.0f }, north);
    CHECK(driving.y == doctest::Approx(6.0f * kMeter));
    const Vec3 reversing = ExhaustVelocity({ 0.0f, -10.0f * kMeter, 0.0f }, north);
    CHECK(reversing.y == doctest::Approx(-1.35f * kMeter));
}

TEST_CASE("fx model: added sprites live their scaled life, the exhaust's rising late")
{
    Emitter smoke(kExhaust);
    smoke.Add({ 0.0f, 0.0f, 50.0f }, { 0.0f, -20.0f, 0.0f }, 1.0f);
    smoke.Add({ 0.0f, 0.0f, 50.0f }, {}, 0.0f);  // no life: never added
    CHECK(smoke.Sprites().size() == 1);
    smoke.Move(0.25f);
    CHECK(smoke.Sprites()[0].at.z == doctest::Approx(50.0f));  // no force before 0.3 of its life
    for (int i = 0; i < 2; ++i) {
        smoke.Move(0.2f);
    }
    CHECK(smoke.Sprites()[0].speed.z > 0.0f);
    for (int i = 0; i < 3; ++i) {
        smoke.Move(0.2f);
    }
    CHECK(smoke.Empty());
}

TEST_CASE("fx model: tank_fire's smoke comes out along the barrel for 0.3 s and rises")
{
    Emitter      smoke(kTankSmoke);
    const Source muzzle[1] = { { { 0.0f, 0.0f, 200.0f }, { 0.0f, 1.0f, 0.0f } } };
    float        t = 0.0f;
    for (; t < kOneOffLength; t += 0.02f) {
        smoke.Move(0.02f);
        smoke.Emit(0.02f, t, muzzle);
    }
    // 100 a second falling to 50 over 0.3 s: about 22.
    CHECK(smoke.Sprites().size() >= 18);
    CHECK(smoke.Sprites().size() <= 28);
    for (const auto& sprite : smoke.Sprites()) {
        CHECK(sprite.at.y >= 0.0f);  // out of the barrel
    }
    for (int i = 0; i < 50; ++i) {
        smoke.Move(0.02f);
    }
    for (const auto& sprite : smoke.Sprites()) {
        CHECK(sprite.speed.z > 0.0f);  // slowed, and rising
    }
    Emitter flash(kTankFlash);
    flash.Emit(0.05f, 0.05f, muzzle);
    CHECK_FALSE(flash.Empty());
    flash.Move(0.05f);
    CHECK(flash.Empty());  // 0.02 s each
    flash.Emit(0.05f, 0.2f, muzzle);
    CHECK(flash.Empty());  // only the first 0.1 s
}

// The player's own files, never in the repository. Without AHSR_SA_PATH this checks nothing.
TEST_CASE("The San Andreas install's effectsPC.txd has the particles' sprites")
{
    const char* sa = std::getenv("AHSR_SA_PATH");
    if (!sa || !*sa) {
        MESSAGE("AHSR_SA_PATH not set, skipped");
        return;
    }
    std::ifstream file(std::filesystem::path(sa) / "models/effectsPC.txd", std::ios::binary);
    REQUIRE(file);
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto                      textures = Txd::Read(data.data(), data.size());
    for (const auto* prim : { &kJetFlame, &kJetGas, &kNitroCooler, &kNitroFlame, &kNitroCore, &kExhaust, &kRotorDust, &kTankSmoke, &kTankFlash, &kGunFlash }) {
        INFO(prim->texture);
        CHECK(Txd::Find(textures, prim->texture));
    }
}

TEST_CASE("fx model: an idling car's two exhausts puff in turn, out of the back")
{
    Emitter      smoke(kExhaust);
    const Source pipes[2] = { { { -30.0f, -150.0f, 20.0f }, { 0.0f, -1.0f, 0.0f } }, { { 30.0f, -150.0f, 20.0f }, { 0.0f, -1.0f, 0.0f } } };
    const Vec3   forward{ 0.0f, 1.0f, 0.0f };
    const float  rate = ExhaustRate(0.0f, false) * ExhaustScale(0.0f) * 2.0f;
    for (int i = 0; i < 25; ++i) {
        smoke.Move(0.02f);
        smoke.Puff(0.02f, rate, pipes, ExhaustVelocity({}, forward), ExhaustLife(0.0f));
    }
    // 0.5 s at about 44 a second, none gone yet (they last 0.6 s at least).
    CHECK(smoke.Sprites().size() >= 20);
    CHECK(smoke.Sprites().size() <= 25);
    int left = 0;
    for (const auto& sprite : smoke.Sprites()) {
        CHECK(sprite.at.y <= -150.0f);  // behind the car
        CHECK(sprite.life >= 0.6f - 1e-4f);
        CHECK(sprite.life <= 1.4f + 1e-4f);
        left += sprite.at.x < 0.0f ? 1 : 0;
    }
    CHECK(left >= 10);
    CHECK(left <= 13);
    // Fast enough, none at all.
    Emitter fast(kExhaust);
    fast.Puff(0.5f, ExhaustRate(1.0f, false) * ExhaustScale(30.0f), pipes, {}, ExhaustLife(30.0f));
    CHECK(fast.Empty());
}
