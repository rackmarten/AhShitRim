// Skyrim's conversations as San Andreas cutscenes (v21), the game-free half: which of CJ's line
// categories answers a topic the player picks (the topic table, else a rule on the topic's text),
// noticing the pick, which shot the camera takes when, and where each shot's camera stands.
// Header-only and free of game types, so the protocol tests cover it; Skyrim's half is
// skse/src/player/Conversation.cpp, San Andreas's sa/src/scenes.cpp.
//
// Positions are Skyrim world units, Z up.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "pose.h"
#include "voice_lines.h"

namespace AhShitRim::Dialogue
{
    // What CJ says; InputState.lineKind (v21) carries it, 0 for nothing.
    enum class Category : std::uint8_t
    {
        None,
        Greet,
        Agree,
        Refuse,
        Ask,
        Threaten,
        Joke,
        Goodbye,
    };
    inline constexpr std::uint8_t kCategoryCount = 8;

    constexpr const char* NameOf(Category a_category)
    {
        switch (a_category) {
        case Category::Greet: return "greet";
        case Category::Agree: return "agree";
        case Category::Refuse: return "refuse";
        case Category::Ask: return "ask";
        case Category::Threaten: return "threaten";
        case Category::Joke: return "joke";
        case Category::Goodbye: return "goodbye";
        default: return "none";
        }
    }
}

#include "dialogue_topics.h"

namespace AhShitRim::Dialogue
{
    // ---- What CJ answers ----

    // CJ's lines for a category (voice_lines.h's dialogue picks); none for Category::None.
    inline std::span<const VoiceLines::Line> LinesOf(Category a_category)
    {
        switch (a_category) {
        case Category::Greet: return VoiceLines::kGreet;
        case Category::Agree: return VoiceLines::kAgree;
        case Category::Refuse: return VoiceLines::kRefuse;
        case Category::Ask: return VoiceLines::kAsk;
        case Category::Threaten: return VoiceLines::kThreaten;
        case Category::Joke: return VoiceLines::kJoke;
        case Category::Goodbye: return VoiceLines::kGoodbye;
        default: return {};
        }
    }

    // The topic table's category for a Skyrim.esm topic (DIAL FormID); None when it isn't in it.
    constexpr Category TopicCategory(std::uint32_t a_formId)
    {
        const auto it = std::lower_bound(kTopics.begin(), kTopics.end(), a_formId,
            [](const TopicEntry& a_entry, std::uint32_t a_id) { return a_entry.formId < a_id; });
        return it != kTopics.end() && it->formId == a_formId ? it->category : Category::None;
    }

    // The rule's view of a line: lower case, its words between single spaces with one at either end,
    // apostrophes kept, "(...)" asides (a price, "Persuade") left out.
    inline std::string Words(std::string_view a_text)
    {
        std::string out = " ";
        int         depth = 0;
        for (const char ch : a_text) {
            const auto c = static_cast<unsigned char>(ch);
            if (c == '(') {
                depth++;
            } else if (c == ')') {
                depth = depth > 0 ? depth - 1 : 0;
            } else if (depth == 0 && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '\'')) {
                out += static_cast<char>(c);
                continue;
            } else if (depth == 0 && c >= 'A' && c <= 'Z') {
                out += static_cast<char>(c - 'A' + 'a');
                continue;
            }
            if (out.back() != ' ') {
                out += ' ';
            }
        }
        if (out.back() != ' ') {
            out += ' ';
        }
        return out;
    }

    // The fallback rule's word lists, checked in this order (protocol/tools/topic-table.mjs has the
    // same rule for the topics it can't otherwise classify: keep the two in step).
    namespace Rule
    {
        using List = std::span<const std::string_view>;
        inline constexpr std::string_view kIntimidate[]{ "or else" };
        inline constexpr std::string_view kGoodbye[]{ "goodbye", "farewell", "bye", "see ya", "later", "part ways", "i should go",
            "i have to go", "i need to go", "i must go", "i'll be going", "time to go" };
        inline constexpr std::string_view kRefuseStart[]{ "no", "nope", "nah" };
        inline constexpr std::string_view kRefuse[]{ "never mind", "nevermind", "not interested", "forget it", "i won't", "i will not", "i'm not going",
            "not going to", "i don't want", "rather not", "no thanks", "maybe not", "not now", "another time", "too rich", "i refuse", "i can't",
            "i cannot" };
        inline constexpr std::string_view kGreetStart[]{ "hello", "hi", "hey", "greetings", "good day", "good morning", "good evening", "well met",
            "hail" };
        inline constexpr std::string_view kJoke[]{ "joke", "kidding", "funny", "haha", "ha ha" };
        inline constexpr std::string_view kThreaten[]{ "kill you", "you'll regret", "stand aside", "out of my way", "shut up", "rather die", "or i'll",
            "i'll kill", "you'll pay", "back off" };
        inline constexpr std::string_view kAgreeStart[]{ "yes", "yeah", "yep", "sure", "okay", "ok", "alright", "all right", "fine", "deal",
            "very well", "of course", "gladly", "sounds good", "let's", "agreed", "understood", "got it", "certainly" };
        inline constexpr std::string_view kAgree[]{ "i'll do", "i'll take", "i'll help", "i'll bring", "i'll deliver", "i'll get", "i'll find",
            "i'll come", "i'll go", "i'll talk", "i'll see", "i'll pay", "i'll rent", "i'll hire", "i'd like", "i want to", "i accept",
            "consider it done", "count me in", "i'm in", "i'm ready", "i'm sure", "take this", "here you go", "i have the", "i got you", "i submit",
            "follow me", "wait here", "maybe i will", "i will", "let me" };

        // a_words (Words) has one of the phrases as whole words: anywhere (Has), or at its start (Starts).
        inline bool Has(const std::string& a_words, List a_list)
        {
            return std::any_of(a_list.begin(), a_list.end(),
                [&](std::string_view a_phrase) { return a_words.find(" " + std::string(a_phrase) + " ") != std::string::npos; });
        }

        inline bool Starts(const std::string& a_words, List a_list)
        {
            return std::any_of(a_list.begin(), a_list.end(),
                [&](std::string_view a_phrase) { return a_words.starts_with(" " + std::string(a_phrase) + " "); });
        }
    }

    // The runtime fallback for a topic the table doesn't know: a rule on the text the player picked.
    // An intimidation threatens; a goodbye; a question asks; then refusals, greetings, jokes, threats
    // and agreement by their words; anything else asks.
    inline Category Classify(std::string_view a_text)
    {
        std::string lower(a_text);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        const auto words = Words(a_text);
        if (lower.find("(intimidate)") != std::string::npos || lower.find("(brawl)") != std::string::npos || Rule::Has(words, Rule::kIntimidate)) {
            return Category::Threaten;
        }
        if (Rule::Has(words, Rule::kGoodbye)) {
            return Category::Goodbye;
        }
        if (lower.find('?') != std::string::npos) {
            return Category::Ask;
        }
        if (Rule::Starts(words, Rule::kRefuseStart) || Rule::Has(words, Rule::kRefuse)) {
            return Category::Refuse;
        }
        if (Rule::Starts(words, Rule::kGreetStart)) {
            return Category::Greet;
        }
        if (Rule::Has(words, Rule::kJoke)) {
            return Category::Joke;
        }
        if (Rule::Has(words, Rule::kThreaten)) {
            return Category::Threaten;
        }
        if (Rule::Starts(words, Rule::kAgreeStart) || Rule::Has(words, Rule::kAgree)) {
            return Category::Agree;
        }
        return Category::Ask;
    }

    // A picked topic's category: the table's, else the rule's on what the player picked.
    inline Category CategoryOf(std::uint32_t a_topic, std::string_view a_text)
    {
        const auto category = TopicCategory(a_topic);
        return category != Category::None ? category : Classify(a_text);
    }

    // ---- Who may talk ----

    inline constexpr float kTalkSpeedMax = 2.5f;  // m/s: a brisk walk (San Andreas's walk is about 1.5, its jog about 4)

    // While CJ has the player, nobody starts a dialogue with him, neither he with E nor someone walking up
    // to him or force-greeting him, while he is in a vehicle or moving faster than a walk: the cutscene
    // takes the controls, and a vehicle would run on without him. A speed that isn't a number counts as
    // moving.
    constexpr bool MayTalk(bool a_inVehicle, float a_metersPerSecond)
    {
        return !a_inVehicle && a_metersPerSecond <= kTalkSpeedMax;
    }

    // ---- Noticing a pick ----

    // The player picking a topic in the open dialogue, from what Skyrim's MenuTopicManager shows
    // each frame: the dialogue last selected (an id that changes with each topic, 0 for none) and
    // the response it leads to, and the response the speaker says now (0 while quiet). A new
    // selection is a pick. Picking the same topic again changes nothing there, so a second start of
    // its response, after the one that answered the first pick, is a pick too.
    class PickTracker
    {
    public:
        // The dialogue opened: what is there already is no pick.
        void Open(std::uint64_t a_selected, std::uint32_t a_selectedInfo, std::uint32_t a_saying)
        {
            _selected = a_selected;
            _info = a_selectedInfo;
            _saying = a_saying;
            _answered = true;
        }

        bool Update(std::uint64_t a_selected, std::uint32_t a_selectedInfo, std::uint32_t a_saying)
        {
            if (a_selected != 0 && (a_selected != _selected || a_selectedInfo != _info)) {
                _selected = a_selected;
                _info = a_selectedInfo;
                _answered = a_saying != 0 && a_saying == a_selectedInfo;
                _saying = a_saying;
                return true;
            }
            const bool starts = a_saying != 0 && a_saying == _info && a_saying != _saying;
            _saying = a_saying;
            if (!starts) {
                return false;
            }
            if (_answered) {
                return true;
            }
            _answered = true;
            return false;
        }

    private:
        std::uint64_t _selected = 0;
        std::uint32_t _info = 0;
        std::uint32_t _saying = 0;
        bool          _answered = true;
    };

    // ---- The cutscene ----

    enum class Shot : std::uint8_t
    {
        None,     // no dialogue: San Andreas's camera
        TwoShot,  // CJ and the speaker side on
        Speaker,  // the speaker over CJ's shoulder
    };

    // When the camera cuts and when CJ talks. The dialogue opening and each of CJ's answers cut to
    // the two-shot, then to the speaker. CJ greets on opening and says goodbye on leaving, unless he
    // has just spoken (a topic that ends the dialogue: his answer to it stays the last word).
    class Director
    {
    public:
        static constexpr float kOpenTwoShot = 2.5f;   // s on the two-shot after the dialogue opens
        static constexpr float kLineTwoShot = 2.0f;   // and after CJ answers a topic
        static constexpr float kGoodbyeQuiet = 2.5f;  // s CJ must have been quiet to say goodbye

        Category Open()
        {
            _open = true;
            _twoShot = kOpenTwoShot;
            _sinceLine = 0.0f;
            return Category::Greet;
        }

        Category Pick(Category a_category)
        {
            if (!_open || a_category == Category::None) {
                return Category::None;
            }
            _twoShot = kLineTwoShot;
            _sinceLine = 0.0f;
            return a_category;
        }

        Category Close()
        {
            if (!_open) {
                return Category::None;
            }
            _open = false;
            return _sinceLine >= kGoodbyeQuiet ? Category::Goodbye : Category::None;
        }

        void Tick(float a_seconds)
        {
            _twoShot = (std::max)(_twoShot - a_seconds, 0.0f);
            _sinceLine += a_seconds;
        }

        bool IsOpen() const { return _open; }
        Shot Current() const { return !_open ? Shot::None : _twoShot > 0.0f ? Shot::TwoShot : Shot::Speaker; }

    private:
        bool  _open = false;
        float _twoShot = 0.0f;
        float _sinceLine = 0.0f;
    };

    // ---- Where the camera stands ----

    inline constexpr float kTwoShotFov = 60.0f;  // degrees across a 4:3 screen, as San Andreas's camera sends it
    inline constexpr float kSpeakerFov = 45.0f;
    inline constexpr float kNearest = 30.0f;     // units: a camera pulled in by a wall stays at least this far out

    struct CameraShot
    {
        Pose::Vec3 position;
        Pose::Vec3 forward;  // unit
        Pose::Vec3 target;   // what it looks at; a wall between the two pulls the camera in (PullIn)
        float      fov = 0.0f;
    };

    // Which side of the line from CJ to the speaker a_camera is on: +1 left (looking from CJ to
    // the speaker), -1 right. A conversation's shots keep to the side San Andreas's camera was on
    // when it opened, so CJ and the speaker never swap sides on screen.
    inline int SideOf(const Pose::Vec3& a_camera, const Pose::Vec3& a_cj, const Pose::Vec3& a_speaker)
    {
        const float dx = a_speaker.x - a_cj.x, dy = a_speaker.y - a_cj.y;
        const float cx = a_camera.x - a_cj.x, cy = a_camera.y - a_cj.y;
        return dx * cy - dy * cx >= 0.0f ? 1 : -1;
    }

    // a_cj and a_speaker are their eyes. The two-shot stands off to a_side of the middle between
    // them, far enough for both to fit with room around them; the speaker's shot stands behind CJ's
    // head and to a_side of it, looking past his shoulder at the speaker's face.
    inline CameraShot Frame(Shot a_shot, const Pose::Vec3& a_cj, const Pose::Vec3& a_speaker, int a_side)
    {
        float       dx = a_speaker.x - a_cj.x, dy = a_speaker.y - a_cj.y;
        const float apart = std::hypot(dx, dy);
        if (apart < 1.0f) {
            dx = 0.0f;
            dy = 1.0f;
        } else {
            dx /= apart;
            dy /= apart;
        }
        const float side = a_side < 0 ? -1.0f : 1.0f;
        const float nx = -dy * side, ny = dx * side;  // towards the camera's side
        CameraShot  shot;
        if (a_shot == Shot::TwoShot) {
            const Pose::Vec3 middle{ (a_cj.x + a_speaker.x) / 2.0f, (a_cj.y + a_speaker.y) / 2.0f, (a_cj.z + a_speaker.z) / 2.0f - 8.0f };
            const float      distance = 1.45f * apart + 90.0f;  // half of 60 degrees spans both, with a head's room either side
            shot.target = middle;
            shot.position = { middle.x + nx * distance, middle.y + ny * distance, middle.z };
            shot.fov = kTwoShotFov;
        } else {
            shot.target = { a_speaker.x, a_speaker.y, a_speaker.z - 4.0f };
            shot.position = { a_cj.x - dx * 40.0f + nx * 22.0f, a_cj.y - dy * 40.0f + ny * 22.0f, a_cj.z + 2.0f };
            shot.fov = kSpeakerFov;
        }
        const float fx = shot.target.x - shot.position.x, fy = shot.target.y - shot.position.y, fz = shot.target.z - shot.position.z;
        const float length = std::hypot(fx, fy, fz);
        shot.forward = length > 0.0f ? Pose::Vec3{ fx / length, fy / length, fz / length } : Pose::Vec3{ 0.0f, 1.0f, 0.0f };
        return shot;
    }

    // Something solid a_hitFraction of the way from the shot's target to its camera: the camera
    // comes in to just before it, no nearer than kNearest, still looking at the target.
    inline void PullIn(CameraShot& a_shot, float a_hitFraction)
    {
        const float fx = a_shot.position.x - a_shot.target.x, fy = a_shot.position.y - a_shot.target.y, fz = a_shot.position.z - a_shot.target.z;
        const float length = std::hypot(fx, fy, fz);
        if (!(a_hitFraction >= 0.0f && a_hitFraction < 1.0f) || length <= kNearest) {
            return;
        }
        const float keep = (std::max)(a_hitFraction * length - 10.0f, kNearest) / length;
        if (keep < 1.0f) {
            a_shot.position = { a_shot.target.x + fx * keep, a_shot.target.y + fy * keep, a_shot.target.z + fz * keep };
        }
    }
}
