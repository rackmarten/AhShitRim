#include "hud/SaHud.h"

#include "Ini.h"
#include "hud/Overlay.h"
#include "hud_layout.h"
#include "img.h"
#include "link/LinkService.h"
#include "loot.h"
#include "melee.h"
#include "txd.h"
#include "explosives.h"
#include "weapons.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace AhShitRim::SaHud
{
    namespace
    {
        using namespace HudLayout;

        // Overlay textures, after its white one. The ones below kFist are always there; the rest are made when the
        // files have them and are 0 when they don't (nothing is drawn from those).
        enum TextureId : std::uint32_t
        {
            kFont2 = 1,  // Gothic and Menu
            kFont1,      // Subtitles and Pricedown
            kFist,
        };

        // The weapon in CJ's hands for the HUD (v12).
        struct WeaponHud
        {
            bool               valid = false;
            std::uint8_t       type = 0;
            std::string        ammo;  // "" when it counts none
            Weapons::Scope     scope = Weapons::Scope::None;
            bool               gun = false;     // the aim crosshair is for guns
            bool               aiming = false;
            float              crosshair[2] = { 0.5f, 0.4f };
            float              aimRadius = 0.2f;  // San Andreas's crosshair size, growing as he fires
            float              fov = 70.0f;       // San Andreas's camera, degrees across a 4:3 screen
        };

        // Skyrim's HUD clips hidden while San Andreas has the player, under
        // _root.HUDMovieBaseInstance. The rollover clips are Skyrim's own activate prompt ("E Open /
        // Skyrim"): its crosshair pick still runs from the player, where CJ is, and San Andreas's help
        // box says the same (Activation).
        constexpr const char* kSkyrimHud[] = { "CompassShoutMeterHolder", "Health", "Magica", "Stamina", "Crosshair", "RolloverNameInstance",
            "RolloverInfoInstance", "RolloverButton_tf", "RolloverGrayBar_mc" };

        struct Timed
        {
            std::string text;
            float       age = 0.0f;
            float       hold = 0.0f;  // seconds at full before fading out; 0 = not showing
            float       fadeIn = 0.25f;
            float       fadeOut = 0.5f;  // 0 for none: it is there, then it's gone

            void Show(std::string a_text, float a_hold)
            {
                text = std::move(a_text);
                age = 0.0f;
                hold = a_hold;
            }

            void Tick(float a_delta)
            {
                if (hold > 0.0f) {
                    age += a_delta;
                    if (age > fadeIn + hold + fadeOut) {
                        hold = 0.0f;
                    }
                }
            }

            float Alpha() const
            {
                if (hold <= 0.0f) {
                    return 0.0f;
                }
                if (age < fadeIn) {
                    return age / fadeIn;
                }
                if (fadeOut <= 0.0f) {
                    return age <= fadeIn + hold ? 1.0f : 0.0f;
                }
                return std::clamp(1.0f - (age - fadeIn - hold) / fadeOut, 0.0f, 1.0f);
            }
        };

        struct State
        {
            bool             loaded = false;
            HudFont::Values  values{};
            Timed            missionTitle{ {}, 0.0f, 0.0f, 0.25f, 1.0f };
            Timed            big;
            const TextStyle* bigStyle = &kBigMessage;
            bool             bigUnderFade = false;  // "wasted" and "busted" go black with the picture
            Timed            help;
            Timed            prompt{ {}, 0.0f, 0.0f, 0.15f, 0.25f };  // what E activates, in the help box while no help shows
            Timed            subtitle{ {}, 0.0f, 0.0f, 0.0f, 0.0f };  // San Andreas shows and drops its subtitles at once, no fades
            Timed            radio;
            bool             radioRetuning = false;
            bool             drawRadio = false;  // only while San Andreas has the player and no menu is up
            float            fade = 0.0f, fadeFrom = 0.0f, fadeTarget = 0.0f, fadeTime = 0.0f, fadeSeconds = 0.0f;
            float            letterbox = 0.0f;  // 0..1, how far the bars are in
            bool             letterboxOn = false;
            int              wanted = 0;
            bool             wantedFlashing = false;
            float            armor = 0.0f;
            int              demo = 0;
            std::uint32_t    frame = 0;
            bool             skyrimHudHidden = false;
            bool             loggedScreen = false;
            WeaponHud        weapon;
            std::uint32_t    siteM16 = 0, siteRocket = 0, sniperCrosshair = 0, cameraCrosshair = 0;  // texture ids
            std::uint32_t    target256 = 0;  // particle.txd's: the heat seeker's lock-on marker
            bool             mark = false;   // a marker to draw, set again every frame (MarkTarget)
            RE::NiPoint3     markAt;
            bool             markLocked = false;
            std::uint32_t    triangle = 0;     // the lock-on's marker (Melee::MarkerImage), made here
            bool             head = false;     // a head marker to draw, set again every frame (MarkHead)
            RE::NiPoint3     headAt;           // the top of the target's head
            std::uint32_t    headColour = 0;   // RGBA, red in the low byte
            bool             aimShown = false; // the gun's crosshair was drawn last frame, at aimX, aimY (fractions of the screen)
            float            aimX = 0.0f, aimY = 0.0f, aimAspect = 0.0f;
            std::array<std::uint32_t, 64> icons{};  // by weapon type
            Loot::Counter    money;  // the money shown, counting towards the player's gold
            float            delta = 0.0f;
            std::string      menuTitle;
            std::vector<MenuRow> menuRows;  // set again every frame (ShowMenu)
        } state;

        std::vector<std::uint8_t> ReadFile(const std::filesystem::path& a_path)
        {
            std::ifstream file(a_path, std::ios::binary);
            return file ? std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {}) : std::vector<std::uint8_t>{};
        }

        Overlay::Image ImageOf(const Txd::Texture* a_texture)
        {
            return a_texture ? Overlay::Image{ a_texture->width, a_texture->height, a_texture->rgba } : Overlay::Image{};
        }

        std::uint32_t Packed(Colour a_colour) { return a_colour.r | (a_colour.g << 8) | (a_colour.b << 16) | (static_cast<std::uint32_t>(a_colour.a) << 24); }

        // ---- Drawing ----

        struct Frame
        {
            Screen                     screen;
            std::vector<Overlay::Quad> quads;

            void Box(const Rect& a_rect, Colour a_colour)
            {
                if (a_colour.a > 0) {
                    quads.push_back({ Overlay::kWhite, a_rect.x0, a_rect.y0, a_rect.x1, a_rect.y1, 0.0f, 0.0f, 1.0f, 1.0f, Packed(a_colour) });
                }
            }

            void Bar(const Rect& a_rect, float a_fraction, Colour a_colour)
            {
                const auto bar = BarOf(screen, a_rect, a_fraction, a_colour);
                Box(bar.border, WithAlpha(kBlack, a_colour.a / 255.0f));
                Box(bar.full, bar.fullColour);
                Box(bar.empty, bar.emptyColour);
            }

            // a_text in a_style: its outline (SetEdge) in black first, as copies one edge out all
            // round, or its drop shadow (SetDropShadowPosition), then the letters in a_colour. a_x overrides the
            // style's x (pixels).
            void Text(const TextStyle& a_style, std::string_view a_text, Colour a_colour, float a_x = -1.0f)
            {
                if (a_colour.a == 0 || a_text.empty()) {
                    return;
                }
                const auto placed = Place(state.values, screen, a_style, a_text);
                const auto glyphs = HudFont::Layout(state.values, placed.params, a_text, a_x >= 0.0f ? a_x : placed.x, placed.y);
                const auto texture = HudFont::FontOf(a_style.style) == 0 ? kFont2 : kFont1;
                if (a_style.edge > 0) {
                    const float         out = OutlinePixels(screen, a_style);
                    const std::uint32_t drop = Packed(WithAlpha(kBlack, a_colour.a / 255.0f));
                    for (int dx = -1; dx <= 1; dx++) {
                        for (int dy = -1; dy <= 1; dy++) {
                            if (dx == 0 && dy == 0) {
                                continue;
                            }
                            for (const auto& g : glyphs) {
                                quads.push_back({ texture, g.x0 + dx * out, g.y0 + dy * out, g.x1 + dx * out, g.y1 + dy * out, g.u0, g.v0, g.u1, g.v1, drop });
                            }
                        }
                    }
                }
                if (a_style.shadow > 0) {
                    const auto [sx, sy] = ShadowPixels(screen, a_style);
                    const std::uint32_t drop = Packed(WithAlpha(kBlack, a_colour.a / 255.0f));
                    for (const auto& g : glyphs) {
                        quads.push_back({ texture, g.x0 + sx, g.y0 + sy, g.x1 + sx, g.y1 + sy, g.u0, g.v0, g.u1, g.v1, drop });
                    }
                }
                const std::uint32_t colour = Packed(a_colour);
                for (const auto& g : glyphs) {
                    quads.push_back({ texture, g.x0, g.y0, g.x1, g.y1, g.u0, g.v0, g.u1, g.v1, colour });
                }
            }
        };

        // The four quarters of a crosshair or scope, the three after the first mirrored.
        void DrawQuarters(Frame& a_frame, std::uint32_t a_texture, const std::array<Quarter, 4>& a_quarters)
        {
            if (a_texture == 0) {
                return;
            }
            for (const auto& q : a_quarters) {
                a_frame.quads.push_back({ a_texture, q.rect.x0, q.rect.y0, q.rect.x1, q.rect.y1, q.flipU ? 1.0f : 0.0f, q.flipV ? 1.0f : 0.0f,
                    q.flipU ? 0.0f : 1.0f, q.flipV ? 0.0f : 1.0f, 0xFFFFFFFF });
            }
        }

        void SetAim(const Screen& a_screen, float a_x, float a_y)
        {
            if (a_screen.width > 0.0f && a_screen.height > 0.0f) {
                state.aimShown = true;
                state.aimX = a_x / a_screen.width;
                state.aimY = a_y / a_screen.height;
                state.aimAspect = a_screen.width / a_screen.height;
            }
        }

        // What San Andreas's camera puts over the picture for the weapon (CHud::DrawCrossHairs): the sniper rifle's
        // scope with the rest of the screen black, the rocket launchers' sight, the camera's viewfinder, the first
        // person rifle's corners and the circle round a gun's aim.
        void DrawScope(Frame& a_frame)
        {
            const auto& w = state.weapon;
            const auto& screen = a_frame.screen;
            switch (w.scope) {
            case Weapons::Scope::Sniper: {
                const auto quarters = SniperScope(screen);
                a_frame.Box({ 0.0f, 0.0f, quarters[0].rect.x0, screen.height }, kBlack);
                a_frame.Box({ quarters[1].rect.x1, 0.0f, screen.width, screen.height }, kBlack);
                a_frame.Box({ quarters[0].rect.x0, 0.0f, quarters[1].rect.x1, quarters[0].rect.y0 }, kBlack);
                a_frame.Box({ quarters[0].rect.x0, quarters[2].rect.y1, quarters[1].rect.x1, screen.height }, kBlack);
                DrawQuarters(a_frame, state.sniperCrosshair, quarters);
                break;
            }
            case Weapons::Scope::Rocket: DrawQuarters(a_frame, state.siteRocket, RocketSight(screen)); break;
            case Weapons::Scope::Camera: DrawQuarters(a_frame, state.cameraCrosshair, CameraFinder(screen)); break;
            case Weapons::Scope::Rifle: DrawQuarters(a_frame, state.siteM16, AimSight(screen, 0.5f, 0.5f, 1.0f)); break;
            case Weapons::Scope::Aim:
                if (w.gun) {
                    const auto* camera = RE::Main::WorldRootCamera();
                    const auto& frustum = camera ? camera->GetRuntimeData2().viewFrustum : RE::NiFrustum{};
                    if (frustum.fRight > 0.01f && frustum.fTop > 0.01f && w.fov > 1.0f) {
                        const auto [x, y] = AimPoint(screen, w.crosshair[0], w.crosshair[1], w.fov, frustum.fRight, frustum.fTop);
                        DrawQuarters(a_frame, state.siteM16, AimSightAt(screen, x, y, w.aimRadius));
                        SetAim(screen, x, y);
                    } else {
                        DrawQuarters(a_frame, state.siteM16, AimSight(screen, w.crosshair[0], w.crosshair[1], w.aimRadius));
                        SetAim(screen, w.crosshair[0] * screen.width, w.crosshair[1] * screen.height);
                    }
                }
                break;
            default: break;
            }
        }

        // The heat seeker's marker round its target (CWeaponEffects::MarkTarget's): San Andreas's target256 over
        // where the target shows through Skyrim's camera, kHsMarkerSize across there (no smaller than the rocket
        // sight's quarter), white and see-through while it locks on, red once locked.
        void DrawMark(Frame& a_frame)
        {
            auto* camera = RE::Main::WorldRootCamera();
            if (!state.mark || !state.target256 || !camera) {
                return;
            }
            const auto& screen = a_frame.screen;
            float       x = 0.0f, y = 0.0f, z = 0.0f, topX = 0.0f, topY = 0.0f;
            const RE::NiPoint3 top = state.markAt + RE::NiPoint3(0.0f, 0.0f, 0.5f * Explosives::kHsMarkerSize * Explosives::kMeter);
            if (!camera->WorldPtToScreenPt3(state.markAt, x, y, z, 1e-5f) || !camera->WorldPtToScreenPt3(top, topX, topY, z, 1e-5f)) {
                return;
            }
            const float cx = x * screen.width, cy = (1.0f - y) * screen.height;
            const float half = (std::max)(std::abs(topY - y) * screen.height, 24.0f * screen.unitY);
            const std::uint32_t colour = state.markLocked ? 0xFF0000FF : 0x64FFFFFF;  // red; white at San Andreas's alpha 100
            a_frame.quads.push_back({ state.target256, cx - half, cy - half, cx + half, cy + half, 0.0f, 0.0f, 1.0f, 1.0f, colour });
        }

        // The lock-on's marker over the target's head: Melee::MarkerImage, kMarkerGap over it and kMarkerHeight tall
        // where it shows through Skyrim's camera (no smaller than a few pixels far off), tinted by its health.
        void DrawHead(Frame& a_frame)
        {
            auto* camera = RE::Main::WorldRootCamera();
            if (!state.head || !state.triangle || !camera) {
                return;
            }
            const auto&        screen = a_frame.screen;
            const RE::NiPoint3 base = state.headAt + RE::NiPoint3(0.0f, 0.0f, Melee::kMarkerGap * RunOver::kMeter);
            const RE::NiPoint3 tip = base + RE::NiPoint3(0.0f, 0.0f, Melee::kMarkerHeight * RunOver::kMeter);
            float              x = 0.0f, y = 0.0f, z = 0.0f, tipX = 0.0f, tipY = 0.0f, tipZ = 0.0f;
            if (!camera->WorldPtToScreenPt3(base, x, y, z, 1e-5f) || !camera->WorldPtToScreenPt3(tip, tipX, tipY, tipZ, 1e-5f)) {
                return;
            }
            const float cx = x * screen.width, bottom = (1.0f - y) * screen.height;
            const float height = (std::max)(std::abs(tipY - y) * screen.height, 6.0f * screen.unitY);
            const float half = 0.5f * height * Melee::kMarkerWidth / Melee::kMarkerHeight;
            a_frame.quads.push_back({ state.triangle, cx - half, bottom - height, cx + half, bottom, 0.0f, 0.0f, 1.0f, 1.0f, state.headColour });
        }

        // The top right HUD, from the Skyrim player.
        void DrawHud(Frame& a_frame, RE::PlayerCharacter* a_player)
        {
            const auto icon = WeaponIcon(a_frame.screen);
            const auto weaponIcon = state.weapon.valid && state.weapon.type < state.icons.size() && state.icons[state.weapon.type] ? state.icons[state.weapon.type] : kFist;
            a_frame.quads.push_back({ weaponIcon, icon.x0, icon.y0, icon.x1, icon.y1, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF });
            if (!state.weapon.ammo.empty()) {
                a_frame.Text(kAmmo, state.weapon.ammo, kAmmo.colour);
            }

            auto* calendar = RE::Calendar::GetSingleton();
            a_frame.Text(kClock, ClockText(calendar ? calendar->GetHour() : 0.0f), kClock.colour);

            // San Andreas hides the armour bar at 0.
            if (state.armor > 0.0f) {
                a_frame.Bar(ArmourBar(a_frame.screen), state.armor, kLightGray);
            }
            const float health = a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            const float maxHealth = a_player->GetActorValueMax(RE::ActorValue::kHealth);
            const float fraction = maxHealth > 0.0f ? health / maxHealth : 0.0f;
            // Under a tenth San Andreas flashes it, off every 8th frame.
            if (fraction >= 0.1f || (state.frame / 8) % 2 == 0) {
                a_frame.Bar(HealthBar(a_frame.screen), fraction, kRed);
            }

            const std::int64_t gold = state.money.Tick(a_player->GetGoldAmount(), state.delta, true);
            a_frame.Text(kMoney, MoneyText(gold), MoneyColour(gold));

            if (state.wanted > 0) {
                // Flashing, the gold ones go black every other 8 frames, as the health bar's flash.
                const bool lit = !state.wantedFlashing || (state.frame / 8) % 2 == 0;
                for (int i = 0; i < kStarCount; i++) {
                    a_frame.Text(kWantedStar, "]", i < state.wanted && lit ? kGold : kBlack, StarRight(a_frame.screen, i));
                }
            }
        }

        // San Andreas's standard menu (CMenuSystem::DisplayStandardMenu): Ammu-Nation's.
        void DrawMenu(Frame& a_frame)
        {
            if (state.menuRows.empty()) {
                return;
            }
            a_frame.Box(MenuWindow(a_frame.screen, state.menuRows.size()), kMenuWindow);
            a_frame.Text(kMenuTitle, state.menuTitle, kMenuTitle.colour);
            for (std::size_t i = 0; i < state.menuRows.size(); i++) {
                const auto& row = state.menuRows[i];
                const auto  colour = MenuRowColour(row.highlighted, row.available, row.owned);
                a_frame.Text(HudLayout::MenuRow(i, false), row.name, colour);
                a_frame.Text(HudLayout::MenuRow(i, true), row.price, colour);
            }
        }

        void DrawText(Frame& a_frame)
        {
            // The cutscene bars under the text, so subtitles show on the bottom one.
            if (state.letterbox > 0.0f) {
                auto top = LetterboxTop(a_frame.screen);
                auto bottom = LetterboxBottom(a_frame.screen);
                top.y1 *= state.letterbox;
                bottom.y0 = a_frame.screen.height - (a_frame.screen.height - bottom.y0) * state.letterbox;
                a_frame.Box(top, kBlack);
                a_frame.Box(bottom, kBlack);
            }
            if (const float alpha = state.help.Alpha(); alpha > 0.0f) {
                a_frame.Box(HelpBox(state.values, a_frame.screen, state.help.text), WithAlpha(kHelpBox, alpha));
                a_frame.Text(kHelp, state.help.text, WithAlpha(kHelp.colour, alpha));
            } else if (const float shown = state.prompt.Alpha(); shown > 0.0f) {
                a_frame.Box(HelpBox(state.values, a_frame.screen, state.prompt.text), WithAlpha(kHelpBox, shown));
                a_frame.Text(kHelp, state.prompt.text, WithAlpha(kHelp.colour, shown));
            }
            if (const float alpha = state.subtitle.Alpha(); alpha > 0.0f) {
                a_frame.Text(kSubtitles, state.subtitle.text, WithAlpha(kSubtitles.colour, alpha));
            }
            if (const float alpha = state.missionTitle.Alpha(); alpha > 0.0f) {
                a_frame.Text(kMissionTitle, state.missionTitle.text, WithAlpha(kMissionTitle.colour, alpha));
            }
            if (const float alpha = state.radio.Alpha(); alpha > 0.0f && state.drawRadio) {
                a_frame.Text(kRadioName, state.radio.text, WithAlpha(state.radioRetuning ? kDarkGray : kRadioName.colour, alpha));
            }
            const float big = state.big.Alpha();
            if (big > 0.0f && state.bigUnderFade) {
                a_frame.Text(*state.bigStyle, state.big.text, WithAlpha(state.bigStyle->colour, big));
            }
            if (state.fade > 0.0f) {
                a_frame.Box({ 0.0f, 0.0f, a_frame.screen.width, a_frame.screen.height }, WithAlpha(kBlack, state.fade));
            }
            // "mission passed!" shows over the fade; "wasted" and "busted" fade to black with the screen, as in San Andreas.
            if (big > 0.0f && !state.bigUnderFade) {
                a_frame.Text(*state.bigStyle, state.big.text, WithAlpha(state.bigStyle->colour, big));
            }
        }

        // Skyrim's compass, attribute bars and crosshair hidden (each frame, as Skyrim's HUD modes show
        // them again), and given back as they were once San Andreas lets go.
        void HideSkyrimHud(bool a_hide)
        {
            if (!a_hide && !state.skyrimHudHidden) {
                return;
            }
            const bool first = a_hide && !state.skyrimHudHidden;
            state.skyrimHudHidden = a_hide;
            SKSE::GetTaskInterface()->AddUITask([a_hide, first]() {
                static bool wasVisible[std::size(kSkyrimHud)] = {};
                static bool logged = false;
                auto*       ui = RE::UI::GetSingleton();
                auto        menu = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
                auto*       movie = menu ? menu->uiMovie.get() : nullptr;
                if (!movie) {
                    return;
                }
                for (std::size_t i = 0; i < std::size(kSkyrimHud); i++) {
                    RE::GFxValue clip;
                    const auto   path = std::string("_root.HUDMovieBaseInstance.") + kSkyrimHud[i];
                    if (!movie->GetVariable(&clip, path.c_str()) || !clip.IsDisplayObject()) {
                        if (!logged) {
                            SKSE::log::warn("sa hud: Skyrim's HUD clip {} was not found", path);
                        }
                        continue;
                    }
                    if (first) {
                        RE::GFxValue visible;
                        wasVisible[i] = !clip.GetMember("_visible", &visible) || !visible.IsBool() || visible.GetBool();
                    }
                    clip.SetMember("_visible", RE::GFxValue(a_hide ? false : wasVisible[i]));
                }
                logged = true;
            });
        }

        void TickFade(float a_delta)
        {
            if (state.fadeSeconds <= 0.0f) {
                state.fade = state.fadeTarget;
                return;
            }
            state.fadeTime = (std::min)(state.fadeTime + a_delta, state.fadeSeconds);
            state.fade = state.fadeFrom + (state.fadeTarget - state.fadeFrom) * (state.fadeTime / state.fadeSeconds);
        }
    }

    void Load()
    {
        auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
        if (!Overlay::Install(window ? window->swapChain : nullptr)) {
            SKSE::log::warn("sa hud: no swap chain to draw on, San Andreas's HUD stays off");
            return;
        }
        const std::filesystem::path sa = Ini::Value("SanAndreas", "Path");
        if (sa.empty()) {
            SKSE::log::warn("sa hud: no [SanAndreas] Path in {}, San Andreas's HUD stays off", Ini::kPath);
            return;
        }
        const auto dat = ReadFile(sa / "data" / "fonts.dat");
        if (!HudFont::Parse(std::string_view(reinterpret_cast<const char*>(dat.data()), dat.size()), state.values)) {
            SKSE::log::warn("sa hud: {} didn't read ({} bytes), San Andreas's HUD stays off", (sa / "data" / "fonts.dat").string(), dat.size());
            return;
        }
        std::uint32_t skipped = 0;
        const auto    fontsData = ReadFile(sa / "models" / "fonts.txd");
        const auto    fonts = Txd::Read(fontsData.data(), fontsData.size(), &skipped);
        const auto    hudData = ReadFile(sa / "models" / "hud.txd");
        const auto    hud = Txd::Read(hudData.data(), hudData.size(), &skipped);
        const auto*   font2 = Txd::Find(fonts, HudFont::kTextureNames[0]);
        const auto*   font1 = Txd::Find(fonts, HudFont::kTextureNames[1]);
        const auto*   fist = Txd::Find(hud, "fist");
        if (!font1 || !font2 || !fist) {
            SKSE::log::warn("sa hud: San Andreas's fonts or HUD sprites are missing ({} font textures, {} HUD textures, {} skipped), its HUD stays off",
                fonts.size(), hud.size(), skipped);
            return;
        }
        std::vector<Overlay::Image> images(3);
        images[kFont2 - 1] = ImageOf(font2);
        images[kFont1 - 1] = ImageOf(font1);
        images[kFist - 1] = ImageOf(fist);
        const auto add = [&](const Txd::Texture* a_texture) -> std::uint32_t {
            if (!a_texture) {
                return 0;
            }
            images.push_back(ImageOf(a_texture));
            return static_cast<std::uint32_t>(images.size());  // the overlay's ids count from 1
        };
        state.siteM16 = add(Txd::Find(hud, "siteM16"));
        state.siteRocket = add(Txd::Find(hud, "siterocket"));
        const auto particleData = ReadFile(sa / "models" / "particle.txd");
        state.target256 = add(Txd::Find(Txd::Read(particleData.data(), particleData.size(), &skipped), "target256"));
        {
            auto marker = Melee::MarkerImage();  // San Andreas draws it in vertex colours, so it has no sprite to read
            images.push_back({ marker.width, marker.height, std::move(marker.rgba) });
            state.triangle = static_cast<std::uint32_t>(images.size());
        }

        // The weapons' icons and scopes are in each weapon's own dictionary in gta3.img.
        std::ifstream      archive(sa / "models" / "gta3.img", std::ios::binary);
        const Img::Reader  read = [&archive](std::uint64_t a_offset, void* a_out, std::size_t a_size) {
            archive.clear();
            archive.seekg(static_cast<std::streamoff>(a_offset));
            return static_cast<bool>(archive.read(static_cast<char*>(a_out), static_cast<std::streamsize>(a_size)));
        };
        const auto directory = Img::ReadDirectory(read);
        int        icons = 0, missing = 0;
        if (directory) {
            for (std::uint8_t type = 1; type <= Weapons::kLastCarried; ++type) {
                if (!Weapons::Carried(type)) {
                    continue;
                }
                const auto  model = std::string(Weapons::ModelName(type));
                const auto* entry = Img::Find(*directory, model + ".txd");
                const auto  bytes = entry ? Img::ReadEntry(read, *entry) : std::vector<std::uint8_t>{};
                const auto  textures = Txd::Read(bytes.data(), bytes.size(), &skipped);
                if (const auto id = add(Txd::Find(textures, Weapons::IconName(type)))) {
                    state.icons[type] = id;
                    icons++;
                } else {
                    missing++;
                }
                if (const auto crosshair = Weapons::CrosshairName(type); !crosshair.empty()) {
                    (type == Weapons::SniperRifle ? state.sniperCrosshair : state.cameraCrosshair) = add(Txd::Find(textures, crosshair));
                }
            }
        }
        Overlay::SetImages(std::move(images));
        state.loaded = true;
        SKSE::log::info("sa hud: San Andreas's fonts and HUD sprites read from {} ({} and {} textures); {} weapon icons ({} missing), scopes: sniper {}, camera {}, rocket {}, rifle {}, lock-on marker {}{}",
            sa.string(), fonts.size(), hud.size(), icons, missing, state.sniperCrosshair != 0, state.cameraCrosshair != 0, state.siteRocket != 0, state.siteM16 != 0,
            state.target256 != 0,
            directory ? "" : "; gta3.img did not read, the weapon icon stays the fist");
    }

    void Update(float a_delta, bool a_owned, bool a_menu)
    {
        HideSkyrimHud(a_owned);
        if (!state.loaded) {
            return;
        }
        state.frame++;
        state.delta = a_delta;
        for (auto* timed : { &state.missionTitle, &state.big, &state.help, &state.prompt, &state.subtitle, &state.radio }) {
            timed->Tick(a_delta);
        }
        state.drawRadio = a_owned && !a_menu;
        TickFade(a_delta);
        const float slide = a_delta / 0.5f;
        state.letterbox = std::clamp(state.letterbox + (state.letterboxOn ? slide : -slide), 0.0f, 1.0f);

        Frame frame;
        float width = 0.0f, height = 0.0f;
        if (!Overlay::ScreenSize(width, height)) {
            return;
        }
        frame.screen = ScreenOf(width, height);
        if (!state.loggedScreen) {
            state.loggedScreen = true;
            SKSE::log::info("sa hud: screen {}x{}, {:.3f} x {:.3f} pixels per HUD unit", width, height, frame.screen.unitX, frame.screen.unitY);
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        state.aimShown = false;  // DrawScope sets it again while the gun's crosshair is up
        if (a_owned && !a_menu && player) {
            DrawHud(frame, player);
            DrawScope(frame);
            DrawMark(frame);
            DrawHead(frame);
            DrawMenu(frame);
        } else if (player) {
            state.money.Tick(player->GetGoldAmount(), a_delta, false);  // not shown: it comes back as it is
        }
        state.mark = false;
        state.head = false;
        state.menuRows.clear();
        DrawText(frame);
        Overlay::Submit(std::move(frame.quads));
    }

    void SetWeapon(const Protocol::PlayerState* a_pose)
    {
        auto& w = state.weapon;
        w = {};
        if (!a_pose) {
            return;
        }
        w.valid = true;
        w.type = a_pose->weaponType;
        w.scope = Weapons::ScopeOf(a_pose->cameraMode);
        w.aiming = (a_pose->weaponFlags & Protocol::kWeaponAiming) != 0;
        w.gun = (w.type >= Weapons::Pistol && w.type <= Weapons::CountryRifle) || w.type == Weapons::Flamethrower || w.type == Weapons::Minigun;
        w.crosshair[0] = a_pose->crosshair[0];
        w.crosshair[1] = a_pose->crosshair[1];
        w.aimRadius = a_pose->aimRadius > 0 ? (std::min)(a_pose->aimRadius / 1000.0f, 4.0f) : 0.2f;
        if (Protocol::BodyState body{}; LinkService::Get().ReadBody(body) && std::isfinite(body.cameraFov) && body.cameraFov > 1.0f) {
            w.fov = body.cameraFov;
        }
        if (Weapons::CountsAmmo(w.type)) {
            const auto& entry = a_pose->loadout[(std::min)(static_cast<std::uint32_t>(a_pose->weaponSlot), Protocol::kWeaponSlotCount - 1)];
            w.ammo = Weapons::AmmoText(entry.ammo, entry.clip, a_pose->ammoStyle);
        }
    }

    void MarkTarget(const RE::NiPoint3& a_at, bool a_locked)
    {
        state.mark = true;
        state.markAt = a_at;
        state.markLocked = a_locked;
    }

    void MarkHead(const RE::NiPoint3& a_headTop, std::uint32_t a_colour)
    {
        state.head = true;
        state.headAt = a_headTop;
        state.headColour = a_colour;
    }

    bool AimCrosshair(float& a_x, float& a_y, float& a_aspect)
    {
        a_x = state.aimX;
        a_y = state.aimY;
        a_aspect = state.aimAspect;
        return state.aimShown;
    }

    void ShowMissionTitle(std::string a_text) { state.missionTitle.Show(std::move(a_text), 4.0f); }

    void ShowBigMessage(BigMessage a_message, float a_seconds)
    {
        state.bigStyle = a_message == BigMessage::MissionPassed ? &kMissionPassed : &kBigMessage;
        state.bigUnderFade = a_message != BigMessage::MissionPassed;
        state.big.Show(a_message == BigMessage::Wasted ? "Wasted" : a_message == BigMessage::Busted ? "Busted" : "mission passed!", a_seconds);  // as San Andreas's GXT has them
        state.big.fadeIn = 0.65f;  // San Andreas brings "wasted" up over about two thirds of a second
    }

    void HideBigMessage() { state.big.hold = 0.0f; }

    void ShowHelp(std::string a_text, float a_seconds) { state.help.Show(std::move(a_text), a_seconds); }

    void ShowPrompt(std::string a_text)
    {
        if (a_text.empty()) {
            return;  // the one up fades out on its own
        }
        const bool same = state.prompt.hold > 0.0f && state.prompt.text == a_text;
        const float age = state.prompt.age;
        state.prompt.Show(std::move(a_text), 0.1f);
        if (same) {
            state.prompt.age = (std::min)(age, state.prompt.fadeIn);  // already up: stays, without fading in again
        }
    }

    void ShowSubtitle(std::string a_text, float a_seconds) { state.subtitle.Show(std::move(a_text), a_seconds); }

    void HoldSubtitle(std::string a_text)
    {
        if (a_text.empty()) {
            return;  // the one up fades out on its own
        }
        const bool  same = state.subtitle.hold > 0.0f && state.subtitle.text == a_text;
        const float age = state.subtitle.age;
        state.subtitle.Show(std::move(a_text), 0.1f);
        if (same) {
            state.subtitle.age = (std::min)(age, state.subtitle.fadeIn);  // already up: stays, without fading in again
        }
    }

    void HideSubtitle() { state.subtitle.hold = 0.0f; }

    void ShowRadioStation(std::string a_name, bool a_retuning)
    {
        const bool same = state.radio.hold > 0.0f && state.radio.text == a_name;
        state.radio.Show(std::move(a_name), kRadioNameSeconds);
        if (same) {
            state.radio.age = state.radio.fadeIn;  // already up: stays at full, only the colour changes
        }
        state.radioRetuning = a_retuning;
    }

    void ShowMenu(std::string a_title, std::vector<MenuRow> a_rows)
    {
        state.menuTitle = std::move(a_title);
        state.menuRows = std::move(a_rows);
    }

    void FadeTo(float a_black, float a_seconds)
    {
        state.fadeFrom = state.fade;
        state.fadeTarget = std::clamp(a_black, 0.0f, 1.0f);
        state.fadeTime = 0.0f;
        state.fadeSeconds = (std::max)(a_seconds, 0.0f);
    }

    void SetLetterbox(bool a_on) { state.letterboxOn = a_on; }

    void SetWantedLevel(int a_stars) { state.wanted = std::clamp(a_stars, 0, kStarCount); }

    void SetWantedFlashing(bool a_flashing) { state.wantedFlashing = a_flashing; }

    void SetArmor(float a_fraction) { state.armor = std::clamp(a_fraction, 0.0f, 1.0f); }


    void ClearText()
    {
        for (auto* timed : { &state.missionTitle, &state.big, &state.help, &state.prompt, &state.subtitle, &state.radio }) {
            timed->hold = 0.0f;
        }
        FadeTo(0.0f, 0.0f);
        state.letterboxOn = false;
    }

    void NextDemo()
    {
        static constexpr const char* kSteps[] = { "mission title", "wasted", "busted", "mission passed", "help box", "subtitles", "fade to black",
            "letterbox", "wanted stars", "radio station", "off" };
        const int step = state.demo;
        state.demo = (state.demo + 1) % static_cast<int>(std::size(kSteps));
        ClearText();
        SetWantedLevel(0);
        switch (step) {
        case 0: ShowMissionTitle("Jarl Johnson"); break;
        case 1: ShowBigMessage(BigMessage::Wasted); break;
        case 2: ShowBigMessage(BigMessage::Busted); break;
        case 3: ShowBigMessage(BigMessage::MissionPassed); break;
        case 4: ShowHelp("Welcome to Los Solitude. Press F to get into a vehicle and F again to get out of it."); break;
        case 5: ShowSubtitle("Ah shit, here we go again."); break;
        case 6: FadeTo(1.0f, 1.0f); break;
        case 7: SetLetterbox(true); ShowSubtitle("Thane of Grove Street.", 6.0f); break;
        case 8: SetWantedLevel(3); break;
        case 9: ShowRadioStation("Radio Sovngarde", false); break;
        default: break;
        }
        SKSE::log::info("sa hud: demo {} ({})", step + 1, kSteps[step]);
    }
}
