#include "audio/SaAudio.h"

#include "hud/SaHud.h"
#include "link/LinkService.h"
#include "radio.h"

#include <filesystem>

#undef GetObject  // windows.h

namespace AhShitRim::Audio
{
    namespace
    {
        // Skyrim.esm's effects category (AudioCategorySFX), the first slider under master in its
        // audio menu. Master and music come from the default objects.
        constexpr RE::FormID kEffectsCategory = 0x000172A1;

        constexpr float kEndedSeconds = 2.0f;  // a station's track counts as over once it stopped playing this long

        constexpr std::uint16_t kFadeMs = 500;

        constexpr float kCombatSeconds = 2.0f;  // a fight (or its end) counts once it lasted this long
        constexpr float kRetrySeconds = 10.0f;  // after a track that wouldn't open

        struct State
        {
            RE::BGSSoundCategory* effects = nullptr;
            RE::BGSSoundCategory* music = nullptr;

            std::int8_t   station = Protocol::kRadioNone;  // what CJ's radio last played, as shown
            bool          retuning = false;
            bool          silenced = false;  // Skyrim's music attenuated by us
            RE::BSIMusicType* pausedMusic = nullptr;  // Skyrim's music type we paused
            RE::Setting*      masterVolume = nullptr;  // fAudioMasterVolume: the master slider has no category

            Radio::Sovngarde  sovngarde{ static_cast<std::uint32_t>(::GetTickCount64()) };
            Radio::Shuffle    sweetroll{ {}, static_cast<std::uint32_t>(::GetTickCount64() >> 3) };  // its installed tracks
            bool              sweetrollWarned = false;  // tuned in with none installed, logged
            bool              tunedIn = false;          // a station Skyrim plays was just tuned in: its first track next
            RE::BSSoundHandle track;
            bool              trackPaused = false;
            bool              trackStarted = false;  // seen playing once: only then can it have ended
            float             trackSeconds = 0.0f;   // how long the track has played
            float             trackLength = 0.0f;    // seconds, 0 = unknown
            float             trackSilent = 0.0f;    // how long it hasn't been playing since it started
            bool              combat = false;        // the mood Radio Sovngarde follows
            float             combatFlip = 0.0f;     // how long the player's combat state has differed from it
        } state;

        float Volume(const RE::BGSSoundCategory* a_category)
        {
            return a_category ? a_category->GetCategoryVolume() : 1.0f;
        }

        void SendMusic(RE::BSIMusicType* a_type, RE::BSMusicEvent::MUSIC_MESSAGE_TYPE a_message)
        {
            auto*            manager = RE::BSMusicManager::GetSingleton();
            RE::BSMusicEvent event{};
            event.musicType = a_type;
            event.msgType = a_message;
            if (manager && a_type) {
                manager->ProcessEvent(&event, nullptr);
            }
        }

        // Skyrim's own music pauses while a station plays: every frame, whichever music type Skyrim
        // plays now (it moves on to combat music, a dungeon's), and that one goes on afterwards.
        // Attenuating its music category didn't reach the tracks.
        void SilenceSkyrimMusic(bool a_silence)
        {
            auto* manager = RE::BSMusicManager::GetSingleton();
            auto* current = manager ? manager->current : nullptr;
            if (a_silence) {
                if (current && current->typeStatus.get() == RE::BSIMusicType::MUSIC_STATUS::kPlaying) {
                    SendMusic(current, RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kPause);
                    state.pausedMusic = current;
                }
            } else if (state.silenced) {
                if (state.pausedMusic && state.pausedMusic == current) {
                    SendMusic(current, RE::BSMusicEvent::MUSIC_MESSAGE_TYPE::kUnpause);
                }
                state.pausedMusic = nullptr;
            }
            if (a_silence != state.silenced) {
                SKSE::log::info("audio: Skyrim's music {}", a_silence ? "paused for San Andreas" : "back");
                state.silenced = a_silence;
            }
        }

        void StopTrack()
        {
            if (state.track.IsValid()) {
                state.track.FadeOutAndRelease(kFadeMs);
            }
            state.track = {};
            state.trackPaused = false;
        }

        // The next track of the station tuned in (Radio Sovngarde or Sweetroll FM).
        void PlayNext(float a_volume)
        {
            StopTrack();
            state.tunedIn = false;
            const auto name = Radio::StationName(state.station);
            std::string path;
            if (state.station == Radio::kSweetroll) {
                path = state.sweetroll.Next();
            } else {
                auto* calendar = RE::Calendar::GetSingleton();
                path = state.sovngarde.Next(state.combat, calendar ? calendar->GetHour() : 12.0f).path;
            }
            RE::BSResource::ID id;
            id.GenerateFromPath(path.c_str());
            RE::BSAudioManager::GetSingleton()->GetSoundHandleByFile(state.track, id, 0x1A, 128);
            state.trackSeconds = 0.0f;
            state.trackStarted = false;
            state.trackSilent = 0.0f;
            if (!state.track.IsValid()) {
                SKSE::log::warn("audio: {} could not open {}, trying another in {:.0f} s", name, path, kRetrySeconds);
                state.trackLength = kRetrySeconds;
                return;
            }
            state.track.SetVolume(a_volume);
            state.track.Play();
            state.trackLength = static_cast<float>(state.track.GetDuration()) / 1000.0f;
            SKSE::log::info("audio: {} plays {} ({:.0f} s)", name, path, state.trackLength);
        }

        // A station Skyrim plays while it is tuned in: the next track when one ends, or on Radio
        // Sovngarde when the mood changes.
        void PlayStation(float a_delta, float a_volume)
        {
            if (state.trackPaused) {
                state.track.Play();
                state.trackPaused = false;
            }
            auto*      player = RE::PlayerCharacter::GetSingleton();
            const bool combat = player && player->IsInCombat();
            state.combatFlip = combat != state.combat ? state.combatFlip + a_delta : 0.0f;
            if (state.combatFlip >= kCombatSeconds) {
                state.combat = combat;
                state.combatFlip = 0.0f;
            }
            state.trackSeconds += a_delta;
            bool ended = !state.track.IsValid() && state.trackSeconds > state.trackLength;  // the retry after a failed open
            if (state.track.IsValid()) {
                // IsPlaying drops out now and then mid-track: over only once it stays out.
                const bool playing = state.track.IsPlaying();
                state.trackStarted |= playing;
                state.trackSilent = state.trackStarted && !playing ? state.trackSilent + a_delta : 0.0f;
                ended = state.trackSilent >= kEndedSeconds || (state.trackLength > 0.0f && state.trackSeconds > state.trackLength + 1.0f);
            }
            const bool moodChanged = state.station == Radio::kSovngarde && state.sovngarde.MoodChanged(state.combat);
            if (state.tunedIn || ended || moodChanged) {
                PlayNext(a_volume);
            } else if (state.track.IsValid()) {
                state.track.SetVolume(a_volume);
            }
        }
    }

    void SetPaused(bool a_paused)
    {
        if (a_paused && state.track.IsValid() && !state.trackPaused) {
            state.track.Pause();
            state.trackPaused = true;
        }
    }

    void OnDataLoaded()
    {
        auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
        if (defaults) {
            state.music = defaults->GetObject<RE::BGSSoundCategory>(RE::DEFAULT_OBJECT::kMusicSoundCategory);
        }
        state.effects = RE::TESForm::LookupByID<RE::BGSSoundCategory>(kEffectsCategory);
        if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
            state.masterVolume = prefs->GetSetting("fAudioMasterVolume:AudioMenu");
        }
        SKSE::log::info("audio: master volume {}", state.masterVolume ? std::format("{:.2f} (fAudioMasterVolume)", state.masterVolume->GetFloat()) : "not found, full");
        for (auto* category : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSSoundCategory>()) {
            if (category && category->IsMenuCategory()) {
                SKSE::log::info("audio: menu sound category {:08X} \"{}\" at {:.2f}{}", category->GetFormID(), category->GetFullName(),
                    category->GetCategoryVolume(),
                    category == state.music ? " (music)" : category == state.effects ? " (effects)" : "");
            }
        }
        if (!state.music || !state.effects) {
            SKSE::log::warn("audio: sound categories missing (effects {}, music {}): those follow full volume", state.effects != nullptr, state.music != nullptr);
        }

        // Sweetroll FM plays the tracks installed as loose files (the working directory is Skyrim's).
        std::vector<std::string_view> installed;
        for (const auto track : Radio::kSweetrollTracks) {
            std::error_code error;
            if (std::filesystem::exists(std::filesystem::path("Data") / std::string(track), error)) {
                installed.push_back(track);
            }
        }
        state.sweetroll.SetTracks(installed);
        SKSE::log::info("audio: Sweetroll FM has {} of {} tracks installed in Data\\Music\\AhShitRim\\SweetrollFM", installed.size(), Radio::kSweetrollTracks.size());
    }

    void Update(float a_delta, bool a_owned, bool a_paused, const Protocol::PlayerState* a_pose, bool a_cue)
    {
        auto& link = LinkService::Get();
        const auto volumes = Radio::FromSliders(state.masterVolume ? state.masterVolume->GetFloat() : 1.0f, Volume(state.effects), Volume(state.music), a_owned, a_cue);
        Protocol::AudioState audio{};
        audio.effects = volumes.effects;
        audio.music = volumes.music;
        link.WriteAudio(audio);

        const bool         inVehicle = a_owned && a_pose && (a_pose->flags & Protocol::kPlayerInVehicle) != 0;
        const std::int8_t  station = inVehicle ? a_pose->radioStation : Protocol::kRadioNone;
        const bool         retuning = inVehicle && (a_pose->flags & Protocol::kPlayerRadioRetuning) != 0;
        if (station != state.station || retuning != state.retuning) {
            if (const auto name = Radio::StationName(station); !name.empty() && inVehicle) {
                // Sweetroll FM with none of its tracks installed (0.1 ships without them) says so on the dial.
                const bool offAir = station == Radio::kSweetroll && state.sweetroll.Size() == 0;
                SaHud::ShowRadioStation(offAir ? std::string(name) + " - off air" : std::string(name), retuning);
            }
            if (station != state.station) {
                StopTrack();
                state.tunedIn = Radio::SkyrimPlays(station);
                if (station == Radio::kSovngarde) {
                    state.sovngarde.Tune();
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    state.combat = player && player->IsInCombat();
                    state.combatFlip = 0.0f;
                } else if (station == Radio::kSweetroll) {
                    state.sweetroll.Tune();
                    if (state.sweetroll.Size() == 0 && !state.sweetrollWarned) {
                        SKSE::log::warn("audio: Sweetroll FM has no tracks installed (Data\\Music\\AhShitRim\\SweetrollFM), so it stays silent");
                        state.sweetrollWarned = true;
                    }
                }
            }
            SKSE::log::info("audio: radio {}{}", station == Protocol::kRadioNone ? "none"sv : Radio::StationName(station), retuning ? " (retuning)" : "");
            state.station = station;
            state.retuning = retuning;
        }

        SilenceSkyrimMusic(Radio::Plays(station) || a_cue);
        if (a_paused || a_cue) {
            SetPaused(true);  // a cue: San Andreas stops its own radio for the tune too
        } else if (Radio::SkyrimPlays(station) && !retuning && (station != Radio::kSweetroll || state.sweetroll.Size() > 0)) {
            PlayStation(a_delta, volumes.music);
        }
    }
}
