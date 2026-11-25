// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "GPU_OpenGL.h"
#include "NDS.h"

#include "common/logging/log.h"
#include "frame_cores/ds_gl/ds_gl_vk.h"
#include "frame_cores/ds_gl/nds_gl_captures.h"
#include "frame_cores/nds_gl_stereo.h"
#include "frame_cores/nds_resolution.h"
#include "video_core/renderer_vulkan/alt_graphics.h"

namespace FrameCores {

namespace {

// The textures of melonDS's renderer that hold the display captures, private to it.
GLuint Captures256(const melonDS::GLRenderer& renderer);
GLuint Captures128(const melonDS::GLRenderer& renderer);
template <auto Wide, auto Small>
struct CapturesOf {
    friend GLuint Captures256(const melonDS::GLRenderer& renderer) {
        return renderer.*Wide;
    }
    friend GLuint Captures128(const melonDS::GLRenderer& renderer) {
        return renderer.*Small;
    }
};
template struct CapturesOf<&melonDS::GLRenderer::CaptureOutput256Tex,
                           &melonDS::GLRenderer::CaptureOutput128Tex>;

void LogLayer(int level, const char* text) {
    switch (level) {
    case 0:
        LOG_ERROR(Render_Vulkan, "ds_gl: {}", text);
        break;
    case 1:
        LOG_WARNING(Render_Vulkan, "ds_gl: {}", text);
        break;
    default:
        LOG_DEBUG(Render_Vulkan, "ds_gl: {}", text);
        break;
    }
}

struct DestroyContext {
    void operator()(DsGl::Context* context) const {
        DsGl::Destroy(context);
    }
};
using ContextPtr = std::unique_ptr<DsGl::Context, DestroyContext>;

/// The context: before melonDS's renderer among the bases, which draws with it to its end.
struct HasContext {
    explicit HasContext(ContextPtr context_) : context{std::move(context_)} {}
    ContextPtr context;
};

/// melonDS's own hi-res renderer, drawing on Vulkan.
class NdsGlRenderer final : private HasContext, public StereoGl, public NdsHires {
public:
    NdsGlRenderer(melonDS::NDS& nds, ContextPtr context_)
        : HasContext{std::move(context_)}, StereoGl{nds} {}

    bool Init() override {
        // A context that failed makes no shaders or textures, and says so here alone.
        return StereoGl::Init() && DsGl::Healthy();
    }

    bool SetScale(uint32_t scale) override {
        alive.reset(); // the screens' images are made again
        eyes_alive = {};
        melonDS::RendererSettings settings{};
        settings.ScaleFactor = static_cast<int>(scale);
        SetRenderSettings(settings);
        if (!std::exchange(has_captures, true)) {
            const melonDS::GPU& gpu = GPU;
            DsGl::LoadCaptures({gpu.VRAM_A, gpu.VRAM_B, gpu.VRAM_C, gpu.VRAM_D}, Captures256(*this),
                               Captures128(*this), settings.ScaleFactor);
        }
        return DsGl::Healthy();
    }

    bool SetDepth(const NdsDepth::Depth& depth) override {
        if (depth.on != HasEyes()) {
            eyes_alive = {};
        }
        return StereoGl::SetDepth(depth) && DsGl::Healthy();
    }

    bool GetEyes(int screen, Vulkan::AltScreenImage& left, Vulkan::AltScreenImage& right) override {
        std::array<DsGl::Texture, 2> textures; // by Eye
        for (const NdsDepth::Eye eye : {NdsDepth::Eye::Left, NdsDepth::Eye::Right}) {
            DsGl::Texture& texture = textures[static_cast<std::size_t>(eye)];
            const GLuint name = Picture(eye, screen);
            if (name == 0 || !DsGl::GetTexture(name, texture) || texture.layers < 2) {
                return false;
            }
        }
        for (auto& alive_eye : eyes_alive) {
            if (!alive_eye) {
                alive_eye = std::make_shared<char>();
            }
        }
        Show(left, textures[0], static_cast<uint32_t>(screen), eyes_alive[0]);
        Show(right, textures[1], static_cast<uint32_t>(screen), eyes_alive[1]);
        return true;
    }

    bool ShowsDepth(int screen) const override {
        return StereoGl::ShowsDepth(screen);
    }

    int EyesShift() const override {
        return StereoGl::Shift();
    }

    bool GetScreens(Vulkan::AltScreenImage& top, Vulkan::AltScreenImage& bottom) override {
        DsGl::Flush(false);
        DsGl::Texture texture;
        if (!DsGl::Healthy() || !DsGl::GetTexture(Output(), texture) || texture.layers < 2) {
            return false;
        }
        alive = std::make_shared<char>(); // of this image
        eyes_alive = {};
        Show(top, texture, 0, alive);
        Show(bottom, texture, 1, alive);
        return true;
    }

    bool ReadScreens(Vulkan::AltScreenImage& top, Vulkan::AltScreenImage& bottom) override {
        bool ok = true;
        for (uint32_t i = 0; i < 2; i++) {
            Vulkan::AltScreenImage& out = i == 0 ? top : bottom;
            if (out.image == 0 || out.pixels) {
                continue; // nothing to read, or read already
            }
            if (!DsGl::ReadTexture(Output(), i, pixels[i]) ||
                pixels[i].size() != std::size_t{out.width} * out.height) {
                ok = false;
                continue;
            }
            out.pixels = pixels[i].data();
        }
        return ok;
    }

private:
    static void Show(Vulkan::AltScreenImage& out, const DsGl::Texture& texture, uint32_t layer,
                     const std::shared_ptr<char>& alive) {
        out = {};
        out.width = texture.width;
        out.height = texture.height;
        out.stride_pixels = texture.width;
        out.bgra = false;
        out.image = reinterpret_cast<uint64_t>(texture.image);
        out.layer = layer;
        out.alive = alive;
    }

    uint32_t Output() {
        void* top = nullptr;
        void* bottom = nullptr;
        GetFramebuffers(&top, &bottom); // melonDS gives its texture's name
        return *static_cast<const GLuint*>(top);
    }

    bool has_captures = false; ///< those of before it was the console's renderer
    std::shared_ptr<char> alive;
    std::array<std::shared_ptr<char>, 2> eyes_alive; ///< by Eye
    std::array<std::vector<uint32_t>, 2> pixels;
};

} // namespace

NdsHiresMaker VulkanNdsHires(const Vulkan::AltGraphics& graphics) {
    return [graphics](melonDS::NDS& nds, std::string& why) -> std::unique_ptr<NdsHiresRenderer> {
        DsGl::SetLog(LogLayer);
        const DsGl::Device device{graphics.physical, graphics.device, graphics.queue,
                                  graphics.queue_family, graphics.queue_mutex};
        ContextPtr context{DsGl::Create(device, why)};
        if (!context) {
            return nullptr;
        }
        auto renderer = std::make_unique<NdsGlRenderer>(nds, std::move(context));
        auto made = std::make_unique<NdsHiresRenderer>();
        made->control = renderer.get();
        made->renderer = std::move(renderer);
        return made;
    };
}

} // namespace FrameCores
