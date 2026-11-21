#ifndef ENGINE_ACTOR_PASTE_FIXTURE_H
#define ENGINE_ACTOR_PASTE_FIXTURE_H
static bool TestActorPaste(PMovie parent)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_PASTE_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    bool ok=true;
    for (int mode=0;ok && mode<64;++mode)
    {
        PMovie movie=Movie::PmvieNew(false,parent->Pmcc(),nullptr,cnoNil);
        PChunkyResourceFile target=nullptr,source=nullptr; PChunkyFile file=nullptr;
        PTemplate textTemplate=nullptr; auto actor=NewObj TrackTestActor(); StageTestScene *scene=nullptr;
        ok=movie && actor && movie->FNewScenInsCore(0) && movie->FAutoSave() && movie->FEnsureAutosave(&target);
        if (ok && (mode&32))
        { file=ChunkyFile::PcflCreateTemp(); source=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr; ok=source!=nullptr; }
        else if (ok) { source=target; source->AddRef(); }
        byte dummy=0; ChunkNumber soundNumber=0,templateNumber=0;
        if (ok) ok=MovieArchiveFixture(source).AddBytes(&dummy,1,kctgMsnd,&soundNumber);
        if (ok && (mode&16))
        {
            String text; text.SetSzs(PszLit("Paste")); TAG font={1,nullptr,kctgTdf,0};
            auto tdt=EngineText::PtdtNew(&text,tdtsNormal,&font);
            ok=tdt && EngineText::FWrite(tdt, source->ArchiveWriter(), kctgTmpl, &templateNumber); mm_template_owner_release(tdt);
            if (ok) { textTemplate=EngineTemplate::Fetch(source, kctgTmpl,templateNumber); ok=textTemplate!=nullptr; }
        }
        if (ok) { scene=NewObj StageTestScene(movie); ok=scene && scene->Init() && actor->FInitPlayback(scene,source); }
        if (ok)
        { std::fprintf(stderr,"[actor paste fixture] mode=%d\n",mode); ok=actor->FTestPaste(mode,snapshot,scene,source,target,soundNumber,textTemplate,templateNumber); }
        ReleasePpo(&actor); ReleasePpo(&scene); mm_template_owner_release(textTemplate);
        ReleasePpo(&source); ReleasePpo(&file); ReleasePpo(&movie);
    }
    vpers->Clear(); if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}
#endif
