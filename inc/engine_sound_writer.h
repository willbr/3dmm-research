#ifndef ENGINE_SOUND_WRITER_H
#define ENGINE_SOUND_WRITER_H

#include "kauai_core.h"
#include "handle_services.h"
#include "mm/sound_resource.h"

struct EngineSoundWriter
{
    MMArchiveWriter archive;
    byte wire[kcbMaxDataStn];
    MMChunkNameValue name;

    EngineSoundWriter(PChunkyFile file, PString text) : EngineSoundWriter(file->WriteIndex(), text) {}

    EngineSoundWriter(const MMArchiveWriter &writer, PString text)
        : archive(writer), name(ChunkyFile::EncodeName(text, wire))
    {
        // The original sound importer writes little-endian envelopes.
        archive.big_endian = 0;
    }
};

struct EngineSoundImportHost
{
    PFileObject file;
    Filename filename;
    byte wire[kcbMaxDataStn];

    explicit EngineSoundImportHost(PFileObject source) : file(source) {}

    MMSoundImportSource Input(PString name);
    static void Bind(void *source, void *name, MMSoundImportSource *output)
    {
        *output = static_cast<EngineSoundImportHost *>(source)->Input(static_cast<PString>(name));
    }
    static int Wave(void *, const MMReadStream *, MMI32 style, const MMChunkNameValue *, MMU32 *,
                    const MMArchiveWriter *);
};
#endif
