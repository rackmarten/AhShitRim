#include "player/Activation.h"

#include "activate.h"
#include "hud/SaHud.h"
#include "link/LinkService.h"
#include "player/AmmuNation.h"
#include "player/Conversation.h"
#include "player/Homies.h"
#include "player/Looting.h"
#include "player/PlayerSync.h"
#include "pad.h"
#include "reactions.h"
#include "weapons.h"

namespace AhShitRim::Activation
{
    namespace
    {
        using Activate::Vec3;

        // References this much further out than the reach are looked at: a door's position is its
        // hinge, a table's its middle.
        constexpr float kSearchSlack = 300.0f;
        constexpr int   kMaxSightHits = 12;  // the player's own body, homies, zones (Volume, Body), passed through

        struct State
        {
            RE::ObjectRefHandle target;
            std::string         prompt;
            RE::FormID          logged = 0;  // the last target logged, so a change logs once
            DebugState          debug;       // the last Update's, for the test kit
            bool                debugOn = false;  // kept only once the test kit has asked for it (Debug)
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

        // What can be picked up or read.
        bool IsItem(RE::FormType a_type)
        {
            switch (a_type) {
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

        // Invisible volumes round things (an actor zone, a trigger): the crosshair's ray always goes on through them,
        // also when one is a candidate (a nameless trigger E has no words for).
        bool Volume(RE::COL_LAYER a_layer)
        {
            switch (a_layer) {
            case RE::COL_LAYER::kActorZone:
            case RE::COL_LAYER::kTrigger:
            case RE::COL_LAYER::kProjectileZone:
            case RE::COL_LAYER::kGasTrap:
            case RE::COL_LAYER::kCloudTrap:
            case RE::COL_LAYER::kAcousticSpace:
            case RE::COL_LAYER::kNonCollidable:
                return true;
            default:
                return false;
            }
        }

        // Someone's capsule or body: the ray goes on through it when it isn't a candidate (a homie beside CJ, whom E
        // never picks; a body), and stops at someone E can talk to. Stopped at a homie's capsule or the zone round
        // Jurgen Windcaller's tomb, the ray found nothing under the crosshair and E fell back to the nearest box in
        // front of CJ, the tomb's, so the note on it couldn't be read.
        bool Body(RE::COL_LAYER a_layer)
        {
            switch (a_layer) {
            case RE::COL_LAYER::kCharController:
            case RE::COL_LAYER::kBiped:
            case RE::COL_LAYER::kBipedNoCC:
            case RE::COL_LAYER::kDeadBip:
                return true;
            default:
                return false;
            }
        }

        // The first thing the camera's ray through the crosshair meets, passing through the player's own body, any
        // Volume and any Body that isn't a candidate: which candidate it is (if any) and how far along. Statics,
        // activators and the capsules of people E can talk to stop it; clutter doesn't, and Activate::Aimed tests
        // its boxes up to the hit. Out of passes, the ray counts as stopped where the last one started, never as
        // open (a crowd of homies between the camera and CJ mustn't let E pick through a wall).
        Activate::CrosshairHit CrosshairHitOf(RE::PlayerCharacter* a_player, const std::vector<RE::TESObjectREFR*>& a_refs, const Vec3& a_camera,
            const Vec3& a_dir, float a_length)
        {
            Activate::CrosshairHit result;
            auto*                  cell = a_player->GetParentCell();
            auto*                  bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto*                  world = bhk ? bhk->GetWorld1() : nullptr;
            if (!world) {
                return result;
            }
            const float              scale = RE::bhkWorld::GetWorldScale();
            RE::hkpWorldRayCastInput input;
            input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
            RE::BSReadLockGuard lock(bhk->worldLock);
            float               from = 0.0f;
            for (int i = 0; i < kMaxSightHits; i++) {
                const Vec3 start = Activate::Add(a_camera, Activate::Scale(a_dir, from));
                const Vec3 end = Activate::Add(a_camera, Activate::Scale(a_dir, a_length));
                input.from = RE::hkVector4(start.x * scale, start.y * scale, start.z * scale, 0.0f);
                input.to = RE::hkVector4(end.x * scale, end.y * scale, end.z * scale, 0.0f);
                RE::hkpWorldRayCastOutput output;
                world->CastRay(input, output);
                if (!output.HasHit()) {
                    return result;
                }
                const float at = from + (a_length - from) * output.hitFraction;
                auto*       hit = output.rootCollidable ? RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable) : nullptr;
                const auto  found = hit ? std::ranges::find(a_refs, hit) : a_refs.end();
                const auto  layer = output.rootCollidable ? output.rootCollidable->GetCollisionLayer() : RE::COL_LAYER::kUnidentified;
                if (hit != a_player && !Volume(layer) && (found != a_refs.end() || !Body(layer))) {
                    if (state.debugOn) {
                        state.debug.hit = hit ? hit->GetFormID() : 0;
                        state.debug.hitDistance = at;
                        state.debug.hitLayer = static_cast<int>(layer);
                    }
                    result.distance = at;
                    if (found != a_refs.end()) {
                        result.candidate = static_cast<std::size_t>(found - a_refs.begin());
                    }
                    return result;
                }
                from = at + 1.0f;
            }
            result.distance = from;  // out of passes: blocked here
            return result;
        }

        // What the crosshair is on (Activate::Aimed): from the camera San Andreas sees through, its ray through
        // San Andreas's crosshair while CJ aims a gun and through the middle of the picture otherwise.
        std::optional<std::size_t> Aimed(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState& a_pose,
            const Vec3& a_chest, float a_reach, const std::vector<Activate::Candidate>& a_candidates, const std::vector<RE::TESObjectREFR*>& a_refs)
        {
            Protocol::BodyState body{};
            if (!LinkService::Get().ReadBody(body) || (body.flags & Protocol::kBodyValid) == 0 || body.handover != a_pose.handover) {
                return std::nullopt;
            }
            const bool free = Weapons::ScopeOf(a_pose.cameraMode) == Weapons::Scope::Aim;
            const auto ray = Reactions::AimRay({ body.cameraForward[0], body.cameraForward[1], body.cameraForward[2] },
                { body.cameraUp[0], body.cameraUp[1], body.cameraUp[2] }, body.cameraFov, free ? a_pose.crosshair[0] : 0.5f, free ? a_pose.crosshair[1] : 0.5f);
            const Vec3 camera{ a_anchor.x + body.cameraPosition[0], a_anchor.y + body.cameraPosition[1], a_anchor.z + body.cameraPosition[2] };
            const Vec3 dir{ ray.x, ray.y, ray.z };
            if (!std::isfinite(camera.x + camera.y + camera.z) || !(Activate::Dot(dir, dir) > 0.5f)) {
                return std::nullopt;
            }
            // As far as the camera is from CJ's chest, and his reach beyond.
            const Vec3  back = Activate::Sub(a_chest, camera);
            const float length = std::sqrt(Activate::Dot(back, back)) + a_reach + kSearchSlack;
            for (std::size_t i = 0; state.debugOn && i < a_candidates.size() && i < state.debug.candidates.size(); ++i) {
                if (const auto span = Activate::Through(camera, dir, a_candidates[i].box)) {
                    state.debug.candidates[i].enter = span->enter;
                    state.debug.candidates[i].leave = span->leave;
                }
            }
            return Activate::Aimed(camera, dir, a_chest, a_reach, a_candidates, CrosshairHitOf(a_player, a_refs, camera, dir, length));
        }

        std::string PromptOf(RE::TESObjectREFR* a_ref)
        {
            RE::BSString text;
            auto*        base = a_ref->GetBaseObject();
            if (!base || !base->GetActivateText(a_ref, text)) {
                text = "";
            }
            const char* name = a_ref->GetDisplayFullName();
            return Activate::Prompt(text.c_str() ? text.c_str() : "", name ? name : "", PlayerSync::PadInUse() ? Pad::kActivateName : "E");
        }
    }

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, bool a_menu)
    {
        state.target = {};
        state.prompt.clear();
        if (state.debugOn) {
            state.debug = {};
        }
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
            if (a_ref && a_ref != a_player && !a_ref->IsDisabled() && !a_ref->IsDeleted() && !Homies::IsHomie(a_ref) && Activatable(a_ref)) {  // v39: a homie has nothing to say, G (aimed) is his key
                if (auto* root = a_ref->Get3D(); root && !root->GetAppCulled()) {
                    auto* actor = a_ref->As<RE::Actor>();
                    auto* base = a_ref->GetBaseObject();
                    auto  kind = Activate::Kind::Other;  // someone in a seat beats the seat (Activate::Pick)
                    if (actor) {
                        kind = actor->IsDead() ? Activate::Kind::Body : Activate::Kind::Person;
                    } else if (base && base->Is(RE::FormType::Furniture)) {
                        kind = Activate::Kind::Furniture;
                    } else if (base && IsItem(base->GetFormType())) {
                        kind = Activate::Kind::Item;  // lying on whatever the crosshair's ray hits under it (Activate::Aimed)
                    }
                    candidates.push_back({ BoxOf(a_ref, root), a_ref->GetFormID(), kind });
                    refs.push_back(a_ref);
                    if (state.debugOn) {
                        const auto& box = candidates.back().box;
                        state.debug.candidates.push_back({ a_ref->GetFormID(), a_ref->GetName() ? a_ref->GetName() : "",
                            kind == Activate::Kind::Person ? "person" : kind == Activate::Kind::Body ? "body" : kind == Activate::Kind::Furniture ? "furniture" : kind == Activate::Kind::Item ? "item" : "other", -1.0f, -1.0f,
                            { box.center.x, box.center.y, box.center.z, box.half.x, box.half.y, box.half.z } });
                    }
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });

        // A reference Skyrim has words for (or that keeps a shop) is it.
        const auto take = [&](RE::TESObjectREFR* a_ref) {
            if (Homies::IsHomie(a_ref)) {
                return false;
            }
            auto shop = AmmuNation::PromptFor(a_ref);
            if (auto prompt = shop.empty() ? PromptOf(a_ref) : std::move(shop); !prompt.empty()) {
                state.target = a_ref->GetHandle();
                state.prompt = std::move(prompt);
                return true;
            }
            return false;
        };

        // What the crosshair is on first; else nearest first along his facing, the first one CJ sees.
        bool aimed = false;
        while (const auto index = Aimed(a_player, a_anchor, *a_pose, origin, reach, candidates, refs)) {
            aimed = take(refs[*index]);
            if (aimed || candidates[*index].kind != Activate::Kind::Item) {
                break;
            }
            // An item Skyrim has no words for: what it lies on, or what else the crosshair is on.
            candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(*index));
            refs.erase(refs.begin() + static_cast<std::ptrdiff_t>(*index));
            if (state.debugOn) {
                state.debug.candidates.erase(state.debug.candidates.begin() + static_cast<std::ptrdiff_t>(*index));
            }
        }
        while (!aimed && !candidates.empty()) {
            const auto picked = Activate::Pick(origin, dir, reach, candidates);
            if (!picked) {
                break;
            }
            auto* ref = refs[picked->index];
            if (InSight(a_player, ref, origin, dir, picked->distance) && take(ref)) {
                break;
            }
            candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(picked->index));
            refs.erase(refs.begin() + static_cast<std::ptrdiff_t>(picked->index));
        }

        const auto id = state.target ? state.target.get()->GetFormID() : 0;
        if (state.debugOn) {
            state.debug.target = id;
            state.debug.aimed = aimed;
            state.debug.prompt = state.prompt;
        }
        if (id != state.logged) {
            state.logged = id;
            if (id) {
                SKSE::log::info("activation: CJ {} {:08X} ({})", aimed ? "aims at" : "faces", id, state.prompt);
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
        if (auto* actor = ref->As<RE::Actor>(); actor && !actor->IsDead() && !Conversation::MayTalk()) {
            SKSE::log::info("activation: {:08X} not talked to, CJ is in a vehicle", ref->GetFormID());
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

    DebugState Debug()
    {
        state.debugOn = true;
        return state.debug;
    }
}
