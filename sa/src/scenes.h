#pragma once

class CPlayerPed;
class CVector;

namespace AhShitRim::Protocol
{
    struct InputState;
}

// Scenes while CJ has the player (protocol v9, the rules in protocol/cpp/scene.h).
// - Following: when Skyrim moves its player itself (a scene, a seat, a bed; kInputScripted), CJ is put
//   where the player is every frame, without collision or gravity, facing its heading, sitting
//   (SEAT_IDLE) where it sits and walking or running by how fast it moves. His vehicle stays where
//   he left it. When Skyrim gives the player back he goes on from there.
// - Talking: with kInputFace CJ stands still and turns to the one he talks to.
// - Shouting: a new InputState.shoutCount plays SHOVE_PARTIAL over whatever he does, with an angry face.
// - Swearing (v16): a new InputState.swearCount has CJ say an insult, and give the finger on foot.
// Game thread only.
namespace AhShitRim::Scenes
{
    // Every input read (CPad::UpdatePads), before the pad is set: remembers what Skyrim asks for and
    // plays the gesture of a new shout. Returns whether CJ follows Skyrim's player, so the pad stays
    // empty.
    bool OnInput(CPlayerPed* a_ped, const Protocol::InputState& a_input, bool a_owned);

    // Once a frame after the ground (ChunkWorld::Update) and before CJ's pose is read: puts CJ where
    // Skyrim's player is, or turns him to a speaker. a_spawn is where this handover put him.
    void Update(CPlayerPed* a_ped, const CVector& a_spawn);

    // Skyrim has the player back, or a new handover: no scene goes on.
    void End(CPlayerPed* a_ped);

    bool Following();
}
