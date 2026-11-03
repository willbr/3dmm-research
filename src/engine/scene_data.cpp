/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include <cstring>
ASSERTNAME

bool SceneData::FRead(PChunkyFile file, ChunkNumber number)
{
    DataBlock block;
    SceneOnFile header;
    if (!file->FFind(kctgScen, number, &block) || !block.FUnpackData() || block.Cb() != size(SceneOnFile) ||
        !block.FReadRgb(&header, sizeof(header), 0))
        return fFalse;
    if (header.bo == kboOther)
        SwapBytesBom(&header, kbomScenh);
    if (header.bo != kboCur || header.nfrmFirst == INT32_MIN || header.nfrmLast < header.nfrmFirst ||
        header.trans < transNil || header.trans >= transLim)
        return fFalse;
    SceneData loaded;
    loaded.firstFrame = header.nfrmFirst;
    loaded.lastFrame = header.nfrmLast;
    loaded.currentFrame = header.nfrmFirst - 1;
    loaded.transition = header.trans;
    file->FGetName(kctgScen, number, &loaded.name);
    *this = loaded;
    return fTrue;
}

bool SceneData::FWrite(PChunkyFile file, ChunkNumber number)
{
    if (firstFrame <= INT32_MIN || firstFrame > INT32_MAX || lastFrame > INT32_MAX || lastFrame < firstFrame ||
        transition < transNil || transition >= transLim)
        return fFalse;
    SceneOnFile header = {kboCur, koskCur, static_cast<int32_t>(lastFrame), static_cast<int32_t>(firstFrame),
                          transition};
    return file->FPutPv(&header, sizeof(header), kctgScen, number) && file->FSetName(kctgScen, number, &name);
}

namespace
{
bool ValidEvents(PGeneralGroup group, short bo, bool start)
{
    if (group->CbFixed() != size(SceneEvent))
        return false;
    for (LegacyLong i = 0; i < group->IvMac(); ++i)
    {
        SceneEvent event;
        group->GetFixed(i, &event);
        if (bo == kboOther)
            SwapBytesBom(&event, kbomSev);
        LegacyLong bytes = group->Cb(i);
        switch (event.sevt)
        {
        case sevtAddActr:
        case sevtAddTbox:
            if (!start || bytes != 4)
                return false;
            break;
        case sevtSetBkgd:
            if (!start || bytes != size(TAGOnFile))
                return false;
            break;
        case sevtChngCamera:
            if (bytes != 4)
                return false;
            break;
        case sevtPause:
            if (start || bytes != size(SceneEventPause))
                return false;
            break;
        case sevtPlaySnd: {
            if (start || bytes < 16)
                return false;
            int32_t count;
            std::memcpy(&count, static_cast<byte *>(group->QvGet(i)) + 12, sizeof(count));
            if (bo == kboOther)
                SwapBytesBom(&count, kbomSceneLong);
            if (count <= 0 || count > (bytes - 16) / 20 || bytes != 16 + count * 20)
                return false;
            break;
        }
        default:
            return false;
        }
    }
    return true;
}
} // namespace

PGeneralGroup SceneData::PggReadEvents(PDataBlock block, short *byteOrder, bool startEvents)
{
    short bo;
    PGeneralGroup group = GeneralGroup::PggRead(block, &bo);
    if (group == pvNil)
        return pvNil;
    if ((bo != kboCur && bo != kboOther) || !ValidEvents(group, bo, startEvents))
    {
        ReleasePpo(&group);
        return pvNil;
    }
    if (byteOrder != pvNil)
        *byteOrder = bo;
    return group;
}

bool SceneData::FWriteEvents(PChunkyFile file, ChunkNumber number, PGeneralGroup frames, PGeneralGroup starts)
{
    if (!ValidEvents(frames, kboCur, false) || !ValidEvents(starts, kboCur, true))
        return fFalse;
    ChildChunkIdentification oldFrame, oldStart;
    bool hadFrame = file->FGetKidChidCtg(kctgScen, number, 0, kctgFrmGg, &oldFrame);
    bool hadStart = file->FGetKidChidCtg(kctgScen, number, 1, kctgStartGg, &oldStart);
    ChunkNumber frame = cnoNil, start = cnoNil;
    bool createdFrame = false, createdStart = false;
    bool adoptedFrame = false, adoptedStart = false;
    DataBlock block;
    if (!file->FAdd(frames->CbOnFile(), kctgFrmGg, &frame, &block))
        goto fail;
    createdFrame = true;
    if (!frames->FWrite(&block))
        goto fail;
    if (!file->FAdd(starts->CbOnFile(), kctgStartGg, &start, &block))
        goto fail;
    createdStart = true;
    if (!starts->FWrite(&block))
        goto fail;
    if (!file->FAdoptChild(kctgScen, number, kctgFrmGg, frame, 0))
        goto fail;
    adoptedFrame = true;
    if (!file->FAdoptChild(kctgScen, number, kctgStartGg, start, 1))
        goto fail;
    adoptedStart = true;
    if (hadFrame)
        file->DeleteChild(kctgScen, number, kctgFrmGg, oldFrame.cki.cno, 0);
    if (hadStart)
        file->DeleteChild(kctgScen, number, kctgStartGg, oldStart.cki.cno, 1);
    file->SetLoner(kctgFrmGg, frame, fFalse);
    file->SetLoner(kctgStartGg, start, fFalse);
    return fTrue;
fail:
    if (adoptedFrame)
        file->DeleteChild(kctgScen, number, kctgFrmGg, frame, 0);
    else if (createdFrame)
        file->Delete(kctgFrmGg, frame);
    if (adoptedStart)
        file->DeleteChild(kctgScen, number, kctgStartGg, start, 1);
    else if (createdStart)
        file->Delete(kctgStartGg, start);
    return fFalse;
}

bool SceneData::FCreate(PChunkyFile file, ChunkNumber *number)
{
    PGeneralGroup frames = GeneralGroup::PggNew(size(SceneEvent));
    PGeneralGroup starts = GeneralGroup::PggNew(size(SceneEvent));
    *number = cnoNil;
    bool created = frames != pvNil && starts != pvNil && file->FAdd(size(SceneOnFile), kctgScen, number);
    bool success = created && FWrite(file, *number) && FWriteEvents(file, *number, frames, starts);
    ReleasePpo(&frames);
    ReleasePpo(&starts);
    if (!success && created)
    {
        file->Delete(kctgScen, *number);
        *number = cnoNil;
    }
    if (!success)
        *number = cnoNil;
    return success;
}
