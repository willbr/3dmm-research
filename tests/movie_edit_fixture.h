/* Headless Movie actor/roll-call/undo dispatch compatibility probe. */
#ifndef MOVIE_EDIT_FIXTURE_H
#define MOVIE_EDIT_FIXTURE_H
class MovieEditClient : public MovieClientCallbacks
{
  public:
    long rollcalls = 0, undoChanges = 0, undoState = 0, nuked = 0;
    MovieEditClient() : MovieClientCallbacks(640, 480, 0)
    {
    }
    void UpdateRollCall() override
    {
        ++rollcalls;
    }
    void SetUndo(long value) override
    {
        ++undoChanges;
        undoState = value;
    }
    void ActorNuked() override
    {
        ++nuked;
    }
};
class MovieEditUndo : public MovieUndo
{
  public:
    bool FDo(PDocumentBase) override
    {
        return true;
    }
    bool FUndo(PDocumentBase) override
    {
        return true;
    }
    long SavedScene() const
    {
        return _iscen;
    }
};
static bool TestMovieEdit()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_MOVIE_EDIT_SNAPSHOT"))
        if (!(snapshot = std::fopen(path, "w+b")))
            return false;
    if (!snapshot)
        snapshot = std::tmpfile();
    if (!snapshot)
        return false;
    auto record = [&](const void *data, size_t bytes) {
        return !snapshot || std::fwrite(data, 1, bytes, snapshot) == bytes;
    };
    bool ok = true;
    for (int mode = 0; ok && mode < 16; ++mode)
    {
        auto client = NewObj MovieEditClient();
        auto movie = client ? Movie::PmvieNew(false, client) : nullptr;
        auto actor = NewObj TrackTestActor();
        auto undo = NewObj MovieEditUndo();
        PChunkyResourceFile resource = nullptr;
        ok = movie && actor && undo;
        if (ok)
        {
            undo->SetNfrm(77);
            ok = movie->FAddUndo(undo);
            LegacyLong row[] = {mode,
                                -1,
                                ok,
                                LegacyLong(undo->SavedScene()),
                                LegacyLong(undo->Nfrm()),
                                LegacyLong(client->undoState),
                                LegacyLong(movie->CundbUndo())};
            ok = ok && record(row, sizeof(row));
            movie->ClearUndo();
            ok = ok && movie->FNewScenInsCore(0) && movie->FEnsureAutosave(&resource);
        }
        if (ok)
        {
            byte dummy = 0;
            ChunkNumber number;
            ok = resource->Pcfl()->FAddPv(&dummy, 1, kctgTmpl, &number) &&
                 resource->Pcfl()->FAddPv(&dummy, 1, kctgTmpl, &number) && number == 1 &&
                 actor->FInitPlayback(movie->Pscen(), resource);
        }
        String original, renamed;
        original.SetSzs(PszLit("Original"));
        renamed.SetSzs(PszLit("Renamed"));
        if (ok)
        {
            actor->SetArid((mode & 1) ? 77 : aridNil);
            ok = movie->FAddToRollCall(actor, &original);
        }
        auto state = [&](int operation, bool result) {
            long id = 333, refs = 444;
            TAG tag = {};
            String name;
            bool found = movie->FGetArid(0, &id, &name, &refs, &tag);
            LegacyLong row[] = {mode,
                                operation,
                                result,
                                found,
                                LegacyLong(id),
                                LegacyLong(refs),
                                LegacyLong(movie->CmactrMac()),
                                MovieSaveProbe::Data(movie)->actorIdLimit,
                                LegacyLong(client->rollcalls),
                                LegacyLong(client->undoChanges),
                                LegacyLong(client->undoState),
                                LegacyLong(client->nuked),
                                LegacyLong(movie->AridSelected()),
                                MovieSaveProbe::AutosaveDirty(movie),
                                LegacyLong(resource->CactRef()),
                                LegacyLong(name.Cch()),
                                tag.sid,
                                LegacyLong(tag.ctg),
                                LegacyLong(tag.cno)};
            return record(row, sizeof(row)) && record(name.Psz(), name.Cch() * sizeof(achar));
        };
        if (ok)
            ok = state(0, true) && movie->FAddToRollCall(actor, nullptr) && state(1, true);
        if (ok)
        {
            movie->RemFromRollCall(actor);
            ok = state(2, true);
        }
        if (ok)
            ok = movie->FNameActr(actor->Arid(), &renamed) && state(3, true);
        if (ok)
        {
            TAG tag;
            actor->GetTagTmpl(&tag);
            ++tag.cno;
            movie->ChangeActrTag(actor->Arid(), &tag);
            ok = state(4, true) && movie->FAddToRollCall(actor, nullptr) && state(5, true);
            movie->RemFromRollCall(actor, false);
            movie->RemFromRollCall(actor, true);
            ok = ok && state(6, true);
        }
        if (ok)
        {
            actor->SetArid(aridNil);
            ok = movie->Pscen()->FAddActrCore(actor);
            movie->Pscen()->SelectActr(actor);
            ok = ok && state(7, movie->FChooseArid(actor->Arid())); // No view: placement cannot begin.
        }
        if (ok)
        {
            bool result = movie->FRotateActr(BRA(0x100 * mode), BRA(0x200 + mode), BRA(0x300), (mode & 2) != 0);
            ok = result && state(8, result) && TrackTestActor::RecordClipboardActor(actor, snapshot);
        }
        if (ok)
        {
            bool result = movie->FSquashStretchActr((mode & 4) ? rZero : BR_SCALAR(.75));
            ok = result && state(9, result) && TrackTestActor::RecordClipboardActor(actor, snapshot);
        }
        if (ok)
        {
            bool result = movie->FScaleActr(BR_SCALAR(1.25));
            ok = result && state(10, result) && TrackTestActor::RecordClipboardActor(actor, snapshot);
        }
        if (ok)
        {
            bool result = movie->FSoonerLaterActr(movie->Pscen()->Nfrm() + ((mode & 8) ? 1 : 0));
            ok = result && state(11, result) && TrackTestActor::RecordClipboardActor(actor, snapshot);
        }
        if (ok)
            ok = movie->FSetTransition(transFadeToBlack) && state(12, true) && movie->FPause(witForTime, 5) &&
                 state(13, true);
        if (ok)
        {
            bool result = movie->FAddOnstage(499);
            ok = !result && state(14, result);
            movie->Pscen()->SelectActr(actor);
            result = movie->FRemActr();
            ok = ok && result && state(15, result) && client->nuked == 1;
        }
        if (movie)
            movie->ClearUndo();
        ReleasePpo(&undo);
        ReleasePpo(&actor);
        ReleasePpo(&movie);
        ReleasePpo(&client);
    }
#ifdef _WIN64
    ok = std::fflush(snapshot) == 0 && ok;
    ok = std::fseek(snapshot, 0, SEEK_SET) == 0 && ok;
    ThreeMMU32 hash = 2166136261U;
    size_t bytes = 0, count;
    unsigned char buffer[4096];
    while ((count = std::fread(buffer, 1, sizeof(buffer), snapshot)) != 0)
    {
        bytes += count;
        for (size_t i = 0; i < count; ++i)
            hash = (hash ^ buffer[i]) * 16777619U;
    }
    ok = ok && !std::ferror(snapshot) && bytes == 61552 && hash == 0x85c0c7b5U;
#endif
    ok = std::fclose(snapshot) == 0 && ok;
    return ok;
}
#endif
