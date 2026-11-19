#ifndef ENGINE_SOUND_WRITER_H
#define ENGINE_SOUND_WRITER_H

#include "kauai_core.h"
#include "handle_services.h"
#include "mm/sound_resource.h"

struct EngineSoundWriter
{
    PChunkyFile file;
    DataBlock child;

    MMSoundWriteServices Services()
    {
        return {
            this, 0, koskCur, KauaiCopyAllocator(),
            [](void *p, const unsigned char *header, MMU32 *number) -> int {
                return static_cast<EngineSoundWriter *>(p)->file->FAddPv(
                    const_cast<unsigned char *>(header), MM_SOUND_HEADER_BYTES, MM_SOUND_TYPE, number);
            },
            [](void *p, MMU32 parent, MMU32 slot, MMU32 type, MMU32 bytes, MMStorageWriter *out) -> int {
                auto &writer = *static_cast<EngineSoundWriter *>(p);
                ChunkNumber number;
                if (!writer.file->FAddChild(MM_SOUND_TYPE, parent, slot, bytes, type, &number, &writer.child))
                    return 0;
                *out = {&writer.child, bytes, [](void *block, MMU32 offset, const void *data, size_t count) -> int {
                    return static_cast<PDataBlock>(block)->FWriteRgb(const_cast<void *>(data), count, offset);
                }};
                return 1;
            },
            [](void *p, MMU32 number, const void *name) -> int {
                return static_cast<EngineSoundWriter *>(p)->file->FSetName(
                    MM_SOUND_TYPE, number, const_cast<String *>(static_cast<const String *>(name)));
            },
            [](void *p, MMU32 number) {
                static_cast<EngineSoundWriter *>(p)->file->Delete(MM_SOUND_TYPE, number);
            }};
    }
};
#endif
