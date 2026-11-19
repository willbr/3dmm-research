/* Headless Movie composition and imported roll-call/actor-ID probe. */
#ifndef MOVIE_COMPOSE_FIXTURE_H
#define MOVIE_COMPOSE_FIXTURE_H
class MovieComposeSceneAccess : public Scene
{
  public:
    static bool Thumbnail(PScene scene, int color)
    {
        byte pixels[48];
        std::memset(pixels, color, sizeof(pixels));
        RC bounds(0, 0, 8, 6);
        auto bitmap = MaskedBitmapMBMP::PmbmpNew(pixels, 8, 6, &bounds, 0, 0, 0);
        if (!bitmap)
            return false;
        auto set = &MovieComposeSceneAccess::_SetOwner;
        (scene->*set)(MM_SCENE_THUMBNAIL, bitmap);
        return true;
    }
};
class MovieComposeClient : public MovieEditClient
{
  public:
    int actorTools = 0, textTools = 0, sceneNuked = 0;
    void EnableActorTools() override
    {
        ++actorTools;
    }
    void EnableTboxTools() override
    {
        ++textTools;
    }
    void SceneNuked() override
    {
        ++sceneNuked;
    }
};
#ifdef MM_ENGINE_MOVIE_COMPOSE_H
static bool TestComposeLiveIndex()
{
    auto client = NewObj MovieComposeClient();
    auto movie = client ? Movie::PmvieNew(false, client) : nullptr;
    CompositeMovie composite = {};
    bool ok = movie != nullptr;
    for (int i = 0; ok && i < 2; ++i)
        ok = movie->FNewScenInsCore(i) && MovieComposeSceneAccess::Thumbnail(movie->Pscen(), 30 + i);
    long insertion = 0;
    if (ok)
        ok = movie->FAddToCmvi(&composite, &insertion);
    if (ok)
    {
        SceneDescriptor deleted;
        composite.pglscend->Get(0, &deleted);
        deleted.fNuked = true;
        composite.pglscend->Put(0, &deleted);
        auto current = movie->Pscen();
        ok = movie->FSetCmvi(&composite) && movie->Cscen() == 1 && movie->Iscen() == 0 && movie->Pscen() == current;
    }
    if (composite.pglscend && composite.pglmvied)
        composite.Empty();
    if (movie)
        movie->ClearUndo();
    ReleasePpo(&movie);
    ReleasePpo(&client);
    return ok;
}
#endif
static bool TestMovieCompose()
{
    FILE *out = nullptr;
    if (const char *path = std::getenv("MM_MOVIE_COMPOSE_SNAPSHOT"))
        out = std::fopen(path, "w+b");
    else
        out = std::tmpfile();
    if (!out)
        return false;
    auto record = [&](const void *p, size_t n) { return std::fwrite(p, 1, n, out) == n; };
    bool ok = true;
    for (int mode = 0; ok && mode < 8; ++mode)
    {
        auto client = NewObj MovieComposeClient();
        auto otherClient = NewObj MovieComposeClient();
        auto movie = client ? Movie::PmvieNew(false, client) : nullptr;
        auto other = otherClient ? Movie::PmvieNew(false, otherClient) : nullptr;
        CompositeMovie composite = {};
        long insertion = 0;
        ok = movie && other;
        for (int which = 0; ok && which < 2; ++which)
        {
            auto target = which ? other : movie;
            for (int i = 0; ok && i < (mode == 5 && !which ? 0 : 2); ++i)
            {
                ok = target->FNewScenInsCore(i) &&
                     MovieComposeSceneAccess::Thumbnail(target->Pscen(), 10 + i + 2 * which);
                if (ok)
                    target->Pscen()->SetTransitionCore(TRANS(1 + i + 2 * which));
            }
        }
        if (ok && mode == 5)
            ok = movie->FEnsureAutosave();
        auto state = [&](int operation) {
            std::vector<LegacyLong> trace;
            MovieSaveProbe::Record(movie, trace, operation, true);
            auto data = MovieSaveProbe::Data(movie);
            auto external = MovieSaveProbe::Data(other)->Resources();
            LegacyLong fields[] = {mode,
                                   operation,
                                   LegacyLong(insertion),
                                   LegacyLong(client->actorTools),
                                   LegacyLong(client->textTools),
                                   LegacyLong(client->sceneNuked),
                                   LegacyLong(client->rollcalls),
                                   data->Resources() ? data->Resources()->CactRef() : 0,
                                   composite.pglscend ? composite.pglscend->IvMac() : -1,
                                   composite.pglmvied ? composite.pglmvied->IvMac() : -1};
            if (!record(fields, sizeof(fields)) || !record(trace.data(), (trace.size)() * sizeof(trace[0])))
                return false;
            if (composite.pglmvied)
                for (long i = 0; i < composite.pglmvied->IvMac(); ++i)
                {
                    MovieDescriptor row;
                    composite.pglmvied->Get(i, &row);
                    LegacyLong values[] = {LegacyLong(i),
                                           row.pcrf == data->Resources() ? 0
                                           : row.pcrf == external        ? 1
                                                                         : 2,
                                           LegacyLong(row.cno), LegacyLong(row.aridLim), row.pcrf->CactRef()};
                    if (!record(values, sizeof(values)))
                        return false;
                }
            if (composite.pglscend)
                for (long i = 0; i < composite.pglscend->IvMac(); ++i)
                {
                    SceneDescriptor row;
                    composite.pglscend->Get(i, &row);
                    LegacyLong values[] = {LegacyLong(i),        LegacyLong(row.imvied), LegacyLong(row.cno),
                                           LegacyLong(row.chid), LegacyLong(row.trans),  row.fNuked,
                                           row.pmbmp->CactRef(), row.pmbmp->CbOnFile()};
                    if (!record(values, sizeof(values)))
                        return false;
                }
            if (data->Resources())
                for (long i = 0; i < movie->Cscen(); ++i)
                {
                    ChildChunkIdentification child;
                    TRANS transition;
                    auto file = data->Resources()->Pcfl();
                    if (!file->FGetKidChidCtg(kctgMvie, data->movieNumber, i, kctgScen, &child) ||
                        !Scene::FTransOnFile(data->Resources(), child.cki.cno, &transition))
                        return false;
                    LegacyLong values[] = {LegacyLong(i), LegacyLong(child.cki.cno), LegacyLong(transition)};
                    if (!record(values, sizeof(values)))
                        return false;
                }
            return true;
        };
        if (ok)
            ok = movie->FAddToCmvi(&composite, &insertion) && state(0);
        if (ok && mode != 4 && mode != 5)
            ok = other->FAddToCmvi(&composite, &insertion) && state(1);
        if (ok)
        {
            if (mode == 1 || mode == 2 || mode == 3 || mode == 4)
            {
                int index = mode == 2 ? 0 : 1;
                SceneDescriptor row;
                composite.pglscend->Get(index, &row);
                row.fNuked = true;
                composite.pglscend->Put(index, &row);
                composite.pglscend->Move(index, composite.pglscend->IvMac());
                if (mode == 3 || mode == 4)
                {
                    composite.pglscend->Get(0, &row);
                    row.fNuked = true;
                    composite.pglscend->Put(0, &row);
                    composite.pglscend->Move(0, composite.pglscend->IvMac());
                }
            }
            if (mode == 6)
                composite.pglscend->Move(2, 1);
            if (mode == 7)
                composite.pglscend->Move(1, 0);
            for (long i = 0; i < composite.pglscend->IvMac(); ++i)
            {
                SceneDescriptor row;
                composite.pglscend->Get(i, &row);
                row.trans = TRANS((row.trans + 1) % transLim);
                composite.pglscend->Put(i, &row);
            }
            ok = state(2) && movie->FSetCmvi(&composite) && state(3);
        }
        if (composite.pglscend && composite.pglmvied)
            composite.Empty();
        if (ok)
            ok = state(4);
        if (movie)
            movie->ClearUndo();
        if (other)
            other->ClearUndo();
        ReleasePpo(&movie);
        ReleasePpo(&other);
        ReleasePpo(&client);
        ReleasePpo(&otherClient);
        if (!ok)
            std::fprintf(stderr, "Movie composition failed: mode=%d\n", mode);
    }
    for (int mode = 0; ok && mode < 4; ++mode)
    {
        auto client = NewObj MovieComposeClient();
        auto movie = client ? Movie::PmvieNew(false, client) : nullptr;
        PChunkyResourceFile resources = nullptr;
        MovieData table;
        ok = movie && table.FInitActors() && movie->FEnsureAutosave(&resources);
        ChunkNumber root = 0, scenes[2] = {}, actors[2] = {}, gst;
        if (ok)
            ok = MovieData::FCreateRoot(resources->Pcfl(), &root);
        for (int i = 0; ok && i < 2; ++i)
        {
            MMRollCallActor row = {{i * 3, 1, 123, 1, kctgTmpl, ChunkNumber(100 + i)}, nullptr};
            String name;
            name.SetSzs(i ? PszLit("Second") : PszLit("First"));
            ActorChunkOnFile header = {kboCur, koskCur, {}, i * 3, 1, 1, {123, 0, kctgTmpl, ChunkNumber(100 + i)}};
            if (mode & 2)
                SwapBytesBom(&header, kbomActf);
            auto file = resources->Pcfl();
            byte empty = 0;
            ok = mm_rollcall_table_add(&table.actors, name.Prgch(), name.Cch(), &row) &&
                 file->FAddPv(&header, sizeof(header), kctgActr, &actors[i]) &&
                 file->FAddChildPv(kctgMvie, root, i, &empty, 1, kctgScen, &scenes[i]);
        }
        if (ok)
        {
            auto file = resources->Pcfl();
            ok = MovieData::FWriteRollCall(file, root, &table.actors, &gst) &&
                 file->FAdoptChild(kctgScen, scenes[0], kctgActr, actors[0], 0) &&
                 file->FAdoptChild(kctgScen, scenes[1], kctgActr, actors[0], 0) &&
                 file->FAdoptChild(kctgActr, actors[0], kctgActr, actors[1], 0) &&
                 movie->_FAddMvieToRollCall(root, (mode & 1) ? 20 : 0);
            for (int i = 0; ok && i < 2; ++i)
            {
                DataBlock block;
                ActorChunkOnFile header = {};
                long id = 0, refs = 0;
                String name;
                TAG tag = {};
                ok = file->FFind(kctgActr, actors[i], &block) && ActorData::FReadHeader(&block, &header) &&
                     movie->FGetArid(i, &id, &name, &refs, &tag);
                LegacyLong values[] = {mode,
                                       i,
                                       header.arid,
                                       LegacyLong(id),
                                       LegacyLong(refs),
                                       tag.sid,
                                       LegacyLong(tag.cno),
                                       LegacyLong(name.Cch()),
                                       LegacyLong(resources->CactRef())};
                ok = ok && record(values, sizeof(values)) && record(name.Psz(), name.Cch() * sizeof(achar));
            }
        }
        ReleasePpo(&movie);
        ReleasePpo(&client);
        if (!ok)
            std::fprintf(stderr, "Movie roll-call remap failed: mode=%d\n", mode);
    }
#ifdef MM_ENGINE_MOVIE_COMPOSE_H
    ok = TestComposeLiveIndex() && ok;
#endif
#ifdef _WIN64
    ok = std::fflush(out) == 0 && ok;
    ok = std::fseek(out, 0, SEEK_SET) == 0 && ok;
    MMU32 hash = 2166136261U;
    size_t bytes = 0, count;
    unsigned char buffer[4096];
    while ((count = std::fread(buffer, 1, sizeof(buffer), out)) != 0)
    {
        bytes += count;
        for (size_t i = 0; i < count; ++i)
            hash = (hash ^ buffer[i]) * 16777619U;
    }
    ok = ok && !std::ferror(out) && bytes == 19044 && hash == 0x8eecd466U;
#endif
    ok = std::fclose(out) == 0 && ok;
    return ok;
}
#endif
