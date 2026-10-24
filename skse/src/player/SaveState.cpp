#include "player/SaveState.h"

#include "cosave.h"
#include "mercy.h"
#include "revive.h"
#include "player/CheatCodes.h"
#include "player/PlayerSync.h"
#include "player/Police.h"
#include "player/Respawns.h"
#include "wanted.h"

namespace AhShitRim::SaveState
{
    namespace
    {
        int WeaponCount(const Cosave::Record& a_record)
        {
            int count = 0;
            for (const auto& entry : a_record.weapons.entries) {
                count += entry.type != 0;
            }
            return count;
        }

        void Save(SKSE::SerializationInterface* a_intfc)
        {
            CheatCodes::Save(a_intfc);  // NOMERCY, a record of its own
            Police::Save(a_intfc);      // the wanted level, another
            Respawns::Save(a_intfc);    // the dead waiting to come back, another
            Cosave::Record record;
            PlayerSync::Snapshot(record);
            const auto bytes = Cosave::Write(record);
            if (!a_intfc->WriteRecord(Cosave::kRecordType, Cosave::kVersion, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
                SKSE::log::error("save state: the co-save record could not be written");
                return;
            }
            SKSE::log::info("save state: saved (San Andreas {} the player, armour {:.0f}, {} weapons{}, {} one-off pickups taken)",
                record.handover ? "has" : "doesn't have", record.armor, WeaponCount(record),
                record.car.model ? std::format(", in a vehicle of model {}", record.car.model) : std::string(), record.pickupsTaken.size());
        }

        void Load(SKSE::SerializationInterface* a_intfc)
        {
            std::uint32_t type = 0, version = 0, length = 0;
            while (a_intfc->GetNextRecordInfo(type, version, length)) {
                if (type != Cosave::kRecordType && type != Mercy::kRecordType && type != Wanted::kRecordType && type != Revive::kRecordType) {
                    continue;
                }
                std::vector<std::uint8_t> bytes(length);
                if (length && a_intfc->ReadRecordData(bytes.data(), length) != length) {
                    SKSE::log::warn("save state: the co-save record {:08X} is cut short, ignored", type);
                    continue;
                }
                if (type == Mercy::kRecordType) {
                    CheatCodes::Load(version, bytes.data(), bytes.size());
                    continue;
                }
                if (type == Wanted::kRecordType) {
                    Police::Load(version, bytes.data(), bytes.size());
                    continue;
                }
                if (type == Revive::kRecordType) {
                    Respawns::Load(version, bytes.data(), bytes.size());
                    continue;
                }
                Cosave::Record record;
                switch (Cosave::Read(version, bytes.data(), bytes.size(), record)) {
                case Cosave::Result::Ok:
                    SKSE::log::info("save state: loaded version {} (San Andreas {} the player, armour {:.0f}, {} weapons{})", version,
                        record.handover ? "has" : "doesn't have", record.armor, WeaponCount(record),
                        record.car.model ? std::format(", in a vehicle of model {}", record.car.model) : std::string());
                    PlayerSync::Restore(record);
                    break;
                case Cosave::Result::TooNew:
                    SKSE::log::warn("save state: the co-save record is version {}, newer than this AhShitRim's {}, ignored", version, Cosave::kVersion);
                    break;
                default:
                    SKSE::log::warn("save state: the co-save record (version {}, {} bytes) doesn't read, ignored", version, length);
                    break;
                }
            }
        }

        void Revert(SKSE::SerializationInterface*)
        {
            PlayerSync::Revert();
            CheatCodes::Revert();
            Police::Revert();
            Respawns::Revert();
        }
    }

    void Install()
    {
        const auto* serialization = SKSE::GetSerializationInterface();
        serialization->SetUniqueID(Cosave::kRecordType);
        serialization->SetSaveCallback(Save);
        serialization->SetLoadCallback(Load);
        serialization->SetRevertCallback(Revert);
        SKSE::log::info("save state: co-save record ASRM version {}", Cosave::kVersion);
    }
}
