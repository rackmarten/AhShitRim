#include "player/Respawns.h"

#include "revive.h"

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
            bool          unique = false;
            bool          byPlayer = false;
        };

        // The sink may run off the main thread; Update takes what it queued.
        struct State
        {
            std::mutex        lock;
            std::vector<Seen> seen;
            Revive::Roster    roster;  // main thread
            float             sinceCheck = 0.0f;
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

        bool HomeInView(const Home& a_home, RE::PlayerCharacter* a_player)
        {
            if (a_home.cell) {
                return Revive::HomeInView(true, a_home.cell->IsAttached(), false, 0.0f);
            }
            const bool inWorld = a_player && a_player->GetWorldspace() == a_home.world;
            return Revive::HomeInView(false, false, inWorld, inWorld ? a_player->GetPosition().GetDistance(a_home.position) : 0.0f);
        }

        bool BodyLoaded(RE::Actor* a_actor)
        {
            const auto* cell = a_actor->GetParentCell();
            return a_actor->Is3DLoaded() || (cell && cell->IsAttached());
        }

        // Brings a_death back if it's time. Returns whether it's no longer waiting (back, or gone for good).
        bool Check(const Revive::Death& a_death, float a_now, RE::PlayerCharacter* a_player)
        {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_death.id);
            auto* base = actor ? actor->GetActorBase() : nullptr;
            if (!base || base->GetFormID() != a_death.base) {
                SKSE::log::info("respawns: {:08X} is gone or someone else now, not brought back", a_death.id);
                return true;
            }
            if (!actor->IsDead()) {
                SKSE::log::info("respawns: {} ({:08X}) is up already", NameOf(actor), a_death.id);
                return true;
            }
            Home       home;
            const bool hasHome = HomeOf(actor, home);
            if (!Revive::Ready(a_death.at, a_now, BodyLoaded(actor), hasHome && HomeInView(home, a_player))) {
                return false;
            }
            actor->Resurrect(true, false);
            if (hasHome) {
                MoveTo(actor, home.cell ? home.cell : home.world->persistentCell, home.world, home.position, home.angle);
            }
            SKSE::log::info("respawns: {} ({:08X}) is back{} after {:.1f} game hours", NameOf(actor), a_death.id, hasHome ? " at their editor spot" : " where they fell",
                (a_now - a_death.at) * 24.0f);
            return true;
        }

        class DeathSink : public RE::BSTEventSink<RE::TESDeathEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
            {
                auto* dying = a_event ? a_event->actorDying.get() : nullptr;
                auto* actor = dying ? dying->As<RE::Actor>() : nullptr;
                auto* base = actor ? actor->GetActorBase() : nullptr;
                if (!base) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                const auto* killer = a_event->actorKiller.get();
                const Seen  seen{ actor->GetFormID(), base->GetFormID(), base->IsUnique(), killer && killer == RE::PlayerCharacter::GetSingleton() };
                std::scoped_lock guard(state.lock);
                state.seen.push_back(seen);
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void OnDataLoaded()
    {
        static DeathSink deaths;
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESDeathEvent>(&deaths);
            SKSE::log::info("respawns: unique people CJ kills are back {:.0f} game minutes later, out of sight", Revive::kDelayDays * 24.0f * 60.0f);
        }
    }

    void Update(bool a_cjHasPlayer, float a_delta)
    {
        std::vector<Seen> seen;
        {
            std::scoped_lock guard(state.lock);
            seen.swap(state.seen);
        }
        for (const auto& death : seen) {
            if (Revive::Counts(a_cjHasPlayer, death.byPlayer, death.unique, death.id)) {
                state.roster.Note(death.id, death.base, Now());
                SKSE::log::info("respawns: CJ killed {:08X} (base {:08X}), back in a game hour", death.id, death.base);
            }
        }
        state.sinceCheck += a_delta;
        if (state.sinceCheck < kCheckEvery || state.roster.Empty()) {
            return;
        }
        state.sinceCheck = 0.0f;
        const float now = Now();
        auto*       player = RE::PlayerCharacter::GetSingleton();
        const auto  dead = state.roster.Dead();  // Check can't change it under us, but Remove does
        for (const auto& death : dead) {
            if (Check(death, now, player)) {
                state.roster.Remove(death.id);
            }
        }
    }

    void OnGameLoad()
    {
        std::scoped_lock guard(state.lock);
        state.seen.clear();
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
        state.sinceCheck = 0.0f;
    }
}
