#pragma once

#include "movie_archive_host.h"
#include "mm/text_box_runtime.h"

namespace TextBoxArchiveFixture
{
inline void SetBounds(MMTextBoxState &state, MMI32 left, MMI32 top, MMI32 right, MMI32 bottom)
{
    state.left = left;
    state.top = top;
    state.right = right;
    state.bottom = bottom;
}

inline bool Write(PChunkyFile file, ChunkNumber number, const MMTextBoxState &state)
{
    MovieArchiveHost host{file};
    const auto editor = host.Editor();
    return mm_text_box_write(&state, number, &editor) > 0;
}

inline bool Read(PChunkyFile file, ChunkNumber number, MMTextBoxState &state, MMTextChunk *child)
{
    MovieArchiveHost host{file};
    const auto editor = host.Editor();
    return mm_text_box_read(&state, number, child, &editor) > 0;
}
} // namespace TextBoxArchiveFixture
