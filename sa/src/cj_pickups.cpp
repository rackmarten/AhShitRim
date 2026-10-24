#include "cj_pickups.h"

#include "body.h"
#include "cj_gear.h"
#include "cj_shots.h"
#include "cj_weapons.h"
#include "log.h"
#include "model_sender.h"
#include "pickups.h"
#include "pose.h"
#include "skin_model.h"
#include "vehicle_kinds.h"
#include "weapons.h"

#include <plugin.h>

#include <CAudioEngine.h>
#include <CBaseModelInfo.h>
#include <CModelInfo.h>
#include <CPickup.h>
#include <CPickups.h>
#include <CPlayerPed.h>
#include <CStreaming.h>
#include <CWeaponInfo.h>
#include <common.h>

#include <Windows.h>

#include <array>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace AhShitRim::CjPickups
{
    namespace
    {
        using Kind = Protocol::PickupKind;

        constexpr std::uint32_t kPickupCount = 620;  // CPickups::aPickUps
        constexpr int           kLoaded = 1;         // LOADSTATE_LOADED
        constexpr int           kStreamKeep = 0x2 | 0x8;  // game required, keep in memory
        constexpr int           kPickupNone = 0;     // PICKUP_NONE
        constexpr int           kAeWeapon = 0x6;     // AE_FRONTEND_PICKUP_WEAPON
        constexpr int           kAeMoney = 0x7;      // AE_FRONTEND_PICKUP_MONEY
        constexpr int           kAeHealth = 0x8;     // AE_FRONTEND_PICKUP_HEALTH
        constexpr int           kAeArmour = 0xA;     // AE_FRONTEND_PICKUP_BODY_ARMOUR
        constexpr int           kAeInfo = 0xB;       // AE_FRONTEND_PICKUP_INFO: the jetpack's (GivePlayerGoodiesWithPickUpMI)

        struct Model
        {
            std::uint32_t                                                id = 0;
            std::uint32_t                                                partCount = 0;
            std::array<Protocol::BodyBone, Protocol::kPickupPartMax> parts{};
            bool                                                         failed = false;
        };

        struct State
        {
            std::deque<Protocol::PickupCommand> commands;
            std::map<std::uint16_t, Model>      models;  // by Pickups::ModelKey
            std::uint32_t                       builds = 0;
            int                                 nextKey = ModelSender::kPickupKey;
        } state;

        Body::RwRows ToRows(const RwMatrix& a_m)
        {
            return { { a_m.right.x, a_m.right.y, a_m.right.z }, { a_m.up.x, a_m.up.y, a_m.up.z }, { a_m.at.x, a_m.at.y, a_m.at.z },
                { a_m.pos.x, a_m.pos.y, a_m.pos.z } };
        }

        // Where a part sits on the pickup: its frame's matrices up to the instance's own frame.
        Body::RwRows PartOnObject(RpAtomic* a_atomic, RwFrame* a_root)
        {
            Body::RwRows rows;
            for (RwFrame* frame = RpAtomicGetFrame(a_atomic); frame && frame != a_root; frame = RwFrameGetParent(frame)) {
                rows = Body::Then(rows, ToRows(*RwFrameGetMatrix(frame)));
            }
            return rows;
        }

        // San Andreas's model index for a kind (a weapon's from weapon.dat), -1 for none.
        int ModelIndexOf(Kind a_kind, std::uint8_t a_weapon)
        {
            if (a_kind == Kind::Weapon) {
                const auto* info = Weapons::Carried(a_weapon) ? CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(a_weapon), 1) : nullptr;
                return info && info->m_nModelId > 0 ? info->m_nModelId : -1;
            }
            const auto name = std::string(Pickups::ModelName(a_kind));
            int        index = -1;
            if (name.empty() || !CModelInfo::GetModelInfo(name.c_str(), &index)) {
                return -1;
            }
            return index;
        }

        bool LoadModel(int a_index)
        {
            if (CStreaming::ms_aInfoForModel[a_index].m_nLoadState != kLoaded) {
                CStreaming::RequestModel(a_index, kStreamKeep);
                CStreaming::LoadAllRequestedModels(false);
            }
            return CStreaming::ms_aInfoForModel[a_index].m_nLoadState == kLoaded;
        }

        void Destroy(RwObject* a_object)
        {
            if (RwObjectGetType(a_object) == rpCLUMP) {
                RpClumpDestroy(reinterpret_cast<RpClump*>(a_object));
            } else if (RwObjectGetType(a_object) == rpATOMIC) {
                auto*    atomic = reinterpret_cast<RpAtomic*>(a_object);
                RwFrame* frame = RpAtomicGetFrame(atomic);
                RpAtomicDestroy(atomic);
                if (frame) {
                    RwFrameDestroy(frame);
                }
            }
        }

        // The model for a kind, built and sent the first time Skyrim asks; it keeps its id and its ModelSender key
        // for good (ModelSender sends it again whenever the link comes up).
        const Model* ModelFor(Kind a_kind, std::uint8_t a_weapon)
        {
            auto& model = state.models[Pickups::ModelKey(a_kind, a_weapon)];
            if (model.id || model.failed) {
                return model.id ? &model : nullptr;
            }
            model.failed = true;  // until it builds
            const int index = ModelIndexOf(a_kind, a_weapon);
            auto*     info = index >= 0 ? CModelInfo::GetModelInfo(index) : nullptr;
            if (!info || !LoadModel(index)) {
                Log(std::format("pickups: no model for pickup {} (weapon {}, model {})", static_cast<int>(a_kind), a_weapon, index));
                return nullptr;
            }
            RwObject* object = info->CreateInstance();
            if (!object) {
                Log(std::format("pickups: model {} made no instance", index));
                return nullptr;
            }
            std::vector<RpAtomic*> parts;
            const auto             label = a_kind == Kind::Weapon ? std::string(Weapons::ModelName(a_weapon)) : std::string(Pickups::ModelName(a_kind));
            auto                   blob = CjWeapons::BuildModel(object, a_kind == Kind::Weapon ? a_weapon : 0, parts, label);
            if (!blob.empty()) {
                RwFrame* root = static_cast<RwFrame*>(object->parent);
                model.partCount = static_cast<std::uint32_t>((std::min)(parts.size(), static_cast<std::size_t>(Protocol::kPickupPartMax)));
                for (std::uint32_t i = 0; i < model.partCount; ++i) {
                    model.parts[i] = Body::ToBone(PartOnObject(parts[i], root), {});
                }
            }
            Destroy(object);
            if (blob.empty()) {
                Log(std::format("pickups: model {} ({}) has nothing to draw", index, label));
                return nullptr;
            }
            model.failed = false;
            model.id = (static_cast<std::uint32_t>(::GetTickCount64()) << 4 | (++state.builds & 0xF)) | 1;
            ModelSender::Set(state.nextKey++, model.id, SkinModel::Chunks(model.id, blob), std::format("pickups: the {}'s model {}", label, index));
            return &model;
        }

        void SendModel(Kind a_kind, std::uint8_t a_weapon)
        {
            const Model* model = ModelFor(a_kind, a_weapon);
            if (!model) {
                return;
            }
            Protocol::PickupEvent event{};
            event.kind = static_cast<std::uint8_t>(Protocol::PickupEventKind::Model);
            event.pickup = static_cast<std::uint8_t>(a_kind);
            event.weapon = a_kind == Kind::Weapon ? a_weapon : 0;
            event.partCount = static_cast<std::uint8_t>(model->partCount);
            event.model = model->id;
            std::memcpy(event.parts, model->parts.data(), sizeof(event.parts));
            GetLink().Send(Protocol::MessageType::PickupEvent, &event, sizeof(event));
        }

        // CJ took one in Skyrim: San Andreas's own sound for it, as CPickup::Update plays it, and the jetpack on him
        // or the parachute in his hands (the gear's dev path, CjGear::Give). A weapon came as a GiveWeapon command.
        void Collected(CPlayerPed* a_ped, const Protocol::PickupCommand& a_command)
        {
            const auto kind = static_cast<Kind>(a_command.pickup);
            int        sound = kAeWeapon;
            switch (kind) {
            case Kind::Health: sound = kAeHealth; break;
            case Kind::Armour: sound = kAeArmour; break;
            case Kind::Money: sound = kAeMoney; break;
            case Kind::Jetpack: sound = kAeInfo; break;
            default: break;
            }
            AudioEngine.ReportFrontendAudioEvent(sound, 0.0f, 1.0f);
            if (kind == Kind::Jetpack) {
                CjGear::Give(a_ped, VehicleKinds::kJetpack);
            } else if (kind == Kind::Parachute) {
                CjGear::Give(a_ped, VehicleKinds::kParachute);
            }
            Log(std::format("pickups: CJ took a pickup of kind {}{}", a_command.pickup, kind == Kind::Weapon ? std::format(" (weapon {})", a_command.weapon) : ""));
        }

        // What a pickup San Andreas made is to Skyrim, by its model: the jetpack, a weapon, the heart or the armour.
        Kind KindOf(int a_model, std::uint8_t& a_weapon)
        {
            a_weapon = 0;
            for (std::uint8_t type = Weapons::BrassKnuckle; type <= Weapons::kLastCarried; ++type) {
                const auto* info = Weapons::Carried(type) ? CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(type), 1) : nullptr;
                if (info && info->m_nModelId == a_model) {
                    a_weapon = type;
                    return type == Weapons::Parachute ? Kind::Parachute : Kind::Weapon;
                }
            }
            auto* info = a_model >= 0 ? CModelInfo::GetModelInfo(a_model) : nullptr;
            if (!info) {
                return Kind::None;
            }
            for (auto kind : { Kind::Health, Kind::Armour, Kind::Money, Kind::Jetpack }) {
                if (ModelIndexOf(kind, 0) == a_model) {
                    return kind;
                }
            }
            return Kind::None;
        }

        // San Andreas's own pickups (the jetpack CJ took off): nothing draws them here, so each goes to Skyrim, which
        // keeps it, and out of San Andreas's list.
        void HandOver(bool a_owned)
        {
            CVector  spawnFeet;
            unsigned handover = 0;
            if (!a_owned || !GetLink().IsUp() || !CjShots::Frame(spawnFeet, handover)) {
                return;
            }
            for (std::uint32_t i = 0; i < kPickupCount; ++i) {
                CPickup& pickup = CPickups::aPickUps[i];
                if (pickup.m_nPickupType == kPickupNone || pickup.m_nFlags.bDisabled) {
                    continue;
                }
                std::uint8_t weapon = 0;
                const Kind   kind = KindOf(pickup.m_nModelIndex, weapon);
                if (kind == Kind::None) {
                    continue;
                }
                const CVector       at = pickup.GetPosn() - spawnFeet;
                Protocol::PickupEvent event{};
                event.handover = handover;
                event.kind = static_cast<std::uint8_t>(Protocol::PickupEventKind::Dropped);
                event.pickup = static_cast<std::uint8_t>(kind);
                event.weapon = weapon;
                event.amount = kind == Kind::Weapon ? (pickup.m_nAmmo ? pickup.m_nAmmo : Pickups::StreetAmmo(weapon)) : pickup.m_nAmmo;
                event.at[0] = at.x * Pose::kUnitsPerMeter;
                event.at[1] = at.y * Pose::kUnitsPerMeter;
                event.at[2] = at.z * Pose::kUnitsPerMeter;
                if (!GetLink().Send(Protocol::MessageType::PickupEvent, &event, sizeof(event))) {
                    return;  // the ring is full: next frame
                }
                Log(std::format("pickups: San Andreas dropped a pickup of model {} (kind {}) at ({:.1f},{:.1f},{:.1f}) m from CJ's anchor, handed to Skyrim",
                    pickup.m_nModelIndex, static_cast<int>(kind), at.x, at.y, at.z));
                pickup.Remove();
            }
        }
    }

    void Queue(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        if (a_size < sizeof(Protocol::PickupCommand) || state.commands.size() >= 64) {
            return;
        }
        Protocol::PickupCommand command;
        std::memcpy(&command, a_data, sizeof(command));
        state.commands.push_back(command);
    }

    void Update(CPlayerPed* a_ped, bool a_owned)
    {
        while (!state.commands.empty()) {
            const auto command = state.commands.front();
            state.commands.pop_front();
            const auto kind = static_cast<Kind>(command.pickup);
            if (command.kind == static_cast<std::uint8_t>(Protocol::PickupCommandKind::Model)) {
                SendModel(kind, command.weapon);
            } else if (command.kind == static_cast<std::uint8_t>(Protocol::PickupCommandKind::Collected) && a_owned) {
                Collected(a_ped, command);
            }
        }
        HandOver(a_owned);
    }
}
