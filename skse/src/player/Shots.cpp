#include "player/Shots.h"

#include "Ini.h"
#include "damage.h"
#include "link/LinkService.h"
#include "mega_punch.h"
#include "player/CheatCodes.h"
#include "player/Particles.h"
#include "player/Chatter.h"
#include "player/ObjectHits.h"
#include "player/People.h"
#include "player/Projectiles.h"
#include "weapons.h"

#include <random>

namespace AhShitRim::Shots
{
    namespace
    {
        constexpr std::size_t kQueueMax = 256;        // shots kept for the next frame; a minigun fires about 20 a second
        constexpr int         kPassMax = 8;           // things a bullet goes through (the player, the dead) before it gives up
        constexpr float       kMeleeSearch = 600.0f;  // actors this close to the blow are looked at
        constexpr float       kActorRadiusMin = 20.0f;
        constexpr float       kBlowObjectReach = 45.0f;  // units each way along CJ's facing a blow finds an object in
        constexpr float       kImpactScale = 2.5f;    // the arrows' impacts, bigger for a bullet
        constexpr int         kLoggedHits = 12;       // the first hits are logged in full, then a summary
        constexpr int         kLoggedBlocks = 20;     // and the first things that stop a bullet

        // Vanilla weapons whose impact sets the shots borrow: the Hunting Bow's arrows for bullets, the Iron
        // Sword for blades, the Iron Mace for the rest of the melee.
        constexpr RE::FormID kHuntingBow = 0x00013985;
        constexpr RE::FormID kIronSword = 0x00012EB7;
        constexpr RE::FormID kIronMace = 0x00013982;

        using ProcessHitFn = void (*)(RE::Actor*, RE::HitData&);

        struct State
        {
            std::vector<Protocol::ShotEvent> queue;
            Damage::Blows                    landed;
            Damage::SkillHits                hits;
            RE::COL_LAYER                    layer = RE::COL_LAYER::kProjectile;
            RE::BGSImpactDataSet*            bulletSet = nullptr;
            RE::BGSImpactDataSet*            bladeSet = nullptr;
            RE::BGSImpactDataSet*            bluntSet = nullptr;
            ProcessHitFn                     processHit = nullptr;
            int                              logged = 0;
            int                              loggedBlocks = 0;
            // For the summary.
            unsigned                         bullets = 0, blows = 0, actorHits = 0, staticHits = 0, misses = 0, stale = 0;
            std::chrono::steady_clock::time_point summaryAt;
        } state;

        double NowSeconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        // Skyrim's own hit: the melee hit (Actor's 37673/38627) hands its HitData to it with a call at 0x4A8, which
        // gives its address. nullptr when the call isn't there (another game version): then DoDamage, without
        // the crime.
        ProcessHitFn ResolveProcessHit()
        {
            REL::Relocation<std::uintptr_t> caller{ RELOCATION_ID(37673, 38627) };
            const auto                      site = caller.address() + REL::Relocate(0x3C0, 0x4A8);
            const auto*                     code = reinterpret_cast<const std::uint8_t*>(site);
            if (code[0] != 0xE8) {
                return nullptr;
            }
            std::int32_t rel = 0;
            std::memcpy(&rel, code + 1, sizeof(rel));
            return reinterpret_cast<ProcessHitFn>(site + 5 + rel);
        }

        RE::BGSImpactDataSet* ImpactSetOf(RE::FormID a_weapon, const char* a_what)
        {
            auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(a_weapon);
            auto* set = weapon ? weapon->impactDataSet : nullptr;
            SKSE::log::info("shots: {} impacts from {:08X} ({}): {}", a_what, a_weapon, weapon ? weapon->GetName() : "not found",
                set ? std::format("impact set {:08X}", set->GetFormID()) : std::string("none, no impacts"));
            return set;
        }

        // ---- Impacts ----

        RE::BGSImpactData* ImpactFor(RE::BGSImpactDataSet* a_set, RE::BGSMaterialType* a_material)
        {
            if (!a_set || !a_material) {
                return nullptr;
            }
            const auto found = a_set->impactMap.find(a_material);
            return found != a_set->impactMap.end() ? found->second : nullptr;
        }

        // The surface's material: the hit shape's own (bhkShape::materialID). GetMaterialID, which asks per
        // triangle, crashed Skyrim on a static's shape, so a mesh of several materials counts as its first.
        RE::BGSMaterialType* MaterialOf(const RE::hkpCollidable* a_collidable)
        {
            const auto* shape = a_collidable ? a_collidable->GetShape() : nullptr;
            auto*       owner = shape ? netimmerse_cast<RE::bhkShape*>(static_cast<RE::NiObject*>(shape->userData)) : nullptr;
            return owner ? RE::BGSMaterialType::GetMaterialType(owner->materialID) : nullptr;
        }

        void PlayImpact(RE::TESObjectCELL* a_cell, RE::BGSImpactData* a_impact, const RE::NiPoint3& a_at, const RE::NiPoint3& a_normal, RE::NiAVObject* a_target)
        {
            if (!a_cell || !a_impact) {
                return;
            }
            if (const char* model = a_impact->GetModel(); model && *model) {
                const float duration = a_impact->data.effectDuration > 0.0f ? a_impact->data.effectDuration : 1.0f;
                RE::BSTempEffectParticle::Spawn(a_cell, duration, model, a_normal, a_at, kImpactScale, 7, a_target);
            }
            if (auto* sound = a_impact->sound1) {
                RE::BSSoundHandle handle;
                if (auto* audio = RE::BSAudioManager::GetSingleton(); audio && audio->GetSoundHandle(handle, sound)) {
                    handle.SetPosition(a_at);
                    handle.Play();
                }
            }
        }

        // ---- Hurting ----

        struct Victim
        {
            RE::Actor*      actor = nullptr;
            RE::NiAVObject* node = nullptr;  // the bone hit, if Havok said
            Damage::Part    part = Damage::Part::Torso;
            RE::NiPoint3    at;
            RE::NiPoint3    normal;
        };

        float Roll()
        {
            static std::mt19937                          random{ std::random_device{}() };
            static std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            return unit(random);
        }

        // Skyrim's own stagger, as a shield bash or a shout starts it.
        void Stagger(RE::Actor* a_actor, RE::Actor* a_from)
        {
            const auto  at = a_actor->GetPosition();
            const auto  from = a_from->GetPosition();
            const float heading = std::atan2(from.x - at.x, from.y - at.y);
            a_actor->SetGraphVariableFloat("staggerDirection", Damage::StaggerDirection(a_actor->GetAngleZ(), heading));
            a_actor->SetGraphVariableFloat("staggerMagnitude", 0.5f);
            a_actor->NotifyAnimationGraph("staggerStart");
        }

        Damage::Class ClassOfRace(RE::Actor* a_actor)
        {
            auto* race = a_actor->GetRace();
            if (!race) {
                return Damage::Class::Normal;
            }
            const char* id = race->GetFormEditorID();
            return Damage::ClassOf(id ? id : "", race->HasKeywordString("ActorTypeDragon"));
        }

        // a_skyrimDamage, when over 0, is the damage itself (a mega punch's), not worked out from San Andreas's.
        void Hurt(RE::PlayerCharacter* a_player, const Victim& a_victim, const Protocol::ShotEvent& a_shot, const RE::NiPoint3& a_dir, float a_saDamage,
            bool a_blood = true, float a_skyrimDamage = 0.0f)
        {
            auto*      actor = a_victim.actor;
            const auto level = actor->GetLevel();
            const auto cls = ClassOfRace(actor);
            const float damage = a_skyrimDamage > 0.0f ? a_skyrimDamage : Damage::ToSkyrim(a_saDamage, a_shot.weaponType, a_victim.part, level, cls);
            const float before = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            const bool  melee = a_shot.kind == Protocol::kShotMelee;
            // v29: a kill made sneaking, of someone not fighting yet, is a stealth kill CJ may remark on.
            const bool unseen = a_player->IsSneaking() && !actor->IsInCombat() && !actor->IsDead();

            if (state.processHit) {
                auto* hit = RE::HitData::Create(a_player, actor, nullptr, false);
                if (hit) {
                    hit->hitPosition = a_victim.at;
                    hit->hitDirection = a_dir;
                    hit->totalDamage = damage;
                    hit->physicalDamage = damage;
                    hit->targetedLimbDamage = 0.0f;
                    hit->percentBlocked = 0.0f;
                    hit->resistedPhysicalDamage = 0.0f;
                    hit->resistedTypedDamage = 0.0f;
                    hit->reflectedDamage = 0.0f;
                    hit->sneakAttackBonus = 1.0f;
                    hit->criticalEffect = nullptr;
                    if (!melee) {
                        hit->stagger = 0.0f;
                    }
                    hit->flags.reset(RE::HitData::Flag::kBlocked, RE::HitData::Flag::kBlockWithWeapon, RE::HitData::Flag::kBlockCandidate,
                        RE::HitData::Flag::kCritical, RE::HitData::Flag::kSneakAttack, RE::HitData::Flag::kPowerAttack, RE::HitData::Flag::kBash,
                        RE::HitData::Flag::kTimedBash, RE::HitData::Flag::kDisarm);
                    hit->flags.set(RE::HitData::Flag::kIgnoreCritical);
                    if (!melee) {
                        hit->flags.reset(RE::HitData::Flag::kMeleeAttack);
                    }
                    state.processHit(actor, *hit);
                    hit->~HitData();
                    RE::free(hit);
                }
            } else {
                actor->DoDamage(damage, a_player, true);
            }

            state.actorHits++;
            if (unseen && (actor->IsDead() || actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0.0f)) {
                Chatter::StealthKill();
            }
            if (melee && a_skyrimDamage <= 0.0f && !actor->IsDead() && Roll() < Damage::StaggerChance(a_shot.weaponType)) {
                Stagger(actor, a_player);
            }
            if (state.logged < kLoggedHits) {
                state.logged++;
                const char* id = actor->GetRace() ? actor->GetRace()->GetFormEditorID() : "";
                SKSE::log::info("shots: weapon {} hit {} ({:08X}, level {}, {}) in the {} ({}): {:.1f} of San Andreas's {:.0f}, health {:.1f} -> {:.1f}",
                    a_shot.weaponType, actor->GetName(), actor->GetFormID(), level, id ? id : "?",
                    a_victim.part == Damage::Part::Head ? "head" : a_victim.part == Damage::Part::Limb ? "limb" : "body",
                    a_victim.node ? a_victim.node->name.c_str() : "no bone", damage, a_saDamage, before,
                    actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth));
            }

            // The race's blood where it went in, on the bone it hit.
            if (!a_blood) {
                return;
            }
            auto* race = actor->GetRace();
            auto* set = !melee ? state.bulletSet : Weapons::IsMelee(a_shot.weaponType) && (a_shot.weaponType == Weapons::Knife || a_shot.weaponType == Weapons::Katana ||
                                                                                       a_shot.weaponType == Weapons::Chainsaw)
                                                       ? state.bladeSet
                                                       : state.bluntSet;
            PlayImpact(actor->GetParentCell(), ImpactFor(set, race ? race->bloodImpactMaterial : nullptr), a_victim.at, a_victim.normal,
                a_victim.node ? a_victim.node : actor->Get3D());
        }

        // ---- Bullets ----

        // One bullet, or one of a shotgun's pellets, from a_origin along a_dir (a unit vector).
        // What stops a bullet: buildings, rocks, trees, furniture, the land and water. Clutter (cups, plates,
        // candlesticks), debris, triggers and invisible walls don't; actors are told apart by their reference.
        bool Blocks(RE::COL_LAYER a_layer)
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

        // The actor's head bone, or nullptr for a skeleton that names it otherwise.
        RE::NiAVObject* HeadOf(RE::Actor* a_actor)
        {
            auto* root = a_actor->Get3D();
            if (!root) {
                return nullptr;
            }
            for (const char* name : { "NPC Head [Head]", "NPC Head", "Head [Head]", "Canine_Head", "Head" }) {
                if (auto* node = root->GetObjectByName(name)) {
                    return node;
                }
            }
            return nullptr;
        }

        // Where on the actor a line hit its body: the head when it passes through the head bone's sphere. Havok
        // gives the character's capsule, not a bone.
        Damage::Part PartOf(RE::Actor* a_actor, const RE::NiPoint3& a_origin, const RE::NiPoint3& a_dir, const RE::NiPoint3& a_at)
        {
            const auto   feet = a_actor->GetPosition();
            const float  height = (std::max)(a_actor->GetHeight(), 1.0f);
            RE::NiPoint3 head = feet + RE::NiPoint3(0.0f, 0.0f, height * 0.92f);
            if (auto* node = HeadOf(a_actor)) {
                head = node->world.translate;
            }
            const bool headshot = Damage::PassesWithin({ a_origin.x, a_origin.y, a_origin.z }, { a_dir.x, a_dir.y, a_dir.z }, { head.x, head.y, head.z },
                Damage::HeadRadius(ClassOfRace(a_actor), a_actor->GetScale()));
            return Damage::PartOfHit(headshot, (a_at.z - feet.z) / height);
        }

        // One bullet, or one of a shotgun's pellets, from a_origin along a_dir (a unit vector).
        // One bullet, or one pellet: true when it hit an actor.
        bool Ray(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot, const RE::NiPoint3& a_origin, const RE::NiPoint3& a_dir)
        {
            state.bullets++;
            auto* cell = a_player->GetParentCell();
            auto* bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto* world = bhk ? bhk->GetWorld1() : nullptr;
            if (!world) {
                return false;
            }
            const RE::NiPoint3& origin = a_origin;
            const RE::NiPoint3& dir = a_dir;
            const float         scale = RE::bhkWorld::GetWorldScale();
            const RE::NiPoint3  end = origin + dir * a_shot.range;

            Victim               victim;
            bool                 blocked = false;
            RE::NiPoint3         at, normal;
            RE::BGSMaterialType* material = nullptr;
            RE::TESObjectREFR*   blocker = nullptr;
            int                  blockerLayer = 0;
            float                blockedAt = 0.0f;
            {
                RE::hkpWorldRayCastInput input;
                input.filterInfo.SetCollisionLayer(state.layer);
                input.to = RE::hkVector4(end.x * scale, end.y * scale, end.z * scale, 0.0f);
                RE::BSReadLockGuard lock(bhk->worldLock);
                float               from = 0.0f;
                for (int pass = 0; pass < kPassMax; ++pass) {
                    const RE::NiPoint3 start = origin + dir * from;
                    input.from = RE::hkVector4(start.x * scale, start.y * scale, start.z * scale, 0.0f);
                    RE::hkpWorldRayCastOutput output;
                    world->CastRay(input, output);
                    if (!output.HasHit()) {
                        break;
                    }
                    const float distance = from + (a_shot.range - from) * output.hitFraction;
                    auto*       ref = RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable);
                    auto*       actor = ref ? ref->As<RE::Actor>() : nullptr;
                    const auto  layer = output.rootCollidable->GetCollisionLayer();
                    if (ref == a_player || (actor && actor->IsDead()) || (!actor && !Blocks(layer) && !ObjectHits::Destructible(ref))) {
                        from = distance + 1.0f;  // through the player's own body, the dead and the clutter (not a lamp or a web)
                        continue;
                    }
                    at = origin + dir * distance;
                    normal = RE::NiPoint3(output.normal.quad.m128_f32[0], output.normal.quad.m128_f32[1], output.normal.quad.m128_f32[2]);
                    if (actor) {
                        victim.actor = actor;
                        victim.at = at;
                        victim.normal = normal;
                    } else {
                        blocked = true;
                        blocker = ref;
                        blockerLayer = static_cast<int>(layer);
                        blockedAt = distance;
                        material = MaterialOf(output.rootCollidable);
                    }
                    break;
                }
            }
            if (victim.actor) {
                victim.part = PartOf(victim.actor, origin, dir, victim.at);
                victim.node = victim.part == Damage::Part::Head ? HeadOf(victim.actor) : nullptr;
                const float sa = a_shot.damage > 0.0f ? a_shot.damage : Damage::SanAndreasDamage(a_shot.weaponType, a_shot.skill);
                Hurt(a_player, victim, a_shot, dir, sa);
                return true;
            }
            if (blocked) {
                state.staticHits++;
                if (state.loggedBlocks < kLoggedBlocks) {
                    state.loggedBlocks++;
                    auto* base = blocker ? blocker->GetBaseObject() : nullptr;
                    SKSE::log::info("shots: weapon {}'s bullet stopped {:.0f} units out on layer {} by {:08X} ({}, {})", a_shot.weaponType, blockedAt, blockerLayer,
                        blocker ? blocker->GetFormID() : 0, blocker ? blocker->GetDisplayFullName() : "the land", base ? RE::FormTypeToString(base->GetFormType()) : "");
                }
                if (!material) {
                    material = RE::BGSMaterialType::GetMaterialType(RE::MATERIAL_ID::kStone);  // a shape without one
                }
                PlayImpact(cell, ImpactFor(state.bulletSet, material), at, normal, nullptr);
                if (blocker) {
                    ObjectHits::Struck(a_player, blocker, Traps::Hit::Bullet, a_shot.weaponType,
                        a_shot.damage > 0.0f ? a_shot.damage : Damage::SanAndreasDamage(a_shot.weaponType, a_shot.skill));
                }
            } else {
                state.misses++;
            }
            return false;
        }

        void Bullet(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot, const RE::NiPoint3& a_anchor)
        {
            const RE::NiPoint3 origin = a_anchor + RE::NiPoint3(a_shot.origin[0], a_shot.origin[1], a_shot.origin[2]);
            RE::NiPoint3       dir(a_shot.direction[0], a_shot.direction[1], a_shot.direction[2]);
            if (!(dir.Unitize() >= 1e-4f) || !(a_shot.range > 0.0f) || !std::isfinite(origin.x + origin.y + origin.z)) {
                return;
            }
            if (a_shot.flags & Protocol::kShotVehicleGun) {
                Particles::GunFlash(origin, dir);  // v26: the Hunter's guns flash as in San Andreas
            }
            bool      hit = false;
            const int pellets = (a_shot.flags & Protocol::kShotSpread) ? Damage::Pellets(a_shot.weaponType) : 1;
            for (int i = 0; i < pellets; ++i) {
                const auto d = Damage::PelletDirection({ dir.x, dir.y, dir.z }, i, pellets, Damage::SpreadDegrees(a_shot.weaponType));
                hit |= Ray(a_player, a_shot, origin, RE::NiPoint3(d.x, d.y, d.z));
            }
            if (hit) {
                state.hits.Add(a_shot.weaponType);  // a shotgun's blast is one hit, as in San Andreas
            }
        }

        // ---- Blows ----

        float CapsuleRadius(RE::Actor* a_actor)
        {
            const auto  min = a_actor->GetBoundMin();
            const auto  max = a_actor->GetBoundMax();
            const float half = 0.5f * (std::min)(max.x - min.x, max.y - min.y) * a_actor->GetScale();
            return (std::max)(kActorRadiusMin, std::isfinite(half) ? half : 0.0f);
        }

        // IAVENJQ's blow (protocol/cpp/mega_punch.h): the victim's whole health, and thrown far from CJ unless it's a giant,
        // a mammoth or a dragon. An essential actor kneels instead of dying, as from any blow.
        void MegaBlow(RE::PlayerCharacter* a_player, const Victim& a_victim, const Protocol::ShotEvent& a_shot, const RE::NiPoint3& a_facing, float a_saDamage)
        {
            auto*       actor = a_victim.actor;
            const float health = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            Hurt(a_player, a_victim, a_shot, a_facing, a_saDamage, true, MegaPunch::DamageFor(health));
            const auto  cls = ClassOfRace(actor);
            const float knock = MegaPunch::KnockFor(cls);
            auto*       process = actor->GetActorRuntimeData().currentProcess;
            if (knock > 0.0f && process) {
                const auto from = MegaPunch::KnockFrom({ a_victim.at.x, a_victim.at.y, a_victim.at.z }, { a_facing.x, a_facing.y, a_facing.z });
                process->KnockExplosion(actor, RE::NiPoint3(from.x, from.y, from.z), knock);
            }
            SKSE::log::info("shots: mega punch ({}) on {} ({:08X}): {:.0f} health, {}", a_shot.weaponType, actor->GetName(), actor->GetFormID(), health,
                knock > 0.0f ? std::format("thrown with {:.0f}", knock) : std::string("too big to throw"));
        }

        // The first object a blow's line from a_from to a_to meets: what stops a bullet, or anything destructible
        // (a web). Actors and the player are passed through; nullptr for none, or for the land.
        RE::TESObjectREFR* ObjectOn(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
        {
            auto* cell = a_player->GetParentCell();
            auto* bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto* world = bhk ? bhk->GetWorld1() : nullptr;
            const float length = a_from.GetDistance(a_to);
            if (!world || !(length > 1.0f)) {
                return nullptr;
            }
            const RE::NiPoint3 dir = (a_to - a_from) / length;
            const float        scale = RE::bhkWorld::GetWorldScale();
            RE::hkpWorldRayCastInput input;
            input.filterInfo.SetCollisionLayer(state.layer);
            input.to = RE::hkVector4(a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f);
            RE::BSReadLockGuard lock(bhk->worldLock);
            float               from = 0.0f;
            for (int pass = 0; pass < kPassMax; ++pass) {
                const RE::NiPoint3 start = a_from + dir * from;
                input.from = RE::hkVector4(start.x * scale, start.y * scale, start.z * scale, 0.0f);
                RE::hkpWorldRayCastOutput output;
                world->CastRay(input, output);
                if (!output.HasHit()) {
                    return nullptr;
                }
                const float distance = from + (length - from) * output.hitFraction;
                auto*       ref = RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable);
                if (ref && ref != a_player && !ref->As<RE::Actor>() && (Blocks(output.rootCollidable->GetCollisionLayer()) || ObjectHits::Destructible(ref))) {
                    return ref;
                }
                if (!ref && Blocks(output.rootCollidable->GetCollisionLayer())) {
                    return nullptr;  // the land
                }
                from = distance + 1.0f;
            }
            return nullptr;
        }

        // A blow lands on the object in front of it too (a web), once per strike, the chainsaw's as its cuts.
        void BlowOnObject(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot, const RE::NiPoint3& a_point, const RE::NiPoint3& a_facing, double a_now)
        {
            RE::NiPoint3 facing = a_facing;
            if (!(facing.Unitize() > 1e-4f)) {
                return;
            }
            const float reach = (std::max)(a_shot.range, kBlowObjectReach);
            auto*       ref = ObjectOn(a_player, a_point - facing * reach, a_point + facing * reach);
            if (!ref) {
                return;
            }
            const bool  saw = a_shot.weaponType == Weapons::Chainsaw;
            const float landed = state.landed.Lands(ref->GetFormID(), a_shot.strike, a_now, saw);
            if (landed > 0.0f) {
                ObjectHits::Struck(a_player, ref, Traps::Hit::Blow, a_shot.weaponType,
                    Damage::BlowDamage(a_shot.damage > 0.0f ? a_shot.damage : Damage::SanAndreasDamage(a_shot.weaponType), a_shot.weaponType, landed));
            }
        }

        void Blow(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot, const RE::NiPoint3& a_anchor)
        {
            state.blows++;
            const RE::NiPoint3 point = a_anchor + RE::NiPoint3(a_shot.origin[0], a_shot.origin[1], a_shot.origin[2]);
            const RE::NiPoint3 facing(a_shot.direction[0], a_shot.direction[1], a_shot.direction[2]);
            const bool         saw = a_shot.weaponType == Weapons::Chainsaw;
            const double       now = NowSeconds();
            if (saw) {
                People::ChainsawCut();
            }
            auto*              lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            bool       landedAny = false;
            const bool mega = CheatCodes::On(Cheats::SaCheat::MegaPunch) && MegaPunch::Punches(a_shot.weaponType);
            for (auto& handle : lists->highActorHandles) {
                auto  ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == a_player || actor->IsDead() || !actor->Is3DLoaded() || actor->GetPosition().GetDistance(point) > kMeleeSearch) {
                    continue;
                }
                const auto feet = actor->GetPosition();
                float      height = 0.0f;
                if (!Damage::Reaches({ point.x, point.y, point.z }, a_shot.range, { feet.x, feet.y, feet.z }, actor->GetHeight(), CapsuleRadius(actor), &height)) {
                    continue;
                }
                const float landed = state.landed.Lands(actor->GetFormID(), a_shot.strike, now, saw);
                if (landed <= 0.0f) {
                    continue;
                }
                Victim victim;
                victim.actor = actor;
                victim.at = RE::NiPoint3(point.x, point.y, feet.z + height);
                victim.normal = -facing;
                const float sa = Damage::BlowDamage(a_shot.damage > 0.0f ? a_shot.damage : Damage::SanAndreasDamage(a_shot.weaponType), a_shot.weaponType, landed);
                if (mega) {
                    MegaBlow(a_player, victim, a_shot, facing, sa);
                } else {
                    Hurt(a_player, victim, a_shot, facing, sa);
                }
                landedAny = true;
            }
            if (landedAny) {
                state.hits.Add(a_shot.weaponType);
            } else {
                BlowOnObject(a_player, a_shot, point, facing, now);
            }
        }

        void SendHits()
        {
            state.hits.Flush([](std::uint8_t a_type, std::uint32_t a_count) {
                Protocol::WeaponCommand command{};
                command.kind = static_cast<std::uint32_t>(Protocol::WeaponCommandKind::Hit);
                command.argument = a_type;
                command.entries[0].ammo = a_count;
                return LinkService::Get().Send(Protocol::MessageType::WeaponCommand, &command, sizeof(command));
            });
        }

        void Summary()
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - state.summaryAt < std::chrono::seconds(5)) {
                return;
            }
            if (state.bullets || state.blows || state.stale) {
                SKSE::log::info("shots: {} bullets ({} hit actors and blows that landed, {} hit statics, {} hit nothing), {} blows, {} for an old handover",
                    state.bullets, state.actorHits, state.staticHits, state.misses, state.blows, state.stale);
            }
            state.bullets = state.blows = state.actorHits = state.staticHits = state.misses = state.stale = 0;
            state.summaryAt = now;
        }
    }

    void OnDataLoaded()
    {
        state.bulletSet = ImpactSetOf(kHuntingBow, "bullet");
        state.bladeSet = ImpactSetOf(kIronSword, "blade");
        state.bluntSet = ImpactSetOf(kIronMace, "blunt");
        if (const auto layer = Ini::Value("Weapons", "ShotLayer"); !layer.empty()) {
            try {
                state.layer = static_cast<RE::COL_LAYER>(std::stoi(layer));
            } catch (...) {
                SKSE::log::warn("shots: [Weapons] ShotLayer={} isn't a number, kept {}", layer, static_cast<int>(state.layer));
            }
        }
        state.processHit = ResolveProcessHit();
        SKSE::log::info("shots: bullets ray test on collision layer {}; {}", static_cast<int>(state.layer),
            state.processHit ? "hits go through Skyrim's own hit (crime, aggro, kill credit)" : "Skyrim's hit wasn't found, hits only take health");
    }

    void Queue(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        if (a_size != sizeof(Protocol::ShotEvent)) {
            SKSE::log::warn("shots: a shot of {} bytes (expected {}), ignored", a_size, sizeof(Protocol::ShotEvent));
            return;
        }
        if (state.queue.size() >= kQueueMax) {
            return;
        }
        Protocol::ShotEvent shot{};
        std::memcpy(&shot, a_data, sizeof(shot));
        state.queue.push_back(shot);
    }

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover)
    {
        auto queue = std::move(state.queue);
        state.queue.clear();
        for (const auto& shot : queue) {
            if (shot.handover != a_handover) {
                state.stale++;
                continue;
            }
            if (shot.kind == Protocol::kShotMelee) {
                Blow(a_player, shot, a_anchor);
            } else if (shot.kind == Protocol::kShotFlame) {
                Projectiles::Flame(a_player, shot, a_anchor);
            } else {
                Bullet(a_player, shot, a_anchor);
            }
        }
        SendHits();
        Summary();
    }

    void Clear()
    {
        state.queue.clear();
        state.landed.Clear();
        state.hits.Clear();  // hits that never went belong to the game before
    }

    void Hurt(RE::PlayerCharacter* a_player, RE::Actor* a_actor, std::uint8_t a_weapon, float a_saDamage, const RE::NiPoint3& a_at,
        const RE::NiPoint3& a_dir, bool a_blood)
    {
        if (!a_player || !a_actor || a_actor->IsDead() || !(a_saDamage > 0.0f)) {
            return;
        }
        Victim victim;
        victim.actor = a_actor;
        victim.at = a_at;
        victim.normal = -a_dir;
        Protocol::ShotEvent shot{};
        shot.weaponType = a_weapon;
        shot.kind = Protocol::kShotBullet;
        Hurt(a_player, victim, shot, a_dir, a_saDamage, a_blood);
    }

    Damage::Class ClassOf(RE::Actor* a_actor) { return ClassOfRace(a_actor); }
}
