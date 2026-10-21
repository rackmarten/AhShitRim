#include "body/MuzzleFlash.h"

#include "body/ModelView.h"
#include "flash_model.h"

namespace AhShitRim::MuzzleFlash
{
    namespace
    {
        constexpr float         kSeconds = 0.07f;  // two San Andreas frames
        constexpr float         kLightRadius = 320.0f;  // Skyrim units, about 4.5 m
        constexpr std::uint32_t kModelId = 0xF1A50000;  // + 2 per hand: even, so never an id San Andreas sends

        struct State
        {
            bool                      light = true;
            bool                      lightFailed = false;
            bool                      modelFailed = false;
            bool                      lit = false;  // a flash was shown this frame
            float                     left[Protocol::kWeaponHandMax] = {};
            RE::NiPointer<RE::NiPointLight> pointLight;
            std::uint32_t             frame = 0;
        } state;

        bool EnsureModel(std::uint32_t a_hand)
        {
            const std::uint32_t id = kModelId + a_hand * 2;
            if (ModelView::Has(id)) {
                return true;
            }
            if (state.modelFailed) {
                return false;
            }
            const auto blob = SkinModel::Serialize(FlashModel::Build());
            if (!ModelView::AddModel(id, blob)) {
                state.modelFailed = true;
                SKSE::log::warn("muzzle flash: the flash model did not build, no flash will show");
                return false;
            }
            return true;
        }

        RE::NiNode* Scene()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* root = player ? player->Get3D() : nullptr;
            return root ? root->parent : nullptr;
        }

        bool EnsureLight()
        {
            if (state.pointLight) {
                return true;
            }
            if (!state.light || state.lightFailed) {
                return false;
            }
            auto* scene = Scene();
            auto* shadowScene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
            if (!scene || !shadowScene) {
                return false;  // not up yet: tried again next shot
            }
            auto* light = RE::NiPointLight::Create();
            if (!light) {
                state.lightFailed = true;
                SKSE::log::warn("muzzle flash: no point light could be made, shots won't light the scene");
                return false;
            }
            light->name = "AhShitRimMuzzleLight";
            auto& data = light->GetLightRuntimeData();
            data.ambient = RE::NiColor(0.0f, 0.0f, 0.0f);
            data.diffuse = RE::NiColor(1.0f, 0.72f, 0.35f);
            data.radius = RE::NiPoint3(kLightRadius, kLightRadius, kLightRadius);
            data.fade = 0.0f;
            light->SetLightAttenuation(kLightRadius);
            state.pointLight.reset(light);
            scene->AttachChild(light, true);
            // Every kind of surface lit, not the convenience overload's defaults (which left the land out).
            RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
            params.dynamic = true;
            params.shadowLight = false;
            params.portalStrict = false;
            params.affectLand = true;
            params.affectWater = true;
            params.neverFades = true;
            params.falloff = 1.0f;
            shadowScene->AddLight(light, params);
            SKSE::log::info("muzzle flash: a point light of radius {:.0f} added to the scene", kLightRadius);
            return true;
        }
    }

    void Configure(bool a_light)
    {
        state.light = a_light;
        SKSE::log::info("muzzle flash: shots {} the scene ([Weapons] MuzzleLight)", a_light ? "light" : "don't light");
    }

    void Fire(std::uint32_t a_hand)
    {
        if (a_hand < Protocol::kWeaponHandMax) {
            state.left[a_hand] = kSeconds;
        }
    }

    void Show(std::uint32_t a_hand, const RE::NiPoint3& a_feet, const float (&a_muzzle)[3], const float (&a_rotation)[4], float a_delta)
    {
        if (a_hand >= Protocol::kWeaponHandMax || state.left[a_hand] <= 0.0f || !EnsureModel(a_hand)) {
            return;
        }
        const float strength = (std::min)(1.0f, state.left[a_hand] / kSeconds);
        state.left[a_hand] -= a_delta;

        Protocol::BodyState flash{};
        flash.frame = ++state.frame;
        flash.flags = Protocol::kBodyValid;
        flash.model = kModelId + a_hand * 2;
        flash.boneCount = 1;
        std::copy(std::begin(a_rotation), std::end(a_rotation), flash.bones[0].rotation);
        std::copy(std::begin(a_muzzle), std::end(a_muzzle), flash.bones[0].translation);
        flash.cameraForward[1] = 1.0f;
        if (!ModelView::Show(flash, a_feet)) {
            return;
        }
        if (EnsureLight()) {
            state.lit = true;
            auto* light = state.pointLight.get();
            if (auto* scene = Scene(); scene && light->parent != scene) {  // the player's 3D was rebuilt (a load door)
                if (light->parent) {
                    light->parent->DetachChild(light);
                }
                scene->AttachChild(light, true);
            }
            const RE::NiPoint3 at = a_feet + RE::NiPoint3(a_muzzle[0], a_muzzle[1], a_muzzle[2]);
            light->local.translate = at;
            light->GetLightRuntimeData().fade = strength;
            RE::NiUpdateData update{};
            light->Update(update);
        }
    }

    void EndFrame()
    {
        if (!state.lit && state.pointLight) {
            state.pointLight->GetLightRuntimeData().fade = 0.0f;
        }
        state.lit = false;
    }

    void Reset()
    {
        for (auto& left : state.left) {
            left = 0.0f;
        }
        EndFrame();
    }
}
