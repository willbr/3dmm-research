/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "preview_background.h"
#include "zbmp.h"
#include "palette_state.h"
#include "engine_read_stream.h"
#include "three_mm/background_camera.h"
#include <cstring>
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
        LightPosition value;
        static_assert(sizeof(value.bmat34) == sizeof(light->matrix), "Light matrix layout");
        std::memcpy(&value.bmat34, &light->matrix, sizeof(value.bmat34));
        value.rIntensity = light->intensity;
        value.lt = light->type;
        try
        {
            static_cast<Host *>(p)->background->lights.push_back(value);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    };
    s.append_camera = [](void *p, ThreeMMU32 number) {
        try
        {
            static_cast<Host *>(p)->background->cameras.push_back(number);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
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
bool PreviewBackground::SetCamera(std::uint32_t slot)
{
    if (slot >= cameras.size())
        return false;
    auto file = source->Pcfl();
    DataBlock block;
    if (!file->FFind(kctgCam, cameras[slot], &block) || !block.FUnpackData())
        return false;
    auto stream = EngineReadStream::FromBlock(block);
    ThreeMMBackgroundCameraData decoded;
    if (!three_mm_background_camera_decode(&stream, &decoded))
        return false;
    // Native renderer view; wire decoding belongs to the shared C backend.
    camera.bo = kboCur;
    camera.zrHither = decoded.near_z;
    camera.zrYon = decoded.far_z;
    camera.aFov = decoded.fov;
    camera.apos.xrPlace = decoded.place.dxr;
    camera.apos.yrPlace = decoded.place.dyr;
    camera.apos.zrPlace = decoded.place.dzr;
    static_assert(sizeof(camera.bmat34Cam) == sizeof(decoded.matrix), "Camera matrix layout");
    std::memcpy(&camera.bmat34Cam, &decoded.matrix, sizeof(camera.bmat34Cam));
    if (camera.zrHither <= 0 || camera.zrYon <= camera.zrHither)
        return false;
    ChildChunkIdentification child;
    if (!file->FGetKidChidCtg(kctgCam, cameras[slot], 0, kctgMbmp, &child))
        return false;
    auto bitmap =
        static_cast<PMaskedBitmapMBMP>(source->PbacoFetch(child.cki.ctg, child.cki.cno, MaskedBitmapMBMP::FReadMbmp));
    if (!bitmap)
        return false;
    RC bounds;
    bitmap->GetRc(&bounds);
    // MBMP's reference point may be outside the image (including bottom-right
    // in shipped content). Normalize it into the top-left raster coordinate.
    const bool valid = bounds.Dxp() > 0 && bounds.Dyp() > 0 && bounds.Dxp() <= 1024 && bounds.Dyp() <= 1024;
    if (!valid)
    {
        ReleasePpo(&bitmap);
        return false;
    }
    width = bounds.Dxp();
    height = bounds.Dyp();
    colours.assign(std::size_t(width) * height, 0);
    bitmap->Draw(colours.data(), width, height, -bounds.xpLeft, -bounds.ypTop);
    ReleasePpo(&bitmap);
    if (!file->FGetKidChidCtg(kctgCam, cameras[slot], 0, kctgZbmp, &child))
        return false;
    auto depth = static_cast<PZBMP>(source->PbacoFetch(child.cki.ctg, child.cki.cno, ZBMP::FReadZbmp));
    if (!depth)
        return false;
    depths.assign(std::size_t(width) * height, 0xffff);
    // ZBMP::Draw requires a clip rectangle or region; both omitted means no
    // pixels are copied, leaving the background unable to occlude actors.
    RC depthClip;
    depthClip.Set(0, 0, width, height);
    depth->Draw(reinterpret_cast<byte *>(depths.data()), width * 2, height, 0, 0, &depthClip);
    ReleasePpo(&depth);
    activeCamera = slot;
    cameraState.current = slot;
    return true;
}
PreviewBackgroundMesh &PreviewBackground::Mesh()
{
    for (std::size_t i = 0; i < meshes.size(); ++i)
        if (meshes[i].camera == activeCamera)
        {
            auto cached = std::move(meshes[i]);
            meshes.erase(meshes.begin() + i);
            meshes.push_back(std::move(cached));
            return *meshes.back().mesh;
        }
    std::unique_ptr<PreviewBackgroundMesh> mesh(new PreviewBackgroundMesh);
    BackgroundMatrix transform = {}, projection = {};
    transform[15] = 1;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned axis = 0; axis < 3; ++axis)
            transform[row * 4 + axis] = BrScalarToFloat(camera.bmat34Cam.m[row][axis]);
    br_matrix4 matrix;
    BrMatrix4Perspective(&matrix, camera.aFov, BrFloatToScalar(float(width) / height), -camera.zrHither, -camera.zrYon);
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned axis = 0; axis < 4; ++axis)
            projection[row * 4 + axis] = BrScalarToFloat(matrix.m[row][axis]);
    mesh->Build(width, height, colours, depths, transform, projection);
    if (meshes.size() == 2)
        meshes.erase(meshes.begin());
    meshes.push_back({activeCamera, std::move(mesh)});
    return *meshes.back().mesh;
}
} // namespace Internal
} // namespace ThreeMM
