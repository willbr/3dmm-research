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
        auto bitmap = NewMaskedBitmap(pixels, 8, 6, &bounds, 0, 0, 0);
        if (!bitmap)
            return false;
        auto member = &MovieComposeSceneAccess::_owner;
        (scene->*member).owned[MM_SCENE_THUMBNAIL] = bitmap;
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
        ok = mm_composite_remove_scene(&composite, 0) && mm_composite_move_scene(&composite, 1, 0);
        auto current = movie->Pscen();
        ok = ok && movie->FSetCmvi(&composite) && movie->Cscen() == 1 && movie->Iscen() == 0 &&
             movie->Pscen() == current;
    }
    composite.Empty();
    if (movie)
        movie->ClearUndo();
    ReleasePpo(&movie);
    ReleasePpo(&client);
    return ok;
}

static bool TestPrivateMovieCompose()
{
    auto client = NewObj MovieComposeClient();
    auto sourceClient = NewObj MovieComposeClient();
    auto movie = client ? Movie::PmvieNew(false, client) : nullptr;
    auto source = sourceClient ? Movie::PmvieNew(false, sourceClient) : nullptr;
    CompositeMovie composite = {};
    bool ok = movie && source;
    if (ok)
    {
        auto resources = NewPrivateMovieResources();
        MovieSaveProbe::Data(movie)->resources = resources;
        movie->SetDirty();
        ok = resources && !resources->services.objects.state && movie->FAutoSave() && movie->FNewScenInsCore(0) &&
             MovieComposeSceneAccess::Thumbnail(movie->Pscen(), 21);
    }
    for (int slot = 0; ok && slot < 2; ++slot)
    {
        ok = source->FNewScenInsCore(slot) && MovieComposeSceneAccess::Thumbnail(source->Pscen(), 22 + slot);
        if (ok)
            source->Pscen()->SetTransitionCore(TRANS(slot + 1));
    }
    long insertion = 0;
    if (ok)
        ok = movie->FAddToCmvi(&composite, &insertion) && source->FAddToCmvi(&composite, &insertion) &&
             mm_composite_move_scene(&composite, 2, 1) && movie->FSetCmvi(&composite) &&
             movie->Cscen() == 3 && movie->FSwitchScen(1) && movie->Pscen()->Trans() == TRANS(2) &&
             movie->FSwitchScen(2) && movie->Pscen()->Trans() == TRANS(1) &&
             movie->FRemScenCore(0) && movie->Cscen() == 2;
    composite.Empty();
    ReleasePpo(&movie);
    ReleasePpo(&source);
    ReleasePpo(&client);
    ReleasePpo(&sourceClient);
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
                                   data->Resources() ? data->Resources()->references->count : 0,
                                   composite.movie_count ? LegacyLong(composite.scene_count) : -1,
                                   composite.movie_count ? LegacyLong(composite.movie_count) : -1};
            if (!record(fields, sizeof(fields)) || !record(trace.data(), (trace.size)() * sizeof(trace[0])))
                return false;
            for (MMU32 i = 0; i < composite.movie_count; ++i)
            {
                MMComposeMovie row;
                if (!mm_composite_movie(&composite, i, &row))
                    return false;
                auto resources = static_cast<MMResourceCache *>(row.resources);
                LegacyLong values[] = {LegacyLong(i), resources == data->Resources() ? 0 : resources == external ? 1 : 2,
                                       LegacyLong(row.number), row.actor_limit, resources->references->count};
                if (!record(values, sizeof(values)))
                    return false;
            }
            for (MMU32 i = 0; i < composite.scene_count; ++i)
            {
                MMComposeScene row;
                if (!mm_composite_scene(&composite, i, &row))
                    return false;
                auto bitmap = static_cast<PMaskedBitmapMBMP>(row.thumbnail);
                LegacyLong values[] = {LegacyLong(i), row.movie, LegacyLong(row.number), LegacyLong(row.child),
                                       row.transition, row.deleted, mm_masked_owner_services()->references(bitmap)->count, MaskedBitmapSize(bitmap)};
                if (!record(values, sizeof(values)))
                    return false;
            }
            if (data->Resources())
            {
                const MovieArchiveFixture archive(data->Resources());
                for (long i = 0; i < movie->Cscen(); ++i)
                {
                    ChunkNumber scene;
                    TRANS transition;
                    if (!archive.Link(kctgMvie, data->movieNumber, i, kctgScen, &scene) ||
                        !Scene::FTransOnFile(data->Resources(), scene, &transition))
                        return false;
                    LegacyLong values[] = {LegacyLong(i), LegacyLong(scene), LegacyLong(transition)};
                    if (!record(values, sizeof(values)))
                        return false;
                }
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
                ok = mm_composite_remove_scene(&composite, mode == 2 ? 0 : 1) != 0;
                if (mode == 3 || mode == 4)
                    ok = ok && mm_composite_remove_scene(&composite, 0);
            }
            if (mode == 6)
                ok = mm_composite_move_scene(&composite, 2, 1) != 0;
            if (mode == 7)
                ok = mm_composite_move_scene(&composite, 1, 0) != 0;
            for (MMU32 i = 0; ok && i < composite.scene_count; ++i)
            {
                MMComposeScene row;
                ok = mm_composite_scene(&composite, i, &row) &&
                     mm_composite_set_transition(&composite, i, (row.transition + 1) % transLim);
            }
            ok = ok && state(2) && movie->FSetCmvi(&composite) && state(3);
        }
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
        MMResourceCache *resources = nullptr;
        MovieData table;
        ok = movie && table.FInitActors() && movie->FEnsureAutosave(&resources);
        const MovieArchiveFixture archive(resources);
        ChunkNumber root = 0, scenes[2] = {}, actors[2] = {}, gst;
        if (ok)
            ok = mm_movie_create_root(&archive.writer, &root) != 0;
        for (int i = 0; ok && i < 2; ++i)
        {
            MMRollCallActor row = {{i * 3, 1, 123, 1, kctgTmpl, ChunkNumber(100 + i)}, nullptr};
            String name;
            name.SetSzs(i ? PszLit("Second") : PszLit("First"));
            ActorChunkOnFile header = {kboCur, koskCur, {}, i * 3, 1, 1, {123, 0, kctgTmpl, ChunkNumber(100 + i)}};
            if (mode & 2)
                SwapBytesBom(&header, kbomActf);
            byte empty = 0;
            ok = mm_rollcall_table_add(&table.actors, name.Prgch(), name.Cch(), &row) &&
                 archive.AddBytes(&header, sizeof(header), kctgActr, &actors[i]) &&
                 archive.AddChildBytes(kctgMvie, root, i, &empty, 1, kctgScen, &scenes[i]);
        }
        if (ok)
        {
            ok = mm_movie_write_rollcall(&archive.writer, root, &table.actors, &gst) &&
                 archive.Adopt(kctgScen, scenes[0], kctgActr, actors[0], 0) &&
                 archive.Adopt(kctgScen, scenes[1], kctgActr, actors[0], 0) &&
                 archive.Adopt(kctgActr, actors[0], kctgActr, actors[1], 0) &&
                 movie->_FAddMvieToRollCall(root, (mode & 1) ? 20 : 0);
            for (int i = 0; ok && i < 2; ++i)
            {
                DataBlock block;
                ActorChunkOnFile header = {};
                long id = 0, refs = 0;
                String name;
                TAG tag = {};
                ok = archive.Find(kctgActr, actors[i], &block) && ActorData::FReadHeader(&block, &header) &&
                     movie->FGetArid(i, &id, &name, &refs, &tag);
                LegacyLong values[] = {mode,
                                       i,
                                       header.arid,
                                       LegacyLong(id),
                                       LegacyLong(refs),
                                       tag.sid,
                                       LegacyLong(tag.cno),
                                       LegacyLong(name.Cch()),
                                       LegacyLong(resources->references->count)};
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
