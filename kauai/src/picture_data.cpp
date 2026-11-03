/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "picture_data.h"
#include <utility>
ASSERTNAME
namespace
{
uint32_t Read32(const byte *bytes, bool big)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(bytes[i]) << (8 * (big ? 3 - i : i));
    return value;
}
void Write32(byte *bytes, uint32_t value, bool big)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = static_cast<byte>(value >> (8 * (big ? 3 - i : i)));
}
} // namespace
void PictureData::Swap(PictureData &other)
{
    std::swap(_bytes, other._bytes);
    std::swap(_count, other._count);
    std::swap(_bounds, other._bounds);
}
bool PictureData::FRead(PDataBlock block, bool bigEndian)
{
    if (!block->FUnpackData() || block->Cb() <= HeaderBytes || block->Cb() > kcbMax)
        return false;
    PictureData loaded;
    loaded._count = block->Cb();
    if (!FAllocHq(&loaded._bytes, loaded._count, fmemNil, mprNormal))
        return false;
    auto *bytes = static_cast<byte *>(QvFromHq(loaded._bytes));
    if (!block->FReadRgb(bytes, loaded._count, 0) ||
        Read32(bytes + 16, bigEndian) != static_cast<uint32_t>(loaded._count))
        return false;
    loaded._bounds.Set(
        static_cast<int32_t>(Read32(bytes, bigEndian)), static_cast<int32_t>(Read32(bytes + 4, bigEndian)),
        static_cast<int32_t>(Read32(bytes + 8, bigEndian)), static_cast<int32_t>(Read32(bytes + 12, bigEndian)));
    if (loaded._bounds.FEmpty())
        return false;
    Swap(loaded);
    return true;
}
bool PictureData::FInit(const RC &bounds, const void *payload, LegacyLong count, bool bigEndian)
{
    RC copy = bounds;
    if (copy.FEmpty() || !payload || count <= 0 || count > kcbMax - HeaderBytes)
        return false;
    PictureData loaded;
    loaded._bounds = bounds;
    loaded._count = count + HeaderBytes;
    if (!FAllocHq(&loaded._bytes, loaded._count, fmemNil, mprNormal))
        return false;
    auto *bytes = static_cast<byte *>(QvFromHq(loaded._bytes));
    Write32(bytes, static_cast<uint32_t>(bounds.xpLeft), bigEndian);
    Write32(bytes + 4, static_cast<uint32_t>(bounds.ypTop), bigEndian);
    Write32(bytes + 8, static_cast<uint32_t>(bounds.xpRight), bigEndian);
    Write32(bytes + 12, static_cast<uint32_t>(bounds.ypBottom), bigEndian);
    Write32(bytes + 16, static_cast<uint32_t>(loaded._count), bigEndian);
    CopyPb(const_cast<void *>(payload), bytes + HeaderBytes, count);
    Swap(loaded);
    return true;
}
bool PictureData::FWrite(PDataBlock block) const
{
    return _bytes && block->Cb() == _count && block->FWriteRgb(QvFromHq(_bytes), _count, 0);
}
