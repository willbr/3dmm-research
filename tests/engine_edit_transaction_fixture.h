#ifndef ENGINE_EDIT_TRANSACTION_FIXTURE_H
#define ENGINE_EDIT_TRANSACTION_FIXTURE_H
static bool TestActorEditTransactions(PMovie parent)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("THREE_MM_ACTOR_EDIT_TRANSACTION_SNAPSHOT"))
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
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}
#endif
