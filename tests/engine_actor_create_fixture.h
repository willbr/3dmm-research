#ifndef ENGINE_ACTOR_CREATE_FIXTURE_H
#define ENGINE_ACTOR_CREATE_FIXTURE_H
static bool TestActorCreation(PMovie movie)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("THREE_MM_ACTOR_CREATE_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    bool ok=true; int successes=0,bodyFailures=0;
    for (int mode=0;ok && mode<120;++mode)
    {
        const int asset=mode/40,warm=(mode/20)%2,failure=mode%20;
        auto file=ChunkyFile::PcflCreateTemp(); auto resource=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
        byte dummy=0; ChunkNumber bad=0,number=0;
        ok=resource && file->FAddPv(&dummy,1,kctgTmpl,&bad) && TrackTestTemplate::FWriteFixture(file,&number);
        if (ok && asset==2)
        {
            ChildChunkIdentification action,cel;
            ok=file->FGetKidChidCtg(kctgTmpl,number,0,kctgActn,&action) &&
                file->FGetKidChidCtg(kctgActn,action.cki.cno,0,kctgGgcl,&cel);
            if (ok) file->DeleteChild(kctgActn,action.cki.cno,kctgGgcl,cel.cki.cno,cel.chid);
        }
        TAG tag={ksidUseCrf,resource,kctgTmpl,asset==1?bad:number};
        if (ok && warm)
        {
            auto tmpl=static_cast<PTemplate>(resource->PbacoFetch(tag.ctg,tag.cno,Template::FReadTmpl));
            if (tmpl && asset==0) { LegacyUlong flags=0; ok=tmpl->FGetGrfactn(0,&flags); }
            ReleasePpo(&tmpl);
        }
        const long refs=resource?resource->CactRef():0;
        PActor actor=nullptr;
        if (ok)
        {
#ifdef DEBUG
            if (failure) { vmutxMem.Enter(); vdmglob.dmaglBase.cactDo=failure; vdmglob.dmaglBase.cactFail=1; vmutxMem.Leave(); }
#endif
            actor=Actor::PactrNew(&tag);
#ifdef DEBUG
            vmutxMem.Enter(); vdmglob.dmaglBase.cactFail=0; vmutxMem.Leave();
#endif
            bool expected=asset==0;
#ifdef DEBUG
            expected=expected && (!failure || failure>=(warm?3:10));
#endif
            if ((actor!=nullptr)!=expected) ok=false;
        }
        const bool bodyFail=actor && ((warm && !failure) || failure%4==1);
        bool beforeBody=actor && actor->Pbody(),beforeScene=actor && actor->Pscen();
        if (actor)
        {
            ++successes;
#ifdef DEBUG
            if (bodyFail) { vmutxMem.Enter(); vdmglob.dmaglBase.cactDo=1; vdmglob.dmaglBase.cactFail=1; vmutxMem.Leave(); }
#endif
            actor->SetPscen(movie->Pscen());
#ifdef DEBUG
            vmutxMem.Enter(); vdmglob.dmaglBase.cactFail=0; vmutxMem.Leave();
            if (bodyFail) { ++bodyFailures; ok=ok && !actor->Pbody(); }
            else
#endif
                ok=ok && actor->Pbody()!=nullptr;
            ok=ok && !beforeBody && !beforeScene && actor->Pscen()==movie->Pscen();
            if (actor->Pbody())
            {
                auto body=actor->Pbody(); const long bodyRefs=body->CactRef(); actor->SetPscen(movie->Pscen());
                ok=ok && actor->Pbody()==body && body->CactRef()==bodyRefs;
            }
        }
        LegacyLong values[]={mode,actor!=nullptr,bodyFail,beforeBody,beforeScene,actor && actor->Pbody(),
            actor && actor->Pscen()==movie->Pscen(),refs,resource?resource->CactRef():0};
        ok=record(values,sizeof(values)) && ok;
        if (actor) ok=TrackTestActor::RecordClipboardActor(actor,snapshot) && ok;
        ReleasePpo(&actor);
        if (resource && resource->CactRef()!=refs) ok=false;
        ReleasePpo(&resource); ReleasePpo(&file); vpers->Clear();
        if (!ok) std::fprintf(stderr,"Actor creation fixture failed at mode %d\n",mode);
    }
    std::fprintf(stderr,"[actor creation] successes=%d bodyFailures=%d\n",successes,bodyFailures);
    ok=ok && successes>=2;
#ifdef DEBUG
    ok=ok && successes==29 && bodyFailures==7;
#endif
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}
#endif
