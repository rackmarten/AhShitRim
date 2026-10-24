#include <cstring>
#include <ostream>  // before doctest, so it can print string_view
#include <string_view>

#include <doctest/doctest.h>

#include "../../ahshitrim_protocol.h"

using namespace AhShitRim::Protocol;

// The static_asserts in the header pin the layout at compile time (in the 64-bit tests and the
// 32-bit ASI build alike). These spell out the values a peer of any version relies on.

TEST_CASE("the magic reads ASRM in memory")
{
    char bytes[4];
    std::memcpy(bytes, &kMagic, sizeof(bytes));
    CHECK(std::string_view(bytes, 4) == "ASRM");
}

TEST_CASE("the frozen header layout")
{
    CHECK(sizeof(PeerInfo) == 32);
    CHECK(sizeof(LinkHeader) == 128);
    CHECK(offsetof(LinkHeader, magic) == 0);
    CHECK(offsetof(LinkHeader, creatorVersion) == 12);
    CHECK(offsetof(LinkHeader, peers) == 16);
    CHECK(offsetof(LinkHeader, peers) + static_cast<std::size_t>(Side::SanAndreas) * sizeof(PeerInfo) == 48);
}

TEST_CASE("protocol v24: sound cues")
{
    CHECK(kProtocolVersion == 24);
    CHECK(kCueMissionPassed == 1);
    CHECK(kCueIntroLine == 2);
}

TEST_CASE("protocol v23: the vehicle keymap")
{
    CHECK(kButtonVehicleFire == 0x40000);
    CHECK(kButtonVehicleFireAlt == 0x80000);
    CHECK(kButtonLookBehind == 0x100000);
    CHECK(kButtonCameraView == 0x200000);
    const std::uint32_t before = 0x3FFFF;  // every button up to v22's kButtonHydraulics
    CHECK((before & kButtonHydraulics) != 0);
    CHECK(((kButtonVehicleFire | kButtonVehicleFireAlt | kButtonLookBehind | kButtonCameraView) & before) == 0);
}

TEST_CASE("protocol v22: busted")
{
    CHECK(kInputBusted == 0x80);
    CHECK(static_cast<std::uint8_t>(EffectKind::KnockCj) == 5);
    CHECK((kInputBusted & (kInputOwnsPlayer | kInputPaused | kInputFace | kInputScripted | kInputSeated | kInputWasted | kInputWastedCamera)) == 0);
}

TEST_CASE("protocol v20: cheats")
{
    CHECK(static_cast<std::uint16_t>(MessageType::Cheat) == 7);
    CHECK(sizeof(CheatCommand) == 8);
    CHECK(offsetof(CheatCommand, key) == 0);
    CHECK(offsetof(CheatCommand, cheat) == 4);
    CHECK(offsetof(CheatCommand, flags) == 5);
    CHECK(kCheatOn == 0x1);
}

TEST_CASE("protocol v19 constants")
{
    CHECK(kWeaponSkillCount == 11);
    CHECK(static_cast<std::uint16_t>(MessageType::Shot) == 4);
    CHECK(static_cast<std::uint32_t>(WeaponCommandKind::Hit) == 4);
    CHECK(kShotBullet == 0);
    CHECK(kShotMelee == 1);
    CHECK(kShotSpread == 0x1);
    CHECK(kShotFirstPerson == 0x2);
    CHECK(sizeof(ShotEvent) == 64);
    CHECK(offsetof(ShotEvent, strike) == 8);
    CHECK(offsetof(ShotEvent, damage) == 12);
    CHECK(offsetof(ShotEvent, origin) == 16);
    CHECK(offsetof(ShotEvent, direction) == 28);
    CHECK(offsetof(ShotEvent, range) == 40);
    CHECK(kShotFlame == 2);
    CHECK(static_cast<std::uint16_t>(MessageType::Projectile) == 5);
    CHECK(kProjectileBurst == 0x1);
    CHECK(kProjectilePartMax == 2);
    CHECK(sizeof(ProjectileEvent) == 128);
    CHECK(offsetof(ProjectileEvent, flags) == 5);
    CHECK(offsetof(ProjectileEvent, model) == 8);
    CHECK(offsetof(ProjectileEvent, fuseMs) == 12);
    CHECK(offsetof(ProjectileEvent, origin) == 16);
    CHECK(offsetof(ProjectileEvent, velocity) == 28);
    CHECK(offsetof(ProjectileEvent, rotation) == 40);
    CHECK(offsetof(ProjectileEvent, parts) == 56);
    CHECK(offsetof(ProjectileEvent, id) == 112);
    CHECK(static_cast<std::uint16_t>(MessageType::Effect) == 6);
    CHECK(static_cast<std::uint8_t>(EffectKind::ProjectileGone) == 1);
    CHECK(static_cast<std::uint8_t>(EffectKind::BurnCj) == 4);
    CHECK(sizeof(EffectEvent) == 32);
    CHECK(offsetof(EffectEvent, projectile) == 8);
    CHECK(offsetof(EffectEvent, at) == 12);
    CHECK(offsetof(EffectEvent, seconds) == 24);
    CHECK(kPlayerOnFire == 0x20);
    CHECK(kButtonFire == 0x200);
    CHECK(kButtonAim == 0x400);
    CHECK(kButtonLookLeft == 0x800);
    CHECK(kButtonLookRight == 0x1000);
    CHECK(kButtonTurretLeft == 0x2000);
    CHECK(kButtonTurretRight == 0x4000);
    CHECK(kButtonTurretUp == 0x8000);
    CHECK(kButtonTurretDown == 0x10000);
    CHECK(kBodyJetpack == 0x10);
    CHECK(kBodyParachute == 0x20);
    CHECK(kSkinModelWeapon == 2);
    CHECK(kWeaponHandMax == 2);
    CHECK(kWeaponSlotCount == 13);
    CHECK(static_cast<std::uint16_t>(MessageType::WeaponCommand) == 3);
    CHECK(static_cast<std::uint32_t>(WeaponCommandKind::SetLoadout) == 1);
    CHECK(static_cast<std::uint32_t>(WeaponCommandKind::GiveSet) == 2);
    CHECK(static_cast<std::uint32_t>(WeaponCommandKind::GiveWeapon) == 3);
    CHECK(kInputFace == 0x4);
    CHECK(kInputScripted == 0x8);
    CHECK(kInputSeated == 0x10);
    CHECK(kInputWasted == 0x20);
    CHECK(kInputWastedCamera == 0x40);
    CHECK(kButtonHandbrake == 0x100);
    CHECK(kPlayerRadioRetuning == 0x10);
    CHECK(kRadioNone == -1);
    CHECK(kPlayerCrouching == 0x4);
    CHECK(kPlayerInVehicle == 0x8);
    CHECK(kButtonEnterExit == 0x10);
    CHECK(kButtonHorn == 0x20);
    CHECK(kBodyDriven == 0x2);
    CHECK(kVehicleMax == 4);
    CHECK(kSkinModelVehicle == 1);
    CHECK(kMappingSize == 8404736);
    CHECK(kCollisionChunkUnits == 2048);
    CHECK(static_cast<std::uint16_t>(MessageType::CollisionChunk) == 1);
    CHECK(static_cast<std::uint16_t>(MessageType::ModelChunk) == 2);
    CHECK(kBodyBoneMax == 64);
    CHECK(kModelChunkMax == 256u * 1024);
    CHECK(kPeerTimeoutMs == 2000);
    CHECK((std::wstring_view(kMappingName) == L"Local\\AhShitRim.Link"));  // doctest cannot print wide strings
    CHECK(static_cast<std::uint32_t>(Side::Skyrim) == 0);
    CHECK(static_cast<std::uint32_t>(Side::SanAndreas) == 1);
}

TEST_CASE("the v14 slot and ring layout")
{
    CHECK(kOffsetInputSlot == 128);
    CHECK(kOffsetPlayerSlot == 272);
    CHECK(sizeof(InputState) == 116);
    CHECK(offsetof(InputState, anchor) == 44);
    CHECK(offsetof(InputState, spawnModel) == 56);
    CHECK(offsetof(InputState, spawnCount) == 58);
    CHECK(offsetof(InputState, radioNext) == 60);
    CHECK(offsetof(InputState, radioPrev) == 62);
    CHECK(offsetof(InputState, sceneOffset) == 64);
    CHECK(offsetof(InputState, sceneHeading) == 76);
    CHECK(offsetof(InputState, shoutCount) == 80);
    CHECK(offsetof(InputState, carModel) == 82);
    CHECK(offsetof(InputState, insultCount) == 84);
    CHECK(offsetof(InputState, swearCount) == 90);
    CHECK(offsetof(InputState, carColours) == 86);
    CHECK(offsetof(InputState, carOffset) == 92);
    CHECK(offsetof(InputState, carHeading) == 104);
    CHECK(offsetof(InputState, lineCount) == 108);
    CHECK(offsetof(InputState, lineKind) == 110);
    CHECK(offsetof(InputState, cueCount) == 112);  // v22: in the slot's padding, the slot stays 144 bytes
    CHECK(offsetof(InputState, cue) == 114);
    CHECK(sizeof(InputSlot) == 144);
    CHECK(sizeof(PlayerState) == 248);
    CHECK(offsetof(PlayerState, skills) == 224);
    CHECK(offsetof(PlayerState, aimRadius) == 246);
    CHECK(sizeof(WeaponEntry) == 8);
    CHECK(sizeof(WeaponCommand) == 136);
    CHECK(offsetof(WeaponCommand, skills) == 112);
    CHECK(offsetof(PlayerState, weaponType) == 80);
    CHECK(offsetof(PlayerState, weaponSlot) == 81);
    CHECK(offsetof(PlayerState, weaponFlags) == 82);
    CHECK(offsetof(PlayerState, shots) == 84);
    CHECK(offsetof(PlayerState, cameraMode) == 86);
    CHECK(offsetof(PlayerState, ammoStyle) == 87);
    CHECK(kAmmoTotal == 0);
    CHECK(kAmmoClip == 1);
    CHECK(kAmmoFlame == 2);
    CHECK(offsetof(PlayerState, muzzle) == 88);
    CHECK(offsetof(PlayerState, crosshair) == 112);
    CHECK(offsetof(PlayerState, loadout) == 120);
    CHECK(offsetof(PlayerState, radioStation) == 48);
    CHECK(offsetof(PlayerState, vehicleModel) == 50);
    CHECK(offsetof(PlayerState, vehicleColours) == 52);
    CHECK(offsetof(PlayerState, vehicleOffset) == 56);
    CHECK(offsetof(PlayerState, vehicleHeading) == 68);
    CHECK(offsetof(PlayerState, damage) == 72);
    CHECK(sizeof(PlayerSlot) == 304);
    CHECK(offsetof(InputSlot, value) == 8);
    CHECK(offsetof(PlayerSlot, value) == 8);
    CHECK(kOffsetAudioSlot == 576);
    CHECK(sizeof(AudioSlot) == 64);
    CHECK(offsetof(AudioSlot, value) == 8);
    CHECK(kOffsetAudioSlot + sizeof(AudioSlot) == kOffsetBodySlot);
    CHECK(kOffsetBodySlot == 640);
    CHECK(sizeof(BodySlot) == 1920);
    CHECK(offsetof(BodySlot, value) == 8);
    CHECK(sizeof(BodyState) == 1892);
    CHECK(offsetof(BodyState, cameraUp) == 1856);
    CHECK(offsetof(BodyState, health) == 1868);
    CHECK(offsetof(BodyState, engine) == 1872);
    CHECK(offsetof(BodyState, hiddenParts) == 1884);
    CHECK(kOffsetVehicleSlots == 2560);
    CHECK(kOffsetVehicleSlots % 64 == 0);
    CHECK(kOffsetWeaponSlots == 2560 + 4 * 1920);
    CHECK(kOffsetWeaponSlots % 64 == 0);
    CHECK(kOffsetGearSlot == 10240 + 2 * 1920);
    CHECK(kOffsetGearSlot % 64 == 0);
    CHECK(kOffsetRingToSanAndreas == kOffsetGearSlot + 1920);
    CHECK(kRingToSanAndreasCapacity == 4u * 1024 * 1024);
    CHECK(kOffsetRingToSkyrim == 4210368);
    CHECK(kRingToSkyrimCapacity == 4u * 1024 * 1024);
    CHECK(sizeof(ModelChunkHeader) == 16);
    CHECK(sizeof(SkinVertex) == 52);
    CHECK(sizeof(CollisionChunkHeader) == 40);
}
