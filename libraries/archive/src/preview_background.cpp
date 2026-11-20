/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <algorithm>
#include "mm/preview.h"
#include "preview_background.h"
#include "palette_state.h"
#include "mm/background_camera.h"
#include "mm/indexed_display.h"
#include <cstring>
#include <new>
#undef size
ASSERTNAME
namespace MM
{
namespace Internal
{
void ResolveDisplayPalette(Color *palette)
{
    KauaiPixels::PaletteState display;
    display.Set(palette, 256, KauaiPixels::paletteIdentity);
    std::memcpy(palette, display.Colors(), 256 * sizeof(Color));
}
namespace
{
PreviewBackground &BackgroundHost(void *context)
{
    return *static_cast<PreviewBackground *>(static_cast<MMBackgroundPreviewGraph *>(context)->owner);
}
} // namespace

bool PreviewBackground::Load(const MMMovieSessionGraph &archive, ChunkNumber background, const Color *basePalette)
{
    mm_preview_projection_cache_reset(&cameraProjections);
    graph = archive;
    number = background;
    std::memcpy(palette, basePalette, sizeof(palette));
    MMBackgroundPreviewGraph host = {&graph, this};
    MMBackgroundServices s = {};
    s.context = &host;
    const unsigned short one = 1;
    s.host_big_endian = *reinterpret_cast<const byte *>(&one) == 0;
    s.retain_palette = 1;
    s.child = [](void *p, MMU32 type, MMU32 number, MMU32 id, MMU32 childType, MMU32 *out) {
        const auto graph = static_cast<MMBackgroundPreviewGraph *>(p)->graph;
        MMChunkAccess chunks;
        graph->access(graph->context, &chunks);
        return int(mm_chunk_link_number(&chunks, type, number, id, childType, out) == MM_CHUNK_OK);
    };
    s.payload = mm_background_preview_graph_payload;
    s.palette_color = [](void *p, MMI32 index, const unsigned char *bytes) {
        std::memcpy(&BackgroundHost(p).palette[index], bytes, 4);
    };
    s.resolve_palette = [](void *p) { ResolveDisplayPalette(BackgroundHost(p).palette); };
    s.configure_light = [](void *p, MMBackgroundResource *, MMI32, const MMBackgroundLight *light) {
        return mm_background_preview_light(&BackgroundHost(p).preview, light);
    };
    s.append_camera = [](void *p, MMU32 number) {
        return mm_background_preview_camera(&BackgroundHost(p).preview, number);
    };
    s.select_camera = [](void *p, MMI32 index) {
        try
        {
            return int(BackgroundHost(p).SetCamera(index));
        }
        catch (...)
        {
            return 0;
        }
    };
    mm_background_resource_dispose(&resource, nullptr);
    mm_background_resource_init(&resource);
    return mm_background_read(&resource, &preview.placement, kctgBkgd, background, MM_BACKGROUND_PREVIEW, &s) !=
           0;
}
void PreviewBackground::OverlayAuthoredPalette(unsigned char bgrx[1024]) const
{
    MMU32 count;
    MMI32 base;
    const auto bytes = mm_background_palette(&resource, &count, &base);
    mm_display_palette_overlay(bgrx, 256, base, bytes, count);
}

PreviewBackground::PreviewBackground()
{
    if (!mm_preview_projection_cache_init(&cameraProjections, nullptr) ||
        !mm_background_preview_init(&preview, nullptr))
        throw std::bad_alloc();
}
PreviewBackground::~PreviewBackground()
{
    mm_background_resource_dispose(&resource, nullptr);
    mm_preview_projection_cache_dispose(&cameraProjections);
    auto s = Services(nullptr);
    mm_background_preview_dispose(&preview, &s);
}
void PreviewBackground::Publish(const MMBackgroundPreview &p)
{
    width = p.width;
    height = p.height;
    cameras.Reset(reinterpret_cast<const ChunkNumber *>(p.metadata.buffers[0].data), p.camera_count);
    lights.Reset(reinterpret_cast<const MMBackgroundLight *>(p.metadata.buffers[1].data), p.light_count);
    colours.Reset(p.images.buffers[0].data, p.rgb_count);
    depths.Reset(reinterpret_cast<const std::uint16_t *>(p.images.buffers[1].data), p.depth_count);
}
bool PreviewBackground::CameraProjection(std::uint32_t slot, PreviewProjection &projection)
{
    projection = {};
    MMBackgroundPreviewGraph host = {&graph, this};
    auto services = Services(&host);
    MMPreviewProjection p;
    const auto result = mm_preview_projection_cache_get(&cameraProjections,
        reinterpret_cast<const MMU32 *>(cameras.data()), static_cast<MMU32>(cameras.size()), slot, &p, &services);
    if (result == MM_PREVIEW_CAMERA_MEMORY) throw std::bad_alloc();
    projection.valid = p.valid != 0;
    projection.orthographic = p.orthographic != 0;
    std::copy(p.transform, p.transform + 12, projection.transform);
    projection.verticalFov = p.vertical_fov; projection.aspect = p.aspect;
    projection.nearClip = p.near_clip; projection.farClip = p.far_clip;
    projection.displayDepth = p.display_depth; projection.orthoHeight = p.ortho_height;
    return result == MM_PREVIEW_CAMERA_OK;
}
MMBackgroundPreviewServices PreviewBackground::Services(MMBackgroundPreviewGraph *host)
{
    MMBackgroundPreviewServices s = {};
    mm_background_preview_bind_graph(&s, host);
    s.publish = [](void *p, const MMBackgroundPreview *state) {
        BackgroundHost(p).Publish(*state);
    };
    s.mesh_new = [](void *) -> void * {
        try
        {
            return new (std::nothrow) PreviewBackgroundMesh;
        }
        catch (...)
        {
            return nullptr;
        }
    };
    s.mesh_release = [](void *, void *p) { delete static_cast<PreviewBackgroundMesh *>(p); };
    s.mesh_build = [](void *, void *mesh, const MMBackgroundPreview *p, const double *transform,
                      const double *projection) {
        BackgroundMatrix t, q;
        std::memcpy(t.data(), transform, sizeof(t));
        std::memcpy(q.data(), projection, sizeof(q));
        try
        {
            static_cast<PreviewBackgroundMesh *>(mesh)->Build(
                p->width, p->height, p->images.buffers[0].data, p->rgb_count,
                reinterpret_cast<const std::uint16_t *>(p->images.buffers[1].data), p->depth_count, t, q);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    };
    s.scalar_float = [](void *, MMI32 value) -> float { return BrScalarToFloat(value); };
    s.float_scalar = [](void *, float value) -> MMI32 { return BrFloatToScalar(value); };
    s.perspective = [](void *, unsigned short fov, MMI32 aspect, MMI32 near_z, MMI32 far_z,
                       MMI32 out[16]) {
        br_matrix4 matrix;
        BrMatrix4Perspective(&matrix, fov, aspect, near_z, far_z);
        static_assert(sizeof(matrix) == 16 * sizeof(*out), "Projection matrix layout");
        std::memcpy(out, &matrix, sizeof(matrix));
    };
    return s;
}
bool PreviewBackground::SetCamera(std::uint32_t slot, bool updatePlacement)
{
    MMBackgroundPreviewGraph host = {&graph, this};
    auto s = Services(&host);
    s.child = nullptr;
    const int result = mm_background_preview_select_graph(&preview, &graph, number, slot, updatePlacement, &s);
    if (result == MM_BACKGROUND_PREVIEW_PLACEMENT_MEMORY)
        throw std::bad_alloc();
    return result > 0;
}
bool PreviewBackground::NextPlacementPoint(float point[3])
{
    MMActorPoint place;
    if (!point || !mm_background_preview_place(&preview, &place))
        return false;
    point[0] = BrScalarToFloat(place.dxr);
    point[1] = BrScalarToFloat(place.dyr);
    point[2] = BrScalarToFloat(place.dzr);
    return true;
}
void PreviewBackground::ReusePlacementPoint()
{
    mm_background_preview_reuse_place(&preview);
}
PreviewBackgroundMesh &PreviewBackground::Mesh()
{
    MMBackgroundPreviewGraph host = {&graph, this};
    auto s = Services(&host);
    auto mesh = static_cast<PreviewBackgroundMesh *>(mm_background_preview_mesh(&preview, &s));
    if (!mesh)
        throw std::bad_alloc();
    return *mesh;
}
} // namespace Internal
} // namespace MM
