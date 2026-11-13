#ifndef ENGINE_GROUP_UNDO_FIXTURE_H
#define ENGINE_GROUP_UNDO_FIXTURE_H
struct GroupUndoProbe
{
    int count=0,fail=0;
    LegacyLong calls[16]{};
};
class GroupUndoChild : public ActorUndo
{
  public:
    GroupUndoProbe *probe=nullptr;
    int id=0;
    bool FUndo(PDocumentBase document) override
    {
        probe->calls[probe->count++]=id;
        return Pmvie()==document && Iscen()==7 && Nfrm()==13 && id!=probe->fail;
    }
};
class TestMoveGroupUndo : public ActorMoveGroupUndo
{
  public:
    TestMoveGroupUndo() {}
    bool Init() { _pglpaund=DynamicArray::PglNew(sizeof(PActorUndo)); return _pglpaund!=nullptr; }
    bool AddNull() { PActorUndo child=nullptr; return _pglpaund->FAdd(&child); }
};
static bool TestActorGroupUndo(PMovie parent)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("THREE_MM_ACTOR_GROUP_UNDO_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    bool ok=true;
    for (int mode=0;ok && mode<16;++mode)
    {
        auto group=NewObj TestMoveGroupUndo(); GroupUndoChild *children[3]{}; GroupUndoProbe probe;
        probe.fail=(mode&2)?((mode&4)?2:1):0;
        ok=group && group->Init();
        for (int i=0;ok && i<3;++i)
        {
            children[i]=NewObj GroupUndoChild(); ok=children[i]!=nullptr;
            if (ok) { children[i]->id=i+1; children[i]->probe=&probe; children[i]->SetIscen(-1); children[i]->SetNfrm(99); ok=group->FAddChild(children[i]); }
            if (ok && i==0 && (mode&1)) ok=group->AddNull();
        }
        if (ok)
        {
            group->SetPmvie(parent); group->SetIscen(7); group->SetNfrm(13);
            bool changed=(mode&8)?group->FDo(parent):group->FUndo(parent);
            LegacyLong values[]={mode,changed,group->Cchild(),probe.count,probe.calls[0],probe.calls[1],probe.calls[2],
                children[0]->Iscen(),children[0]->Nfrm(),children[1]->Iscen(),children[1]->Nfrm(),children[2]->Iscen(),children[2]->Nfrm(),
                children[0]->CactRef(),children[1]->CactRef(),children[2]->CactRef()};
            ok=record(values,sizeof(values)) && changed==(probe.fail==0);
        }
        ReleasePpo(&group);
        for (auto &child:children) { if (child && child->CactRef()!=1) ok=false; ReleasePpo(&child); }
    }
    for (int mode=0;ok && mode<4;++mode)
    {
#ifdef DEBUG
        if (mode) { vmutxMem.Enter(); vdmglob.dmaglBase.cactDo=mode; vdmglob.dmaglBase.cactFail=1; vmutxMem.Leave(); }
#endif
        PActorMoveGroupUndo move=ActorMoveGroupUndo::PamguNew();
#ifdef DEBUG
        vmutxMem.Enter(); vdmglob.dmaglBase.cactFail=0; vmutxMem.Leave();
#endif
        LegacyLong made[]={100+mode,move!=nullptr,move?move->Cchild():-1}; ok=record(made,sizeof(made)); ReleasePpo(&move);
#ifdef DEBUG
        if (mode) { vmutxMem.Enter(); vdmglob.dmaglBase.cactDo=mode; vdmglob.dmaglBase.cactFail=1; vmutxMem.Leave(); }
#endif
        PActorRenameGroupUndo rename=ActorRenameGroupUndo::PargNew();
#ifdef DEBUG
        vmutxMem.Enter(); vdmglob.dmaglBase.cactFail=0; vmutxMem.Leave();
#endif
        made[0]=200+mode; made[1]=rename!=nullptr; made[2]=rename?rename->Cchild():-1;
        ok=record(made,sizeof(made)) && ok; ReleasePpo(&rename);
    }
    PChunkyFile file=ChunkyFile::PcflCreateTemp(); PChunkyResourceFile resource=file?ChunkyResourceFile::PcrfNew(file,0):nullptr;
    byte dummy=0; ChunkNumber number=0;
    ok=ok && resource && file->FAddPv(&dummy,1,kctgTmpl,&number) && file->FAddPv(&dummy,1,kctgTmpl,&number) && number==1;
    for (int mode=0;ok && mode<8;++mode)
    {
        const long refs=resource->CactRef(); PMovie movie=Movie::PmvieNew(fFalse,parent->Pmcc(),nullptr,cnoNil);
        auto group=ActorRenameGroupUndo::PargNew(); TrackTestActor *actors[2]{};
        ok=movie && group && movie->FNewScenInsCore(0);
        String previous,current,other; previous.SetSzs(PszLit("previous")); current.SetSzs(PszLit("current")); other.SetSzs(PszLit("other"));
        for (int i=0;ok && i<2;++i)
        {
            actors[i]=NewObj TrackTestActor(); ok=actors[i] && actors[i]->FInitPlayback(movie->Pscen(),resource);
            if (ok) { actors[i]->SetArid(aridNil); ok=movie->Pscen()->FAddActrCore(actors[i]) && movie->FNameActr(actors[i]->Arid(),&current); }
        }
        if (ok)
        {
            group->SetPmvie(movie); group->SetIscen(0); group->SetNfrm(1);
            ok=group->FAddChild(actors[0]->Arid(),&previous);
            if (ok && (mode&1)) ok=group->FAddChild(123456,&previous);
            if (ok) ok=group->FAddChild(actors[1]->Arid(),&other);
            if (ok && (mode&2)) ok=group->FAddChild(actors[0]->Arid(),&other);
            for (int phase=0;ok && phase<3;++phase)
            {
                bool changed=((mode+phase)&4)?group->FDo(movie):group->FUndo(movie);
                LegacyLong values[]={300+mode,phase,changed,group->Cchild(),resource->CactRef()};
                ok=record(values,sizeof(values)) && changed;
                for (int i=0;ok && i<2;++i)
                {
                    String name; ok=movie->FGetName(actors[i]->Arid(),&name); LegacyLong count=name.Cch();
                    ok=record(&count,sizeof(count)) && record(name.Psz(),count*sizeof(achar)) && ok;
                }
            }
        }
        ReleasePpo(&group); for (auto &actor:actors) { ReleasePpo(&actor); } ReleasePpo(&movie);
        if (resource->CactRef()!=refs) ok=false;
    }
    ReleasePpo(&resource); ReleasePpo(&file); vpers->Clear();
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}
#endif
