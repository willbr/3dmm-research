/* Headless Movie save publication and working-file compatibility probe. */
#ifndef MOVIE_SAVE_FIXTURE_H
#define MOVIE_SAVE_FIXTURE_H
#include "mm/private_archive.h"
#include "movie_archive_fixture.h"
#include "movie_state_fixture.h"
class MovieSaveProbe
{
  public:
    static void Container(PMovie movie, MMMovieContainer container)
    {
        movie->_save.container = container;
    }
    static MMMovieState *Data(PMovie movie)
    {
        return &movie->_data;
    }
    static bool AutosaveDirty(PMovie movie) { return movie->_save.autosaveDirty != 0; }
    static void Record(PMovie movie, std::vector<LegacyLong> &trace, int operation, bool result)
    {
        Filename path;
        bool named = movie->FGetFni(&path);
        auto data = Data(movie);
        const auto resources = static_cast<MMResourceCache *>(data->resources);
        const auto backing = resources ? mm_resource_cache_reader(resources)->storage->owner->files.main_file : nullptr;
        LegacyLong row[] = {operation,
                            result,
                            movie->_save.autosaveDirty,
                            movie->_fDirty,
                            movie->_save.closing,
                            movie->_save.nameValid,
                            movie->_save.readOnly,
                            movie->_save.containerNamed,
                            movie->_save.file != nullptr,
                            named,
                            named ? LegacyLong(path.Ftg()) : 0,
                            LegacyLong(movie->Iscen()),
                            LegacyLong(movie->Cscen()),
                            LegacyLong(data->movieNumber),
                            backing ? static_cast<PFileObject>(backing)->FTemp() : -1};
        trace.insert(trace.end(), row, row + sizeof(row) / sizeof(row[0]));
        if (!resources)
            return;
        const auto chunks = mm_resource_cache_chunks(resources);
        for (MMU32 i = 0; i < chunks.count; ++i)
        {
            MMChunkEntry chunk;
            if (mm_chunk_at(&chunks, i, &chunk) != MM_CHUNK_OK)
                break;
            LegacyLong item[] = {LegacyLong(chunk.type), LegacyLong(chunk.number), LegacyLong(chunk.child_count)};
            trace.insert(trace.end(), item, item + 3);
            for (long j = 0; j < item[2]; ++j)
            {
                MMChunkChild child;
                if (mm_chunk_child_at(&chunks, i, j, &child) != MM_CHUNK_OK)
                {
                    trace.push_back(-1);
                    break;
                }
                LegacyLong link[] = {LegacyLong(child.type), LegacyLong(child.number), LegacyLong(child.slot)};
                trace.insert(trace.end(), link, link + 3);
            }
        }
        trace.push_back(-2);
    }
};
struct MovieSaveContainer
{
    bool bundled = false, fail = false;
    std::vector<LegacyLong> *trace;
};

static const MMMovieContainerServices movieSaveContainerServices = {
    [](void *p) { delete static_cast<MovieSaveContainer *>(p); },
    [](void *p) -> int { return static_cast<MovieSaveContainer *>(p)->bundled; },
    [](void *p, void *working, void *destination) -> int {
        auto container = static_cast<MovieSaveContainer *>(p);
        auto path = static_cast<PFilename>(destination);
        LegacyLong row[] = {800, working != nullptr, path && path->Ftg() == MM_SAVE_BUNDLE, container->fail};
        container->trace->insert(container->trace->end(), row, row + 4);
        return working && !container->fail;
    },
    [](void *) -> int { return false; }};
static bool TestMovieSave(PMovie parent)
{
    std::vector<LegacyLong> trace;
    bool ok = true;
    for (int mode = 0; ok && mode < 16; ++mode)
    {
        auto movie = Movie::PmvieNew(false, parent->Pmcc());
        Filename path;
        ok = movie && path.FGetTemp() && path.FChangeFtg((mode & 4) ? 'VMM' : kftg3mm);
        MovieSaveContainer *container = nullptr;
        if (ok && (mode & 2))
        {
            container = new MovieSaveContainer{(mode & 8) != 0, (mode & 1) != 0, &trace};
            MovieSaveProbe::Container(movie, {container, &movieSaveContainerServices});
        }
        auto record = [&](int operation, bool result) {
            MovieSaveProbe::Record(movie, trace, 100 * mode + operation, result);
        };
        if (ok)
        {
            record(0, movie->FAutoSave()); // No resources: cannot autosave.
            ok = movie->FNewScenInsCore(0);
        }
        if (ok)
        {
            movie->Pscen()->SetTransitionCore(transFadeToBlack);
            record(1, movie->FAutoSave());
            record(2, movie->FAutoSave()); // Clean temporary save is a no-op.
            record(3, movie->FSaveToFni(&path, (mode & 1) != 0));
            if (container)
                container->fail = false;
            record(4, movie->FSaveToFni(&path, true));
            movie->SetDirty();
            record(5, movie->FAutoSave(&path, true));
            MMResourceCache *resource = nullptr;
            record(6, movie->FEnsureAutosave(&resource));
            ok = resource && !resource->services.objects.state && movie->FSwitchScen(ivNil) && movie->FSwitchScen(0) &&
                 movie->Pscen()->Trans() == transFadeToBlack;
            record(7, ok);
            movie->SetDocClosing(true);
            record(8, movie->FAutoSave()); // Closing bypasses the clean-save fast path.
            movie->SetDocClosing(false);
            movie->ForceSaveAs();
            record(9, true);
        }
        ReleasePpo(&movie);
        path.FDelete();
    }
    // Frozen from the original native and SDL debug/release save controller.
#ifdef _WIN64
    const auto *bytes = reinterpret_cast<const unsigned char *>(trace.data());
    const auto byteCount = (trace.size)() * sizeof(LegacyLong);
    MMU32 hash = 2166136261U;
    for (size_t i = 0; i < byteCount; ++i)
        hash = (hash ^ bytes[i]) * 16777619U;
    ok = ok && byteCount == 29440 && hash == 0x696eaa7cU;
#endif
    if (const char *path = std::getenv("MM_MOVIE_SAVE_SNAPSHOT"))
    {
        FILE *file = std::fopen(path, "wb");
        if (!file)
            ok = false;
        else
        {
            ok = std::fwrite(trace.data(), sizeof(LegacyLong), (trace.size)(), file) == (trace.size)() && ok;
            ok = std::fclose(file) == 0 && ok;
        }
    }
    return ok;
}

static MMResourceCache *NewPrivateMovieResources()
{
    auto archive = KauaiTemporaryArchive();
    auto resources = NewTypedPrivateCache(archive, 0);
    mm_private_archive_close(archive);
    return resources;
}

static bool TestPrivateMovieSave(PMovie parent)
{
    auto movie = Movie::PmvieNew(false, parent->Pmcc());
    if (!movie)
        return false;
    auto resources = NewPrivateMovieResources();
    MovieSaveProbe::Data(movie)->resources = resources;

    Filename path, remembered;
    const bool havePath = path.FGetTemp() && path.FChangeFtg(kftg3mm);
    bool ok = resources && !resources->services.objects.state && havePath;
    if (ok)
    {
        movie->SetDirty();
        ok = movie->FAutoSave() && movie->FSaveToFni(&path, true) &&
             movie->FGetFni(&remembered) && remembered.FEqual(&path);
    }
    if (ok)
    {
        MMResourceCache *working = nullptr;
        ok = movie->FEnsureAutosave(&working) && working == resources;
        movie->SetDirty();
        ok = ok && movie->FAutoSave();
    }
    ReleasePpo(&movie);
    if (ok)
    {
        MovieStateFixture loaded;
        const auto services = EngineMovieHost::OwnerServices();
        ok = mm_movie_load(&loaded, &path, cnoNil, &services) && loaded.sceneCount == 0;
    }
    if (havePath && path.TExists() != tNo)
        path.FDelete();
    return ok;
}
#endif
