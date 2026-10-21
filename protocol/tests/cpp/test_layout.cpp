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

TEST_CASE("protocol v12 constants")
{
    CHECK(kProtocolVersion == 12);
    CHECK(kButtonFire == 0x200);
    CHECK(kButtonAim == 0x400);
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
    CHECK(kMappingSize == 8402752);
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

TEST_CASE("the v12 slot and ring layout")
{
    CHECK(kOffsetInputSlot == 128);
    CHECK(kOffsetPlayerSlot == 272);
    CHECK(sizeof(InputState) == 112);
    CHECK(offsetof(InputState, anchor) == 44);
    CHECK(offsetof(InputState, spawnModel) == 56);
    CHECK(offsetof(InputState, spawnCount) == 58);
    CHECK(offsetof(InputState, radioNext) == 60);
    CHECK(offsetof(InputState, radioPrev) == 62);
    CHECK(offsetof(InputState, sceneOffset) == 64);
    CHECK(offsetof(InputState, sceneHeading) == 76);
    CHECK(offsetof(InputState, shoutCount) == 80);
    CHECK(offsetof(InputState, carModel) == 82);
    CHECK(offsetof(InputState, carColours) == 86);
    CHECK(offsetof(InputState, carOffset) == 92);
    CHECK(offsetof(InputState, carHeading) == 104);
    CHECK(sizeof(InputSlot) == 144);
    CHECK(sizeof(PlayerState) == 224);
    CHECK(sizeof(WeaponEntry) == 8);
    CHECK(sizeof(WeaponCommand) == 112);
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
    CHECK(sizeof(PlayerSlot) == 240);
    CHECK(offsetof(InputSlot, value) == 8);
    CHECK(offsetof(PlayerSlot, value) == 8);
    CHECK(kOffsetAudioSlot == 512);
    CHECK(sizeof(AudioSlot) == 64);
    CHECK(offsetof(AudioSlot, value) == 8);
    CHECK(kOffsetAudioSlot + sizeof(AudioSlot) == kOffsetBodySlot);
    CHECK(kOffsetBodySlot == 576);
    CHECK(sizeof(BodySlot) == 1920);
    CHECK(offsetof(BodySlot, value) == 8);
    CHECK(sizeof(BodyState) == 1856);
    CHECK(kOffsetVehicleSlots == 2496);
    CHECK(kOffsetVehicleSlots % 64 == 0);
    CHECK(kOffsetWeaponSlots == 2496 + 4 * 1920);
    CHECK(kOffsetWeaponSlots % 64 == 0);
    CHECK(kOffsetRingToSanAndreas == 10176 + 2 * 1920);
    CHECK(kRingToSanAndreasCapacity == 4u * 1024 * 1024);
    CHECK(kOffsetRingToSkyrim == 4208384);
    CHECK(kRingToSkyrimCapacity == 4u * 1024 * 1024);
    CHECK(sizeof(ModelChunkHeader) == 16);
    CHECK(sizeof(SkinVertex) == 52);
    CHECK(sizeof(CollisionChunkHeader) == 40);
}
