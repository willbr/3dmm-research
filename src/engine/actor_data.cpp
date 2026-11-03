/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include <cstddef>
#include <cstring>
ASSERTNAME
using namespace ActorEvent;

bool ActorData::FReadHeader(PDataBlock pblck, ActorChunkOnFile *pactf)
{
    AssertPo(pblck, 0);
    AssertVarMem(pactf);
    bool fOldActf = fFalse;

    if (!pblck->FUnpackData())
        return fFalse;

    if (pblck->Cb() != size(ActorChunkOnFile))
    {
        if (pblck->Cb() != size(ActorChunkOnFile) - size(int32_t))
            return fFalse;
        fOldActf = fTrue;
    }

    if (!pblck->FReadRgb(pactf, pblck->Cb(), 0))
        return fFalse;

    if (fOldActf)
    {
        const size_t offsetLast = offsetof(ActorChunkOnFile, nfrmLast);
        byte *bytes = reinterpret_cast<byte *>(pactf);
        std::memmove(bytes + offsetLast + sizeof(int32_t), bytes + offsetLast,
                     sizeof(ActorChunkOnFile) - offsetLast - sizeof(int32_t));
    }

    if (kboOther == pactf->bo)
        SwapBytesBom(pactf, kbomActf);
    if (kboCur != pactf->bo)
    {
        return fFalse;
    }

    if (fOldActf)
        pactf->nfrmLast = knfrmInvalid;
    return fTrue;
}

bool ActorData::FWriteHeader(PChunkyFile file, ChunkNumber number, ActorChunkOnFile *header)
{
    return file->FPutPv(header, sizeof(*header), kctgActr, number);
}

bool ActorData::FAdjustId(PChunkyFile file, ChunkNumber number, LegacyLong delta)
{
    ActorChunkOnFile header;
    DataBlock block;
    if (!file->FFind(kctgActr, number, &block) || !FReadHeader(&block, &header))
        return fFalse;
    const int64_t adjusted = static_cast<int64_t>(header.arid) + delta;
    if (adjusted < INT32_MIN || adjusted > INT32_MAX)
        return fFalse;
    header.arid = static_cast<int32_t>(adjusted);
    return FWriteHeader(file, number, &header);
}

namespace
{
bool ValidEvents(PGeneralGroup group, bool wire, short bo)
{
    if (group->CbFixed() != size(Base))
        return false;
    for (LegacyLong i = 0; i < group->IvMac(); ++i)
    {
        Base event;
        group->GetFixed(i, &event);
        if (bo == kboOther)
            SwapBytesBom(&event, kbomAev);
        LegacyLong bytes;
        switch (event.aet)
        {
        case aetAdd:
            bytes = size(Add);
            break;
        case aetActn:
            bytes = size(Action);
            break;
        case aetCost:
            bytes = wire ? size(CostumeOnFile) : size(Costume);
            break;
        case aetSnd:
            bytes = wire ? size(SoundOnFile) : size(Sound);
            break;
        case aetRotF:
        case aetRotH:
            bytes = size(BMAT34);
            break;
        case aetPull:
            bytes = size(Stretch);
            break;
        case aetSize:
        case aetFreeze:
        case aetStep:
            bytes = 4;
            break;
        case aetMove:
        case aetTweak:
            bytes = size(RoutePoint);
            break;
        case aetRem:
            bytes = 0;
            break;
        default:
            return false;
        }
        if (group->Cb(i) != bytes)
            return false;
    }
    return true;
}

PGeneralGroup MarshalFromWire(PGeneralGroup pggOnFile)
{
    AssertPo(pggOnFile, 0);

    LegacyLong iaev, iaevMac;
    Base aev;
    CostumeOnFile costOnFile;
    Costume cost;
    SoundOnFile sndOnFile;
    Sound snd;
    PGeneralGroup pggRuntime;

    pggRuntime = GeneralGroup::PggNew(size(Base));
    if (pvNil == pggRuntime)
        goto LFail;

    iaevMac = pggOnFile->IvMac();
    for (iaev = 0; iaev < iaevMac; iaev++)
    {
        pggOnFile->GetFixed(iaev, &aev);
        switch (aev.aet)
        {
        case aetCost:
            // Variable part is CostumeOnFile -> Costume marshal.
            Assert(pggOnFile->Cb(iaev) == size(CostumeOnFile), "bad CostumeOnFile size");
            CopyPb(pggOnFile->QvGet(iaev), &costOnFile, size(CostumeOnFile));
            cost.ibset = costOnFile.ibset;
            cost.cmid = costOnFile.cmid;
            cost.fCmtl = costOnFile.fCmtl;
            TagFromOnFile(&cost.tag, costOnFile.tag);
            if (!pggRuntime->FInsert(iaev, size(Costume), &cost, &aev))
                goto LFail;
            break;
        case aetSnd:
            // Variable part is SoundOnFile -> Sound marshal.
            Assert(pggOnFile->Cb(iaev) == size(SoundOnFile), "bad SoundOnFile size");
            CopyPb(pggOnFile->QvGet(iaev), &sndOnFile, size(SoundOnFile));
            snd.fLoop = sndOnFile.fLoop;
            snd.fQueue = sndOnFile.fQueue;
            snd.vlm = sndOnFile.vlm;
            snd.celn = sndOnFile.celn;
            snd.sty = sndOnFile.sty;
            snd.fNoSound = sndOnFile.fNoSound;
            snd.chid = sndOnFile.chid;
            TagFromOnFile(&snd.tag, sndOnFile.tag);
            if (!pggRuntime->FInsert(iaev, size(Sound), &snd, &aev))
                goto LFail;
            break;
        default:
            // No TAG embed: variable part is identical on disk and in runtime.
            if (!pggRuntime->FInsert(iaev, pggOnFile->Cb(iaev), pggOnFile->QvGet(iaev), &aev))
                goto LFail;
            break;
        }
    }

    ReleasePpo(&pggOnFile);
    return pggRuntime;

LFail:
    ReleasePpo(&pggOnFile);
    ReleasePpo(&pggRuntime);
    return pvNil;
}

void SwapWireEvents(PGeneralGroup pggaev)
{
    AssertPo(pggaev, 0);

    LegacyLong iaev;

    for (iaev = 0; iaev < pggaev->IvMac(); iaev++)
    {
        SwapBytesBom(pggaev->QvFixedGet(iaev), kbomAev);
        switch (((Base *)pggaev->QvFixedGet(iaev))->aet)
        {
        case aetCost:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevcost);
            break;
        case aetSnd:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevsnd);
            break;
        case aetSize:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevsize);
            break;
        case aetPull:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevpull);
            break;
        case aetRotF:
        case aetRotH:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevrot);
            break;
        case aetActn:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevactn);
            break;
        case aetAdd:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevadd);
            break;
        case aetFreeze:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevfreeze);
            break;
        case aetMove:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevmove);
            break;
        case aetTweak:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevtweak);
            break;
        case aetStep:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevstep);
            break;
        case aetRem:
            // no var data
            break;
        default:
            Bug("Unknown ActorEventType");
            break;
        }
    }
}

} // namespace

PDynamicArray ActorData::PglReadRoute(PDataBlock block)
{
    short bo;
    PDynamicArray route = DynamicArray::PglRead(block, &bo);
    if (route == pvNil)
        return pvNil;
    if (route->CbEntry() != size(RouteDistancePoint) || (bo != kboCur && bo != kboOther))
    {
        ReleasePpo(&route);
        return pvNil;
    }
    if (bo == kboOther && route->IvMac() > 0)
        SwapBytesRglw(route->QvGet(0), LwMul(route->IvMac(), size(RouteDistancePoint) / size(int32_t)));
    return route;
}

PGeneralGroup ActorData::PggReadEvents(PDataBlock block)
{
    short bo;
    PGeneralGroup wire = GeneralGroup::PggRead(block, &bo);
    if (wire == pvNil)
        return pvNil;
    if ((bo != kboCur && bo != kboOther) || !ValidEvents(wire, true, bo))
    {
        ReleasePpo(&wire);
        return pvNil;
    }
    if (bo == kboOther)
        SwapWireEvents(wire);
    return MarshalFromWire(wire); // consumes wire on either outcome
}

PGeneralGroup ActorData::PggWriteEvents(PGeneralGroup events)
{
    if (!ValidEvents(events, false, kboCur))
        return pvNil;
    PGeneralGroup pggaevOnFile = GeneralGroup::PggNew(size(Base));
    if (pvNil == pggaevOnFile)
        return pvNil;
    LegacyLong iaevSave, iaevMacSave = events->IvMac();
    Base aevSave;
    Costume costSave;
    CostumeOnFile costSaveOnFile;
    Sound sndSave;
    SoundOnFile sndSaveOnFile;
    bool fOk = fTrue;
    for (iaevSave = 0; iaevSave < iaevMacSave && fOk; iaevSave++)
    {
        events->GetFixed(iaevSave, &aevSave);
        switch (aevSave.aet)
        {
        case aetCost:
            events->GetRgb(iaevSave, 0, size(Costume), &costSave);
            costSaveOnFile.ibset = costSave.ibset;
            costSaveOnFile.cmid = costSave.cmid;
            costSaveOnFile.fCmtl = costSave.fCmtl;
            costSaveOnFile.tag.From(costSave.tag);
            if (!pggaevOnFile->FInsert(iaevSave, size(CostumeOnFile), &costSaveOnFile, &aevSave))
                fOk = fFalse;
            break;
        case aetSnd:
            events->GetRgb(iaevSave, 0, size(Sound), &sndSave);
            sndSaveOnFile.fLoop = sndSave.fLoop;
            sndSaveOnFile.fQueue = sndSave.fQueue;
            sndSaveOnFile.vlm = sndSave.vlm;
            sndSaveOnFile.celn = sndSave.celn;
            sndSaveOnFile.sty = sndSave.sty;
            sndSaveOnFile.fNoSound = sndSave.fNoSound;
            sndSaveOnFile.chid = sndSave.chid;
            sndSaveOnFile.tag.From(sndSave.tag);
            if (!pggaevOnFile->FInsert(iaevSave, size(SoundOnFile), &sndSaveOnFile, &aevSave))
                fOk = fFalse;
            break;
        default:
            if (!pggaevOnFile->FInsert(iaevSave, events->Cb(iaevSave), events->QvGet(iaevSave), &aevSave))
                fOk = fFalse;
            break;
        }
    }
    if (!fOk)
    {
        ReleasePpo(&pggaevOnFile);
        return pvNil;
    }
    return pggaevOnFile;
}
