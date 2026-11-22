#ifndef ENGINE_EDIT_TRANSACTION_FIXTURE_H
#define ENGINE_EDIT_TRANSACTION_FIXTURE_H
static bool TestActorSoundTransaction(PMovie parent)
{
    auto movie = Movie::PmvieNew(fFalse, parent->Pmcc(), nullptr, cnoNil);
    auto scene = movie ? NewObj StageTestScene(movie) : nullptr;
    auto actor = NewObj TrackTestActor();
    MMResourceCache *resource = nullptr;
    bool ok = scene && actor && movie->FNewScenInsCore(0) && movie->FSwitchScen(0) && movie->FAutoSave() &&
              movie->FEnsureAutosave(&resource) && scene->Init() && actor->FInitPlayback(scene, resource);
    if (ok)
    {
        scene->Disable(fscenSounds);
        const MovieArchiveFixture archive(resource);
        MovieSoundFile header = {kboCur, koskCur, stySfx, 32768, false};
        ChunkNumber number = 0;
        String name;
        name.SetSzs(PszLit("Silent actor sound"));
        ok = archive.AddBytes(&header, sizeof(header), kctgMsnd, &number) && archive.SetName(kctgMsnd, number, &name) &&
             scene->TrackActor(actor, 2) && actor->FGotoFrame(2);
        TAG tag = {ksidUseCrf, resource, kctgMsnd, number};
        TAG missing = {ksidUseCrf, resource, kctgMsnd, 999999};
        ok = ok && actor->FSetSnd(&tag, tYes, tNo, tNo, 24576, stySfx) &&
             !actor->FSetSnd(&missing, tNo, tNo, tNo, 12345, stySfx);
        PDynamicArray tags = nullptr;
        LegacyLong volume = 0;
        bool loop = false;
        ok = ok && actor->FQuerySnd(stySfx, false, &tags, &volume, &loop) &&
             tags && tags->IvMac() == 1 && volume == 24576 && loop;
        if (ok)
        {
            TAG read;
            tags->Get(0, &read);
            ok = read.sid == tag.sid && read.ctg == tag.ctg && read.cno == tag.cno;
        }
        ReleasePpo(&tags);
    }
    if (movie)
        movie->ClearUndo();
    ReleasePpo(&actor);
    ReleasePpo(&scene);
    ReleasePpo(&movie);
    vpers->Clear();
    if (!ok)
        std::fprintf(stderr, "Actor sound transaction fixture failed\n");
    return ok;
}

static bool TestActorEditTransactions(PMovie parent)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_EDIT_TRANSACTION_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file?ChunkyResourceFile::PcrfNew(file,0):nullptr;
    bool ok=resource!=nullptr;
    for (int mode=0;ok && mode<80;++mode)
    {
        const long refs=resource->CactRef();
        PMovie movie=Movie::PmvieNew(fFalse,parent->Pmcc(),nullptr,cnoNil);
        auto scene=movie?NewObj StageTestScene(movie):nullptr; auto actor=NewObj TrackTestActor();
        ok=movie && movie->FNewScenInsCore(0) && scene && scene->Init() && actor && actor->FInitPlayback(scene,resource);
        if (ok) { std::fprintf(stderr,"[transaction fixture] mode=%d\n",mode); ok=actor->FTestEditTransaction(mode,snapshot,scene); }
        if (movie) movie->ClearUndo(); ReleasePpo(&actor); ReleasePpo(&scene); ReleasePpo(&movie);
        if (resource->CactRef()!=refs) { std::fprintf(stderr,"Edit transaction reference imbalance at mode %d\n",mode); ok=false; }
    }
    ReleasePpo(&resource); ReleasePpo(&file);
    file=ChunkyFile::PcflCreateTemp(); resource=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
    ChunkNumber materials[2]={},customs[2]={},templateNumber=0; byte dummy=0;
    ok=ok && resource && file->FAddPv(&dummy,1,kctgTmpl,&templateNumber);
    for (int i=0;ok && i<2;++i)
    {
        MaterialOnFile material={kboCur,koskCur,0xffffff,0,0,0,static_cast<byte>(32+i*16),15,rOne};
        CustomMaterialOnFile custom={kboCur,koskCur,0};
        ok=file->FAddPv(&material,sizeof(material),kctgMtrl,&materials[i]) &&
            file->FAddPv(&custom,sizeof(custom),kctgCmtl,&customs[i]) &&
            file->FAdoptChild(kctgCmtl,customs[i],kctgMtrl,materials[i],0) &&
            file->FAdoptChild(kctgTmpl,templateNumber,kctgCmtl,customs[i],i);
    }
    for (int mode=128;ok && mode<160;++mode)
    {
        const long refs=resource->CactRef(); PMovie movie=Movie::PmvieNew(fFalse,parent->Pmcc(),nullptr,cnoNil);
        auto scene=movie?NewObj StageTestScene(movie):nullptr; auto actor=NewObj TrackTestActor();
        ok=movie && movie->FNewScenInsCore(0) && scene && scene->Init() && actor && actor->FInit(scene,resource);
        if (ok) ok=actor->FTestCostumeEdit(mode,snapshot,resource,templateNumber,materials,customs,true);
        if (movie) movie->ClearUndo(); ReleasePpo(&actor); ReleasePpo(&scene); ReleasePpo(&movie);
        if (resource->CactRef()!=refs) ok=false;
    }
    ReleasePpo(&resource); ReleasePpo(&file); vpers->Clear();
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok && TestActorSoundTransaction(parent);
}
#endif
