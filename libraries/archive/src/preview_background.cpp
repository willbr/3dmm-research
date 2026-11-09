/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "preview_background.h"
#include "zbmp.h"
#include <cstring>
#undef size
ASSERTNAME
namespace ThreeMM
{
namespace Internal
{
bool PreviewBackground::Load(PChunkyResourceFile file, ChunkNumber background, const Color *basePalette)
{
    source = file;
    number = background;
    std::memcpy(palette, basePalette, sizeof(palette));
    auto chunky = source->Pcfl();
    DataBlock block;
    BackgroundFile header;
    if (!chunky->FFind(kctgBkgd, background, &block) || !block.FUnpackData() || block.Cb() != sizeof(header) ||
        !block.FReadRgb(&header, sizeof(header), 0))
        return false;
    if (header.bo == kboOther)
        SwapBytesBom(&header, kbomBkgdf);
    if (header.bo != kboCur)
        return false;
    ChildChunkIdentification child;
    short order;
    if (chunky->FGetKidChidCtg(kctgBkgd, background, 0, kctgColorTable, &child))
    {
        if (!chunky->FFind(child.cki.ctg, child.cki.cno, &block))
            return false;
        auto colours = DynamicArray::PglRead(&block, &order);
        const bool valid =
            colours && colours->CbEntry() == sizeof(Color) && colours->IvMac() <= 256 - header.bIndexBase;
        if (valid)
        {
            if (order == kboOther)
                SwapBytesRglw(colours->QvGet(0), colours->IvMac());
            std::memcpy(palette + header.bIndexBase, colours->QvGet(0), colours->IvMac() * sizeof(Color));
        }
        ReleasePpo(&colours);
        if (!valid)
            return false;
    }
    if (!chunky->FGetKidChidCtg(kctgBkgd, background, 0, kctgGllt, &child) ||
        !chunky->FFind(child.cki.ctg, child.cki.cno, &block))
        return false;
    auto list = DynamicArray::PglRead(&block, &order);
    const bool valid = list && list->CbEntry() == sizeof(LightPosition) && list->IvMac() <= 16;
    if (valid)
        for (LegacyLong i = 0; i < list->IvMac(); ++i)
        {
            LightPosition light;
            list->Get(i, &light);
            if (order == kboOther)
                SwapBytesBom(&light, kbomLite);
            lights.push_back(light);
        }
    ReleasePpo(&list);
    if (!valid)
        return false;
    for (std::uint32_t slot = 0; chunky->FGetKidChidCtg(kctgBkgd, background, slot, kctgCam, &child); ++slot)
        cameras.push_back(child.cki.cno);
    return !cameras.empty() && SetCamera(0);
}
bool PreviewBackground::SetCamera(std::uint32_t slot)
{
    if (slot >= cameras.size())
        return false;
    auto file = source->Pcfl();
    DataBlock block;
    if (!file->FFind(kctgCam, cameras[slot], &block) || !block.FUnpackData() || block.Cb() < sizeof(camera) ||
        !block.FReadRgb(&camera, sizeof(camera), 0))
        return false;
    if (camera.bo == kboOther)
    {
        SwapBytesBom(&camera, kbomCam);
        SwapBytesRglw(&camera.bmat34Cam, sizeof(camera.bmat34Cam) / 4);
    }
    if (camera.bo != kboCur || camera.zrHither <= 0 || camera.zrYon <= camera.zrHither)
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
    return true;
}
} // namespace Internal
} // namespace ThreeMM
