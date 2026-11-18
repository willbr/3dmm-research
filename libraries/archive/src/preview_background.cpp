/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <algorithm>
#include "mm/preview.h"
#include "preview_background.h"
#include "zbmp.h"
#include "palette_state.h"
#include "engine_movie_graph.h"
#include "mm/background_camera.h"
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
bool PreviewBackground::Load(PChunkyResourceFile file, ChunkNumber background, const Color *basePalette)
{
    mm_preview_projection_cache_reset(&cameraProjections);
    source = file;
    number = background;
    std::memcpy(palette, basePalette, sizeof(palette));
    struct Host
    {
        PreviewBackground *background;
        PChunkyFile file;
    } host = {this, source->Pcfl()};
    MMBackgroundServices s = {};
    s.context = &host;
    const unsigned short one = 1;
    s.host_big_endian = *reinterpret_cast<const byte *>(&one) == 0;
    s.child = [](void *p, MMU32 type, MMU32 number, MMU32 id, MMU32 childType, MMU32 *out) {
        const auto chunks = static_cast<Host *>(p)->file->ReadChunkAccess();
        return int(mm_chunk_link_number(&chunks, type, number, id, childType, out) == MM_CHUNK_OK);
    };
    s.payload = [](void *p, MMU32 type, MMU32 number, MMReadReceiver read, void *out, int *result) {
        return mm_chunk_read_payload(KauaiChunkPayloadServices(), static_cast<Host *>(p)->file, type, number,
                                     read, out, result);
    };
    s.palette_color = [](void *p, MMI32 index, const unsigned char *bytes) {
        std::memcpy(&static_cast<Host *>(p)->background->palette[index], bytes, 4);
    };
    s.resolve_palette = [](void *p) { ResolveDisplayPalette(static_cast<Host *>(p)->background->palette); };
    s.configure_light = [](void *p, MMBackgroundResource *, MMI32, const MMBackgroundLight *light) {
        return mm_background_preview_light(&static_cast<Host *>(p)->background->preview, light);
    };
    s.append_camera = [](void *p, MMU32 number) {
        return mm_background_preview_camera(&static_cast<Host *>(p)->background->preview, number);
    };
    s.select_camera = [](void *p, MMI32 index) {
        try
        {
            return int(static_cast<Host *>(p)->background->SetCamera(index));
        }
        catch (...)
        {
            return 0;
        }
    };
    mm_background_resource_init(&resource);
    return mm_background_read(&resource, &preview.placement, kctgBkgd, background, MM_BACKGROUND_PREVIEW, &s) !=
           0;
}

namespace
{
struct BackgroundImageHost
{
    PreviewBackground *background;
    PChunkyResourceFile source;
};
} // namespace
PreviewBackground::PreviewBackground()
{
    if (!mm_preview_projection_cache_init(&cameraProjections, nullptr) ||
        !mm_background_preview_init(&preview, nullptr))
        throw std::bad_alloc();
}
PreviewBackground::~PreviewBackground()
{
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
    BackgroundImageHost host = {this, source};
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
MMBackgroundPreviewServices PreviewBackground::Services(void *host)
{
    MMBackgroundPreviewServices s = {};
    s.context = host;
    s.payload = [](void *p, MMU32 type, MMU32 number, MMReadReceiver read, void *out, int *result) {
        auto h = static_cast<BackgroundImageHost *>(p);
        return mm_chunk_read_payload(KauaiChunkPayloadServices(), h->source->Pcfl(), type, number, read, out, result);
    };
    s.child = [](void *p, MMU32 camera, int depth, MMU32 *out) {
        auto h = static_cast<BackgroundImageHost *>(p);
        const auto chunks = h->source->Pcfl()->ReadChunkAccess();
        return int(mm_background_camera_image_link(&chunks, camera, depth, out) == MM_CHUNK_OK);
    };
    s.fetch = [](void *p, MMU32 number, int depth) -> void * {
        auto h = static_cast<BackgroundImageHost *>(p);
        return h->source->PbacoFetch(depth ? kctgZbmp : kctgMbmp, number,
                                     depth ? ZBMP::FReadZbmp : MaskedBitmapMBMP::FReadMbmp);
    };
    s.release = [](void *, void *p) { static_cast<PBaseCacheableObject>(p)->Release(); };
    s.bounds = [](void *, void *p, MMI32 out[4]) {
        RC r;
        static_cast<PMaskedBitmapMBMP>(p)->GetRc(&r);
        out[0] = r.xpLeft;
        out[1] = r.ypTop;
        out[2] = r.xpRight;
        out[3] = r.ypBottom;
    };
    s.draw_rgb = [](void *, void *bitmap, unsigned char *pixels, MMU32 w, MMU32 h, MMI32 x,
                    MMI32 y) { static_cast<PMaskedBitmapMBMP>(bitmap)->Draw(pixels, w, h, x, y); };
    s.draw_depth = [](void *, void *bitmap, unsigned short *pixels, MMU32 w, MMU32 h) {
        RC clip;
        clip.Set(0, 0, w, h);
        static_cast<PZBMP>(bitmap)->Draw(reinterpret_cast<byte *>(pixels), w * 2, h, 0, 0, &clip);
    };
    s.publish = [](void *p, const MMBackgroundPreview *state) {
        static_cast<BackgroundImageHost *>(p)->background->Publish(*state);
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
    BackgroundImageHost host = {this, source};
    auto s = Services(&host);
    s.child = nullptr;
    struct Selection
    {
        MMBackgroundPreview *preview;
        MMU32 slot;
        bool placement;
        const MMBackgroundPreviewServices *services;
    } selection = {&preview, slot, updatePlacement, &s};
    const auto graph = EngineMovieGraph::FromFile(source->Pcfl());
    const int result = mm_movie_camera_graph(&graph, number, slot,
        [](void *p, const MMBackgroundCameraBinding *binding, const MMReadStream *stream) -> int {
            auto &s = *static_cast<Selection *>(p);
            const int applied = mm_background_preview_apply(s.preview, s.slot, binding, stream, s.placement, s.services);
            if (applied == MM_BACKGROUND_PREVIEW_PLACEMENT_MEMORY)
                return MM_MOVIE_MEMORY;
            return applied ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
        }, &selection, nullptr);
    if (result == MM_MOVIE_MEMORY)
        throw std::bad_alloc();
    return result == MM_MOVIE_OK;
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
    BackgroundImageHost host = {this, source};
    auto s = Services(&host);
    auto mesh = static_cast<PreviewBackgroundMesh *>(mm_background_preview_mesh(&preview, &s));
    if (!mesh)
        throw std::bad_alloc();
    return *mesh;
}
} // namespace Internal
} // namespace MM
