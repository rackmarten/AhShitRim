#include "player/Activation.h"

#include "activate.h"
#include "hud/SaHud.h"
#include "player/AmmuNation.h"
#include "player/Looting.h"

namespace AhShitRim::Activation
{
    namespace
    {
        using Activate::Vec3;

        // References this much further out than the reach are looked at: a door's position is its
        // hinge, a table's its middle.
        constexpr float kSearchSlack = 300.0f;
        constexpr int   kMaxSightHits = 4;  // the player's own body and the like, passed through

        struct State
        {
            RE::ObjectRefHandle target;
            std::string         prompt;
            RE::FormID          logged = 0;  // the last target logged, so a change logs once
        } state;

        Vec3 ToVec(const RE::NiPoint3& a_p) { return { a_p.x, a_p.y, a_p.z }; }

        // Skyrim's own reach for its activate key (fActivatePickLength), read once.
        float Reach()
        {
            static const float reach = [] {
                auto* settings = RE::GameSettingCollection::GetSingleton();
                auto* setting = settings ? settings->GetSetting("fActivatePickLength") : nullptr;
                const float value = setting ? setting->GetFloat() : 0.0f;
                SKSE::log::info("activation: reach {:.0f} units", value > 0.0f ? value : Activate::kDefaultReach);
                return value > 0.0f ? value : Activate::kDefaultReach;
            }();
            return reach;
        }

        // What Skyrim's activate key acts on: doors, levers and other activators, containers, people,
        // furniture (the player sits, and CJ follows it, see PlayerSync), and what can be picked up.
        bool Activatable(RE::TESObjectREFR* a_ref)
        {
            if (a_ref->IsActor()) {
                return true;
            }
            auto* base = a_ref->GetBaseObject();
            if (!base) {
                return false;
            }
            switch (base->GetFormType()) {
            case RE::FormType::Door:
            case RE::FormType::Activator:
            case RE::FormType::TalkingActivator:
            case RE::FormType::Container:
            case RE::FormType::Furniture:
            case RE::FormType::Flora:
            case RE::FormType::Tree:
            case RE::FormType::Weapon:
            case RE::FormType::Armor:
            case RE::FormType::Ammo:
            case RE::FormType::Book:
            case RE::FormType::Misc:
            case RE::FormType::Ingredient:
            case RE::FormType::AlchemyItem:
            case RE::FormType::Scroll:
            case RE::FormType::KeyMaster:
            case RE::FormType::SoulGem:
            case RE::FormType::Note:
                return true;
            default:
                return false;
            }
        }

        // The box the pick ray tests: the base object's bounds on its 3D, a standing box for a living
        // person (their bounds are often empty), the 3D's bounding sphere for a body on the ground or
        // anything whose bounds say nothing.
        Activate::Box BoxOf(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_3d)
        {
            const auto& bound = a_3d->worldBound;
            if (auto* actor = a_ref->As<RE::Actor>()) {
                if (!actor->IsDead()) {
                    return Activate::StandingBox(ToVec(actor->GetPosition()), actor->GetAngleZ(), actor->GetScale());
                }
                return Activate::SphereBox(ToVec(bound.center), (std::min)(bound.radius, 90.0f));
            }
            if (auto* base = a_ref->GetBaseObject()) {
                const auto& b = base->boundData;
                float       rotate[3][3];
                for (int r = 0; r < 3; r++) {
                    for (int c = 0; c < 3; c++) {
                        rotate[r][c] = a_3d->world.rotate.entry[r][c];
                    }
                }
                const auto box = Activate::BoxOf({ static_cast<float>(b.boundMin.x), static_cast<float>(b.boundMin.y), static_cast<float>(b.boundMin.z) },
                    { static_cast<float>(b.boundMax.x), static_cast<float>(b.boundMax.y), static_cast<float>(b.boundMax.z) }, rotate,
                    ToVec(a_3d->world.translate), a_3d->world.scale);
                if (Activate::Usable(box)) {
                    return box;
                }
            }
            return Activate::SphereBox(ToVec(bound.center), bound.radius);
        }

        // Nothing solid between CJ's chest and the target: a line of sight ray along the pick ray, as far
        // as the target, passing through the player's own body.
        bool InSight(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_target, const Vec3& a_origin, const Vec3& a_dir, float a_distance)
        {
            auto* cell = a_player->GetParentCell();
            auto* bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto* world = bhk ? bhk->GetWorld1() : nullptr;
            if (!world) {
                return true;  // nothing to test against: Skyrim's own activation decides
            }
            const float              scale = RE::bhkWorld::GetWorldScale();
            const float              length = a_distance + 2.0f * Activate::kSightTolerance;
            RE::hkpWorldRayCastInput input;
            input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
            RE::BSReadLockGuard lock(bhk->worldLock);
            float               from = 0.0f;
            for (int i = 0; i < kMaxSightHits; i++) {
                const Vec3 start = Activate::Add(a_origin, Activate::Scale(a_dir, from));
                const Vec3 end = Activate::Add(a_origin, Activate::Scale(a_dir, length));
                input.from = RE::hkVector4(start.x * scale, start.y * scale, start.z * scale, 0.0f);
                input.to = RE::hkVector4(end.x * scale, end.y * scale, end.z * scale, 0.0f);
                RE::hkpWorldRayCastOutput output;
                world->CastRay(input, output);
                if (!output.HasHit()) {
                    return true;
                }
                const float at = from + (length - from) * output.hitFraction;
                auto*       hit = output.rootCollidable ? RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable) : nullptr;
                if (hit != a_player) {
                    return Activate::InSight(true, at, hit == a_target, a_distance);
                }
                from = at + 1.0f;
            }
            return true;
        }

        std::string PromptOf(RE::TESObjectREFR* a_ref)
        {
            RE::BSString text;
            auto*        base = a_ref->GetBaseObject();
            if (!base || !base->GetActivateText(a_ref, text)) {
                text = "";
            }
            const char* name = a_ref->GetDisplayFullName();
            return Activate::Prompt(text.c_str() ? text.c_str() : "", name ? name : "");
        }
    }

    void Update(RE::PlayerCharacter* a_player, const Protocol::PlayerState* a_pose, bool a_menu)
    {
        state.target = {};
        state.prompt.clear();
        if (!a_pose || a_menu || (a_pose->flags & Protocol::kPlayerInVehicle) != 0) {
            return;
        }
        auto* tes = RE::TES::GetSingleton();
        if (!tes) {
            return;
        }
        const float reach = Reach();
        const auto  feet = a_player->GetPosition();
        const Vec3  origin{ feet.x, feet.y, feet.z + Activate::kRayHeight };
        const Vec3  dir = Activate::Direction(a_pose->viewHeading, a_pose->viewPitch);

        std::vector<Activate::Candidate>   candidates;
        std::vector<RE::TESObjectREFR*>    refs;
        tes->ForEachReferenceInRange(a_player, reach + kSearchSlack, [&](RE::TESObjectREFR* a_ref) {
            if (a_ref && a_ref != a_player && !a_ref->IsDisabled() && !a_ref->IsDeleted() && Activatable(a_ref)) {
                if (auto* root = a_ref->Get3D(); root && !root->GetAppCulled()) {
                    candidates.push_back({ BoxOf(a_ref, root), a_ref->GetFormID() });
                    refs.push_back(a_ref);
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });

        // Nearest first: the first one CJ sees and Skyrim has words for is it.
        while (!candidates.empty()) {
            const auto picked = Activate::Pick(origin, dir, reach, candidates);
            if (!picked) {
                break;
            }
            auto* ref = refs[picked->index];
            if (InSight(a_player, ref, origin, dir, picked->distance)) {
                auto shop = AmmuNation::PromptFor(ref);
                if (auto prompt = shop.empty() ? PromptOf(ref) : std::move(shop); !prompt.empty()) {
                    state.target = ref->GetHandle();
                    state.prompt = std::move(prompt);
                    break;
                }
            }
            candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(picked->index));
            refs.erase(refs.begin() + static_cast<std::ptrdiff_t>(picked->index));
        }

        const auto id = state.target ? state.target.get()->GetFormID() : 0;
        if (id != state.logged) {
            state.logged = id;
            if (id) {
                SKSE::log::info("activation: CJ faces {:08X} ({})", id, state.prompt);
            }
        }
        SaHud::ShowPrompt(state.prompt);
    }

    bool Activate(RE::PlayerCharacter* a_player, Input::KeyLayout a_layout)
    {
        const auto ref = state.target.get();
        if (!ref) {
            return false;
        }
        state.target = {};
        if (AmmuNation::Open(ref.get(), a_layout) || Looting::Activate(a_player, ref.get())) {
            return true;
        }
        SKSE::log::info("activation: {:08X} activated by the player ({})", ref->GetFormID(), state.prompt);
        ref->ActivateRef(a_player, 0, nullptr, 1, false);
        return true;
    }

    void Clear()
    {
        state.target = {};
        state.prompt.clear();
        state.logged = 0;
    }
}
