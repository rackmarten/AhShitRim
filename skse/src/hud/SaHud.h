#pragma once

// San Andreas's HUD and text styles, drawn by Skyrim over its frame (Overlay) in San Andreas's own
// fonts and sprites, read from the player's San Andreas install at startup and never written to
// disk (fonts.dat, fonts.txd, hud.txd; the install is AhShitRim.ini's [SanAndreas] Path).
//
// While San Andreas has the player: the HUD top right as San Andreas lays it out (clock on Skyrim's
// game time, weapon icon, armour bar hidden at 0, health bar on the Skyrim player's health, money
// on their gold, counting up and down to it as San Andreas's does, wanted stars once there is a wanted level), and Skyrim's compass, attribute bars
// and crosshair hidden. The text styles below show whenever something asks for them.
namespace AhShitRim::Protocol
{
    struct PlayerState;
}

namespace AhShitRim::SaHud
{
    enum class BigMessage
    {
        Wasted,         // "wasted", grey Gothic in the middle
        Busted,         // "busted"
        MissionPassed,  // "mission passed!", gold Pricedown in the middle, and "respect +" in white under it
    };

    void Load();  // reads the San Andreas files and hooks the overlay; call on kDataLoaded

    // The weapon in CJ's hands (protocol v12), once a frame: its icon and ammo in the HUD, and the crosshair or scope
    // San Andreas's camera puts up. nullptr when San Andreas has no pose for the player.
    void SetWeapon(const Protocol::PlayerState* a_pose);

    // Every frame from PlayerCharacter::Update: a_owned while San Andreas has the player, a_menu
    // while a Skyrim menu has the input (the HUD steps aside).
    void Update(float a_delta, bool a_owned, bool a_menu);

    // The text styles; a_seconds is how long each stays before it fades.
    void MarkTarget(const RE::NiPoint3& a_at, bool a_locked);  // the heat seeker's marker on what it locks on to (world point), as long as it's called every frame
    void ShowMissionTitle(std::string a_text);  // bottom right, gold Pricedown, fades out after a few seconds
    void ShowBigMessage(BigMessage a_message, float a_seconds = 4.0f);  // "wasted" and "busted" under the fade, "mission passed!" over it
    void HideBigMessage();
    void ShowHelp(std::string a_text, float a_seconds = 6.0f);      // the box top left
    void ShowPrompt(std::string a_text);  // the help box while no help shows, as long as it's called every frame ("E: Open Breezehome")
    void ShowSubtitle(std::string a_text, float a_seconds = 4.0f);  // bottom centre
    void HoldSubtitle(std::string a_text);  // the subtitles, as long as it's called every frame with the line (Skyrim's subtitles, a scene's lines)
    void HideSubtitle();                    // the subtitles gone at once (a skipped line)
    void FadeTo(float a_black, float a_seconds);  // 0 = clear, 1 = black over everything
    void SetLetterbox(bool a_on);                 // cutscene bars, sliding in and out
    void SetWantedLevel(int a_stars);             // 0..6; 0 shows none
    void SetWantedFlashing(bool a_flashing);      // v22: the stars flash (just changed, or CJ is out of the guards' sight)
    void SetArmor(float a_fraction);              // 0..1 of the armour bar; 0 hides it
    void ShowRadioStation(std::string a_name, bool a_retuning);  // top centre, gold, dark grey while retuning

    // San Andreas's standard menu top left (Ammu-Nation, #830), as long as it's called every frame: a title and
    // rows of a name and a price.
    struct MenuRow
    {
        std::string name;
        std::string price;
        bool        highlighted = false;
        bool        available = true;  // dark grey when not (too dear, or full)
        bool        owned = false;     // green: more of what CJ has
    };
    void ShowMenu(std::string a_title, std::vector<MenuRow> a_rows);
    void ClearText();                             // every text style, the fade and the bars off

    // The dev key (/ while San Andreas has the player): shows the next text style in turn.
    void NextDemo();
}
