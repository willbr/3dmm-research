/* Headless Movie save publication and working-file compatibility probe. */
#ifndef MOVIE_SAVE_FIXTURE_H
#define MOVIE_SAVE_FIXTURE_H
class MovieSaveProbe
{
  public:
    static void Container(PMovie movie, const std::shared_ptr<MovieContainer::Document> &document)
    {
        movie->_container = document;
    }
    static MovieData *Data(PMovie movie)
    {
        return &movie->_data;
    }
    static void Record(PMovie movie, std::vector<LegacyLong> &trace, int operation, bool result)
    {
        Filename path;
        bool named = movie->FGetFni(&path);
        auto data = Data(movie);
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
                            data->Resources() ? data->Resources()->Pcfl()->FTemp() : -1};
        trace.insert(trace.end(), row, row + sizeof(row) / sizeof(row[0]));
        if (!data->Resources())
            return;
        auto file = data->Resources()->Pcfl();
        for (long i = 0;; ++i)
        {
            ChunkIdentification chunk;
            if (!file->FGetCki(i, &chunk))
                break;
            LegacyLong item[] = {LegacyLong(chunk.ctg), LegacyLong(chunk.cno),
                                 LegacyLong(file->Ckid(chunk.ctg, chunk.cno))};
            trace.insert(trace.end(), item, item + 3);
            for (long j = 0; j < item[2]; ++j)
            {
                ChildChunkIdentification child;
                if (!file->FGetKid(chunk.ctg, chunk.cno, j, &child))
                {
                    trace.push_back(-1);
                    break;
                }
                LegacyLong link[] = {LegacyLong(child.cki.ctg), LegacyLong(child.cki.cno), LegacyLong(child.chid)};
                trace.insert(trace.end(), link, link + 3);
            }
        }
        trace.push_back(-2);
    }
};
class MovieSaveContainer : public MovieContainer::Document
{
  public:
    bool bundled = false, fail = false;
    std::vector<LegacyLong> *trace;
    explicit MovieSaveContainer(std::vector<LegacyLong> *value) : trace(value)
    {
    }
    bool Bundled() const override
    {
        return bundled;
    }
    bool Save(PFilename working, PFilename destination) override
    {
        LegacyLong row[] = {800, working != nullptr, MovieContainer::IsVmm(destination), fail};
        trace->insert(trace->end(), row, row + 4);
        return working && !fail;
    }
    bool Commit() override
    {
        return false;
    }
};
static bool TestMovieSave(PMovie parent)
{
    std::vector<LegacyLong> trace;
    bool ok = true;
    for (int mode = 0; ok && mode < 16; ++mode)
    {
        auto movie = Movie::PmvieNew(false, parent->Pmcc());
        Filename path;
        ok = movie && path.FGetTemp() && path.FChangeFtg((mode & 4) ? 'VMM' : kftg3mm);
        std::shared_ptr<MovieSaveContainer> container;
        if (ok && (mode & 2))
        {
            container = std::make_shared<MovieSaveContainer>(&trace);
            container->bundled = (mode & 8) != 0;
            container->fail = (mode & 1) != 0;
            MovieSaveProbe::Container(movie, container);
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
            PChunkyResourceFile resource = nullptr;
            record(6, movie->FEnsureAutosave(&resource));
            ok = resource && movie->FSwitchScen(ivNil) && movie->FSwitchScen(0) &&
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
    ThreeMMU32 hash = 2166136261U;
    for (size_t i = 0; i < byteCount; ++i)
        hash = (hash ^ bytes[i]) * 16777619U;
    ok = ok && byteCount == 29440 && hash == 0x696eaa7cU;
#endif
    if (const char *path = std::getenv("THREE_MM_MOVIE_SAVE_SNAPSHOT"))
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
#endif
