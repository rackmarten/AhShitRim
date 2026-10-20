#pragma once

#include "../ahshitrim_protocol.h"

// E while San Andreas has the player: activates what CJ faces (a door, a lever, a chest, an item, a
// person) through Skyrim's own activation, as the player, so locks, crime, quests and load doors all
// work as they do for Skyrim's own activate key (which refuses meanwhile). The pick runs along San
// Andreas's camera from CJ's chest (protocol/cpp/activate.h) over the references round the player,
// and what it picked shows in San Andreas's help box ("E: Open Breezehome"). Nothing is picked in a
// vehicle: load doors only open on foot.
namespace AhShitRim::Activation
{
    // Every frame San Andreas has the player. a_pose is San Andreas's latest for this handover, or
    // nullptr when there is none yet; a_menu while a Skyrim menu has the input.
    void Update(RE::PlayerCharacter* a_player, const Protocol::PlayerState* a_pose, bool a_menu);

    // E pressed: activates what was picked. False when nothing was.
    bool Activate(RE::PlayerCharacter* a_player);

    void Clear();  // nothing picked (a handover ends or starts over)
}
