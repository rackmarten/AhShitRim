#include "player/People.h"

#include "Ini.h"
#include "hud/SaHud.h"
#include "link/LinkService.h"
#include "reactions.h"
#include "weapons.h"

namespace AhShitRim::People
{
    namespace
    {
        constexpr RE::FormID kIdleCoweringLoose = 0x000EFC68;  // the cowering loop (event IdleCowering), no conditions
        constexpr RE::FormID kIdleCower = 0x000FF9E4;          // the same loop with conditions, if the other is missing
        constexpr double     kAimCheckEvery = 0.2;             // who CJ aims at, looked at this often
        constexpr double     kSawCheckEvery = 0.25;            // and who is near the running chainsaw
        constexpr float      kBodyRadius = 25.0f;              // a person around the chest, for the aim
        constexpr float      kChestHeight = 0.7f;              // of an actor's height
        constexpr float      kCjChest = 100.0f;                // over CJ's feet, where Skyrim's player looks from
        constexpr int        kLogged = 30;                     // the first reactions are logged one by one

        struct State
        {
            Reactions::Scares   scares;
            Reactions::Bounties bounties;
            RE::TESIdleForm*    cower = nullptr;
            std::int32_t        bounty = Reactions::kGunfireBounty;
            bool                enabled = true;
            bool                haveShots = false;
            std::uint16_t       lastShots = 0;
            std::uint32_t       handover = 0;
            std::vector<RE::NiPoint3> blasts;
            double              cutAt = -1e9, alertAt = -1e9, aimCheckAt = -1e9, sawCheckAt = -1e9;
            int                 logged = 0;
            unsigned            cowered = 0, fled = 0, reported = 0;  // for the summary
            std::chrono::steady_clock::time_point summaryAt;
        } state;

        double NowSeconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        void Log(RE::Actor* a_actor, const char* a_what)
        {
            if (state.logged < kLogged) {
                state.logged++;
                SKSE::log::info("people: {} ({:08X}) {}", a_actor->GetName(), a_actor->GetFormID(), a_what);
            }
        }

        Reactions::Person PersonOf(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
        {
            Reactions::Person person;
            auto*             race = a_actor->GetRace();
            person.npc = race && race->HasKeywordString("ActorTypeNPC");
            person.dead = a_actor->IsDead();
            person.follower = a_actor->IsPlayerTeammate() || a_actor->IsCommandedActor();
            person.guard = a_actor->IsGuard();
            person.hostile = a_actor->IsHostileToActor(a_player);
            person.fighting = a_actor->IsInCombat();
            person.confidence = static_cast<int>(a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kConfidence));
            return person;
        }

        // Every loaded actor within a_radius of a_at but the player.
        template <class F>
        void ForPeopleNear(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, float a_radius, F&& a_each)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            for (auto& handle : lists->highActorHandles) {
                auto  ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == a_player || !actor->Is3DLoaded()) {
                    continue;
                }
                const float distance = actor->GetPosition().GetDistance(a_at);
                if (distance <= a_radius) {
                    a_each(actor, distance);
                }
            }
        }

        const char* PackageOf(RE::Actor* a_actor)
        {
            auto* process = a_actor->GetActorRuntimeData().currentProcess;
            auto* package = process ? process->GetRunningPackage() : nullptr;
            return package ? package->GetObjectTypeName() : "no package";
        }

        void Flee(RE::PlayerCharacter* a_player, RE::Actor* a_actor, double a_now)
        {
            const bool cowering = state.scares.DoingOf(a_actor->GetFormID()) == Reactions::Doing::Cowering;
            if (!state.scares.StartFlee(a_actor->GetFormID(), a_now)) {
                return;
            }
            auto* process = a_actor->GetActorRuntimeData().currentProcess;
            if (cowering && process) {
                process->StopCurrentIdle(a_actor, true);
            }
            // Away from the player, outside combat: a flee package (no crime, no aggro).
            a_actor->InitiateFlee(a_player, false, true, false, nullptr, nullptr, Reactions::kFleeDistance, Reactions::kFleeDistance);
            state.fled++;
            if (state.logged < kLogged) {
                Log(a_actor, std::format("flees (now running {})", PackageOf(a_actor)).c_str());
            }
        }

        void Cower(RE::PlayerCharacter* a_player, RE::Actor* a_actor, double a_now)
        {
            if (!state.scares.StartCower(a_actor->GetFormID(), a_now)) {
                return;
            }
            auto* process = a_actor->GetActorRuntimeData().currentProcess;
            if (state.cower && process && process->PlayIdle(a_actor, state.cower, a_player)) {
                state.cowered++;
                Log(a_actor, "cowers");
                return;
            }
            Log(a_actor, "won't take the cowering idle, flees instead");
            Flee(a_player, a_actor, a_now);
        }

        void LetGo(std::uint32_t a_id, Reactions::Doing a_doing)
        {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
            if (!actor || actor->IsDead()) {
                return;
            }
            auto* process = actor->GetActorRuntimeData().currentProcess;
            if (a_doing == Reactions::Doing::Cowering && process) {
                process->StopCurrentIdle(actor, true);
            } else if (a_doing == Reactions::Doing::Fleeing) {
                actor->EvaluatePackage(true, false);
            }
            if (state.logged < kLogged) {
                Log(actor, std::format("calms down ({})", PackageOf(actor)).c_str());
            }
        }

        // The player's detection event: everyone in earshot hears it as one of Skyrim's own sounds.
        void Alert(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, Reactions::Sound a_sound, double a_now)
        {
            if (a_now - state.alertAt < Reactions::kAlertEvery) {
                return;
            }
            state.alertAt = a_now;
            if (auto* process = a_player->GetActorRuntimeData().currentProcess) {
                process->SetActorsDetectionEvent(a_player, a_at, RE::AIFormulas::GetSoundLevelValue(static_cast<RE::SOUND_LEVEL>(a_sound)), a_player);
            }
        }

        // A guard heard it: a bounty for the hold.
        void Report(RE::PlayerCharacter* a_player, RE::Actor* a_guard, double a_now)
        {
            auto* faction = a_guard->GetCrimeFaction();
            if (state.bounty <= 0 || !faction || !state.bounties.Due(faction->GetFormID(), a_player->IsInCombat(), a_now)) {
                return;
            }
            a_player->ModCrimeGoldValue(faction, false, state.bounty);
            state.reported++;
            SKSE::log::info("people: {} ({:08X}) heard the gunfire: {} gold bounty with {:08X}", a_guard->GetName(), a_guard->GetFormID(), state.bounty,
                faction->GetFormID());
            SaHud::ShowHelp(std::format("Shots fired: {} gold bounty", state.bounty), 4.0f);
        }

        // Shots or a blast at a_at: civilians who hear it flee, guards who hear it report it.
        void Gunfire(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, const Reactions::Noise& a_noise, double a_now)
        {
            Alert(a_player, a_at, a_noise.sound, a_now);
            ForPeopleNear(a_player, a_at, (std::max)(a_noise.heard, a_noise.seen), [&](RE::Actor* a_actor, float a_distance) {
                const auto person = PersonOf(a_actor, a_player);
                const auto role = Reactions::RoleOf(person);
                if (role == Reactions::Role::None || (role == Reactions::Role::Civilian && !Reactions::Reacts(person.confidence, Reactions::Cause::Gunfire))) {
                    return;
                }
                bool sees = false;
                if (a_distance > a_noise.heard) {  // only seen out here: facing CJ, with a line of sight to him
                    const float  heading = a_actor->GetAngleZ();
                    const auto   to = a_at - a_actor->GetPosition();
                    bool         unused = false;
                    sees = std::sin(heading) * to.x + std::cos(heading) * to.y > 0.0f && a_actor->HasLineOfSight(a_player, unused);
                }
                if (!Reactions::Hears(a_noise, a_distance, sees)) {
                    return;
                }
                if (role == Reactions::Role::Guard) {
                    Report(a_player, a_actor, a_now);
                } else {
                    Flee(a_player, a_actor, a_now);
                }
            });
        }

        // Who CJ aims at: the nearest person along San Andreas's aim ray he has a line of sight to.
        void Aim(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState& a_pose, double a_now)
        {
            const float range = Reactions::AimRange(a_pose.weaponType);
            if (range <= 0.0f || a_now - state.aimCheckAt < kAimCheckEvery) {
                return;
            }
            state.aimCheckAt = a_now;
            Protocol::BodyState body{};
            if (!LinkService::Get().ReadBody(body) || (body.flags & Protocol::kBodyValid) == 0 || body.handover != state.handover) {
                return;
            }
            const bool  free = Weapons::ScopeOf(a_pose.cameraMode) == Weapons::Scope::Aim;
            const auto  ray = Reactions::AimRay({ body.cameraForward[0], body.cameraForward[1], body.cameraForward[2] },
                 { body.cameraUp[0], body.cameraUp[1], body.cameraUp[2] }, body.cameraFov, free ? a_pose.crosshair[0] : 0.5f, free ? a_pose.crosshair[1] : 0.5f);
            const Reactions::Vec3 camera{ a_anchor.x + body.cameraPosition[0], a_anchor.y + body.cameraPosition[1], a_anchor.z + body.cameraPosition[2] };
            const RE::NiPoint3    cj = a_anchor + RE::NiPoint3(a_pose.offset[0], a_pose.offset[1], a_pose.offset[2] + kCjChest);
            const float nearest = Reactions::Dot({ cj.x - camera.x, cj.y - camera.y, cj.z - camera.z }, ray) + kBodyRadius;
            if (!std::isfinite(ray.x + ray.y + ray.z + camera.x + camera.y + camera.z)) {
                return;
            }

            RE::Actor* target = nullptr;
            float      best = range + 1.0f;
            ForPeopleNear(a_player, cj, range + 500.0f, [&](RE::Actor* a_actor, float) {
                const auto feet = a_actor->GetPosition();
                const float along = Reactions::AimedAlong(camera, ray, { feet.x, feet.y, feet.z + a_actor->GetHeight() * kChestHeight }, nearest, range + nearest,
                    kBodyRadius * a_actor->GetScale());
                if (along >= 0.0f && along < best) {
                    best = along;
                    target = a_actor;
                }
            });
            if (!target) {
                return;
            }
            const auto person = PersonOf(target, a_player);
            if (Reactions::RoleOf(person) != Reactions::Role::Civilian || !Reactions::Reacts(person.confidence, Reactions::Cause::Aimed)) {
                return;
            }
            bool unused = false;
            if (target->HasLineOfSight(a_player, unused)) {
                Cower(a_player, target, a_now);
            }
        }

        // The chainsaw in CJ's hands: everyone close by runs, further while it cuts.
        void Chainsaw(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cj, bool a_cutting, double a_now)
        {
            if (a_now - state.sawCheckAt < kSawCheckEvery) {
                return;
            }
            state.sawCheckAt = a_now;
            Alert(a_player, a_cj, a_cutting ? Reactions::Sound::Loud : Reactions::Sound::Normal, a_now);
            ForPeopleNear(a_player, a_cj, a_cutting ? Reactions::kChainsawCutting : Reactions::kChainsawRunning, [&](RE::Actor* a_actor, float) {
                const auto person = PersonOf(a_actor, a_player);
                if (Reactions::RoleOf(person) == Reactions::Role::Civilian && Reactions::Reacts(person.confidence, Reactions::Cause::Chainsaw)) {
                    Flee(a_player, a_actor, a_now);
                }
            });
        }

        void Summary()
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - state.summaryAt < std::chrono::seconds(10)) {
                return;
            }
            if (state.cowered || state.fled || state.reported) {
                SKSE::log::info("people: {} cowered, {} fled, {} bounties; {} still scared", state.cowered, state.fled, state.reported, state.scares.Size());
            }
            state.cowered = state.fled = state.reported = 0;
            state.summaryAt = now;
        }
    }

    void OnDataLoaded()
    {
        state.cower = RE::TESForm::LookupByID<RE::TESIdleForm>(kIdleCoweringLoose);
        if (!state.cower) {
            state.cower = RE::TESForm::LookupByID<RE::TESIdleForm>(kIdleCower);
        }
        state.enabled = Ini::Value("Reactions", "Enabled") != "0";
        if (const auto bounty = Ini::Value("Reactions", "GunfireBounty"); !bounty.empty()) {
            try {
                state.bounty = std::stoi(bounty);
            } catch (...) {
                SKSE::log::warn("people: [Reactions] GunfireBounty={} isn't a number, kept {}", bounty, state.bounty);
            }
        }
        SKSE::log::info("people: reactions {}; cowering idle {}, {} gold bounty for gunfire a guard hears", state.enabled ? "on" : "off",
            state.cower ? std::format("{:08X}", state.cower->GetFormID()) : std::string("not found (aimed at, people flee)"), state.bounty);
    }

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, bool a_quiet)
    {
        const double now = NowSeconds();
        for (const auto& [id, doing] : state.scares.Expired(now)) {
            LetGo(id, doing);
        }
        auto blasts = std::move(state.blasts);
        state.blasts.clear();
        if (!a_pose) {
            return;
        }
        if (!state.haveShots || a_pose->handover != state.handover) {
            state.haveShots = true;
            state.handover = a_pose->handover;
            state.lastShots = a_pose->shots;
        }
        const auto shots = Reactions::ShotsSince(state.lastShots, a_pose->shots);
        state.lastShots = a_pose->shots;
        if (!state.enabled || a_quiet) {
            return;
        }

        const RE::NiPoint3 cj = a_anchor + RE::NiPoint3(a_pose->offset[0], a_pose->offset[1], a_pose->offset[2]);
        for (const auto& at : blasts) {
            Gunfire(a_player, at, { Reactions::kBlastHeard, 0.0f, Reactions::Sound::VeryLoud }, now);
        }
        if (shots > 0) {
            if (const auto noise = Reactions::NoiseOf(a_pose->weaponType); noise.noisy()) {
                Gunfire(a_player, cj, noise, now);
            }
        }
        const bool onFoot = (a_pose->flags & Protocol::kPlayerInVehicle) == 0;
        if (onFoot && (a_pose->weaponFlags & Protocol::kWeaponAiming)) {
            Aim(a_player, a_anchor, *a_pose, now);
        }
        if (onFoot && a_pose->weaponType == Weapons::Chainsaw && (a_pose->weaponFlags & Protocol::kWeaponInHand)) {
            Chainsaw(a_player, cj, now - state.cutAt < Reactions::kCutLasts || (a_pose->weaponFlags & Protocol::kWeaponFiring), now);
        }
        Summary();
    }

    void Blast(const RE::NiPoint3& a_at)
    {
        if (state.blasts.size() < 16) {
            state.blasts.push_back(a_at);
        }
    }

    void ChainsawCut() { state.cutAt = NowSeconds(); }

    void Clear()
    {
        for (const auto& [id, doing] : state.scares.All()) {
            LetGo(id, doing);
        }
        state.blasts.clear();
        state.haveShots = false;
        state.cutAt = -1e9;
    }
}
