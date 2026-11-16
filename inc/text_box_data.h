/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef TEXT_BOX_DATA_H
#define TEXT_BOX_DATA_H
#include "kauai_core.h"
#include "rich_text_data.h"
#include <cstdint>
#include "mm/text_box_runtime.h"

#define kbomTboxh 0x5FFFC000

//
// header information for saving text boxes to a file
//
struct TextBoxOnFile
{
    int16_t bo;
    int16_t osk;
    int32_t nfrmFirst;
    int32_t nfrmMax;
    int32_t xpLeft;
    int32_t xpRight;
    int32_t ypTop;
    int32_t ypBottom;
    ChildChunkID chid;
    uint8_t fStory;
    uint8_t reserved[3];
};
static_assert(sizeof(TextBoxOnFile) == 36, "TextBoxOnFile on-disk layout drift");

struct TextBoxData
{
    MMTextBoxState state = {};
    RC Bounds() const { return RC(state.left, state.top, state.right, state.bottom); }
    void SetBounds(LegacyLong left, LegacyLong top, LegacyLong right, LegacyLong bottom)
    { state.left = left; state.top = top; state.right = right; state.bottom = bottom; }
    void SetBounds(const RC &bounds)
    { SetBounds(bounds.xpLeft, bounds.ypTop, bounds.xpRight, bounds.ypBottom); }
    bool FRead(PChunkyFile file, ChunkNumber number, ChunkIdentification *richText = pvNil);
    bool FWrite(PChunkyFile file, ChunkNumber number);
    bool FVisibleAt(LegacyLong frame) const
    {
        return mm_text_box_visible_range(state.first, state.end, frame) != 0;
    }
};
#endif
