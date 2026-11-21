/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include <exception>
#include <new>
#include "rich_text_data.h"
#include "rich_text_store_host.h"
ASSERTNAME
static_assert(kctgRichText == MM_TEXT_ROOT && kctgText == MM_TEXT_BYTES && kctgTxtProps == MM_TEXT_PROPERTIES &&
                  kctgTxtPropArgs == MM_TEXT_ARGUMENTS && kcbMax == MM_TEXT_LIMIT,
              "rich text wire contract");
RichTextData::~RichTextData()
{
    Clear();
}
void RichTextData::Clear()
{
    auto state = RichTextStoreHost::State(*this);
    const auto objects = RichTextStoreHost::Objects();
    mm_rich_text_dispose(&state, &objects);
    text.pfil = nullptr;
    properties = nullptr;
    arguments = nullptr;
}
bool RichTextData::FRead(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    MovieArchiveHost movie{file};
    const auto editor = movie.Editor();
    RichTextStoreHost::Host host{editor, file->ReadOwner()};
    auto services = host.Read();
    auto state = RichTextStoreHost::State(*this);
    int result = mm_rich_text_read(&state, type, number, koskCur, RichTextStoreHost::NativeBig(), &services);
    if (result > 0)
        RichTextStoreHost::Publish(*this, state);
    host.Rethrow();
    return result > 0;
}
bool RichTextData::FCreateRoot(PChunkyFile file, ChunkIdentification *written, RichTextProperties *defaults,
                               PString fontName)
{
    MovieArchiveHost movie{file};
    const auto editor = movie.Editor();
    RichTextStoreHost::Host host{editor};
    auto services = host.Write();
    auto data = RichTextStoreHost::Defaults(*defaults);
    auto font = RichTextStoreHost::Font(*fontName);
    MMTextChunk out = {0, 0};
    int result = mm_rich_text_create(&data, &font, &out, RichTextStoreHost::NativeBig(), &services);
    if (out.type == MM_TEXT_ROOT)
    {
        written->ctg = out.type;
        written->cno = out.number;
    }
    host.Rethrow();
    return result > 0;
}
bool RichTextData::FWrite(PChunkyFile file, ChunkIdentification *written)
{
    MovieArchiveHost movie{file};
    const auto editor = movie.Editor();
    RichTextStoreHost::Host host{editor};
    auto state = RichTextStoreHost::State(*this);
    MMTextChunk out = {0, 0};
    int result = mm_rich_text_archive_write(&state, nullptr, &out, &host.archive);
    if (out.type == MM_TEXT_ROOT)
    {
        written->ctg = out.type;
        written->cno = out.number;
    }
    host.Rethrow();
    return result > 0;
}
