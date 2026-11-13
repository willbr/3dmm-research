#ifndef ENGINE_ACTOR_COPY_FIXTURE_H
#define ENGINE_ACTOR_COPY_FIXTURE_H
static bool TestActorCopy(PMovie parent)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("THREE_MM_ACTOR_COPY_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    bool ok=true;
    for (int mode=0;ok && mode<320;++mode)
    {
        PMovie movie=Movie::PmvieNew(fFalse,parent->Pmcc(),nullptr,cnoNil); PChunkyResourceFile resource=nullptr;
        auto actor=NewObj TrackTestActor(); byte dummy=0;
        ok=movie && actor && movie->FNewScenInsCore(0) && movie->FAutoSave() && movie->FEnsureAutosave(&resource);
        ChunkNumber templateNumber=0,materialNumber=0,soundNumber=0,targetSound=0; ChildChunkID child=0;
        MaterialOnFile material={kboCur,koskCur,0xffffff,0,0,0,32,15,rOne};
        PChunkyFile file=resource?resource->Pcfl():nullptr;
        ok=ok && file->FAddPv(&dummy,1,kctgTmpl,&templateNumber) && file->FAddPv(&dummy,1,kctgTmpl,&templateNumber) &&
            file->FAddPv(&material,sizeof(material),kctgMtrl,&materialNumber) && file->FAddPv(&dummy,1,kctgMsnd,&soundNumber) &&
            file->FAddPv(&dummy,1,kctgMsnd,&targetSound) && movie->FChidFromUserSndCno(targetSound,&child);
        if (ok) ok=actor->FInitPlayback(movie->Pscen(),resource);
        if (ok) { std::fprintf(stderr,"[copy fixture] mode=%d\n",mode); ok=actor->FTestCopy(mode,snapshot,resource,templateNumber,materialNumber,soundNumber,child); }
        ReleasePpo(&actor); ReleasePpo(&movie);
    }
    vpers->Clear(); if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}
#endif
