#include "player/ObjectHits.h"

#include "collision/CollisionSampler.h"
#include "explosives.h"

#include <chrono>
#include <format>

namespace AhShitRim::ObjectHits
{
    namespace
    {
        constexpr int    kLogged = 24;
        constexpr double kConeEvery = 0.1;  // seconds between the flamethrower's looks for objects (it puffs every frame)

        struct State
        {
            Traps::Scorched scorched;
            int             logged = 0;
            double          lastCone = 0.0;
        } state;

        double NowSeconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        Traps::Base BaseOf(const RE::TESBoundObject* a_base)
        {
            switch (a_base->GetFormType()) {
            case RE::FormType::Static: return Traps::Base::Static;
            case RE::FormType::Activator:
            case RE::FormType::TalkingActivator: return Traps::Base::Activator;
            case RE::FormType::MovableStatic: return Traps::Base::MovableStatic;
            case RE::FormType::Furniture: return Traps::Base::Furniture;
            case RE::FormType::Container: return Traps::Base::Container;
            case RE::FormType::Door: return Traps::Base::Door;
            default: return Traps::Base::Other;
            }
        }

        // Tells Skyrim's scripts of the hit, and damages a destructible object.
        void Land(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref, Traps::Hit a_hit, std::uint8_t a_weapon, float a_skyrimDamage)
        {
            const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
            if (!base || a_ref->As<RE::Actor>() || a_ref->IsDisabled() || a_ref->IsDeleted()) {
                return;
            }
            const bool destructible = Destructible(a_ref);
            if (!Traps::Told(BaseOf(base), destructible)) {
                return;
            }
            RE::TESHitEvent event(a_ref, a_player, Traps::SourceOf(a_hit, a_weapon), 0, RE::TESHitEvent::Flag::kNone);
            if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
                events->SendEvent(&event);
            }
            const float damage = destructible ? Traps::ObjectDamage(a_hit, a_skyrimDamage) : 0.0f;
            if (damage > 0.0f) {
                a_ref->DamageObject(damage, false);
                CollisionSampler::Reread(a_ref->GetPosition(), Traps::kRereadAfter);
            }
            if (state.logged < kLogged) {
                state.logged++;
                SKSE::log::info("object hits: {} ({:08X}, {}) hit by CJ's {} of weapon {}{}", a_ref->GetDisplayFullName(), a_ref->GetFormID(),
                    RE::FormTypeToString(base->GetFormType()), a_hit == Traps::Hit::Bullet ? "bullet" : a_hit == Traps::Hit::Blow ? "blow" : a_hit == Traps::Hit::Fire ? "fire" : "blast",
                    a_weapon, damage > 0.0f ? std::format(", {:.0f} damage to it", damage) : "");
            }
        }

        // Every object whose bound comes within a_radius of a_at and passes a_reaches(its centre, its radius).
        template <class F>
        void ForObjectsNear(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, float a_radius, F&& a_reaches, std::vector<RE::TESObjectREFR*>& a_out)
        {
            auto* tes = RE::TES::GetSingleton();
            if (!tes) {
                return;
            }
            const float around = a_player->GetPosition().GetDistance(a_at) + a_radius + 500.0f;  // bounds reach past their centres
            tes->ForEachReferenceInRange(a_player, around, [&](RE::TESObjectREFR* a_ref) {
                if (!a_ref || a_ref == a_player || a_ref->As<RE::Actor>()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                auto* root = a_ref->Get3D();
                if (!root) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                const auto& bound = root->worldBound;
                if (bound.center.GetDistance(a_at) <= a_radius + bound.radius && a_reaches(bound.center, bound.radius)) {
                    a_out.push_back(a_ref);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        void Burn(RE::PlayerCharacter* a_player, const std::vector<RE::TESObjectREFR*>& a_refs, Traps::Hit a_hit, std::uint8_t a_weapon, float a_skyrimDamage)
        {
            const double now = NowSeconds();
            for (auto* ref : a_refs) {
                if (state.scorched.Takes(ref->GetFormID(), now)) {
                    Land(a_player, ref, a_hit, a_weapon, a_skyrimDamage);
                }
            }
        }
    }

    bool Destructible(const RE::TESObjectREFR* a_ref)
    {
        const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
        const auto* form = base ? base->As<RE::BGSDestructibleObjectForm>() : nullptr;
        return form && form->data && (form->data->health > 0 || form->data->numStages > 0);
    }

    void Struck(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref, Traps::Hit a_hit, std::uint8_t a_weapon, float a_saDamage)
    {
        Land(a_player, a_ref, a_hit, a_weapon, Traps::SkyrimDamage(a_saDamage, a_weapon));
    }

    void FireCone(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_origin, const RE::NiPoint3& a_dir, float a_range)
    {
        const double now = NowSeconds();
        if (now - state.lastCone < kConeEvery) {
            return;
        }
        state.lastCone = now;
        std::vector<RE::TESObjectREFR*> refs;
        ForObjectsNear(a_player, a_origin, a_range, [&](const RE::NiPoint3& a_center, float a_radius) {
            return Explosives::InFlame({ a_origin.x, a_origin.y, a_origin.z }, { a_dir.x, a_dir.y, a_dir.z }, a_range, { a_center.x, a_center.y, a_center.z }, a_radius);
        }, refs);
        Burn(a_player, refs, Traps::Hit::Fire, Weapons::Flamethrower, 0.0f);
    }

    void Around(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, float a_radius, Traps::Hit a_hit, float a_skyrimDamage)
    {
        std::vector<RE::TESObjectREFR*> refs;
        ForObjectsNear(a_player, a_at, a_radius, [](const RE::NiPoint3&, float) { return true; }, refs);
        Burn(a_player, refs, a_hit, a_hit == Traps::Hit::Fire ? Weapons::Molotov : Weapons::RocketLauncher, a_skyrimDamage);
    }

    void Clear()
    {
        state.scorched.Clear();
    }
}
