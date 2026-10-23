#include "player/Projectiles.h"

#include "body/ModelView.h"
#include "hud/SaHud.h"
#include "explosives.h"
#include "link/LinkService.h"
#include "player/Particles.h"
#include "player/People.h"
#include "player/Puffs.h"
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
        constexpr std::uint32_t kCopyBase = 0xA5C00000;  // + 2 per copy of a projectile's model: even, never San Andreas's

        // Vanilla forms (Skyrim.esm).
        constexpr RE::FormID kBlastExplosion = 0x000439C0;    // FireBallExp01: the fireball's blast, no damage of its own
        constexpr float      kMolotovScale = 2.0f;            // San Andreas's Molotov fireball is twice a rocket's
        constexpr float      kVehicleScale = 2.0f;            // and a vehicle's (EXPLOSION_CAR) as big
        constexpr const char* kFireModel = "Effects\\FXFireOilHazard.nif";  // TrapOilHazard01's burning oil, drawn alone
        constexpr const char* kCjFireModel = "Effects\\FXFire02New.nif";   // a small fire (FireSmLongHazard's), on CJ while he burns
        constexpr float       kCjFireEvery = 0.2f;  // seconds between the flames put on CJ
        constexpr RE::FormID kGasHazard = 0x00106380;         // DA13PustuleGasCloud: a gas cloud that hurts no one itself
        constexpr RE::FormID kFlames = 0x00012FCD;            // Flames
        constexpr double      kPutOutPuffEvery = 1.0;  // seconds between the puffs where one actor is put out

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
            std::uint8_t                                                 weapon = 0;  // what it counts as (the launcher for a rocket)
            std::uint32_t                                                target = 0;  // a locked heat seeker's target, a form id
            const void*                                                  place = nullptr;  // the interior or worldspace it is in
            bool                                                         stuck = false;   // a satchel that landed
            RE::NiPointer<RE::NiAVObject>                                root;  // the 3D of what it stuck to, none for the land
            RE::NiPointer<RE::NiAVObject>                                node;  // and the node on it it follows
            RE::ObjectRefHandle                                          on;
            Stuck                                                        stuckOn;
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
            bool         out = false;  // the extinguisher put it out
            std::uint8_t type = 0;
            RE::NiPointer<RE::BSTempEffectParticle> shown;  // its burning oil, hidden when it is put out
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
            Damage::Blows                          sprays;  // the spray can's, as the flamethrower's
            std::map<RE::FormID, double>           coughs;  // when the spray can or extinguisher last made each one cough
            std::map<RE::FormID, double>           doused;  // when the extinguisher last showed one put out
            RE::BGSExplosion*                      blast = nullptr;
            RE::BGSHazard*                         gas = nullptr;
            RE::SpellItem*                         flamesSpell = nullptr;
            int                                    logged = 0;
            unsigned                               stale = 0;
            HsLock                                 lock;       // the heat seeker's, while CJ aims it
            float                                  unaimed = 0.0f;  // seconds since CJ last aimed the heat seeker
            float                                  losTick = 0.0f;
            bool                                   inSight = false;
            std::uint8_t                           cjWeapon = 0;
            std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> copies;  // (model, nth shown) to its copy's id
            std::uint32_t                          copyCount = 0;
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
            const void*   place = nullptr;
        } frame;

        // Where the player is: its interior cell, or the worldspace outside. A projectile left behind in another is dropped.
        const void* PlaceOf(RE::PlayerCharacter* a_player)
        {
            auto* cell = a_player->GetParentCell();
            if (cell && cell->IsInteriorCell()) {
                return cell;
            }
            return a_player->GetWorldspace();
        }

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

        // Returns what CJ took, nothing when !a_hurtCj (he sat in the vehicle that blew up: San Andreas killed him).
        float Explode(RE::PlayerCharacter* a_player, Kind a_kind, std::uint8_t a_weapon, const RE::NiPoint3& a_at, bool a_hurtCj = true)
        {
            const double now = NowSeconds();
            int          caught = 0;
            People::Blast(a_at);  // heard far and wide
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
            const float self = a_hurtCj ? BlastDamage(a_kind, DistanceTo(a_player, a_at)) : 0.0f;
            if (self > 0.0f) {
                Vitals::Hurt(a_player, self);
            }
            Log(std::format("{} went off, {} caught, CJ took {:.0f}", a_weapon, caught, self));
            return self;
        }

        void GoOff(RE::PlayerCharacter* a_player, const Projectile& a_p, const RE::NiPoint3& a_at)
        {
            const auto weapon = a_p.weapon ? a_p.weapon : WeaponOf(a_p.type);
            switch (a_p.kind) {
            case Kind::Rocket:
            case Kind::Grenade:
            case Kind::Satchel:
                {
                    Tell(Protocol::EffectKind::Explosion, frame.handover, frame.anchor, a_at, a_p.type, a_p.id);
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
            bool               hit = false;
            RE::Actor*         actor = nullptr;
            RE::TESObjectREFR* ref = nullptr;  // what it touched, none for the land
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
                contact.ref = ref;
                contact.at = a_from + dir * distance;
                contact.normal = RE::NiPoint3(output.normal.quad.m128_f32[0], output.normal.quad.m128_f32[1], output.normal.quad.m128_f32[2]);
                if (contact.normal.Unitize() < 1e-3f || actor) {
                    contact.normal = -dir;  // off an actor's capsule: straight back
                }
                break;
            }
            return contact;
        }

        // ---- Sticking ----

        Explosives::Node NodeOf(const RE::NiTransform& a_world)
        {
            const auto&      m = a_world.rotate.entry;
            Explosives::Node node;
            node.x = { m[0][0], m[1][0], m[2][0] };
            node.y = { m[0][1], m[1][1], m[2][1] };
            node.z = { m[0][2], m[1][2], m[2][2] };
            node.translate = V(a_world.translate);
            node.scale = a_world.scale;
            return node;
        }

        // The node of a_root nearest a_at: a bone of an actor's skeleton (a mammoth's spine, a horse's flank).
        RE::NiAVObject* NearestNode(RE::NiAVObject* a_root, const RE::NiPoint3& a_at)
        {
            RE::NiAVObject* best = a_root;
            float           bestDistance = a_root ? a_root->world.translate.GetDistance(a_at) : 0.0f;
            RE::BSVisit::TraverseScenegraphObjects(a_root, [&](RE::NiAVObject* a_object) {
                const std::string_view name = a_object->name.c_str() ? a_object->name.c_str() : "";
                if (a_object->AsNode() && name.find("Camera") == std::string_view::npos) {
                    const float distance = a_object->world.translate.GetDistance(a_at);
                    if (distance < bestDistance) {
                        best = a_object;
                        bestDistance = distance;
                    }
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
            return best;
        }

        // A satchel landing on a_contact sticks there: on an actor to its nearest bone, on anything else with 3D
        // (a cart, a door, a rock) to that, on the land where it is.
        void Stick(Projectile& a_p, const Contact& a_contact)
        {
            const RE::NiPoint3 at = a_contact.at + a_contact.normal * 2.0f;
            a_p.stuck = true;
            a_p.body.position = V(at);
            a_p.body.velocity = {};
            auto* root = a_contact.ref ? a_contact.ref->Get3D() : nullptr;
            auto* node = a_contact.actor ? NearestNode(root, at) : root;
            if (node) {
                a_p.root.reset(root);
                a_p.node.reset(node);
                a_p.on = a_contact.ref->GetHandle();
                a_p.stuckOn = StickTo(NodeOf(node->world), V(at), a_p.turn);
            }
            Log(std::format("satchel stuck to {}{}", a_contact.ref ? a_contact.ref->GetDisplayFullName() : "the land",
                node ? std::format(" at {}", node->name.c_str() ? node->name.c_str() : "") : ""));
        }

        // A stuck satchel goes where its node goes; once that's unloaded it stays where it was.
        void Follow(Projectile& a_p)
        {
            if (!a_p.node) {
                return;
            }
            auto ref = a_p.on.get();
            if (!ref || ref->Get3D() != a_p.root.get()) {
                a_p.node.reset();
                a_p.root.reset();
                return;
            }
            const auto node = NodeOf(a_p.node->world);
            a_p.body.position = StuckAt(node, a_p.stuckOn);
            a_p.turn = StuckTurn(node, a_p.stuckOn);
        }

        // ---- The heat seeker ----

        RE::NiPoint3 Middle(RE::Actor* a_actor)
        {
            const auto feet = a_actor->GetPosition();
            return { feet.x, feet.y, feet.z + 0.5f * a_actor->GetHeight() };
        }

        bool Lockable(RE::Actor* a_actor)
        {
            auto*       race = a_actor->GetRace();
            const char* id = race ? race->GetFormEditorID() : nullptr;
            return HeatSeekerLocks(id ? id : "", race && race->HasKeywordString("ActorTypeDragon"), a_actor->IsOnMount());
        }

        constexpr float kLockKept = 0.3f;  // seconds the lock outlasts the aim, for a launch that comes in after it

        // While CJ aims the heat seeker: the best target through Skyrim's camera as San Andreas rates them, in sight
        // (tested every kHsLosEvery), locked once held kHsLockSeconds, and marked in the HUD.
        void LockOn(RE::PlayerCharacter* a_player, bool a_aiming, float a_dt)
        {
            state.unaimed = a_aiming ? 0.0f : state.unaimed + a_dt;
            auto* camera = RE::PlayerCamera::GetSingleton();
            auto* root = camera ? camera->cameraRoot.get() : nullptr;
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!a_aiming) {
                if (state.unaimed > kLockKept) {
                    state.lock = {};
                }
                return;
            }
            if (!root || !lists) {
                state.lock = {};
                return;
            }
            // The camera node San Andreas's camera is put on (PlayerSync): +Y forward, as camera.h has it.
            const auto&        world = root->world;
            const RE::NiPoint3 eye = world.translate;
            const Vec3         look{ world.rotate.entry[0][1], world.rotate.entry[1][1], world.rotate.entry[2][1] };
            const Vec3         eyeM = Scale(V(eye), 1.0f / kMeter);
            if (static bool logged = false; !logged) {
                logged = true;
                SKSE::log::info("explosives: the heat seeker aims from ({:.0f},{:.0f},{:.0f}) along ({:.2f},{:.2f},{:.2f})", eye.x, eye.y, eye.z, look.x, look.y, look.z);
            }
            RE::Actor*         best = nullptr;
            float              bestRating = 0.0f;
            for (auto& handle : lists->highActorHandles) {
                auto  ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == a_player || actor->IsDead() || !actor->Is3DLoaded() || !Lockable(actor)) {
                    continue;
                }
                const float rating = HsRating(eyeM, look, Scale(V(Middle(actor)), 1.0f / kMeter), actor->GetFormID() == state.lock.target);
                if (rating >= 0.0f && (!best || rating < bestRating)) {
                    best = actor;
                    bestRating = rating;
                }
            }
            const std::uint32_t target = best ? best->GetFormID() : 0;
            state.losTick += a_dt;
            if (best && (target != state.lock.target || state.losTick >= kHsLosEvery)) {
                state.losTick = 0.0f;
                const auto contact = Segment(a_player, eye, Middle(best));
                state.inSight = !contact.hit || contact.actor == best || (contact.actor && Lockable(contact.actor));
            }
            const bool locked = state.lock.Locked();
            Track(state.lock, target, best && state.inSight, a_dt);
            if (best && state.lock.target == target) {
                SaHud::MarkTarget(Middle(best), state.lock.Locked());
            }
            if (state.lock.Locked() && !locked) {
                Log(std::format("heat seeker locked on to {}", best->GetDisplayFullName()));
            }
        }

        // A locked heat seeker steers for its target a_dt seconds on and goes off close by it (true then); one whose
        // target is gone flies on straight.
        bool Home(RE::PlayerCharacter* a_player, Projectile& a_p, float a_dt)
        {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_p.target);
            if (!actor || actor->IsDead() || !actor->Is3DLoaded()) {
                a_p.target = 0;
                return false;
            }
            const auto middle = V(Middle(actor));
            if (NearTarget(a_p.body.position, middle, RadiusOf(actor))) {
                GoOff(a_player, a_p, Ni(a_p.body.position));
                return true;
            }
            const Vec3 before = a_p.body.velocity;
            a_p.body.velocity = Steer(before, a_p.body.position, middle, a_dt);
            a_p.turn = Mul(FromTo(before, a_p.body.velocity), a_p.turn);
            return false;
        }

        bool FlyOn(RE::PlayerCharacter* a_player, Projectile& a_p, float a_dt);

        // One projectile a_dt seconds on; false once it is gone (San Andreas told, for a rocket's stand-in).
        bool Fly(RE::PlayerCharacter* a_player, Projectile& a_p, float a_dt)
        {
            const bool flying = a_p.place == frame.place && FlyOn(a_player, a_p, a_dt);
            if (!flying && (a_p.kind == Kind::Rocket || a_p.kind == Kind::Molotov || a_p.kind == Kind::Satchel)) {
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
            if (a_p.stuck) {
                Follow(a_p);
                return true;
            }
            if (a_p.kind == Kind::Satchel && a_p.age >= kLifeMax) {
                return false;  // thrown off a cliff into nothing: it never lands
            }
            float left = a_dt;
            while (!a_p.resting && left > 0.0f) {
                const float  h = (std::min)(left, kStepMax);
                left -= h;
                if (a_p.target && Home(a_player, a_p, h)) {
                    return false;
                }
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
                if (SticksOnContact(a_p.kind)) {
                    Stick(a_p, contact);
                    return true;
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

        // The id to show the a_nth (from 0) projectile drawn with a_model this frame by: the model, then copies of it.
        std::uint32_t CopyOf(std::uint32_t a_model, std::uint32_t a_nth)
        {
            if (a_nth == 0) {
                return a_model;
            }
            auto& id = state.copies[{ a_model, a_nth }];
            if (!id) {
                id = kCopyBase + 2 * ++state.copyCount;
            }
            return ModelView::AddCopy(a_model, id) ? id : 0;
        }

        void Draw(const std::deque<Projectile>& a_flights)
        {
            std::map<std::uint32_t, std::uint32_t> shown;  // by model: how many are drawn with it so far
            for (auto it = a_flights.rbegin(); it != a_flights.rend(); ++it) {
                const auto model = it->model && it->partCount ? CopyOf(it->model, shown[it->model]++) : 0;
                if (!model) {
                    continue;
                }
                Protocol::BodyState pose{};
                pose.flags = Protocol::kBodyValid;
                pose.model = model;
                pose.boneCount = it->partCount;
                for (std::uint32_t i = 0; i < it->partCount; ++i) {
                    pose.bones[i] = PartBone(it->turn, it->parts[i]);
                }
                ModelView::Show(pose, Ni(it->body.position));
            }
        }

        // The detonator: every satchel CJ has out goes off where it is.
        void Detonate(RE::PlayerCharacter* a_player)
        {
            int blown = 0;
            for (auto it = state.flights.begin(); it != state.flights.end();) {
                if (it->kind != Kind::Satchel) {
                    ++it;
                    continue;
                }
                const auto p = *it;
                it = state.flights.erase(it);
                GoOff(a_player, p, Ni(p.body.position));
                blown++;
            }
            Log(std::format("the detonator blew {} satchels", blown));
        }

        void Launch(RE::PlayerCharacter* a_player, const Protocol::ProjectileEvent& a_event, const RE::NiPoint3& a_anchor)
        {
            if (a_event.weaponType == Weapons::Detonator) {
                Detonate(a_player);
                return;
            }
            Projectile p;
            p.type = a_event.weaponType;
            p.kind = KindOf(p.type);
            p.weapon = WeaponOf(p.type);
            p.place = frame.place;
            if (p.kind == Kind::Rocket && state.cjWeapon == Weapons::RocketLauncherHs) {
                // San Andreas fires a plain rocket from the heat seeker without a lock of its own (it locks on to
                // vehicles only): Skyrim's lock steers it.
                p.weapon = Weapons::RocketLauncherHs;
                p.target = state.lock.Locked() ? state.lock.target : 0;
            }
            const RE::NiPoint3 at = a_anchor + RE::NiPoint3(a_event.origin[0], a_event.origin[1], a_event.origin[2]);
            if (p.kind == Kind::None || !std::isfinite(at.x + at.y + at.z)) {
                return;
            }
            if (a_event.flags & Protocol::kProjectileCannon) {
                // v23: the Rhino's cannon fires with San Andreas's tank_fire, its smoke and flash, at the muzzle.
                RE::NiPoint3 dir(a_event.velocity[0], a_event.velocity[1], a_event.velocity[2]);
                if (dir.Unitize() > 0.0f) {
                    Particles::TankFire(at, dir);
                }
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
            Log(std::format("projectile {} launched at {:.0f} units a second, fuse {:.1f} s{}", p.type, Length(p.body.velocity), p.fuse,
                p.target ? std::format(", locked on to {:08X}", p.target) : ""));
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
            if (a_fire.out) {
                return false;
            }
            if (!a_fire.caught) {
                a_fire.start -= a_dt;
                if (a_fire.start > 0.0f) {
                    return true;
                }
                a_fire.caught = true;
                if (auto* cell = a_player->GetParentCell()) {
                    a_fire.shown.reset(RE::BSTempEffectParticle::Spawn(cell, a_fire.left, kFireModel, RE::NiPoint3(0.0f, 0.0f, 1.0f), a_fire.at, 1.0f, 7, nullptr));
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

        // ---- The spray can and the extinguisher ----

        RE::NiPoint3 MiddleOf(RE::Actor* a_actor)
        {
            const auto feet = a_actor->GetPosition();
            return { feet.x, feet.y, feet.z + 0.5f * a_actor->GetHeight() };
        }

        bool InConeOf(RE::Actor* a_actor, const RE::NiPoint3& a_origin, const RE::NiPoint3& a_dir, float a_range, Cone a_cone)
        {
            return InCone(V(a_origin), V(a_dir), a_range, V(MiddleOf(a_actor)), RadiusOf(a_actor) + 0.25f * a_actor->GetHeight(), HalfAngle(a_cone));
        }

        // Someone in the spray can's or the extinguisher's cloud coughs (a stagger) every kSprayCough seconds.
        void Cough(RE::Actor* a_actor, const RE::NiPoint3& a_from, double a_now)
        {
            if (state.coughs.size() > 64) {
                std::erase_if(state.coughs, [&](const auto& a_entry) { return a_now - a_entry.second >= kSprayCough; });
            }
            const auto [cough, fresh] = state.coughs.try_emplace(a_actor->GetFormID(), a_now);
            if ((fresh || a_now - cough->second >= kSprayCough) && !a_actor->IsDead() && Shots::ClassOf(a_actor) == Damage::Class::Normal) {
                cough->second = a_now;
                Stagger(a_actor, a_from);
            }
        }

        // The spray can: everyone in its cone chokes as in tear gas, hurt a little and coughing.
        void Spray(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_origin, const RE::NiPoint3& a_dir, float a_range, double a_now)
        {
            Puffs::Puff(Puffs::Kind::Spray, a_origin, a_dir);
            ForActorsNear(a_player, a_origin, [&](RE::Actor* a_actor) {
                if (a_actor->IsDead() || !InConeOf(a_actor, a_origin, a_dir, a_range, Cone::Spray)) {
                    return;
                }
                const float landed = state.sprays.Lands(a_actor->GetFormID(), 0, a_now, true);
                if (landed > 0.0f) {
                    Shots::Hurt(a_player, a_actor, Weapons::Spraycan, kSprayPerSecond * landed, MiddleOf(a_actor), a_dir, false);
                }
                Cough(a_actor, a_origin, a_now);
            });
        }

        // Puts a_actor out: no more afterburn, and every fire effect on it (the Flames we cast, a fire mage's) dispelled.
        bool PutOut(RE::Actor* a_actor)
        {
            bool       was = state.burning.erase(a_actor->GetFormID()) > 0;
            auto*      target = a_actor->AsMagicTarget();
            auto*      effects = target ? target->GetActiveEffectList() : nullptr;
            std::vector<RE::ActiveEffect*> fire;
            if (effects) {
                for (auto* effect : *effects) {
                    const auto* base = effect ? effect->GetBaseObject() : nullptr;
                    if (base && base->data.resistVariable == RE::ActorValue::kResistFire && !effect->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
                        fire.push_back(effect);
                    }
                }
            }
            for (auto* effect : fire) {
                effect->Dispel(true);
            }
            if (was || !fire.empty()) {
                // The fire shown on it (the Flames' shader) goes with it, not when the effect runs out.
                if (auto* lists = RE::ProcessLists::GetSingleton()) {
                    lists->StopAllMagicEffects(*a_actor);
                }
                return true;
            }
            return false;
        }

        // The extinguisher: whoever burns in its cone is put out, and the Molotovs' fire on the ground it reaches.
        // Whoever is in it coughs as in the spray can's, unhurt and not angered, as in San Andreas. San Andreas
        // puts out its own there (and CJ) when it sends the puff.
        void Hose(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_origin, const RE::NiPoint3& a_dir, float a_range, double a_now)
        {
            Puffs::Puff(Puffs::Kind::Hose, a_origin, a_dir);
            if (state.doused.size() > 64) {
                std::erase_if(state.doused, [&](const auto& a_entry) { return a_now - a_entry.second >= kPutOutPuffEvery; });
            }
            ForActorsNear(a_player, a_origin, [&](RE::Actor* a_actor) {
                if (!InConeOf(a_actor, a_origin, a_dir, a_range, Cone::Hose)) {
                    return;
                }
                if (PutOut(a_actor)) {
                    const auto [doused, fresh] = state.doused.try_emplace(a_actor->GetFormID(), a_now);
                    if (fresh || a_now - doused->second >= kPutOutPuffEvery) {
                        doused->second = a_now;
                        Puffs::PutOut(MiddleOf(a_actor));
                        SKSE::log::info("explosives: the extinguisher put out {} ({:08X})", a_actor->GetDisplayFullName(), a_actor->GetFormID());
                    }
                }
                if (!a_actor->IsDead()) {
                    Cough(a_actor, a_origin, a_now);
                }
            });
            int doused = 0;
            for (auto& fire : state.fires) {
                if (fire.out || !PutsOut(V(a_origin), V(a_dir), a_range, V(fire.at))) {
                    continue;
                }
                fire.out = true;
                doused++;
                if (fire.shown) {
                    fire.shown->lifetime = fire.shown->age;
                    if (fire.shown->particleObject) {
                        fire.shown->particleObject->SetAppCulled(true);
                    }
                }
                if (fire.caught) {
                    Puffs::PutOut(fire.at);
                }
            }
            if (doused) {
                SKSE::log::info("explosives: the extinguisher put out {} of a Molotov's fire patches", doused);
            }
        }
    }

    void OnDataLoaded()
    {
        state.blast = Lookup<RE::BGSExplosion>(kBlastExplosion, "the rockets' and grenades' blast (FireBallExp01)");
        state.blast = Silent(state.blast);
        state.gas = Lookup<RE::BGSHazard>(kGasHazard, "the tear gas cloud (DA13PustuleGasCloud)");
        state.flamesSpell = Lookup<RE::SpellItem>(kFlames, "the flamethrower's flames (Flames)");
    }

    void VehicleBlast(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, bool a_hurtCj)
    {
        if (auto ball = Place(a_player, state.blast, a_at).get()) {
            ball->SetScale(kVehicleScale);
        }
        Explode(a_player, Kind::Vehicle, Weapons::RocketLauncher, a_at, a_hurtCj);
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
        const Cone         cone = ConeOf(a_shot.weaponType);
        if (cone == Cone::None || !(dir.Unitize() >= 1e-4f) || !std::isfinite(origin.x + origin.y + origin.z)) {
            return;
        }
        const float  range = ConeRange(cone, a_shot.range);
        const double now = NowSeconds();
        if (cone == Cone::Spray) {
            Spray(a_player, origin, dir, range, now);
            return;
        }
        if (cone == Cone::Hose) {
            Hose(a_player, origin, dir, range, now);
            return;
        }
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

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, float a_delta, const RE::NiPoint3* a_cjBurning,
        std::uint8_t a_cjWeapon, bool a_aiming)
    {
        frame.handover = a_handover;
        frame.anchor = a_anchor;
        frame.place = PlaceOf(a_player);
        state.cjWeapon = a_cjWeapon;
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
        LockOn(a_player, a_aiming && a_cjWeapon == Weapons::RocketLauncherHs, dt);
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
        Puffs::Update(dt);
        if (a_cjBurning) {
            FlamesOnCj(a_player, *a_cjBurning, dt);
        }
        Draw(state.flights);
    }

    // Never from inside Update: it empties what Update iterates.
    void Clear()
    {
        state.queue.clear();
        for (const auto& p : state.flights) {
            if (p.kind == Kind::Satchel) {
                Tell(Protocol::EffectKind::ProjectileGone, frame.handover, frame.anchor, Ni(p.body.position), p.type, p.id);  // San Andreas drops its stand-in
            }
        }
        state.flights.clear();
        state.lock = {};
        for (auto& gas : state.gases) {
            Remove(gas.cloud);
        }
        state.gases.clear();
        state.fires.clear();
        state.flames.Clear();
        state.burning.clear();
        state.sprays.Clear();
        state.coughs.clear();
        state.doused.clear();
        Puffs::Clear();
    }
}
