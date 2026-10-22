#include "player/Conversation.h"

#include "hud/SaHud.h"

namespace AhShitRim::Conversation
{
    namespace
    {
        constexpr float kCjEyeHeight = 112.0f;  // units over CJ's feet, about 1.6 m
        constexpr int   kMaxRayHits = 4;        // people a camera ray passes through before it gives up

        // Main thread only: PlayerCharacter::Update runs all of it.
        struct State
        {
            Dialogue::Director    director;
            Dialogue::PickTracker picks;
            int                   side = 0;  // the side of CJ and the speaker the shots keep to; 0 until the first shot
            bool                  subtitlesHidden = false;
        } state;

        // The speaker of the open dialogue, or nullptr.
        RE::NiPointer<RE::TESObjectREFR> Speaker(RE::MenuTopicManager* a_topics)
        {
            return a_topics && a_topics->menuOpen ? a_topics->speaker.get() : nullptr;
        }

        // What MenuTopicManager shows about the player's last pick (PickTracker's input), under its lock:
        // the dialogue thread changes it.
        struct Selection
        {
            std::uint64_t dialogue = 0;
            std::uint32_t info = 0;
            std::uint32_t saying = 0;
            RE::FormID    topic = 0;
            std::string   text;
        };

        Selection ReadSelection(RE::MenuTopicManager* a_topics)
        {
            Selection s;
            REX::W32::EnterCriticalSection(&a_topics->criticalSection);
            if (auto* d = a_topics->lastSelectedDialogue) {
                s.dialogue = reinterpret_cast<std::uintptr_t>(d);
                s.info = d->parentTopicInfo ? d->parentTopicInfo->GetFormID() : 0;
                s.topic = d->parentTopic ? d->parentTopic->GetFormID() : 0;
                s.text = d->topicText.c_str();
            }
            s.saying = a_topics->currentTopicInfo ? a_topics->currentTopicInfo->GetFormID() : 0;
            REX::W32::LeaveCriticalSection(&a_topics->criticalSection);
            return s;
        }

        RE::NiPoint3 EyeOf(RE::TESObjectREFR* a_ref)
        {
            if (auto* head = a_ref->GetNodeByName(RE::BSFixedString("NPC Head [Head]"))) {
                return head->world.translate + RE::NiPoint3(0.0f, 0.0f, 6.0f);
            }
            return a_ref->GetPosition() + RE::NiPoint3(0.0f, 0.0f, a_ref->GetHeight() * 0.92f);
        }

        bool Interface(const char* a_name)
        {
            auto* prefs = RE::INIPrefSettingCollection::GetSingleton();
            auto* setting = prefs ? prefs->GetSetting(a_name) : nullptr;
            return !setting || setting->GetBool();
        }

        // The line Skyrim subtitles now (its current speaker's), as its Interface settings allow:
        // dialogue subtitles while a dialogue is open, general ones otherwise.
        std::string Subtitle(bool a_dialogue)
        {
            auto* subtitles = RE::SubtitleManager::GetSingleton();
            if (!subtitles || !Interface(a_dialogue ? "bDialogueSubtitles:Interface" : "bGeneralSubtitles:Interface")) {
                return {};
            }
            RE::BSSpinLockGuard lock(subtitles->lock);
            for (auto& subtitle : subtitles->subtitles) {
                if (subtitle.speaker == subtitles->currentSpeaker && !subtitle.subtitle.empty()) {
                    return subtitle.subtitle.c_str();
                }
            }
            return {};
        }

        // Skyrim's own subtitles (a clip of its HUD movie) are hidden while San Andreas draws them.
        void HideSkyrimSubtitles(bool a_hide)
        {
            if (a_hide == state.subtitlesHidden) {
                return;
            }
            state.subtitlesHidden = a_hide;
            SKSE::GetTaskInterface()->AddUITask([a_hide]() {
                auto* ui = RE::UI::GetSingleton();
                auto  menu = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
                auto* movie = menu ? menu->uiMovie.get() : nullptr;
                RE::GFxValue clip;
                if (!movie || !movie->GetVariable(&clip, "_root.HUDMovieBaseInstance.SubtitleTextHolder") || !clip.IsDisplayObject()) {
                    static bool logged = false;
                    if (movie && !std::exchange(logged, true)) {
                        SKSE::log::warn("conversation: Skyrim's subtitle clip (_root.HUDMovieBaseInstance.SubtitleTextHolder) was not found");
                    }
                    return;
                }
                clip.SetMember("_visible", RE::GFxValue(!a_hide));
            });
        }

        // How far from a_from to a_to (0..1) the first solid thing is, 1 when nothing is. People (the
        // hidden player, the speaker) don't count.
        float FirstHit(RE::PlayerCharacter* a_player, const Pose::Vec3& a_from, const Pose::Vec3& a_to)
        {
            auto* cell = a_player->GetParentCell();
            auto* bhk = cell ? cell->GetbhkWorld() : nullptr;
            auto* world = bhk ? bhk->GetWorld1() : nullptr;
            const float length = std::hypot(a_to.x - a_from.x, a_to.y - a_from.y, a_to.z - a_from.z);
            if (!world || length < 1.0f) {
                return 1.0f;
            }
            const float              scale = RE::bhkWorld::GetWorldScale();
            RE::hkpWorldRayCastInput input;
            input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
            RE::BSReadLockGuard lock(bhk->worldLock);
            float               from = 0.0f;
            for (int i = 0; i < kMaxRayHits && from < 1.0f; i++) {
                const Pose::Vec3 start{ a_from.x + (a_to.x - a_from.x) * from, a_from.y + (a_to.y - a_from.y) * from, a_from.z + (a_to.z - a_from.z) * from };
                input.from = RE::hkVector4(start.x * scale, start.y * scale, start.z * scale, 0.0f);
                input.to = RE::hkVector4(a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f);
                RE::hkpWorldRayCastOutput output;
                world->CastRay(input, output);
                if (!output.HasHit()) {
                    return 1.0f;
                }
                const float at = from + (1.0f - from) * output.hitFraction;
                auto*       hit = output.rootCollidable ? RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable) : nullptr;
                if (!hit || !hit->IsActor()) {
                    return at;
                }
                from = at + 1.0f / length;  // a unit past them
            }
            return 1.0f;
        }
    }

    Dialogue::Category Update(float a_delta)
    {
        HideSkyrimSubtitles(true);
        auto*      topics = RE::MenuTopicManager::GetSingleton();
        const auto speaker = Speaker(topics);
        auto       say = Dialogue::Category::None;
        if (speaker && !state.director.IsOpen()) {
            const auto s = ReadSelection(topics);
            state.picks.Open(s.dialogue, s.info, s.saying);
            state.side = 0;
            say = state.director.Open();
            SaHud::SetLetterbox(true);
            SKSE::log::info("conversation: talking to {} ({:08X}), CJ greets", speaker->GetName(), speaker->GetFormID());
        } else if (!speaker && state.director.IsOpen()) {
            say = state.director.Close();
            SaHud::SetLetterbox(false);
            SKSE::log::info("conversation: the dialogue closed{}", say == Dialogue::Category::Goodbye ? ", CJ says goodbye" : "");
        } else if (speaker) {
            const auto s = ReadSelection(topics);
            if (state.picks.Update(s.dialogue, s.info, s.saying)) {
                const auto fromTable = Dialogue::TopicCategory(s.topic);
                const auto category = Dialogue::CategoryOf(s.topic, s.text);
                say = state.director.Pick(category);
                SKSE::log::info("conversation: the player picked topic {:08X} (response {:08X}), CJ answers: {} ({})", s.topic, s.info,
                    Dialogue::NameOf(category), fromTable != Dialogue::Category::None ? "topic table" : "rule");
            }
        }
        state.director.Tick(a_delta);
        if (auto text = Subtitle(speaker != nullptr); !text.empty()) {
            SaHud::HoldSubtitle(std::move(text));
        }
        return say;
    }

    bool Shot(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cjFeet, const RE::NiPoint3& a_camera, Dialogue::CameraShot& a_out)
    {
        const auto shot = state.director.Current();
        const auto speaker = shot != Dialogue::Shot::None ? Speaker(RE::MenuTopicManager::GetSingleton()) : nullptr;
        if (!speaker) {
            return false;
        }
        const auto       eye = EyeOf(speaker.get());
        const Pose::Vec3 cj{ a_cjFeet.x, a_cjFeet.y, a_cjFeet.z + kCjEyeHeight };
        const Pose::Vec3 them{ eye.x, eye.y, eye.z };
        if (state.side == 0) {
            state.side = Dialogue::SideOf({ a_camera.x, a_camera.y, a_camera.z }, cj, them);
        }
        a_out = Dialogue::Frame(shot, cj, them, state.side);
        Dialogue::PullIn(a_out, FirstHit(a_player, a_out.target, a_out.position));
        return true;
    }

    void Clear()
    {
        state.director = {};
        state.side = 0;
        HideSkyrimSubtitles(false);
    }
}
