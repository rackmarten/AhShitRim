#pragma once

#include "dialogue.h"

// Skyrim's conversations as San Andreas cutscenes while San Andreas has the player (protocol v21; the
// game-free rules in protocol/cpp/dialogue.h). When Skyrim's dialogue menu opens, San Andreas's
// letterbox bars slide in and Skyrim's camera cuts to a two-shot of CJ and the speaker, then to the
// speaker over CJ's shoulder; each topic the player picks has CJ answer with one of his own lines of
// its category (the topic table, else a rule on the topic's text) and cuts back to the two-shot. CJ
// greets when it opens and says goodbye when it closes. Skyrim's subtitles, in or out of a dialogue,
// are drawn in San Andreas's subtitle style instead of Skyrim's (as its Interface settings allow). The
// topic list stays Skyrim's dialogue menu.
namespace AhShitRim::Conversation
{
    // Once a frame while San Andreas has the player, before the input goes out: follows the dialogue
    // menu and Skyrim's subtitles. Returns what CJ says now (InputState.lineKind), None for nothing.
    Dialogue::Category Update(float a_delta);

    // Where Skyrim's camera goes this frame instead of San Andreas's, while a dialogue is open. a_cjFeet
    // is where CJ is drawn; a_camera is San Andreas's camera, whose side of the two the shots keep to.
    bool Shot(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cjFeet, const RE::NiPoint3& a_camera, Dialogue::CameraShot& a_out);

    void Clear();  // the handover ended: no cutscene, Skyrim's own subtitles back
}
