/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "preview_background.h"
#include "zbmp.h"
#include "palette_state.h"
#include "engine_read_stream.h"
#include "three_mm/background_camera.h"
#include <cstring>
#include <new>
#undef size
ASSERTNAME
namespace ThreeMM
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
    source = file;
    number = background;
    std::memcpy(palette, basePalette, sizeof(palette));
    struct Host
    {
        PreviewBackground *background;
        PChunkyFile file;
        DataBlock block;
    } host = {this, source->Pcfl()};
    ThreeMMBackgroundServices s = {};
    s.context = &host;
    const unsigned short one = 1;
    s.host_big_endian = *reinterpret_cast<const byte *>(&one) == 0;
    s.child = [](void *p, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 id, ThreeMMU32 childType, ThreeMMU32 *out) {
        ChildChunkIdentification child;
        if (!static_cast<Host *>(p)->file->FGetKidChidCtg(type, number, id, childType, &child))
            return 0;
        *out = child.cki.cno;
        return 1;
    };
    s.find = [](void *p, ThreeMMU32 type, ThreeMMU32 number, int unpack) {
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
    s.count = [](void *, void *list) -> ThreeMMI32 { return static_cast<PDynamicArray>(list)->IvMac(); };
    s.stride = [](void *, void *list) -> ThreeMMI32 { return static_cast<PDynamicArray>(list)->CbEntry(); };
    s.get = [](void *, void *list, ThreeMMI32 i, void *out, size_t bytes) {
        std::memcpy(out, static_cast<PDynamicArray>(list)->QvGet(i), bytes);
    };
    s.release = [](void *, void *list) { static_cast<PDynamicArray>(list)->Release(); };
    s.palette_color = [](void *p, ThreeMMI32 index, const unsigned char *bytes) {
        std::memcpy(&static_cast<Host *>(p)->background->palette[index], bytes, 4);
    };
    s.resolve_palette = [](void *p) { ResolveDisplayPalette(static_cast<Host *>(p)->background->palette); };
    s.configure_light = [](void *p, ThreeMMBackgroundResource *, ThreeMMI32, const ThreeMMBackgroundLight *light) {
        return three_mm_background_preview_light(&static_cast<Host *>(p)->background->preview, light);
    };
    s.append_camera = [](void *p, ThreeMMU32 number) {
        return three_mm_background_preview_camera(&static_cast<Host *>(p)->background->preview, number);
    };
    s.select_camera = [](void *p, ThreeMMI32 index) {
        try
        {
            return int(static_cast<Host *>(p)->background->SetCamera(index));
        }
        catch (...)
        {
            return 0;
        }
    };
    three_mm_background_resource_init(&resource);
    three_mm_background_camera_init(&cameraState);
    return three_mm_background_read(&resource, &cameraState, kctgBkgd, background, THREE_MM_BACKGROUND_PREVIEW, &s) !=
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
} // namespace
PreviewBackground::PreviewBackground()
{
    if (!three_mm_background_preview_init(&preview, nullptr))
        throw std::bad_alloc();
}
PreviewBackground::~PreviewBackground()
{
    auto s = Services(nullptr);
    three_mm_background_preview_dispose(&preview, &s);
}
void PreviewBackground::Publish(const ThreeMMBackgroundPreview &p)
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
    lights.Reset(reinterpret_cast<const ThreeMMBackgroundLight *>(p.metadata.buffers[1].data), p.light_count);
    colours.Reset(p.images.buffers[0].data, p.rgb_count);
    depths.Reset(reinterpret_cast<const std::uint16_t *>(p.images.buffers[1].data), p.depth_count);
}
ThreeMMBackgroundPreviewServices PreviewBackground::Services(void *host)
{
    ThreeMMBackgroundPreviewServices s = {};
    s.context = host;
    s.camera_read = [](void *p, ThreeMMU32 number, ThreeMMReadStream *out) {
        auto h = static_cast<BackgroundImageHost *>(p);
        if (!h->source->Pcfl()->FFind(kctgCam, number, &h->block) || !h->block.FUnpackData())
            return 0;
        *out = EngineReadStream::FromBlock(h->block);
        return 1;
    };
    s.child = [](void *p, ThreeMMU32 camera, int depth, ThreeMMU32 *out) {
        auto h = static_cast<BackgroundImageHost *>(p);
        ChildChunkIdentification child;
        if (!h->source->Pcfl()->FGetKidChidCtg(kctgCam, camera, 0, depth ? kctgZbmp : kctgMbmp, &child))
            return 0;
        *out = child.cki.cno;
        return 1;
    };
    s.fetch = [](void *p, ThreeMMU32 number, int depth) -> void * {
        auto h = static_cast<BackgroundImageHost *>(p);
        return h->source->PbacoFetch(depth ? kctgZbmp : kctgMbmp, number,
                                     depth ? ZBMP::FReadZbmp : MaskedBitmapMBMP::FReadMbmp);
    };
    s.release = [](void *, void *p) { static_cast<PBaseCacheableObject>(p)->Release(); };
    s.bounds = [](void *, void *p, ThreeMMI32 out[4]) {
        RC r;
        static_cast<PMaskedBitmapMBMP>(p)->GetRc(&r);
        out[0] = r.xpLeft;
        out[1] = r.ypTop;
        out[2] = r.xpRight;
        out[3] = r.ypBottom;
    };
    s.draw_rgb = [](void *, void *bitmap, unsigned char *pixels, ThreeMMU32 w, ThreeMMU32 h, ThreeMMI32 x,
                    ThreeMMI32 y) { static_cast<PMaskedBitmapMBMP>(bitmap)->Draw(pixels, w, h, x, y); };
    s.draw_depth = [](void *, void *bitmap, unsigned short *pixels, ThreeMMU32 w, ThreeMMU32 h) {
        RC clip;
        clip.Set(0, 0, w, h);
        static_cast<PZBMP>(bitmap)->Draw(reinterpret_cast<byte *>(pixels), w * 2, h, 0, 0, &clip);
    };
    s.publish = [](void *p, const ThreeMMBackgroundPreview *state) {
        static_cast<BackgroundImageHost *>(p)->background->Publish(*state);
    };
    s.selected = [](void *p, ThreeMMU32 slot) {
        static_cast<BackgroundImageHost *>(p)->background->cameraState.current = slot;
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
    s.mesh_build = [](void *, void *mesh, const ThreeMMBackgroundPreview *p, const double *transform,
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
    s.scalar_float = [](void *, ThreeMMI32 value) -> float { return BrScalarToFloat(value); };
    s.float_scalar = [](void *, float value) -> ThreeMMI32 { return BrFloatToScalar(value); };
    s.perspective = [](void *, unsigned short fov, ThreeMMI32 aspect, ThreeMMI32 near_z, ThreeMMI32 far_z,
                       ThreeMMI32 out[16]) {
        br_matrix4 matrix;
        BrMatrix4Perspective(&matrix, fov, aspect, near_z, far_z);
        static_assert(sizeof(matrix) == 16 * sizeof(*out), "Projection matrix layout");
        std::memcpy(out, &matrix, sizeof(matrix));
    };
    return s;
}
bool PreviewBackground::SetCamera(std::uint32_t slot)
{
    BackgroundImageHost host = {this, source};
    auto s = Services(&host);
    return three_mm_background_preview_select(&preview, slot, &s) != 0;
}
PreviewBackgroundMesh &PreviewBackground::Mesh()
{
    BackgroundImageHost host = {this, source};
    auto s = Services(&host);
    auto mesh = static_cast<PreviewBackgroundMesh *>(three_mm_background_preview_mesh(&preview, &s));
    if (!mesh)
        throw std::bad_alloc();
    return *mesh;
}
} // namespace Internal
} // namespace ThreeMM
