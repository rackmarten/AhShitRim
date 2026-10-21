#include "player/Projectiles.h"

#include "body/ModelView.h"
#include "explosives.h"
#include "link/LinkService.h"
#include "player/Shots.h"
#include "player/Vitals.h"

#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <set>
#include <vector>

namespace AhShitRim::Projectiles
{
    using namespace Explosives;

    namespace
    {
        constexpr std::size_t kQueueMax = 64;
        constexpr std::size_t kFlightMax = 32;   // San Andreas's own limit
        constexpr int         kPassMax = 8;      // things a step goes through (the player, the dead, clutter)
        constexpr float       kStepMax = 1.0f / 30.0f;  // seconds a flight step covers at most
        constexpr float       kSearch = 1500.0f;  // actors this close to a blast or a cloud are looked at
        constexpr float       kGasTick = 0.5f;    // seconds between the gas's chokes
        constexpr float       kFlameEvery = 0.05f;  // seconds between the flames the flamethrower shoots
        constexpr float       kBurnEvery = 1.0f;    // seconds between setting one actor burning again
        constexpr float       kActorRadiusMin = 20.0f;
        constexpr int         kLogged = 12;

        // Vanilla forms (Skyrim.esm).
        constexpr RE::FormID kBlastExplosion = 0x000439C0;    // FireBallExp01: the fireball's blast, no damage of its own
        constexpr float      kMolotovScale = 2.0f;            // San Andreas's Molotov fireball is twice a rocket's
        constexpr const char* kFireModel = "Effects\\FXFireOilHazard.nif";  // TrapOilHazard01's burning oil, drawn alone
        constexpr const char* kCjFireModel = "Effects\\FXFire02New.nif";   // a small fire (FireSmLongHazard's), on CJ while he burns
        constexpr float       kCjFireEvery = 0.2f;  // seconds between the flames put on CJ
        constexpr RE::FormID kGasHazard = 0x00106380;         // DA13PustuleGasCloud: a gas cloud that hurts no one itself
        constexpr RE::FormID kFlames = 0x00012FCD;            // Flames

        struct Projectile
        {
            Kind                                                         kind = Kind::None;
            std::uint8_t                                                 type = 0;  // San Andreas's projectile type
            Flight                                                       body;
            Quat                                                         turn;
            float                                                        age = 0.0f;
            float                                                        fuse = 0.0f;
            bool                                                         resting = false;
            std::uint32_t                                                id = 0;  // San Andreas's, for the Effect that says it's over
            std::uint32_t                                                model = 0;
            std::uint32_t                                                partCount = 0;
            std::array<Protocol::BodyBone, Protocol::kProjectilePartMax> parts{};
        };

        struct Fire
        {
            RE::NiPoint3 at;
            float        start = 0.0f;  // seconds until it catches
            float        left = kFireSeconds;
            float        tick = 0.0f;
            bool         caught = false;
            std::uint8_t type = 0;
        };

        struct Burning
        {
            double cast = -1e9;   // when the Flames were last cast on it
            double until = 0.0;   // when it stops burning
        };

        struct Gas
        {
            RE::NiPoint3       at;
            float              left = kGasSeconds;
            float              tick = 0.0f;
            float              cough = 0.0f;
            RE::ObjectRefHandle cloud;
        };

        struct State
        {
            std::vector<Protocol::ProjectileEvent> queue;
            std::deque<Projectile>                 flights;  // oldest first
            std::vector<Gas>                       gases;
            std::vector<Fire>                      fires;
            float                                  cjFire = 0.0f;  // seconds since the last flames on CJ
            Damage::Blows                          flames;   // the flamethrower's cuts, as the chainsaw's
            std::map<RE::FormID, Burning>          burning;
            float                                  burnTick = 0.0f;
            double                                 lastFlame = 0.0;
            RE::BGSExplosion*                      blast = nullptr;
            RE::BGSHazard*                         gas = nullptr;
            RE::SpellItem*                         flamesSpell = nullptr;
            int                                    logged = 0;
            unsigned                               stale = 0;
        } state;

        double NowSeconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        RE::NiPoint3 Ni(const Vec3& a_v) { return { a_v.x, a_v.y, a_v.z }; }
        Vec3         V(const RE::NiPoint3& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        template <class T>
        T* Lookup(RE::FormID a_id, const char* a_what)
        {
            auto* form = RE::TESForm::LookupByID<T>(a_id);
            SKSE::log::info("explosives: {} {:08X}: {}", a_what, a_id, form ? "found" : "not found, left out");
            return form;
        }

        void Log(const std::string& a_line)
        {
            if (state.logged < kLogged) {
                state.logged++;
                SKSE::log::info("explosives: {}", a_line);
            }
        }

        // ---- San Andreas's side of it ----

        // San Andreas plays its own sound and fire for what CJ's explosives do (Effect messages).
        void Tell(Protocol::EffectKind a_kind, std::uint32_t a_handover, const RE::NiPoint3& a_anchor, const RE::NiPoint3& a_at, std::uint8_t a_type,
            std::uint32_t a_projectile = 0, float a_seconds = 0.0f)
        {
            Protocol::EffectEvent effect{};
            effect.handover = a_handover;
            effect.kind = static_cast<std::uint8_t>(a_kind);
            effect.weaponType = a_type;
            effect.projectile = a_projectile;
            const auto at = a_at - a_anchor;
            effect.at[0] = at.x;
            effect.at[1] = at.y;
            effect.at[2] = at.z;
            effect.seconds = a_seconds;
            LinkService::Get().Send(Protocol::MessageType::Effect, &effect, sizeof(effect));
        }

        struct Frame
        {
            std::uint32_t handover = 0;
            RE::NiPoint3  anchor;
        } frame;

        // A vanilla explosion made again without its sounds: San Andreas's own play instead.
        RE::BGSExplosion* Silent(RE::BGSExplosion* a_vanilla)
        {
            auto* factory = a_vanilla ? RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSExplosion>() : nullptr;
            auto* copy = factory ? factory->Create() : nullptr;
            if (!copy) {
                SKSE::log::warn("explosives: {:08X} couldn't be copied, its own sound stays", a_vanilla ? a_vanilla->GetFormID() : 0);
                return a_vanilla;
            }
            copy->SetModel(a_vanilla->GetModel());
            copy->boundData = a_vanilla->boundData;
            copy->imageSpaceModifying = a_vanilla->imageSpaceModifying;
            copy->data = a_vanilla->data;
            copy->data.sound1 = nullptr;
            copy->data.sound2 = nullptr;
            return copy;
        }

        // ---- Placing Skyrim's own ----

        RE::ObjectRefHandle Place(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_form, const RE::NiPoint3& a_at)
        {
            auto* handler = RE::TESDataHandler::GetSingleton();
            auto* cell = a_player->GetParentCell();
            if (!a_form || !handler || !cell) {
                return {};
            }
            return handler->CreateReferenceAtLocation(a_form, a_at, RE::NiPoint3(), cell, a_player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(),
                false, false);
        }

        void Remove(RE::ObjectRefHandle& a_handle)
        {
            if (auto ref = a_handle.get()) {
                ref->Disable();
                ref->SetDelete(true);
            }
            a_handle = {};
        }

        void CastFlames(RE::PlayerCharacter* a_player, RE::Actor* a_actor, Burning& a_burning, double a_now)
        {
            if (!state.flamesSpell || a_now - a_burning.cast < kBurnEvery) {
                return;
            }
            a_burning.cast = a_now;
            if (auto* caster = a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
                caster->CastSpellImmediate(state.flamesSpell, false, a_actor, 1.0f, false, 0.0f, a_player);
            }
        }

        // Sets a_actor burning for kBurnSeconds from now (the Flames spell, cast by the player, shows it).
        void SetBurning(RE::PlayerCharacter* a_player, RE::Actor* a_actor, double a_now)
        {
            if (a_actor->IsDead()) {
                return;
            }
            auto& burning = state.burning[a_actor->GetFormID()];
            burning.until = a_now + kBurnSeconds;
            CastFlames(a_player, a_actor, burning, a_now);
        }

        // Everyone burning burns on, hurt a little and set alight again, until their time is up.
        void Afterburn(RE::PlayerCharacter* a_player, float a_dt)
        {
            state.burnTick += a_dt;
            if (state.burnTick < kGasTick) {
                return;
            }
            state.burnTick -= kGasTick;
            const double now = NowSeconds();
            for (auto it = state.burning.begin(); it != state.burning.end();) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
                if (!actor || actor->IsDead() || now >= it->second.until) {
                    it = state.burning.erase(it);
                    continue;
                }
                Shots::Hurt(a_player, actor, Weapons::Molotov, kBurnPerSecond * kGasTick, actor->GetPosition(), RE::NiPoint3(0.0f, 0.0f, 1.0f), false);
                CastFlames(a_player, actor, it->second, now);
                ++it;
            }
        }

        // Skyrim's own stagger, as a shield bash or a shout starts it (a cough).
        void Stagger(RE::Actor* a_actor, const RE::NiPoint3& a_from)
        {
            const auto  at = a_actor->GetPosition();
            const float heading = std::atan2(a_from.x - at.x, a_from.y - at.y);
            a_actor->SetGraphVariableFloat("staggerDirection", Damage::StaggerDirection(a_actor->GetAngleZ(), heading));
            a_actor->SetGraphVariableFloat("staggerMagnitude", 0.3f);
            a_actor->NotifyAnimationGraph("staggerStart");
        }

        // ---- Who is in reach ----

        float RadiusOf(RE::Actor* a_actor)
        {
            const auto  min = a_actor->GetBoundMin();
            const auto  max = a_actor->GetBoundMax();
            const float half = 0.5f * (std::min)(max.x - min.x, max.y - min.y) * a_actor->GetScale();
            return (std::max)(kActorRadiusMin, std::isfinite(half) ? half : 0.0f);
        }

        // How far a_point is from an actor's body (a capsule from its feet up its height), and the closest point.
        float DistanceTo(RE::Actor* a_actor, const RE::NiPoint3& a_point, RE::NiPoint3* a_closest = nullptr)
        {
            const auto  feet = a_actor->GetPosition();
            const float height = (std::max)(a_actor->GetHeight(), 1.0f);
            const RE::NiPoint3 closest(feet.x, feet.y, std::clamp(a_point.z, feet.z, feet.z + height));
            if (a_closest) {
                *a_closest = closest;
            }
            return (std::max)(0.0f, closest.GetDistance(a_point) - RadiusOf(a_actor));
        }

        template <class F>
        void ForActorsNear(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, F&& a_each)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            std::vector<RE::NiPointer<RE::Actor>> nearby;
            for (auto& handle : lists->highActorHandles) {
                auto ptr = handle.get();
                if (ptr && ptr.get() != a_player && ptr->Is3DLoaded() && ptr->GetPosition().GetDistance(a_at) <= kSearch) {
                    nearby.push_back(ptr);
                }
            }
            for (auto& actor : nearby) {
                a_each(actor.get());
            }
        }

        // ---- Locks ----

        void Unlock(RE::TESObjectREFR* a_ref)
        {
            if (auto* lock = a_ref->GetLock()) {
                lock->SetLocked(false);
                a_ref->AddChange(RE::TESObjectREFR::ChangeFlags::kLockExtra);
            }
        }

        // A blast opens the locked doors and containers close by: a crime where picking the lock would be one
        // (owned, and someone sees it), and the other side of a load door with it.
        void OpenLocks(RE::PlayerCharacter* a_player, Kind a_kind, const RE::NiPoint3& a_at, RE::TESObjectREFR* a_origin)
        {
            if (!BlastOf(a_kind).opensLocks) {
                return;
            }
            std::vector<RE::TESObjectREFR*> opened;
            const auto each = [&](RE::TESObjectREFR* a_ref) {
                auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
                auto* lock = base && (base->Is(RE::FormType::Door) || base->Is(RE::FormType::Container)) ? a_ref->GetLock() : nullptr;
                if (lock && OpensLock(a_kind, a_ref->GetPosition().GetDistance(a_at), lock->IsLocked(), lock->GetLockLevel(a_ref) == RE::LOCK_LEVEL::kRequiresKey)) {
                    opened.push_back(a_ref);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            };
            if (a_origin) {
                RE::TES::GetSingleton()->ForEachReferenceInRange(a_origin, kLockRadius, each);
            } else if (auto* cell = a_player->GetParentCell()) {
                cell->ForEachReferenceInRange(a_at, kLockRadius, each);
            }
            for (auto* ref : opened) {
                const bool crime = ref->IsCrimeToActivate();
                Unlock(ref);
                if (auto* teleport = ref->extraList.GetByType<RE::ExtraTeleport>(); teleport && teleport->teleportData) {
                    if (auto other = teleport->teleportData->linkedDoor.get()) {
                        Unlock(other.get());
                    }
                }
                auto* owner = ref->GetOwner();
                if (!owner && ref->GetParentCell()) {
                    owner = ref->GetParentCell()->GetOwner();
                }
                if (crime && owner) {
                    a_player->TrespassAlarm(ref, owner, RE::PackageNS::CRIME_TYPES::kTrespass);
                }
                SKSE::log::info("explosives: a blast opened {} ({:08X}){}", ref->GetDisplayFullName(), ref->GetFormID(), crime ? ", a crime if seen" : "");
            }
        }

        // ---- Going off ----

        // Returns what CJ took.
        float Explode(RE::PlayerCharacter* a_player, Kind a_kind, std::uint8_t a_weapon, const RE::NiPoint3& a_at)
        {
            const double now = NowSeconds();
            int          caught = 0;
            ForActorsNear(a_player, a_at, [&](RE::Actor* a_actor) {
                RE::NiPoint3 closest;
                const float  distance = DistanceTo(a_actor, a_at, &closest);
                const float  damage = BlastDamage(a_kind, distance);
                if (!(damage > 0.0f)) {
                    return;
                }
                caught++;
                RE::NiPoint3 dir = closest - a_at;
                if (dir.Unitize() < 1e-3f) {
                    dir = RE::NiPoint3(0.0f, 0.0f, 1.0f);
                }
                Shots::Hurt(a_player, a_actor, a_weapon, damage, closest, dir, false);
                if (a_kind == Kind::Molotov) {
                    SetBurning(a_player, a_actor, now);
                }
                const float knock = KnockMagnitude(a_kind, distance, Shots::ClassOf(a_actor));
                if (knock > 0.0f) {
                    if (auto* process = a_actor->GetActorRuntimeData().currentProcess) {
                        process->KnockExplosion(a_actor, a_at, knock);
                    }
                }
            });
            // CJ in his own blast.
            const float self = BlastDamage(a_kind, DistanceTo(a_player, a_at));
            if (self > 0.0f) {
                Vitals::Hurt(a_player, self);
            }
            Log(std::format("{} went off, {} caught, CJ took {:.0f}", a_weapon, caught, self));
            return self;
        }

        void GoOff(RE::PlayerCharacter* a_player, const Projectile& a_p, const RE::NiPoint3& a_at)
        {
            const auto weapon = WeaponOf(a_p.type);
            switch (a_p.kind) {
            case Kind::Rocket:
            case Kind::Grenade:
                {
                    Tell(Protocol::EffectKind::Explosion, frame.handover, frame.anchor, a_at, a_p.type);
                    auto explosion = Place(a_player, state.blast, a_at);
                    Explode(a_player, a_p.kind, weapon, a_at);
                    OpenLocks(a_player, a_p.kind, a_at, explosion.get().get());
                    break;
                }
            case Kind::Molotov:
                {
                    Tell(Protocol::EffectKind::Explosion, frame.handover, frame.anchor, a_at, a_p.type, a_p.id);  // its bottle smashes there
                    if (auto ball = Place(a_player, state.blast, a_at).get()) {
                        ball->SetScale(kMolotovScale);
                    }
                    for (int i = 0; i < kFirePatchCount; ++i) {
                        const auto patch = FirePatch(i);
                        Fire       fire;
                        fire.at = a_at + Ni(patch.offset);
                        fire.start = patch.start;
                        fire.left = kFireSeconds - patch.start;
                        fire.type = a_p.type;
                        state.fires.push_back(fire);
                    }
                }
                if (Explode(a_player, a_p.kind, weapon, a_at) > 0.0f) {
                    Tell(Protocol::EffectKind::BurnCj, frame.handover, frame.anchor, a_at, a_p.type);
                }
                break;
            case Kind::TearGas:
                {
                    Gas gas;
                    gas.at = a_at;
                    gas.cloud = Place(a_player, state.gas, a_at);
                    state.gases.push_back(gas);
                    Log(std::format("tear gas let out{}", gas.cloud ? "" : " (no cloud to show)"));
                    break;
                }
            default: break;
            }
        }

        // ---- Flight ----

        struct Contact
        {
            bool         hit = false;
            RE::Actor*   actor = nullptr;
            RE::NiPoint3 at;
            RE::NiPoint3 normal;
        };

        bool Stops(RE::COL_LAYER a_layer)
        {
            switch (a_layer) {
            case RE::COL_LAYER::kStatic:
            case RE::COL_LAYER::kAnimStatic:
            case RE::COL_LAYER::kTransparent:
            case RE::COL_LAYER::kTrees:
            case RE::COL_LAYER::kProps:
            case RE::COL_LAYER::kWater:
            case RE::COL_LAYER::kTerrain:
            case RE::COL_LAYER::kTrap:
            case RE::COL_LAYER::kGround: return true;
            default: return false;
            }
        }

        // What the segment a_from to a_to runs into first, as a bullet would: statics, the land, water and
        // living actors (not the player, the dead or clutter).
        Contact Segment(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
        {
            Contact contact;
            auto*   cell = a_player->GetParentCell();
            auto*   bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto*   world = bhk ? bhk->GetWorld1() : nullptr;
            RE::NiPoint3 dir = a_to - a_from;
            const float  length = dir.Unitize();
            if (!world || !(length > 1e-3f)) {
                return contact;
            }
            const float              scale = RE::bhkWorld::GetWorldScale();
            RE::hkpWorldRayCastInput input;
            input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kProjectile);
            input.to = RE::hkVector4(a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f);
            RE::BSReadLockGuard lock(bhk->worldLock);
            float               from = 0.0f;
            for (int pass = 0; pass < kPassMax && from < length; ++pass) {
                const RE::NiPoint3 start = a_from + dir * from;
                input.from = RE::hkVector4(start.x * scale, start.y * scale, start.z * scale, 0.0f);
                RE::hkpWorldRayCastOutput output;
                world->CastRay(input, output);
                if (!output.HasHit()) {
                    break;
                }
                const float distance = from + (length - from) * output.hitFraction;
                if (distance > length) {
                    break;
                }
                auto*       ref = RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable);
                auto*       actor = ref ? ref->As<RE::Actor>() : nullptr;
                if (ref == a_player || (actor && actor->IsDead()) || (!actor && !Stops(output.rootCollidable->GetCollisionLayer()))) {
                    from = distance + 1.0f;
                    continue;
                }
                contact.hit = true;
                contact.actor = actor;
                contact.at = a_from + dir * distance;
                contact.normal = RE::NiPoint3(output.normal.quad.m128_f32[0], output.normal.quad.m128_f32[1], output.normal.quad.m128_f32[2]);
                if (contact.normal.Unitize() < 1e-3f || actor) {
                    contact.normal = -dir;  // off an actor's capsule: straight back
                }
                break;
            }
            return contact;
        }

        bool FlyOn(RE::PlayerCharacter* a_player, Projectile& a_p, float a_dt);

        // One projectile a_dt seconds on; false once it is gone (San Andreas told, for a rocket's stand-in).
        bool Fly(RE::PlayerCharacter* a_player, Projectile& a_p, float a_dt)
        {
            const bool flying = FlyOn(a_player, a_p, a_dt);
            if (!flying && (a_p.kind == Kind::Rocket || a_p.kind == Kind::Molotov)) {
                Tell(Protocol::EffectKind::ProjectileGone, frame.handover, frame.anchor, Ni(a_p.body.position), a_p.type, a_p.id);
            }
            return flying;
        }

        bool FlyOn(RE::PlayerCharacter* a_player, Projectile& a_p, float a_dt)
        {
            a_p.age += a_dt;
            if (a_p.age >= a_p.fuse) {
                if (a_p.kind != Kind::Molotov) {
                    GoOff(a_player, a_p, Ni(a_p.body.position));
                }
                return false;
            }
            float left = a_dt;
            while (!a_p.resting && left > 0.0f) {
                const float  h = (std::min)(left, kStepMax);
                left -= h;
                const Flight next = Step(a_p.body, a_p.kind, h);
                const auto   contact = Segment(a_player, Ni(a_p.body.position), Ni(next.position));
                if (!contact.hit) {
                    a_p.body = next;
                    continue;
                }
                if (BurstsOnContact(a_p.kind)) {
                    GoOff(a_player, a_p, contact.at + contact.normal * 5.0f);
                    return false;
                }
                a_p.body.position = V(contact.at + contact.normal * 2.0f);
                a_p.body.velocity = Bounce(next.velocity, V(contact.normal));
                if (Rests(a_p.body.velocity) && contact.normal.z > 0.5f) {
                    a_p.resting = true;
                    a_p.body.velocity = {};
                }
            }
            a_p.turn = Tumble(a_p.turn, a_p.kind, a_p.body.velocity, a_dt);
            return true;
        }

        void Draw(const std::deque<Projectile>& a_flights)
        {
            std::set<std::uint32_t> shown;  // ModelView draws one of each model: the newest
            for (auto it = a_flights.rbegin(); it != a_flights.rend(); ++it) {
                if (!it->model || !it->partCount || !shown.insert(it->model).second) {
                    continue;
                }
                Protocol::BodyState pose{};
                pose.flags = Protocol::kBodyValid;
                pose.model = it->model;
                pose.boneCount = it->partCount;
                for (std::uint32_t i = 0; i < it->partCount; ++i) {
                    pose.bones[i] = PartBone(it->turn, it->parts[i]);
                }
                ModelView::Show(pose, Ni(it->body.position));
            }
        }

        void Launch(RE::PlayerCharacter* a_player, const Protocol::ProjectileEvent& a_event, const RE::NiPoint3& a_anchor)
        {
            Projectile p;
            p.type = a_event.weaponType;
            p.kind = KindOf(p.type);
            const RE::NiPoint3 at = a_anchor + RE::NiPoint3(a_event.origin[0], a_event.origin[1], a_event.origin[2]);
            if (p.kind == Kind::None || !std::isfinite(at.x + at.y + at.z)) {
                return;
            }
            if (a_event.flags & Protocol::kProjectileBurst) {
                GoOff(a_player, p, at);
                return;
            }
            p.turn = { a_event.rotation[0], a_event.rotation[1], a_event.rotation[2], a_event.rotation[3] };
            p.body.position = V(at);
            p.body.velocity = LaunchVelocity(p.kind, { a_event.velocity[0], a_event.velocity[1], a_event.velocity[2] }, Rotate(p.turn, { 0.0f, 1.0f, 0.0f }));
            p.fuse = FuseSeconds(p.kind, a_event.fuseMs);
            p.id = a_event.id;
            p.model = a_event.model;
            p.partCount = (std::min)(static_cast<std::uint32_t>(a_event.partCount), Protocol::kProjectilePartMax);
            for (std::uint32_t i = 0; i < p.partCount; ++i) {
                p.parts[i] = a_event.parts[i];
            }
            Log(std::format("projectile {} launched at {:.0f} units a second, fuse {:.1f} s", p.type, Length(p.body.velocity), p.fuse));
            if (state.flights.size() >= kFlightMax) {
                Tell(Protocol::EffectKind::ProjectileGone, frame.handover, frame.anchor, Ni(state.flights.front().body.position), state.flights.front().type,
                    state.flights.front().id);
                state.flights.pop_front();
            }
            state.flights.push_back(p);
        }

        // ---- Fire ----

        // A Molotov's patch: whoever stands in it burns (San Andreas's own fire there burns CJ).
        bool Burn(RE::PlayerCharacter* a_player, Fire& a_fire, float a_dt)
        {
            if (!a_fire.caught) {
                a_fire.start -= a_dt;
                if (a_fire.start > 0.0f) {
                    return true;
                }
                a_fire.caught = true;
                if (auto* cell = a_player->GetParentCell()) {
                    RE::BSTempEffectParticle::Spawn(cell, a_fire.left, kFireModel, RE::NiPoint3(0.0f, 0.0f, 1.0f), a_fire.at, 1.0f, 7, nullptr);
                }
                Tell(Protocol::EffectKind::Fire, frame.handover, frame.anchor, a_fire.at, a_fire.type, 0, a_fire.left);
            }
            a_fire.left -= a_dt;
            if (a_fire.left <= 0.0f) {
                return false;
            }
            a_fire.tick += a_dt;
            if (a_fire.tick < kGasTick) {
                return true;
            }
            a_fire.tick -= kGasTick;
            const double now = NowSeconds();
            ForActorsNear(a_player, a_fire.at, [&](RE::Actor* a_actor) {
                if (a_actor->IsDead() || !InFire(V(a_fire.at), V(a_actor->GetPosition()), RadiusOf(a_actor))) {
                    return;
                }
                Shots::Hurt(a_player, a_actor, Weapons::Molotov, kFirePerSecond * kGasTick, a_actor->GetPosition(), RE::NiPoint3(0.0f, 0.0f, 1.0f), false);
                SetBurning(a_player, a_actor, now);
            });
            return true;
        }

        // Flames on CJ while San Andreas has him burning: small fires at his hips and chest, over and over.
        void FlamesOnCj(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_feet, float a_dt)
        {
            state.cjFire += a_dt;
            auto* cell = a_player->GetParentCell();
            if (state.cjFire < kCjFireEvery || !cell) {
                return;
            }
            state.cjFire = 0.0f;
            for (const float height : { 60.0f, 100.0f }) {
                RE::BSTempEffectParticle::Spawn(cell, 0.5f, kCjFireModel, RE::NiPoint3(0.0f, 0.0f, 1.0f), a_feet + RE::NiPoint3(0.0f, 0.0f, height), 0.5f, 7, nullptr);
            }
        }

        // ---- Gas ----

        bool Choke(RE::PlayerCharacter* a_player, Gas& a_gas, float a_dt)
        {
            a_gas.left -= a_dt;
            if (a_gas.left <= 0.0f) {
                Remove(a_gas.cloud);
                return false;
            }
            a_gas.tick += a_dt;
            a_gas.cough += a_dt;
            if (a_gas.tick < kGasTick) {
                return true;
            }
            a_gas.tick -= kGasTick;
            const bool cough = a_gas.cough >= kGasCough;
            if (cough) {
                a_gas.cough = 0.0f;
            }
            const float damage = kGasPerSecond * kGasTick;
            ForActorsNear(a_player, a_gas.at, [&](RE::Actor* a_actor) {
                if (a_actor->IsDead() || !InGas(V(a_gas.at), V(a_actor->GetPosition()))) {
                    return;
                }
                Shots::Hurt(a_player, a_actor, Weapons::TearGas, damage, a_actor->GetPosition(), RE::NiPoint3(0.0f, 0.0f, 1.0f), false);
                if (cough && !a_actor->IsDead() && Shots::ClassOf(a_actor) == Damage::Class::Normal) {
                    Stagger(a_actor, a_gas.at);
                }
            });
            if (InGas(V(a_gas.at), V(a_player->GetPosition()))) {
                Vitals::Hurt(a_player, damage);
            }
            return true;
        }
    }

    void OnDataLoaded()
    {
        state.blast = Lookup<RE::BGSExplosion>(kBlastExplosion, "the rockets' and grenades' blast (FireBallExp01)");
        state.blast = Silent(state.blast);
        state.gas = Lookup<RE::BGSHazard>(kGasHazard, "the tear gas cloud (DA13PustuleGasCloud)");
        state.flamesSpell = Lookup<RE::SpellItem>(kFlames, "the flamethrower's flames (Flames)");
    }

    void Queue(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        if (a_size != sizeof(Protocol::ProjectileEvent)) {
            SKSE::log::warn("explosives: a projectile of {} bytes (expected {}), ignored", a_size, sizeof(Protocol::ProjectileEvent));
            return;
        }
        if (state.queue.size() >= kQueueMax) {
            return;
        }
        Protocol::ProjectileEvent event{};
        std::memcpy(&event, a_data, sizeof(event));
        state.queue.push_back(event);
    }

    void Flame(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot, const RE::NiPoint3& a_anchor)
    {
        const RE::NiPoint3 origin = a_anchor + RE::NiPoint3(a_shot.origin[0], a_shot.origin[1], a_shot.origin[2]);
        RE::NiPoint3       dir(a_shot.direction[0], a_shot.direction[1], a_shot.direction[2]);
        if (!(dir.Unitize() >= 1e-4f) || !std::isfinite(origin.x + origin.y + origin.z)) {
            return;
        }
        const float  range = std::clamp(a_shot.range, kFlameRangeMin, kFlameRangeMax);
        const double now = NowSeconds();
        // Skyrim's own flames from the nozzle, as a Flames caster's, not every puff.
        if (state.flamesSpell && now - state.lastFlame >= kFlameEvery) {
            state.lastFlame = now;
            RE::Projectile::ProjectileRot rot{ -std::asin(std::clamp(dir.z, -1.0f, 1.0f)), std::atan2(dir.x, dir.y) };
            RE::ProjectileHandle          handle;
            RE::Projectile::LaunchSpell(&handle, a_player, state.flamesSpell, origin, rot);
        }
        ForActorsNear(a_player, origin, [&](RE::Actor* a_actor) {
            const auto feet = a_actor->GetPosition();
            const RE::NiPoint3 middle(feet.x, feet.y, feet.z + 0.5f * a_actor->GetHeight());
            if (a_actor->IsDead() || !InFlame(V(origin), V(dir), range, V(middle), RadiusOf(a_actor) + 0.25f * a_actor->GetHeight())) {
                return;
            }
            const float landed = state.flames.Lands(a_actor->GetFormID(), 0, now, true);
            if (landed > 0.0f) {
                Shots::Hurt(a_player, a_actor, Weapons::Flamethrower, kFlamePerSecond * landed, middle, dir, false);
            }
            SetBurning(a_player, a_actor, now);
        });
    }

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, float a_delta, const RE::NiPoint3* a_cjBurning)
    {
        frame.handover = a_handover;
        frame.anchor = a_anchor;
        auto queue = std::move(state.queue);
        state.queue.clear();
        for (const auto& event : queue) {
            if (event.handover != a_handover) {
                state.stale++;
                continue;
            }
            Launch(a_player, event, a_anchor);
        }
        const float dt = std::clamp(a_delta, 0.0f, 0.25f);
        for (auto it = state.flights.begin(); it != state.flights.end();) {
            it = Fly(a_player, *it, dt) ? it + 1 : state.flights.erase(it);
        }
        for (auto it = state.gases.begin(); it != state.gases.end();) {
            it = Choke(a_player, *it, dt) ? it + 1 : state.gases.erase(it);
        }
        for (auto it = state.fires.begin(); it != state.fires.end();) {
            it = Burn(a_player, *it, dt) ? it + 1 : state.fires.erase(it);
        }
        Afterburn(a_player, dt);
        if (a_cjBurning) {
            FlamesOnCj(a_player, *a_cjBurning, dt);
        }
        Draw(state.flights);
    }

    // Never from inside Update: it empties what Update iterates.
    void Clear()
    {
        state.queue.clear();
        state.flights.clear();
        for (auto& gas : state.gases) {
            Remove(gas.cloud);
        }
        state.gases.clear();
        state.fires.clear();
        state.flames.Clear();
        state.burning.clear();
    }
}
