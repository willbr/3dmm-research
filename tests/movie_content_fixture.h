/* Headless Movie material, 3-D text and caption transaction probe. */
#ifndef MOVIE_CONTENT_FIXTURE_H
#define MOVIE_CONTENT_FIXTURE_H
class MovieContentTagAccess : public TagManager
{
  public:
    static bool FontSource(PChunkyResourceFile resources)
    {
        auto member = &MovieContentTagAccess::_sources;
        auto &sources = vptagm->*member;
        auto manager = ChunkyResourceManager::PcrmNew(1);
        auto paths = sources.owners.new_paths(sources.owners.context);
        auto sibling = resources->PcrfNewSibling(1024 * 1024);
        const bool added = manager && sibling &&
                           mm_resource_manager_add_cache(manager->CoreManager(), sibling->CoreCache(), nullptr) == MM_STORAGE_OK;
        ReleasePpo(&sibling);
        if (!added || !paths ||
            mm_storage_ensure(&sources.storage, (sources.count + 1) * sizeof(MMSourceRecord), 0, 0))
        {
            ReleasePpo(&manager);
            if (paths)
                sources.owners.release_paths(paths);
            return false;
        }
        MMSourceRecord row = {123, 1, manager, paths};
        std::memcpy(sources.storage.buffers[0].data + sources.count * sizeof(row), &row, sizeof(row));
        ++sources.count;
        return true;
    }
};
class MovieContentSceneAccess : public Scene
{
  public:
    static bool PlacePoints(PScene scene)
    {
        if (scene->Pbkgd())
            return false;
        auto background = mm_background_owner_create(&EngineBackground::Host());
        if (!background)
            return false;
        if (!InitializeStageBackground(background))
        {
            mm_background_owner_release(background);
            return false;
        }
        auto member = &MovieContentSceneAccess::_owner;
        (scene->*member).owned[MM_SCENE_BACKGROUND] = background;
        return true;
    }
};
static bool TestMovieContentCore()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("MM_MOVIE_CONTENT_SNAPSHOT"))
        snapshot = std::fopen(path, "w+b");
    else
        snapshot = std::tmpfile();
    if (!snapshot)
        return false;
    auto record = [&](const void *p, size_t bytes) { return std::fwrite(p, 1, bytes, snapshot) == bytes; };
    bool ok = true;
    for (int mode = 0; ok && mode < 8; ++mode)
    {
        auto client = NewObj MovieEditClient();
        auto movie = client ? Movie::PmvieNew(false, client) : nullptr;
        auto material = EngineMaterial::New(mode + 1, 8);
        PChunkyResourceFile resource = nullptr;
        ok = movie && material && movie->FNewScenInsCore(0) && movie->FEnsureAutosave(&resource);
        auto state = [&](int operation) {
            std::fprintf(stderr, "[movie content] mode=%d operation=%d\n", mode, operation);
            std::vector<LegacyLong> trace;
            MovieSaveProbe::Record(movie, trace, operation, true);
            LegacyLong fields[] = {mode,
                                   operation,
                                   LegacyLong(resource->CactRef()),
                                   LegacyLong(movie->AridSelected()),
                                   LegacyLong(movie->CmactrMac()),
                                   LegacyLong(movie->Pscen()->Ctbox()),
                                   LegacyLong(client->rollcalls),
                                   LegacyLong(client->undoChanges),
                                   LegacyLong(client->undoState),
                                   LegacyLong(client->nuked)};
            if (!record(fields, sizeof(fields)) || !record(trace.data(), (trace.size)() * sizeof(trace[0])))
                return false;
            for (long i = 0; i < movie->CmactrMac(); ++i)
            {
                long id, refs;
                TAG tag;
                String name;
                if (!movie->FGetArid(i, &id, &name, &refs, &tag))
                    return false;
                LegacyLong row[] = {LegacyLong(id),        LegacyLong(refs),    tag.sid,
                                    LegacyLong(tag.ctg),   LegacyLong(tag.cno), tag.pcrf == resource,
                                    LegacyLong(name.Cch())};
                if (!record(row, sizeof(row)) || !record(name.Psz(), name.Cch() * sizeof(achar)))
                    return false;
            }
            for (long i = 0; i < movie->Pscen()->Ctbox(); ++i)
            {
                auto box = movie->Pscen()->PtboxFromItbox(i);
                long first = 0, last = 0;
                bool lifetime = box->FGetLifetime(&first, &last);
                LegacyLong row[] = {LegacyLong(i),     box == movie->Pscen()->PtboxSelected(),
                                    box->FIsVisible(), box->FStory(),
                                    lifetime,          LegacyLong(first),
                                    LegacyLong(last)};
                if (!record(row, sizeof(row)))
                    return false;
            }
            if (auto actor = movie->Pscen()->PactrSelected())
            {
                String text;
                LegacyLong shape;
                TAG font;
                EngineText::GetInfo(static_cast<PThreeDText>(actor->Ptmpl()), &text, &shape, &font);
                LegacyLong row[] = {shape,
                                    font.sid,
                                    LegacyLong(font.ctg),
                                    LegacyLong(font.cno),
                                    font.pcrf == resource,
                                    LegacyLong(text.Cch())};
                return record(row, sizeof(row)) && record(text.Psz(), text.Cch() * sizeof(achar));
            }
            return true;
        };
        TAG tag = {};
        if (ok)
        {
            ok = movie->FInsertMtrl(material, &tag) && tag.sid == ksidUseCrf && tag.pcrf == resource && state(0);
            if (tag.pcrf)
                TagManager::CloseTag(&tag);
        }
        RC bounds = {10, 20, 210, 120};
        if (ok)
        {
            /* Build the insertion prefix without interactive mouse placement. */
            auto box = TextBox::PtboxNew(movie->Pscen(), &bounds, (mode & 1) != 0);
            if (box)
                box->SetDypFontDef(client->DypTboxDef());
            ok = box && movie->Pscen()->FAddTbox(box);
            if (ok)
                movie->Pscen()->SelectTbox(box);
            ReleasePpo(&box);
            ok = ok && state(1);
        }
        if (ok)
            ok = ((mode & 2) ? movie->FNukeTbox() : movie->FHideTbox()) && state(2);
        if (ok)
        {
            movie->Pscen()->SelectTbox(nullptr);
            ok = !movie->FNukeTbox() && !movie->FHideTbox() && state(4);
            vpers->Clear();
        }
        /* A complete synthetic font lets the ordinary Movie/Actor factories run. */
        std::vector<byte> wire(12 + 256 * 8);
        int16_t order = kboCur, system = koskCur;
        int32_t count = 256;
        BRS maximum = BR_SCALAR(2);
        std::memcpy(wire.data(), &order, 2);
        std::memcpy(wire.data() + 2, &system, 2);
        std::memcpy(wire.data() + 4, &count, 4);
        std::memcpy(wire.data() + 8, &maximum, 4);
        for (int i = 0; i < 512; ++i)
            std::memcpy(wire.data() + 12 + i * 4, &maximum, 4);
        BRV vertices[3] = {};
        BRF face = {};
        vertices[1].p.v[0] = BR_SCALAR(1);
        vertices[2].p.v[1] = BR_SCALAR(2);
        face.vertices[1] = 1;
        face.vertices[2] = 2;
        auto model = EngineModel::New(3, vertices, 1, &face);
        ChunkNumber fontNumber = 0, modelNumber = 0;
        if (ok)
        {
            const MovieArchiveFixture archive(resource);
            ok = model && archive.AddBytes(wire.data(), (wire.size)(), kctgTdf, &fontNumber) &&
                 archive.Add(0, kctgBmdl, &modelNumber) && EngineModel::Write(model, archive.writer, kctgBmdl, modelNumber);
            for (int i = 0; ok && i < 256; ++i)
                ok = archive.Adopt(kctgTdf, fontNumber, kctgBmdl, modelNumber, i);
        }
        mm_model_owner_release(model);
        if (ok)
            ok = MovieContentTagAccess::FontSource(resource) &&
                 MovieContentSceneAccess::PlacePoints(movie->Pscen());
        TAG font = {123, nullptr, kctgTdf, fontNumber};
        String text;
        text.SetSzs(PszLit("AB"));
        if (ok)
            ok = movie->FInsTdt(&text, mode, &font) && state(5);
        if (ok)
        {
            text.SetSzs((mode & 2) ? PszLit("A B") : PszLit("BA"));
            ok = movie->FChangeActrTdt(movie->Pscen()->PactrSelected(), &text, 7 - mode, &font) && state(6);
        }
        if (ok)
        {
            text.SetSzs((mode & 4) ? PszLit("") : PszLit("   "));
            ok = movie->FChangeActrTdt(movie->Pscen()->PactrSelected(), &text, 0, &font) && state(7);
        }
        if (movie)
            movie->ClearUndo();
        mm_material_owner_release(material);
        ReleasePpo(&movie);
        ReleasePpo(&client);
        vptagm->InvalidateSource(123);
        if (!ok)
            std::fprintf(stderr, "Movie content fixture failed: mode=%d\n", mode);
    }
#ifdef _WIN64
    ok = std::fflush(snapshot) == 0 && ok;
    ok = std::fseek(snapshot, 0, SEEK_SET) == 0 && ok;
    MMU32 hash = 2166136261U;
    size_t bytes = 0, count;
    unsigned char buffer[4096];
    while ((count = std::fread(buffer, 1, sizeof(buffer), snapshot)) != 0)
    {
        bytes += count;
        for (size_t i = 0; i < count; ++i) hash = (hash ^ buffer[i]) * 16777619U;
    }
    ok = ok && !std::ferror(snapshot) && bytes == 91484 && hash == 0xb21e0181U;
#endif
    ok = std::fclose(snapshot) == 0 && ok;
    return ok;
}
static bool TestMovieContent()
{
#ifdef WIN
    vwig.hdcApp = GetDC(nullptr);
    bool ok = vwig.hdcApp && vntl.FInit() && TestMovieContentCore();
    if (vwig.hdcApp)
        ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
    return ok;
#else
    return vntl.FInit() && TestMovieContentCore();
#endif
}
#endif
