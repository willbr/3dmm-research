#ifndef KAUAI_MOVIE_ARCHIVE_HOST_H
#define KAUAI_MOVIE_ARCHIVE_HOST_H
#include "engine_read_stream.h"
#include "mm/chunk_storage.h"
#include "mm/movie_archive_edit.h"

struct MovieArchiveHost
{
    PChunkyFile file;
    DataBlock input, output;
    MMBlockIO output_io{};

    bool OutputWriter(MMStorageWriter *writer)
    {
        return mm_owned_block_io(output.CoreBlock(), &output_io) == MM_DATA_OK &&
               mm_block_io_writer(&output_io, writer) == MM_DATA_OK;
    }

    MMMovieArchiveEditor Editor()
    {
        const unsigned short one = 1;
        return {
            this, sizeof(achar), koskCur, *reinterpret_cast<const unsigned char *>(&one) == 0,
            [](void *, const void *bytes, size_t count, int encoding, void *text, size_t capacity) -> size_t {
                return CchTranslateRgb(const_cast<void *>(bytes), count, encoding, static_cast<achar *>(text), capacity);
            },
            [](void *value, MMU32 type, MMU32 number, MMReadStream *stream) -> int {
                auto &io = *static_cast<MovieArchiveHost *>(value);
                if (!io.file->FFind(type, number, &io.input) || !io.input.FUnpackData()) return false;
                *stream = EngineReadStream::FromBlock(io.input);
                return true;
            },
            [](void *value, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *writer) -> int {
                auto &io = *static_cast<MovieArchiveHost *>(value);
                const auto archive = io.file->WriteIndex();
                auto block = writer ? io.output.CoreBlock() : nullptr;
                if (block)
                    mm_owned_block_clear(block);
                if (mm_chunk_add_block(&archive.edit, type, bytes, number, block) == MM_CHUNK_OK)
                {
                    if (!writer || io.OutputWriter(writer))
                        return true;
                    mm_chunk_delete(&archive.edit, type, *number);
                    mm_owned_block_clear(block);
                }
                archive.invalidate_number(number);
                return false;
            },
            [](void *value, MMU32 type, MMU32 number, const void *bytes, MMU32 size) -> int {
                const auto archive = static_cast<MovieArchiveHost *>(value)->file->WriteIndex();
                return mm_chunk_put_bytes(&archive.edit, archive.payloads->files, type, number, bytes, size) == MM_CHUNK_OK;
            },
            [](void *value, MMU32 type, MMU32 number, const MMMovieTitle *title) -> int {
                const auto io = KauaiChunkTitleIO();
                return mm_chunk_title_write(title, static_cast<MovieArchiveHost *>(value)->file, type, number, &io);
            },
            [](void *value, MMU32 type, MMU32 number, MMU32 bytes, MMStorageWriter *writer) -> int {
                auto &io = *static_cast<MovieArchiveHost *>(value);
                return io.file->FPut(bytes, type, number, &io.output) && io.OutputWriter(writer);
            },
            [](void *value, MMU32 type, MMU32 number, MMU32 offset, const void *bytes, MMU32 size) -> int {
                auto &io = *static_cast<MovieArchiveHost *>(value);
                if (!io.file->FFind(type, number, &io.output)) return false;
                const bool packed = io.output.FPacked();
                return io.output.FUnpackData() && io.output.FWriteRgb(const_cast<void *>(bytes), size, offset) &&
                       (!packed || io.file->FPutBlck(&io.output, type, number));
            },
            file->EditIndex()
        };
    }
};

#endif
