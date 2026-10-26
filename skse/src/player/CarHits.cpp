#include "player/CarHits.h"

#include "body.h"
#include "link/LinkService.h"
#include "melee.h"
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
        constexpr int         kDiveWaitFrames = 30;  // frames an actor the horn knocked gets to go ragdoll
        constexpr int         kDiveFrames = 3;       // frames its ragdoll is thrown for (the first settle eats one)
        constexpr int         kLoggedDives = 4;      // the first few divers of each honk are logged

        // An actor the horn knocked down, whose ragdoll is to be thrown aside, in Havok units a second.
        struct Dive
        {
            RE::ActorHandle actor;
            RE::hkVector4   velocity;
            int             wait = kDiveWaitFrames;
            int             frames = kDiveFrames;
        };

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
            RunOver::HornPulse                       horn;
            std::vector<Dive>                        dives;
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
        // On foot (a_onFoot), who CJ bumps into: the nearest within Melee::kFootRange, children too, followers
        // not (they'd hem him in), and a_target, his lock-on, flagged among them.
        void SendProxies(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, bool a_onFoot, RE::FormID a_target)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            const double                 now = NowMs();
            const auto                   centre = a_player->GetPosition();
            std::vector<RE::Actor*>      actors;
            std::vector<RunOver::Candidate> candidates;
            std::size_t                     target = SIZE_MAX;
            for (auto& handle : lists->highActorHandles) {
                auto  ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == a_player || actor->IsDead() || !actor->Is3DLoaded() || actor->IsOnMount()) {
                    continue;
                }
                if (a_onFoot ? actor->IsPlayerTeammate() || actor->IsCommandedActor() : actor->IsChild() || Riders::Rides(actor->GetFormID())) {
                    continue;
                }
                if (actor->GetFormID() == a_target) {
                    target = actors.size();
                }
                actors.push_back(actor);
                candidates.push_back({ actor->GetFormID(), actor->GetPosition().GetDistance(centre) });
            }
            Protocol::ProxyState out{};
            out.frame = ++state.frame;
            out.handover = a_handover;
            for (const auto i : a_onFoot ? Melee::OnFoot(candidates, target) : RunOver::Nearest(candidates)) {
                auto*       actor = actors[i];
                const auto  build = BuildOf(actor);
                const auto  shape = RunOver::ShapeOf(build, actor->GetScale(), actor->GetHeight());
                const auto  feet = actor->GetPosition() - a_anchor;
                const auto  capsule = RunOver::Place(shape, { feet.x, feet.y, feet.z }, actor->GetAngleZ());
                const auto  velocity = VelocityOf(actor, now);
                auto&       proxy = out.proxies[out.count++];
                proxy.id = actor->GetFormID();
                proxy.flags = static_cast<std::uint8_t>((shape.heavy ? Protocol::kProxyHeavy : 0) | (build == RunOver::Build::Person ? Protocol::kProxyPerson : 0) |
                                                        (i == target ? Protocol::kProxyTarget : 0));
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

        // Whether the horn can make it dive: standing on its own feet (not ragdolled, knocked down, bleeding out, sitting,
        // sleeping, dead or riding), and not the player, a passenger of CJ's or a child (Skyrim never lets them come to harm).
        bool Standing(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
        {
            if (!a_actor || a_actor == a_player || a_actor->IsDead() || a_actor->IsDisabled() || !a_actor->Is3DLoaded() || a_actor->IsInRagdollState() ||
                a_actor->IsOnMount() || a_actor->IsChild() || Riders::Rides(a_actor->GetFormID())) {
                return false;
            }
            auto* actorState = a_actor->AsActorState();
            return !actorState || (actorState->GetKnockState() == RE::KNOCK_STATE_ENUM::kNormal && !actorState->IsBleedingOut() &&
                                      actorState->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal);
        }

        // Sets every rigid body of the actor's ragdoll moving at a_velocity.
        void ThrowRagdoll(RE::Actor* a_actor, const RE::hkVector4& a_velocity)
        {
            if (auto* root = a_actor->Get3D(false)) {
                RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) {
                    if (auto* body = a_object->body ? skyrim_cast<RE::bhkRigidBody*>(a_object->body.get()) : nullptr) {
                        body->SetLinearVelocity(a_velocity);
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            }
        }

        // Each frame: the horn's divers that went ragdoll are thrown aside, for a few frames.
        void Throw()
        {
            for (auto it = state.dives.begin(); it != state.dives.end();) {
                auto actor = it->actor.get();
                bool done = !actor;
                if (actor && actor->IsInRagdollState()) {
                    ThrowRagdoll(actor.get(), it->velocity);
                    done = --it->frames <= 0;
                } else if (actor && --it->wait <= 0) {
                    done = true;  // it never went ragdoll
                }
                it = done ? state.dives.erase(it) : it + 1;
            }
        }

        void Play(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::ImpactEvent& a_impact)
        {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_impact.id);
            if (!actor || actor->IsDead() || !actor->Is3DLoaded()) {
                state.gone++;
                return;
            }
            state.hits++;
            const bool         wall = (a_impact.flags & Protocol::kImpactHeavy) != 0;
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
            const float knock = RunOver::KnockFor(speed, wall, RunOver::Heavy(BuildOf(actor)), Shots::ClassOf(actor) == Damage::Class::Normal);
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
                    actor->GetName(), actor->GetFormID(), actor->GetLevel(), race ? race : "?", a_impact.speed, wall ? ", a wall: the vehicle stopped" : "",
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

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, bool a_inVehicle, bool a_onFoot, RE::FormID a_target)
    {
        if (a_inVehicle || a_onFoot) {
            SendProxies(a_player, a_anchor, a_handover, !a_inVehicle, a_target);
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

    void Horn(RE::PlayerCharacter* a_player, bool a_held, const RE::NiPoint3& a_vehicleAt, const RE::NiPoint3& a_velocity, float a_heading)
    {
        Throw();
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!state.horn.Step(a_held, NowMs() / 1000.0) || !lists) {
            return;
        }
        const auto way = RunOver::HornWay(a_velocity.x, a_velocity.y, a_heading);
        int        scared = 0;
        for (auto& handle : lists->highActorHandles) {
            auto actor = handle.get();
            if (!Standing(actor.get(), a_player)) {
                continue;
            }
            const auto  pos = actor->GetPosition();
            const float dx = pos.x - a_vehicleAt.x, dy = pos.y - a_vehicleAt.y;
            if (!RunOver::InHornPath(dx, dy, pos.z - a_vehicleAt.z, way[0], way[1]) || RunOver::Heavy(BuildOf(actor.get()))) {
                continue;
            }
            // A dive already asked for (not a ragdoll yet, so still standing) isn't asked again.
            const bool diving = std::any_of(state.dives.begin(), state.dives.end(), [&](const Dive& a_dive) { return a_dive.actor.get().get() == actor.get(); });
            auto*      process = actor->GetActorRuntimeData().currentProcess;
            if (!diving && process) {
                const auto  move = RunOver::HornDodge(dx, dy, way[0], way[1]);
                const float length = std::hypot(move[0], move[1]);
                // Knocked from its side nearest the vehicle's line, then thrown over the move in kHornDiveSeconds
                // (Havok's units are metres, near enough).
                RE::NiPoint3 from = pos;
                from.x -= move[0] / length * 100.0f;
                from.y -= move[1] / length * 100.0f;
                process->KnockExplosion(actor.get(), from, RunOver::kHornDiveKnock);
                const float perSecond = Pose::kMetersPerUnit / RunOver::kHornDiveSeconds;
                state.dives.push_back({ actor->CreateRefHandle(), RE::hkVector4(move[0] * perSecond, move[1] * perSecond, RunOver::kHornDiveLift, 0.0f) });
            }
            if (scared++ < kLoggedDives) {
                SKSE::log::info("car hits: the horn makes {} ({:08X}) dive aside, {:.0f} units away{}", actor->GetName(), actor->GetFormID(), pos.GetDistance(a_vehicleAt),
                    diving ? " (already diving)" : "");
            }
        }
        if (scared > 0) {
            SKSE::log::info("car hits: CJ honked at {:.0f} km/h, {} in the way", std::hypot(a_velocity.x, a_velocity.y) * Pose::kMetersPerUnit * 3.6f, scared);
        }
    }

    void Clear()
    {
        state.dives.clear();
        if (state.sending) {
            LinkService::Get().WriteProxies(Protocol::ProxyState{});
            state.sending = false;
        }
        state.queue.clear();
        state.seen.clear();
    }
}
