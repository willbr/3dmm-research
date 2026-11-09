#include "gpu_renderer.h"
#include "zb_capture.h"
#include "gpu_shaders.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace
{
bool Fail(ThreeMM::Error &error, const char *message)
{
    error.code = ThreeMM::ErrorCode::decode_failed;
    error.message = std::string(message) + ": " + SDL_GetError();
    return false;
}
struct Vertex
{
    float x, y, z, q;
};
struct RasterTriangle
{
    Vertex position[3], varying[3];
    Uint32 texture[4], shade[4];
};
static_assert(sizeof(RasterTriangle) == 128, "HLSL triangle layout");
struct Parameters
{
    Uint32 width, height, tilesX, background;
};
struct Resources
{
    SDL_GPUDevice *device;
    std::vector<SDL_GPUBuffer *> buffers;
    std::vector<SDL_GPUTransferBuffer *> transfers;
    SDL_GPUTexture *colour = nullptr, *depth = nullptr;
    ~Resources()
    {
        for (auto buffer : buffers)
            SDL_ReleaseGPUBuffer(device, buffer);
        for (auto buffer : transfers)
            SDL_ReleaseGPUTransferBuffer(device, buffer);
        if (colour)
            SDL_ReleaseGPUTexture(device, colour);
        if (depth)
            SDL_ReleaseGPUTexture(device, depth);
    }
    SDL_GPUBuffer *Buffer(Uint32 size, SDL_GPUBufferUsageFlags usage)
    {
        SDL_GPUBufferCreateInfo info = {};
        info.size = std::max(128u, size);
        info.usage = usage;
        auto result = SDL_CreateGPUBuffer(device, &info);
        if (result)
            buffers.push_back(result);
        return result;
    }
    SDL_GPUTransferBuffer *Transfer(Uint32 size, SDL_GPUTransferBufferUsage usage)
    {
        SDL_GPUTransferBufferCreateInfo info = {};
        info.size = std::max(128u, size);
        info.usage = usage;
        auto result = SDL_CreateGPUTransferBuffer(device, &info);
        if (result)
            transfers.push_back(result);
        return result;
    }
};
class Renderer final : public ThreeMM::PreviewRasterizer
{
    SDL_GPUDevice *device = nullptr;
    SDL_GPUComputePipeline *computePipeline = nullptr;
    SDL_GPUGraphicsPipeline *graphicsPipeline = nullptr;
    bool compute, video = false, failed = false;
    unsigned width = 0, height = 0;
    std::vector<RasterTriangle> triangles;
    std::vector<Uint32> texels;
    using MapKey = std::tuple<const unsigned char *, int, int, int>;
    std::map<MapKey, Uint32> maps;

    Uint32 Map(const BrRasterMap &map)
    {
        if (!map.pixels || map.width <= 0 || map.height <= 0)
            return 0;
        MapKey key(map.pixels, map.width, map.height, map.stride);
        auto found = maps.find(key);
        if (found != maps.end())
            return found->second;
        const auto size = std::size_t(map.width) * map.height;
        if (size > 16 * 1024 * 1024 || texels.size() + size > 16 * 1024 * 1024)
            throw std::runtime_error("GPU texture budget exceeded");
        auto offset = static_cast<Uint32>(texels.size());
        for (int y = 0; y < map.height; ++y)
            for (int x = 0; x < map.width; ++x)
                texels.push_back(map.pixels[y * map.stride + x]);
        maps.emplace(key, offset);
        return offset;
    }
    std::vector<Uint32> Bins() const
    {
        const unsigned columns = (width + 15) / 16, rows = (height + 15) / 16;
        std::vector<std::vector<Uint32>> tiles(columns * rows);
        std::size_t entries = 0;
        for (Uint32 i = 0; i < triangles.size(); ++i)
        {
            const auto &p = triangles[i].position;
            int x0 = std::max(0, int(std::floor(std::min({p[0].x, p[1].x, p[2].x}) / 16)));
            int y0 = std::max(0, int(std::floor(std::min({p[0].y, p[1].y, p[2].y}) / 16)));
            int x1 = std::min(int(columns) - 1, int(std::floor(std::max({p[0].x, p[1].x, p[2].x}) / 16)));
            int y1 = std::min(int(rows) - 1, int(std::floor(std::max({p[0].y, p[1].y, p[2].y}) / 16)));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                {
                    if (++entries > 16 * 1024 * 1024)
                        throw std::runtime_error("GPU tile budget exceeded");
                    tiles[y * columns + x].push_back(i);
                }
        }
        std::vector<Uint32> result(tiles.size() + 1);
        result.reserve(result.size() + entries);
        for (unsigned i = 0; i < tiles.size(); ++i)
        {
            result[i] = static_cast<Uint32>(result.size());
            result.insert(result.end(), tiles[i].begin(), tiles[i].end());
        }
        result[tiles.size()] = static_cast<Uint32>(result.size());
        return result;
    }

  public:
    explicit Renderer(bool value) : compute(value)
    {
    }
    ~Renderer() override
    {
        if (computePipeline)
            SDL_ReleaseGPUComputePipeline(device, computePipeline);
        if (graphicsPipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, graphicsPipeline);
        if (device)
            SDL_DestroyGPUDevice(device);
        if (video)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    bool Init(ThreeMM::Error &error)
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            return Fail(error, "Cannot initialize GPU video");
        video = true;
        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL, false, nullptr);
        if (!device)
            return Fail(error, "Cannot create an SDL GPU device");
        const bool dxil = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) != 0;
        const auto format = dxil ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
        if (compute)
        {
            SDL_GPUComputePipelineCreateInfo info = {};
            info.code = dxil ? compute_dxil : compute_spv;
            info.code_size = dxil ? sizeof(compute_dxil) : sizeof(compute_spv);
            info.entrypoint = "main";
            info.format = format;
            info.num_readonly_storage_buffers = 3;
            info.num_readwrite_storage_buffers = 1;
            info.num_uniform_buffers = 1;
            info.threadcount_x = info.threadcount_y = 8;
            info.threadcount_z = 1;
            computePipeline = SDL_CreateGPUComputePipeline(device, &info);
            return computePipeline || Fail(error, "Cannot create BRender compute pipeline");
        }
        if (!SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) ||
            !SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        {
            error.code = ThreeMM::ErrorCode::unsupported_format;
            error.message = "GPU does not support the indexed colour and depth targets.";
            return false;
        }
        SDL_GPUShaderCreateInfo shaderInfo = {};
        shaderInfo.code = dxil ? vertex_dxil : vertex_spv;
        shaderInfo.code_size = dxil ? sizeof(vertex_dxil) : sizeof(vertex_spv);
        shaderInfo.entrypoint = "main";
        shaderInfo.format = format;
        shaderInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        shaderInfo.num_storage_buffers = 1;
        shaderInfo.num_uniform_buffers = 1;
        auto vertex = SDL_CreateGPUShader(device, &shaderInfo);
        if (!vertex)
            return Fail(error, "Cannot create GPU vertex shader");
        shaderInfo.code = dxil ? fragment_dxil : fragment_spv;
        shaderInfo.code_size = dxil ? sizeof(fragment_dxil) : sizeof(fragment_spv);
        shaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        shaderInfo.num_storage_buffers = 3;
        auto fragment = SDL_CreateGPUShader(device, &shaderInfo);
        if (!fragment)
        {
            SDL_ReleaseGPUShader(device, vertex);
            return Fail(error, "Cannot create GPU fragment shader");
        }
        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader = vertex;
        info.fragment_shader = fragment;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        SDL_GPUColorTargetDescription target = {};
        target.format = SDL_GPU_TEXTUREFORMAT_R32_UINT;
        info.target_info.color_target_descriptions = &target;
        info.target_info.num_color_targets = 1;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        info.target_info.has_depth_stencil_target = true;
        graphicsPipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
        SDL_ReleaseGPUShader(device, vertex);
        SDL_ReleaseGPUShader(device, fragment);
        return graphicsPipeline || Fail(error, "Cannot create SDL GPU graphics pipeline");
    }
    bool Begin(unsigned w, unsigned h, ThreeMM::Error &) override
    {
        width = w;
        height = h;
        failed = false;
        triangles.clear();
        texels.clear();
        maps.clear();
        return true;
    }
    void Triangle(const BrRasterTriangle &input) noexcept override
    {
        if (failed)
            return;
        try
        {
            if (triangles.size() >= 1024 * 1024)
                throw std::runtime_error("GPU triangle budget exceeded");
            RasterTriangle out = {};
            out.texture[0] = Map(input.texture);
            out.texture[1] = input.texture.pixels ? input.texture.width : 0;
            out.texture[2] = input.texture.height;
            out.texture[3] = input.flags;
            out.shade[0] = Map(input.shade);
            out.shade[1] = input.shade.pixels ? input.shade.width : 0;
            out.shade[2] = input.shade.height;
            for (int i = 0; i < 3; ++i)
            {
                const auto &v = input.vertices[i];
                const float q = v.w > 0 ? 1.f / v.w : 1.f;
                out.position[i] = {v.x, v.y, v.z, q};
                const float perspective = (input.flags & 0x20) ? q : 1.f;
                out.varying[i] = {v.u * perspective, v.v * perspective, v.intensity, 0};
            }
            triangles.push_back(out);
        }
        catch (...)
        {
            failed = true;
        }
    }
    bool End(unsigned char *colour, int colourStride, unsigned char *depth, int depthStride,
             ThreeMM::Error &error) override
    {
        if (failed)
        {
            error.code = ThreeMM::ErrorCode::resource_limit;
            error.message = "Cannot collect GPU geometry: memory or scene budget exceeded.";
            return false;
        }
        const Uint32 pixelBytes = width * height * 4;
        std::vector<Uint32> seed(width * height);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
            {
                Uint16 z;
                std::memcpy(&z, depth + y * depthStride + x * 2, 2);
                seed[y * width + x] = (Uint32(z) << 8) | colour[y * colourStride + x];
            }
        auto bins = compute ? Bins() : std::vector<Uint32>();
        Resources resources{device};
        const SDL_GPUBufferUsageFlags read =
            compute ? SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ : SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        auto triangleBuffer = resources.Buffer(static_cast<Uint32>(triangles.size() * sizeof(RasterTriangle)), read);
        auto texelBuffer = resources.Buffer(static_cast<Uint32>(texels.size() * 4), read);
        auto thirdBuffer = resources.Buffer(compute ? static_cast<Uint32>(bins.size() * 4) : pixelBytes, read);
        auto resultBuffer = compute ? resources.Buffer(pixelBytes, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE) : nullptr;
        auto download = resources.Transfer(pixelBytes, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD);
        if (!triangleBuffer || !texelBuffer || !thirdBuffer || !download || (compute && !resultBuffer))
            return Fail(error, "Cannot allocate GPU buffers");
        struct Upload
        {
            SDL_GPUBuffer *buffer;
            const void *data;
            Uint32 size;
            SDL_GPUTransferBuffer *transfer;
        };
        std::vector<Upload> uploads = {
            {triangleBuffer, triangles.data(), static_cast<Uint32>(triangles.size() * sizeof(RasterTriangle)), nullptr},
            {texelBuffer, texels.data(), static_cast<Uint32>(texels.size() * 4), nullptr},
            {thirdBuffer, compute ? bins.data() : seed.data(),
             compute ? static_cast<Uint32>(bins.size() * 4) : pixelBytes, nullptr}};
        if (compute)
            uploads.push_back({resultBuffer, seed.data(), pixelBytes, nullptr});
        for (auto &upload : uploads)
        {
            if (!upload.size)
                continue;
            upload.transfer = resources.Transfer(upload.size, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
            auto mapped = upload.transfer ? SDL_MapGPUTransferBuffer(device, upload.transfer, false) : nullptr;
            if (!mapped)
                return Fail(error, "Cannot map GPU upload");
            std::memcpy(mapped, upload.data, upload.size);
            SDL_UnmapGPUTransferBuffer(device, upload.transfer);
        }
        if (!compute)
        {
            SDL_GPUTextureCreateInfo info = {};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = SDL_GPU_TEXTUREFORMAT_R32_UINT;
            info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            info.width = width;
            info.height = height;
            info.layer_count_or_depth = info.num_levels = 1;
            info.sample_count = SDL_GPU_SAMPLECOUNT_1;
            resources.colour = SDL_CreateGPUTexture(device, &info);
            info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
            info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            resources.depth = SDL_CreateGPUTexture(device, &info);
            if (!resources.colour || !resources.depth)
                return Fail(error, "Cannot allocate GPU render targets");
        }
        auto commands = SDL_AcquireGPUCommandBuffer(device);
        if (!commands)
            return Fail(error, "Cannot acquire GPU commands");
        // Cancel unfinished commands on every early exit, including exceptions.
        struct CommandScope
        {
            SDL_GPUCommandBuffer *commands;
            ~CommandScope()
            {
                if (commands)
                    SDL_CancelGPUCommandBuffer(commands);
            }
        } commandScope{commands};
        auto copy = SDL_BeginGPUCopyPass(commands);
        if (!copy)
            return Fail(error, "Cannot begin GPU upload pass");
        for (const auto &upload : uploads)
            if (upload.size)
            {
                SDL_GPUTransferBufferLocation source = {upload.transfer, 0};
                SDL_GPUBufferRegion target = {upload.buffer, 0, upload.size};
                SDL_UploadToGPUBuffer(copy, &source, &target, false);
            }
        SDL_EndGPUCopyPass(copy);
        Parameters parameters{width, height, (width + 15) / 16, 0};
        SDL_GPUBuffer *storage[] = {triangleBuffer, texelBuffer, thirdBuffer};
        if (compute)
        {
            SDL_GPUStorageBufferReadWriteBinding output = {resultBuffer, false};
            auto pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &output, 1);
            if (!pass)
                return Fail(error, "Cannot begin GPU compute pass");
            SDL_BindGPUComputePipeline(pass, computePipeline);
            SDL_BindGPUComputeStorageBuffers(pass, 0, storage, 3);
            SDL_PushGPUComputeUniformData(commands, 0, &parameters, sizeof(parameters));
            SDL_DispatchGPUCompute(pass, (width + 7) / 8, (height + 7) / 8, 1);
            SDL_EndGPUComputePass(pass);
        }
        else
        {
            SDL_GPUColorTargetInfo target = {};
            target.texture = resources.colour;
            target.load_op = SDL_GPU_LOADOP_DONT_CARE;
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPUDepthStencilTargetInfo z = {};
            z.texture = resources.depth;
            z.clear_depth = 1;
            z.load_op = SDL_GPU_LOADOP_CLEAR;
            z.store_op = SDL_GPU_STOREOP_DONT_CARE;
            z.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            z.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            auto pass = SDL_BeginGPURenderPass(commands, &target, 1, &z);
            if (!pass)
                return Fail(error, "Cannot begin GPU render pass");
            SDL_BindGPUGraphicsPipeline(pass, graphicsPipeline);
            SDL_BindGPUVertexStorageBuffers(pass, 0, storage, 1);
            SDL_BindGPUFragmentStorageBuffers(pass, 0, storage, 3);
            parameters.background = 1;
            SDL_PushGPUVertexUniformData(commands, 0, &parameters, sizeof(parameters));
            SDL_PushGPUFragmentUniformData(commands, 0, &parameters, sizeof(parameters));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            parameters.background = 0;
            SDL_PushGPUVertexUniformData(commands, 0, &parameters, sizeof(parameters));
            SDL_PushGPUFragmentUniformData(commands, 0, &parameters, sizeof(parameters));
            if (!triangles.empty())
                SDL_DrawGPUPrimitives(pass, static_cast<Uint32>(triangles.size() * 3), 1, 0, 0);
            SDL_EndGPURenderPass(pass);
        }
        copy = SDL_BeginGPUCopyPass(commands);
        if (!copy)
            return Fail(error, "Cannot begin GPU download pass");
        if (compute)
        {
            SDL_GPUBufferRegion source = {resultBuffer, 0, pixelBytes};
            SDL_GPUTransferBufferLocation target = {download, 0};
            SDL_DownloadFromGPUBuffer(copy, &source, &target);
        }
        else
        {
            SDL_GPUTextureRegion source = {};
            source.texture = resources.colour;
            source.w = width;
            source.h = height;
            source.d = 1;
            SDL_GPUTextureTransferInfo target = {};
            target.transfer_buffer = download;
            target.pixels_per_row = width;
            target.rows_per_layer = height;
            SDL_DownloadFromGPUTexture(copy, &source, &target);
        }
        SDL_EndGPUCopyPass(copy);
        auto fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        commandScope.commands = nullptr;
        if (!fence)
            return Fail(error, "Cannot submit GPU frame");
        const bool waited = SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);
        if (!waited)
            return Fail(error, "Cannot wait for GPU frame");
        auto pixels = static_cast<const Uint32 *>(SDL_MapGPUTransferBuffer(device, download, false));
        if (!pixels)
            return Fail(error, "Cannot read GPU frame");
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
            {
                const Uint32 value = pixels[y * width + x];
                colour[y * colourStride + x] = static_cast<unsigned char>(value);
                const Uint16 z = static_cast<Uint16>(value >> 8);
                std::memcpy(depth + y * depthStride + x * 2, &z, 2);
            }
        SDL_UnmapGPUTransferBuffer(device, download);
        return true;
    }
};
} // namespace
std::unique_ptr<ThreeMM::PreviewRasterizer> MarcusCreateGPURenderer(bool compute, ThreeMM::Error &error)
{
    std::unique_ptr<Renderer> result(new Renderer(compute));
    if (!result->Init(error))
        return nullptr;
    return result;
}
