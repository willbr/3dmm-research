#ifndef ENGINE_ROUTE_PASTE_FIXTURE_H
#define ENGINE_ROUTE_PASTE_FIXTURE_H
static bool TestActorRoutePaste(PMovie movie)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("THREE_MM_ACTOR_ROUTE_PASTE_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    PChunkyFile file=ChunkyFile::PcflCreateTemp(); PChunkyResourceFile resource=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
    ChunkNumber materialNumber=0,soundNumber=0; byte dummy=0;
    MaterialOnFile material={kboCur,koskCur,0xffffff,0,0,0,32,15,rOne};
    bool ok=resource && file->FAddPv(&material,sizeof(material),kctgMtrl,&materialNumber) && file->FAddPv(&dummy,1,kctgMsnd,&soundNumber);
    for (int mode=0;ok && mode<256;++mode)
    {
        const long refs=resource->CactRef(); auto scene=NewObj StageTestScene(movie);
        auto actor=NewObj TrackTestActor(); auto clipboard=NewObj TrackTestActor();
        ok=scene && scene->Init() && actor && clipboard && actor->FInitPlayback(scene,resource) && clipboard->FInitPlayback(scene,resource);
        if (ok) { std::fprintf(stderr,"[route paste fixture] mode=%d\n",mode); ok=actor->FTestRoutePaste(mode,snapshot,scene,clipboard,resource,materialNumber,soundNumber); }
        ReleasePpo(&clipboard); ReleasePpo(&actor); ReleasePpo(&scene);
        if (resource->CactRef()!=refs) { std::fprintf(stderr,"Route paste ownership imbalance at mode %d\n",mode); ok=false; }
    }
    ReleasePpo(&resource); ReleasePpo(&file); vpers->Clear();
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}
#endif
