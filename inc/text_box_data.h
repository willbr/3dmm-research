/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef TEXT_BOX_DATA_H
#define TEXT_BOX_DATA_H
#include "kauai_core.h"
#include "rich_text_data.h"
#include <cstdint>

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
    LegacyLong firstFrame = 0;
    LegacyLong maxFrame = 0; // exclusive; klwMax means through the scene's end
    LegacyLong currentFrame = 0;
    bool story = false;
    RC bounds = RC(0, 0, 0, 0);
    ChildChunkID textChild = 0;
    bool FRead(PChunkyFile file, ChunkNumber number, ChunkIdentification *richText = pvNil);
    bool FWrite(PChunkyFile file, ChunkNumber number);
    bool FVisibleAt(LegacyLong frame) const
    {
        return frame >= firstFrame && frame < maxFrame;
    }
};
#endif
