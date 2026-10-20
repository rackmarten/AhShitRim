#include "vitals.h"

#include "health.h"
#include "log.h"

#include <plugin.h>

#include <CPlayerPed.h>
#include <CTimer.h>

#include <format>
#include <utility>

namespace AhShitRim::Vitals
{
    namespace
    {
        constexpr float kSlowMotion = 0.3f;  // game speed while the player is dying, as Wasted::Timing

        struct State
        {
            bool  counting = false;
            float total = 0.0f;
            bool  wasted = false;
        bool  deadLogged = false;
        } state;
    }

    void Begin(CPlayerPed* a_ped, bool a_counting)
    {
        if (!a_ped) {
            return;
        }
        state.counting = a_counting;
        a_ped->m_fMaxHealth = Health::kSaHealth;
        a_ped->m_fHealth = Health::kSaHealth;
        a_ped->m_fArmour = 0.0f;
    }

    void End(CPlayerPed* a_ped)
    {
        if (!a_ped) {
            return;
        }
        const float lost = Health::Lost(Health::kSaHealth, a_ped->m_fHealth);
        if (state.counting && lost > 0.0f) {
            state.total += lost;
            Log(std::format("health: San Andreas took {:.1f} off CJ ({:.1f} of a bar in all)", lost, state.total / Health::kSaFullHealth));
        }
        if (!a_ped->IsAlive() && !std::exchange(state.deadLogged, true)) {
            Log("health: CJ died in San Andreas anyway (his health was a full bar and a buffer); Skyrim's health is what counts");
        }
    }

    void SetWasted(bool a_wasted)
    {
        if (a_wasted == state.wasted) {
            return;
        }
        state.wasted = a_wasted;
        CTimer::ms_fTimeScale = a_wasted ? kSlowMotion : 1.0f;
        Log(a_wasted ? "health: Wasted, San Andreas in slow motion" : "health: Wasted is over, San Andreas at full speed");
    }

    float Damage() { return state.total; }
}
