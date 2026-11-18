/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_movie_graph.h"
ASSERTNAME
static_assert(transNil == -1 && transCut == 0 && transLim == MM_SCENE_TRANSITION_LIMIT,
              "scene transition encoding");
static_assert(sevtAddActr == 0 && sevtPlaySnd == 1 && sevtAddTbox == 2 && sevtChngCamera == 3 && sevtSetBkgd == 4 &&
                  sevtPause == 5,
              "scene event encoding");
static_assert(kboCur == MM_SCENE_GROUP_NATIVE_ORDER && kboOther == MM_SCENE_GROUP_OTHER_ORDER,
              "Scene group byte order markers");
namespace
{
bool NativeBig()
{
    const unsigned short one = 1;
    return *reinterpret_cast<const byte *>(&one) == 0;
}
void GetNameValue(const MMMovieTitle *name, PString out)
{
    out->SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(name))), name->count);
}
void SetNameValue(MMMovieTitle *name, PString value)
{
    AssertDo(mm_movie_title_set(name, value->Prgch(), value->Cch()), "bad scene name");
}
struct SceneEventHost
{
    PChunkyFile file;
    DataBlock block;
};
MMSceneIO SceneIO(PChunkyFile file)
{
    return {file,
            [](void *c, MMU32 number, MMReadReceiver receive, void *out) -> int {
                return EngineMovieGraph::ReceivePayload(static_cast<PChunkyFile>(c), kctgScen, number, receive, out);
            },
            [](void *c, MMU32 number, MMMovieTitle *name) {
                String value;
                GetNameValue(name, &value);
                static_cast<PChunkyFile>(c)->FGetName(kctgScen, number, &value);
                SetNameValue(name, &value);
            },
            [](void *c, MMU32 number, const unsigned char *bytes) -> int {
                return static_cast<PChunkyFile>(c)->FPutPv(const_cast<unsigned char *>(bytes), 16, kctgScen, number);
            },
            [](void *c, MMU32 number, const MMMovieTitle *name) -> int {
                String value;
                GetNameValue(name, &value);
                return static_cast<PChunkyFile>(c)->FSetName(kctgScen, number, &value);
            }};
}
MMSceneGroupServices Groups()
{
    return {[](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->CbFixed(); },
            [](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->IvMac(); },
            [](void *g, MMU32 i, unsigned char *bytes) { static_cast<PGeneralGroup>(g)->GetFixed(i, bytes); },
            [](void *g, MMU32 i) -> MMI32 { return static_cast<PGeneralGroup>(g)->Cb(i); },
            [](void *g, MMU32 i) -> const void * { return static_cast<PGeneralGroup>(g)->QvGet(i); }};
}
MMSceneEventIO Events(SceneEventHost *host)
{
    return {
        host,
        Groups(),
        [](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->CbOnFile(); },
        [](void *c, MMU32 scene, int start, MMU32 *number) -> int {
            ChildChunkIdentification child;
            auto file = static_cast<SceneEventHost *>(c)->file;
            if (!file->FGetKidChidCtg(kctgScen, scene, start, start ? kctgStartGg : kctgFrmGg, &child))
                return 0;
            *number = child.cki.cno;
            return 1;
        },
        [](void *c, int start, MMU32 bytes, MMU32 *number) -> int {
            auto h = static_cast<SceneEventHost *>(c);
            return h->file->FAdd(bytes, start ? kctgStartGg : kctgFrmGg, number, &h->block);
        },
        [](void *c, void *g) -> int {
            return static_cast<PGeneralGroup>(g)->FWrite(&static_cast<SceneEventHost *>(c)->block);
        },
        [](void *c, MMU32 scene, int start, MMU32 number) -> int {
            auto file = static_cast<SceneEventHost *>(c)->file;
            return file->FAdoptChild(kctgScen, scene, start ? kctgStartGg : kctgFrmGg, number, start);
        },
        [](void *c, MMU32 scene, int start, MMU32 number, int linked) {
            auto file = static_cast<SceneEventHost *>(c)->file;
            if (linked)
                file->DeleteChild(kctgScen, scene, start ? kctgStartGg : kctgFrmGg, number, start);
            else
                file->Delete(start ? kctgStartGg : kctgFrmGg, number);
        },
        [](void *c, int start, MMU32 number) {
            static_cast<SceneEventHost *>(c)->file->SetLoner(start ? kctgStartGg : kctgFrmGg, number, fFalse);
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
    const auto io = SceneIO(file);
    io.load_name(io.context, number, &name);
}
bool SceneData::FRead(PChunkyFile file, ChunkNumber number)
{
    const auto io = SceneIO(file);
    return mm_scene_read(this, number, &io) != 0;
}
bool SceneData::FWrite(PChunkyFile file, ChunkNumber number)
{
    const auto io = SceneIO(file);
    return mm_scene_write(this, number, koskCur, NativeBig(), &io) != 0;
}
bool SceneData::FReadTransition(PChunkyFile file, ChunkNumber number, MMI32 *out)
{
    const auto io = SceneIO(file);
    return mm_scene_transition_read(number, sizeof(achar), out, &io) != 0;
}
bool SceneData::FWriteTransition(PChunkyFile file, ChunkNumber number, MMI32 transition)
{
    const auto io = SceneIO(file);
    return mm_scene_transition_write(number, sizeof(achar), transition, koskCur, NativeBig(), &io) != 0;
}
static PGeneralGroup ReadSceneEvents(PDataBlock block, short *byteOrder, int layout)
{
    const MMSceneReadGroupServices services = {
        block, Groups(),
        [](void *p, int *order) -> void * {
            short bo;
            auto group = GeneralGroup::PggRead(static_cast<PDataBlock>(p), &bo);
            if (group)
                *order = bo;
            return group;
        },
        [](void *, void *group) { static_cast<PGeneralGroup>(group)->Release(); }};
    int order;
    auto group =
        static_cast<PGeneralGroup>(mm_scene_read_event_group(layout, NativeBig(), &order, &services));
    if (group && byteOrder)
        *byteOrder = static_cast<short>(order);
    return group;
}
PGeneralGroup SceneData::PggReadEvents(PDataBlock block, short *byteOrder, bool startEvents)
{
    return ReadSceneEvents(block, byteOrder, startEvents);
}
PGeneralGroup SceneData::PggReadEventHeaders(PDataBlock block, short *byteOrder)
{
    return ReadSceneEvents(block, byteOrder, MM_SCENE_GROUP_HEADERS_ONLY);
}
bool SceneData::FWriteEvents(PChunkyFile file, ChunkNumber number, PGeneralGroup frames, PGeneralGroup starts)
{
    SceneEventHost host = {file};
    const auto io = Events(&host);
    return mm_scene_write_events(number, frames, starts, &io) != 0;
}
bool SceneData::FCreate(PChunkyFile file, ChunkNumber *number)
{
    SceneEventHost host = {file};
    const MMSceneCreateServices services = {
        SceneIO(file),
        Events(&host),
        [](void *) -> void * { return GeneralGroup::PggNew(MM_SCENE_EVENT_BYTES); },
        [](void *g) { static_cast<PGeneralGroup>(g)->Release(); },
        [](void *c, MMU32 bytes, MMU32 *number) -> int {
            return static_cast<PChunkyFile>(c)->FAdd(bytes, kctgScen, number);
        },
        [](void *c, MMU32 number) { static_cast<PChunkyFile>(c)->Delete(kctgScen, number); }};
    return mm_scene_create(this, number, koskCur, NativeBig(), &services) != 0;
}
