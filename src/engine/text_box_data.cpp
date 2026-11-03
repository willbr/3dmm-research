/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME

bool TextBoxData::FRead(PChunkyFile file, ChunkNumber number, ChunkIdentification *richText)
{
    DataBlock block;
    TextBoxOnFile header;
    if (!file->FFind(kctgTbox, number, &block) || !block.FUnpackData() || block.Cb() != size(header) ||
        !block.FReadRgb(&header, size(header), 0))
        return fFalse;
    if (header.bo == kboOther)
        SwapBytesBom(&header, kbomTboxh);
    if (header.bo != kboCur || header.nfrmFirst == INT32_MIN || header.fStory > 1)
        return fFalse;
    ChildChunkIdentification child;
    if (richText != pvNil && !file->FGetKidChidCtg(kctgTbox, number, header.chid, kctgRichText, &child))
        return fFalse;
    firstFrame = header.nfrmFirst;
    maxFrame = header.nfrmMax;
    currentFrame = firstFrame - 1;
    story = header.fStory;
    bounds.Set(header.xpLeft, header.ypTop, header.xpRight, header.ypBottom);
    textChild = header.chid;
    if (richText != pvNil)
        *richText = child.cki;
    return fTrue;
}

bool TextBoxData::FWrite(PChunkyFile file, ChunkNumber number)
{
    if (firstFrame <= INT32_MIN || firstFrame > INT32_MAX || maxFrame > INT32_MAX)
        return fFalse;
    TextBoxOnFile header = {};
    header.bo = kboCur;
    header.osk = koskCur;
    header.nfrmFirst = static_cast<int32_t>(firstFrame);
    header.nfrmMax = static_cast<int32_t>(maxFrame);
    header.xpLeft = bounds.xpLeft;
    header.xpRight = bounds.xpRight;
    header.ypTop = bounds.ypTop;
    header.ypBottom = bounds.ypBottom;
    header.chid = textChild;
    header.fStory = story;
    return file->FPutPv(&header, size(header), kctgTbox, number);
}
