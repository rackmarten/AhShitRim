#include "player/Respawns.h"

#include "player/CheatCodes.h"

#include <mutex>

namespace AhShitRim::Respawns
{
    namespace
    {
        constexpr float kCheckEvery = 1.0f;  // seconds between looks at who's ready

        // A death as the sink saw it; whether it counts is decided on the main thread.
        struct Seen
        {
            std::uint32_t id = 0;
            std::uint32_t base = 0;
            bool          byPlayer = false;
        };

        // The sinks may run off the main thread; Update takes what they queued.
        struct State
        {
            std::mutex                 lock;
            std::vector<Seen>          seen;
            std::vector<std::uint32_t> stages;  // quests a stage was set in
            Revive::Roster             roster;  // main thread from here on
            Revive::QuestWatch         watch;
            float                      clock = 0.0f;  // seconds the game ran, for the quest window
            float                      sinceCheck = 0.0f;
            bool                       viewChecked = false;  // the frustum compared with the engine's projection since the load, CJ's camera
            std::vector<std::uint32_t> held;  // the hour is up but they're in view, logged once
        } state;

        float Now()
        {
            const auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetCurrentGameTime() : 0.0f;
        }

        const char* NameOf(RE::Actor* a_actor)
        {
            const char* name = a_actor->GetDisplayFullName();
            return name && *name ? name : "someone";
        }

        std::string QuestName(std::uint32_t a_quest)
        {
            const auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_quest);
            const char* id = quest ? quest->GetFormEditorID() : nullptr;
            return id && *id ? std::format("{} ({:08X})", id, a_quest) : std::format("{:08X}", a_quest);
        }

        Revive::Vec3 Vec(const RE::NiPoint3& a_point) { return { a_point.x, a_point.y, a_point.z }; }

        // ---- Quests ----

        // The quest aliases holding a_actor, and whether one of them makes it essential.
        std::vector<Revive::Holder> Holders(RE::Actor* a_actor, bool& a_essential)
        {
            std::vector<Revive::Holder> holders;
            auto* extra = a_actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
            if (!extra) {
                return holders;
            }
            RE::BSReadLockGuard guard(extra->lock);
            for (const auto* instance : extra->aliases) {
                if (!instance || !instance->quest || !instance->alias) {
                    continue;
                }
                holders.push_back({ instance->quest->GetFormID(), instance->alias->aliasID, instance->quest->IsRunning() });
                if (instance->quest->IsRunning() && instance->alias->flags.any(RE::BGSBaseAlias::FLAGS::kEssential)) {
                    a_essential = true;
                }
            }
            return holders;
        }

        std::vector<RE::TESQuest*> QuestsOf(const std::vector<Revive::Holder>& a_holders)
        {
            std::vector<RE::TESQuest*> quests;
            for (const auto& holder : a_holders) {
                auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(holder.quest);
                if (quest && std::find(quests.begin(), quests.end(), quest) == quests.end()) {
                    quests.push_back(quest);
                }
            }
            return quests;
        }

        std::vector<Revive::Objective> ObjectivesOf(const std::vector<RE::TESQuest*>& a_quests)
        {
            std::vector<Revive::Objective> objectives;
            for (auto* quest : a_quests) {
                for (const auto* objective : quest->objectives) {
                    if (!objective) {
                        continue;
                    }
                    Revive::Objective out{ quest->GetFormID(), static_cast<std::uint8_t>(objective->state.underlying()), {} };
                    for (std::uint32_t i = 0; objective->targets && i < objective->numTargets; i++) {
                        if (objective->targets[i]) {
                            out.aliases.push_back(objective->targets[i]->alias);
                        }
                    }
                    objectives.push_back(std::move(out));
                }
            }
            return objectives;
        }

        std::vector<Revive::QuestMark> MarksOf(const std::vector<RE::TESQuest*>& a_quests)
        {
            std::vector<Revive::QuestMark> marks;
            for (auto* quest : a_quests) {
                Revive::QuestMark mark{ quest->GetFormID(), quest->GetCurrentStageID(), {} };
                for (const auto* objective : quest->objectives) {
                    mark.objectives.push_back(objective ? static_cast<std::uint8_t>(objective->state.underlying()) : 0);
                }
                marks.push_back(std::move(mark));
            }
            return marks;
        }

        std::vector<Revive::QuestMark> MarksOf(RE::Actor* a_actor)
        {
            bool essential = false;
            return MarksOf(QuestsOf(Holders(a_actor, essential)));
        }

        // ---- Who ----

        struct Judged
        {
            Revive::Kill               kill;
            Revive::Verdict            verdict = Revive::Verdict::NotCj;
            std::uint32_t              target = 0;  // the quest it was a kill target of
            std::vector<RE::TESQuest*> quests;      // holding it
        };

        Judged JudgeActor(RE::Actor* a_actor, std::uint32_t a_base, bool a_cjHasPlayer, bool a_byPlayer, RE::PlayerCharacter* a_player)
        {
            Judged out;
            auto& kill = out.kill;
            kill.cjHasPlayer = a_cjHasPlayer;
            kill.byPlayer = a_byPlayer;
            kill.id = a_actor->GetFormID();
            const auto* base = a_actor->GetActorBase();
            // the base's AI data, as the grudges read it (Police::ForgivesOf): two bits, 0 unaggressive to 3 frenzied
            kill.aggression = base ? (base->aiData.aggression1 ? 1 : 0) | (base->aiData.aggression2 ? 2 : 0) : 0;
            kill.reaction = a_player ? static_cast<Revive::Reaction>(a_actor->GetFactionReaction(a_player)) : Revive::Reaction::Neutral;
            bool       aliasEssential = false;
            const auto holders = Holders(a_actor, aliasEssential);
            out.quests = QuestsOf(holders);
            kill.spared = aliasEssential || a_actor->IsEssential() || (base && base->IsEssential()) || CheatCodes::Spared(kill.id, a_base);
            out.target = Revive::KillTarget(holders, ObjectivesOf(out.quests));
            kill.killTarget = out.target != 0;
            out.verdict = Revive::Judge(kill);
            return out;
        }

        // ---- In view ----

        // The camera as the game drew the last frame. Gamebryo's camera looks along its local x, up its y, right its z.
        bool ViewOf(Revive::View& a_out)
        {
            auto* camera = RE::Main::WorldRootCamera();
            if (!camera) {
                return false;
            }
            const auto& m = camera->world.rotate.entry;
            a_out.eye = Vec(camera->world.translate);
            a_out.forward = { m[0][0], m[1][0], m[2][0] };
            a_out.up = { m[0][1], m[1][1], m[2][1] };
            a_out.right = { m[0][2], m[1][2], m[2][2] };
            const auto& frustum = camera->GetRuntimeData2().viewFrustum;
            if (!frustum.bOrtho && frustum.fRight > 0.0f && frustum.fTop > 0.0f) {
                a_out.tanRight = frustum.fRight;
                a_out.tanTop = frustum.fTop;
            }
            return true;
        }

        // Nothing solid between the camera and a_to (Skyrim's line-of-sight layer, the player's own collision left
        // out). A hit within a person's radius of a_to is them, or the ground they stand on.
        bool Clear(RE::PlayerCharacter* a_player, const Revive::Vec3& a_from, const Revive::Vec3& a_to)
        {
            auto* cell = a_player->GetParentCell();
            auto* world = cell ? cell->GetbhkWorld() : nullptr;
            if (!world) {
                return true;  // nothing to ask: count it as seen
            }
            const float  scale = RE::bhkWorld::GetWorldScale();
            RE::bhkPickData pick;
            pick.rayInput.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.0f);
            pick.rayInput.to = RE::hkVector4(a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f);
            pick.ray = RE::hkVector4((a_to.x - a_from.x) * scale, (a_to.y - a_from.y) * scale, (a_to.z - a_from.z) * scale, 0.0f);
            RE::CFilter own;
            a_player->GetCollisionFilterInfo(own);
            pick.rayInput.filterInfo.filter = (own.filter & 0xFFFF0000u) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
            {
                RE::BSReadLockGuard guard(world->worldLock);
                world->PickObject(pick);
            }
            if (!pick.rayOutput.HasHit()) {
                return true;
            }
            const auto  to = Revive::Sub(a_to, a_from);
            const float length = std::sqrt(Revive::Dot(to, to));
            return length <= 0.0f || pick.rayOutput.hitFraction * length >= length - Revive::kPersonRadius;
        }

        // Whether someone standing (or lying, a_lying) at a_feet can be seen from a_view.
        bool InView(RE::PlayerCharacter* a_player, const Revive::View& a_view, bool a_sameSpace, const RE::NiPoint3& a_feet, bool a_lying)
        {
            const Revive::Vec3 middle{ a_feet.x, a_feet.y, a_feet.z + (a_lying ? 25.0f : Revive::kPersonMiddle) };
            const bool         inFrustum = a_sameSpace && Revive::InFrustum(a_view, middle, Revive::kPersonRadius);
            if (!inFrustum) {
                return false;
            }
            const Revive::Vec3 head{ a_feet.x, a_feet.y, a_feet.z + (a_lying ? 40.0f : 2.0f * Revive::kPersonMiddle) };
            return Revive::Seen(a_sameSpace, inFrustum, Clear(a_player, a_view.eye, middle) || Clear(a_player, a_view.eye, head));
        }

        // TESObjectREFR::MoveTo_Impl, private in CommonLib: what Papyrus's MoveTo comes down to.
        void MoveTo(RE::Actor* a_actor, RE::TESObjectCELL* a_cell, RE::TESWorldSpace* a_world, const RE::NiPoint3& a_position, const RE::NiPoint3& a_angle)
        {
            using func_t = void (*)(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
            static REL::Relocation<func_t> func{ RELOCATION_ID(56227, 56626) };
            func(a_actor, a_actor->CreateRefHandle(), a_cell, a_world, a_position, a_angle);
        }

        // Where a_actor was placed in the editor, if it was.
        struct Home
        {
            RE::NiPoint3       position;
            RE::NiPoint3       angle;
            RE::TESObjectCELL* cell = nullptr;   // an interior
            RE::TESWorldSpace* world = nullptr;  // or outdoors
        };

        bool HomeOf(RE::Actor* a_actor, Home& a_out)
        {
            RE::TESForm* where = nullptr;
            if (!a_actor->GetEditorLocation(a_out.position, a_out.angle, where, nullptr) || !where) {
                return false;
            }
            a_out.cell = where->As<RE::TESObjectCELL>();
            a_out.world = where->As<RE::TESWorldSpace>();
            return a_out.cell || a_out.world;
        }

        bool HomeInView(const Home& a_home, RE::PlayerCharacter* a_player, const Revive::View& a_view)
        {
            const auto* here = a_player->GetParentCell();
            const bool  sameSpace = a_home.cell ? here == a_home.cell : here && !here->IsInteriorCell() && a_player->GetWorldspace() == a_home.world;
            return InView(a_player, a_view, sameSpace, a_home.position, false);
        }

        bool BodyInView(RE::Actor* a_actor, RE::PlayerCharacter* a_player, const Revive::View& a_view)
        {
            if (!a_actor->Is3DLoaded()) {
                return false;
            }
            const auto* here = a_player->GetParentCell();
            const auto* there = a_actor->GetParentCell();
            const bool  sameSpace = here && there && (here == there || (!here->IsInteriorCell() && !there->IsInteriorCell() && a_player->GetWorldspace() == a_actor->GetWorldspace()));
            return InView(a_player, a_view, sameSpace, a_actor->GetPosition(), true);
        }

        // Once a load: whether the frustum here and the engine's own projection agree, on the player and on
        // eight points around it (a wrong camera axis shows up at once in the log).
        void CheckView(RE::PlayerCharacter* a_player, const Revive::View& a_view)
        {
            auto* camera = RE::Main::WorldRootCamera();
            if (!camera) {
                return;
            }
            const auto feet = a_player->GetPosition();
            int        agree = 0, total = 0;
            bool       playerSeen = false;
            for (int i = -1; i < 8; i++) {
                RE::NiPoint3 at{ feet.x, feet.y, feet.z + Revive::kPersonMiddle };
                if (i >= 0) {
                    const float angle = static_cast<float>(i) * 0.785398f;
                    at.x += std::sin(angle) * 1500.0f;
                    at.y += std::cos(angle) * 1500.0f;
                }
                float      x = 0.0f, y = 0.0f, depth = 0.0f;
                const bool engine = camera->WorldPtToScreenPt3(at, x, y, depth, 1e-5f) && x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;
                const bool ours = Revive::InFrustum(a_view, Vec(at), 0.0f);
                if (i < 0) {
                    playerSeen = ours;
                }
                agree += engine == ours ? 1 : 0;
                total++;
                if (engine != ours) {
                    SKSE::log::info("respawns: view check: a point {} of the player: the engine says {}, the frustum {}", i < 0 ? "on" : "around",
                        engine ? "on screen" : "off", ours ? "in" : "out");
                }
            }
            SKSE::log::info("respawns: view check: {} of {} points agree with the engine's projection, the player {}", agree, total,
                playerSeen ? "in view" : "out of view");
        }

        // ---- When ----

        // Brings a_death back if it's time. Returns whether it's no longer waiting (back, or gone for good).
        bool Check(const Revive::Death& a_death, float a_now, RE::PlayerCharacter* a_player, const Revive::View& a_view)
        {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_death.id);
            if (!actor) {
                // Not in memory: its cell is on disk until the player comes near again.
                if (Revive::Forget(a_death.at, a_now)) {
                    SKSE::log::info("respawns: {:08X} wasn't near for {:.0f} game days, left to Skyrim's cell reset", a_death.id, Revive::kForgetDays);
                    return true;
                }
                return false;
            }
            auto* base = actor->GetActorBase();
            if (!base || base->GetFormID() != a_death.base) {
                SKSE::log::info("respawns: {:08X} is someone else now, not brought back", a_death.id);
                return true;
            }
            if (!actor->IsDead()) {
                SKSE::log::info("respawns: {} ({:08X}) is up already", NameOf(actor), a_death.id);
                return true;
            }
            if (state.watch.Watching(a_death.id)) {
                return false;  // a quest may still move on
            }
            Home       home;
            const bool hasHome = HomeOf(actor, home);
            const bool bodyInView = BodyInView(actor, a_player, a_view);
            const bool homeInView = hasHome && HomeInView(home, a_player, a_view);
            if (!Revive::Ready(a_death.at, a_now, bodyInView, homeInView)) {
                // The hour is up but they're in view: said once, so the log shows why someone is still down.
                if (Revive::Ready(a_death.at, a_now, false, false) && std::find(state.held.begin(), state.held.end(), a_death.id) == state.held.end()) {
                    state.held.push_back(a_death.id);
                    SKSE::log::info("respawns: {} ({:08X}) waits, {} in view", NameOf(actor), a_death.id, bodyInView ? "the body is" : "their spot is");
                }
                return false;
            }
            std::erase(state.held, a_death.id);
            actor->Resurrect(true, false);
            if (hasHome) {
                MoveTo(actor, home.cell ? home.cell : home.world->persistentCell, home.world, home.position, home.angle);
            }
            SKSE::log::info("respawns: {} ({:08X}) is back{} after {:.1f} game hours", NameOf(actor), a_death.id, hasHome ? " at their editor spot" : " where they fell",
                (a_now - a_death.at) * 24.0f);
            return true;
        }

        // A death the sink saw: noted to come back, or why not.
        void Note(const Seen& a_seen, bool a_cjHasPlayer, RE::PlayerCharacter* a_player)
        {
            if (!a_cjHasPlayer || !a_seen.byPlayer) {
                return;  // Skyrim's death, not worth a line
            }
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_seen.id);
            if (!actor) {
                return;
            }
            const auto judged = JudgeActor(actor, a_seen.base, a_cjHasPlayer, a_seen.byPlayer, a_player);
            const auto& kill = judged.kill;
            if (judged.verdict != Revive::Verdict::Back) {
                state.roster.Remove(a_seen.id);  // killed again after a quest made them matter
                state.watch.Stop(a_seen.id);
                SKSE::log::info("respawns: CJ killed {} ({:08X}, base {:08X}, aggression {}, reaction {}): {}{}", NameOf(actor), a_seen.id, a_seen.base, kill.aggression,
                    static_cast<int>(kill.reaction), Revive::Why(judged.verdict), judged.target ? std::format(", {}", QuestName(judged.target)) : std::string());
                return;
            }
            state.roster.Note(a_seen.id, a_seen.base, Now());
            if (kill.spared) {
                state.watch.Stop(a_seen.id);  // essential: no quest wanted them dead
            } else {
                state.watch.Start(a_seen.id, state.clock, MarksOf(judged.quests));
            }
            SKSE::log::info("respawns: CJ killed {} ({:08X}, base {:08X}){}, back in a game hour{}", NameOf(actor), a_seen.id, a_seen.base, kill.spared ? ", essential" : "",
                judged.quests.empty() ? "" : std::format(" unless one of the {} quests holding them moves on", judged.quests.size()));
        }

        void QuestDeath(std::uint32_t a_id, std::uint32_t a_quest, const char* a_how)
        {
            if (!state.roster.Remove(a_id)) {
                return;
            }
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
            SKSE::log::info("respawns: {} ({:08X}) {} {}: a quest death, stays dead", actor ? NameOf(actor) : "someone", a_id, a_how, QuestName(a_quest));
        }

        class DeathSink : public RE::BSTEventSink<RE::TESDeathEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
            {
                if (!a_event || !a_event->dead) {
                    return RE::BSEventNotifyControl::kContinue;  // the "dying" one; the "dead" one follows
                }
                auto* dying = a_event->actorDying.get();
                auto* actor = dying ? dying->As<RE::Actor>() : nullptr;
                auto* base = actor ? actor->GetActorBase() : nullptr;
                if (!base) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                const auto* killer = a_event->actorKiller.get();
                const Seen  seen{ actor->GetFormID(), base->GetFormID(), killer && killer == RE::PlayerCharacter::GetSingleton() };
                std::scoped_lock guard(state.lock);
                state.seen.push_back(seen);
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        class StageSink : public RE::BSTEventSink<RE::TESQuestStageEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* a_event, RE::BSTEventSource<RE::TESQuestStageEvent>*) override
            {
                if (a_event) {
                    std::scoped_lock guard(state.lock);
                    state.stages.push_back(a_event->formID);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void OnDataLoaded()
    {
        static DeathSink deaths;
        static StageSink stages;
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESDeathEvent>(&deaths);
            events->AddEventSink<RE::TESQuestStageEvent>(&stages);
            SKSE::log::info("respawns: whoever CJ kills that wasn't hostile on sight is back {:.0f} game minutes later, out of view, but a quest's deaths",
                Revive::kDelayDays * 24.0f * 60.0f);
        }
    }

    void Update(bool a_cjHasPlayer, float a_delta)
    {
        std::vector<Seen>          seen;
        std::vector<std::uint32_t> stages;
        {
            std::scoped_lock guard(state.lock);
            seen.swap(state.seen);
            stages.swap(state.stages);
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (a_delta > 0.0f) {
            state.clock += a_delta;
        }
        // Deaths first: a stage the death set in the same frame is then theirs.
        for (const auto& death : seen) {
            Note(death, a_cjHasPlayer, player);
        }
        for (const auto quest : stages) {
            for (const auto id : state.watch.Stage(quest, state.clock)) {
                QuestDeath(id, quest, "moved on");
            }
        }
        for (const auto& due : state.watch.Due(state.clock)) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(due.id);
            if (const auto quest = actor ? Revive::Moved(due.quests, MarksOf(actor)) : 0) {
                QuestDeath(due.id, quest, "moved on");
            }
        }

        state.sinceCheck += a_delta;
        if (state.sinceCheck < kCheckEvery) {
            return;
        }
        state.sinceCheck = 0.0f;
        auto* ui = RE::UI::GetSingleton();
        Revive::View view;
        if (!player || !player->Is3DLoaded() || !player->GetParentCell() || (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) || !ViewOf(view)) {
            return;  // no camera to hide from yet
        }
        if (!state.viewChecked && a_cjHasPlayer) {
            state.viewChecked = true;
            CheckView(player, view);
        }
        if (state.roster.Empty()) {
            return;
        }
        const float now = Now();
        const auto  dead = state.roster.Dead();  // Check can't change it under us, but Remove does
        for (const auto& death : dead) {
            if (Check(death, now, player, view)) {
                state.roster.Remove(death.id);
            }
        }
    }

    const char* DryRun(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
    {
        const auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
        return base ? Revive::Why(JudgeActor(a_actor, base->GetFormID(), true, true, a_player).verdict) : "no base";
    }

    bool DebugWaiting(std::uint32_t a_id)
    {
        const auto& dead = state.roster.Dead();
        return std::any_of(dead.begin(), dead.end(), [a_id](const Revive::Death& a_death) { return a_death.id == a_id; });
    }

    bool DebugHeld(std::uint32_t a_id) { return std::find(state.held.begin(), state.held.end(), a_id) != state.held.end(); }

    void OnGameLoad()
    {
        std::scoped_lock guard(state.lock);
        state.seen.clear();
        state.stages.clear();
        state.watch.Clear();
        state.viewChecked = false;
        state.held.clear();
    }

    void Save(SKSE::SerializationInterface* a_intfc)
    {
        const auto bytes = Revive::Write(state.roster);
        if (!a_intfc->WriteRecord(Revive::kRecordType, Revive::kVersion, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
            SKSE::log::error("respawns: the co-save record could not be written");
            return;
        }
        SKSE::log::info("respawns: saved {} dead waiting to come back", state.roster.Dead().size());
    }

    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size)
    {
        if (!Revive::Read(a_version, a_data, a_size, state.roster)) {
            SKSE::log::warn("respawns: the co-save record (version {}, {} bytes) doesn't read, nobody is waiting", a_version, a_size);
            return;
        }
        SKSE::log::info("respawns: loaded {} dead waiting to come back", state.roster.Dead().size());
    }

    void Revert()
    {
        state.roster.Clear();
        state.watch.Clear();
        state.sinceCheck = 0.0f;
    }
}
