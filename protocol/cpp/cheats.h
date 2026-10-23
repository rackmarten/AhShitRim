// San Andreas's cheat codes typed in Skyrim (v20), the game-free half: the cheats AhShitRim has, how a typed
// letter completes one, and which of them are on. Header-only and free of game types, so the protocol tests
// cover it; the SKSE plugin types every letter key into a Typist while CJ has the player.
//
// The typing is San Andreas's own (CCheat::AddToCheatString, gta-reversed): the letters go into a string,
// newest first, and after each one the key (CKeyGen::GetKey, a CRC-32 without its final flip) of its first
// 6, 7, ... 29 letters is looked up among the cheats' keys, so San Andreas's alternative codes that share a
// key (FULLCLIP for WANRLTW, THUGSARMOURY for LXGIWYL) work as they do there. A match empties the string.
// It runs on Skyrim's side, which has the keys; San Andreas only hears what a finished code does. M, Skyrim's
// map key, is a letter like any other while CJ has the player (Input::OpensSkyrimMenu).
//
// What a cheat does is its Kind: San Andreas's own toggles and one-shot cheats go over the ring as a Cheat
// command (its key and San Andreas's eCheats index), the weapon sets as the weapon commands' GiveSet, the
// vehicles, the jetpack and the parachute as spawn requests, and the rest is Skyrim's (HESOYAM's health,
// armour and gold, BAGUVIX's half, NOMERCY). Adding one (AEZAKMI with the wanted level) is a row in kCheats.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../ahshitrim_protocol.h"
#include "input.h"
#include "vehicle_kinds.h"

namespace AhShitRim::Cheats
{
    // San Andreas's key of a string's first a_count letters (CKeyGen::GetKey): CRC-32's register, not flipped
    // at the end.
    constexpr std::uint32_t KeyOf(const char* a_letters, std::size_t a_count)
    {
        std::uint32_t key = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < a_count; ++i) {
            key ^= static_cast<std::uint8_t>(a_letters[i]);
            for (int bit = 0; bit < 8; ++bit) {
                key = (key >> 1) ^ (0xEDB88320u & (0u - (key & 1u)));
            }
        }
        return key;
    }

    // The key San Andreas keeps for a code as it's typed: the code backwards, newest letter first.
    constexpr std::uint32_t CodeKey(std::string_view a_code)
    {
        std::uint32_t key = 0xFFFFFFFFu;
        for (std::size_t i = a_code.size(); i-- > 0;) {
            key ^= static_cast<std::uint8_t>(a_code[i]);
            for (int bit = 0; bit < 8; ++bit) {
                key = (key >> 1) ^ (0xEDB88320u & (0u - (key & 1u)));
            }
        }
        return key;
    }

    // San Andreas's eCheats (plugin-sdk's CCheat.h), the index into CCheat's tables; the ones used here.
    enum class SaCheat : std::uint8_t
    {
        WeaponSet1 = 0,
        WeaponSet2 = 1,
        WeaponSet3 = 2,
        HealthArmourMoney = 3,
        SpawnRhino = 17,
        SpawnBloodringBanger = 18,
        SpawnRancher = 19,
        SpawnHotringA = 20,
        SpawnHotringB = 21,
        SpawnRomero = 22,
        SpawnStretch = 23,
        SpawnTrashmaster = 24,
        SpawnCaddy = 25,
        BlowUpCars = 26,
        PerfectHandling = 28,
        Suicide = 29,
        Fat = 36,
        Muscle = 37,
        Skinny = 38,
        CarsFly = 48,
        HugeBunnyHop = 49,
        SpawnHydra = 50,
        SpawnVortex = 51,
        SmashNBoom = 52,
        AllCarsNitro = 53,
        CarsFloatAway = 54,
        MegaJump = 60,
        InfiniteHealth = 61,
        Parachute = 63,
        Jetpack = 64,
        InfiniteAmmo = 72,
        MaxStamina = 81,
        HitmanAllWeapons = 82,
        MaxDrivingSkills = 83,
        SpawnHunter = 84,
        SpawnQuad = 85,
        SpawnTanker = 86,
        SpawnDozer = 87,
        SpawnStuntPlane = 88,
        SpawnMonster = 89,
        None = 0xFF,  // ours alone (NOMERCY)
    };
    inline constexpr std::uint8_t kSaCheatCount = 92;  // TOTAL_CHEATS

    enum class Kind : std::uint8_t
    {
        Health,     // HESOYAM: Skyrim's health and CJ's armour full, 250,000 gold; San Andreas mends CJ's vehicle (its own cheat)
        Invincible, // BAGUVIX: the player takes no damage (Skyrim's half), San Andreas's infinite health on (its toggle)
        Weapons,    // one of San Andreas's weapon sets (WeaponCommand GiveSet, argument the set)
        Spawn,      // a vehicle, the jetpack or the parachute in front of CJ (a spawn request, argument the model)
        SaToggle,   // San Andreas's own toggle (flying cars, infinite ammo): on, and off typed again
        SaOnce,     // San Andreas's own one-shot cheat (fat, muscle, blow up the vehicles)
        Suicide,    // the player's health gone: Wasted
        NoMercy,    // ours: nobody is essential (protocol/cpp/mercy.h), kept in the save
    };

    // Whether typing it again turns it off (San Andreas's CHEAT8, "Cheat deactivated").
    constexpr bool Toggles(Kind a_kind) { return a_kind == Kind::Invincible || a_kind == Kind::SaToggle || a_kind == Kind::NoMercy; }

    // Whether San Andreas gets a Cheat command for it.
    constexpr bool ToSanAndreas(Kind a_kind)
    {
        return a_kind == Kind::Health || a_kind == Kind::Invincible || a_kind == Kind::SaToggle || a_kind == Kind::SaOnce;
    }

    struct Cheat
    {
        std::string_view code;      // as typed, San Andreas's PC code
        Kind             kind;
        SaCheat          sa;        // San Andreas's index (SaCheat::None for ours)
        std::uint16_t    argument;  // the weapon set, or the model a spawn asks for
        std::string_view what;      // for the log and the README
    };

    inline constexpr Cheat kCheats[] = {
        { "HESOYAM", Kind::Health, SaCheat::HealthArmourMoney, 0, "health, armour, $250k" },
        { "BAGUVIX", Kind::Invincible, SaCheat::InfiniteHealth, 0, "infinite health" },
        { "LXGIWYL", Kind::Weapons, SaCheat::WeaponSet1, 1, "weapon set 1 (thug's tools)" },
        { "KJKSZPJ", Kind::Weapons, SaCheat::WeaponSet2, 2, "weapon set 2 (professional tools)" },
        { "UZUMYMW", Kind::Weapons, SaCheat::WeaponSet3, 3, "weapon set 3 (nutter tools)" },
        { "FULLCLIP", Kind::SaToggle, SaCheat::InfiniteAmmo, 0, "infinite ammo, no reloading" },
        { "NCSGDAG", Kind::SaOnce, SaCheat::HitmanAllWeapons, 0, "hitman in every weapon skill" },
        { "AIWPRTON", Kind::Spawn, SaCheat::SpawnRhino, 432, "Rhino" },
        { "CQZIJMB", Kind::Spawn, SaCheat::SpawnBloodringBanger, 504, "Bloodring Banger" },
        { "JQNTDMH", Kind::Spawn, SaCheat::SpawnRancher, 505, "Rancher" },
        { "PDNEJOH", Kind::Spawn, SaCheat::SpawnHotringA, 502, "Hotring Racer" },
        { "VPJTQWV", Kind::Spawn, SaCheat::SpawnHotringB, 503, "Hotring Racer 2" },
        { "AQTBCODX", Kind::Spawn, SaCheat::SpawnRomero, 442, "Romero" },
        { "KRIJEBR", Kind::Spawn, SaCheat::SpawnStretch, 409, "Stretch" },
        { "UBHYZHQ", Kind::Spawn, SaCheat::SpawnTrashmaster, 408, "Trashmaster" },
        { "RZHSUEW", Kind::Spawn, SaCheat::SpawnCaddy, 457, "Caddy" },
        { "JUMPJET", Kind::Spawn, SaCheat::SpawnHydra, 520, "Hydra" },
        { "KGGGDKP", Kind::Spawn, SaCheat::SpawnVortex, 539, "Vortex" },
        { "OHDUDE", Kind::Spawn, SaCheat::SpawnHunter, 425, "Hunter" },
        { "AKJJYGLC", Kind::Spawn, SaCheat::SpawnQuad, 471, "Quad" },
        { "AMOMHRER", Kind::Spawn, SaCheat::SpawnTanker, 514, "Tanker (without its trailer)" },
        { "EEGCYXT", Kind::Spawn, SaCheat::SpawnDozer, 486, "Dozer" },
        { "URKQSRK", Kind::Spawn, SaCheat::SpawnStuntPlane, 513, "Stuntplane" },
        { "AGBDLCID", Kind::Spawn, SaCheat::SpawnMonster, 556, "Monster" },
        { "YECGAA", Kind::Spawn, SaCheat::Jetpack, VehicleKinds::kJetpack, "jetpack" },
        { "AIYPWZQP", Kind::Spawn, SaCheat::Parachute, VehicleKinds::kParachute, "parachute" },
        { "RIPAZHA", Kind::SaToggle, SaCheat::CarsFly, 0, "flying cars" },
        { "JHJOECW", Kind::SaToggle, SaCheat::HugeBunnyHop, 0, "huge bunny hop" },
        { "PGGOMOY", Kind::SaToggle, SaCheat::PerfectHandling, 0, "perfect handling" },
        { "COXEFGU", Kind::SaToggle, SaCheat::AllCarsNitro, 0, "nitro in every car" },
        { "JCNRUAD", Kind::SaToggle, SaCheat::SmashNBoom, 0, "smash'n'boom (cars blow up on contact)" },
        { "LFGMHAL", Kind::SaToggle, SaCheat::MegaJump, 0, "mega jump" },
        { "CPKTNWT", Kind::SaOnce, SaCheat::BlowUpCars, 0, "blow up every vehicle" },
        { "BTCDBCB", Kind::SaOnce, SaCheat::Fat, 0, "fat CJ" },
        { "BUFFMEUP", Kind::SaOnce, SaCheat::Muscle, 0, "muscular CJ" },
        { "KVGYZQK", Kind::SaOnce, SaCheat::Skinny, 0, "skinny CJ" },
        { "VKYPQCF", Kind::SaOnce, SaCheat::MaxStamina, 0, "maximum stamina" },
        { "VQIMAHA", Kind::SaOnce, SaCheat::MaxDrivingSkills, 0, "maximum driving, flying and bike skills" },
        { "SZCMAWO", Kind::Suicide, SaCheat::Suicide, 0, "suicide" },
        { "NOMERCY", Kind::NoMercy, SaCheat::None, 0, "nobody is essential (can break quests)" },
    };
    inline constexpr std::size_t kCheatCount = std::size(kCheats);
    static_assert(kCheatCount <= 64, "Toggles keeps a bit per cheat");

    // The help box's lines, San Andreas's CHEAT1 and CHEAT8.
    inline constexpr std::string_view kActivated = "Cheat activated";
    inline constexpr std::string_view kDeactivated = "Cheat deactivated";

    // The letter a key types into the cheat string, by its place on a US keyboard (DirectInput scan codes
    // are places, not letters, as are San Andreas's), or 0 for a key that isn't a letter.
    constexpr char LetterOf(std::uint32_t a_dik)
    {
        constexpr std::string_view top = "QWERTYUIOP";  // 0x10..0x19
        constexpr std::string_view home = "ASDFGHJKL";  // 0x1E..0x26
        constexpr std::string_view bottom = "ZXCVBNM";  // 0x2C..0x32
        if (a_dik >= 0x10 && a_dik < 0x10 + top.size()) {
            return top[a_dik - 0x10];
        }
        if (a_dik >= 0x1E && a_dik < 0x1E + home.size()) {
            return home[a_dik - 0x1E];
        }
        if (a_dik >= 0x2C && a_dik < 0x2C + bottom.size()) {
            return bottom[a_dik - 0x2C];
        }
        return 0;
    }

    // The cheats' row of San Andreas's index a_sa, or nullptr.
    constexpr const Cheat* Find(SaCheat a_sa)
    {
        for (const auto& cheat : kCheats) {
            if (cheat.sa == a_sa && a_sa != SaCheat::None) {
                return &cheat;
            }
        }
        return nullptr;
    }

    constexpr std::size_t IndexOf(const Cheat& a_cheat) { return static_cast<std::size_t>(&a_cheat - kCheats); }

    // The cheat string, as San Andreas keeps it (CCheat::m_CheatString, 30 characters with its end).
    class Typist
    {
    public:
        static constexpr std::size_t kLength = 29;
        static constexpr std::size_t kShortest = 6;  // CHEAT_MIN_HASH_SIZE

        // A key went down (a scan code): letters go into the string, anything else is ignored. Returns the cheat
        // it finished, or nullptr.
        const Cheat* Key(std::uint32_t a_dik)
        {
            const char letter = LetterOf(a_dik);
            return letter ? Type(letter) : nullptr;
        }

        // San Andreas's AddToCheatString: a_letter goes in front, and the shortest run of the newest letters
        // whose key is a cheat's completes it, which empties the string.
        const Cheat* Type(char a_letter)
        {
            for (std::size_t i = kLength - 1; i > 0; --i) {
                _letters[i] = _letters[i - 1];
            }
            _letters[0] = a_letter;
            _count = _count < kLength ? _count + 1 : kLength;
            for (std::size_t n = kShortest; n <= _count; ++n) {
                const std::uint32_t key = KeyOf(_letters, n);
                for (const auto& cheat : kCheats) {
                    if (CodeKey(cheat.code) == key) {
                        Clear();
                        return &cheat;
                    }
                }
            }
            return nullptr;
        }

        void Clear() { _count = 0; }

        std::size_t Count() const { return _count; }

    private:
        char        _letters[kLength] = {};
        std::size_t _count = 0;
    };

    // Which toggles are on, by row in kCheats.
    class Switches
    {
    public:
        bool On(const Cheat& a_cheat) const { return (_bits >> IndexOf(a_cheat)) & 1u; }

        // Typed again: on becomes off and off on. Returns whether it is on now.
        bool Flip(const Cheat& a_cheat)
        {
            _bits ^= 1ull << IndexOf(a_cheat);
            return On(a_cheat);
        }

        void Set(const Cheat& a_cheat, bool a_on)
        {
            const auto bit = 1ull << IndexOf(a_cheat);
            _bits = a_on ? (_bits | bit) : (_bits & ~bit);
        }

        bool Any() const { return _bits != 0; }
        void Clear() { _bits = 0; }

        // Calls a_do(cheat) for each that is on, in kCheats order.
        template <class F>
        void ForEachOn(F&& a_do) const
        {
            for (const auto& cheat : kCheats) {
                if (On(cheat)) {
                    a_do(cheat);
                }
            }
        }

    private:
        std::uint64_t _bits = 0;
    };

    // The Cheat command for a cheat San Andreas has a part in, a toggle's state in a_on.
    constexpr Protocol::CheatCommand CommandFor(const Cheat& a_cheat, bool a_on = true)
    {
        Protocol::CheatCommand command{};
        command.key = CodeKey(a_cheat.code);
        command.cheat = static_cast<std::uint8_t>(a_cheat.sa);
        command.flags = a_on ? Protocol::kCheatOn : 0;
        return command;
    }
}
