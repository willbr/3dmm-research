/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "engine_movie_archive.h"
#include "mm/text_store.h"
ASSERTNAME

bool TextBoxData::FRead(PChunkyFile file, ChunkNumber number, ChunkIdentification *richText)
{
    MovieArchiveHost host{file, {}, {}};
    const auto editor = host.Editor();
    MMTextChunk child;
    if (mm_text_box_read(&state, number, richText ? &child : nullptr, &editor) <= 0)
        return false;
    if (richText)
        *richText = {child.type, child.number};
    return true;
}

bool TextBoxData::FWrite(PChunkyFile file, ChunkNumber number)
{
    MovieArchiveHost host{file, {}, {}};
    const auto editor = host.Editor();
    return mm_text_box_write(&state, number, &editor) > 0;
}