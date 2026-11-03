/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef RICH_TEXT_DATA_H
#define RICH_TEXT_DATA_H
#include "kauai_core.h"
#include <cstdint>

struct RichTextProperties
{
    int16_t bo;
    int16_t oskFont;
    int32_t dxpDef;
    int32_t dypFont;
    int32_t lwAcrBack;
};
static_assert(sizeof(RichTextProperties) == 16, "Rich text properties wire layout drift");
const ByteOrderMask kbomRdop = 0x5FC00000;
struct RichTextPropertyEntry
{
    uint32_t spcp;
    int32_t lw;
};
static_assert(sizeof(RichTextPropertyEntry) == 8, "Rich text property entry wire layout drift");

// Stored rich text, independent of document views, font registration and GDI.
// Text bytes and property arguments retain their original encoding/byte order.
class RichTextData
{
    void Clear();

  public:
    RichTextProperties defaults = {kboCur, koskCur, 640, 12, 0};
    String fontName;
    FileLocation text = {};
    short textOsk = koskCur;
    PDynamicArray properties = pvNil;
    short propertyBo = kboCur;
    short propertyOsk = koskCur;
    PAllocatedGroup arguments = pvNil;
    short argumentBo = kboCur;
    short argumentOsk = koskCur;
    RichTextData() = default;
    ~RichTextData();
    RichTextData(const RichTextData &) = delete;
    RichTextData &operator=(const RichTextData &) = delete;
    bool FRead(PChunkyFile file, ChunkTagOrType type, ChunkNumber number);
    bool FWrite(PChunkyFile file, ChunkIdentification *written);
    static bool FCreateRoot(PChunkyFile file, ChunkIdentification *written, RichTextProperties *defaults,
                            PString fontName);
};
#endif
