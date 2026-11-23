#pragma once

#include "movie_archive_host.h"
#include "scene_data.h"
#include <cstdlib>

namespace SceneArchiveFixture
{
inline MMSceneState State()
{
    MMSceneState scene;
    mm_scene_init(&scene, sizeof(achar));
    return scene;
}

inline bool Read(PChunkyFile file, ChunkNumber number, MMSceneState &scene)
{
    const auto archive = file->WriteIndex();
    const auto titles = KauaiChunkTitleIO();
    const auto io = mm_scene_file_io(file, &archive, &titles);
    return mm_scene_read(&scene, number, &io) != 0;
}

inline bool Write(PChunkyFile file, ChunkNumber number, const MMSceneState &scene)
{
    const auto archive = file->WriteIndex();
    const auto titles = KauaiChunkTitleIO();
    const auto io = mm_scene_file_io(file, &archive, &titles);
    return mm_scene_write(&scene, number, &io) != 0;
}

inline bool Create(PChunkyFile file, ChunkNumber *number, const MMSceneState &scene)
{
    MovieArchiveHost host{file};
    const auto editor = host.Editor();
    return mm_scene_archive_create(&editor, &scene, number) != 0;
}

// Tests build packed, foreign-endian and malformed fixtures with Kauai's raw group reader.
inline PGeneralGroup ReadEvents(PDataBlock block, short *byteOrder, bool starts)
{
    short order;
    auto group = GeneralGroup::PggRead(block, &order);
    if (!group)
        return nullptr;
    const unsigned short one = 1;
    const bool nativeBig = *reinterpret_cast<const byte *>(&one) == 0;
    auto rows = group->CoreRows();
    const auto services = mm_scene_group_wire_services();
    if (!mm_scene_valid_events(&rows, (order == kboCur) == nativeBig, starts, &services))
    {
        group->Release();
        return nullptr;
    }
    if (byteOrder)
        *byteOrder = order;
    return group;
}

inline bool WriteEvents(PChunkyFile file, ChunkNumber number, PGeneralGroup frames, PGeneralGroup starts)
{
    MovieArchiveHost host{file};
    const auto editor = host.Editor();
    auto frameRows = frames->CoreRows();
    auto startRows = starts->CoreRows();
    return mm_scene_archive_write_groups(&editor, number, &frameRows, &startRows) != 0;
}

inline SceneEvent ReadTrackEvent(const MMSceneTrack *track, MMI32 index)
{
    MMSceneTrackEvent event;
    if (!mm_scene_track_get(track, index, &event))
        std::abort();
    return {event.frame, static_cast<SceneEventType>(event.kind)};
}

inline int InsertTrackEvent(MMSceneTrack *track, MMI32 index, MMI32 bytes, const void *data, const SceneEvent &event)
{
    const MMSceneTrackEvent header = {event.nfrm, event.sevt};
    return mm_scene_track_insert(track, index, bytes, data, &header);
}
} // namespace SceneArchiveFixture
