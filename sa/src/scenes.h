#pragma once

#include <cstdint>

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
// - Shouting: a new InputState.shoutCount plays SHOVE_PARTIAL over whatever he does, with an angry face,
//   and (v25) a line of a shout's kind (lineKind from Thuum::kLineShout up, protocol/cpp/thuum.h) has
//   CJ say his line for the shout and its words, cutting off his last one; Skyrim mutes the Dragonborn's voice.
// - Swearing (v16): a new InputState.insultCount has CJ say an insult, and give the finger on foot; a new
//   swearCount has him swear to himself. Neither waits for San Andreas's repeat time, and each plays
//   the lines picked in protocol/cpp/voice_lines.h when it has any.
// - Talking (v21): a new InputState.lineCount has CJ say the next of his lines of lineKind
//   (protocol/cpp/dialogue.h's categories: greet, agree, refuse, ask, threaten, joke, goodbye), cutting
//   off his last line, also while he follows Skyrim's player in a scene.
// - Crashing (v28): CarHits has him say a CRASH_CAR line when his vehicle hits someone.
// - Talking on his own (v29, the rules in protocol/cpp/situations.h): a blow of his that lands, the first
//   hit of a burst of gunfire, aiming a gun on foot, a stealth kill, looting gold, buying in a shop and
//   leaving it, sprinting with a hostile near (kInputHostileNear) and his vehicle crashing each may have
//   him say a line, 4 s after his last at the soonest and never over one; settled in a vehicle with the
//   radio off, or idle on foot, with nothing said for 90 s, he sings until he moves off or the radio
//   comes on. The keys above always cut in.
// Game thread only.
namespace AhShitRim::Scenes
{
    // Once the window shows (after SilentPatch): CJ's swear keys play the lines picked in voice_lines.h.
    void Install();

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

    // CJ's vehicle hit someone (v28, CarHits), or someone hit it (v31, a_why says which for the log): he says one of
    // his CRASH_CAR lines now, San Andreas's pick.
    void SayCrash(CPlayerPed* a_ped, const char* a_why = "hit someone with his vehicle");

    // A hit of CJ's that Skyrim counted (the Hit command, v13): a fight line for a blow, a shoot line for
    // a burst's first hit, now and then.
    void OnHit(CPlayerPed* a_ped, std::uint8_t a_weapon);
}
