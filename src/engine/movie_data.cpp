/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "movie_data.h"
#include "engine_read_stream.h"
#include "mm/movie_session.h"
ASSERTNAME

namespace
{
struct NativeMovieHost
{
    PFilename filename;
};
struct NativeRollCallHost
{
    String name;
    DataBlock block;
};
int ReadMovieRecord(void *context, MMU32, const MMChunkEntry *entry, void *bytes, MMU32 count)
{
    DataBlock block;
    if (!static_cast<PChunkyFile>(context)->FFind(entry->type, entry->number, &block) ||
        !block.FUnpackData() || block.Cb() != static_cast<LegacyLong>(count))
        return MM_MOVIE_BAD_FILE;
    return block.FReadRgb(bytes, count, 0) ? MM_MOVIE_OK : MM_MOVIE_IO;
}
bool CheckMovieOpen(int result)
{
    if (result == MM_MOVIE_OK)
        return true;
    PushErc(result == MM_MOVIE_BAD_VERSION ? ercSocBadVersion : ercSocBadFile);
    return false;
}
MMRollCallActor CoreActor(const RollCallActorEntry &a)
{
    return {{a.arid, a.cactRef, a.tagTmpl.sid, a.grfbrws, a.tagTmpl.ctg, a.tagTmpl.cno},
            a.tagTmpl.sid == ksidUseCrf ? a.tagTmpl.pcrf : nullptr};
}
RollCallActorEntry NativeActor(const MMRollCallActor &a)
{
    RollCallActorEntry out = {};
    out.arid = a.data.actor_id;
    out.cactRef = a.data.reference_count;
    out.grfbrws = a.data.browser_flags;
    out.tagTmpl.sid = a.data.source_id;
    out.tagTmpl.ctg = a.data.template_type;
    out.tagTmpl.cno = a.data.template_number;
    out.tagTmpl.pcrf = static_cast<PChunkyResourceFile>(a.cache);
    return out;
}
void ReadActor(void *table, MMU32 index, MMRollCallActor *out)
{
    RollCallActorEntry a;
    static_cast<PRollCallTable>(table)->GetExtra(index, &a);
    *out = CoreActor(a);
}
void CloseActor(void *, MMRollCallActor *a)
{
    auto value = NativeActor(*a);
    TagManager::CloseTag(&value.tagTmpl);
    a->cache = value.tagTmpl.pcrf;
}
void ReleaseTable(void *p)
{
    static_cast<PRollCallTable>(p)->Release();
}
MMU32 CountActors(void *p)
{
    return static_cast<PRollCallTable>(p)->IvMac();
}
void *NewActors(void *, int wire)
{
    return RollCallTable::New(wire);
}
MMRollCallServices RollCallServices(NativeRollCallHost *host)
{
    return {host,
            [](void *c, void *resources, MMU32 movie, int *big) -> void * {
                auto h = static_cast<NativeRollCallHost *>(c);
                auto file = static_cast<PChunkyResourceFile>(resources)->Pcfl();
                ChildChunkIdentification child;
                if (!file->FGetKidChidCtg(kctgMvie, movie, 0, kctgGst, &child) ||
                    !file->FFind(child.cki.ctg, child.cki.cno, &h->block))
                {
                    PushErc(ercSocBadFile);
                    return nullptr;
                }
                return RollCallTable::Read(&h->block, big);
            },
            [](void *p) -> MMU32 { return static_cast<PRollCallTable>(p)->CbExtra(); },
            sizeof(MMRollCallActor),
            NewActors,
            ReleaseTable,
            CountActors,
            [](void *c, void *table, MMU32 i, unsigned char *bytes) {
                static_cast<PRollCallTable>(table)->GetStn(i, &static_cast<NativeRollCallHost *>(c)->name);
                AssertDo(mm_rollcall_table_extra(&static_cast<PRollCallTable>(table)->storage, i, bytes),
                         "bad wire row");
            },
            ReadActor,
            [](void *c, void *table, MMU32 i, MMRollCallActor *a) {
                static_cast<PRollCallTable>(table)->GetStn(i, &static_cast<NativeRollCallHost *>(c)->name);
                ReadActor(table, i, a);
            },
            [](void *c, void *table, const MMRollCallActor *a) -> int {
                auto value = NativeActor(*a);
                return static_cast<PRollCallTable>(table)->FAddStn(&static_cast<NativeRollCallHost *>(c)->name, &value);
            },
            [](void *c, void *table, const unsigned char *bytes) -> int {
                auto name = &static_cast<NativeRollCallHost *>(c)->name;
                return mm_rollcall_table_add(&static_cast<PRollCallTable>(table)->storage, name->Prgch(),
                                                   name->Cch(), bytes);
            },
            [](void *, MMRollCallActor *a, void *resources) -> int {
                auto value = NativeActor(*a);
                const bool ok = TagManager::FOpenTag(&value.tagTmpl, static_cast<PChunkyResourceFile>(resources));
                *a = CoreActor(value);
                return ok;
            },
            CloseActor,
            [](void *p) -> MMU32 { return static_cast<PRollCallTable>(p)->CbOnFile(); },
            [](void *c, void *file, MMU32 bytes, MMU32 *number) -> int {
                return static_cast<PChunkyFile>(file)->FAdd(bytes, kctgGst, number,
                                                            &static_cast<NativeRollCallHost *>(c)->block);
            },
            [](void *c, void *table) -> int {
                return static_cast<PRollCallTable>(table)->FWrite(&static_cast<NativeRollCallHost *>(c)->block);
            },
            [](void *, void *file, MMU32 movie, MMU32 number) -> int {
                return static_cast<PChunkyFile>(file)->FAdoptChild(kctgMvie, movie, kctgGst, number, 0);
            },
            [](void *, void *file, MMU32 number) { static_cast<PChunkyFile>(file)->Delete(kctgGst, number); }};
}
} // namespace
RollCallTable *RollCallTable::_New(MMU32 extra)
{
    auto table = NewObj RollCallTable;
    if (!table)
        return nullptr;
    const auto allocator = CollectionAllocator();
    if (mm_rollcall_table_init(&table->storage, extra, sizeof(achar), koskCur,
#ifdef DEBUG
                                     1,
#else
                                     0,
#endif
                                     &allocator) != MM_STORAGE_OK)
    {
        ReleasePpo(&table);
        return nullptr;
    }
    return table;
}
RollCallTable *RollCallTable::New(bool wire)
{
    return _New(wire ? MM_MOVIE_ACTOR_BYTES : sizeof(MMRollCallActor));
}
RollCallTable *RollCallTable::Read(PDataBlock block, int *bigEndian)
{
    auto table = _New(0);
    if (!table)
        return nullptr;
    if (!block->FUnpackData())
    {
        ReleasePpo(&table);
        return nullptr;
    }
    const auto stream = EngineReadStream::FromBlock(*block);
    const auto convert = [](void *, const void *input, size_t bytes, int encoding, void *output,
                            size_t capacity) -> size_t {
        return CchTranslateRgb(const_cast<void *>(input), bytes, encoding, static_cast<achar *>(output), capacity);
    };
    if (mm_rollcall_table_read(&table->storage, &stream, convert, nullptr, bigEndian) != MM_STORAGE_OK)
    {
        ReleasePpo(&table);
        return nullptr;
    }
    return table;
}
RollCallTable::~RollCallTable()
{
    if (storage.storage.allocator.release)
        AssertDo(mm_rollcall_table_dispose(&storage) == MM_STORAGE_OK, "invalid roll-call disposal");
}
void RollCallTable::GetStn(LegacyLong index, PString name)
{
    const void *text;
    MMU32 count;
    AssertDo(mm_rollcall_table_name(&storage, index, &text, &count), "bad actor name");
    name->SetRgch(const_cast<achar *>(static_cast<const achar *>(text)), count);
}
void RollCallTable::GetExtra(LegacyLong index, RollCallActorEntry *actor)
{
    MMRollCallActor value;
    AssertDo(mm_rollcall_table_extra(&storage, index, &value), "bad actor row");
    *actor = NativeActor(value);
}
void RollCallTable::PutExtra(LegacyLong index, const RollCallActorEntry *actor)
{
    const auto value = CoreActor(*actor);
    AssertDo(mm_rollcall_table_set_extra(&storage, index, &value), "bad actor row");
}
bool RollCallTable::FAddStn(PString name, const RollCallActorEntry *actor)
{
    const auto value = CoreActor(*actor);
    return mm_rollcall_table_add(&storage, name->Prgch(), name->Cch(), &value) != 0;
}
bool RollCallTable::FPutStn(LegacyLong index, PString name)
{
    return mm_rollcall_table_rename(&storage, index, name->Prgch(), name->Cch()) != 0;
}
void RollCallTable::Delete(LegacyLong index)
{
    AssertDo(mm_rollcall_table_delete(&storage, index), "bad actor deletion");
}
bool RollCallTable::FWrite(PDataBlock block)
{
    const MMStorageWriter writer = {block, static_cast<MMU32>(block->Cb()),
                                         [](void *c, MMU32 offset, const void *data, size_t bytes) -> int {
                                             return static_cast<PDataBlock>(c)->FWriteRgb(const_cast<void *>(data),
                                                                                          bytes, offset);
                                         }};
    const unsigned short one = 1;
    return mm_rollcall_table_write(&storage, &writer, *reinterpret_cast<const unsigned char *>(&one) == 0) ==
           MM_STORAGE_OK;
}
#ifdef DEBUG
void RollCallTable::AssertValid(LegacyUlong flags)
{
    BASE::AssertValid(flags);
    Assert(storage.stride >= 4 && storage.stride % 4 == 0, "bad roll-call stride");
    Assert(storage.count <= storage.storage.buffers[1].capacity / storage.stride, "bad roll-call count");
}
void RollCallTable::MarkMem()
{
    BASE::MarkMem();
    MarkPv(storage.storage.buffers[0].data);
    MarkPv(storage.storage.buffers[1].data);
}
#endif

MMMovieOwnerServices MovieData::_OwnerServices(void *context)
{
    return {
        context,
        [](void *) -> void * { return ChunkyFile::PcflCreateTemp(); },
        [](void *c) -> void * { return ChunkyFile::PcflOpen(static_cast<NativeMovieHost *>(c)->filename, fcflNil); },
        [](void *, void *file, MMU32 *number, MMMovieSession **out) -> int {
            const MMMovieSessionGraph graph = {
                file,
                [](void *context, MMChunkAccess *access) { *access = static_cast<PChunkyFile>(context)->ReadChunkAccess(); },
                [](void *context, void *, MMU32 position, const MMChunkEntry *entry, void *bytes, MMU32 count) -> int {
                    return ReadMovieRecord(context, position, entry, bytes, count);
                }};
            return CheckMovieOpen(mm_movie_session_open_graph(&graph, nullptr, number, out));
        },
        [](void *, void *file) -> void * { return ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(file), 0); },
        [](void *p) { static_cast<PChunkyFile>(p)->Release(); },
        [](void *p) { static_cast<PChunkyResourceFile>(p)->Release(); },
        NewActors,
        ReleaseTable,
        CountActors,
        ReadActor,
        CloseActor,
        [](void *p) -> void * { return static_cast<PChunkyResourceFile>(p)->Pcfl(); },
        [](void *, void *file, MMU32 *number) -> int {
            return FCreateRoot(static_cast<PChunkyFile>(file), number);
        },
        [](void *, void *resources, MMU32 number, void **actors, MMI32 *limit) -> int {
            PRollCallTable table = nullptr;
            LegacyLong next = *limit;
            const bool ok = FReadRollCall(static_cast<PChunkyResourceFile>(resources), number, &table, &next);
            if (ok)
                *limit = next;
            *actors = table;
            return ok;
        },
        [](void *, void *file, MMU32 number, MMMovieTitle *title) {
            String value;
            static_cast<PChunkyFile>(file)->FGetName(kctgMvie, number, &value);
            AssertDo(mm_movie_title_set(title, value.Prgch(), value.Cch()), "bad movie title");
        },
        [](void *, void *file, MMU32 movie, MMU32 *previous) -> int {
            ChildChunkIdentification child;
            const bool found = static_cast<PChunkyFile>(file)->FGetKidChidCtg(kctgMvie, movie, 0, kctgGst, &child);
            if (found)
                *previous = child.cki.cno;
            return found;
        },
        [](void *, void *file, MMU32 movie, void *actors, MMU32 *written) -> int {
            return FWriteRollCall(static_cast<PChunkyFile>(file), movie, static_cast<PRollCallTable>(actors), written);
        },
        [](void *, void *file, MMU32 movie, MMU32 previous) {
            static_cast<PChunkyFile>(file)->DeleteChild(kctgMvie, movie, kctgGst, previous, 0);
        },
        [](void *, void *file, MMU32 movie, const MMMovieTitle *title) -> int {
            String value;
            value.SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(title))),
                          title->count);
            return static_cast<PChunkyFile>(file)->FSetName(kctgMvie, movie, &value);
        },
        [](void *c, void *file) -> int {
            return static_cast<PChunkyFile>(file)->FSave(kctgSoc, static_cast<NativeMovieHost *>(c)->filename);
        },
        [](void *file) { static_cast<PChunkyFile>(file)->SetTemp(fFalse); }};
}
MovieData::~MovieData()
{
    const auto s = _OwnerServices(nullptr);
    mm_movie_state_dispose(this, &s);
}
void MovieData::ClearActors()
{
    const auto s = _OwnerServices(nullptr);
    mm_movie_clear_actors(this, &s);
}
void MovieData::ReleaseResources()
{
    const auto s = _OwnerServices(nullptr);
    mm_movie_release_resources(this, &s);
}
void MovieData::GetTitle(PString out) const
{
    out->SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(&title))), title.count);
}
void MovieData::SetTitle(PString value)
{
    AssertDo(mm_movie_title_set(&title, value->Prgch(), value->Cch()), "bad movie title");
}
bool MovieData::FWriteTitle(PChunkyFile file)
{
    String value;
    GetTitle(&value);
    return file->FSetName(kctgMvie, movieNumber, &value);
}
bool MovieData::FInitEmpty()
{
    const auto s = _OwnerServices(nullptr);
    return mm_movie_init_empty(this, &s) != 0;
}
bool MovieData::FLoad(PFilename filename, ChunkNumber number)
{
    NativeMovieHost host = {filename};
    const auto s = _OwnerServices(&host);
    return mm_movie_load(this, number, &s) != 0;
}
bool MovieData::FResolveScene(ChildChunkID slot, ChunkNumber *number)
{
    const auto s = _OwnerServices(nullptr);
    return mm_movie_state_scene_number(this, slot, number, &s) != 0;
}
bool MovieData::FSave(PFilename filename)
{
    NativeMovieHost host = {filename};
    const auto s = _OwnerServices(&host);
    return mm_movie_save(this, &s) != 0;
}
namespace
{
struct NativeMovieRootHost
{
    PChunkyFile file;
    DataBlock block;
};
MMMovieRootServices MovieRootServices(NativeMovieRootHost *host)
{
    return {
        host,
        nullptr,
        nullptr,
        [](void *c, MMU32 bytes, MMU32 *number) -> int {
            auto h = static_cast<NativeMovieRootHost *>(c);
            return h->file->FAdd(bytes, kctgMvie, number, &h->block);
        },
        [](void *c, const void *bytes, MMU32 count) -> int {
            return static_cast<NativeMovieRootHost *>(c)->block.FWriteRgb(const_cast<void *>(bytes), count, 0);
        },
        [](void *c, MMU32 number) { static_cast<NativeMovieRootHost *>(c)->file->Delete(kctgMvie, number); },
        nullptr};
}
} // namespace
bool MovieData::FCreateRoot(PChunkyFile file, ChunkNumber *number)
{
    NativeMovieRootHost host = {file};
    const auto s = MovieRootServices(&host);
    const unsigned short one = 1;
    return mm_movie_create_root(number, koskCur, *reinterpret_cast<const byte *>(&one) == 0, &s) != 0;
}
bool MovieData::FVerifyVersion(PChunkyFile file, ChunkNumber *number, MMMovieHeader *header)
{
    const MMMovieChunkView view = {file->ReadChunkAccess(), file, ReadMovieRecord};
    return CheckMovieOpen(mm_movie_view_root(&view, number, header));
}

bool MovieData::FReadRollCall(PChunkyResourceFile resources, ChunkNumber number, PRollCallTable *actors,
                              LegacyLong *limit)
{
    Assert(*actors == pvNil, "Overwriting existing StringTable_GST");
    NativeRollCallHost host;
    const auto s = RollCallServices(&host);
    void *output = nullptr;
    MMI32 next = limit ? *limit : 0;
    const bool ok = mm_movie_read_rollcall(resources, number, &output, limit ? &next : nullptr, &s) != 0;
    if (ok && limit)
        *limit = next;
    *actors = static_cast<PRollCallTable>(output);
    return ok;
}
bool MovieData::FWriteRollCall(PChunkyFile file, ChunkNumber number, PRollCallTable actors, ChunkNumber *written)
{
    NativeRollCallHost host;
    const auto s = RollCallServices(&host);
    return mm_movie_write_rollcall(file, number, actors, written, &s) != 0;
}
