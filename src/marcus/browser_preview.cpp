#include "browser_preview.h"
#include "three_mm/archive.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "soc_core.h"
#undef size
ASSERTNAME

BrowserPreview ReadBrowserPreview(const std::string &movie, const std::string &resources)
{
    BrowserPreview result;
    ThreeMM::Archive archive({16 * 1024 * 1024, 1024 * 1024});
    ThreeMM::Error error;
    if (!archive.Open(movie, error))
    {
        result.error = error.message;
        return result;
    }
    std::vector<ThreeMM::Id> scenes;
    for (const auto &chunk : archive.Chunks())
        if (chunk.id.type == kctgMvie)
        {
            result.title = chunk.name;
            for (const auto &child : chunk.children)
                if (child.target.type == kctgScen && child.present)
                    scenes.push_back(child.target);
            break;
        }
    long long frames = 0;
    ThreeMM::Id thumb;
    for (const auto &id : scenes)
        for (std::size_t i = 0; i < archive.Chunks().size(); ++i)
            if (archive.Chunks()[i].id == id)
            {
                ThreeMM::RecordHeader record;
                if (archive.ReadRecord(i, record, error) && record.kind == ThreeMM::RecordKind::scene)
                    frames += std::max(0LL, static_cast<long long>(record.scene.lastFrame) - record.scene.firstFrame + 1);
                if (!thumb.type)
                    for (const auto &child : archive.Chunks()[i].children)
                        if (child.target.type == kctgThumbMbmp && child.present)
                        {
                            thumb = child.target;
                            break;
                        }
                break;
            }
    result.details = std::to_string(scenes.size()) + " scenes / " + std::to_string(frames) + " scene frames";
    const auto seconds = (frames + kfps / 2) / kfps;
    char duration[96];
    if (seconds >= 3600)
        std::snprintf(duration, sizeof(duration), "Est. %lld:%02lld:%02lld (frames only)", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else
        std::snprintf(duration, sizeof(duration), "Est. %lld:%02lld (frames only)", seconds / 60, seconds % 60);
    result.details += std::string("\n") + duration;
    if (scenes.empty())
    {
        result.error = "No movie scenes found.";
        return result;
    }
    if (!thumb.type)
        return result;
    // Use the existing bounded payload reader and MBMP decoder, without opening
    // a second player or changing the live movie's renderer/content callbacks.
    ThreeMM::PayloadPage payload;
    for (std::size_t i = 0; i < archive.Chunks().size(); ++i)
        if (archive.Chunks()[i].id == thumb)
        {
            if (!archive.ReadPayload(i, 0, 1024 * 1024, payload, error) || payload.totalBytes != payload.bytes.size())
                return result;
            break;
        }
    if (payload.bytes.empty())
        return result;
    DataBlock block;
    if (!block.FSetTemp(static_cast<LegacyLong>(payload.bytes.size())) || !block.FWrite(payload.bytes.data()))
        return result;
    auto bitmap = MaskedBitmapMBMP::PmbmpRead(&block);
    if (!bitmap)
        return result;
    RC bounds;
    bitmap->GetRc(&bounds);
    const auto width = bounds.Dxp(), height = bounds.Dyp();
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024)
    {
        ReleasePpo(&bitmap);
        return result;
    }
    std::vector<byte> indices(static_cast<std::size_t>(width) * height, 0);
    bitmap->Draw(indices.data(), width, height, -bounds.xpLeft, -bounds.ypTop);
    ReleasePpo(&bitmap);
    ThreeMM::Archive resource({16 * 1024 * 1024, 1024 * 1024});
    if (!resource.Open(resources, error))
        return result;
    DataBlock paletteBlock;
    PDynamicArray palette = nullptr;
    short order = kboCur;
    for (const auto number : {0x20001u, 0u})
    {
        for (std::size_t i = 0; i < resource.Chunks().size(); ++i)
            if (resource.Chunks()[i].id.type == kctgColorTable && resource.Chunks()[i].id.number == number &&
                resource.ReadPayload(i, 0, 8192, payload, error) && payload.totalBytes == payload.bytes.size() &&
                paletteBlock.FSetTemp(static_cast<LegacyLong>(payload.bytes.size())) &&
                paletteBlock.FWrite(payload.bytes.data()))
            {
                palette = DynamicArray::PglRead(&paletteBlock, &order);
                break;
            }
        if (palette)
            break;
    }
    if (palette && palette->CbEntry() == sizeof(Color) && palette->IvMac() == 256)
    {
        if (order == kboOther) SwapBytesRglw(palette->QvGet(0), palette->IvMac());
        result.width = width;
        result.height = height;
        result.rgba.resize(indices.size() * 4);
        for (std::size_t i = 0; i < indices.size(); ++i)
        {
            Color colour;
            palette->Get(indices[i], &colour);
            result.rgba[i * 4] = colour.bRed;
            result.rgba[i * 4 + 1] = colour.bGreen;
            result.rgba[i * 4 + 2] = colour.bBlue;
            result.rgba[i * 4 + 3] = 255;
        }
    }
    ReleasePpo(&palette);
    return result;
}
