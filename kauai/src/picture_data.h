/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef PICTURE_DATA_H
#define PICTURE_DATA_H
#include "kauai_core.h"

// Stored native picture envelope: four signed 32-bit bounds and a 32-bit
// total byte count, followed by opaque native bytes. Windows META uses little
// endian; historical Mac PICT uses big endian. No GDI decoding takes place.
class PictureData
{
    HQ _bytes = hqNil;
    LegacyLong _count = 0;
    RC _bounds = RC(0, 0, 0, 0);

  public:
    static constexpr LegacyLong HeaderBytes = 20;
    PictureData() = default;
    ~PictureData()
    {
        FreePhq(&_bytes);
    }
    PictureData(const PictureData &) = delete;
    PictureData &operator=(const PictureData &) = delete;
    void Swap(PictureData &other);
    bool FRead(PDataBlock block, bool bigEndian = false);
    bool FInit(const RC &bounds, const void *payload, LegacyLong count, bool bigEndian = false);
    bool FWrite(PDataBlock block) const;
    LegacyLong CbOnFile() const
    {
        return _count;
    }
    RC Bounds() const
    {
        return _bounds;
    }
    const byte *Payload() const
    {
        return _bytes ? static_cast<const byte *>(QvFromHq(_bytes)) + HeaderBytes : pvNil;
    }
    LegacyLong PayloadBytes() const
    {
        return _count ? _count - HeaderBytes : 0;
    }
};
#endif
