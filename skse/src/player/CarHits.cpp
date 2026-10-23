#include "player/CarHits.h"

#include "body.h"
#include "link/LinkService.h"
#include "player/Riders.h"
#include "player/Shots.h"
#include "run_over.h"

#include <unordered_map>

namespace AhShitRim::CarHits
{
    namespace
    {
        constexpr std::size_t kQueueMax = 64;
        constexpr int         kLoggedHits = 12;      // the first hits are logged in full
        constexpr float       kKnockBehind = 60.0f;  // Skyrim units behind the actor's middle the knock comes from

        struct Pending
        {
            Protocol::ImpactEvent impact;
            double                dueMs = 0.0;
        };

        struct Seen
        {
            RE::NiPoint3 at;
            double       ms = 0.0;
            RE::NiPoint3 velocity;  // units a second, from its last two positions
        };

        struct State
        {
            std::vector<Pending>                     queue;
            std::unordered_map<RE::FormID, Seen>     seen;
            bool                                     sending = false;  // the slot holds proxies
            std::uint32_t                            frame = 0;
            int                                      logged = 0;
            unsigned                                 hits = 0, stale = 0, gone = 0;
        } state;

        double NowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        RunOver::Build BuildOf(RE::Actor* a_actor)
        {
            auto* race = a_actor->GetRace();
            if (!race) {
                return RunOver::Build::Beast;
            }
            const char* id = race->GetFormEditorID();
            return RunOver::BuildOf(id ? id : "", race->HasKeywordString("ActorTypeNPC"), race->HasKeywordString("ActorTypeDragon"));
        }

        // Its velocity, from where it was the last time it was seen (Skyrim's own linear velocity is the
        // character controller's, in Havok's units).
        RE::NiPoint3 VelocityOf(RE::Actor* a_actor, double a_nowMs)
        {
            const auto at = a_actor->GetPosition();
            auto [it, fresh] = state.seen.try_emplace(a_actor->GetFormID(), Seen{ at, a_nowMs, {} });
            Seen& seen = it->second;
            if (!fresh && a_nowMs > seen.ms) {
                const double dt = (a_nowMs - seen.ms) / 1000.0;
                const auto   v = (at - seen.at) / static_cast<float>(dt);
                seen.velocity = dt < 0.5 && v.Length() < 50.0f * RunOver::kMeter ? v : RE::NiPoint3();
                seen.at = at;
                seen.ms = a_nowMs;
            }
            return seen.velocity;
        }

        // Who San Andreas tests CJ's vehicle against: the living, loaded actors nearest the player, not its
        // passengers, nor children (Skyrim never lets them come to harm), nor anyone riding (the mount is hit).
        void SendProxies(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            const double                 now = NowMs();
            const auto                   centre = a_player->GetPosition();
            std::vector<RE::Actor*>      actors;
            std::vector<RunOver::Candidate> candidates;
            for (auto& handle : lists->highActorHandles) {
                auto  ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == a_player || actor->IsDead() || !actor->Is3DLoaded() || actor->IsChild() || actor->IsOnMount() ||
                    Riders::Rides(actor->GetFormID())) {
                    continue;
                }
                actors.push_back(actor);
                candidates.push_back({ actor->GetFormID(), actor->GetPosition().GetDistance(centre) });
            }
            Protocol::ProxyState out{};
            out.frame = ++state.frame;
            out.handover = a_handover;
            for (const auto i : RunOver::Nearest(candidates)) {
                auto*       actor = actors[i];
                const auto  build = BuildOf(actor);
                const auto  shape = RunOver::ShapeOf(build, actor->GetScale(), actor->GetHeight());
                const auto  feet = actor->GetPosition() - a_anchor;
                const auto  capsule = RunOver::Place(shape, { feet.x, feet.y, feet.z }, actor->GetAngleZ());
                const auto  velocity = VelocityOf(actor, now);
                auto&       proxy = out.proxies[out.count++];
                proxy.id = actor->GetFormID();
                proxy.flags = static_cast<std::uint8_t>((shape.heavy ? Protocol::kProxyHeavy : 0) | (build == RunOver::Build::Person ? Protocol::kProxyPerson : 0));
                proxy.a[0] = capsule.a.x;
                proxy.a[1] = capsule.a.y;
                proxy.a[2] = capsule.a.z;
                proxy.b[0] = capsule.b.x;
                proxy.b[1] = capsule.b.y;
                proxy.b[2] = capsule.b.z;
                proxy.radius = capsule.radius;
                proxy.mass = shape.mass;
                proxy.velocity[0] = velocity.x;
                proxy.velocity[1] = velocity.y;
                proxy.velocity[2] = velocity.z;
            }
            LinkService::Get().WriteProxies(out);
            state.sending = true;
            // Forget the ones not seen for a while.
            std::erase_if(state.seen, [&](const auto& a_entry) { return now - a_entry.second.ms > 2000.0; });
        }

        void Play(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::ImpactEvent& a_impact)
        {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_impact.id);
            if (!actor || actor->IsDead() || !actor->Is3DLoaded()) {
                state.gone++;
                return;
            }
            state.hits++;
            const bool         heavy = (a_impact.flags & Protocol::kImpactHeavy) != 0;
            const RE::NiPoint3 at = a_anchor + RE::NiPoint3(a_impact.at[0], a_impact.at[1], a_impact.at[2]);
            RE::NiPoint3       push(a_impact.push[0], a_impact.push[1], a_impact.push[2]);
            const float        speed = push.Length() * Pose::kMetersPerUnit;
            RE::NiPoint3       dir = push;
            if (dir.Unitize() < 1e-3f) {
                dir = actor->GetPosition() - at;
                dir.z = 0.0f;
                if (dir.Unitize() < 1e-3f) {
                    dir = RE::NiPoint3(0.0f, 0.0f, 1.0f);
                }
            }
            const float before = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            Shots::Hurt(a_player, actor, RunOver::kRammedByCar, a_impact.damage, at, dir, true);
            const float knock = heavy || Shots::ClassOf(actor) != Damage::Class::Normal ? 0.0f : RunOver::KnockOf(speed);
            if (knock > 0.0f) {
                if (auto* process = actor->GetActorRuntimeData().currentProcess) {
                    const auto middle = actor->GetPosition() + RE::NiPoint3(0.0f, 0.0f, 0.5f * actor->GetHeight());
                    process->KnockExplosion(actor, middle - dir * kKnockBehind, knock);
                }
            }
            if (state.logged < kLoggedHits) {
                state.logged++;
                const char* race = actor->GetRace() ? actor->GetRace()->GetFormEditorID() : "";
                SKSE::log::info("car hits: hit {} ({:08X}, level {}, {}) at {:.1f} m/s{}: San Andreas's {:.0f}, health {:.1f} -> {:.1f}, thrown at {:.1f} m/s (knock {:.1f})",
                    actor->GetName(), actor->GetFormID(), actor->GetLevel(), race ? race : "?", a_impact.speed, heavy ? ", heavy: the vehicle stopped" : "",
                    a_impact.damage, before, actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth), speed, knock);
            }
        }
    }

    void Queue(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        if (a_size != sizeof(Protocol::ImpactEvent)) {
            SKSE::log::warn("car hits: an impact of {} bytes (expected {}), ignored", a_size, sizeof(Protocol::ImpactEvent));
            return;
        }
        if (state.queue.size() >= kQueueMax) {
            return;
        }
        Pending pending;
        std::memcpy(&pending.impact, a_data, sizeof(pending.impact));
        // San Andreas is a little ahead of what Skyrim draws (Body::Interpolator's delay): the hit lands when the
        // vehicle is seen to touch.
        pending.dueMs = NowMs() + Body::Interpolator::kDelayMs;
        state.queue.push_back(pending);
    }

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, bool a_inVehicle)
    {
        if (a_inVehicle) {
            SendProxies(a_player, a_anchor, a_handover);
        } else if (state.sending) {
            LinkService::Get().WriteProxies(Protocol::ProxyState{});
            state.sending = false;
            state.seen.clear();
        }
        const double now = NowMs();
        std::vector<Pending> later;
        for (const auto& pending : state.queue) {
            if (pending.impact.handover != a_handover) {
                state.stale++;
            } else if (pending.dueMs > now) {
                later.push_back(pending);
            } else {
                Play(a_player, a_anchor, pending.impact);
            }
        }
        state.queue = std::move(later);
    }

    void Clear()
    {
        if (state.sending) {
            LinkService::Get().WriteProxies(Protocol::ProxyState{});
            state.sending = false;
        }
        state.queue.clear();
        state.seen.clear();
    }
}
