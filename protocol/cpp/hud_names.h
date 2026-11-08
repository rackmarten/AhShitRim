// San Andreas's two names bottom right while San Andreas has the player: the zone name (Skyrim's
// location, as CHud::DrawAreaName draws San Andreas's zones) and the vehicle name (CHud::DrawVehicleName,
// for the vehicle CJ is in). Each fades in over a second, stays three and fades out over a second; a new
// one while another shows fades the old one out first (San Andreas's NAME_SWITCH). One coming up fades
// the other out, and both step aside for the subtitles: the zone name for the mission title and the big
// messages too. Game-free, so the protocol tests cover it; SaHud draws them (hud_layout.h's kZoneName and
// kVehicleName).

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace AhShitRim::HudNames
{
    inline constexpr float kFadeSeconds = 1.0f;  // in, and out again
    inline constexpr float kShowSeconds = 3.0f;  // at full in between

    enum class Phase : std::uint8_t
    {
        Hidden,
        FadeIn,
        Shown,
        FadeOut,
        Switch,  // the one up fading out for the next
    };

    struct Name
    {
        Phase       phase = Phase::Hidden;
        std::string printed;      // what shows
        std::string next;         // what shows once a switch is done
        float       fade = 0.0f;  // seconds into the fade, 0..kFadeSeconds
        float       shown = 0.0f;  // seconds drawn since it came up
        float       alpha = 0.0f;  // 0..1, this frame; 0 when not drawn

        bool Up() const { return phase == Phase::Shown || phase == Phase::FadeIn; }
    };

    // What may be drawn this frame, from what else the HUD shows.
    struct Frame
    {
        float delta = 0.0f;         // seconds since the last frame
        bool  drawZone = true;      // false in an aircraft, a menu or a cutscene: the zone name waits as it is
        bool  drawVehicle = true;   // false in a menu or a cutscene
        bool  subtitle = false;     // the subtitles show: neither name is drawn
        bool  bigText = false;      // the mission title, "wasted", "busted" or "mission passed!": the zone name fades out
        bool  screenFading = false; // the screen fades to or from black: the zone name's fades wait
    };

    class Names
    {
    public:
        // A location entered: its name comes up, or replaces the one up. The same name while it's up stays as it is.
        void SetZone(std::string a_name)
        {
            if (a_name.empty()) {
                return;
            }
            switch (zone.phase) {
            case Phase::Hidden:
                zone.phase = Phase::FadeIn;
                zone.printed = std::move(a_name);
                zone.fade = 0.0f;
                zone.shown = 0.0f;
                if (vehicle.Up()) {
                    vehicle.phase = Phase::FadeOut;
                }
                break;
            case Phase::Shown:
            case Phase::FadeIn:
                if (a_name == zone.printed) {
                    return;
                }
                [[fallthrough]];
            case Phase::FadeOut:
                zone.phase = Phase::Switch;
                zone.next = std::move(a_name);
                zone.shown = 0.0f;
                break;
            case Phase::Switch:
                zone.next = std::move(a_name);
                zone.shown = 0.0f;
                break;
            }
        }

        // Every frame: the vehicle CJ is in, empty on foot (San Andreas's CCurrentVehicle::Display).
        void SetVehicle(const std::string& a_name)
        {
            if (a_name.empty()) {
                vehicle = {};
                lastVehicle.clear();
                return;
            }
            if (a_name == lastVehicle) {
                return;
            }
            lastVehicle = a_name;
            switch (vehicle.phase) {
            case Phase::Hidden:
                vehicle.phase = Phase::FadeIn;
                vehicle.printed = a_name;
                vehicle.fade = 0.0f;
                vehicle.shown = 0.0f;
                if (zone.Up()) {
                    zone.phase = Phase::FadeOut;
                }
                break;
            default:
                vehicle.phase = Phase::Switch;
                vehicle.next = a_name;
                vehicle.shown = 0.0f;
                break;
            }
        }

        void Update(const Frame& a_frame)
        {
            zone.alpha = 0.0f;
            vehicle.alpha = 0.0f;
            if (a_frame.drawZone) {
                UpdateZone(a_frame);
            }
            if (a_frame.drawVehicle) {
                UpdateVehicle(a_frame);
            }
        }

        void Clear()
        {
            zone = {};
            vehicle = {};
            lastVehicle.clear();
        }

        const Name& Zone() const { return zone; }
        const Name& Vehicle() const { return vehicle; }

    private:
        static float AlphaOf(float a_fade) { return std::clamp(a_fade / kFadeSeconds, 0.0f, 1.0f); }

        void UpdateZone(const Frame& a_frame)
        {
            const float dt = (std::max)(a_frame.delta, 0.0f);
            switch (zone.phase) {
            case Phase::Hidden:
                return;
            case Phase::Shown:
                zone.fade = kFadeSeconds;
                if (zone.shown > kShowSeconds) {
                    zone.phase = Phase::FadeOut;
                }
                break;
            case Phase::FadeIn:
                if (!a_frame.screenFading) {
                    zone.fade += dt;
                }
                if (zone.fade > kFadeSeconds) {
                    zone.fade = kFadeSeconds;
                    zone.phase = Phase::Shown;
                }
                break;
            case Phase::FadeOut:
                if (!a_frame.screenFading) {
                    zone.fade -= dt;
                }
                if (zone.fade < 0.0f) {
                    zone = {};
                    return;
                }
                break;
            case Phase::Switch:
                // Its clock runs on through the switch and isn't reset after it, as m_ZoneNameTimer isn't in
                // CHud::DrawAreaName (the vehicle name's is): a switched-to zone is up for less time than a fresh one.
                zone.fade -= dt;
                if (zone.fade < 0.0f) {
                    zone.fade = 0.0f;
                    zone.phase = Phase::FadeIn;
                    zone.printed = std::move(zone.next);
                    zone.next.clear();
                }
                break;
            }
            if (a_frame.subtitle || a_frame.bigText) {
                if (zone.phase != Phase::Switch) {
                    zone.phase = Phase::FadeOut;  // not drawn meanwhile, and gone once nothing's in its way
                }
                return;
            }
            zone.shown += dt;
            zone.alpha = AlphaOf(zone.fade);
        }

        void UpdateVehicle(const Frame& a_frame)
        {
            const float dt = (std::max)(a_frame.delta, 0.0f);
            switch (vehicle.phase) {
            case Phase::Hidden:
                return;
            case Phase::Shown:
                vehicle.fade = kFadeSeconds;
                if (vehicle.shown > kShowSeconds) {
                    vehicle.phase = Phase::FadeOut;
                }
                break;
            case Phase::FadeIn:
                vehicle.fade += dt;
                if (vehicle.fade > kFadeSeconds) {
                    vehicle.fade = kFadeSeconds;
                    vehicle.phase = Phase::Shown;
                }
                break;
            case Phase::FadeOut:
                vehicle.fade -= dt;
                if (vehicle.fade < 0.0f) {
                    vehicle.phase = Phase::Hidden;
                    vehicle.fade = 0.0f;
                    return;
                }
                break;
            case Phase::Switch:
                vehicle.fade -= dt;
                if (vehicle.fade < 0.0f) {
                    vehicle.fade = 0.0f;
                    vehicle.shown = 0.0f;
                    vehicle.phase = Phase::FadeIn;
                    vehicle.printed = std::move(vehicle.next);
                    vehicle.next.clear();
                }
                break;
            }
            if (a_frame.subtitle) {
                return;  // not drawn meanwhile, its clock stopped
            }
            vehicle.shown += dt;
            vehicle.alpha = AlphaOf(vehicle.fade);
        }

        Name        zone;
        Name        vehicle;
        std::string lastVehicle;
    };
}
