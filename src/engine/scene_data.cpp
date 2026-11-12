/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_read_stream.h"
ASSERTNAME
static_assert(transNil == -1 && transCut == 0 && transLim == THREE_MM_SCENE_TRANSITION_LIMIT,
              "scene transition encoding");
static_assert(sevtAddActr == 0 && sevtPlaySnd == 1 && sevtAddTbox == 2 && sevtChngCamera == 3 && sevtSetBkgd == 4 &&
                  sevtPause == 5,
              "scene event encoding");
namespace
{
bool NativeBig()
{
    const unsigned short one = 1;
    return *reinterpret_cast<const byte *>(&one) == 0;
}
void GetNameValue(const ThreeMMMovieTitle *name, PString out)
{
    out->SetRgch(const_cast<achar *>(static_cast<const achar *>(three_mm_movie_title_text(name))), name->count);
}
void SetNameValue(ThreeMMMovieTitle *name, PString value)
{
    AssertDo(three_mm_movie_title_set(name, value->Prgch(), value->Cch()), "bad scene name");
}
struct Host
{
    PChunkyFile file;
    DataBlock block;
};
ThreeMMSceneIO SceneIO(Host *host)
{
    return {host,
            [](void *c, ThreeMMU32 number, ThreeMMReadStream *stream) -> int {
                auto h = static_cast<Host *>(c);
                if (!h->file->FFind(kctgScen, number, &h->block) || !h->block.FUnpackData())
                    return 0;
                *stream = EngineReadStream::FromBlock(h->block);
                return 1;
            },
            [](void *c, ThreeMMU32 number, ThreeMMMovieTitle *name) {
                String value;
                GetNameValue(name, &value);
                static_cast<Host *>(c)->file->FGetName(kctgScen, number, &value);
                SetNameValue(name, &value);
            },
            [](void *c, ThreeMMU32 number, const unsigned char *bytes) -> int {
                return static_cast<Host *>(c)->file->FPutPv(const_cast<unsigned char *>(bytes), 16, kctgScen, number);
            },
            [](void *c, ThreeMMU32 number, const ThreeMMMovieTitle *name) -> int {
                String value;
                GetNameValue(name, &value);
                return static_cast<Host *>(c)->file->FSetName(kctgScen, number, &value);
            }};
}
ThreeMMSceneGroupServices Groups()
{
    return {[](void *g) -> ThreeMMU32 { return static_cast<PGeneralGroup>(g)->CbFixed(); },
            [](void *g) -> ThreeMMU32 { return static_cast<PGeneralGroup>(g)->IvMac(); },
            [](void *g, ThreeMMU32 i, unsigned char *bytes) { static_cast<PGeneralGroup>(g)->GetFixed(i, bytes); },
            [](void *g, ThreeMMU32 i) -> ThreeMMI32 { return static_cast<PGeneralGroup>(g)->Cb(i); },
            [](void *g, ThreeMMU32 i) -> const void * { return static_cast<PGeneralGroup>(g)->QvGet(i); }};
}
ThreeMMSceneEventIO Events(Host *host)
{
    return {
        host,
        Groups(),
        [](void *g) -> ThreeMMU32 { return static_cast<PGeneralGroup>(g)->CbOnFile(); },
        [](void *c, ThreeMMU32 scene, int start, ThreeMMU32 *number) -> int {
            ChildChunkIdentification child;
            if (!static_cast<Host *>(c)->file->FGetKidChidCtg(kctgScen, scene, start, start ? kctgStartGg : kctgFrmGg,
                                                              &child))
                return 0;
            *number = child.cki.cno;
            return 1;
        },
        [](void *c, int start, ThreeMMU32 bytes, ThreeMMU32 *number) -> int {
            auto h = static_cast<Host *>(c);
            return h->file->FAdd(bytes, start ? kctgStartGg : kctgFrmGg, number, &h->block);
        },
        [](void *c, void *g) -> int { return static_cast<PGeneralGroup>(g)->FWrite(&static_cast<Host *>(c)->block); },
        [](void *c, ThreeMMU32 scene, int start, ThreeMMU32 number) -> int {
            return static_cast<Host *>(c)->file->FAdoptChild(kctgScen, scene, start ? kctgStartGg : kctgFrmGg, number,
                                                             start);
        },
        [](void *c, ThreeMMU32 scene, int start, ThreeMMU32 number, int linked) {
            auto file = static_cast<Host *>(c)->file;
            if (linked)
                file->DeleteChild(kctgScen, scene, start ? kctgStartGg : kctgFrmGg, number, start);
            else
                file->Delete(start ? kctgStartGg : kctgFrmGg, number);
        },
        [](void *c, int start, ThreeMMU32 number) {
            static_cast<Host *>(c)->file->SetLoner(start ? kctgStartGg : kctgFrmGg, number, fFalse);
        }};
}
} // namespace
void SceneData::GetName(PString out) const
{
    GetNameValue(&name, out);
}
void SceneData::SetName(PString value)
{
    SetNameValue(&name, value);
}
void SceneData::LoadName(PChunkyFile file, ChunkNumber number)
{
    Host host = {file};
    const auto io = SceneIO(&host);
    io.load_name(io.context, number, &name);
}
bool SceneData::FRead(PChunkyFile file, ChunkNumber number)
{
    Host host = {file};
    const auto io = SceneIO(&host);
    return three_mm_scene_read(this, number, &io) != 0;
}
bool SceneData::FWrite(PChunkyFile file, ChunkNumber number)
{
    Host host = {file};
    const auto io = SceneIO(&host);
    return three_mm_scene_write(this, number, koskCur, NativeBig(), &io) != 0;
}
PGeneralGroup SceneData::PggReadEvents(PDataBlock block, short *byteOrder, bool startEvents)
{
    short bo;
    auto group = GeneralGroup::PggRead(block, &bo);
    if (!group)
        return nullptr;
    const auto services = Groups();
    if ((bo != kboCur && bo != kboOther) ||
        !three_mm_scene_valid_events(group, (bo == kboCur) == NativeBig(), startEvents, &services))
    {
        ReleasePpo(&group);
        return nullptr;
    }
    if (byteOrder)
        *byteOrder = bo;
    return group;
}
bool SceneData::FWriteEvents(PChunkyFile file, ChunkNumber number, PGeneralGroup frames, PGeneralGroup starts)
{
    Host host = {file};
    const auto io = Events(&host);
    return three_mm_scene_write_events(number, frames, starts, &io) != 0;
}
bool SceneData::FCreate(PChunkyFile file, ChunkNumber *number)
{
    Host host = {file};
    const ThreeMMSceneCreateServices services = {
        SceneIO(&host),
        Events(&host),
        [](void *) -> void * { return GeneralGroup::PggNew(THREE_MM_SCENE_EVENT_BYTES); },
        [](void *g) { static_cast<PGeneralGroup>(g)->Release(); },
        [](void *c, ThreeMMU32 bytes, ThreeMMU32 *number) -> int {
            return static_cast<Host *>(c)->file->FAdd(bytes, kctgScen, number);
        },
        [](void *c, ThreeMMU32 number) { static_cast<Host *>(c)->file->Delete(kctgScen, number); }};
    return three_mm_scene_create(this, number, koskCur, NativeBig(), &services) != 0;
}
