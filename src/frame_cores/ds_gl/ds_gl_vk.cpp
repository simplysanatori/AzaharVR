// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>

#include "ds_gl_decl.h"
#include "ds_gl_state.h"
#include "ds_gl_vk.h"

#define VKD VULKAN_HPP_DEFAULT_DISPATCHER

namespace DsGl {
namespace {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

void DefaultLog(int level, const char* text) {
    std::fprintf(stderr, "DsGl %s: %s\n",
                 level == 0   ? "error"
                 : level == 1 ? "warning"
                              : "note",
                 text);
}
void (*g_log)(int, const char*) = DefaultLog;

void Log(int level, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_log(level, buf);
}

constexpr VkDeviceSize kChunkSize = 4u << 20;
constexpr u32 kUnits = 16;
constexpr u32 kUboPoints = 36;
constexpr u32 kAttrs = 4;
constexpr u32 kMaxSamplerArray = 8;

struct Buf {
    std::vector<u8> data;
    u64 version = 1;
    // where the last copy for the GPU is
    u64 snap_version = 0;
    u64 snap_batch = 0;
    VkBuffer snap_buffer = VK_NULL_HANDLE;
    VkDeviceSize snap_offset = 0;
    void* mapped_for = nullptr;
};

struct Tex {
    GLenum target = 0;
    GLenum ifmt = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    u32 w = 0, h = 0, layers = 0;
    bool array = false;
    bool depth = false;
    bool stencil = false;
    bool swap_rb = false; ///< 1555 data as OpenGL packs it
    bool can_render = false;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    std::vector<VkImageView> layer_views;
    GLenum wrap_s = GL_REPEAT, wrap_t = GL_REPEAT;
    GLenum mag = GL_NEAREST;
};

struct Fbo {
    struct Att {
        GLuint tex = 0;
        int layer = 0;
    };
    Att color[2];
    Att ds;
};

struct Vao {
    struct Attr {
        bool enabled = false;
        GLuint buffer = 0;
        int size = 0;
        GLenum type = 0;
        bool integer = false;
        u32 stride = 0;
        u32 offset = 0;
    };
    Attr attr[kAttrs];
    GLuint elements = 0;
};

struct Shader {
    GLenum type = 0;
    std::string source;
};

struct Program {
    std::vector<GLuint> shaders;
    std::map<std::string, int> attrib_locs, frag_locs;
    bool linked = false;
    std::string log;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    struct Uniform {
        std::string name;
        bool sampler = false;
        u32 binding = 0; ///< index into `bindings`
        u32 offset = 0;
    };
    std::vector<Uniform> uniforms;
    struct Binding {
        u32 binding = 0;
        bool sampler = false;
        u32 count = 1;
        int gl_type = 0;
        std::array<int, kMaxSamplerArray> units{};
        std::string block;
        bool is_default = false;
        u32 point = 0;
        u32 size = 0;
    };
    std::vector<Binding> bindings;
    std::vector<int> blocks; ///< block index (glGetUniformBlockIndex) to `bindings`
    std::vector<u8> defaults;
    Buf defaults_buf;
    std::array<int, kAttrs> input_kind{}; ///< 0 none, 1 float, 2 int, 3 uint
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

struct State {
    bool depth_test = false, stencil_test = false, blend = false, scissor_test = false;
    GLenum depth_func = GL_LESS;
    bool depth_mask = true;
    GLenum stencil_func = GL_ALWAYS;
    int stencil_ref = 0;
    u32 stencil_cmp_mask = 0xFFFFFFFF;
    u32 stencil_write_mask = 0xFFFFFFFF;
    GLenum sfail = GL_KEEP, dpfail = GL_KEEP, dppass = GL_KEEP;
    GLenum blend_src_rgb = GL_ONE, blend_dst_rgb = GL_ZERO, blend_src_a = GL_ONE,
           blend_dst_a = GL_ZERO;
    GLenum blend_eq_rgb = GL_FUNC_ADD, blend_eq_a = GL_FUNC_ADD;
    float blend_color[4] = {0, 0, 0, 0};
    u8 color_mask[2] = {0xF, 0xF};
    int viewport[4] = {0, 0, 0, 0};
    int scissor[4] = {0, 0, 0, 0};
    float clear_color[4] = {0, 0, 0, 0};
    float clear_depth = 1.0f;
    GLuint program = 0;
    GLuint vao = 0;
    GLuint draw_fbo = 0, read_fbo = 0;
    GLuint array_buffer = 0, uniform_buffer = 0;
    u32 active_unit = 0;
    GLuint units[kUnits][2] = {}; ///< [unit][0: 2D, 1: 2D array]
    GLuint ubo_points[kUboPoints] = {};
};

struct Chunk {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    u8* map = nullptr;
    VkDeviceSize size = 0;
};

struct Batch {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> pools;
    std::vector<Chunk> chunks;
    VkDeviceSize used = 0; ///< of the last chunk
    std::vector<std::function<void()>> garbage;
    bool pending = false;
};

VkFormat VertexFormat(const Vao::Attr& a, int kind) {
    if (!a.integer) {
        static const VkFormat f[4] = {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
                                      VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
        return a.type == GL_FLOAT ? f[a.size - 1] : VK_FORMAT_UNDEFINED;
    }
    const bool s = kind == 2;
    static const VkFormat u8f[4] = {VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT,
                                    VK_FORMAT_R8G8B8A8_UINT};
    static const VkFormat s8f[4] = {VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT,
                                    VK_FORMAT_R8G8B8A8_SINT};
    static const VkFormat u16f[4] = {VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT,
                                     VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT};
    static const VkFormat s16f[4] = {VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT,
                                     VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT};
    static const VkFormat u32f[4] = {VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT,
                                     VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT};
    static const VkFormat s32f[4] = {VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT,
                                     VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT};
    switch (a.type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return (s ? s8f : u8f)[a.size - 1];
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        return (s ? s16f : u16f)[a.size - 1];
    case GL_INT:
    case GL_UNSIGNED_INT:
        return (s ? s32f : u32f)[a.size - 1];
    }
    return VK_FORMAT_UNDEFINED;
}

VkBlendFactor BlendFactor(GLenum f) {
    switch (f) {
    case GL_ZERO:
        return VK_BLEND_FACTOR_ZERO;
    case GL_ONE:
        return VK_BLEND_FACTOR_ONE;
    case 0x0300:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case 0x0301:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case GL_SRC_ALPHA:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x0304:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case 0x0305:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x0306:
        return VK_BLEND_FACTOR_DST_COLOR;
    case 0x0307:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case GL_CONSTANT_COLOR:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 0x8002:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 0x8003:
        return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 0x8004:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    }
    Log(0, "blend factor %x", f);
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp BlendOp(GLenum e) {
    switch (e) {
    case GL_FUNC_ADD:
        return VK_BLEND_OP_ADD;
    case 0x8007:
        return VK_BLEND_OP_MIN;
    case GL_MAX:
        return VK_BLEND_OP_MAX;
    case 0x800A:
        return VK_BLEND_OP_SUBTRACT;
    case 0x800B:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    }
    Log(0, "blend equation %x", e);
    return VK_BLEND_OP_ADD;
}

VkStencilOp StencilOp(GLenum o) {
    switch (o) {
    case GL_KEEP:
        return VK_STENCIL_OP_KEEP;
    case GL_ZERO:
        return VK_STENCIL_OP_ZERO;
    case GL_REPLACE:
        return VK_STENCIL_OP_REPLACE;
    case GL_INVERT:
        return VK_STENCIL_OP_INVERT;
    case 0x1E02:
        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 0x1E03:
        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    }
    Log(0, "stencil op %x", o);
    return VK_STENCIL_OP_KEEP;
}

VkSamplerAddressMode Wrap(GLenum w) {
    switch (w) {
    case GL_REPEAT:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case GL_MIRRORED_REPEAT:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case GL_CLAMP_TO_BORDER:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

TBuiltInResource Resources() {
    TBuiltInResource r{};
    r.maxLights = 32;
    r.maxClipPlanes = 6;
    r.maxTextureUnits = 32;
    r.maxTextureCoords = 32;
    r.maxVertexAttribs = 64;
    r.maxVertexUniformComponents = 4096;
    r.maxVaryingFloats = 64;
    r.maxVertexTextureImageUnits = 32;
    r.maxCombinedTextureImageUnits = 80;
    r.maxTextureImageUnits = 32;
    r.maxFragmentUniformComponents = 4096;
    r.maxDrawBuffers = 32;
    r.maxVertexUniformVectors = 128;
    r.maxVaryingVectors = 8;
    r.maxFragmentUniformVectors = 16;
    r.maxVertexOutputVectors = 16;
    r.maxFragmentInputVectors = 15;
    r.minProgramTexelOffset = -8;
    r.maxProgramTexelOffset = 7;
    r.maxClipDistances = 8;
    r.maxVertexOutputComponents = 64;
    r.maxFragmentInputComponents = 128;
    r.maxVaryingComponents = 60;
    r.maxCombinedClipAndCullDistances = 8;
    r.maxSamples = 4;
    r.limits.nonInductiveForLoops = true;
    r.limits.whileLoops = true;
    r.limits.doWhileLoops = true;
    r.limits.generalUniformIndexing = true;
    r.limits.generalAttributeMatrixVectorIndexing = true;
    r.limits.generalVaryingIndexing = true;
    r.limits.generalSamplerIndexing = true;
    r.limits.generalVariableIndexing = true;
    r.limits.generalConstantMatrixVectorIndexing = true;
    return r;
}

bool g_glslang = false;
void StartGlslang() {
    if (!g_glslang) {
        glslang::InitializeProcess();
        g_glslang = true;
    }
}

} // namespace

class Context {
public:
    Device d;
    Stats stats;
    bool healthy = true;
    VkPhysicalDeviceMemoryProperties memprops{};
    VkPhysicalDeviceProperties props{};
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFormat ds_format = VK_FORMAT_D24_UNORM_S8_UINT;

    std::vector<std::unique_ptr<Batch>> batches;
    Batch* cur = nullptr;
    u64 batch_serial = 1;

    State st;
    State other;              ///< the state not in use (UseState)
    int state = 0;            ///< which `st` is
    std::vector<GLuint> made; ///< the names made in state 1, some of them deleted since
    u32 next_name = 1;

    u32 NewName() {
        if (state == 1) {
            made.push_back(next_name);
        }
        return next_name++;
    }
    std::unordered_map<GLuint, Buf> bufs;
    std::unordered_map<GLuint, Tex> texs;
    std::unordered_map<GLuint, Fbo> fbos;
    std::unordered_map<GLuint, Vao> vaos;
    std::unordered_map<GLuint, Shader> shaders;
    std::unordered_map<GLuint, Program> programs;

    std::map<std::array<u32, 3>, VkRenderPass> render_passes;
    std::map<std::array<u64, 5>, VkFramebuffer> framebuffers;
    std::map<std::array<u32, 3>, VkSampler> samplers;
    std::unordered_map<std::string, VkPipeline> pipelines;
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE; ///< the same programs come again (Compiled)

    // the open render pass
    bool pass_open = false;
    GLuint pass_fbo = 0;
    std::array<u64, 5> pass_key{};
    VkRenderPass pass_rp = VK_NULL_HANDLE;
    u32 pass_w = 0, pass_h = 0;
    bool pass_wrote[3] = {false, false, false};
    bool need_barrier = true;

    // textures to stand in for nothing bound: 2D, array, int 2D, uint 2D, uint array
    GLuint dummy[5] = {};
    GLuint clear_program = 0, clear_vao = 0;

    // ---- device helpers

    void Fail(const char* what, VkResult r) {
        if (healthy) {
            Log(0, "%s failed (%d); the renderer stops", what, static_cast<int>(r));
        }
        healthy = false;
    }

    u32 MemoryType(u32 bits, VkMemoryPropertyFlags want) {
        for (u32 i = 0; i < memprops.memoryTypeCount; i++) {
            if ((bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & want) == want) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    bool Init(std::string& why) {
        VKD.vkGetPhysicalDeviceMemoryProperties(d.physical, &memprops);
        VKD.vkGetPhysicalDeviceProperties(d.physical, &props);
        VkPhysicalDeviceFeatures feats{};
        VKD.vkGetPhysicalDeviceFeatures(d.physical, &feats);
        if (!feats.independentBlend) {
            why = "the graphics device has no independent blending";
            return false;
        }
        const auto supports = [&](VkFormat f, VkFormatFeatureFlags want) {
            VkFormatProperties fp{};
            VKD.vkGetPhysicalDeviceFormatProperties(d.physical, f, &fp);
            return (fp.optimalTilingFeatures & want) == want;
        };
        const VkFormatFeatureFlags dsf =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (supports(VK_FORMAT_D24_UNORM_S8_UINT, dsf)) {
            ds_format = VK_FORMAT_D24_UNORM_S8_UINT;
        } else if (supports(VK_FORMAT_D32_SFLOAT_S8_UINT, dsf)) {
            ds_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        } else {
            why = "the graphics device has no depth and stencil format that can be sampled";
            return false;
        }
        if (!supports(VK_FORMAT_A1R5G5B5_UNORM_PACK16,
                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
            why = "the graphics device cannot draw into 16 bit colour";
            return false;
        }
        const VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
                                          VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                          d.queue_family};
        if (VkResult r = VKD.vkCreateCommandPool(d.device, &pci, nullptr, &pool); r != VK_SUCCESS) {
            why = "no command pool";
            return false;
        }
        const VkPipelineCacheCreateInfo cci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr,
                                            0, 0, nullptr};
        if (VKD.vkCreatePipelineCache(d.device, &cci, nullptr, &pipeline_cache) != VK_SUCCESS) {
            pipeline_cache = VK_NULL_HANDLE; // slower
        }
        StartGlslang();
        NewBatch();
        return healthy;
    }

    // ---- batches

    Chunk MakeChunk(VkDeviceSize size) {
        Chunk c;
        c.size = size;
        const VkBufferCreateInfo bi{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr};
        VkResult r = VKD.vkCreateBuffer(d.device, &bi, nullptr, &c.buffer);
        if (r != VK_SUCCESS) {
            Fail("vkCreateBuffer", r);
            return c;
        }
        VkMemoryRequirements mr{};
        VKD.vkGetBufferMemoryRequirements(d.device, c.buffer, &mr);
        u32 type = MemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                     VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        if (type == UINT32_MAX) {
            type = MemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
        const VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, mr.size,
                                      type};
        r = VKD.vkAllocateMemory(d.device, &ai, nullptr, &c.memory);
        if (r != VK_SUCCESS) {
            Fail("vkAllocateMemory", r);
            return c;
        }
        VKD.vkBindBufferMemory(d.device, c.buffer, c.memory, 0);
        void* p = nullptr;
        r = VKD.vkMapMemory(d.device, c.memory, 0, VK_WHOLE_SIZE, 0, &p);
        if (r != VK_SUCCESS) {
            Fail("vkMapMemory", r);
        }
        c.map = static_cast<u8*>(p);
        return c;
    }

    void FreeChunk(Chunk& c) {
        if (c.buffer) {
            VKD.vkDestroyBuffer(d.device, c.buffer, nullptr);
        }
        if (c.memory) {
            VKD.vkFreeMemory(d.device, c.memory, nullptr);
        }
        c = {};
    }

    VkDescriptorPool MakePool() {
        const VkDescriptorPoolSize sizes[2] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192},
        };
        const VkDescriptorPoolCreateInfo ci{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1024, 2, sizes};
        VkDescriptorPool p = VK_NULL_HANDLE;
        if (VkResult r = VKD.vkCreateDescriptorPool(d.device, &ci, nullptr, &p); r != VK_SUCCESS) {
            Fail("vkCreateDescriptorPool", r);
        }
        return p;
    }

    void Recycle(Batch& b) {
        for (auto& g : b.garbage) {
            g();
        }
        b.garbage.clear();
        for (VkDescriptorPool p : b.pools) {
            VKD.vkResetDescriptorPool(d.device, p, 0);
        }
        // one pool and one chunk stay; more of them were a peak
        while (b.pools.size() > 1) {
            VKD.vkDestroyDescriptorPool(d.device, b.pools.back(), nullptr);
            b.pools.pop_back();
        }
        while (b.chunks.size() > 1) {
            FreeChunk(b.chunks.back());
            b.chunks.pop_back();
        }
        b.used = 0;
        b.pending = false;
    }

    void NewBatch() {
        cur = nullptr;
        for (auto& b : batches) {
            if (b->pending && VKD.vkGetFenceStatus(d.device, b->fence) == VK_SUCCESS) {
                Recycle(*b);
            }
        }
        for (auto& b : batches) {
            if (!b->pending) {
                cur = b.get();
                break;
            }
        }
        if (!cur) {
            if (batches.size() >= 6) {
                // the GPU is far behind: wait for the oldest
                Batch& b = *batches.front();
                VKD.vkWaitForFences(d.device, 1, &b.fence, VK_TRUE, UINT64_MAX);
                Recycle(b);
                cur = &b;
            } else {
                auto b = std::make_unique<Batch>();
                const VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                     nullptr, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                     1};
                VkResult r = VKD.vkAllocateCommandBuffers(d.device, &ai, &b->cmd);
                if (r != VK_SUCCESS) {
                    Fail("vkAllocateCommandBuffers", r);
                }
                const VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
                r = VKD.vkCreateFence(d.device, &fi, nullptr, &b->fence);
                if (r != VK_SUCCESS) {
                    Fail("vkCreateFence", r);
                }
                cur = b.get();
                batches.push_back(std::move(b));
            }
        }
        if (cur->pools.empty()) {
            cur->pools.push_back(MakePool());
        }
        if (cur->chunks.empty()) {
            cur->chunks.push_back(MakeChunk(kChunkSize));
        }
        VKD.vkResetFences(d.device, 1, &cur->fence);
        VKD.vkResetCommandBuffer(cur->cmd, 0);
        const VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                          VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        VKD.vkBeginCommandBuffer(cur->cmd, &bi);
        batch_serial++;
        need_barrier = true;
    }

    void Submit(bool wait, const std::function<void()>& before_reuse = {}) {
        EndPass();
        Batch* b = cur;
        VkResult r = VKD.vkEndCommandBuffer(b->cmd);
        if (r != VK_SUCCESS) {
            Fail("vkEndCommandBuffer", r);
        }
        const VkSubmitInfo si{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &b->cmd, 0, nullptr};
        {
            std::unique_lock<std::mutex> lock;
            if (d.queue_mutex) {
                lock = std::unique_lock<std::mutex>(*d.queue_mutex);
            }
            r = VKD.vkQueueSubmit(d.queue, 1, &si, b->fence);
        }
        if (r != VK_SUCCESS) {
            Fail("vkQueueSubmit", r);
        } else {
            b->pending = true;
            if (wait) {
                r = VKD.vkWaitForFences(d.device, 1, &b->fence, VK_TRUE, UINT64_MAX);
                if (r != VK_SUCCESS) {
                    Fail("vkWaitForFences", r);
                }
            }
        }
        stats.submits++;
        if (before_reuse) {
            before_reuse();
        }
        NewBatch();
    }

    /// `size` bytes for the GPU to read during this batch
    u8* Stream(VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset) {
        const VkDeviceSize align =
            std::max<VkDeviceSize>(256, props.limits.minUniformBufferOffsetAlignment);
        Chunk* c = &cur->chunks.back();
        VkDeviceSize at = (cur->used + align - 1) / align * align;
        if (at + size > c->size) {
            cur->chunks.push_back(MakeChunk(std::max(kChunkSize, size)));
            c = &cur->chunks.back();
            at = 0;
        }
        cur->used = at + size;
        buffer = c->buffer;
        offset = at;
        stats.stream_bytes += size;
        static u8 nothing[16];
        return c->map ? c->map + at : nothing;
    }

    void Snapshot(Buf& b) {
        if (b.snap_version == b.version && b.snap_batch == batch_serial) {
            return;
        }
        const VkDeviceSize size = std::max<size_t>(b.data.size(), 16);
        u8* p = Stream(size, b.snap_buffer, b.snap_offset);
        if (healthy && !b.data.empty()) {
            std::memcpy(p, b.data.data(), b.data.size());
        }
        b.snap_version = b.version;
        b.snap_batch = batch_serial;
    }

    void Later(std::function<void()> f) {
        cur->garbage.push_back(std::move(f));
    }

    void Barrier() {
        if (!need_barrier) {
            return;
        }
        const VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                 VK_ACCESS_MEMORY_WRITE_BIT,
                                 VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
        VKD.vkCmdPipelineBarrier(cur->cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0,
                                 nullptr);
        need_barrier = false;
    }

    void EndPass() {
        if (pass_open) {
            VKD.vkCmdEndRenderPass(cur->cmd);
            pass_open = false;
            need_barrier = true;
        }
    }

    // ---- textures

    Tex* BoundTex(GLenum target) {
        const GLuint name = st.units[st.active_unit][target == GL_TEXTURE_2D_ARRAY ? 1 : 0];
        auto it = texs.find(name);
        return it == texs.end() ? nullptr : &it->second;
    }

    void ReleaseImage(Tex& t) {
        if (!t.image) {
            return;
        }
        EndPass();
        // framebuffers made of its views go with it
        for (auto it = framebuffers.begin(); it != framebuffers.end();) {
            bool uses = false;
            for (VkImageView v : t.layer_views) {
                for (int i = 1; i < 4; i++) {
                    uses |= it->first[i] == reinterpret_cast<u64>(v);
                }
            }
            if (uses) {
                VkFramebuffer fb = it->second;
                Later([this, fb] { VKD.vkDestroyFramebuffer(d.device, fb, nullptr); });
                it = framebuffers.erase(it);
            } else {
                ++it;
            }
        }
        std::vector<VkImageView> views = t.layer_views;
        views.push_back(t.view);
        const VkImage image = t.image;
        const VkDeviceMemory memory = t.memory;
        Later([this, views, image, memory] {
            for (VkImageView v : views) {
                if (v) {
                    VKD.vkDestroyImageView(d.device, v, nullptr);
                }
            }
            VKD.vkDestroyImage(d.device, image, nullptr);
            VKD.vkFreeMemory(d.device, memory, nullptr);
        });
        t.image = VK_NULL_HANDLE;
        t.memory = VK_NULL_HANDLE;
        t.view = VK_NULL_HANDLE;
        t.layer_views.clear();
    }

    VkImageAspectFlags Aspect(const Tex& t, bool sampling) {
        if (t.depth) {
            return VK_IMAGE_ASPECT_DEPTH_BIT |
                   (t.stencil && !sampling ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        }
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }

    void Allocate(Tex& t, GLenum target, GLenum ifmt, u32 w, u32 h, u32 layers) {
        if (t.image && t.target == target && t.ifmt == ifmt && t.w == w && t.h == h &&
            t.layers == layers) {
            return;
        }
        // The renderer makes its pictures again at another scale while a display capture is still
        // to be shown from them; OpenGL leaves their contents undefined, here they stay.
        const bool keep = t.image && t.can_render && t.target == target && t.ifmt == ifmt &&
                          ifmt == GL_RGBA && t.layers == layers && w > 0 && h > 0;
        const VkImage old_image = keep ? t.image : VK_NULL_HANDLE;
        const VkOffset3D old_size{static_cast<int32_t>(t.w), static_cast<int32_t>(t.h), 1};
        ReleaseImage(t); // after this batch
        t.target = target;
        t.ifmt = ifmt;
        t.w = w;
        t.h = h;
        t.layers = layers;
        t.array = target == GL_TEXTURE_2D_ARRAY;
        t.depth = t.stencil = t.swap_rb = false;
        switch (ifmt) {
        case GL_RGBA:
        case GL_RGB:
            t.format = VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case GL_RGBA8UI:
            t.format = VK_FORMAT_R8G8B8A8_UINT;
            break;
        case GL_R32UI:
            t.format = VK_FORMAT_R32_UINT;
            break;
        case GL_R8UI:
            t.format = VK_FORMAT_R8_UINT;
            break;
        case GL_R8I:
            t.format = VK_FORMAT_R8_SINT;
            break;
        case GL_RGB5_A1:
            t.format = VK_FORMAT_A1R5G5B5_UNORM_PACK16;
            t.swap_rb = true;
            break;
        case GL_DEPTH24_STENCIL8:
            t.format = ds_format;
            t.depth = t.stencil = true;
            break;
        case GL_DEPTH_COMPONENT16:
            t.format = VK_FORMAT_D16_UNORM;
            t.depth = true;
            break;
        default:
            Log(0, "texture format %x is not known", ifmt);
            t.format = VK_FORMAT_R8G8B8A8_UNORM;
        }
        if (w == 0 || h == 0 || layers == 0) {
            return;
        }
        VkFormatProperties fp{};
        VKD.vkGetPhysicalDeviceFormatProperties(d.physical, t.format, &fp);
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        t.can_render = false;
        if (t.depth) {
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            t.can_render = true;
        } else if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) {
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            t.can_render = true;
        }
        const VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                   nullptr,
                                   0,
                                   VK_IMAGE_TYPE_2D,
                                   t.format,
                                   {w, h, 1},
                                   1,
                                   layers,
                                   VK_SAMPLE_COUNT_1_BIT,
                                   VK_IMAGE_TILING_OPTIMAL,
                                   usage,
                                   VK_SHARING_MODE_EXCLUSIVE,
                                   0,
                                   nullptr,
                                   VK_IMAGE_LAYOUT_UNDEFINED};
        VkResult r = VKD.vkCreateImage(d.device, &ci, nullptr, &t.image);
        if (r != VK_SUCCESS) {
            Fail("vkCreateImage", r);
            t.image = VK_NULL_HANDLE;
            return;
        }
        VkMemoryRequirements mr{};
        VKD.vkGetImageMemoryRequirements(d.device, t.image, &mr);
        const VkMemoryAllocateInfo ai{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, mr.size,
            MemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        r = VKD.vkAllocateMemory(d.device, &ai, nullptr, &t.memory);
        if (r != VK_SUCCESS) {
            Fail("vkAllocateMemory", r);
            VKD.vkDestroyImage(d.device, t.image, nullptr);
            t.image = VK_NULL_HANDLE;
            return;
        }
        VKD.vkBindImageMemory(d.device, t.image, t.memory, 0);
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 nullptr,
                                 0,
                                 t.image,
                                 t.array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
                                 t.format,
                                 {},
                                 {Aspect(t, true), 0, 1, 0, layers}};
        if (t.swap_rb) {
            vi.components = {VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R,
                             VK_COMPONENT_SWIZZLE_A};
        }
        r = VKD.vkCreateImageView(d.device, &vi, nullptr, &t.view);
        if (r != VK_SUCCESS) {
            Fail("vkCreateImageView", r);
        }
        t.layer_views.assign(layers, VK_NULL_HANDLE);
        if (t.can_render) {
            for (u32 l = 0; l < layers; l++) {
                const VkImageViewCreateInfo li{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                               nullptr,
                                               0,
                                               t.image,
                                               VK_IMAGE_VIEW_TYPE_2D,
                                               t.format,
                                               {},
                                               {Aspect(t, false), 0, 1, l, 1}};
                r = VKD.vkCreateImageView(d.device, &li, nullptr, &t.layer_views[l]);
                if (r != VK_SUCCESS) {
                    Fail("vkCreateImageView", r);
                }
            }
        }
        // to the one layout it will ever have, and with defined contents
        EndPass();
        VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                nullptr,
                                0,
                                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_GENERAL,
                                VK_QUEUE_FAMILY_IGNORED,
                                VK_QUEUE_FAMILY_IGNORED,
                                t.image,
                                {Aspect(t, false), 0, 1, 0, layers}};
        VKD.vkCmdPipelineBarrier(cur->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &ib);
        if (t.depth) {
            const VkClearDepthStencilValue cv{1.0f, 0};
            VKD.vkCmdClearDepthStencilImage(cur->cmd, t.image, VK_IMAGE_LAYOUT_GENERAL, &cv, 1,
                                            &ib.subresourceRange);
        } else {
            const VkClearColorValue cv{};
            VKD.vkCmdClearColorImage(cur->cmd, t.image, VK_IMAGE_LAYOUT_GENERAL, &cv, 1,
                                     &ib.subresourceRange);
        }
        need_barrier = true;
        if (old_image) {
            Barrier();
            const VkImageSubresourceLayers sub{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
            const VkImageBlit blit{
                sub,
                {{0, 0, 0}, old_size},
                sub,
                {{0, 0, 0}, {static_cast<int32_t>(w), static_cast<int32_t>(h), 1}}};
            VKD.vkCmdBlitImage(cur->cmd, old_image, VK_IMAGE_LAYOUT_GENERAL, t.image,
                               VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
            need_barrier = true;
        }
    }

    static u32 PixelBytes(GLenum format, GLenum type) {
        if (type == GL_UNSIGNED_SHORT_1_5_5_5_REV) {
            return 2;
        }
        if (format == GL_RED_INTEGER) {
            return type == GL_UNSIGNED_INT ? 4 : 1;
        }
        return 4; // RGBA and RGBA_INTEGER in bytes
    }

    void Upload(Tex& t, u32 x, u32 y, u32 layer, u32 w, u32 h, GLenum format, GLenum type,
                const void* data) {
        if (!t.image || !data || w == 0 || h == 0) {
            return;
        }
        if (t.depth) {
            Log(0, "depth data cannot be loaded");
            return;
        }
        const u32 bpp = PixelBytes(format, type);
        const u32 row = (w * bpp + 3) & ~3u; // OpenGL's rows are padded to 4 bytes
        const VkDeviceSize size = static_cast<VkDeviceSize>(row) * h;
        VkBuffer buffer;
        VkDeviceSize offset;
        u8* p = Stream(size, buffer, offset);
        if (!healthy) {
            return;
        }
        std::memcpy(p, data, static_cast<size_t>(row) * (h - 1) + w * bpp);
        EndPass();
        Barrier();
        const VkBufferImageCopy region{offset,
                                       row / bpp,
                                       h,
                                       {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1},
                                       {static_cast<int32_t>(x), static_cast<int32_t>(y), 0},
                                       {w, h, 1}};
        VKD.vkCmdCopyBufferToImage(cur->cmd, buffer, t.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        need_barrier = true;
        stats.uploads++;
        stats.upload_bytes += size;
    }

    VkSampler SamplerFor(const Tex& t) {
        const bool linear = t.mag == GL_LINEAR;
        const std::array<u32, 3> key{t.wrap_s, t.wrap_t, linear ? 1u : 0u};
        auto it = samplers.find(key);
        if (it != samplers.end()) {
            return it->second;
        }
        const VkFilter f = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        const VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                     nullptr,
                                     0,
                                     f,
                                     f,
                                     VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                     Wrap(t.wrap_s),
                                     Wrap(t.wrap_t),
                                     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                     0.0f,
                                     VK_FALSE,
                                     1.0f,
                                     VK_FALSE,
                                     VK_COMPARE_OP_NEVER,
                                     0.0f,
                                     0.0f,
                                     VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                                     VK_FALSE};
        VkSampler s = VK_NULL_HANDLE;
        if (VkResult r = VKD.vkCreateSampler(d.device, &ci, nullptr, &s); r != VK_SUCCESS) {
            Fail("vkCreateSampler", r);
        }
        samplers[key] = s;
        return s;
    }

    // ---- framebuffers and passes

    struct Target {
        Tex* tex[3] = {nullptr, nullptr, nullptr};
        VkImageView view[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        u32 w = 0, h = 0;
        bool ok = false;
    };

    Target Resolve(GLuint name) {
        Target t;
        auto it = fbos.find(name);
        if (name == 0 || it == fbos.end()) {
            return t;
        }
        const Fbo& f = it->second;
        const Fbo::Att* atts[3] = {&f.color[0], &f.color[1], &f.ds};
        for (int i = 0; i < 3; i++) {
            auto ti = texs.find(atts[i]->tex);
            if (atts[i]->tex == 0 || ti == texs.end() || !ti->second.image) {
                continue;
            }
            Tex& tex = ti->second;
            const u32 layer = static_cast<u32>(std::max(atts[i]->layer, 0));
            if (!tex.can_render || layer >= tex.layer_views.size()) {
                continue;
            }
            t.tex[i] = &tex;
            t.view[i] = tex.layer_views[layer];
            t.w = t.w ? std::min(t.w, tex.w) : tex.w;
            t.h = t.h ? std::min(t.h, tex.h) : tex.h;
        }
        t.ok = t.tex[0] != nullptr || t.tex[2] != nullptr;
        if (t.tex[1] && !t.tex[0]) {
            t.ok = false;
        }
        return t;
    }

    VkRenderPass RenderPassFor(const Target& t) {
        const std::array<u32, 3> key{t.tex[0] ? t.tex[0]->format : 0u,
                                     t.tex[1] ? t.tex[1]->format : 0u,
                                     t.tex[2] ? t.tex[2]->format : 0u};
        auto it = render_passes.find(key);
        if (it != render_passes.end()) {
            return it->second;
        }
        VkAttachmentDescription ad[3];
        VkAttachmentReference colors[2];
        VkAttachmentReference dsref{};
        u32 n = 0, ncolor = 0;
        for (int i = 0; i < 3; i++) {
            if (!key[i]) {
                continue;
            }
            ad[n] = {0,
                     static_cast<VkFormat>(key[i]),
                     VK_SAMPLE_COUNT_1_BIT,
                     VK_ATTACHMENT_LOAD_OP_LOAD,
                     VK_ATTACHMENT_STORE_OP_STORE,
                     VK_ATTACHMENT_LOAD_OP_LOAD,
                     VK_ATTACHMENT_STORE_OP_STORE,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_GENERAL};
            if (i < 2) {
                colors[ncolor++] = {n, VK_IMAGE_LAYOUT_GENERAL};
            } else {
                dsref = {n, VK_IMAGE_LAYOUT_GENERAL};
            }
            n++;
        }
        const VkSubpassDescription sub{
            0,       VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, ncolor, colors,
            nullptr, key[2] ? &dsref : nullptr,       0, nullptr};
        const VkRenderPassCreateInfo ci{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, n, ad, 1, &sub, 0, nullptr};
        VkRenderPass rp = VK_NULL_HANDLE;
        if (VkResult r = VKD.vkCreateRenderPass(d.device, &ci, nullptr, &rp); r != VK_SUCCESS) {
            Fail("vkCreateRenderPass", r);
        }
        render_passes[key] = rp;
        return rp;
    }

    /// The pass on the draw framebuffer, open
    bool BeginPass(Target& t) {
        t = Resolve(st.draw_fbo);
        if (!t.ok || !healthy) {
            return false;
        }
        const VkRenderPass rp = RenderPassFor(t);
        const std::array<u64, 5> key{reinterpret_cast<u64>(rp), reinterpret_cast<u64>(t.view[0]),
                                     reinterpret_cast<u64>(t.view[1]),
                                     reinterpret_cast<u64>(t.view[2]),
                                     (static_cast<u64>(t.w) << 32) | t.h};
        if (pass_open && key == pass_key) {
            return true;
        }
        EndPass();
        VkFramebuffer fb = VK_NULL_HANDLE;
        auto it = framebuffers.find(key);
        if (it != framebuffers.end()) {
            fb = it->second;
        } else {
            VkImageView views[3];
            u32 n = 0;
            for (int i = 0; i < 3; i++) {
                if (t.view[i]) {
                    views[n++] = t.view[i];
                }
            }
            const VkFramebufferCreateInfo ci{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, rp, n, views, t.w, t.h, 1};
            if (VkResult r = VKD.vkCreateFramebuffer(d.device, &ci, nullptr, &fb);
                r != VK_SUCCESS) {
                Fail("vkCreateFramebuffer", r);
                return false;
            }
            framebuffers[key] = fb;
        }
        Barrier();
        const VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       nullptr,
                                       rp,
                                       fb,
                                       {{0, 0}, {t.w, t.h}},
                                       0,
                                       nullptr};
        VKD.vkCmdBeginRenderPass(cur->cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
        pass_open = true;
        pass_key = key;
        pass_rp = rp;
        pass_fbo = st.draw_fbo;
        pass_w = t.w;
        pass_h = t.h;
        pass_wrote[0] = pass_wrote[1] = pass_wrote[2] = false;
        stats.passes++;
        return true;
    }

    // ---- programs

    static std::string Place(std::string src, const std::map<std::string, int>& locs,
                             const char* dir) {
        for (const auto& [name, loc] : locs) {
            const std::regex re(std::string("\\b") + dir + "\\s+(\\w+)\\s+" + name + "\\s*;");
            src = std::regex_replace(src, re,
                                     "layout(location=" + std::to_string(loc) + ") " + dir +
                                         " $1 " + name + ";");
        }
        return src;
    }

    static void Dialect(std::string& vs, std::string& fs) {
        // layout(location=) came with 330, which has all of 140's core
        for (std::string* src : {&vs, &fs}) {
            const size_t at = src->find("#version 140");
            if (at != std::string::npos) {
                src->replace(at, 12, "#version 330");
            }
        }
        // OpenGL's depth is -w..w, Vulkan's 0..w
        const size_t eol = vs.find('\n') + 1;
        vs = vs.substr(0, eol) + "#define main dsgl_main\n" + vs.substr(eol) +
             "\n#undef main\nvoid main() { dsgl_main(); gl_Position.z = (gl_Position.z + "
             "gl_Position.w) * 0.5; }\n";
    }

    bool Link(Program& p) {
        std::string vs, fs;
        for (GLuint s : p.shaders) {
            auto it = shaders.find(s);
            if (it == shaders.end()) {
                continue;
            }
            (it->second.type == GL_VERTEX_SHADER ? vs : fs) = it->second.source;
        }
        if (vs.empty() || fs.empty()) {
            p.log = "a vertex and a fragment shader are needed";
            return false;
        }
        vs = Place(vs, p.attrib_locs, "in");
        fs = Place(fs, p.frag_locs, "out");
        Dialect(vs, fs);

        // Two renderers have the same programs, and one is made again with every change of Layer
        // Depth: their SPIR-V is kept
        static std::map<std::string, Compiled> compiled;
        const auto [entry, is_new] = compiled.try_emplace(vs + '\0' + fs);
        if (is_new && !Compile(vs, fs, entry->second, p.log)) {
            compiled.erase(entry);
            return false;
        }
        const Compiled& c = entry->second;
        p.uniforms = c.uniforms;
        p.bindings = c.bindings;
        p.blocks = c.blocks;
        p.input_kind = c.input_kind;
        p.defaults.assign(c.defaults, 0);
        p.defaults_buf.data = p.defaults;
        return Make(p, c);
    }

    /// What is found out of a program's source.
    struct Compiled {
        std::vector<Program::Uniform> uniforms;
        std::vector<Program::Binding> bindings;
        std::vector<int> blocks;
        std::array<int, kAttrs> input_kind{};
        size_t defaults = 0;
        std::array<std::vector<u32>, 2> spv; ///< vertex, fragment
    };

    static bool Compile(const std::string& vs, const std::string& fs, Compiled& p,
                        std::string& log) {
        const TBuiltInResource res = Resources();
        const EShMessages msg =
            static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
        glslang::TShader tvs(EShLangVertex), tfs(EShLangFragment);
        const char* srcs[2] = {vs.c_str(), fs.c_str()};
        glslang::TShader* sh[2] = {&tvs, &tfs};
        const EShLanguage langs[2] = {EShLangVertex, EShLangFragment};
        for (int i = 0; i < 2; i++) {
            sh[i]->setStrings(&srcs[i], 1);
            sh[i]->setEnvInput(glslang::EShSourceGlsl, langs[i], glslang::EShClientVulkan, 100);
            sh[i]->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
            sh[i]->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
            sh[i]->setEnvInputVulkanRulesRelaxed();
            sh[i]->setAutoMapBindings(true);
            sh[i]->setAutoMapLocations(true);
            if (!sh[i]->parse(&res, 140, ECoreProfile, false, true, msg)) {
                log = std::string(i ? "fragment: " : "vertex: ") + sh[i]->getInfoLog();
                return false;
            }
        }
        glslang::TProgram prog;
        prog.addShader(&tvs);
        prog.addShader(&tfs);
        bool built = prog.link(msg);
        if (built) {
            // the plain mapper numbers each stage on its own; this one lines the two up by name
            const std::unique_ptr<glslang::TIoMapResolver> resolver(
                prog.getGlslIoResolver(EShLangVertex));
            const std::unique_ptr<glslang::TIoMapper> mapper(glslang::GetGlslIoMapper());
            built = prog.mapIO(resolver.get(), mapper.get());
        }
        if (!built) {
            log = std::string("link: ") + prog.getInfoLog();
            return false;
        }
        prog.buildReflection(EShReflectionDefault);

        // blocks
        int default_block = -1;
        for (int i = 0; i < prog.getNumUniformBlocks(); i++) {
            const auto& b = prog.getUniformBlock(i);
            Program::Binding bind;
            bind.binding = static_cast<u32>(b.getBinding());
            bind.block = b.name;
            bind.size = static_cast<u32>(b.size);
            bind.is_default = b.name == "gl_DefaultUniformBlock";
            if (bind.is_default) {
                default_block = i;
                p.defaults = (bind.size + 15) & ~15u;
            }
            p.blocks.push_back(static_cast<int>(p.bindings.size()));
            p.bindings.push_back(bind);
        }
        for (int i = 0; i < prog.getNumUniformVariables(); i++) {
            const auto& u = prog.getUniform(i);
            Program::Uniform un;
            un.name = u.name;
            if (u.index < 0 && u.getBinding() >= 0) {
                // an array of samplers is listed element by element, at one binding
                u32 element = 0;
                const size_t br = un.name.find('[');
                if (br != std::string::npos) {
                    element = static_cast<u32>(std::atoi(un.name.c_str() + br + 1));
                    un.name.resize(br);
                }
                const u32 count =
                    std::min(element + static_cast<u32>(std::max(u.size, 1)), kMaxSamplerArray);
                const u32 binding = static_cast<u32>(u.getBinding());
                auto known = std::find_if(p.bindings.begin(), p.bindings.end(), [&](const auto& b) {
                    return b.sampler && b.binding == binding;
                });
                if (known != p.bindings.end()) {
                    known->count = std::max(known->count, count);
                    continue;
                }
                Program::Binding bind;
                bind.binding = binding;
                bind.sampler = true;
                bind.count = count;
                bind.gl_type = u.glDefineType;
                un.sampler = true;
                un.binding = static_cast<u32>(p.bindings.size());
                p.bindings.push_back(bind);
                p.uniforms.push_back(un);
            } else if (u.index == default_block && default_block >= 0) {
                const size_t dot = un.name.find('.');
                if (dot != std::string::npos) {
                    un.name.erase(0, dot + 1);
                }
                un.offset = static_cast<u32>(u.offset);
                p.uniforms.push_back(un);
            }
        }
        for (int i = 0; i < prog.getNumPipeInputs(); i++) {
            const auto& in = prog.getPipeInput(i);
            const u32 loc = in.layoutLocation();
            if (loc >= kAttrs) {
                continue;
            }
            const int ty = in.glDefineType;
            const bool is_int = ty == 0x1404 || (ty >= 0x8B53 && ty <= 0x8B55);
            const bool is_uint = ty == 0x1405 || (ty >= 0x8DC6 && ty <= 0x8DC8);
            p.input_kind[loc] = is_int ? 2 : is_uint ? 3 : 1;
        }

        for (size_t i = 0; i < 2; i++) {
            glslang::SpvOptions opt;
            opt.disableOptimizer = true;
            glslang::GlslangToSpv(*prog.getIntermediate(langs[i]), p.spv[i], &opt);
        }
        return true;
    }

    bool Make(Program& p, const Compiled& compiled) {
        for (size_t i = 0; i < 2; i++) {
            const std::vector<u32>& spv = compiled.spv[i];
            const VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr,
                                              0, spv.size() * 4, spv.data()};
            VkShaderModule m = VK_NULL_HANDLE;
            if (VkResult r = VKD.vkCreateShaderModule(d.device, &ci, nullptr, &m);
                r != VK_SUCCESS) {
                p.log = "no shader module";
                return false;
            }
            (i ? p.fs : p.vs) = m;
        }
        std::vector<VkDescriptorSetLayoutBinding> lb;
        for (const auto& b : p.bindings) {
            lb.push_back({b.binding,
                          b.sampler ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                    : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                          b.count, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          nullptr});
        }
        const VkDescriptorSetLayoutCreateInfo li{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
            static_cast<u32>(lb.size()), lb.data()};
        VkResult r = VKD.vkCreateDescriptorSetLayout(d.device, &li, nullptr, &p.set_layout);
        if (r != VK_SUCCESS) {
            p.log = "no descriptor set layout (two uniforms at one binding?)";
            return false;
        }
        const VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                            nullptr,
                                            0,
                                            1,
                                            &p.set_layout,
                                            0,
                                            nullptr};
        r = VKD.vkCreatePipelineLayout(d.device, &pi, nullptr, &p.layout);
        if (r != VK_SUCCESS) {
            p.log = "no pipeline layout";
            return false;
        }
        return true;
    }

    void FreeProgram(GLuint name, Program& p) {
        EndPass();
        const std::string tag(reinterpret_cast<const char*>(&name), sizeof(name));
        for (auto it = pipelines.begin(); it != pipelines.end();) {
            if (it->first.compare(0, sizeof(name), tag) == 0) {
                const VkPipeline pl = it->second;
                Later([this, pl] { VKD.vkDestroyPipeline(d.device, pl, nullptr); });
                it = pipelines.erase(it);
            } else {
                ++it;
            }
        }
        const VkShaderModule vs = p.vs, fs = p.fs;
        const VkDescriptorSetLayout sl = p.set_layout;
        const VkPipelineLayout pl = p.layout;
        Later([this, vs, fs, sl, pl] {
            if (vs) {
                VKD.vkDestroyShaderModule(d.device, vs, nullptr);
            }
            if (fs) {
                VKD.vkDestroyShaderModule(d.device, fs, nullptr);
            }
            if (pl) {
                VKD.vkDestroyPipelineLayout(d.device, pl, nullptr);
            }
            if (sl) {
                VKD.vkDestroyDescriptorSetLayout(d.device, sl, nullptr);
            }
        });
    }

    // ---- drawing

    struct PipeKey {
        GLuint program;
        u64 render_pass;
        u32 topology;
        u8 depth_test, depth_write, depth_func, stencil_test;
        u8 stencil_func, sfail, dpfail, dppass;
        u8 blend, mask0, mask1, ncolor;
        u16 src_rgb, dst_rgb, src_a, dst_a, eq_rgb, eq_a;
        struct {
            u32 format;
            u32 stride;
            u32 offset;
        } attr[kAttrs];
    };

    VkPipeline PipelineFor(Program& p, const Vao& vao, const Target& t, GLenum mode, bool depth_on,
                           bool stencil_on) {
        PipeKey k;
        std::memset(&k, 0, sizeof(k));
        k.program = st.program;
        k.render_pass = reinterpret_cast<u64>(pass_rp);
        k.topology = mode == GL_LINES ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                      : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        k.depth_test = depth_on;
        k.depth_write = depth_on && st.depth_mask;
        k.depth_func = depth_on ? static_cast<u8>(st.depth_func - GL_NEVER) : 7;
        k.stencil_test = stencil_on;
        if (stencil_on) {
            k.stencil_func = static_cast<u8>(st.stencil_func - GL_NEVER);
            k.sfail = static_cast<u8>(StencilOp(st.sfail));
            k.dpfail = static_cast<u8>(StencilOp(st.dpfail));
            k.dppass = static_cast<u8>(StencilOp(st.dppass));
        }
        k.ncolor = (t.tex[0] ? 1 : 0) + (t.tex[1] ? 1 : 0);
        k.blend = st.blend;
        k.mask0 = st.color_mask[0];
        k.mask1 = st.color_mask[1];
        if (st.blend) {
            k.src_rgb = static_cast<u16>(BlendFactor(st.blend_src_rgb));
            k.dst_rgb = static_cast<u16>(BlendFactor(st.blend_dst_rgb));
            k.src_a = static_cast<u16>(BlendFactor(st.blend_src_a));
            k.dst_a = static_cast<u16>(BlendFactor(st.blend_dst_a));
            k.eq_rgb = static_cast<u16>(BlendOp(st.blend_eq_rgb));
            k.eq_a = static_cast<u16>(BlendOp(st.blend_eq_a));
        }
        for (u32 i = 0; i < kAttrs; i++) {
            if (p.input_kind[i] && vao.attr[i].enabled) {
                k.attr[i].format = VertexFormat(vao.attr[i], p.input_kind[i]);
                k.attr[i].stride = vao.attr[i].stride;
                k.attr[i].offset = vao.attr[i].offset;
            }
        }
        const std::string key(reinterpret_cast<const char*>(&k), sizeof(k));
        auto it = pipelines.find(key);
        if (it != pipelines.end()) {
            return it->second;
        }

        const VkPipelineShaderStageCreateInfo stages[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, p.vs, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, p.fs, "main", nullptr},
        };
        VkVertexInputBindingDescription vb[kAttrs];
        VkVertexInputAttributeDescription va[kAttrs];
        u32 nv = 0;
        for (u32 i = 0; i < kAttrs; i++) {
            if (!k.attr[i].format) {
                continue;
            }
            vb[nv] = {i, k.attr[i].stride, VK_VERTEX_INPUT_RATE_VERTEX};
            va[nv] = {i, i, static_cast<VkFormat>(k.attr[i].format), k.attr[i].offset};
            nv++;
        }
        const VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, nv, vb, nv, va};
        const VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0,
            static_cast<VkPrimitiveTopology>(k.topology), VK_FALSE};
        const VkPipelineViewportStateCreateInfo vp{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            nullptr,
            0,
            1,
            nullptr,
            1,
            nullptr};
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = k.depth_test;
        ds.depthWriteEnable = k.depth_write;
        ds.depthCompareOp = static_cast<VkCompareOp>(k.depth_func);
        ds.stencilTestEnable = k.stencil_test;
        ds.front = {static_cast<VkStencilOp>(k.sfail),
                    static_cast<VkStencilOp>(k.dppass),
                    static_cast<VkStencilOp>(k.dpfail),
                    static_cast<VkCompareOp>(k.stencil_func),
                    0xFF,
                    0xFF,
                    0};
        ds.back = ds.front;
        VkPipelineColorBlendAttachmentState ba[2];
        for (int i = 0; i < 2; i++) {
            ba[i] = {};
            ba[i].blendEnable = k.blend;
            if (k.blend) {
                ba[i].srcColorBlendFactor = static_cast<VkBlendFactor>(k.src_rgb);
                ba[i].dstColorBlendFactor = static_cast<VkBlendFactor>(k.dst_rgb);
                ba[i].srcAlphaBlendFactor = static_cast<VkBlendFactor>(k.src_a);
                ba[i].dstAlphaBlendFactor = static_cast<VkBlendFactor>(k.dst_a);
                ba[i].colorBlendOp = static_cast<VkBlendOp>(k.eq_rgb);
                ba[i].alphaBlendOp = static_cast<VkBlendOp>(k.eq_a);
            }
            ba[i].colorWriteMask = i ? k.mask1 : k.mask0;
        }
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = k.ncolor;
        cb.pAttachments = ba;
        const VkDynamicState dyn[6] = {VK_DYNAMIC_STATE_VIEWPORT,
                                       VK_DYNAMIC_STATE_SCISSOR,
                                       VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                                       VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                       VK_DYNAMIC_STATE_STENCIL_REFERENCE,
                                       VK_DYNAMIC_STATE_BLEND_CONSTANTS};
        const VkPipelineDynamicStateCreateInfo dy{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 6, dyn};
        VkGraphicsPipelineCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = &ds;
        ci.pColorBlendState = &cb;
        ci.pDynamicState = &dy;
        ci.layout = p.layout;
        ci.renderPass = pass_rp;
        VkPipeline pl = VK_NULL_HANDLE;
        if (VkResult r =
                VKD.vkCreateGraphicsPipelines(d.device, pipeline_cache, 1, &ci, nullptr, &pl);
            r != VK_SUCCESS) {
            Fail("vkCreateGraphicsPipelines", r);
        }
        pipelines[key] = pl;
        stats.pipelines++;
        return pl;
    }

    Tex* DummyFor(int gl_type) {
        int i = 0;
        switch (gl_type) {
        case 0x8DC1: // sampler2DArray
            i = 1;
            break;
        case 0x8DCA: // isampler2D
            i = 2;
            break;
        case 0x8DD2: // usampler2D
            i = 3;
            break;
        case 0x8DD7: // usampler2DArray
            i = 4;
            break;
        }
        if (!dummy[i]) {
            static const GLenum fmt[5] = {GL_RGBA, GL_RGBA, GL_R8I, GL_RGBA8UI, GL_RGBA8UI};
            dummy[i] = next_name++;
            Tex& t = texs[dummy[i]];
            Allocate(t, i == 1 || i == 4 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, fmt[i], 1, 1, 1);
        }
        return &texs[dummy[i]];
    }

    static bool IsArrayType(int gl_type) {
        return gl_type == 0x8DC1 || gl_type == 0x8DD7 || gl_type == 0x8DCF;
    }

    VkDescriptorSet AllocSet(VkDescriptorSetLayout layout) {
        for (int attempt = 0; attempt < 2; attempt++) {
            const VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                 nullptr, cur->pools.back(), 1, &layout};
            VkDescriptorSet set = VK_NULL_HANDLE;
            const VkResult r = VKD.vkAllocateDescriptorSets(d.device, &ai, &set);
            if (r == VK_SUCCESS) {
                return set;
            }
            if (attempt == 1) {
                Fail("vkAllocateDescriptorSets", r);
            }
            cur->pools.push_back(MakePool());
        }
        return VK_NULL_HANDLE;
    }

    void Draw(GLenum mode, u32 first, u32 count, bool indexed, u32 index_offset) {
        if (!healthy || count == 0) {
            return;
        }
        auto pi = programs.find(st.program);
        auto vi = vaos.find(st.vao);
        if (pi == programs.end() || !pi->second.linked || vi == vaos.end()) {
            return;
        }
        Program& p = pi->second;
        const Vao& vao = vi->second;
        Target t = Resolve(st.draw_fbo);
        if (!t.ok) {
            return;
        }
        const bool depth_on = st.depth_test && t.tex[2];
        const bool stencil_on = st.stencil_test && t.tex[2] && t.tex[2]->stencil;
        const bool writes[3] = {
            t.tex[0] && st.color_mask[0] != 0, t.tex[1] && st.color_mask[1] != 0,
            (depth_on && st.depth_mask) || (stencil_on && (st.stencil_write_mask & 0xFF) != 0)};

        // what the shader reads
        struct Sampled {
            Tex* tex;
        };
        std::vector<VkDescriptorImageInfo> images;
        std::vector<VkDescriptorBufferInfo> buffers;
        std::vector<VkWriteDescriptorSet> writes_ds;
        images.reserve(32);
        buffers.reserve(16);
        bool restart = false;
        for (const auto& b : p.bindings) {
            if (!b.sampler) {
                continue;
            }
            for (u32 e = 0; e < b.count; e++) {
                const u32 unit = static_cast<u32>(b.units[e]) % kUnits;
                auto ti = texs.find(st.units[unit][IsArrayType(b.gl_type) ? 1 : 0]);
                Tex* tex = ti != texs.end() && ti->second.image ? &ti->second : nullptr;
                if (!tex) {
                    tex = DummyFor(b.gl_type);
                }
                for (int a = 0; a < 3; a++) {
                    if (tex == t.tex[a]) {
                        if (writes[a]) {
                            Log(1, "a texture is drawn into and read at once");
                        }
                        restart |= pass_open && pass_wrote[a];
                    }
                }
                images.push_back({SamplerFor(*tex), tex->view, VK_IMAGE_LAYOUT_GENERAL});
            }
        }
        if (restart) {
            EndPass();
            stats.pass_splits++;
        }
        // buffers before the pass: snapshots are memory copies only
        for (const auto& b : p.bindings) {
            if (b.sampler) {
                continue;
            }
            Buf* buf = nullptr;
            if (b.is_default) {
                buf = &p.defaults_buf;
            } else {
                auto bi = bufs.find(st.ubo_points[b.point % kUboPoints]);
                if (bi != bufs.end()) {
                    buf = &bi->second;
                }
            }
            if (!buf || buf->data.size() < b.size) {
                static Buf zero;
                if (zero.data.size() < b.size) {
                    zero.data.assign(b.size, 0);
                    zero.version++;
                }
                buf = &zero;
            }
            Snapshot(*buf);
            buffers.push_back({buf->snap_buffer, buf->snap_offset, std::max<u32>(b.size, 16)});
        }
        VkBuffer vbufs[kAttrs];
        VkDeviceSize voffs[kAttrs];
        bool vhas[kAttrs] = {};
        for (u32 i = 0; i < kAttrs; i++) {
            if (!p.input_kind[i] || !vao.attr[i].enabled) {
                continue;
            }
            auto bi = bufs.find(vao.attr[i].buffer);
            if (bi == bufs.end()) {
                return;
            }
            Snapshot(bi->second);
            vbufs[i] = bi->second.snap_buffer;
            voffs[i] = bi->second.snap_offset;
            vhas[i] = true;
        }
        Buf* ibuf = nullptr;
        if (indexed) {
            auto bi = bufs.find(vao.elements);
            if (bi == bufs.end()) {
                return;
            }
            ibuf = &bi->second;
            Snapshot(*ibuf);
        }
        if (!healthy || !BeginPass(t)) {
            return;
        }
        const VkPipeline pipeline = PipelineFor(p, vao, t, mode, depth_on, stencil_on);
        const VkDescriptorSet set = AllocSet(p.set_layout);
        if (!pipeline || !set || !healthy) {
            return;
        }
        size_t ii = 0, bi2 = 0;
        for (const auto& b : p.bindings) {
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set;
            w.dstBinding = b.binding;
            w.descriptorCount = b.count;
            if (b.sampler) {
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.pImageInfo = &images[ii];
                ii += b.count;
            } else {
                w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo = &buffers[bi2++];
            }
            writes_ds.push_back(w);
        }
        VKD.vkUpdateDescriptorSets(d.device, static_cast<u32>(writes_ds.size()), writes_ds.data(),
                                   0, nullptr);
        stats.sets++;

        const VkCommandBuffer cmd = cur->cmd;
        VKD.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VKD.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &set, 0,
                                    nullptr);
        for (u32 i = 0; i < kAttrs; i++) {
            if (vhas[i]) {
                VKD.vkCmdBindVertexBuffers(cmd, i, 1, &vbufs[i], &voffs[i]);
            }
        }
        SetDynamic(cmd);
        if (indexed) {
            VKD.vkCmdBindIndexBuffer(cmd, ibuf->snap_buffer, ibuf->snap_offset + index_offset,
                                     VK_INDEX_TYPE_UINT16);
            VKD.vkCmdDrawIndexed(cmd, count, 1, 0, 0, 0);
        } else {
            VKD.vkCmdDraw(cmd, count, 1, first, 0);
        }
        for (int a = 0; a < 3; a++) {
            pass_wrote[a] |= writes[a];
        }
        stats.draws++;
    }

    VkRect2D Area() const {
        VkRect2D r{{0, 0}, {pass_w, pass_h}};
        if (st.scissor_test) {
            const int x0 = std::clamp(st.scissor[0], 0, static_cast<int>(pass_w));
            const int y0 = std::clamp(st.scissor[1], 0, static_cast<int>(pass_h));
            const int x1 = std::clamp(st.scissor[0] + st.scissor[2], x0, static_cast<int>(pass_w));
            const int y1 = std::clamp(st.scissor[1] + st.scissor[3], y0, static_cast<int>(pass_h));
            r = {{x0, y0}, {static_cast<u32>(x1 - x0), static_cast<u32>(y1 - y0)}};
        }
        return r;
    }

    void SetDynamic(VkCommandBuffer cmd) {
        const VkViewport vp{static_cast<float>(st.viewport[0]),
                            static_cast<float>(st.viewport[1]),
                            static_cast<float>(st.viewport[2]),
                            static_cast<float>(st.viewport[3]),
                            0.0f,
                            1.0f};
        VKD.vkCmdSetViewport(cmd, 0, 1, &vp);
        const VkRect2D sc = Area();
        VKD.vkCmdSetScissor(cmd, 0, 1, &sc);
        VKD.vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                                       st.stencil_cmp_mask & 0xFF);
        VKD.vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                                     st.stencil_write_mask & 0xFF);
        VKD.vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                                     static_cast<u32>(st.stencil_ref) & 0xFF);
        VKD.vkCmdSetBlendConstants(cmd, st.blend_color);
    }

    void Clear(GLbitfield mask) {
        if (!healthy) {
            return;
        }
        Target t;
        if ((mask & GL_STENCIL_BUFFER_BIT) && (st.stencil_write_mask & 0xFF) != 0xFF) {
            // OpenGL clears only the bits of the stencil mask; Vulkan's clear takes them all
            ClearStencilBits();
            mask &= ~static_cast<GLbitfield>(GL_STENCIL_BUFFER_BIT);
        }
        if (!mask || !BeginPass(t)) {
            return;
        }
        VkClearAttachment at[3];
        u32 n = 0;
        if (mask & GL_COLOR_BUFFER_BIT) {
            u32 index = 0;
            for (int i = 0; i < 2; i++) {
                if (!t.tex[i]) {
                    continue;
                }
                at[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                at[n].colorAttachment = index++;
                std::memcpy(at[n].clearValue.color.float32, st.clear_color, sizeof(float) * 4);
                n++;
                pass_wrote[i] = true;
            }
        }
        if (t.tex[2] && (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))) {
            at[n].aspectMask = 0;
            if (mask & GL_DEPTH_BUFFER_BIT) {
                at[n].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            if ((mask & GL_STENCIL_BUFFER_BIT) && t.tex[2]->stencil) {
                at[n].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            at[n].colorAttachment = 0;
            at[n].clearValue.depthStencil = {st.clear_depth, 0};
            if (at[n].aspectMask) {
                n++;
                pass_wrote[2] = true;
            }
        }
        const VkClearRect rect{Area(), 0, 1};
        if (n && rect.rect.extent.width && rect.rect.extent.height) {
            VKD.vkCmdClearAttachments(cur->cmd, n, at, 1, &rect);
            stats.clears++;
        }
    }

    void ClearStencilBits();

    Tex* ColorOf(GLuint fbo, int& layer) {
        auto it = fbos.find(fbo);
        if (it == fbos.end()) {
            return nullptr;
        }
        auto ti = texs.find(it->second.color[0].tex);
        if (ti == texs.end() || !ti->second.image) {
            return nullptr;
        }
        layer = std::max(it->second.color[0].layer, 0);
        return &ti->second;
    }

    void Blit(int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1) {
        int sl = 0, dl = 0;
        Tex* s = ColorOf(st.read_fbo, sl);
        Tex* t = ColorOf(st.draw_fbo, dl);
        if (!s || !t || !healthy) {
            return;
        }
        EndPass();
        Barrier();
        const VkImageBlit region{{VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<u32>(sl), 1},
                                 {{sx0, sy0, 0}, {sx1, sy1, 1}},
                                 {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<u32>(dl), 1},
                                 {{dx0, dy0, 0}, {dx1, dy1, 1}}};
        VKD.vkCmdBlitImage(cur->cmd, s->image, VK_IMAGE_LAYOUT_GENERAL, t->image,
                           VK_IMAGE_LAYOUT_GENERAL, 1, &region, VK_FILTER_NEAREST);
        need_barrier = true;
    }

    /// Raw texels of a region, after the GPU is done
    bool Download(Tex& t, u32 layer, int x, int y, u32 w, u32 h, u32 bpp, std::vector<u8>& out) {
        if (!healthy || !t.image || t.depth) {
            return false;
        }
        EndPass();
        Barrier();
        VkBuffer buffer;
        VkDeviceSize offset;
        const VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * bpp;
        u8* p = Stream(size, buffer, offset);
        const VkBufferImageCopy region{
            offset, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1}, {x, y, 0}, {w, h, 1}};
        VKD.vkCmdCopyImageToBuffer(cur->cmd, t.image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &region);
        // copied out before the batch, and with it the chunk, is used again
        Submit(true, [&] {
            if (healthy) {
                out.assign(p, p + size);
            }
        });
        stats.readbacks++;
        return healthy && out.size() == size;
    }

    void ReadPixels(int x, int y, int w, int h, GLenum format, GLenum type, void* pixels) {
        int layer = 0;
        Tex* t = ColorOf(st.read_fbo, layer);
        if (!t || w <= 0 || h <= 0) {
            return;
        }
        std::vector<u8> raw;
        if (type == GL_UNSIGNED_SHORT_1_5_5_5_REV && t->format == VK_FORMAT_A1R5G5B5_UNORM_PACK16) {
            if (!Download(*t, static_cast<u32>(layer), x, y, static_cast<u32>(w),
                          static_cast<u32>(h), 2, raw)) {
                return;
            }
            // what was drawn as red is in Vulkan's red bits (10 to 14); OpenGL packs red lowest
            const u16* src = reinterpret_cast<const u16*>(raw.data());
            u16* dst = static_cast<u16*>(pixels);
            for (int i = 0; i < w * h; i++) {
                const u16 v = src[i];
                dst[i] = static_cast<u16>((v & 0x83E0) | ((v >> 10) & 0x1F) | ((v & 0x1F) << 10));
            }
        } else if (type == GL_UNSIGNED_BYTE && format == GL_RGBA &&
                   t->format == VK_FORMAT_R8G8B8A8_UNORM) {
            if (Download(*t, static_cast<u32>(layer), x, y, static_cast<u32>(w),
                         static_cast<u32>(h), 4, raw)) {
                std::memcpy(pixels, raw.data(), raw.size());
            }
        } else {
            Log(0, "glReadPixels of format %x type %x is not done", format, type);
        }
    }

    void Shutdown() {
        if (!d.device) {
            return;
        }
        EndPass();
        VKD.vkEndCommandBuffer(cur->cmd);
        {
            std::unique_lock<std::mutex> lock;
            if (d.queue_mutex) {
                lock = std::unique_lock<std::mutex>(*d.queue_mutex);
            }
            VKD.vkQueueWaitIdle(d.queue);
        }
        for (auto& [name, t] : texs) {
            ReleaseImage(t);
        }
        for (auto& [name, p] : programs) {
            FreeProgram(name, p);
        }
        for (auto& b : batches) {
            for (auto& g : b->garbage) {
                g();
            }
            for (VkDescriptorPool p : b->pools) {
                VKD.vkDestroyDescriptorPool(d.device, p, nullptr);
            }
            for (Chunk& c : b->chunks) {
                FreeChunk(c);
            }
            VKD.vkDestroyFence(d.device, b->fence, nullptr);
        }
        for (auto& [k, v] : pipelines) {
            VKD.vkDestroyPipeline(d.device, v, nullptr);
        }
        for (auto& [k, v] : framebuffers) {
            VKD.vkDestroyFramebuffer(d.device, v, nullptr);
        }
        for (auto& [k, v] : render_passes) {
            VKD.vkDestroyRenderPass(d.device, v, nullptr);
        }
        for (auto& [k, v] : samplers) {
            VKD.vkDestroySampler(d.device, v, nullptr);
        }
        VKD.vkDestroyCommandPool(d.device, pool, nullptr);
    }
};

namespace {
ShaderPatch g_shader_patch = nullptr;
Context* g_ctx = nullptr; ///< the gl* calls go to it (null: they do nothing)
}

void Context::ClearStencilBits() {
    // a triangle over the picture that replaces the masked bits by zero
    const State saved = st;
    if (!clear_program) {
        static const char* vs = "#version 140\nvoid main() { gl_Position = vec4(float((gl_VertexID "
                                "<< 1) & 2) * 2.0 - 1.0, float(gl_VertexID & 2) * 2.0 - 1.0, 0.0, "
                                "1.0); }\n";
        static const char* fs = "#version 140\nvoid main() {}\n";
        const GLuint v = glCreateShader(GL_VERTEX_SHADER);
        const GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(v, 1, &vs, nullptr);
        glShaderSource(f, 1, &fs, nullptr);
        clear_program = glCreateProgram();
        glAttachShader(clear_program, v);
        glAttachShader(clear_program, f);
        glLinkProgram(clear_program);
        glDeleteShader(v);
        glDeleteShader(f);
        glGenVertexArrays(1, &clear_vao);
    }
    st.program = clear_program;
    st.vao = clear_vao;
    st.depth_test = false;
    st.stencil_test = true;
    st.blend = false;
    st.color_mask[0] = st.color_mask[1] = 0;
    st.stencil_func = GL_ALWAYS;
    st.stencil_ref = 0;
    st.sfail = st.dpfail = st.dppass = GL_REPLACE;
    if (!st.scissor_test) {
        st.viewport[0] = st.viewport[1] = 0;
        st.viewport[2] = st.viewport[3] = 16384;
    }
    Draw(GL_TRIANGLES, 0, 3, false, 0);
    st = saved;
    stats.clears++;
}

Context* Create(const Device& device, std::string& why) {
    if (g_ctx) {
        why = "another is in use";
        return nullptr;
    }
    auto* c = new Context;
    g_ctx = c;
    c->d = device;
    if (!c->Init(why)) {
        if (why.empty()) {
            why = "the graphics device failed";
        }
        Destroy(c);
        return nullptr;
    }
    return c;
}

void Destroy(Context* c) {
    if (!c) {
        return;
    }
    if (g_ctx == c) {
        g_ctx = nullptr;
    }
    if (c->cur) {
        c->Shutdown();
    } else if (c->pool) {
        VKD.vkDestroyCommandPool(c->d.device, c->pool, nullptr);
    }
    if (c->pipeline_cache) {
        VKD.vkDestroyPipelineCache(c->d.device, c->pipeline_cache, nullptr);
    }
    delete c;
}

void UseState(int state) {
    if (Context* c = g_ctx; c && c->state != state) {
        std::swap(c->st, c->other);
        c->state = state;
    }
}

bool Compiles(const std::string& vertex, const std::string& fragment, std::string& log) {
    StartGlslang();
    std::string vs = vertex, fs = fragment;
    Context::Dialect(vs, fs);
    Context::Compiled compiled;
    return Context::Compile(vs, fs, compiled, log);
}

void PatchShaders(ShaderPatch patch) {
    g_shader_patch = patch;
}

void FreeState() {
    Context* c = g_ctx;
    if (!c) {
        return;
    }
    UseState(0);
    for (const GLuint name : std::exchange(c->made, {})) {
        if (name == c->clear_program || name == c->clear_vao) {
            continue; // the layer's own, for both
        }
        glDeleteFramebuffers(1, &name);
        glDeleteTextures(1, &name);
        glDeleteBuffers(1, &name);
        glDeleteVertexArrays(1, &name);
        glDeleteProgram(name);
        glDeleteShader(name);
    }
    c->other = {};
}

void Flush(bool wait) {
    if (g_ctx && g_ctx->healthy) {
        g_ctx->Submit(wait);
    }
}
bool Healthy() {
    return g_ctx && g_ctx->healthy;
}
const Stats& GetStats() {
    static Stats none;
    return g_ctx ? g_ctx->stats : none;
}
bool GetTexture(uint32_t name, Texture& out) {
    if (!g_ctx) {
        return false;
    }
    auto it = g_ctx->texs.find(name);
    if (it == g_ctx->texs.end() || !it->second.image) {
        return false;
    }
    const Tex& t = it->second;
    out = {t.image, t.view, t.format, t.w, t.h, t.layers};
    return true;
}
bool ReadTexture(uint32_t name, uint32_t layer, std::vector<uint32_t>& rgba) {
    if (!g_ctx) {
        return false;
    }
    auto it = g_ctx->texs.find(name);
    if (it == g_ctx->texs.end() || it->second.format != VK_FORMAT_R8G8B8A8_UNORM ||
        layer >= it->second.layers) {
        return false;
    }
    Tex& t = it->second;
    std::vector<u8> raw;
    if (!g_ctx->Download(t, layer, 0, 0, t.w, t.h, 4, raw)) {
        return false;
    }
    rgba.resize(static_cast<size_t>(t.w) * t.h);
    std::memcpy(rgba.data(), raw.data(), raw.size());
    return true;
}
void SetLog(void (*log)(int, const char*)) {
    g_log = log ? log : DefaultLog;
}

} // namespace DsGl

// ---------------------------------------------------------------------------------------------
// The OpenGL calls

using namespace DsGl;
#define CTX                                                                                        \
    Context* c = g_ctx;                                                                            \
    if (!c)                                                                                        \
    return
#define CTXR(v)                                                                                    \
    Context* c = g_ctx;                                                                            \
    if (!c)                                                                                        \
    return v

namespace {
template <typename M>
void Gen(Context* c, M& map, GLsizei n, GLuint* out) {
    for (GLsizei i = 0; i < n; i++) {
        out[i] = c->NewName();
        map[out[i]];
    }
}
Buf* TargetBuf(Context* c, GLenum target) {
    GLuint name = 0;
    if (target == GL_ARRAY_BUFFER) {
        name = c->st.array_buffer;
    } else if (target == GL_UNIFORM_BUFFER) {
        name = c->st.uniform_buffer;
    } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
        auto v = c->vaos.find(c->st.vao);
        name = v == c->vaos.end() ? 0 : v->second.elements;
    }
    auto it = c->bufs.find(name);
    return it == c->bufs.end() ? nullptr : &it->second;
}
Program* CurProgram(Context* c) {
    auto it = c->programs.find(c->st.program);
    return it == c->programs.end() ? nullptr : &it->second;
}
/// Locations: uniform index * kMaxSamplerArray + array element
void SetUniform(Context* c, GLint loc, const void* data, size_t size, bool is_int, int ivalue) {
    Program* p = CurProgram(c);
    if (!p || loc < 0 || static_cast<size_t>(loc / kMaxSamplerArray) >= p->uniforms.size()) {
        return;
    }
    const auto& u = p->uniforms[loc / kMaxSamplerArray];
    if (u.sampler) {
        if (is_int) {
            p->bindings[u.binding].units[loc % kMaxSamplerArray] = ivalue;
        }
        return;
    }
    if (u.offset + size <= p->defaults_buf.data.size() &&
        std::memcmp(p->defaults_buf.data.data() + u.offset, data, size) != 0) {
        std::memcpy(p->defaults_buf.data.data() + u.offset, data, size);
        p->defaults_buf.version++;
    }
}
} // namespace

void glActiveTexture(GLenum texture) {
    CTX;
    c->st.active_unit = (texture - GL_TEXTURE0) % kUnits;
}
void glBindTexture(GLenum target, GLuint texture) {
    CTX;
    c->st.units[c->st.active_unit][target == GL_TEXTURE_2D_ARRAY ? 1 : 0] = texture;
    auto it = c->texs.find(texture);
    if (it != c->texs.end() && !it->second.target) {
        it->second.target = target;
    }
}
void glGenTextures(GLsizei n, GLuint* textures) {
    CTX;
    Gen(c, c->texs, n, textures);
}
void glDeleteTextures(GLsizei n, const GLuint* textures) {
    CTX;
    for (GLsizei i = 0; i < n; i++) {
        auto it = c->texs.find(textures[i]);
        if (it == c->texs.end()) {
            continue;
        }
        c->ReleaseImage(it->second);
        c->texs.erase(it);
        for (auto& unit : c->st.units) {
            for (GLuint& b : unit) {
                if (b == textures[i]) {
                    b = 0;
                }
            }
        }
    }
}
void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    CTX;
    Tex* t = c->BoundTex(target);
    if (!t) {
        return;
    }
    if (pname == GL_TEXTURE_WRAP_S) {
        t->wrap_s = static_cast<GLenum>(param);
    } else if (pname == GL_TEXTURE_WRAP_T) {
        t->wrap_t = static_cast<GLenum>(param);
    } else if (pname == GL_TEXTURE_MAG_FILTER) {
        t->mag = static_cast<GLenum>(param);
    }
}
void glTexImage2D(GLenum target, GLint, GLint internalformat, GLsizei width, GLsizei height, GLint,
                  GLenum format, GLenum type, const void* pixels) {
    CTX;
    Tex* t = c->BoundTex(target);
    if (!t) {
        return;
    }
    c->Allocate(*t, target, static_cast<GLenum>(internalformat), static_cast<u32>(width),
                static_cast<u32>(height), 1);
    c->Upload(*t, 0, 0, 0, static_cast<u32>(width), static_cast<u32>(height), format, type, pixels);
}
void glTexImage3D(GLenum target, GLint, GLint internalformat, GLsizei width, GLsizei height,
                  GLsizei depth, GLint, GLenum format, GLenum type, const void* pixels) {
    CTX;
    Tex* t = c->BoundTex(target);
    if (!t) {
        return;
    }
    c->Allocate(*t, target, static_cast<GLenum>(internalformat), static_cast<u32>(width),
                static_cast<u32>(height), static_cast<u32>(depth));
    if (pixels) {
        const u32 bpp = Context::PixelBytes(format, type);
        for (GLsizei l = 0; l < depth; l++) {
            c->Upload(*t, 0, 0, static_cast<u32>(l), static_cast<u32>(width),
                      static_cast<u32>(height), format, type,
                      static_cast<const u8*>(pixels) +
                          static_cast<size_t>(l) * ((width * bpp + 3) & ~3u) * height);
        }
    }
}
void glTexStorage3D(GLenum target, GLsizei, GLenum internalformat, GLsizei width, GLsizei height,
                    GLsizei depth) {
    CTX;
    if (Tex* t = c->BoundTex(target)) {
        c->Allocate(*t, target, internalformat, static_cast<u32>(width), static_cast<u32>(height),
                    static_cast<u32>(depth));
    }
}
void glTexSubImage2D(GLenum target, GLint, GLint xoffset, GLint yoffset, GLsizei width,
                     GLsizei height, GLenum format, GLenum type, const void* pixels) {
    CTX;
    if (Tex* t = c->BoundTex(target)) {
        c->Upload(*t, static_cast<u32>(xoffset), static_cast<u32>(yoffset), 0,
                  static_cast<u32>(width), static_cast<u32>(height), format, type, pixels);
    }
}
void glTexSubImage3D(GLenum target, GLint, GLint xoffset, GLint yoffset, GLint zoffset,
                     GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type,
                     const void* pixels) {
    CTX;
    Tex* t = c->BoundTex(target);
    if (!t) {
        return;
    }
    const u32 bpp = Context::PixelBytes(format, type);
    for (GLsizei l = 0; l < depth; l++) {
        c->Upload(*t, static_cast<u32>(xoffset), static_cast<u32>(yoffset),
                  static_cast<u32>(zoffset + l), static_cast<u32>(width), static_cast<u32>(height),
                  format, type,
                  static_cast<const u8*>(pixels) +
                      static_cast<size_t>(l) * ((width * bpp + 3) & ~3u) * height);
    }
}

void glGenBuffers(GLsizei n, GLuint* buffers) {
    CTX;
    Gen(c, c->bufs, n, buffers);
}
void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    CTX;
    for (GLsizei i = 0; i < n; i++) {
        c->bufs.erase(buffers[i]);
    }
}
void glBindBuffer(GLenum target, GLuint buffer) {
    CTX;
    if (target == GL_ARRAY_BUFFER) {
        c->st.array_buffer = buffer;
    } else if (target == GL_UNIFORM_BUFFER) {
        c->st.uniform_buffer = buffer;
    } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
        c->vaos[c->st.vao].elements = buffer;
    }
}
void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    CTX;
    if (target == GL_UNIFORM_BUFFER && index < kUboPoints) {
        c->st.ubo_points[index] = buffer;
        c->st.uniform_buffer = buffer;
    }
}
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum) {
    CTX;
    if (Buf* b = TargetBuf(c, target)) {
        b->data.assign(static_cast<size_t>(size), 0);
        if (data) {
            std::memcpy(b->data.data(), data, static_cast<size_t>(size));
        }
        b->version++;
    }
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    CTX;
    Buf* b = TargetBuf(c, target);
    if (b && size > 0 && static_cast<size_t>(offset + size) <= b->data.size()) {
        std::memcpy(b->data.data() + offset, data, static_cast<size_t>(size));
        b->version++;
    }
}
void* glMapBuffer(GLenum target, GLenum) {
    CTXR(nullptr);
    Buf* b = TargetBuf(c, target);
    return b && !b->data.empty() ? b->data.data() : nullptr;
}
GLboolean glUnmapBuffer(GLenum target) {
    CTXR(GL_FALSE);
    if (Buf* b = TargetBuf(c, target)) {
        b->version++;
    }
    return GL_TRUE;
}

void glGenVertexArrays(GLsizei n, GLuint* arrays) {
    CTX;
    Gen(c, c->vaos, n, arrays);
}
void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    CTX;
    for (GLsizei i = 0; i < n; i++) {
        c->vaos.erase(arrays[i]);
    }
}
void glBindVertexArray(GLuint array) {
    CTX;
    c->st.vao = array;
}
void glEnableVertexAttribArray(GLuint index) {
    CTX;
    if (index < kAttrs) {
        c->vaos[c->st.vao].attr[index].enabled = true;
    }
}
namespace {
void AttribPointer(GLuint index, GLint size, GLenum type, bool integer, GLsizei stride,
                   const void* pointer) {
    CTX;
    if (index >= kAttrs || size < 1 || size > 4) {
        return;
    }
    auto& a = c->vaos[c->st.vao].attr[index];
    a.buffer = c->st.array_buffer;
    a.size = size;
    a.type = type;
    a.integer = integer;
    u32 bytes = 4;
    if (type == GL_BYTE || type == GL_UNSIGNED_BYTE) {
        bytes = 1;
    } else if (type == GL_SHORT || type == GL_UNSIGNED_SHORT) {
        bytes = 2;
    }
    a.stride = stride ? static_cast<u32>(stride) : bytes * static_cast<u32>(size);
    a.offset = static_cast<u32>(reinterpret_cast<uintptr_t>(pointer));
}
} // namespace
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean, GLsizei stride,
                           const void* pointer) {
    AttribPointer(index, size, type, false, stride, pointer);
}
void glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride,
                            const void* pointer) {
    AttribPointer(index, size, type, true, stride, pointer);
}

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    CTX;
    Gen(c, c->fbos, n, framebuffers);
}
void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    CTX;
    c->EndPass();
    for (GLsizei i = 0; i < n; i++) {
        c->fbos.erase(framebuffers[i]);
    }
}
void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    CTX;
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        c->st.draw_fbo = framebuffer;
    }
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) {
        c->st.read_fbo = framebuffer;
    }
}
void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint,
                               GLint layer) {
    CTX;
    auto it = c->fbos.find(target == GL_READ_FRAMEBUFFER ? c->st.read_fbo : c->st.draw_fbo);
    if (it == c->fbos.end()) {
        return;
    }
    c->EndPass();
    Fbo::Att att{texture, layer};
    if (attachment == GL_COLOR_ATTACHMENT0) {
        it->second.color[0] = att;
    } else if (attachment == GL_COLOR_ATTACHMENT1) {
        it->second.color[1] = att;
    } else if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        it->second.ds = att;
    }
}
void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    glFramebufferTextureLayer(target, attachment, texture, level, 0);
}
void glDrawBuffer(GLenum) {}
void glDrawBuffers(GLsizei, const GLenum*) {}
void glReadBuffer(GLenum) {}

void glEnable(GLenum cap) {
    CTX;
    switch (cap) {
    case GL_DEPTH_TEST:
        c->st.depth_test = true;
        break;
    case GL_STENCIL_TEST:
        c->st.stencil_test = true;
        break;
    case GL_BLEND:
        c->st.blend = true;
        break;
    case GL_SCISSOR_TEST:
        c->st.scissor_test = true;
        break;
    }
}
void glDisable(GLenum cap) {
    CTX;
    switch (cap) {
    case GL_DEPTH_TEST:
        c->st.depth_test = false;
        break;
    case GL_STENCIL_TEST:
        c->st.stencil_test = false;
        break;
    case GL_BLEND:
        c->st.blend = false;
        break;
    case GL_SCISSOR_TEST:
        c->st.scissor_test = false;
        break;
    }
}
void glDepthFunc(GLenum func) {
    CTX;
    c->st.depth_func = func;
}
void glDepthMask(GLboolean flag) {
    CTX;
    c->st.depth_mask = flag != 0;
}
void glDepthRange(GLdouble, GLdouble) {}
void glClearDepth(GLdouble depth) {
    CTX;
    c->st.clear_depth = static_cast<float>(depth);
}
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    CTX;
    c->st.clear_color[0] = red;
    c->st.clear_color[1] = green;
    c->st.clear_color[2] = blue;
    c->st.clear_color[3] = alpha;
}
void glClear(GLbitfield mask) {
    CTX;
    c->Clear(mask);
}
void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    CTX;
    c->st.stencil_func = func;
    c->st.stencil_ref = ref;
    c->st.stencil_cmp_mask = mask;
}
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    CTX;
    c->st.sfail = fail;
    c->st.dpfail = zfail;
    c->st.dppass = zpass;
}
void glStencilMask(GLuint mask) {
    CTX;
    c->st.stencil_write_mask = mask;
}
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha,
                         GLenum dfactorAlpha) {
    CTX;
    c->st.blend_src_rgb = sfactorRGB;
    c->st.blend_dst_rgb = dfactorRGB;
    c->st.blend_src_a = sfactorAlpha;
    c->st.blend_dst_a = dfactorAlpha;
}
void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    CTX;
    c->st.blend_eq_rgb = modeRGB;
    c->st.blend_eq_a = modeAlpha;
}
void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    CTX;
    c->st.blend_color[0] = red;
    c->st.blend_color[1] = green;
    c->st.blend_color[2] = blue;
    c->st.blend_color[3] = alpha;
}
void glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    CTX;
    if (index < 2) {
        c->st.color_mask[index] =
            static_cast<u8>((r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0));
    }
}
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    CTX;
    c->st.viewport[0] = x;
    c->st.viewport[1] = y;
    c->st.viewport[2] = width;
    c->st.viewport[3] = height;
}
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    CTX;
    c->st.scissor[0] = x;
    c->st.scissor[1] = y;
    c->st.scissor[2] = width;
    c->st.scissor[3] = height;
}
void glLineWidth(GLfloat) {}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    CTX;
    c->Draw(mode, static_cast<u32>(first), static_cast<u32>(count), false, 0);
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    CTX;
    if (type != GL_UNSIGNED_SHORT) {
        return;
    }
    c->Draw(mode, 0, static_cast<u32>(count), true,
            static_cast<u32>(reinterpret_cast<uintptr_t>(indices)));
}
void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0,
                       GLint dstX1, GLint dstY1, GLbitfield, GLenum) {
    CTX;
    c->Blit(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1);
}
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                  void* pixels) {
    CTX;
    c->ReadPixels(x, y, width, height, format, type, pixels);
}

GLuint glCreateShader(GLenum type) {
    CTXR(0);
    const GLuint name = c->NewName();
    c->shaders[name].type = type;
    return name;
}
void glDeleteShader(GLuint shader) {
    CTX;
    c->shaders.erase(shader);
}
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string,
                    const GLint* length) {
    CTX;
    auto it = c->shaders.find(shader);
    if (it == c->shaders.end()) {
        return;
    }
    it->second.source.clear();
    for (GLsizei i = 0; i < count; i++) {
        if (length && length[i] >= 0) {
            it->second.source.append(string[i], static_cast<size_t>(length[i]));
        } else {
            it->second.source.append(string[i]);
        }
    }
    if (g_shader_patch) {
        g_shader_patch(it->second.source);
    }
}
void glCompileShader(GLuint) {}
void glGetShaderiv(GLuint, GLenum pname, GLint* params) {
    // compiled when linked, where the names' places are known
    *params = pname == GL_COMPILE_STATUS ? 1 : 0;
}
void glGetShaderInfoLog(GLuint, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    if (bufSize > 0) {
        infoLog[0] = 0;
    }
    if (length) {
        *length = 0;
    }
}
GLuint glCreateProgram(void) {
    CTXR(0);
    const GLuint name = c->NewName();
    c->programs[name];
    return name;
}
void glDeleteProgram(GLuint program) {
    CTX;
    auto it = c->programs.find(program);
    if (it != c->programs.end()) {
        c->FreeProgram(program, it->second);
        c->programs.erase(it);
    }
}
void glAttachShader(GLuint program, GLuint shader) {
    CTX;
    auto it = c->programs.find(program);
    if (it != c->programs.end()) {
        it->second.shaders.push_back(shader);
    }
}
void glDetachShader(GLuint, GLuint) {}
void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    CTX;
    auto it = c->programs.find(program);
    if (it != c->programs.end()) {
        it->second.attrib_locs[name] = static_cast<int>(index);
    }
}
void glBindFragDataLocation(GLuint program, GLuint color, const GLchar* name) {
    CTX;
    auto it = c->programs.find(program);
    if (it != c->programs.end()) {
        it->second.frag_locs[name] = static_cast<int>(color);
    }
}
void glLinkProgram(GLuint program) {
    CTX;
    auto it = c->programs.find(program);
    if (it == c->programs.end()) {
        return;
    }
    Program& p = it->second;
    p.linked = c->Link(p);
    if (!p.linked) {
        Log(0, "a shader did not build: %s", p.log.c_str());
        return;
    }
    for (const auto& u : p.uniforms) {
        if (u.sampler) {
            const auto& b = p.bindings[u.binding];
            Log(2, "program %u: sampler %s at %u, %u of type %x", program, u.name.c_str(),
                b.binding, b.count, static_cast<unsigned>(b.gl_type));
        } else {
            Log(2, "program %u: uniform %s at byte %u", program, u.name.c_str(), u.offset);
        }
    }
    for (int i : p.blocks) {
        const auto& b = p.bindings[static_cast<size_t>(i)];
        Log(2, "program %u: block %s at %u, %u bytes", program, b.block.c_str(), b.binding, b.size);
    }
}
void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    CTX;
    auto it = c->programs.find(program);
    *params = 0;
    if (it == c->programs.end()) {
        return;
    }
    if (pname == GL_LINK_STATUS) {
        *params = it->second.linked ? 1 : 0;
    } else if (pname == GL_INFO_LOG_LENGTH) {
        *params = static_cast<GLint>(it->second.log.size());
    }
}
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    CTX;
    auto it = c->programs.find(program);
    const std::string log = it == c->programs.end() ? std::string() : it->second.log;
    const size_t n = bufSize > 0 ? std::min(log.size(), static_cast<size_t>(bufSize - 1)) : 0;
    if (bufSize > 0) {
        std::memcpy(infoLog, log.data(), n);
        infoLog[n] = 0;
    }
    if (length) {
        *length = static_cast<GLsizei>(n);
    }
}
void glGetProgramBinary(GLuint, GLsizei, GLsizei* length, GLenum*, void*) {
    if (length) {
        *length = 0;
    }
}
void glProgramBinary(GLuint, GLenum, const void*, GLsizei) {}
void glUseProgram(GLuint program) {
    CTX;
    c->st.program = program;
}
GLint glGetUniformLocation(GLuint program, const GLchar* name) {
    CTXR(-1);
    auto it = c->programs.find(program);
    if (it == c->programs.end()) {
        return -1;
    }
    std::string base = name;
    int element = 0;
    const size_t br = base.find('[');
    if (br != std::string::npos) {
        element = std::atoi(base.c_str() + br + 1);
        base.resize(br);
    }
    const auto& us = it->second.uniforms;
    for (size_t i = 0; i < us.size(); i++) {
        if (us[i].name == base) {
            return static_cast<GLint>(
                i * kMaxSamplerArray +
                (us[i].sampler ? std::clamp(element, 0, static_cast<int>(kMaxSamplerArray) - 1)
                               : 0));
        }
    }
    Log(2, "miss uniform %s", name);
    return -1;
}
GLuint glGetUniformBlockIndex(GLuint program, const GLchar* uniformBlockName) {
    CTXR(GL_INVALID_INDEX);
    auto it = c->programs.find(program);
    if (it == c->programs.end()) {
        return GL_INVALID_INDEX;
    }
    const auto& p = it->second;
    for (size_t i = 0; i < p.blocks.size(); i++) {
        if (p.bindings[p.blocks[i]].block == uniformBlockName) {
            return static_cast<GLuint>(i);
        }
    }
    Log(2, "miss block %s", uniformBlockName);
    return GL_INVALID_INDEX;
}
void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) {
    CTX;
    auto it = c->programs.find(program);
    if (it != c->programs.end() && uniformBlockIndex < it->second.blocks.size()) {
        it->second.bindings[it->second.blocks[uniformBlockIndex]].point = uniformBlockBinding;
    }
}
void glUniform1i(GLint location, GLint v0) {
    CTX;
    SetUniform(c, location, &v0, 4, true, v0);
}
void glUniform1ui(GLint location, GLuint v0) {
    CTX;
    SetUniform(c, location, &v0, 4, false, 0);
}
void glUniform1f(GLint location, GLfloat v0) {
    CTX;
    SetUniform(c, location, &v0, 4, false, 0);
}
void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    CTX;
    const float v[2] = {v0, v1};
    SetUniform(c, location, v, 8, false, 0);
}
void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
    CTX;
    const GLuint v[4] = {v0, v1, v2, v3};
    SetUniform(c, location, v, 16, false, 0);
}

// What only melonDS's compute renderer calls. It is compiled so that the renderer links as it is,
// and never made.
namespace {
[[noreturn]] void NotHere(const char* name) {
    Log(0, "%s: the compute renderer is not part of this", name);
    std::abort();
}
} // namespace
void glBindImageTexture(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum) {
    NotHere("glBindImageTexture");
}
void glBindSampler(GLuint, GLuint) {
    NotHere("glBindSampler");
}
void glDebugMessageCallback(GLDEBUGPROC, const void*) {}
void glDeleteSamplers(GLsizei, const GLuint*) {}
void glDispatchCompute(GLuint, GLuint, GLuint) {
    NotHere("glDispatchCompute");
}
void glDispatchComputeIndirect(GLintptr) {
    NotHere("glDispatchComputeIndirect");
}
void glGenSamplers(GLsizei, GLuint*) {
    NotHere("glGenSamplers");
}
void glMemoryBarrier(GLbitfield) {
    NotHere("glMemoryBarrier");
}
void glSamplerParameteri(GLuint, GLenum, GLint) {
    NotHere("glSamplerParameteri");
}
void glTexBuffer(GLenum, GLenum, GLuint) {
    NotHere("glTexBuffer");
}
void glTexStorage2D(GLenum, GLsizei, GLenum, GLsizei, GLsizei) {
    NotHere("glTexStorage2D");
}
