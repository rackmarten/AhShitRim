#include "player/Hitboxes.h"

#include "link/LinkService.h"
#include "player/Riders.h"
#include "run_over.h"
#include "vehicle_hits.h"
#include "vehicle_kinds.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AhShitRim::Hitboxes
{
    namespace
    {
        constexpr int         kLogged = 16;            // the first hits are logged in full
        constexpr float       kScanRange = 4000.0f;    // projectiles farther than this from the vehicle aren't looked at
        constexpr float       kMeleeRange = 1000.0f;   // nor attackers' swings
        constexpr float       kProjectilePad = 6.0f;   // the box grows by about a projectile's own size
        constexpr float       kCombatReach = 141.0f;   // Skyrim's fCombatDistance: a weapon's reach of 1 in units
        constexpr float       kHeavyReach = 350.0f;    // a giant's club, a mammoth's tusks, a dragon's bite
        constexpr double      kSwingWait = 0.15;       // a swing seen reaching the box waits this long for Skyrim's own hit (it knows a power attack)
        constexpr std::size_t kQueueMax = 32;

        using Vec3 = Pose::Vec3;

        Vec3         V(const RE::NiPoint3& a_p) { return { a_p.x, a_p.y, a_p.z }; }
        RE::NiPoint3 Ni(const Vec3& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        double NowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        struct Reported  // a hit on the player from the event sink, for the next Update
        {
            RE::FormID        attacker = 0;
            VehicleHits::Blow blow;
        };

        struct Swing  // a melee swing seen reaching the box, waiting on Skyrim's own hit
        {
            RE::FormID        attacker = 0;
            VehicleHits::Blow blow;
            RE::NiPoint3      from;
            double            due = 0.0;
        };

        struct State
        {
            bool                 drawn = false;  // SetVehicle had CJ's vehicle this frame
            VehicleHits::Box     box;
            VehicleHits::Frame   frame;
            VehicleHits::Cover   cover = VehicleHits::Cover::None;
            VehicleHits::Proofs  proofs;          // San Andreas's proofs of CJ's vehicle (the Rhino's), from its slot
            float                cjLeast = 1.0f;     // the least share of Skyrim's damage CJ keeps any hit on him since Vitals last asked (a blast: none)
            float                riderLeast = 1.0f;  // the same for his passengers, since the last Update
            std::mutex           lock;  // reported, filled by the event sink
            std::vector<Reported> reported;
            std::vector<Swing>   swings;
            VehicleHits::Gate    gate;
            std::unordered_map<std::uint32_t, RE::NiPoint3>         flying;   // projectiles near it, by handle: where each was last frame
            std::unordered_set<std::uint32_t>                       counted;  // those that already hit it
            std::unordered_map<RE::FormID, RE::ATTACK_STATE_ENUM> attacks;  // each attacker's attack state last frame
            std::vector<RE::FormID>              riding;   // CJ's passengers (Riders), for the sink: their hits are the vehicle's too
            std::unordered_map<RE::FormID, float> riderHealth;  // each passenger's health after the last Update
            int      logged = 0;
            unsigned sent = 0, byEvent = 0, byBox = 0, bySwing = 0, stopped = 0;
        } state;

        std::atomic<bool> g_active{ false };  // CJ is in a vehicle with a shape: the sink keeps the player's hits

        RunOver::Build BuildOf(RE::Actor* a_actor)
        {
            auto* race = a_actor ? a_actor->GetRace() : nullptr;
            if (!race) {
                return RunOver::Build::Beast;
            }
            const char* id = race->GetFormEditorID();
            return RunOver::BuildOf(id ? id : "", race->HasKeywordString("ActorTypeNPC"), race->HasKeywordString("ActorTypeDragon"));
        }

        bool IsPerson(RE::Actor* a_actor)
        {
            auto* race = a_actor ? a_actor->GetRace() : nullptr;
            return race && race->HasKeywordString("ActorTypeNPC");
        }

        // What an attacker's hit is, from its source (a weapon, a spell) and its projectile.
        VehicleHits::Blow BlowOf(RE::Actor* a_attacker, RE::TESForm* a_source, RE::BGSProjectile* a_projectile, bool a_power, bool a_bash)
        {
            VehicleHits::Source source;
            source.bash = a_bash;
            source.person = IsPerson(a_attacker);
            float weight = 0.0f;
            if (auto* weapon = a_source ? a_source->As<RE::TESObjectWEAP>() : nullptr) {
                source.weapon = true;
                source.type = static_cast<VehicleHits::Weapon>(std::clamp(static_cast<int>(weapon->GetWeaponType()), 0, 9));
                weight = weapon->GetWeight();
            } else if (a_source && a_source->IsMagicItem()) {
                source.spell = true;
            }
            if (a_projectile) {
                source.stream = a_projectile->IsFlamethrower() || a_projectile->IsBeam() || a_projectile->IsCone();
                source.explodes = a_projectile->data.explosionType != nullptr;
            }
            return { VehicleHits::AttackOf(source), BuildOf(a_attacker), weight, a_power };
        }

        RE::NiPoint3 ChestOf(RE::Actor* a_actor) { return a_actor->GetPosition() + RE::NiPoint3(0.0f, 0.0f, 0.6f * a_actor->GetHeight()); }

        // One hit on the vehicle at a_at (world): through the gate, its damage to San Andreas.
        void Count(std::uint32_t a_id, const VehicleHits::Blow& a_blow, const RE::NiPoint3& a_at, const RE::NiPoint3& a_anchor,
            std::uint32_t a_handover, double a_now, const char* a_how)
        {
            if (a_blow.attack == VehicleHits::Attack::None || !state.gate.Lands(a_id, a_now)) {
                return;
            }
            const auto  split = VehicleHits::SplitOf(state.cover, a_blow.attack, state.proofs);
            const float damage = VehicleHits::DamageOf(a_blow) * split.vehicle;
            if (!(damage > 0.0f)) {
                return;
            }
            Protocol::EffectEvent effect{};
            effect.handover = a_handover;
            effect.kind = static_cast<std::uint8_t>(Protocol::EffectKind::HitVehicle);
            effect.weaponType = static_cast<std::uint8_t>(a_blow.attack);
            const auto at = a_at - a_anchor;
            effect.at[0] = at.x;
            effect.at[1] = at.y;
            effect.at[2] = at.z;
            effect.seconds = damage;
            LinkService::Get().Send(Protocol::MessageType::Effect, &effect, sizeof(effect));
            state.sent++;
            if (state.logged < kLogged) {
                state.logged++;
                auto* attacker = RE::TESForm::LookupByID<RE::Actor>(a_id);
                SKSE::log::info("hitboxes: {} ({:08X}) hit CJ's vehicle with {} ({}): {:.0f} of its health{}", attacker ? attacker->GetName() : "someone gone", a_id,
                    VehicleHits::NameOf(a_blow.attack), a_how, damage, state.cover == VehicleHits::Cover::Open ? ", CJ on it takes half of Skyrim's own" : "");
            }
        }

        // Skyrim's own hits on the player while CJ is in a vehicle: they hit the vehicle (Vitals gives the player its share back).
        struct HitSink : RE::BSTEventSink<RE::TESHitEvent>
        {
            RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
            {
                if (!a_event || !g_active.load()) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* cause = a_event->cause.get();
                auto* attacker = cause ? cause->As<RE::Actor>() : nullptr;
                auto* target = a_event->target.get();
                if (!player || !target || !attacker || attacker == player) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (target != player) {
                    std::lock_guard guard(state.lock);
                    if (std::find(state.riding.begin(), state.riding.end(), target->GetFormID()) == state.riding.end()) {
                        return RE::BSEventNotifyControl::kContinue;  // not CJ, nor one riding with him
                    }
                }
                using Flag = RE::TESHitEvent::Flag;
                auto*          source = RE::TESForm::LookupByID(a_event->source);
                auto*          projectile = RE::TESForm::LookupByID<RE::BGSProjectile>(a_event->projectile);
                const Reported hit{ attacker->GetFormID(), BlowOf(attacker, source, projectile, a_event->flags.any(Flag::kPowerAttack), a_event->flags.any(Flag::kBashAttack)) };
                std::lock_guard guard(state.lock);
                if (state.reported.size() < kQueueMax) {
                    state.reported.push_back(hit);
                }
                // What of it the one hit keeps: none of a blast, nor of anything the vehicle is proof against.
                const float keeps = VehicleHits::SplitOf(state.cover, hit.blow.attack, state.proofs).cj;
                float&      least = target == player ? state.cjLeast : state.riderLeast;
                least = (std::min)(least, keeps);
                return RE::BSEventNotifyControl::kContinue;
            }
        } hitSink;

        // CJ's passengers are covered as he is (Vitals' shield): what Skyrim took off each since the last frame is given back,
        // all of it in a closed vehicle, half on a bike's pillion.
        void CoverRiders(const std::vector<RE::FormID>& a_riding)
        {
            float share = VehicleHits::SplitOf(state.cover, VehicleHits::Attack::Melee, state.proofs).cj;
            {
                std::lock_guard guard(state.lock);
                share = (std::min)(share, std::exchange(state.riderLeast, 1.0f));
            }
            std::unordered_map<RE::FormID, float> health;
            for (const auto id : a_riding) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
                if (!actor || actor->IsDead()) {
                    continue;
                }
                auto*       values = actor->AsActorValueOwner();
                float       now = values->GetActorValue(RE::ActorValue::kHealth);
                const auto  last = state.riderHealth.find(id);
                if (last != state.riderHealth.end() && now < last->second && share < 1.0f) {
                    const float back = (last->second - now) * (1.0f - share);
                    values->RestoreActorValue(RE::ActorValue::kHealth, back);
                    now = values->GetActorValue(RE::ActorValue::kHealth);
                    if (state.logged < kLogged) {
                        state.logged++;
                        SKSE::log::info("hitboxes: CJ's vehicle took a hit for {} ({:08X}), riding with him: {:.1f} health given back", actor->GetName(), id, back);
                    }
                }
                health[id] = now;
            }
            state.riderHealth = std::move(health);
        }

        // Every projectile near the vehicle that flew into its box since last frame (not CJ's own): a hit. In a closed
        // vehicle an arrow stops there; a spell flies on (its blast, if any, is Skyrim's).
        void Scan(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, double a_now)
        {
            auto* manager = RE::Projectile::Manager::GetSingleton();
            if (!manager) {
                return;
            }
            std::vector<RE::NiPointer<RE::Projectile>>      stop;
            std::unordered_map<std::uint32_t, RE::NiPoint3> flying;
            {
                RE::BSSpinLockGuard guard(manager->projectileLock);
                for (auto* list : { &manager->limited, &manager->unlimited }) {
                    for (auto& handle : *list) {
                        auto  ptr = handle.get();
                        auto* projectile = ptr.get();
                        if (!projectile) {
                            continue;
                        }
                        auto& data = projectile->GetProjectileRuntimeData();
                        if (data.flags.any(RE::Projectile::Flags::kDestroyed)) {
                            continue;
                        }
                        const auto shooterRef = data.shooter.get();
                        auto*      shooter = shooterRef ? shooterRef->As<RE::Actor>() : nullptr;
                        if (!shooter || shooter == a_player || shooter->GetActorRuntimeData().currentCombatTarget.get().get() != a_player) {
                            continue;  // CJ's own (the flamethrower's flames), a trap's, or a guard's at a bandit across the road
                        }
                        const auto at = projectile->GetPosition();
                        if (at.GetDistance(Ni(state.frame.pos)) > kScanRange) {
                            continue;
                        }
                        const std::uint32_t id = handle.native_handle();
                        flying[id] = at;
                        const auto last = state.flying.find(id);
                        if (last == state.flying.end() || state.counted.contains(id)) {
                            continue;  // first seen (no path yet), or counted already
                        }
                        const auto enters = VehicleHits::Enters(state.box, state.frame, V(last->second), V(at), kProjectilePad);
                        if (!enters) {
                            continue;
                        }
                        state.counted.insert(id);
                        RE::TESForm* source = data.weaponSource ? static_cast<RE::TESForm*>(data.weaponSource) : static_cast<RE::TESForm*>(data.spell);
                        const auto   blow = BlowOf(shooter, source, projectile->GetProjectileBase(), false, false);
                        const auto   point = last->second + (at - last->second) * *enters;
                        state.byBox++;
                        Count(shooter->GetFormID(), blow, point, a_anchor, a_handover, a_now, "into its box");
                        const bool arrow = blow.attack == VehicleHits::Attack::Arrow || blow.attack == VehicleHits::Attack::Bolt;
                        if (arrow && state.cover == VehicleHits::Cover::Closed) {
                            stop.push_back(ptr);
                        }
                    }
                }
            }
            state.flying = std::move(flying);
            std::erase_if(state.counted, [&](std::uint32_t a_id) { return !state.flying.contains(a_id); });
            for (auto& projectile : stop) {
                projectile->Kill();  // it hit the car, not CJ inside
                state.stopped++;
            }
        }

        // Every melee swing by someone fighting the player that reaches the box: a hit, once Skyrim's own had its chance.
        void Swings(RE::PlayerCharacter* a_player, double a_now)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            std::unordered_map<RE::FormID, RE::ATTACK_STATE_ENUM> attacks;
            lists->ForEachHighActor([&](RE::Actor* a_actor) {
                if (!a_actor || a_actor == a_player || a_actor->IsDead() || !a_actor->Is3DLoaded() ||
                    a_actor->GetActorRuntimeData().currentCombatTarget.get().get() != a_player || a_actor->GetPosition().GetDistance(Ni(state.frame.pos)) > kMeleeRange) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                auto*       actorState = a_actor->AsActorState();
                const auto  now = actorState ? actorState->GetAttackState() : RE::ATTACK_STATE_ENUM::kNone;
                const auto  last = state.attacks.find(a_actor->GetFormID());
                const auto  before = last != state.attacks.end() ? last->second : RE::ATTACK_STATE_ENUM::kNone;
                attacks[a_actor->GetFormID()] = now;
                const bool swing = now == RE::ATTACK_STATE_ENUM::kHit && before != RE::ATTACK_STATE_ENUM::kHit;
                const bool bash = now == RE::ATTACK_STATE_ENUM::kBash && before != RE::ATTACK_STATE_ENUM::kBash;
                if (!swing && !bash) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                auto* equipped = a_actor->GetEquippedObject(false);
                auto* weapon = equipped ? equipped->As<RE::TESObjectWEAP>() : nullptr;
                if ((weapon && (weapon->IsBow() || weapon->IsCrossbow() || weapon->IsStaff())) || (equipped && equipped->IsMagicItem())) {
                    return RE::BSContainer::ForEachResult::kContinue;  // a shot or a spell, which Scan sees
                }
                const auto  build = BuildOf(a_actor);
                const float reach = RunOver::Heavy(build) ? kHeavyReach : kCombatReach * (weapon ? weapon->weaponData.reach : 1.0f) * a_actor->GetScale();
                const auto  chest = ChestOf(a_actor);
                if (VehicleHits::Reaches(state.box, state.frame, V(chest), a_actor->GetAngleZ(), reach)) {
                    state.swings.push_back({ a_actor->GetFormID(), BlowOf(a_actor, weapon, nullptr, false, bash), chest, a_now + kSwingWait });
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            state.attacks = std::move(attacks);
        }

        void Forget()
        {
            g_active.store(false);
            {
                std::lock_guard guard(state.lock);
                state.reported.clear();
            }
            {
                std::lock_guard guard(state.lock);
                state.riding.clear();
            }
            state.riderHealth.clear();
            state.swings.clear();
            state.flying.clear();
            state.counted.clear();
            state.attacks.clear();
            state.cover = VehicleHits::Cover::None;
            state.proofs = {};
        }
    }

    void OnDataLoaded()
    {
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESHitEvent>(&hitSink);
            SKSE::log::info("hitboxes: Skyrim's hits on the player while CJ is in a vehicle hit the vehicle");
        }
    }

    void SetVehicle(const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet)
    {
        state.drawn = a_pose != nullptr;
        if (!a_pose) {
            return;
        }
        state.box = { { a_pose->boxMin[0], a_pose->boxMin[1], a_pose->boxMin[2] }, { a_pose->boxMax[0], a_pose->boxMax[1], a_pose->boxMax[2] } };
        state.proofs = VehicleHits::ProofsOf(a_pose->flags);
        state.frame = VehicleHits::FrameOf(a_pose->vehicleFrame, V(a_feet));
    }

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, const Protocol::PlayerState* a_pose, bool a_active)
    {
        const bool inVehicle = a_active && a_pose && (a_pose->flags & Protocol::kPlayerInVehicle) && a_pose->vehicleModel && state.drawn && VehicleHits::HasBox(state.box);
        state.drawn = false;  // DrawVehicles tells it again next frame
        if (!inVehicle) {
            if (g_active.load()) {
                SKSE::log::info("hitboxes: CJ is out of the vehicle ({} hit(s) sent so far: {} Skyrim's own, {} into the box, {} swings; {} arrow(s) stopped)", state.sent,
                    state.byEvent, state.byBox, state.bySwing, state.stopped);
            }
            Forget();
            return;
        }
        state.cover = VehicleHits::CoverOf(VehicleKinds::KindOf(a_pose->vehicleModel));
        g_active.store(true);
        const double now = NowSeconds();

        std::vector<Reported> reported;
        const auto            riding = Riders::Riding();
        {
            std::lock_guard guard(state.lock);
            reported.swap(state.reported);
            state.riding = riding;
        }
        CoverRiders(riding);
        for (const auto& hit : reported) {
            auto*      attacker = RE::TESForm::LookupByID<RE::Actor>(hit.attacker);
            const auto from = attacker ? ChestOf(attacker) : Ni(state.frame.pos);
            std::erase_if(state.swings, [&](const Swing& a_swing) { return a_swing.attacker == hit.attacker; });  // Skyrim's own knows better
            state.byEvent++;
            Count(hit.attacker, hit.blow, Ni(VehicleHits::Nearest(state.box, state.frame, V(from))), a_anchor, a_handover, now, "Skyrim's own hit");
        }
        Scan(a_player, a_anchor, a_handover, now);
        Swings(a_player, now);
        for (auto it = state.swings.begin(); it != state.swings.end();) {
            if (it->due > now) {
                ++it;
                continue;
            }
            state.bySwing++;
            Count(it->attacker, it->blow, Ni(VehicleHits::Nearest(state.box, state.frame, V(it->from))), a_anchor, a_handover, now, "a swing at it");
            it = state.swings.erase(it);
        }
    }

    float CjShare(const Protocol::PlayerState* a_pose)
    {
        float least = 1.0f;
        {
            std::lock_guard guard(state.lock);
            least = std::exchange(state.cjLeast, 1.0f);
        }
        if (!g_active.load() || !a_pose || !(a_pose->flags & Protocol::kPlayerInVehicle)) {
            return 1.0f;
        }
        const float share = VehicleHits::SplitOf(VehicleHits::CoverOf(VehicleKinds::KindOf(a_pose->vehicleModel)), VehicleHits::Attack::Melee, state.proofs).cj;
        return (std::min)(share, least);
    }

    bool Shields() { return g_active.load(); }

    void Clear()
    {
        Forget();
        state.drawn = false;
        state.gate.Clear();
    }
}
