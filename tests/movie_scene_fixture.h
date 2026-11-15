/* Headless Movie scene-container compatibility probe. */
#ifndef MOVIE_SCENE_FIXTURE_H
#define MOVIE_SCENE_FIXTURE_H

class MovieSceneClient : public MovieClientCallbacks
{
  public:
    PMovie movie = nullptr;
    std::vector<LegacyLong> trace;
    MovieSceneClient() : MovieClientCallbacks(640, 480, 0)
    {
    }
    void Record(long operation, bool result)
    {
        LegacyLong row[] = {LegacyLong(operation),
                            result,
                            LegacyLong(movie->Cscen()),
                            LegacyLong(movie->Iscen()),
                            movie->Pscen() != nullptr,
                            movie->Pscen() ? LegacyLong(movie->Pscen()->Trans()) : -1,
                            movie->Pscen() ? LegacyLong(movie->Pscen()->Nfrm()) : -1};
        trace.insert(trace.end(), row, row + sizeof(row) / sizeof(row[0]));
    }
    void SceneChange() override
    {
        if (movie)
            Record(900, true);
    }
    void SceneNuked() override
    {
        Record(901, true);
    }
    void SceneUnnuked() override
    {
        Record(902, true);
    }
};

static bool TestMovieScenes()
{
    MovieSceneClient *client = NewObj MovieSceneClient();
    PMovie movie = client ? Movie::PmvieNew(false, client) : nullptr;
    if (!movie)
    {
        ReleasePpo(&client);
        return false;
    }
    client->movie = movie;
    bool ok = true;
    auto check = [&](int operation, bool result) {
        client->Record(operation, result);
        ok = ok && result;
        return result;
    };
    // Distinct transitions identify content when child IDs move in either direction.
    for (int i = 0; ok && i < 3; ++i)
    {
        ok = check(10 + i, movie->FNewScenInsCore(i));
        if (ok)
            movie->Pscen()->SetTransitionCore(TRANS(i + 1));
    }
    for (int i : {2, 0, 1, 1, 2})
    {
        if (!ok)
            break;
        ok = check(20 + i, movie->FSwitchScen(i)) && movie->Pscen()->Trans() == TRANS(i + 1);
    }
    if (ok)
        ok = check(30, movie->FSwitchScen(ivNil)) && movie->Pscen() == nullptr && movie->Iscen() == ivNil;
    if (ok)
        ok = check(31, movie->FSwitchScen(1));
    // Removal snapshots an unopened scene, then undo restores that exact content.
    if (ok)
        ok = check(40, movie->FRemScen(0)) && movie->Cscen() == 2 && movie->Pscen()->Trans() == TRANS(2);
    if (ok)
        ok = check(41, movie->FUndo()) && movie->Cscen() == 3 && movie->Iscen() == 0 &&
             movie->Pscen()->Trans() == TRANS(1);
    if (ok)
        ok = check(42, movie->FRedo()) && movie->Cscen() == 2 && movie->Pscen()->Trans() == TRANS(2);
    movie->ClearUndo();
    // Remove the last current scene, then the final scene; the latter retains native dirty behavior.
    if (ok)
        ok = check(50, movie->FSwitchScen(1)) && movie->Pscen()->Trans() == TRANS(3);
    if (ok)
        ok = check(51, movie->FRemScen(1)) && movie->Cscen() == 1 && movie->Iscen() == 0;
    if (ok)
        ok = check(52, movie->FUndo()) && movie->Cscen() == 2 && movie->Iscen() == 1 &&
             movie->Pscen()->Trans() == TRANS(3);
    movie->ClearUndo();
    if (ok)
        ok = check(53, movie->FRemScenCore(1));
    if (ok)
        ok = check(54, movie->FRemScenCore(0)) && movie->Cscen() == 0 && movie->Pscen() == nullptr;
    if (ok)
        ok = check(55, movie->FNewScenInsCore(0));
    // Frozen from the original native and SDL debug/release controllers.
#ifdef _WIN64
    const auto *bytes = reinterpret_cast<const unsigned char *>(client->trace.data());
    const auto byteCount = (client->trace.size)() * sizeof(LegacyLong);
    ThreeMMU32 hash = 2166136261U;
    for (size_t i = 0; i < byteCount; ++i)
        hash = (hash ^ bytes[i]) * 16777619U;
    ok = ok && byteCount == 1064 && hash == 0xe62aac8fU;
#endif
    if (const char *path = std::getenv("THREE_MM_MOVIE_SCENE_SNAPSHOT"))
    {
        FILE *file = std::fopen(path, "wb");
        if (!file)
            ok = false;
        else
        {
            ok = std::fwrite(client->trace.data(), sizeof(LegacyLong), (client->trace.size)(), file) ==
                     (client->trace.size)() &&
                 ok;
            ok = std::fclose(file) == 0 && ok;
        }
    }
    client->movie = nullptr;
    ReleasePpo(&movie);
    ReleasePpo(&client);
    return ok;
}
#endif
