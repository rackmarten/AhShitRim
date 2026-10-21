#include <doctest/doctest.h>

#include <string>

#include "../../cpp/wasted.h"

using namespace AhShitRim::Wasted;

namespace
{
    // Ticks a sequence in 0.1 s steps until a_phase starts, or a_limit seconds pass; returns how long it took.
    float RunTo(Sequence& a_seq, Phase a_phase, bool a_arrived = false, float a_limit = 60.0f)
    {
        float t = 0.0f;
        while (a_seq.GetPhase() != a_phase && t < a_limit) {
            a_seq.Tick(0.1f, a_arrived);
            t += 0.1f;
        }
        return t;
    }
}

TEST_CASE("the temple of a hold, a city or a small hold's nearest")
{
    CHECK(ForPlace("Whiterun Hold")->id == Temple::Kynareth);
    CHECK(ForPlace("Whiterun")->id == Temple::Kynareth);
    CHECK(ForPlace("The Rift")->id == Temple::Mara);
    CHECK(ForPlace("Riften")->id == Temple::Mara);
    CHECK(ForPlace("Haafingar")->id == Temple::Divines);
    CHECK(ForPlace("Solitude Sawmill")->id == Temple::Divines);
    CHECK(ForPlace("Eastmarch")->id == Temple::Talos);
    CHECK(ForPlace("Windhelm")->id == Temple::Talos);
    CHECK(ForPlace("The Reach")->id == Temple::Dibella);
    CHECK(ForPlace("Markarth")->id == Temple::Dibella);
    CHECK(ForPlace("Falkreath")->id == Temple::Kynareth);
    CHECK(ForPlace("Hjaalmarch")->id == Temple::Divines);
    CHECK(ForPlace("The Pale")->id == Temple::Talos);
    CHECK(ForPlace("Winterhold")->id == Temple::Talos);
}

TEST_CASE("a name is matched by whole words and case doesn't matter")
{
    CHECK(ForPlace("the reach")->id == Temple::Dibella);
    CHECK(ForPlace("Breach") == nullptr);
    CHECK(ForPlace("Driftshade Refuge") == nullptr);  // "Rift" is inside a longer word
    CHECK(ForPlace("Bleak Falls Barrow") == nullptr);
    CHECK(ForPlace("") == nullptr);
}

TEST_CASE("every temple names the cell to wake in")
{
    for (const auto& temple : kTemples) {
        CHECK(std::string_view(temple.cell).size() > 0);
        CHECK(&Info(temple.id) == &temple);
    }
    CHECK(std::string(Info(Temple::Kynareth).name) == "Temple of Kynareth");
}

TEST_CASE("the sequence runs falling, dying, fading, moving and waking, and ends")
{
    Sequence   seq;
    const auto begin = seq.Start();
    CHECK(begin.action == Action::Begin);
    CHECK(begin.active);
    CHECK(seq.GetPhase() == Phase::Falling);
    CHECK(begin.down);
    CHECK_FALSE(begin.camera);  // he falls under the camera he had

    const float falling = RunTo(seq, Phase::Dying);
    CHECK(falling == doctest::Approx(Timing{}.fall).epsilon(0.1));

    // CJ lies dead at full speed and colour under the death camera, the picture still clear.
    const auto mid = seq.Tick(1.0f, false);
    CHECK(mid.down);
    CHECK(mid.camera);
    CHECK(mid.fade == 0.0f);

    const float dying = RunTo(seq, Phase::Fading) + 1.0f;
    CHECK(dying == doctest::Approx(Timing{}.dying).epsilon(0.05));
    const auto fading = seq.Tick(1.5f, false);
    CHECK(fading.fade == doctest::Approx(0.5f));
    CHECK(fading.active);
    CHECK(fading.down);
    CHECK(fading.camera);

    RunTo(seq, Phase::Moving);
    CHECK(seq.GetPhase() == Phase::Moving);
    const auto black = seq.Tick(0.1f, false);
    CHECK(black.fade == doctest::Approx(1.0f));
    CHECK(black.active);
    CHECK_FALSE(black.down);  // black: he gets up, out of sight, for the move
    CHECK_FALSE(black.camera);
    CHECK(black.action == Action::None);

    const auto arrived = seq.Tick(0.1f, true);
    CHECK(arrived.action == Action::Revive);
    CHECK(seq.GetPhase() == Phase::Waking);
    CHECK_FALSE(arrived.active);
    CHECK_FALSE(arrived.down);

    const auto waking = seq.Tick(1.0f, true);
    CHECK(waking.fade == doctest::Approx(0.5f));
    const auto done = seq.Tick(1.5f, true);
    CHECK(done.action == Action::Done);
    CHECK_FALSE(seq.Running());
    CHECK(seq.Tick(1.0f, true).action == Action::None);
}

TEST_CASE("\"wasted\" and the move are asked for exactly once")
{
    Sequence seq;
    seq.Start();
    int wasted = 0;
    int moves = 0;
    for (int i = 0; i < 1000 && seq.Running(); i++) {
        const auto action = seq.Tick(0.05f, seq.GetPhase() == Phase::Moving).action;
        wasted += action == Action::Wasted;
        moves += action == Action::Move;
    }
    CHECK(wasted == 1);
    CHECK(moves == 1);
}

TEST_CASE("a temple that never comes ends the wait")
{
    Sequence seq;
    seq.Start();
    RunTo(seq, Phase::Moving);
    const float waited = RunTo(seq, Phase::Waking, false);
    CHECK(waited >= Timing{}.moveTimeout - 0.2f);
    CHECK(waited < Timing{}.moveTimeout + 0.5f);
}

TEST_CASE("cancelling the sequence leaves nothing running")
{
    Sequence seq;
    seq.Start();
    seq.Tick(1.0f, false);
    seq.Cancel();
    CHECK_FALSE(seq.Running());
    const auto f = seq.Tick(1.0f, false);
    CHECK_FALSE(f.active);
    CHECK(f.fade == 0.0f);
    CHECK_FALSE(f.down);
}

TEST_CASE("San Andreas's timing: the fall, 3 s dead, 3 s to black, \"wasted\" from a moment after the cut until then")
{
    const Timing t;
    CHECK(t.fall == doctest::Approx(1.25f));
    CHECK(t.dying == doctest::Approx(3.0f));
    CHECK(t.fadeOut == doctest::Approx(3.0f));
    CHECK(t.wake == doctest::Approx(2.0f));
    CHECK(TextSeconds(t) == doctest::Approx(5.4f));
}

TEST_CASE("\"wasted\" comes up a moment after the death camera, not with it")
{
    Sequence seq;
    seq.Start();
    RunTo(seq, Phase::Dying);
    float cameraFor = 0.0f;
    for (int i = 0; i < 100; i++) {
        const auto f = seq.Tick(0.05f, false);
        cameraFor += 0.05f;
        CHECK(f.camera);
        if (f.action == Action::Wasted) {
            break;
        }
    }
    CHECK(cameraFor == doctest::Approx(Timing{}.textDelay).epsilon(0.1));
}
