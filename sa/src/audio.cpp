#include "audio.h"

#include "log.h"
#include "missions.h"
#include "radio.h"

#include <plugin.h>

#include <CAECutsceneTrackManager.h>
#include <CAudioEngine.h>
#include <CPlayerPed.h>
#include <CVehicle.h>

#include <safetyhook.hpp>

#include <Windows.h>

#include <cmath>
#include <cstring>
#include <format>
#include <string>
#include <utility>

namespace AhShitRim::Audio
{
    namespace
    {
        // ---- San Andreas 1.0 US addresses (gta-reversed names) ----

        constexpr std::uintptr_t kPauseAllSounds = 0x507430;      // CAudioEngine::PauseAllSounds (thiscall)
        constexpr std::uintptr_t kResumeAllSounds = 0x507440;     // CAudioEngine::ResumeAllSounds (thiscall)
        constexpr std::uintptr_t kRetuneInProgress = 0x506FF0;    // CAudioEngine::IsRadioRetuneInProgress (thiscall)
        constexpr std::uintptr_t kPreloadBeatTrack = 0x507F40;    // CAudioEngine::PreloadBeatTrack(int16) (thiscall)
        constexpr std::uintptr_t kPlayBeatTrack = 0x507180;       // CAudioEngine::PlayPreloadedBeatTrack(bool missionComplete) (thiscall)
        constexpr std::uintptr_t kPreloadMissionAudio = 0x507290;  // CAudioEngine::PreloadMissionAudio(uint8 slot, int32 id) (thiscall)
        constexpr std::uintptr_t kMissionAudioStatus = 0x5072A0;  // CAudioEngine::GetMissionAudioLoadingStatus(uint8) (thiscall): 1 loaded
        constexpr std::uintptr_t kPlayMissionAudio = 0x5072B0;    // CAudioEngine::PlayLoadedMissionAudio(uint8) (thiscall)
        constexpr std::uintptr_t kMissionAudioDone = 0x5072C0;    // CAudioEngine::IsMissionAudioSampleFinished(uint8) (thiscall)
        constexpr std::uintptr_t kClearMissionAudio = 0x5072F0;   // CAudioEngine::ClearMissionAudio(uint8) (thiscall)
        constexpr std::uint8_t   kMissionAudioSlot = 0;           // of San Andreas's four; no script uses them here
        constexpr std::uint64_t  kLineStartMs = 500;              // the line counts as over only this long after it was set playing

        // The world's ambience, hooked to do nothing.
        constexpr std::uintptr_t kNoOps[] = {
            0x5052F0,  // CAEWeatherAudioEntity::Service: city noise, wind and rain
            0x4E7630,  // CAEPoliceScannerAudioEntity::Service
            0x4DF210,  // CAEGlobalWeaponAudioEntity::ServiceAmbientGunFire: distant gunfire, foghorns
        };
        constexpr std::uintptr_t kAmbienceService = 0x4D76C0;  // CAEAmbienceTrackManager::Service(int) (thiscall): area ambience tracks
        constexpr std::uintptr_t kBeatTrackStatus = 0x507170;   // CAudioEngine::GetBeatTrackStatus (thiscall): StartRadio and RetuneRadio do nothing unless 0
        constexpr std::uintptr_t kAmbienceActive = 0x507210;    // CAudioEngine::IsAmbienceTrackActive (thiscall)
        constexpr std::uintptr_t kAmbienceOverrides = 0x507270; // CAudioEngine::DoesAmbienceTrackOverrideRadio (thiscall): then the radio won't start
        constexpr std::uintptr_t kCutsceneActive = 0x507150;    // CAudioEngine::IsCutsceneTrackActive (thiscall)

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
            std::int8_t   dial = Protocol::kRadioNone;  // the station the wheel is on, Skyrim's stations included
            bool          dialed = false;               // a retune we asked for is still going on
            std::uint64_t dialedMs = 0;
            std::int8_t   actual = Protocol::kRadioNone - 1;  // San Andreas's own station id, last logged
            bool          on = false;                         // and whether its radio ran
            bool          inRadio = false;                    // CJ was in a vehicle with a radio at the last Fill
            Radio::Boarding boarding;                          // getting in, on the station carried over
            std::uint64_t inRadioMs = 0;                       // when he got in, until the log 3 s later
            std::int8_t   reported = Protocol::kRadioNone - 1;

            // v24: the sound cue playing (Protocol::kCue*), 0 for none.
            bool          haveCue = false;
            std::uint16_t cueCount = 0;
            std::uint8_t  cue = 0;
            std::uint64_t cueMs = 0;            // when it started
            bool          lineLoaded = false;  // the intro line was loaded and set playing
            std::uint64_t linePlayMs = 0;      // when
            unsigned long trackStatus = 8;     // the tune's CAECutsceneTrackManager status last seen (3 playing, 8 idle)
        } state;

        void __cdecl NoOp() {}
        void __fastcall NoOpAmbience(void*, void*, int) {}

        // What can keep San Andreas's radio from starting or retuning, for the log.
        std::string RadioBlockers()
        {
            const int  beat = plugin::CallMethodAndReturn<std::int8_t, kBeatTrackStatus, CAudioEngine*>(&AudioEngine);
            const bool ambience = plugin::CallMethodAndReturn<bool, kAmbienceActive, CAudioEngine*>(&AudioEngine);
            const bool overrides = plugin::CallMethodAndReturn<bool, kAmbienceOverrides, CAudioEngine*>(&AudioEngine);
            const bool cutscene = plugin::CallMethodAndReturn<bool, kCutsceneActive, CAudioEngine*>(&AudioEngine);
            return std::format("radio on {}, station {}, beat track status {}, ambience track {}{}, cutscene track {}", AudioEngine.IsRadioOn(),
                static_cast<int>(AudioEngine.GetCurrentRadioStationID()), beat, ambience, overrides ? " (overrides the radio)" : "", cutscene);
        }

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

        void EndCue()
        {
            if (state.cue == Protocol::kCueIntroLine) {
                plugin::CallMethod<kClearMissionAudio, CAudioEngine*, std::uint8_t>(&AudioEngine, kMissionAudioSlot);
            }
            state.cue = 0;
        }

        // A cue Skyrim asked for, by San Andreas's own routines and ids, as its scripts play them: the mission
        // passed tune as PLAY_MISSION_PASSED_TUNE 1 does (the radio stops for it and comes back after), CJ's
        // intro line as LOAD_MISSION_AUDIO and PLAY_MISSION_AUDIO do (played once loaded, in ServiceCue).
        void StartCue(std::uint8_t a_cue)
        {
            if (state.cue) {
                EndCue();
            }
            if (a_cue == Protocol::kCueMissionPassed) {
                plugin::CallMethod<kPreloadBeatTrack, CAudioEngine*, std::int16_t>(&AudioEngine, Missions::kMissionPassedBeat);
                plugin::CallMethod<kPlayBeatTrack, CAudioEngine*, bool>(&AudioEngine, true);
                Log("audio: the mission passed tune");
            } else if (a_cue == Protocol::kCueIntroLine) {
                plugin::CallMethod<kPreloadMissionAudio, CAudioEngine*, std::uint8_t, std::int32_t>(&AudioEngine, kMissionAudioSlot, Missions::kIntroLineAudio);
                state.lineLoaded = false;
                Log("audio: CJ's intro line, loading");
            } else {
                return;
            }
            state.cue = a_cue;
            state.cueMs = ::GetTickCount64();
        }

        // The cue playing: the intro line plays once loaded and is cleared once over. Every cue ends after its
        // time at most (Missions::CueSeconds).
        void ServiceCue()
        {
            if (!state.cue) {
                return;
            }
            const auto elapsed = ::GetTickCount64() - state.cueMs;
            if (state.cue == Protocol::kCueIntroLine) {
                if (!state.lineLoaded &&
                    plugin::CallMethodAndReturn<std::int8_t, kMissionAudioStatus, CAudioEngine*, std::uint8_t>(&AudioEngine, kMissionAudioSlot) == 1) {
                    plugin::CallMethod<kPlayMissionAudio, CAudioEngine*, std::uint8_t>(&AudioEngine, kMissionAudioSlot);
                    state.lineLoaded = true;
                    state.linePlayMs = ::GetTickCount64();
                    Log(std::format("audio: CJ's intro line plays ({} ms to load)", elapsed));
                } else if (state.lineLoaded && ::GetTickCount64() - state.linePlayMs > kLineStartMs && plugin::CallMethodAndReturn<bool, kMissionAudioDone, CAudioEngine*, std::uint8_t>(&AudioEngine, kMissionAudioSlot)) {
                    Log(std::format("audio: CJ's intro line over after {} ms", ::GetTickCount64() - state.linePlayMs));
                    EndCue();
                    return;
                }
            } else if (const auto status = AECutsceneTrackManager.m_nStatus; status != state.trackStatus) {
                // The tune: loading (0..2), playing (3), stopping (4..7), done (8).
                if (status == 3 || status == 8) {
                    Log(std::format("audio: the mission passed tune {} after {} ms", status == 3 ? "plays" : "is over", elapsed));
                }
                state.trackStatus = status;
            }
            if (elapsed > static_cast<std::uint64_t>(Missions::CueSeconds(state.cue) * 1000.0f)) {
                if (state.cue == Protocol::kCueIntroLine && !state.lineLoaded) {
                    Log("audio: CJ's intro line never loaded");
                }
                EndCue();
            }
        }
    }

    void Install()
    {
        for (std::size_t i = 0; i < std::size(kNoOps); ++i) {
            g_noOps[i] = Hook(kNoOps[i], &NoOp);
        }
        g_ambience = Hook(kAmbienceService, &NoOpAmbience);
    }

    bool HasRadio(const CVehicle* a_vehicle)
    {
        return a_vehicle && Radio::HasRadio(VehicleKinds::KindOf(static_cast<std::uint16_t>(a_vehicle->m_nModelIndex)),
                                 static_cast<int>(a_vehicle->m_vehicleAudio.m_settings.m_nRadioType));
    }

    void OnInput(const Protocol::InputState& a_input, bool a_radio)
    {
        const bool owned = (a_input.flags & Protocol::kInputOwnsPlayer) != 0;
        const bool paused = (a_input.flags & Protocol::kInputPaused) != 0;
        // The first value read only seeds the counts, as for spawn requests.
        const int steps = state.haveWheel ? static_cast<std::int16_t>(a_input.radioNext - state.radioNext) -
                                                static_cast<std::int16_t>(a_input.radioPrev - state.radioPrev)
                                          : 0;
        // A sound cue (v24), whoever has the player. The first value read only seeds the count.
        if (state.haveCue && a_input.cueCount != state.cueCount) {
            StartCue(a_input.cue);
        }
        state.haveCue = true;
        state.cueCount = a_input.cueCount;
        // While Skyrim has the player nothing dials, and a Skyrim that restarted counts from 0 again.
        state.haveWheel = owned;
        state.radioNext = a_input.radioNext;
        state.radioPrev = a_input.radioPrev;
        if (steps == 0 || !owned || paused || !a_radio) {
            return;
        }
        state.dial = Radio::Dial(state.dial, steps);
        state.dialed = true;
        state.dialedMs = ::GetTickCount64();
        // Radio Sovngarde and Sweetroll FM are San Andreas's radio off; Skyrim plays on them.
        AudioEngine.RetuneRadio(Radio::SaStation(state.dial));
        Log(std::format("audio: the wheel tunes {}", Radio::StationName(state.dial)));
    }

    void Play(HudSounds::Sound a_sound)
    {
        AudioEngine.ReportFrontendAudioEvent(static_cast<int>(a_sound), 0.0f, 1.0f);
        Log(std::format("audio: HUD sound {} ({})", static_cast<int>(a_sound), HudSounds::NameOf(a_sound)));
    }

    void PlayFrontend(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        Protocol::FrontendSoundCommand command{};
        if (a_size < sizeof(command)) {
            return;
        }
        std::memcpy(&command, a_data, sizeof(command));
        if (!HudSounds::Plays(command.event)) {
            Log(std::format("audio: no HUD sound {}", command.event));
            return;
        }
        Play(static_cast<HudSounds::Sound>(command.event));
    }

    void Update(bool a_owned, bool a_paused)
    {
        ServiceCue();
        // While Skyrim has the player San Andreas is heard only for a cue, which Skyrim keeps the volumes up for.
        Protocol::AudioState audio{};
        if (!(a_owned || state.cue) || !GetLink().ReadAudio(audio)) {
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

    void Fill(Protocol::PlayerState& a_pose, CVehicle* a_vehicle)
    {
        const bool  radio = HasRadio(a_vehicle);
        std::int8_t station = Protocol::kRadioNone;
        bool        retuning = false;
        if (radio && !state.inRadio) {
            // Getting in (San Andreas starts the vehicle's radio a frame later): on the station carried over, set as the
            // vehicle's own, the way one it was left tuned to comes on. No inline hook on CAERadioTrackManager::StartRadio:
            // with one, San Andreas's radio never ran at all, its own station included.
            if (const auto start = Radio::StartStation(state.dial, Protocol::kRadioNone); start != Protocol::kRadioNone) {
                auto& settings = a_vehicle->m_vehicleAudio.m_settings;
                settings.m_nRadioID = static_cast<eRadioID>(Radio::SaStation(start));
                if (settings.m_nRadioType == RADIO_EMERGENCY) {
                    settings.m_nRadioType = RADIO_CIVILIAN;  // its radio would come on on Emergency AA, which no retune leaves
                }
                Log(std::format("audio: model {} gets in on {}, the last station", a_vehicle->m_nModelIndex, Radio::StationName(start)));
            }
            state.boarding.Start(state.dial, ::GetTickCount64());
        }
        if (radio) {
            const auto actual = static_cast<std::int8_t>(AudioEngine.GetCurrentRadioStationID());
            const bool on = AudioEngine.IsRadioOn();
            const bool off = actual < Radio::kEmergency || actual >= Radio::kSaUserTracks;
            if (actual != state.actual || on != state.on) {
                Log(std::format("audio: San Andreas's radio at {} (on {}, retuning {})", actual, on, Retuning()));
                state.actual = actual;
                state.on = on;
            }
            // Came on elsewhere (an emergency vehicle's Emergency AA, San Andreas's own retune): retuned once (Radio::Boarding).
            if (const auto retune = state.boarding.Step(on, actual, ::GetTickCount64()); retune >= 0) {
                AudioEngine.RetuneRadio(retune);
                state.dialed = true;
                state.dialedMs = ::GetTickCount64();
                Log(std::format("audio: San Andreas's radio came on at {}, retuned to {}", actual, Radio::StationName(state.dial)));
            }
            if (state.dialed) {
                // A retune the wheel asked for: shown as where it goes until San Andreas gets there.
                const bool there = Radio::SaStation(state.dial) == Radio::kSaRadioOff ? off : actual == state.dial;
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
            } else {
                // Not running yet, its station id is stale: the dial goes on (Radio::Follow), as while getting in.
                const auto followed = Radio::Follow(state.dial, actual, on && !state.boarding.Pending());
                station = followed.station;
                state.dial = followed.dial;
            }
        }
        if (!radio) {
            state.dialed = false;  // out before the retune got there: the dial has it for the next vehicle
        }
        // Getting in, and 3 s after: what San Andreas's radio is doing.
        if (radio && !state.inRadio) {
            state.inRadioMs = ::GetTickCount64();
            Log("audio: in a vehicle with a radio: " + RadioBlockers());
        } else if (radio && state.inRadioMs && ::GetTickCount64() - state.inRadioMs > 3000) {
            state.inRadioMs = 0;
            Log("audio: 3 s in: " + RadioBlockers());
        }
        state.inRadio = radio;
        a_pose.radioStation = station;
        if (retuning) {
            a_pose.flags |= Protocol::kPlayerRadioRetuning;
        }
        if (station != state.reported) {
            Log(station == Protocol::kRadioNone ? std::string("audio: no radio") : std::format("audio: radio on {}", Radio::StationName(station)));
            state.reported = station;
        }
    }

    bool RadioPlays() { return Radio::Plays(state.reported); }
}
