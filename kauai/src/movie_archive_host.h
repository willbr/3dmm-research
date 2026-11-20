#ifndef KAUAI_MOVIE_ARCHIVE_HOST_H
#define KAUAI_MOVIE_ARCHIVE_HOST_H
#include "engine_read_stream.h"
#include "mm/movie_archive_edit.h"

struct MovieArchiveHost
{
    PChunkyFile file;
    DataBlock input, output;

    MMStorageWriter OutputWriter(MMU32 bytes)
    {
        return {&output, bytes, [](void *block, MMU32 offset, const void *data, size_t count) -> int {
            return static_cast<PDataBlock>(block)->FWriteRgb(const_cast<void *>(data), count, offset);
        }};
    }

    MMMovieArchiveEditor Editor()
    {
        const unsigned short one = 1;
        return {
            this, sizeof(achar), koskCur, *reinterpret_cast<const unsigned char *>(&one) == 0,
            [](void *, const void *bytes, size_t count, int encoding, void *text, size_t capacity) -> size_t {
                return CchTranslateRgb(const_cast<void *>(bytes), count, encoding, static_cast<achar *>(text), capacity);
            },
            [](void *value) { return static_cast<MovieArchiveHost *>(value)->file->ReadChunkAccess(); },
            [](void *value, MMU32 type, MMU32 number, MMReadStream *stream) -> int {
                auto &io = *static_cast<MovieArchiveHost *>(value);
                if (!io.file->FFind(type, number, &io.input) || !io.input.FUnpackData()) return false;
                *stream = EngineReadStream::FromBlock(io.input);
                return true;
            },
            [](void *value, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *writer) -> int {
                auto &io = *static_cast<MovieArchiveHost *>(value);
                if (!writer) return io.file->FAdd(bytes, type, number);
                if (!io.file->FAdd(bytes, type, number, &io.output)) return false;
                *writer = io.OutputWriter(bytes);
                return true;
            },
            [](void *value, MMU32 type, MMU32 parent, MMU32 childType, MMU32 child, MMU32 slot) -> int {
                return static_cast<MovieArchiveHost *>(value)->file->FAdoptChild(type, parent, childType, child, slot);
            },
            [](void *value, MMU32 type, MMU32 parent, MMU32 childType, MMU32 child, MMU32 slot) {
                static_cast<MovieArchiveHost *>(value)->file->DeleteChild(type, parent, childType, child, slot);
            },
            [](void *value, MMU32 type, MMU32 number, const void *bytes, MMU32 size) -> int {
                return static_cast<MovieArchiveHost *>(value)->file->FPutPv(const_cast<void *>(bytes), size, type, number);
            },
            [](void *value, MMU32 type, MMU32 number, MMU32 newNumber) -> int {
                auto file = static_cast<MovieArchiveHost *>(value)->file;
                file->Move(type, number, type, newNumber);
                return file->FFind(type, newNumber);
            },
            [](void *value, MMU32 type, MMU32 number, const MMMovieTitle *title) -> int {
                const auto io = KauaiChunkTitleIO();
                return mm_chunk_title_write(title, static_cast<MovieArchiveHost *>(value)->file, type, number, &io);
            },
            [](void *value, MMU32 type, MMU32 number, MMU32 bytes, MMStorageWriter *writer) -> int {
                auto &io = *static_cast<MovieArchiveHost *>(value);
                if (!io.file->FPut(bytes, type, number, &io.output)) return false;
                *writer = io.OutputWriter(bytes);
                return true;
            },
            [](void *value, MMU32 type, MMU32 number, MMU32 offset, const void *bytes, MMU32 size) -> int {
                auto &io = *static_cast<MovieArchiveHost *>(value);
                if (!io.file->FFind(type, number, &io.output)) return false;
                const bool packed = io.output.FPacked();
                return io.output.FUnpackData() && io.output.FWriteRgb(const_cast<void *>(bytes), size, offset) &&
                       (!packed || io.file->FPutBlck(&io.output, type, number));
            },
            [](void *value, MMU32 type, MMU32 number) {
                static_cast<MovieArchiveHost *>(value)->file->Delete(type, number);
            }
        };
    }
};

#endif
