/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// Temporary portable picture adapter. Stored META bytes round-trip unchanged;
// EMF/WMF/PICT import, recording and rendering are explicitly unsupported.
#include "frame.h"
ASSERTNAME

Picture::Picture()
{
    _hpic = hNil;
    _rc.Zero();
}
Picture::~Picture()
{
}
PPicture Picture::PpicFetch(PChunkyFile file, ChunkTagOrType type, ChunkNumber number, ChildChunkID child)
{
    ChildChunkIdentification id;
    DataBlock block;
    return file->FGetKidChidCtg(type, number, child, kctgMeta, &id) && file->FFind(id.cki.ctg, id.cki.cno, &block)
               ? PpicRead(&block)
               : nullptr;
}
PPicture Picture::PpicRead(PDataBlock block)
{
    auto picture = NewObj Picture;
    if (picture && !picture->_stored.FRead(block))
        ReleasePpo(&picture);
    if (picture)
        picture->_rc = picture->_stored.Bounds();
    return picture;
}
LegacyLong Picture::CbOnFile()
{
    return _stored.CbOnFile();
}
bool Picture::FWrite(PDataBlock block)
{
    return _stored.FWrite(block);
}
PPicture Picture::PpicReadNative(Filename *)
{
    PushErc(ercGfxCantDraw);
    return nullptr;
}
