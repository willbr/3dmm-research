#ifndef ENGINE_SOUND_WRITER_H
#define ENGINE_SOUND_WRITER_H

#include "kauai_core.h"
#include "handle_services.h"
#include "mm/sound_resource.h"

struct EngineSoundWriter
{
    PChunkyFile file;
    DataBlock child;
    MMBlockIO child_io{};

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
                return mm_owned_block_io(writer.child.CoreBlock(), &writer.child_io) == MM_DATA_OK &&
                       mm_block_io_writer(&writer.child_io, out) == MM_DATA_OK;
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
