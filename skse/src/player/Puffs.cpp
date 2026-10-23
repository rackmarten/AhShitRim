#include "player/Puffs.h"

#include "Ini.h"
#include "body/ModelView.h"
#include "puff_model.h"
#include "txd.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>

namespace AhShitRim::Puffs
{
    namespace
    {
        constexpr std::uint32_t kModelBase = 0xB5B00000;  // + 2 per prim: even, so never an id San Andreas sends
        constexpr double        kPuffGap = 0.15;          // seconds without a puff before a cloud stops coming out

        struct Stream
        {
            const PuffModel::Prim* prim;
            PuffModel::Cloud       cloud;
            Kind                   kind;
            std::uint32_t          frame = 0;
            bool                   failed = false;
        };

        struct State
        {
            Stream streams[4] = {
                { &PuffModel::kSprayCloud, PuffModel::Cloud(PuffModel::kSprayCloud), Kind::Spray },
                { &PuffModel::kHoseFroosh, PuffModel::Cloud(PuffModel::kHoseFroosh), Kind::Hose },
                { &PuffModel::kHoseChunks, PuffModel::Cloud(PuffModel::kHoseChunks), Kind::Hose },
                { &PuffModel::kPutOutPuff, PuffModel::Cloud(PuffModel::kPutOutPuff), Kind::Hose },
            };
            double                                                      last[2] = { -1e9, -1e9 };  // when each kind last puffed
            PuffModel::Vec3                                             nozzle[2];
            PuffModel::Vec3                                             dir[2];
            bool                                                        read = false;
            std::map<std::string, std::optional<SkinModel::TextureIn>> textures;
            std::vector<Txd::Texture>                                   txd;
        } state;

        constexpr int kPutOutStream = 3;

        double NowSeconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        PuffModel::Vec3 V(const RE::NiPoint3& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        // A sprite from San Andreas's effectsPC.txd, read once from the player's own install (never written anywhere).
        const SkinModel::TextureIn* Texture(const char* a_name)
        {
            if (!state.read) {
                state.read = true;
                const std::filesystem::path     sa = Ini::Value("SanAndreas", "Path");
                const auto                      path = sa / "models" / "effectsPC.txd";
                std::ifstream                   file(path, std::ios::binary);
                const std::vector<std::uint8_t> data = file ? std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {}) : std::vector<std::uint8_t>{};
                state.txd = Txd::Read(data.data(), data.size());
                SKSE::log::info("puffs: {} textures in {}", state.txd.size(), path.string());
            }
            auto& slot = state.textures[a_name];
            if (!slot) {
                if (const auto* found = Txd::Find(state.txd, a_name)) {
                    slot = SkinModel::TextureIn{ found->width, found->height, found->rgba };
                } else {
                    SKSE::log::warn("puffs: no {} in San Andreas's effectsPC.txd, its cloud won't show", a_name);
                    return nullptr;
                }
            }
            return &*slot;
        }

        bool Ensure(Stream& a_stream, std::uint32_t a_id)
        {
            if (ModelView::Has(a_id)) {
                return true;
            }
            if (a_stream.failed) {
                return false;
            }
            const auto* sprite = Texture(a_stream.prim->texture);
            if (!sprite || !ModelView::AddModel(a_id, SkinModel::Serialize(PuffModel::Build(*a_stream.prim, *sprite)))) {
                a_stream.failed = true;
                SKSE::log::warn("puffs: the {} model did not build, it won't show", a_stream.prim->name);
                return false;
            }
            SKSE::log::info("puffs: the {} model built ({} sprites of San Andreas's {})", a_stream.prim->name, PuffModel::kBones, a_stream.prim->texture);
            return true;
        }

        void Draw(Stream& a_stream, std::uint32_t a_id)
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            auto* root = camera ? camera->cameraRoot.get() : nullptr;
            const auto& sprites = a_stream.cloud.Sprites();
            if (!root || sprites.empty() || !Ensure(a_stream, a_id)) {
                return;
            }
            const auto&         eye = root->world.translate;
            const auto&         middle = sprites[sprites.size() / 2].at;
            Protocol::BodyState pose{};
            pose.frame = ++a_stream.frame;
            pose.flags = Protocol::kBodyValid;
            pose.model = a_id;
            pose.cameraForward[1] = 1.0f;
            a_stream.cloud.Pose(middle, { eye.x, eye.y, eye.z }, pose);
            ModelView::Show(pose, RE::NiPoint3(middle.x, middle.y, middle.z));
        }
    }

    void Puff(Kind a_kind, const RE::NiPoint3& a_nozzle, const RE::NiPoint3& a_dir)
    {
        const int k = static_cast<int>(a_kind);
        state.last[k] = NowSeconds();
        state.nozzle[k] = V(a_nozzle);
        state.dir[k] = V(a_dir);
    }

    void PutOut(const RE::NiPoint3& a_at)
    {
        state.streams[kPutOutStream].cloud.Burst(V(a_at), { 0.0f, 0.0f, 1.0f }, PuffModel::kPutOutCount, PuffModel::kPutOutPuff.speed, PuffModel::kPutOutPuff.angle);
    }

    void Update(float a_delta)
    {
        const double now = NowSeconds();
        for (std::uint32_t i = 0; i < std::size(state.streams); ++i) {
            auto&      stream = state.streams[i];
            const int  k = static_cast<int>(stream.kind);
            const bool spraying = i != kPutOutStream && now - state.last[k] < kPuffGap;
            stream.cloud.Step(a_delta, spraying, state.nozzle[k], state.dir[k]);
            Draw(stream, kModelBase + i * 2);
        }
    }

    void Clear()
    {
        for (auto& stream : state.streams) {
            stream.cloud.Clear();
        }
        state.last[0] = state.last[1] = -1e9;
    }
}
