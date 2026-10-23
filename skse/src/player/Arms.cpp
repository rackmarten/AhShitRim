#include "player/Arms.h"

#include "Ini.h"
#include "body.h"
#include "body/ModelView.h"
#include "body/MuzzleFlash.h"
#include "cosave.h"
#include "hud/SaHud.h"
#include "link/LinkService.h"
#include "pose.h"
#include "shop.h"
#include "weapons.h"

namespace AhShitRim::Arms
{
    namespace
    {
        constexpr auto kSettle = std::chrono::milliseconds(1500);  // how long San Andreas gets to apply a loadout before the pose is believed again

        struct State
        {
            bool               debugKey = true;
            std::size_t        debugStep = 0;
            Body::Interpolator hands[Protocol::kWeaponHandMax];
            Cosave::Arms       known;  // CJ's weapons as far as the save is concerned
            bool               pending = false;  // known has to be sent to San Andreas
            bool               settling = false;  // it was sent, San Andreas hasn't shown it yet
            std::chrono::steady_clock::time_point settleUntil;
            bool               wasUp = false;
            int                levels[Protocol::kWeaponSkillCount] = {};  // the skill levels shown so far
            bool               haveLevels = false;  // false until San Andreas shows the loaded save's skills
            std::uint16_t      lastShots = 0;
            bool               haveShots = false;
            unsigned           nextHand = 0;
            std::vector<std::pair<std::uint8_t, std::uint32_t>> gifts;  // weapons and rounds bought or earned, for San Andreas once it shows the loadout
        } state;

        Protocol::WeaponCommand SetCommand(const Cosave::Arms& a_arms)
        {
            Protocol::WeaponCommand command{};
            command.kind = static_cast<std::uint32_t>(Protocol::WeaponCommandKind::SetLoadout);
            command.argument = a_arms.slot;
            std::copy(std::begin(a_arms.entries), std::end(a_arms.entries), command.entries);
            std::copy(std::begin(a_arms.skills), std::end(a_arms.skills), command.skills);
            return command;
        }

        bool Send(const Protocol::WeaponCommand& a_command)
        {
            return LinkService::Get().Send(Protocol::MessageType::WeaponCommand, &a_command, sizeof(a_command));
        }

        // Whether San Andreas's weapons are the ones in a_arms, rounds and all.
        bool Same(const Protocol::PlayerState& a_pose, const Cosave::Arms& a_arms)
        {
            for (std::uint32_t i = 0; i < Protocol::kWeaponSlotCount; ++i) {
                if (a_pose.loadout[i].type != a_arms.entries[i].type || a_pose.loadout[i].ammo != a_arms.entries[i].ammo) {
                    return false;
                }
            }
            return true;
        }

        std::string Describe(const Cosave::Arms& a_arms)
        {
            std::string text;
            for (std::uint32_t i = 0; i < Protocol::kWeaponSlotCount; ++i) {
                if (a_arms.entries[i].type) {
                    text += std::format(" {}:{}", a_arms.entries[i].type, a_arms.entries[i].ammo);
                }
            }
            text = text.empty() ? " none" : text;
            std::string skills;
            for (std::uint32_t i = 0; i < Protocol::kWeaponSkillCount; ++i) {
                if (a_arms.skills[i]) {
                    skills += std::format(" {}:{}", i, a_arms.skills[i]);
                }
            }
            return text + "; skills" + (skills.empty() ? " none" : skills);
        }
    }

    void OnDataLoaded()
    {
        const auto key = Ini::Value("Debug", "WeaponKey");
        state.debugKey = key.empty() || key != "0";
        MuzzleFlash::Configure(Ini::Value("Weapons", "MuzzleLight") != "0");
        SKSE::log::info("arms: the debug weapon key (=) is {} ([Debug] WeaponKey={} in {})", state.debugKey ? "on" : "off", key.empty() ? "unset" : key, Ini::kPath);
    }

    void Update()
    {
        auto&      link = LinkService::Get();
        const bool up = link.IsUp();
        if (up && !state.wasUp && (!Weapons::Empty(state.known.entries) || std::any_of(std::begin(state.known.skills), std::end(state.known.skills), [](auto a_s) { return a_s != 0; }))) {
            state.pending = true;  // a San Andreas that started after Skyrim, or again, has none of them
        }
        state.wasUp = up;

        Protocol::PlayerState pose{};
        const bool posed = up && link.ReadPlayer(pose) && (pose.flags & Protocol::kPlayerValid) != 0 && Pose::Finite(pose);
        if (state.pending && up) {
            const auto command = SetCommand(state.known);
            if (Send(command)) {
                state.pending = false;
                state.settling = true;
                state.settleUntil = std::chrono::steady_clock::now() + kSettle;
                SKSE::log::info("arms: CJ's weapons sent to San Andreas, holding slot {}:{}", state.known.slot, Describe(state.known));
            }
        }
        // Bought or earned: given on top of what San Andreas has, once it has the loadout it was sent.
        while (up && !state.pending && !state.settling && !state.gifts.empty()) {
            Protocol::WeaponCommand command{};
            command.kind = static_cast<std::uint32_t>(Protocol::WeaponCommandKind::GiveWeapon);
            command.argument = state.gifts.front().first;
            command.entries[0].ammo = state.gifts.front().second;
            if (!Send(command)) {
                break;
            }
            SKSE::log::info("arms: weapon {} with {} rounds sent to San Andreas", command.argument, command.entries[0].ammo);
            state.gifts.erase(state.gifts.begin());
        }
        if (state.settling && posed && Same(pose, state.known)) {
            state.settling = false;
        } else if (state.settling && std::chrono::steady_clock::now() > state.settleUntil) {
            state.settling = false;
            SKSE::log::warn("arms: San Andreas still doesn't show the weapons it was sent after {} ms, believing what it shows", kSettle.count());
        }
        if (posed && !state.pending && !state.settling) {
            std::copy(std::begin(pose.loadout), std::end(pose.loadout), state.known.entries);
            Weapons::Sanitise(state.known.entries);
            for (std::uint32_t i = 0; i < Protocol::kWeaponSkillCount; ++i) {
                state.known.skills[i] = (std::min)(pose.skills[i], Cosave::kSkillMax);
                // San Andreas's "skill increased", as a help box: a level up since the save was loaded.
                const int level = Weapons::SkillLevel(i, state.known.skills[i]);
                if (state.haveLevels && level > state.levels[i]) {
                    SaHud::ShowHelp(std::format("{} skill: {}", Weapons::kSkillNames[i], Weapons::SkillLevelName(level)));
                    SKSE::log::info("arms: CJ's {} skill is {} now ({})", Weapons::kSkillNames[i], Weapons::SkillLevelName(level), state.known.skills[i]);
                }
                state.levels[i] = level;
            }
            state.haveLevels = true;
            state.known.slot = (std::min)(pose.weaponSlot, static_cast<std::uint8_t>(Protocol::kWeaponSlotCount - 1));
        }
    }

    bool DebugKeyEnabled() { return state.debugKey; }

    void Give(std::uint8_t a_type, std::uint32_t a_rounds)
    {
        if (!Weapons::Carried(a_type)) {
            return;
        }
        if (LinkService::Get().IsUp()) {
            state.gifts.emplace_back(a_type, a_rounds);
            return;
        }
        // No San Andreas to give it to: into the loadout the save holds and sends it once the link is up.
        Shop::GiveTo(state.known.entries, a_type, a_rounds);
        state.pending = true;
        state.settling = false;
        SKSE::log::info("arms: weapon {} with {} rounds kept for San Andreas, which isn't there:{}", a_type, a_rounds, Describe(state.known));
    }

    const Weapons::Loadout& Loadout() { return state.known.entries; }

    void DebugKey()
    {
        const auto& step = Weapons::kDebugSteps[state.debugStep % Weapons::kDebugStepCount];
        if (Send(Weapons::DebugCommand(state.debugStep))) {
            SKSE::log::info("arms: debug key {}: {}", state.debugStep % Weapons::kDebugStepCount + 1, step.what);
            SaHud::ShowHelp(std::format("Debug {}/{}: {}", state.debugStep % Weapons::kDebugStepCount + 1, Weapons::kDebugStepCount, step.what), 4.0f);
            state.debugStep++;
        } else {
            SKSE::log::warn("arms: debug key: the link is down, nothing given");
        }
    }

    void Draw(const RE::NiPoint3& a_anchor, std::uint32_t a_handover, const Protocol::PlayerState& a_pose, double a_nowMs, float a_delta, bool a_hide)
    {
        auto& link = LinkService::Get();
        if (!state.haveShots) {
            state.haveShots = true;
            state.lastShots = a_pose.shots;
        }
        const auto fired = static_cast<std::uint16_t>(a_pose.shots - state.lastShots);
        state.lastShots = a_pose.shots;
        if (fired > 0 && fired < 64 && Weapons::Flashes(a_pose.weaponType) && (a_pose.weaponFlags & Protocol::kWeaponInHand) != 0) {
            const bool twin = (a_pose.weaponFlags & Protocol::kWeaponTwin) != 0;
            MuzzleFlash::Fire(twin ? state.nextHand : 0);
            state.nextHand = twin ? (state.nextHand + 1) % Protocol::kWeaponHandMax : 0;
        }

        for (std::uint32_t hand = 0; hand < Protocol::kWeaponHandMax; ++hand) {
            Protocol::BodyState weapon{};
            if (link.ReadWeapon(hand, weapon)) {
                if ((weapon.flags & Protocol::kBodyValid) == 0 || weapon.handover != a_handover) {
                    state.hands[hand].Reset();
                } else if (weapon.boneCount <= Protocol::kBodyBoneMax && Body::Finite(weapon)) {
                    state.hands[hand].Push(weapon, a_nowMs);
                }
            }
            Protocol::BodyState pose{};
            if (a_hide || !state.hands[hand].Sample(a_nowMs, pose) || pose.boneCount == 0) {
                continue;
            }
            const RE::NiPoint3 feet = a_anchor + RE::NiPoint3(pose.feet[0], pose.feet[1], pose.feet[2]);
            if (ModelView::Show(pose, feet)) {
                MuzzleFlash::Show(hand, feet, a_pose.muzzle[hand], pose.bones[0].rotation, a_delta);
            }
        }
        MuzzleFlash::EndFrame();
    }

    void Hide()
    {
        for (auto& hand : state.hands) {
            hand.Reset();
        }
        MuzzleFlash::Reset();
        state.haveShots = false;
    }

    void Snapshot(Cosave::Arms& a_out)
    {
        a_out = state.known;
    }

    void Restore(const Cosave::Arms& a_arms)
    {
        state.known = a_arms;
        state.gifts.clear();  // the game they were for is gone
        state.pending = true;
        state.settling = false;
        state.haveLevels = false;
        SKSE::log::info("arms: the loaded save holds slot {}:{}", a_arms.slot, Describe(a_arms));
    }

    void Revert()
    {
        state.known = {};
        state.gifts.clear();
        state.pending = true;  // a new game or a save without weapons: San Andreas starts over too
        state.settling = false;
        state.haveLevels = false;
    }
}
