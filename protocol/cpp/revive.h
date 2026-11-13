// Coming back (v22, everyone not hostile since 0.2), the game-free half: whoever CJ kills that wasn't hostile
// to the player on sight (guards, citizens, merchants, the main quest's people, chickens and cows) gets up
// again an in-game hour later, at their editor spot, once neither their body nor that spot is in view of the
// camera. Bandits, necromancers, draugr and wild predators stay dead until Skyrim resets their cell, and so
// does a death a quest needed: its kill target, or a death that moved a quest on. NOMERCY or not: it lets
// him kill the essential ones, this brings them back. Only CJ's kills: the player killed them while San
// Andreas had the player. Skyrim's own player killing someone, or a dragon, stays as Skyrim has it.
// Header-only and free of game types, so the protocol tests cover it; the SKSE plugin listens for deaths
// and brings them back (skse/src/player/Respawns.cpp).
//
// The dead waiting are in Skyrim's co-save, a record of their own ("ASRV"): version 1 is a u32 count, then
// each one's form id, its base's and when it died (f32, Skyrim's game time in days).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace AhShitRim::Revive
{
    inline constexpr std::uint32_t kRecordType = 0x56525341;  // "ASRV"
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::uint32_t kListMax = 1u << 16;       // more dead than any game has: a bigger count is a broken record

    inline constexpr std::uint32_t kPlayerRef = 0x00000014;   // the player
    inline constexpr std::uint32_t kTemporary = 0xFF000000;   // form ids from here on are made at runtime: no editor spot to go back to

    inline constexpr float kDelayDays = 1.0f / 24.0f;  // an in-game hour (3 real minutes at Skyrim's timescale)
    inline constexpr float kForgetDays = 10.0f;        // someone whose cell stays unloaded this long is Skyrim's: its cell reset brings them back
    inline constexpr float kQuestWindow = 2.0f;        // real seconds after a death in which a quest it moves on keeps them dead

    // ---- Who comes back ----

    // Skyrim's aggression (0 unaggressive, 1 aggressive, 2 very aggressive, 3 frenzied).
    inline constexpr int kAggressive = 1;
    inline constexpr int kVeryAggressive = 2;

    // Whether an actor attacks the player on sight, before anyone has done anything: a very aggressive or
    // frenzied one attacks neutrals too, an aggressive one whoever Skyrim says it's hostile to
    // (Actor::IsHostileToActor). The faction reaction alone doesn't say it: a bandit's to the player is
    // neutral (measured at Halted Stream Camp), the same as a guard's. A lawful actor (one with a crime faction:
    // guards, townsfolk) is hostile only over a crime, a fight CJ started, so that doesn't count. Its base
    // aggression, so a Fury spell doesn't change the answer.
    constexpr bool HostileOnSight(int a_aggression, bool a_hostile, bool a_lawful)
    {
        return a_aggression >= kVeryAggressive || (a_aggression >= kAggressive && a_hostile && !a_lawful);
    }

    // What the plugin knows of a death when it happens.
    struct Kill
    {
        bool          cjHasPlayer = false;  // San Andreas had the player
        bool          byPlayer = false;     // the player killed them
        std::uint32_t id = 0;
        int           aggression = 0;      // base
        bool          hostile = false;     // Skyrim's IsHostileToActor(player)
        bool          lawful = false;      // has a crime faction (a hold's people)
        bool          spared = false;      // essential, by its base, itself, a quest alias holding it, or before NOMERCY cleared it
        bool          killTarget = false;  // a running quest's shown objective points at an alias holding them (KillTarget)
    };

    enum class Verdict
    {
        Back,        // comes back an hour later
        NotCj,       // Skyrim's own player, a dragon, a guard: Skyrim's death
        NotPlaced,   // the player, or made at runtime: no editor spot to go back to
        Hostile,     // hostile on sight: stays dead until Skyrim resets the cell
        KillTarget,  // a quest wants them dead
    };

    // Whether a death brings them back later. An essential one always does, hostile or a quest's target: a
    // quest that holds someone essential needs them, and NOMERCY's essentials come back.
    constexpr Verdict Judge(const Kill& a_kill)
    {
        if (!a_kill.cjHasPlayer || !a_kill.byPlayer) {
            return Verdict::NotCj;
        }
        if (a_kill.id == kPlayerRef || a_kill.id >= kTemporary) {
            return Verdict::NotPlaced;
        }
        if (a_kill.spared) {
            return Verdict::Back;
        }
        if (HostileOnSight(a_kill.aggression, a_kill.hostile, a_kill.lawful)) {
            return Verdict::Hostile;
        }
        return a_kill.killTarget ? Verdict::KillTarget : Verdict::Back;
    }

    constexpr const char* Why(Verdict a_verdict)
    {
        switch (a_verdict) {
        case Verdict::Back: return "back in a game hour";
        case Verdict::NotCj: return "not CJ's kill";
        case Verdict::NotPlaced: return "not placed in the editor";
        case Verdict::Hostile: return "hostile on sight, stays dead";
        case Verdict::KillTarget: return "a quest's kill target, stays dead";
        }
        return "?";
    }

    // ---- Quest deaths ----

    // Skyrim's objective states (QUEST_OBJECTIVE_STATE): only a shown one (displayed) is what the quest asks now.
    inline constexpr std::uint8_t kObjectiveDisplayed = 1;

    // A quest alias holding the dead (the actor's ExtraAliasInstanceArray).
    struct Holder
    {
        std::uint32_t quest = 0;
        std::uint32_t alias = 0;
        bool          running = false;
    };

    // One of a quest's objectives and the aliases its targets point at.
    struct Objective
    {
        std::uint32_t              quest = 0;
        std::uint8_t               state = 0;
        std::vector<std::uint32_t> aliases;
    };

    // The quest whose kill target a_holders make the dead, or 0: a running quest holds them in an alias that one
    // of its shown objectives points at ("Kill the bandit leader", a bounty's giant). Skyrim's objectives
    // don't say "kill", so a shown objective on someone the quest doesn't hold essential is taken for one.
    inline std::uint32_t KillTarget(const std::vector<Holder>& a_holders, const std::vector<Objective>& a_objectives)
    {
        for (const auto& holder : a_holders) {
            if (!holder.running) {
                continue;
            }
            for (const auto& objective : a_objectives) {
                if (objective.quest == holder.quest && objective.state == kObjectiveDisplayed &&
                    std::find(objective.aliases.begin(), objective.aliases.end(), holder.alias) != objective.aliases.end()) {
                    return holder.quest;
                }
            }
        }
        return 0;
    }

    // Where a quest holding the dead was: its stage and each objective's state.
    struct QuestMark
    {
        std::uint32_t             quest = 0;
        std::uint16_t             stage = 0;
        std::vector<std::uint8_t> objectives;
        bool operator==(const QuestMark&) const = default;
    };

    constexpr bool Completed(std::uint8_t a_state) { return a_state == 2 || a_state == 3; }  // kCompleted, kCompletedDisplayed

    // Only a quest the player is in (one of its objectives shown at some point: any state but dormant) has deaths that
    // move it on. Quests the player never started hold townsfolk too and set their stages when they die: a dialogue
    // or scene quest with no objectives at all (Riverwood's intro scene, Hilde), or a dormant one (FreeformRiverwood01,
    // Faendal, before anyone talked to him). Those aren't quest deaths, or the townsfolk would never come back.
    inline bool Watched(const QuestMark& a_mark)
    {
        return std::any_of(a_mark.objectives.begin(), a_mark.objectives.end(), [](std::uint8_t a_state) { return a_state != 0; });
    }

    // The deaths of the last kQuestWindow seconds, each with the quests holding them as they were then: a stage
    // set in one of those quests while the window is open, or a stage or a completed objective found when it
    // closes, and the death moved it on, so the dead stay dead. A quest that only stops (a scene or a radiant
    // quest whose actor died) doesn't count.
    class QuestWatch
    {
    public:
        struct Watch
        {
            std::uint32_t          id = 0;
            float                  until = 0.0f;  // real seconds
            std::vector<QuestMark> quests;
        };

        // a_id died at a_now (real seconds) held by a_quests. Only the quests with objectives are watched (Watched);
        // none of those: nothing to watch.
        void Start(std::uint32_t a_id, float a_now, std::vector<QuestMark> a_quests)
        {
            Stop(a_id);
            std::erase_if(a_quests, [](const QuestMark& a_mark) { return !Watched(a_mark); });
            if (!a_quests.empty()) {
                _watches.push_back(Watch{ a_id, a_now + kQuestWindow, std::move(a_quests) });
            }
        }

        // A stage was set in a_quest at a_now: whoever it held and died within the window, taken out of the
        // watch. Their quest death.
        std::vector<std::uint32_t> Stage(std::uint32_t a_quest, float a_now)
        {
            std::vector<std::uint32_t> moved;
            std::erase_if(_watches, [&](const Watch& a_watch) {
                if (a_now > a_watch.until) {
                    return false;  // Due takes it
                }
                for (const auto& mark : a_watch.quests) {
                    if (mark.quest == a_quest) {
                        moved.push_back(a_watch.id);
                        return true;
                    }
                }
                return false;
            });
            return moved;
        }

        // The watches whose window closed by a_now, taken out: the plugin compares their quests with now (Moved).
        std::vector<Watch> Due(float a_now)
        {
            std::vector<Watch> due;
            std::erase_if(_watches, [&](Watch& a_watch) {
                if (a_now <= a_watch.until) {
                    return false;
                }
                due.push_back(std::move(a_watch));
                return true;
            });
            return due;
        }

        bool Watching(std::uint32_t a_id) const
        {
            return std::any_of(_watches.begin(), _watches.end(), [&](const Watch& a_watch) { return a_watch.id == a_id; });
        }

        void Stop(std::uint32_t a_id)
        {
            std::erase_if(_watches, [&](const Watch& a_watch) { return a_watch.id == a_id; });
        }

        bool Empty() const { return _watches.empty(); }
        void Clear() { _watches.clear(); }

    private:
        std::vector<Watch> _watches;
    };

    // The quest of a_before that moved on by a_after (the same quests, looked at again), or 0: another stage, or
    // an objective completed that wasn't. A quest no longer found (stopped, its alias cleared) didn't move.
    inline std::uint32_t Moved(const std::vector<QuestMark>& a_before, const std::vector<QuestMark>& a_after)
    {
        for (const auto& before : a_before) {
            const auto after = std::find_if(a_after.begin(), a_after.end(), [&](const QuestMark& a_mark) { return a_mark.quest == before.quest; });
            if (after == a_after.end()) {
                continue;
            }
            if (after->stage != before.stage) {
                return before.quest;
            }
            for (std::size_t i = 0; i < after->objectives.size(); i++) {
                if (Completed(after->objectives[i]) && (i >= before.objectives.size() || !Completed(before.objectives[i]))) {
                    return before.quest;
                }
            }
        }
        return 0;
    }

    // ---- In view ----

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    constexpr Vec3  Sub(const Vec3& a_a, const Vec3& a_b) { return { a_a.x - a_b.x, a_a.y - a_b.y, a_a.z - a_b.z }; }
    constexpr float Dot(const Vec3& a_a, const Vec3& a_b) { return a_a.x * a_b.x + a_a.y * a_b.y + a_a.z * a_b.z; }

    // The camera: where it is, its unit axes, and its frustum's half-width and half-height a unit ahead (the
    // tangents of half the field of view, Gamebryo's NiFrustum right and top).
    struct View
    {
        Vec3  eye;
        Vec3  forward;
        Vec3  up;
        Vec3  right;
        float tanRight = 1.0f;
        float tanTop = 0.6f;
    };

    inline constexpr float kViewRange = 8192.0f;    // Skyrim units, two cells: farther off, a person is a few pixels and fades out
    inline constexpr float kPersonRadius = 80.0f;   // a sphere around someone standing or lying there
    inline constexpr float kPersonMiddle = 60.0f;   // its centre above the feet

    // Whether a sphere at a_point (radius a_radius) is inside a_view's frustum and range. A symmetric frustum,
    // as Skyrim's is: each side plane goes through the eye.
    inline bool InFrustum(const View& a_view, const Vec3& a_point, float a_radius)
    {
        const Vec3  to = Sub(a_point, a_view.eye);
        const float ahead = Dot(to, a_view.forward);
        if (ahead < -a_radius || Dot(to, to) > (kViewRange + a_radius) * (kViewRange + a_radius)) {
            return false;
        }
        const float side = Dot(to, a_view.right);
        const float high = Dot(to, a_view.up);
        // the distance outside a side plane: (|side| - tan * ahead) over the plane normal's length
        const auto outside = [&](float a_off, float a_tan) { return (std::fabs(a_off) - a_tan * ahead) / std::sqrt(1.0f + a_tan * a_tan) > a_radius; };
        return !outside(side, a_view.tanRight) && !outside(high, a_view.tanTop);
    }

    // Whether someone at a_point can be seen: in the player's space (the same interior, or the same worldspace
    // outdoors), in the frustum, and nothing in between (a_clear: the plugin's line of sight).
    constexpr bool Seen(bool a_sameSpace, bool a_inFrustum, bool a_clear)
    {
        return a_sameSpace && a_inFrustum && a_clear;
    }

    // ---- When ----

    // Whether someone who died at a_diedAt gets up at a_now: an hour gone, neither the body nor the editor spot
    // in view. A death later than now (a clock that went back) can't be waited out, so it counts as gone.
    constexpr bool Ready(float a_diedAt, float a_now, bool a_bodyInView, bool a_homeInView)
    {
        const bool hourGone = a_now < a_diedAt || a_now - a_diedAt >= kDelayDays;
        return hourGone && !a_bodyInView && !a_homeInView;
    }

    // Whether someone not in memory (a cell that isn't loaded keeps its people on disk) is left to Skyrim: its
    // cell reset brings them back after kForgetDays.
    constexpr bool Forget(float a_diedAt, float a_now)
    {
        return a_now >= a_diedAt && a_now - a_diedAt >= kForgetDays;
    }

    // One dead and waiting, with its base: the same id with another base after a load is someone else.
    struct Death
    {
        std::uint32_t id = 0;
        std::uint32_t base = 0;
        float         at = 0.0f;  // game days
        bool operator==(const Death&) const = default;
    };

    class Roster
    {
    public:
        // a_id died at a_at. Dying again (someone else brought them back first) starts the hour again.
        void Note(std::uint32_t a_id, std::uint32_t a_base, float a_at)
        {
            const auto at = Find(a_id);
            if (at != _dead.end() && at->id == a_id) {
                *at = Death{ a_id, a_base, a_at };
                return;
            }
            _dead.insert(at, Death{ a_id, a_base, a_at });
        }

        // a_id is back (or gone): no longer waiting. Returns false if it wasn't.
        bool Remove(std::uint32_t a_id)
        {
            const auto at = Find(a_id);
            if (at == _dead.end() || at->id != a_id) {
                return false;
            }
            _dead.erase(at);
            return true;
        }

        const std::vector<Death>& Dead() const { return _dead; }
        bool                      Empty() const { return _dead.empty(); }
        void                      Clear() { _dead.clear(); }

    private:
        std::vector<Death>::iterator Find(std::uint32_t a_id)
        {
            return std::lower_bound(_dead.begin(), _dead.end(), a_id, [](const Death& a_d, std::uint32_t a_v) { return a_d.id < a_v; });
        }

        std::vector<Death> _dead;  // sorted by id
    };

    namespace detail
    {
        template <class T>
        void Put(std::vector<std::uint8_t>& a_out, T a_value)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&a_value);
            a_out.insert(a_out.end(), bytes, bytes + sizeof(a_value));
        }

        template <class T>
        bool Get(const std::uint8_t*& a_at, const std::uint8_t* a_end, T& a_value)
        {
            if (a_end - a_at < static_cast<std::ptrdiff_t>(sizeof(a_value))) {
                return false;
            }
            std::memcpy(&a_value, a_at, sizeof(a_value));
            a_at += sizeof(a_value);
            return true;
        }
    }

    inline std::vector<std::uint8_t> Write(const Roster& a_roster)
    {
        std::vector<std::uint8_t> out;
        detail::Put(out, static_cast<std::uint32_t>(a_roster.Dead().size()));
        for (const auto& death : a_roster.Dead()) {
            detail::Put(out, death.id);
            detail::Put(out, death.base);
            detail::Put(out, death.at);
        }
        return out;
    }

    // Reads a record into a_out; false (a_out empty) for a newer version or one that doesn't read.
    inline bool Read(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size, Roster& a_out)
    {
        a_out = {};
        if (a_version < 1 || a_version > kVersion) {
            return false;
        }
        const std::uint8_t* at = a_data;
        const std::uint8_t* end = a_data + a_size;
        std::uint32_t       count = 0;
        if (!detail::Get(at, end, count) || count > kListMax) {
            return false;
        }
        Roster roster;
        for (std::uint32_t i = 0; i < count; ++i) {
            Death death;
            if (!detail::Get(at, end, death.id) || !detail::Get(at, end, death.base) || !detail::Get(at, end, death.at) || death.at != death.at) {
                return false;
            }
            roster.Note(death.id, death.base, death.at);
        }
        a_out = std::move(roster);
        return true;
    }
}
