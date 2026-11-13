#ifndef ENGINE_ACTOR_CLIPBOARD_FIXTURE_H
#define ENGINE_ACTOR_CLIPBOARD_FIXTURE_H
class ClipboardFixtureAccess : public ActorClipboard
{
  public:
    static PActor ActorValue(PActorClipboard clipboard)
    { auto member=&ClipboardFixtureAccess::_clipboard; return static_cast<PActor>((clipboard->*member).actor); }
    static PString Name(PActorClipboard clipboard)
    { auto member=&ClipboardFixtureAccess::_stnName; return &(clipboard->*member); }
};
static bool TestActorClipboard(PMovie parent)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("THREE_MM_ACTOR_CLIPBOARD_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    auto client=static_cast<TestMcc *>(parent->Pmcc()); client->testingClipboard=true;
    PChunkyFile file=ChunkyFile::PcflCreateTemp(); PChunkyResourceFile resource=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
    byte dummy=0; ChunkNumber number=0;
    bool ok=resource && file->FAddPv(&dummy,1,kctgTmpl,&number) && TrackTestTemplate::FWriteFixture(file,&number);
    for (int mode=0;ok && mode<64;++mode)
    {
        const long refs=resource->CactRef(); auto movie=Movie::PmvieNew(false,client,nullptr,cnoNil);
        auto actor=NewObj TrackTestActor(); PActorClipboard clipboard=nullptr;
        ok=movie && actor && movie->FNewScenInsCore(0) && actor->FInitPlayback(movie->Pscen(),resource) && actor->FInitClipboard(resource,number);
        String name;
        const char *names[]={"Alex","Copy of Alex","Copy ofX","copy of Alex"}; name.SetSzs(const_cast<char *>(names[mode>>4]));
        switch ((mode>>2)&3)
        {
        case 0: client->clipboardPrefix.SetSzs(PszLit("Copy of")); break;
        case 1: client->clipboardPrefix.SetSzs(PszLit("copy of")); break;
        case 2: client->clipboardPrefix.SetNil(); break;
        case 3: { achar units[]={'C',0,'X'}; client->clipboardPrefix.SetRgch(units,3); break; }
        }
        if (ok) { actor->SetArid(aridNil); ok=movie->Pscen()->FAddActrCore(actor) && movie->FNameActr(actor->Arid(),&name); }
        const bool route=(mode&1)!=0,entire=!route && (mode&2);
        if (ok) { std::fprintf(stderr,"[clipboard fixture] mode=%d\n",mode); clipboard=ActorClipboard::PaclpNew(actor,route,entire); ok=clipboard!=nullptr; }
        if (ok)
        {
            movie->Pscen()->SelectActr(nullptr); /* No GUI view or selected route target. */
            auto copied=ClipboardFixtureAccess::ActorValue(clipboard); auto stored=ClipboardFixtureAccess::Name(clipboard);
            const bool pasted=clipboard->FPaste(movie);
            LegacyLong values[]={mode,clipboard->FRouteOnly(),pasted,stored->Cch(),resource->CactRef()};
            ok=record(values,sizeof(values)) && record(stored->Psz(),stored->Cch()*sizeof(achar)) &&
                TrackTestActor::RecordClipboardActor(actor,snapshot) && TrackTestActor::RecordClipboardActor(copied,snapshot);
        }
        ReleasePpo(&clipboard); ReleasePpo(&actor); ReleasePpo(&movie); vpers->Clear();
        if (resource->CactRef()!=refs) { std::fprintf(stderr,"Clipboard reference imbalance mode %d\n",mode); ok=false; }
    }
    client->testingClipboard=false; ReleasePpo(&resource); ReleasePpo(&file);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}
#endif
