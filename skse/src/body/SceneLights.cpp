#include "body/SceneLights.h"

#include <unordered_set>

namespace AhShitRim::SceneLights
{
    namespace
    {
        std::unordered_set<const RE::NiLight*> added;  // added once at least: any later add is one Skyrim dropped
        int                                    logged = 0;  // the adds logged, a few dozen at most

        template <class T>
        bool Among(const RE::BSTArray<RE::NiPointer<T>>& a_lights, const RE::NiLight* a_light)
        {
            for (const auto& light : a_lights) {
                if (light && light->light.get() == a_light) {
                    return true;
                }
            }
            return false;
        }
    }

    bool Keep(RE::NiPointLight* a_light)
    {
        auto* shadowScene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
        if (!a_light || !shadowScene) {
            return false;
        }
        auto& data = shadowScene->GetRuntimeData();
        if (Among(data.activeLights, a_light) || Among(data.activeShadowLights, a_light)) {
            return true;
        }
        {
            RE::BSSpinLockGuard guard(data.lightQueueLock);
            if (Among(data.lightQueueAdd, a_light)) {
                return true;  // added already: the renderer takes it in on its next frame
            }
        }
        // Every kind of surface lit, not the convenience overload's defaults (which leave the land out).
        RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
        params.dynamic = true;
        params.shadowLight = false;
        params.portalStrict = false;
        params.affectLand = true;
        params.affectWater = true;
        params.neverFades = true;
        params.falloff = 1.0f;
        shadowScene->AddLight(a_light, params);
        const bool again = !added.insert(a_light).second;
        if (logged++ < 40) {
            SKSE::log::info("scene lights: {} {} the renderer's lights", a_light->name.c_str(),
                again ? "added back to (dropped by a cell change)" : "added to");
        }
        return true;
    }
}
