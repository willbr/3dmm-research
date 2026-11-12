/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "movie_data.h"
#include "engine_read_stream.h"
ASSERTNAME

namespace
{
const short kcvnCur = THREE_MM_MOVIE_CURRENT_VERSION;
const short kcvnBack = THREE_MM_MOVIE_BACKWARD_VERSION;
} // namespace

namespace
{
struct NativeMovieHost
{
    PFilename filename;
    PString title;
};
struct NativeRollCallHost
{
    String name;
    DataBlock block;
};
ThreeMMRollCallActor CoreActor(const RollCallActorEntry &a)
{
    return {{a.arid, a.cactRef, a.tagTmpl.sid, a.grfbrws, a.tagTmpl.ctg, a.tagTmpl.cno},
            a.tagTmpl.sid == ksidUseCrf ? a.tagTmpl.pcrf : nullptr};
}
RollCallActorEntry NativeActor(const ThreeMMRollCallActor &a)
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
void ReadActor(void *table, ThreeMMU32 index, ThreeMMRollCallActor *out)
{
    RollCallActorEntry a;
    static_cast<PStringTable_GST>(table)->GetExtra(index, &a);
    *out = CoreActor(a);
}
void CloseActor(void *, ThreeMMRollCallActor *a)
{
    auto value = NativeActor(*a);
    TagManager::CloseTag(&value.tagTmpl);
    a->cache = value.tagTmpl.pcrf;
}
void ReleaseTable(void *p)
{
    static_cast<PStringTable_GST>(p)->Release();
}
ThreeMMU32 CountActors(void *p)
{
    return static_cast<PStringTable_GST>(p)->IvMac();
}
void *NewActors(void *, int wire)
{
    return StringTable_GST::PgstNew(wire ? sizeof(RollCallActorEntryOnFile) : sizeof(RollCallActorEntry));
}
ThreeMMRollCallServices RollCallServices(NativeRollCallHost *host)
{
    return {host,
            [](void *c, void *resources, ThreeMMU32 movie, int *big) -> void * {
                auto h = static_cast<NativeRollCallHost *>(c);
                auto file = static_cast<PChunkyResourceFile>(resources)->Pcfl();
                ChildChunkIdentification child;
                if (!file->FGetKidChidCtg(kctgMvie, movie, 0, kctgGst, &child) ||
                    !file->FFind(child.cki.ctg, child.cki.cno, &h->block))
                {
                    PushErc(ercSocBadFile);
                    return nullptr;
                }
                short bo;
                auto table = StringTable_GST::PgstRead(&h->block, &bo);
                if (table)
                    *big = *reinterpret_cast<const unsigned char *>(&bo) == 0;
                return table;
            },
            [](void *p) -> ThreeMMU32 { return static_cast<PStringTable_GST>(p)->CbExtra(); },
            sizeof(RollCallActorEntry),
            NewActors,
            ReleaseTable,
            CountActors,
            [](void *c, void *table, ThreeMMU32 i, unsigned char *bytes) {
                static_cast<PStringTable_GST>(table)->GetStn(i, &static_cast<NativeRollCallHost *>(c)->name);
                static_cast<PStringTable_GST>(table)->GetExtra(i, bytes);
            },
            ReadActor,
            [](void *c, void *table, ThreeMMU32 i, ThreeMMRollCallActor *a) {
                static_cast<PStringTable_GST>(table)->GetStn(i, &static_cast<NativeRollCallHost *>(c)->name);
                ReadActor(table, i, a);
            },
            [](void *c, void *table, const ThreeMMRollCallActor *a) -> int {
                auto value = NativeActor(*a);
                return static_cast<PStringTable_GST>(table)->FAddStn(&static_cast<NativeRollCallHost *>(c)->name,
                                                                     &value);
            },
            [](void *c, void *table, const unsigned char *bytes) -> int {
                return static_cast<PStringTable_GST>(table)->FAddStn(&static_cast<NativeRollCallHost *>(c)->name,
                                                                     const_cast<unsigned char *>(bytes));
            },
            [](void *, ThreeMMRollCallActor *a, void *resources) -> int {
                auto value = NativeActor(*a);
                const bool ok = TagManager::FOpenTag(&value.tagTmpl, static_cast<PChunkyResourceFile>(resources));
                *a = CoreActor(value);
                return ok;
            },
            CloseActor,
            [](void *p) -> ThreeMMU32 { return static_cast<PStringTable_GST>(p)->CbOnFile(); },
            [](void *c, void *file, ThreeMMU32 bytes, ThreeMMU32 *number) -> int {
                return static_cast<PChunkyFile>(file)->FAdd(bytes, kctgGst, number,
                                                            &static_cast<NativeRollCallHost *>(c)->block);
            },
            [](void *c, void *table) -> int {
                return static_cast<PStringTable_GST>(table)->FWrite(&static_cast<NativeRollCallHost *>(c)->block);
            },
            [](void *, void *file, ThreeMMU32 movie, ThreeMMU32 number) -> int {
                return static_cast<PChunkyFile>(file)->FAdoptChild(kctgMvie, movie, kctgGst, number, 0);
            },
            [](void *, void *file, ThreeMMU32 number) { static_cast<PChunkyFile>(file)->Delete(kctgGst, number); }};
}
} // namespace
ThreeMMMovieOwnerServices MovieData::_OwnerServices(void *context)
{
    return {
        context,
        [](void *) -> void * { return ChunkyFile::PcflCreateTemp(); },
        [](void *c) -> void * { return ChunkyFile::PcflOpen(static_cast<NativeMovieHost *>(c)->filename, fcflNil); },
        [](void *, void *file, ThreeMMU32 *number) -> int {
            return FVerifyVersion(static_cast<PChunkyFile>(file), number);
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
        [](void *, void *file, ThreeMMU32 *number) -> int {
            return FCreateRoot(static_cast<PChunkyFile>(file), number);
        },
        [](void *, void *resources, ThreeMMU32 number, void **actors, ThreeMMI32 *limit) -> int {
            PStringTable_GST table = nullptr;
            LegacyLong next = *limit;
            const bool ok = FReadRollCall(static_cast<PChunkyResourceFile>(resources), number, &table, &next);
            if (ok)
                *limit = next;
            *actors = table;
            return ok;
        },
        [](void *c, void *file, ThreeMMU32 number) {
            static_cast<PChunkyFile>(file)->FGetName(kctgMvie, number, static_cast<NativeMovieHost *>(c)->title);
        },
        [](void *, void *file, ThreeMMU32 movie, ThreeMMI32 index) -> int {
            ChildChunkIdentification child;
            return static_cast<PChunkyFile>(file)->FGetKidChidCtg(kctgMvie, movie, index, kctgScen, &child);
        },
        [](void *, void *file, ThreeMMU32 movie, ThreeMMU32 *previous) -> int {
            ChildChunkIdentification child;
            const bool found = static_cast<PChunkyFile>(file)->FGetKidChidCtg(kctgMvie, movie, 0, kctgGst, &child);
            if (found)
                *previous = child.cki.cno;
            return found;
        },
        [](void *, void *file, ThreeMMU32 movie, void *actors, ThreeMMU32 *written) -> int {
            return FWriteRollCall(static_cast<PChunkyFile>(file), movie, static_cast<PStringTable_GST>(actors),
                                  written);
        },
        [](void *, void *file, ThreeMMU32 movie, ThreeMMU32 previous) {
            static_cast<PChunkyFile>(file)->DeleteChild(kctgMvie, movie, kctgGst, previous, 0);
        },
        [](void *c, void *file, ThreeMMU32 movie) -> int {
            return static_cast<PChunkyFile>(file)->FSetName(kctgMvie, movie, static_cast<NativeMovieHost *>(c)->title);
        },
        [](void *c, void *file) -> int {
            return static_cast<PChunkyFile>(file)->FSave(kctgSoc, static_cast<NativeMovieHost *>(c)->filename);
        },
        [](void *file) { static_cast<PChunkyFile>(file)->SetTemp(fFalse); }};
}
MovieData::~MovieData()
{
    const auto s = _OwnerServices(nullptr);
    three_mm_movie_state_dispose(this, &s);
}
void MovieData::ClearActors()
{
    const auto s = _OwnerServices(nullptr);
    three_mm_movie_clear_actors(this, &s);
}
void MovieData::ReleaseResources()
{
    const auto s = _OwnerServices(nullptr);
    three_mm_movie_release_resources(this, &s);
}
bool MovieData::FInitEmpty()
{
    const auto s = _OwnerServices(nullptr);
    if (!three_mm_movie_init_empty(this, &s))
        return false;
    title.SetNil();
    return true;
}
bool MovieData::FLoad(PFilename filename, ChunkNumber number)
{
    String loadedTitle;
    NativeMovieHost host = {filename, &loadedTitle};
    const auto s = _OwnerServices(&host);
    if (!three_mm_movie_load(this, number, &s))
        return false;
    title = loadedTitle;
    return true;
}
bool MovieData::FSave(PFilename filename)
{
    NativeMovieHost host = {filename, &title};
    const auto s = _OwnerServices(&host);
    return three_mm_movie_save(this, &s) != 0;
}

bool MovieData::FCreateRoot(PChunkyFile file, ChunkNumber *number)
{
    DataBlock block;
    if (!file->FAdd(size(MovieFilePrefix), kctgMvie, number, &block))
        return fFalse;
    MovieFilePrefix prefix;
    prefix.bo = kboCur;
    prefix.osk = koskCur;
    prefix.dver.Set(kcvnCur, kcvnBack);
    if (block.FWrite(&prefix))
        return fTrue;
    file->Delete(kctgMvie, *number);
    *number = cnoNil;
    return fFalse;
}

bool MovieData::FVerifyVersion(PChunkyFile pcfl, ChunkNumber *pcno, ThreeMMMovieHeader *header)
{
    AssertPo(pcfl, 0);

    ChildChunkIdentification kid;
    ChunkNumber cnoMvie = pcno != pvNil ? *pcno : cnoNil;
    DataBlock blck;

    // Get the cno of the first kid of the movie
    if (pvNil == pcno || cnoNil == *pcno)
    {
        if (!pcfl->FGetCkiCtg(kctgMvie, 0, &(kid.cki)))
        {
            PushErc(ercSocBadFile);
            return fFalse;
        }
        cnoMvie = kid.cki.cno;
        if (pvNil != pcno)
            *pcno = cnoMvie;
    }

    // Get version number of the file
    if (!pcfl->FFind(kctgMvie, cnoMvie, &blck) || !blck.FUnpackData())
    {
        PushErc(ercSocBadFile);
        return fFalse;
    }

    const auto stream = EngineReadStream::FromBlock(blck);
    ThreeMMMovieHeader decoded;
    const int result = three_mm_movie_read_header(&stream, &decoded);
    if (result != THREE_MM_MOVIE_OK)
    {
        PushErc(result == THREE_MM_MOVIE_BAD_VERSION ? ercSocBadVersion : ercSocBadFile);
        return fFalse;
    }
    if (header)
        *header = decoded;
    return fTrue;
}

bool MovieData::FReadRollCall(PChunkyResourceFile resources, ChunkNumber number, PStringTable_GST *actors,
                              LegacyLong *limit)
{
    Assert(*actors == pvNil, "Overwriting existing StringTable_GST");
    NativeRollCallHost host;
    const auto s = RollCallServices(&host);
    void *output = nullptr;
    ThreeMMI32 next = limit ? *limit : 0;
    const bool ok = three_mm_movie_read_rollcall(resources, number, &output, limit ? &next : nullptr, &s) != 0;
    if (ok && limit)
        *limit = next;
    *actors = static_cast<PStringTable_GST>(output);
    return ok;
}
bool MovieData::FWriteRollCall(PChunkyFile file, ChunkNumber number, PStringTable_GST actors, ChunkNumber *written)
{
    NativeRollCallHost host;
    const auto s = RollCallServices(&host);
    return three_mm_movie_write_rollcall(file, number, actors, written, &s) != 0;
}
