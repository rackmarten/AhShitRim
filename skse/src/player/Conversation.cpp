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
            bool                  subtitleShown = false;  // Skyrim's subtitle is up in San Andreas's style
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
            std::string   line;  // what the speaker says now, when Skyrim has its text
        };

        // The text of the response the speaker says now: the current response of the dialogue that
        // leads to it (the one last picked, or another in the menu's list).
        std::string SpokenLine(RE::MenuTopicManager* a_topics)
        {
            auto* saying = a_topics->currentTopicInfo;
            if (!saying) {
                return {};
            }
            const auto lineOf = [&](RE::MenuTopicManager::Dialogue* a_dialogue) -> std::string {
                if (!a_dialogue || a_dialogue->parentTopicInfo != saying || !a_dialogue->currentResponse) {
                    return {};
                }
                const auto* response = a_dialogue->currentResponse->item;
                return response && response->text.c_str() ? response->text.c_str() : "";
            };
            if (auto line = lineOf(a_topics->lastSelectedDialogue); !line.empty()) {
                return line;
            }
            if (a_topics->dialogueList) {
                for (auto* dialogue : *a_topics->dialogueList) {
                    if (auto line = lineOf(dialogue); !line.empty()) {
                        return line;
                    }
                }
            }
            return {};
        }

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
            s.line = SpokenLine(a_topics);
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

        // The subtitle Skyrim's HUD shows: it gets each one, in dialogue or out of it, as a HUD message to
        // show it and one to hide it (a skipped line hides at once). Its menu takes them on the UI's
        // thread, hence the lock.
        struct HudSubtitle
        {
            std::mutex    lock;
            std::string   text;
            bool          shown = false;
            std::uint32_t count = 0;  // messages so far
        } hudSubtitle;

        // Set from the main thread, read wherever Skyrim starts a dialogue (its AI threads too).
        std::atomic<bool> mayTalk{ true };

        // Every dialogue with the player starts with Actor::SetDialogueWithPlayer(true, ...): talking to
        // someone, someone walking up to the player, a force-greet. Refused while CJ may not talk; a
        // package that keeps trying is logged once per speaker per 5 s.
        struct DialogueHook
        {
            static bool thunk(RE::Actor* a_actor, bool a_start, bool a_forceGreet, RE::TESTopicInfo* a_topic)
            {
                if (a_start && !mayTalk) {
                    static std::atomic<RE::FormID> lastId{ 0 };
                    static std::atomic<std::int64_t> lastAt{ 0 };
                    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    const auto id = a_actor ? a_actor->GetFormID() : 0;
                    if (id != lastId.exchange(id) || now - lastAt > 5000) {
                        lastAt = now;
                        SKSE::log::info("conversation: {} ({:08X}) can't {} CJ: he's driving or moving fast", a_actor ? a_actor->GetName() : "?", id,
                            a_forceGreet ? "force-greet" : "talk to");
                    }
                    return false;
                }
                return original(a_actor, a_start, a_forceGreet, a_topic);
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };

        struct HudMessageHook
        {
            static RE::UI_MESSAGE_RESULTS thunk(RE::HUDMenu* a_menu, RE::UIMessage& a_message)
            {
                if (a_message.type == RE::UI_MESSAGE_TYPE::kUpdate && a_message.data) {
                    if (auto* data = skyrim_cast<RE::HUDData*>(a_message.data)) {
                        const auto type = data->type.get();
                        if (type == RE::HUD_MESSAGE_TYPE::kShowSubtitle || type == RE::HUD_MESSAGE_TYPE::kHideSubtitle) {
                            std::scoped_lock guard(hudSubtitle.lock);
                            if (hudSubtitle.count == 0) {
                                SKSE::log::info("conversation: Skyrim's HUD shows its first subtitle");
                            }
                            hudSubtitle.shown = type == RE::HUD_MESSAGE_TYPE::kShowSubtitle;
                            hudSubtitle.text = hudSubtitle.shown && data->text.c_str() ? data->text.c_str() : "";
                            hudSubtitle.count++;
                        }
                    }
                }
                return original(a_menu, a_message);
            }
            static inline REL::Relocation<decltype(thunk)> original;
        };

        // Shows Skyrim's subtitle in San Andreas's style while Skyrim shows it and takes it down the
        // moment Skyrim does, whatever Skyrim's own subtitle settings: San Andreas's cutscenes always
        // have them. With Skyrim's subtitles off its HUD may get no text, so in a dialogue the line the
        // speaker says (a_dialogueLine) stands in.
        void FollowSubtitle(const std::string& a_dialogueLine)
        {
            std::string text;
            bool        shown = false;
            {
                std::scoped_lock guard(hudSubtitle.lock);
                shown = hudSubtitle.shown;
                text = hudSubtitle.text;
            }
            if (!shown || text.empty()) {
                text = a_dialogueLine;
            }
            shown = !text.empty();
            if (shown) {
                SaHud::HoldSubtitle(std::move(text));
            } else if (state.subtitleShown) {
                SaHud::HideSubtitle();
            }
            state.subtitleShown = shown;
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
        const auto s = speaker ? ReadSelection(topics) : Selection{};
        if (speaker && !state.director.IsOpen()) {
            state.picks.Open(s.dialogue, s.info, s.saying);
            state.side = 0;
            say = state.director.Open();
            SaHud::SetLetterbox(true);
            SKSE::log::info("conversation: talking to {} ({:08X}), CJ greets", speaker->GetName(), speaker->GetFormID());
            if (!mayTalk) {
                SKSE::log::warn("conversation: the dialogue opened though CJ is driving or moving fast");
            }
        } else if (!speaker && state.director.IsOpen()) {
            say = state.director.Close();
            SaHud::SetLetterbox(false);
            SKSE::log::info("conversation: the dialogue closed{}", say == Dialogue::Category::Goodbye ? ", CJ says goodbye" : "");
        } else if (speaker) {
            if (state.picks.Update(s.dialogue, s.info, s.saying)) {
                const auto fromTable = Dialogue::TopicCategory(s.topic);
                const auto category = Dialogue::CategoryOf(s.topic, s.text);
                say = state.director.Pick(category);
                SKSE::log::info("conversation: the player picked topic {:08X} (response {:08X}), CJ answers: {} ({})", s.topic, s.info,
                    Dialogue::NameOf(category), fromTable != Dialogue::Category::None ? "topic table" : "rule");
            }
        }
        state.director.Tick(a_delta);
        FollowSubtitle(s.line);
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

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_HUDMenu[0] };
        HudMessageHook::original = vtable.write_vfunc(0x4, HudMessageHook::thunk);
        REL::Relocation<std::uintptr_t> character{ RE::VTABLE_Character[0] };
        DialogueHook::original = character.write_vfunc(0x41, DialogueHook::thunk);
    }

    void SetMayTalk(bool a_mayTalk) { mayTalk = a_mayTalk; }
    bool MayTalk() { return mayTalk; }

    bool DialogueOpen() { return Speaker(RE::MenuTopicManager::GetSingleton()) != nullptr; }

    void Clear()
    {
        state.director = {};
        state.side = 0;
        state.subtitleShown = false;
        HideSkyrimSubtitles(false);
    }
}
