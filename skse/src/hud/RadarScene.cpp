#include "hud/RadarScene.h"

#include "shop.h"

namespace AhShitRim::RadarScene
{
    namespace
    {
        // The space a reference is in: its interior cell, or the outermost worldspace of an exterior, since
        // Skyrim's walled cities are worldspaces of their own laid out in their parent's coordinates.
        const RE::TESForm* SpaceOf(const RE::TESForm* a_cellOrWorld)
        {
            if (const auto* cell = a_cellOrWorld ? a_cellOrWorld->As<RE::TESObjectCELL>() : nullptr) {
                if (cell->IsInteriorCell()) {
                    return cell;
                }
                a_cellOrWorld = cell->GetRuntimeData().worldSpace;
            }
            const auto* world = a_cellOrWorld ? a_cellOrWorld->As<RE::TESWorldSpace>() : nullptr;
            while (world && world->parentWorld) {
                world = world->parentWorld;
            }
            return world;
        }

        const RE::TESForm* SpaceOf(RE::TESObjectREFR* a_ref)
        {
            if (!a_ref) {
                return nullptr;
            }
            if (auto* cell = a_ref->GetParentCell()) {
                return SpaceOf(cell);
            }
            return SpaceOf(a_ref->GetWorldspace());
        }

        // Where a_ref shows on the radar from a_here: itself when it's in the same space, else the first door
        // on a_path that is (the way Skyrim's compass points to it).
        std::optional<RE::NiPoint3> Locate(RE::TESObjectREFR* a_ref, const RE::TeleportPath* a_path, const RE::TESForm* a_here)
        {
            if (!a_ref || !a_here) {
                return std::nullopt;
            }
            if (SpaceOf(a_ref) == a_here) {
                return a_ref->GetPosition();
            }
            if (a_path) {
                for (const auto& link : a_path->teleportRefs) {
                    if (link.ref && SpaceOf(link.ref) == a_here) {
                        return link.ref->GetPosition();
                    }
                }
            }
            return std::nullopt;
        }

        // The Ammu-Nations: where each shopkeeper stands in the editor (the forge), or for one who stands
        // inside (Warmaiden's counter), the door outside that leads in.
        struct Shop
        {
            RE::NiPoint3       at;
            const RE::TESForm* space = nullptr;
        };

        std::vector<Shop>& Shops(const RE::TESForm* a_here)
        {
            static std::vector<Shop>  shops;
            static const RE::TESForm* lookedIn = nullptr;
            // Looked for again in each new space until there are some (the keepers' references may come later).
            if (!shops.empty() || a_here == lookedIn) {
                return shops;
            }
            lookedIn = a_here;
            std::vector<RE::Actor*>                                           keepers;
            std::vector<std::pair<RE::TESObjectREFR*, RE::TESObjectCELL*>> doors;  // a load door outside, and the interior it leads to
            {
                const auto& [forms, lock] = RE::TESForm::GetAllForms();
                const RE::BSReadLockGuard guard(lock);
                if (forms) {
                    for (const auto& [id, form] : *forms) {
                        auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;
                        if (!ref) {
                            continue;
                        }
                        if (auto* actor = ref->As<RE::Actor>()) {
                            auto* base = actor->GetActorBase();
                            if (base && AhShitRim::Shop::ShopkeeperOf(base->GetFormID())) {
                                keepers.push_back(actor);
                            }
                        } else if (ref->extraList.HasType(RE::ExtraDataType::kTeleport)) {
                            const auto inside = ref->extraList.GetTeleportLinkedDoor().get();
                            auto*      cell = inside ? inside->GetParentCell() : nullptr;
                            if (cell && cell->IsInteriorCell()) {
                                doors.emplace_back(ref, cell);
                            }
                        }
                    }
                }
            }
            for (auto* actor : keepers) {
                RE::NiPoint3 at, rotation;
                RE::TESForm* where = nullptr;
                if (!actor->GetEditorLocation(at, rotation, where, nullptr)) {
                    at = actor->GetPosition();
                    where = actor->GetParentCell();
                }
                Shop shop{ at, SpaceOf(where) };
                if (const auto* cell = shop.space ? shop.space->As<RE::TESObjectCELL>() : nullptr) {
                    shop.space = nullptr;
                    for (const auto& [door, inside] : doors) {
                        const auto* outside = SpaceOf(door);
                        if (inside == cell && outside && outside->Is(RE::FormType::WorldSpace)) {
                            shop = { door->GetPosition(), outside };
                            break;
                        }
                    }
                }
                // Two keepers of the same shop (Warmaiden's) show as one.
                const bool same = std::any_of(shops.begin(), shops.end(), [&](const Shop& a_other) {
                    return a_other.space == shop.space && a_other.at.GetDistance(shop.at) < 40.0f * Pose::kUnitsPerMeter;
                });
                if (shop.space && !same) {
                    shops.push_back(shop);
                }
            }
            SKSE::log::info("radar: {} Ammu-Nations from {} shopkeepers", shops.size(), keepers.size());
            return shops;
        }

        void Add(Scene& a_scene, Radar::Kind a_kind, const RE::NiPoint3& a_at) { a_scene.blips.push_back({ a_kind, { a_at.x, a_at.y }, a_at.z }); }
    }

    void Gather(RE::PlayerCharacter* a_player, RE::FormID a_mapWorld, Scene& a_out)
    {
        a_out = {};
        const auto  at = a_player->GetPosition();
        const auto* here = SpaceOf(a_player);
        a_out.cj = { at.x, at.y };
        a_out.z = at.z;
        a_out.mapped = here && here->GetFormID() == a_mapWorld;

        // Whoever is fighting the player: small red squares.
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            for (auto& handle : lists->highActorHandles) {
                auto actor = handle.get();
                if (!actor || actor.get() == a_player || actor->IsDead() || !actor->IsInCombat() ||
                    actor->GetActorRuntimeData().currentCombatTarget.get().get() != a_player) {
                    continue;
                }
                Add(a_out, Radar::Kind::Hostile, actor->GetPosition());
            }
        }

        // What the compass points to, copied out under its lock first.
        std::vector<std::pair<RE::TESQuest*, RE::TESQuestTarget*>> targets;
        {
            const RE::BSSpinLockGuard guard(a_player->GetQuestTargetsLock());
            for (const auto& [quest, list] : a_player->GetQuestTargets()) {
                if (quest && list) {
                    for (auto* target : *list) {
                        if (target) {
                            targets.emplace_back(quest, target);
                        }
                    }
                }
            }
        }
        for (const auto& [quest, target] : targets) {
            RE::ObjectRefHandle handle;
            target->GetTargetRef(handle, false, quest);
            const auto ref = handle.get();
            if (const auto where = Locate(ref.get(), &target->teleportPath, here)) {
                Add(a_out, Radar::Kind::Quest, *where);
            }
        }

        for (const auto& shop : Shops(here)) {
            if (shop.space == here) {
                Add(a_out, Radar::Kind::Shop, shop.at);
            }
        }

        // The player's own marker on Skyrim's map.
        auto&      info = a_player->GetInfoRuntimeData();
        const auto marker = info.playerMapMarker.get();
        if (const auto where = Locate(marker.get(), info.playerMarkerPath, here)) {
            Add(a_out, Radar::Kind::Waypoint, *where);
        }
    }
}
