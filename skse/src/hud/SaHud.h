#pragma once

// San Andreas's HUD and text styles, drawn by Skyrim over its frame (Overlay) in San Andreas's own
// fonts and sprites, read from the player's San Andreas install at startup and never written to
// disk (fonts.dat, fonts.txd, hud.txd; the install is AhShitRim.ini's [SanAndreas] Path).
//
// While San Andreas has the player: the HUD top right as San Andreas lays it out (clock on Skyrim's
// game time, weapon icon, armour bar hidden at 0, health bar on the Skyrim player's health, money
// on their gold, wanted stars once there is a wanted level), and Skyrim's compass, attribute bars
// and crosshair hidden. The text styles below show whenever something asks for them.
namespace AhShitRim::SaHud
{
    enum class BigMessage
    {
        Wasted,         // "wasted", grey Gothic in the middle
        Busted,         // "busted"
        MissionPassed,  // "mission passed!", gold Pricedown in the middle
    };

    void Load();  // reads the San Andreas files and hooks the overlay; call on kDataLoaded

    // Every frame from PlayerCharacter::Update: a_owned while San Andreas has the player, a_menu
    // while a Skyrim menu has the input (the HUD steps aside).
    void Update(float a_delta, bool a_owned, bool a_menu);

    // The text styles; a_seconds is how long each stays before it fades.
    void ShowMissionTitle(std::string a_text);  // bottom right, gold Pricedown, fades out after a few seconds
    void ShowBigMessage(BigMessage a_message, float a_seconds = 4.0f);
    void ShowHelp(std::string a_text, float a_seconds = 6.0f);      // the box top left
    void ShowSubtitle(std::string a_text, float a_seconds = 4.0f);  // bottom centre
    void FadeTo(float a_black, float a_seconds);  // 0 = clear, 1 = black over everything
    void SetLetterbox(bool a_on);                 // cutscene bars, sliding in and out
    void SetWantedLevel(int a_stars);             // 0..6; 0 shows none
    void ShowRadioStation(std::string a_name, bool a_retuning);  // top centre, gold, dark grey while retuning
    void ClearText();                             // every text style, the fade and the bars off

    // The dev key (/ while San Andreas has the player): shows the next text style in turn.
    void NextDemo();
}
