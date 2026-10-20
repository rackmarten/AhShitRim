#include "audio.h"

#include "log.h"
#include "radio.h"

#include <plugin.h>

#include <CAudioEngine.h>

#include <safetyhook.hpp>

#include <Windows.h>

#include <cmath>
#include <format>

namespace AhShitRim::Audio
{
    namespace
    {
        // ---- San Andreas 1.0 US addresses (gta-reversed names) ----

        constexpr std::uintptr_t kPauseAllSounds = 0x507430;      // CAudioEngine::PauseAllSounds (thiscall)
        constexpr std::uintptr_t kResumeAllSounds = 0x507440;     // CAudioEngine::ResumeAllSounds (thiscall)
        constexpr std::uintptr_t kRetuneInProgress = 0x506FF0;    // CAudioEngine::IsRadioRetuneInProgress (thiscall)

        // The world's ambience, hooked to do nothing.
        constexpr std::uintptr_t kNoOps[] = {
            0x5052F0,  // CAEWeatherAudioEntity::Service: city noise, wind and rain
            0x4E7630,  // CAEPoliceScannerAudioEntity::Service
            0x4DF210,  // CAEGlobalWeaponAudioEntity::ServiceAmbientGunFire: distant gunfire, foghorns
        };
        constexpr std::uintptr_t kAmbienceService = 0x4D76C0;  // CAEAmbienceTrackManager::Service(int) (thiscall): area ambience tracks

        constexpr int kVolumeMax = 64;  // San Andreas's master volumes run 0..64
        constexpr std::uint64_t kRetuneMs = 3000;  // a retune shows as one this long at most

        SafetyHookInline g_noOps[std::size(kNoOps)];
        SafetyHookInline g_ambience;

        struct State
        {
            int  effects = -1;  // the volumes last logged, 0..64
            int  music = -1;
            bool paused = false;

            bool          haveWheel = false;
            std::uint16_t radioNext = 0;
            std::uint16_t radioPrev = 0;
            std::int8_t   dial = Protocol::kRadioNone;  // the station the wheel is on, Radio Sovngarde included
            bool          dialed = false;               // a retune we asked for is still going on
            std::uint64_t dialedMs = 0;
            std::int8_t   actual = Protocol::kRadioNone - 1;  // San Andreas's own station id, last logged
            std::int8_t   reported = Protocol::kRadioNone - 1;
        } state;

        void __cdecl NoOp() {}
        void __fastcall NoOpAmbience(void*, void*, int) {}

        template <class T>
        SafetyHookInline Hook(std::uintptr_t a_address, T a_detour)
        {
            auto hook = safetyhook::create_inline(reinterpret_cast<void*>(a_address), reinterpret_cast<void*>(a_detour));
            if (!hook) {
                Log(std::format("audio: could not hook 0x{:X}", a_address));
            }
            return hook;
        }

        int ToVolume(float a_value)
        {
            return static_cast<int>(std::lround(std::clamp(a_value, 0.0f, 1.0f) * kVolumeMax));
        }

        bool Retuning()
        {
            return plugin::CallMethodAndReturn<bool, kRetuneInProgress, CAudioEngine*>(&AudioEngine);
        }
    }

    void Install()
    {
        for (std::size_t i = 0; i < std::size(kNoOps); ++i) {
            g_noOps[i] = Hook(kNoOps[i], &NoOp);
        }
        g_ambience = Hook(kAmbienceService, &NoOpAmbience);
    }

    void OnInput(const Protocol::InputState& a_input, bool a_inVehicle)
    {
        const bool owned = (a_input.flags & Protocol::kInputOwnsPlayer) != 0;
        const bool paused = (a_input.flags & Protocol::kInputPaused) != 0;
        // The first value read only seeds the counts, as for spawn requests.
        const int steps = state.haveWheel ? static_cast<std::int16_t>(a_input.radioNext - state.radioNext) -
                                                static_cast<std::int16_t>(a_input.radioPrev - state.radioPrev)
                                          : 0;
        // While Skyrim has the player nothing dials, and a Skyrim that restarted counts from 0 again.
        state.haveWheel = owned;
        state.radioNext = a_input.radioNext;
        state.radioPrev = a_input.radioPrev;
        if (steps == 0 || !owned || paused || !a_inVehicle) {
            return;
        }
        state.dial = Radio::Dial(state.dial, steps);
        state.dialed = true;
        state.dialedMs = ::GetTickCount64();
        // Radio Sovngarde is San Andreas's radio off; Skyrim plays on it.
        AudioEngine.RetuneRadio(state.dial == Radio::kSovngarde ? Radio::kRadioOff : state.dial);
        Log(std::format("audio: the wheel tunes {}", Radio::StationName(state.dial)));
    }

    void Update(bool a_owned, bool a_paused)
    {
        Protocol::AudioState audio{};
        if (!a_owned || !GetLink().ReadAudio(audio)) {
            audio = {};
        }
        const int effects = ToVolume(audio.effects);
        const int music = ToVolume(audio.music);
        // Every frame: the runner mutes San Andreas while it loads.
        AudioEngine.SetEffectsMasterVolume(static_cast<char>(effects));
        AudioEngine.SetMusicMasterVolume(static_cast<char>(music));
        if (effects != state.effects || music != state.music) {
            if ((effects == 0 && music == 0) != (state.effects <= 0 && state.music <= 0)) {
                Log(effects || music ? std::format("audio: sound on (effects {}/64, music {}/64)", effects, music) : "audio: muted");
            }
            state.effects = effects;
            state.music = music;
        }
        if (a_paused != state.paused) {
            if (a_paused) {
                plugin::CallMethod<kPauseAllSounds, CAudioEngine*>(&AudioEngine);
            } else {
                plugin::CallMethod<kResumeAllSounds, CAudioEngine*>(&AudioEngine);
            }
            state.paused = a_paused;
        }
    }

    void Fill(Protocol::PlayerState& a_pose, bool a_inVehicle)
    {
        std::int8_t station = Protocol::kRadioNone;
        bool        retuning = false;
        if (a_inVehicle) {
            const auto actual = static_cast<std::int8_t>(AudioEngine.GetCurrentRadioStationID());
            const bool off = actual < Radio::kEmergency || actual >= Radio::kSovngarde;
            if (actual != state.actual) {
                Log(std::format("audio: San Andreas's radio at {} (on {}, retuning {})", actual, AudioEngine.IsRadioOn(), Retuning()));
                state.actual = actual;
            }
            if (state.dialed) {
                // A retune the wheel asked for: shown as where it goes until San Andreas gets there.
                const bool there = state.dial == Radio::kSovngarde || state.dial == Radio::kRadioOff ? off : actual == state.dial;
                const bool late = ::GetTickCount64() - state.dialedMs > kRetuneMs;
                if (there || late) {
                    if (!there) {
                        Log(std::format("audio: San Andreas's radio didn't get to {} ({} instead)", Radio::StationName(state.dial), actual));
                    }
                    state.dialed = false;
                }
            }
            if (state.dialed) {
                station = state.dial;
                retuning = true;
            } else if (off) {
                if (state.dial != Radio::kSovngarde) {
                    state.dial = Radio::kRadioOff;  // the wheel goes on from off
                }
                station = state.dial;
            } else {
                station = actual;
                state.dial = actual;  // San Andreas chose it (a vehicle's own station on getting in)
            }
        }
        a_pose.radioStation = station;
        if (retuning) {
            a_pose.flags |= Protocol::kPlayerRadioRetuning;
        }
        if (station != state.reported) {
            Log(station == Protocol::kRadioNone ? std::string("audio: no radio") : std::format("audio: radio on {}", Radio::StationName(station)));
            state.reported = station;
        }
    }
}
