/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_movie_archive.h"
ASSERTNAME

SceneEvent ReadSceneTrackEvent(const MMSceneTrack *track, MMI32 index)
{
    MMSceneTrackEvent event = {};
    AssertDo(mm_scene_track_get(track, index, &event), "invalid scene event index");
    return {event.frame, static_cast<SceneEventType>(event.kind)};
}

int InsertSceneTrackEvent(MMSceneTrack *track, MMI32 index, MMI32 bytes, const void *data,
                                 const SceneEvent &event)
{
    const MMSceneTrackEvent header = {event.nfrm, event.sevt};
    return mm_scene_track_insert(track, index, bytes, data, &header);
}

void SetSceneTrackEvent(MMSceneTrack *track, MMI32 index, const SceneEvent &event)
{
    const MMSceneTrackEvent header = {event.nfrm, event.sevt};
    AssertDo(mm_scene_track_set(track, index, &header), "invalid scene event index");
}

static_assert(transNil == -1 && transCut == 0 && transLim == MM_SCENE_TRANSITION_LIMIT,
              "scene transition encoding");
static_assert(sevtAddActr == 0 && sevtPlaySnd == 1 && sevtAddTbox == 2 && sevtChngCamera == 3 && sevtSetBkgd == 4 &&
                  sevtPause == 5,
              "scene event encoding");
static_assert(kboCur == MM_SCENE_GROUP_NATIVE_ORDER && kboOther == MM_SCENE_GROUP_OTHER_ORDER,
              "Scene group byte order markers");
static_assert(kctgScen == MM_SCENE_CHUNK && kctgFrmGg == MM_SCENE_FRAME_GROUP && kctgStartGg == MM_SCENE_START_GROUP,
              "Scene archive types");
namespace
{
bool NativeBig()
{
    const unsigned short one = 1;
    return *reinterpret_cast<const byte *>(&one) == 0;
}
MMSceneGroupServices Groups()
{
    return {[](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->CbFixed(); },
            [](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->IvMac(); },
            [](void *g, MMU32 i, unsigned char *bytes) { static_cast<PGeneralGroup>(g)->GetFixed(i, bytes); },
            [](void *g, MMU32 i) -> MMI32 { return static_cast<PGeneralGroup>(g)->Cb(i); },
            [](void *g, MMU32 i) -> const void * { return static_cast<PGeneralGroup>(g)->QvGet(i); }};
}
} // namespace
void GetSceneName(const MMMovieTitle &name, PString out)
{
    out->SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(&name))), name.count);
}
void SetSceneName(MMMovieTitle &name, PString value)
{
    AssertDo(mm_movie_title_set(&name, value->Prgch(), value->Cch()), "bad scene name");
}
bool SceneData::FRead(PChunkyFile file, ChunkNumber number)
{
    const auto archive = file->WriteIndex();
    const auto titles = KauaiChunkTitleIO();
    const auto io = mm_scene_file_io(file, &archive, &titles);
    return mm_scene_read(this, number, &io) != 0;
}
bool SceneData::FWrite(PChunkyFile file, ChunkNumber number)
{
    const auto archive = file->WriteIndex();
    const auto titles = KauaiChunkTitleIO();
    const auto io = mm_scene_file_io(file, &archive, &titles);
    return mm_scene_write(this, number, &io) != 0;
}
bool SceneData::FReadTransition(PChunkyFile file, ChunkNumber number, MMI32 *out)
{
    const auto archive = file->WriteIndex();
    const auto titles = KauaiChunkTitleIO();
    const auto io = mm_scene_file_io(file, &archive, &titles);
    return mm_scene_transition_read(number, sizeof(achar), out, &io) != 0;
}
bool SceneData::FWriteTransition(PChunkyFile file, ChunkNumber number, MMI32 transition)
{
    const auto archive = file->WriteIndex();
    const auto titles = KauaiChunkTitleIO();
    const auto io = mm_scene_file_io(file, &archive, &titles);
    return mm_scene_transition_write(number, sizeof(achar), transition, &io) != 0;
}
PGeneralGroup SceneData::PggReadEvents(PDataBlock block, short *byteOrder, bool startEvents)
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
        static_cast<PGeneralGroup>(mm_scene_read_event_group(startEvents, NativeBig(), &order, &services));
    if (group && byteOrder)
        *byteOrder = static_cast<short>(order);
    return group;
}
bool SceneData::FWriteEvents(PChunkyFile file, ChunkNumber number, PGeneralGroup frames, PGeneralGroup starts)
{
    MovieArchiveHost host{file};
    const auto editor = host.Editor();
    const MMSceneEventGroups groups{
        Groups(),
        [](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->CbOnFile(); },
        [](void *g, int system, const MMStorageWriter *writer) -> int {
            return static_cast<PGeneralGroup>(g)->FWriteStream(*writer, kboCur, system);
        }};
    return mm_scene_archive_write_events(&editor, number, frames, starts, &groups) != 0;
}
bool SceneData::FCreate(PChunkyFile file, ChunkNumber *number)
{
    MovieArchiveHost host{file};
    const auto editor = host.Editor();
    return mm_scene_archive_create(&editor, this, number) != 0;
}
