#pragma once

#include "radar.h"

// What San Andreas's radar shows (#900), gathered from Skyrim every frame while San Andreas has the
// player: where CJ is, whether the map is of the space he's in, and the blips. The player's own map
// marker, the quest targets Skyrim's compass points to (through the door that leads to them, when
// they're in another cell or worldspace, as the compass does), Ammu-Nation at the blacksmiths who keep
// one (protocol/cpp/shop.h) and whoever is fighting the player. SaHud draws it (protocol/cpp/radar.h).
namespace AhShitRim::RadarScene
{
    struct Blip
    {
        Radar::Kind kind = Radar::Kind::Quest;
        Radar::Vec2 at;
        float       z = 0.0f;
    };

    struct Scene
    {
        bool              mapped = false;  // CJ is in a_mapWorld or one that shares its map (a city in it)
        Radar::Vec2       cj;
        float             z = 0.0f;
        std::vector<Blip> blips;  // in the order San Andreas draws them: traces, then sprites, the waypoint last
    };

    // a_mapWorld is the worldspace the map is of (Tamriel's form id).
    void Gather(RE::PlayerCharacter* a_player, RE::FormID a_mapWorld, Scene& a_out);
}
