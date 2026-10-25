// Stands in for the Skyrim plugin to check CJ's weapons (v12) against a running San Andreas: hands
// the player over on flat ground, gives the weapon sets the way the debug key does, then switches
// through every weapon with the mouse wheel, aims and fires each one, and reports what San Andreas
// publishes: the loadout, the weapon in his hand and its model and slots, the shots counted and the
// camera mode, and (v13) the Shot messages each one sends: bullets, shotgun shots, melee blows.
// Also sets a loadout the way a loaded save does and checks it came back as sent, skills and all, and
// sends Hit commands and checks they raise the skill.
// AHSHITRIM_SITUATIONS_ONLY=1 instead plays through the situations CJ talks in on his own (v29: punches,
// bursts, aiming, a stealth kill, gold, a shop, a chase, 95 s idle for a song) for San Andreas's log
// (scripts/logs.ps1) to show which lines he said and which he kept back.
// Exits non-zero when a weapon doesn't show up in the hand, doesn't switch, doesn't fire or sends no shot.
// Built with the protocol tests; run it through scripts/fake-weapons.ps1 while San Andreas runs
// (launch-sa.ps1) and Skyrim is closed.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "../cpp/body.h"
#include "../cpp/collision.h"
#include "../cpp/input.h"
#include "../cpp/link.h"
#include "../cpp/pose.h"
#include "../cpp/skin_model.h"
#include "../cpp/weapons.h"

using namespace AhShitRim;
using namespace std::chrono;

constexpr float kAnchor[3] = { 10000.0f, -20000.0f, 3000.0f };

struct ShotCount
{
    unsigned                bullets = 0;
    unsigned                spread = 0;  // shotgun shots
    unsigned                blows = 0;
    std::set<std::uint32_t> strikes;
    Protocol::ShotEvent     last{};
};

struct Received
{
    std::map<std::uint8_t, ShotCount>                   shots;  // by weapon type
    SkinModel::Assembler                                assembler;
    std::map<std::uint32_t, std::vector<std::uint8_t>> blobs;
    std::map<std::uint32_t, std::string>                names;

    std::optional<SkinModel::Model> Model(std::uint32_t a_id) const
    {
        const auto found = blobs.find(a_id);
        return found != blobs.end() ? SkinModel::Parse(found->second.data(), found->second.size()) : std::nullopt;
    }
};

void Drain(Link& a_link, Received& a_received)
{
    a_link.Receive([&](Protocol::MessageType a_type, const std::uint8_t* a_data, std::uint32_t a_size) {
        if (a_type == Protocol::MessageType::Shot && a_size == sizeof(Protocol::ShotEvent)) {
            Protocol::ShotEvent shot{};
            std::memcpy(&shot, a_data, sizeof(shot));
            auto& count = a_received.shots[shot.weaponType];
            if (shot.kind == Protocol::kShotMelee) {
                count.blows++;
                count.strikes.insert(shot.strike);
            } else if (shot.flags & Protocol::kShotSpread) {
                count.spread++;
            } else {
                count.bullets++;
            }
            count.last = shot;
            return;
        }
        if (a_type != Protocol::MessageType::ModelChunk || a_received.assembler.Add(a_data, a_size) != SkinModel::Assembler::Result::Complete) {
            return;
        }
        const auto  id = a_received.assembler.Id();
        const auto& blob = a_received.blobs[id] = a_received.assembler.Blob();
        if (const auto model = SkinModel::Parse(blob.data(), blob.size())) {
            a_received.names[id] = model->name;
        }
    });
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
    const std::uint32_t handoverId = 1;
    const auto     publish = [&] {
        auto s = input.Build(++frame, true, true);
        s.handover = handoverId;
        s.anchor[0] = kAnchor[0];
        s.anchor[1] = kAnchor[1];
        s.anchor[2] = kAnchor[2];
        link.WriteInput(s);
        Protocol::AudioState audio{};
        link.WriteAudio(audio);
    };
    const auto run = [&](double a_seconds) {
        const std::uint64_t until = ms() + static_cast<std::uint64_t>(a_seconds * 1000);
        while (ms() < until) {
            publish();
            link.Tick(ms());
            Drain(link, received);
            std::this_thread::sleep_for(16ms);
        }
    };

    publish();
    {
        const auto cx = Collision::ChunkIndex(kAnchor[0]), cy = Collision::ChunkIndex(kAnchor[1]);
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                Collision::Heightfield f;
                f.Reset(cx + dx, cy + dy, 32);
                std::fill(f.heights.begin(), f.heights.end(), kAnchor[2]);
                std::fill(f.hit.begin(), f.hit.end(), std::uint8_t{ 1 });
                const auto payload = Collision::BuildChunkPayload(f, 0x3C, handoverId);
                link.Send(Protocol::MessageType::CollisionChunk, payload.data(), static_cast<std::uint32_t>(payload.size()));
            }
        }
    }
    run(3.0);

    int failed = 0;
    const auto pose = [&] {
        Protocol::PlayerState p{};
        link.ReadPlayer(p);
        return p;
    };
    const auto command = [&](Protocol::WeaponCommandKind a_kind, std::uint32_t a_argument, const Protocol::WeaponEntry* a_entries = nullptr) {
        Protocol::WeaponCommand c{};
        c.kind = static_cast<std::uint32_t>(a_kind);
        c.argument = a_argument;
        if (a_entries) {
            std::memcpy(c.entries, a_entries, sizeof(c.entries));
        }
        link.Send(Protocol::MessageType::WeaponCommand, &c, sizeof(c));
    };
    const auto loadoutText = [&](const Protocol::PlayerState& p) {
        std::string text;
        for (std::uint32_t i = 0; i < Protocol::kWeaponSlotCount; ++i) {
            if (p.loadout[i].type) {
                text += " [" + std::to_string(i) + "]=" + std::to_string(p.loadout[i].type) + ":" + std::to_string(p.loadout[i].clip) + "/" +
                        std::to_string(p.loadout[i].ammo);
            }
        }
        return text.empty() ? std::string(" empty") : text;
    };

    // One weapon in the hand: what it is, whether its model arrived, and where the weapon is.
    const auto report = [&](const char* a_what) {
        const auto p = pose();
        Protocol::BodyState hand{}, left{};
        const bool haveHand = link.ReadWeapon(0, hand) && (hand.flags & Protocol::kBodyValid) != 0;
        const bool haveLeft = link.ReadWeapon(1, left) && (left.flags & Protocol::kBodyValid) != 0;
        const auto model = haveHand ? received.Model(hand.model) : std::nullopt;
        float      height = -999.0f;
        if (haveHand && hand.boneCount > 0) {
            height = hand.bones[0].translation[2] * Pose::kMetersPerUnit;
        }
        std::printf("%-22s type %2u slot %2u flags 0x%02X shots %5u cam 0x%02X | hand slot %s model %s%s%s bones %u at %.2f m up%s | muzzle (%.2f, %.2f, %.2f) m\n", a_what,
            p.weaponType, p.weaponSlot, p.weaponFlags, p.shots, p.cameraMode, haveHand ? "valid" : "empty", haveHand ? "" : "-",
            model ? model->name.c_str() : (haveHand ? "NOT RECEIVED" : ""), haveLeft ? " + left hand" : "", hand.boneCount, height,
            "", p.muzzle[0][0] * Pose::kMetersPerUnit, p.muzzle[0][1] * Pose::kMetersPerUnit, p.muzzle[0][2] * Pose::kMetersPerUnit);
        return std::make_tuple(p, haveHand, model.has_value(), height);
    };

    // AHSHITRIM_AIM_ONLY=1: only how one-handed guns point while aiming, at each skill level (the gangster
    // and hitman styles hold them differently), then quit.
    if (const char* only = std::getenv("AHSHITRIM_AIM_ONLY"); only && *only == '1') {
        for (const std::uint8_t type : { Weapons::Pistol, Weapons::MicroUzi, Weapons::DesertEagle }) {
            for (const std::uint16_t skill : { std::uint16_t{ 0 }, std::uint16_t{ 500 }, std::uint16_t{ 999 } }) {
                Protocol::WeaponCommand c{};
                c.kind = static_cast<std::uint32_t>(Protocol::WeaponCommandKind::SetLoadout);
                const std::uint32_t slot = type == Weapons::MicroUzi ? 4 : 2;
                c.argument = slot;
                c.entries[slot] = { type, 0, 10, 200 };
                for (auto& s : c.skills) {
                    s = skill;
                }
                link.Send(Protocol::MessageType::WeaponCommand, &c, sizeof(c));
                run(1.5);
                input.SetAim(true);
                run(1.5);
                const auto p = pose();
                Protocol::BodyState hand{};
                link.ReadWeapon(0, hand);
                const float rise = p.muzzle[0][2] - hand.bones[0].translation[2];
                const float reach = std::hypot(p.muzzle[0][0] - hand.bones[0].translation[0], p.muzzle[0][1] - hand.bones[0].translation[1]);
                std::printf("type %2u skill %3u aiming: cam 0x%02X, hand %.2f m up, the gun points %.0f degrees up, flags 0x%02X%s\n", type, skill, p.cameraMode,
                    hand.bones[0].translation[2] * Pose::kMetersPerUnit, std::atan2(rise, reach) * 57.2958f, p.weaponFlags,
                    (p.weaponFlags & Protocol::kWeaponTwin) ? " (twin)" : "");
                input.SetAim(false);
                run(1.0);
                // Firing from the hip, no aim camera.
                input.SetFire(true);
                run(0.4);
                const auto f = pose();
                link.ReadWeapon(0, hand);
                const float riseF = f.muzzle[0][2] - hand.bones[0].translation[2];
                const float reachF = std::hypot(f.muzzle[0][0] - hand.bones[0].translation[0], f.muzzle[0][1] - hand.bones[0].translation[1]);
                std::printf("type %2u skill %3u from the hip: cam 0x%02X, hand %.2f m up, the gun points %.0f degrees up\n", type, skill, f.cameraMode,
                    hand.bones[0].translation[2] * Pose::kMetersPerUnit, std::atan2(riseF, reachF) * 57.2958f);
                input.SetFire(false);
                run(1.0);
            }
        }
        return 0;
    }

    // AHSHITRIM_SITUATIONS_ONLY=1 (v29): what may have CJ talk on his own, for San Andreas's log to show
    // which lines he says and which he keeps back (protocol/cpp/situations.h), then quit.
    if (const char* only = std::getenv("AHSHITRIM_SITUATIONS_ONLY"); only && *only == '1') {
        const auto step = [&](const char* a_what, double a_wait) {
            std::printf("situations: %s\n", a_what);
            run(a_wait);
        };
        Protocol::WeaponEntry one[Protocol::kWeaponSlotCount]{};  // a Hit's count
        one[0].ammo = 1;
        run(5.0);
        for (int i = 0; i < 6; ++i) {
            command(Protocol::WeaponCommandKind::Hit, 0, one);  // fists
            step("a punch lands", 4.5);
        }
        Protocol::WeaponEntry pistol[Protocol::kWeaponSlotCount]{};
        pistol[0].ammo = 200;
        command(Protocol::WeaponCommandKind::GiveWeapon, Weapons::Pistol, pistol);
        run(1.0);
        input.AddWheel(true);  // from fists to the pistol
        run(1.0);
        std::printf("situations: holding weapon %u\n", pose().weaponType);
        for (int burst = 0; burst < 4; ++burst) {
            command(Protocol::WeaponCommandKind::Hit, Weapons::Pistol, one);
            run(0.3);
            command(Protocol::WeaponCommandKind::Hit, Weapons::Pistol, one);
            step("a burst hits", 4.5);
        }
        input.SetAim(true);
        step("aiming the pistol", 2.0);
        input.SetAim(false);
        step("aim off", 4.5);
        input.AddStealthKill();
        step("a stealth kill", 4.5);
        for (int i = 0; i < 3; ++i) {
            input.AddCash();
            step("gold looted", 4.5);
        }
        input.AddShopBuy();
        step("bought in a shop", 4.5);
        input.AddShopLeave();
        step("left the shop", 4.5);
        input.SetHostileNear(true);
        input.SetKey(Input::kDikW, true);
        input.SetKey(Input::kDikLeftShift, true);
        step("sprinting with a hostile near", 3.0);
        input.SetKey(Input::kDikW, false);
        input.SetKey(Input::kDikLeftShift, false);
        input.SetHostileNear(false);
        step("standing still for 95 s: he should sing", 95.0);
        input.SetKey(Input::kDikW, true);
        step("walking off: the song stops", 2.0);
        input.SetKey(Input::kDikW, false);
        run(1.0);
        return 0;
    }

    // The sets, the way the debug key gives them.
    for (std::uint32_t set = 1; set <= 4; ++set) {
        command(Protocol::WeaponCommandKind::GiveSet, set);
        run(1.0);
        std::printf("set %u given:%s\n", set, loadoutText(pose()).c_str());
    }
    for (std::uint8_t melee : { Weapons::GolfClub, Weapons::Nightstick, Weapons::Katana, Weapons::Shovel, Weapons::PoolCue, Weapons::Dildo1, Weapons::Flowers,
             Weapons::Cane, Weapons::Chainsaw, Weapons::Knife, Weapons::BaseballBat }) {
        Protocol::WeaponEntry entries[Protocol::kWeaponSlotCount]{};
        entries[0].ammo = 1;
        command(Protocol::WeaponCommandKind::GiveWeapon, melee, entries);
        run(0.3);
    }

    // Through the wheel: every slot the loadout has, two steps up from fists at a time, until it comes round.
    std::set<std::uint32_t> seen;
    std::uint32_t           steps = 0;
    std::uint16_t           wheelUp = 0;
    for (int round = 0; round < 40; ++round) {
        run(0.7);
        const auto [p, haveHand, haveModel, height] = report("wheel");
        const std::uint32_t key = p.weaponType;
        if (!seen.insert(key).second) {
            break;
        }
        const bool melee = Weapons::IsMelee(p.weaponType);
        if (p.weaponType != 0 && !(haveHand && haveModel && height > 0.2f && height < 1.8f)) {
            std::printf("  ^ weapon %u is not in his hand or its model is missing\n", p.weaponType);
            failed++;
        }
        // Aim, then fire.
        const std::uint32_t shotsBefore = p.shots;
        const std::uint32_t ammoBefore = p.loadout[p.weaponSlot].ammo;
        input.SetAim(true);
        run(0.8);
        report("  aiming");
        input.SetFire(true);
        run(1.0);
        const auto [after, h2, m2, h3] = report("  firing");
        input.SetFire(false);
        run(0.3);
        input.SetAim(false);
        run(0.3);
        {
            const auto& count = received.shots[p.weaponType];
            const auto& last = count.last;
            std::printf("  ^ shots to Skyrim: %u bullets, %u shotgun shots, %u blows in %zu strikes; the last from (%.1f, %.1f, %.1f) m along (%.2f, %.2f, %.2f), "
                        "reach %.0f m, damage %.0f, skill %u\n",
                count.bullets, count.spread, count.blows, count.strikes.size(), last.origin[0] * Pose::kMetersPerUnit, last.origin[1] * Pose::kMetersPerUnit,
                last.origin[2] * Pose::kMetersPerUnit, last.direction[0], last.direction[1], last.direction[2], last.range * Pose::kMetersPerUnit, last.damage, last.skill);
            const bool gun = p.weaponType >= Weapons::Pistol && (p.weaponType <= Weapons::SniperRifle || p.weaponType == Weapons::Minigun);
            const bool none = count.bullets + count.spread + count.blows == 0;
            if ((gun || Weapons::IsMelee(p.weaponType)) && none) {
                std::printf("  ^ weapon %u sent no shot\n", p.weaponType);
                failed++;
            } else if (!none && (last.handover != handoverId || std::hypot(last.origin[0], last.origin[1]) * Pose::kMetersPerUnit > 4.0f)) {
                std::printf("  ^ weapon %u's shot isn't where CJ is\n", p.weaponType);
                failed++;
            }
        }
        const bool fired = after.shots != shotsBefore || after.loadout[after.weaponSlot].ammo < ammoBefore;
        std::printf("  ^ %s: shots %u -> %u, ammo %u -> %u%s\n", melee ? "melee" : "gun", shotsBefore, after.shots, ammoBefore, after.loadout[after.weaponSlot].ammo,
            fired ? "" : (melee || p.weaponType == 0 ? " (nothing to count)" : "  NO SHOT FIRED"));
        if (!melee && p.weaponType != 0 && Weapons::CountsAmmo(p.weaponType) && !fired) {
            failed++;
        }
        input.AddWheel(true);  // next
        steps++;
        wheelUp++;
    }
    std::printf("%u weapons went by on the wheel (%zu different)\n", steps, seen.size());
    failed += seen.size() < 10;

    // A loadout as a loaded save sets it, skills and all.
    Protocol::WeaponEntry entries[Protocol::kWeaponSlotCount]{};
    entries[0] = { Weapons::BrassKnuckle, 0, 1, 1 };
    entries[2] = { Weapons::DesertEagle, 0, 5, 44 };
    entries[5] = { Weapons::Ak47, 0, 12, 300 };
    {
        Protocol::WeaponCommand c{};
        c.kind = static_cast<std::uint32_t>(Protocol::WeaponCommandKind::SetLoadout);
        c.argument = 5;
        std::memcpy(c.entries, entries, sizeof(c.entries));
        c.skills[0] = 10;
        c.skills[8] = 500;  // the AK-47: gangster
        link.Send(Protocol::MessageType::WeaponCommand, &c, sizeof(c));
    }
    run(1.0);
    const auto after = pose();
    std::printf("loadout set:%s, holding slot %u (type %u), skills: pistol %u, AK-47 %u\n", loadoutText(after).c_str(), after.weaponSlot, after.weaponType,
        after.skills[0], after.skills[8]);
    failed += !(after.loadout[2].type == Weapons::DesertEagle && after.loadout[2].ammo == 44 && after.loadout[2].clip == 5 &&
                after.loadout[5].type == Weapons::Ak47 && after.loadout[5].ammo == 300 && after.weaponSlot == 5 && after.loadout[1].type == 0 &&
                after.skills[0] == 10 && after.skills[8] == 500);

    // Hits in Skyrim raise the skill as San Andreas's own would.
    {
        Protocol::WeaponEntry hits[Protocol::kWeaponSlotCount]{};
        hits[0].ammo = 20;
        command(Protocol::WeaponCommandKind::Hit, Weapons::Pistol, hits);
        run(1.0);
        const auto raised = pose();
        std::printf("20 pistol hits: pistol skill %u -> %u\n", after.skills[0], raised.skills[0]);
        failed += !(raised.skills[0] > after.skills[0]);
    }

    // Twin pistols (San Andreas gives them to a CJ with the skill): the second hand's slot carries the same model.
    Protocol::WeaponEntry twins[Protocol::kWeaponSlotCount]{};
    twins[2] = { Weapons::Pistol, 0, 17, 170 };
    command(Protocol::WeaponCommandKind::SetLoadout, 2, twins);
    run(1.5);
    {
        const auto p = pose();
        Protocol::BodyState right{}, left{};
        const bool haveRight = link.ReadWeapon(0, right) && (right.flags & Protocol::kBodyValid) != 0;
        const bool haveLeft = link.ReadWeapon(1, left) && (left.flags & Protocol::kBodyValid) != 0;
        std::printf("pistol held: flags 0x%02X, right hand %s, left hand %s", p.weaponFlags, haveRight ? "valid" : "empty", haveLeft ? "valid" : "empty");
        if (haveRight && haveLeft) {
            std::printf(" (hands %.2f m apart, same model: %s)", std::hypot(right.bones[0].translation[0] - left.bones[0].translation[0],
                            right.bones[0].translation[1] - left.bones[0].translation[1]) * Pose::kMetersPerUnit, right.model != left.model ? "different ids" : "same id");
        }
        std::puts("");
        failed += !haveRight || (((p.weaponFlags & Protocol::kWeaponTwin) != 0) != haveLeft);
    }

    // Aiming: where the camera looks and the gun points as the mouse moves up and down (positive y is down).
    {
        Protocol::WeaponEntry gun[Protocol::kWeaponSlotCount]{};
        gun[2] = { Weapons::DesertEagle, 0, 7, 70 };
        command(Protocol::WeaponCommandKind::SetLoadout, 2, gun);
        run(1.5);
        const auto sample = [&](const char* a_what) {
            const auto p = pose();
            Protocol::BodyState hand{}, body{};
            link.ReadWeapon(0, hand);
            link.ReadBody(body);
            const float rise = p.muzzle[0][2] - hand.bones[0].translation[2];
            const float reach = std::hypot(p.muzzle[0][0] - hand.bones[0].translation[0], p.muzzle[0][1] - hand.bones[0].translation[1]);
            std::printf("     camera at (%.2f, %.2f, %.2f) m from his feet looking (%.2f, %.2f, %.2f); the muzzle is %.2f m above the hand over %.2f m: the gun points %.0f degrees up\n",
                body.cameraPosition[0] * Pose::kMetersPerUnit, body.cameraPosition[1] * Pose::kMetersPerUnit, body.cameraPosition[2] * Pose::kMetersPerUnit,
                body.cameraForward[0], body.cameraForward[1], body.cameraForward[2], rise * Pose::kMetersPerUnit, reach * Pose::kMetersPerUnit,
                std::atan2(rise, reach) * 57.2958f);
            std::printf("aim %-18s cam 0x%02X view pitch %6.2f (positive looks down), hand %.2f m up, muzzle z %.2f m, flags 0x%02X\n", a_what, p.cameraMode, p.viewPitch,
                hand.bones[0].translation[2] * Pose::kMetersPerUnit, p.muzzle[0][2] * Pose::kMetersPerUnit, p.weaponFlags);
        };
        sample("not aiming");
        input.SetAim(true);
        run(1.5);
        sample("aiming");
        for (int i = 0; i < 12; ++i) {
            input.AddMouse(0, 20);  // mouse down
            run(0.05);
        }
        run(0.5);
        sample("mouse moved down");
        for (int i = 0; i < 24; ++i) {
            input.AddMouse(0, -20);  // mouse up
            run(0.05);
        }
        run(0.5);
        sample("mouse moved up");
        input.SetAim(false);
        run(1.0);
        sample("let go");
    }

    // And empty, as a new game does.
    Protocol::WeaponEntry none[Protocol::kWeaponSlotCount]{};
    command(Protocol::WeaponCommandKind::SetLoadout, 0, none);
    run(1.0);
    const auto cleared = pose();
    std::printf("loadout cleared:%s, holding type %u, pistol skill %u\n", loadoutText(cleared).c_str(), cleared.weaponType, cleared.skills[0]);
    failed += !(Weapons::Empty(*reinterpret_cast<const Weapons::Loadout*>(cleared.loadout)) && cleared.weaponType == 0 && cleared.skills[0] == 0);

    link.Tick(ms());
    std::printf("%s\n", failed ? "FAILED" : "weapons check passed");
    return failed ? 1 : 0;
}
