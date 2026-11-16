/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <algorithm>
#include "mm/preview.h"
#include "preview_background.h"
#include "zbmp.h"
#include "palette_state.h"
#include "engine_read_stream.h"
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
        DataBlock block;
    } host = {this, source->Pcfl()};
    MMBackgroundServices s = {};
    s.context = &host;
    const unsigned short one = 1;
    s.host_big_endian = *reinterpret_cast<const byte *>(&one) == 0;
    s.child = [](void *p, MMU32 type, MMU32 number, MMU32 id, MMU32 childType, MMU32 *out) {
        ChildChunkIdentification child;
        if (!static_cast<Host *>(p)->file->FGetKidChidCtg(type, number, id, childType, &child))
            return 0;
        *out = child.cki.cno;
        return 1;
    };
    s.find = [](void *p, MMU32 type, MMU32 number, int unpack) {
        auto h = static_cast<Host *>(p);
        return int(h->file->FFind(type, number, &h->block) && (!unpack || h->block.FUnpackData()));
    };
    s.stream = [](void *p) { return EngineReadStream::FromBlock(static_cast<Host *>(p)->block); };
    s.read_list = [](void *p, int *big) -> void * {
        short order = kboCur;
        auto list = DynamicArray::PglRead(&static_cast<Host *>(p)->block, &order);
        const unsigned short one = 1;
        *big = (*reinterpret_cast<const byte *>(&one) == 0) ^ (order == kboOther);
        return list;
    };
    s.count = [](void *, void *list) -> MMI32 { return static_cast<PDynamicArray>(list)->IvMac(); };
    s.stride = [](void *, void *list) -> MMI32 { return static_cast<PDynamicArray>(list)->CbEntry(); };
    s.get = [](void *, void *list, MMI32 i, void *out, size_t bytes) {
        std::memcpy(out, static_cast<PDynamicArray>(list)->QvGet(i), bytes);
    };
    s.release = [](void *, void *list) { static_cast<PDynamicArray>(list)->Release(); };
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
    mm_background_camera_init(&cameraState);
    return mm_background_read(&resource, &cameraState, kctgBkgd, background, MM_BACKGROUND_PREVIEW, &s) !=
           0;
}

namespace
{
struct BackgroundImageHost
{
    PreviewBackground *background;
    PChunkyResourceFile source;
    DataBlock block;
};
MMBackgroundCameraServices PlacementServices()
{
    MMBackgroundCameraServices s = {};
    s.point = [](void *, void *points, MMI32 i, MMActorPoint *out) {
        *out = (*static_cast<std::vector<MMActorPoint> *>(points))[i];
    };
    s.point_count = [](void *, void *points) -> MMI32 {
        return static_cast<MMI32>(static_cast<std::vector<MMActorPoint> *>(points)->size());
    };
    return s;
}
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
    camera.bo = kboCur;
    camera.zrHither = p.camera.near_z;
    camera.zrYon = p.camera.far_z;
    camera.aFov = p.camera.fov;
    camera.apos.xrPlace = p.camera.place.dxr;
    camera.apos.yrPlace = p.camera.place.dyr;
    camera.apos.zrPlace = p.camera.place.dzr;
    static_assert(sizeof(camera.bmat34Cam) == sizeof(p.camera.matrix), "Camera matrix layout");
    std::memcpy(&camera.bmat34Cam, &p.camera.matrix, sizeof(camera.bmat34Cam));
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
    s.camera_read = [](void *p, MMU32 number, MMReadStream *out) {
        auto h = static_cast<BackgroundImageHost *>(p);
        if (!h->source->Pcfl()->FFind(kctgCam, number, &h->block) || !h->block.FUnpackData())
            return 0;
        *out = EngineReadStream::FromBlock(h->block);
        return 1;
    };
    s.child = [](void *p, MMU32 camera, int depth, MMU32 *out) {
        auto h = static_cast<BackgroundImageHost *>(p);
        ChildChunkIdentification child;
        if (!h->source->Pcfl()->FGetKidChidCtg(kctgCam, camera, 0, depth ? kctgZbmp : kctgMbmp, &child))
            return 0;
        *out = child.cki.cno;
        return 1;
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
    MMReadStream stream;
    MMBackgroundCameraData data = {};
    std::vector<MMActorPoint> points;
    if (updatePlacement)
    {
        if (slot >= preview.camera_count || !s.camera_read(&host,
            reinterpret_cast<const MMU32 *>(preview.metadata.buffers[0].data)[slot], &stream) ||
            !mm_background_camera_decode(&stream, &data)) return false;
        points.resize(data.point_count);
        for (MMI32 i = 0; i < data.point_count; ++i)
            if (!mm_background_point_decode(&stream, &data, i, &points[i])) return false;
    }
    if (!mm_background_preview_select(&preview, slot, &s)) return false;
    if (updatePlacement)
    {
        placementPoints = std::move(points);
        cameraState.points = placementPoints.empty() ? nullptr : &placementPoints;
        cameraState.place = data.place; cameraState.current = slot;
        cameraState.next_point = cameraState.last_point = 0;
    }
    return true;
}
bool PreviewBackground::NextPlacementPoint(float point[3])
{
    if (!point || cameraState.current < 0) return false;
    auto s = PlacementServices();
    MMActorPoint place;
    mm_background_place(&cameraState, &place, &s);
    point[0] = BrScalarToFloat(place.dxr);
    point[1] = BrScalarToFloat(place.dyr);
    point[2] = BrScalarToFloat(place.dzr);
    return true;
}
void PreviewBackground::ReusePlacementPoint()
{
    mm_background_reuse_place(&cameraState);
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
