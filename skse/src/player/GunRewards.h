#pragma once

// Gun rewards (#830, the table in protocol/cpp/rewards.h): a main quest step up to the Greybeards (the
// stage that completes it being set) or a boss of a dungeon in the table dying gives CJ a gun and its
// ammo through Arms::Give, and the help box says so. Only as it happens: a save loaded past a step gives
// nothing again, and a location's boss pays once (until a save is loaded). Whoever has the player, San Andreas or not.
namespace AhShitRim::GunRewards
{
    void OnDataLoaded();  // listens to Skyrim's quest stages and deaths

    // Every frame: hands out what the events brought (they can come off the main thread).
    void Update();

    void OnGameLoad();  // what was waiting belonged to the game before
}
