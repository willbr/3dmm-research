#ifndef ENGINE_BODY_EDIT_FIXTURE_H
#define ENGINE_BODY_EDIT_FIXTURE_H
class BodyEditFixtureAccess : public Body
{
  public:
    static void Seed(PBody b)
    {
        auto member=&BodyEditFixtureAccess::_geometry; auto &g=b->*member;
        g.found=1; g.set=1; g.bounds={1,2,30,40}; g.last_visible={5,6,70,80};
    }
    static bool Record(PBody b,FILE *out,PBACT host)
    {
        auto member=&BodyEditFixtureAccess::_geometry;
        auto record=[&](const void *p,size_t n) { return !out || std::fwrite(p,1,n,out)==n; };
        LegacyLong fields[]={b!=nullptr,b?b->Cbact():0,b?b->Cbset():0,b?BodyCreateFixtureAccess::Hidden(b):0,
            b && (b->*member).found,b?(b->*member).set:0};
        if (!record(fields,sizeof(fields))) return false;
        if (!b) return true;
        if (!record(&(b->*member).bounds,sizeof(RC)) || !record(&(b->*member).last_visible,sizeof(RC))) return false;
        auto root=b->PbactRoot();
        auto index=[&](PBACT p)->LegacyLong { if (!p) return -1; for (int i=0;i<b->Cbact();++i) if (p==root+i) return i; return -2; };
        for (int i=0;i<b->Cbact();++i)
        {
            auto a=root+i; LegacyLong prev=-1;
            if (a->prev) { prev=-2; for (int j=0;j<b->Cbact();++j) {
                if (a->prev==&(root+j)->children) prev=j*2;
                if (a->prev==&(root+j)->next) prev=j*2+1;
            } }
            LegacyLong row[]={a->type,a->render_style,a->t.type,index(a->parent),index(a->children),index(a->next),prev,
                a->identifier==(schar *)b,a->model!=nullptr,a->material!=nullptr,a->parent==host};
            if (!record(row,sizeof(row)) || !record(&a->t.t.mat,sizeof(BMAT34))) return false;
        }
        return true;
    }
};
struct BodyEditRenderHost { PBACT root; int attached,changed; };
static void BodyEditAttach(void *p,PBody body) { auto h=static_cast<BodyEditRenderHost *>(p); ++h->attached; BrActorAdd(h->root,body->PbactRoot()); }
static void BodyEditChanged(void *p) { ++static_cast<BodyEditRenderHost *>(p)->changed; }
static bool TestBodyEditing()
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("THREE_MM_BODY_EDIT_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    BRV vertices[3]={}; BRF face={}; vertices[1].p.v[0]=BR_SCALAR(1); vertices[2].p.v[1]=BR_SCALAR(1);
    face.vertices[1]=1; face.vertices[2]=2;
    auto model=Model::PmodlNew(3,vertices,1,&face);
    auto generic=Material_MTRL::PmtrlNew(),material=Material_MTRL::PmtrlNew();
    PMaterial_MTRL customParts[6]={material,material,material,material,material,material};
    auto custom=material?CustomMaterial_CMTL::PcmtlNew(1,6,customParts):nullptr;
    bool ok=model && generic && material && custom; int successes=0,failures=0;
    const int sourceCounts[]={0,1,6},targetCounts[]={0,1,4,8};
    for (int shape=0;ok && shape<3;++shape) for (int visibility=0;ok && visibility<3;++visibility)
    for (int highlight=0;ok && highlight<2;++highlight) for (int operation=0;ok && operation<2;++operation)
    for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<13;++failure)
    {
#ifdef DEBUG
        const LegacyLong beforeBase=vdmglob.dmaglBase.cv,beforeHq=vdmglob.dmaglHq.cv,beforePv=vdmglob.dmaglPv.cv;
#endif
        BodyEditRenderHost host={BrActorAllocate(BR_ACTOR_NONE,0),0,0};
        auto parents=DynamicArray::PglNew(sizeof(short)),sets=DynamicArray::PglNew(sizeof(short));
        auto newParents=DynamicArray::PglNew(sizeof(short)),newSets=DynamicArray::PglNew(sizeof(short));
        ok=host.root && parents && sets && newParents && newSets;
        int n=sourceCounts[shape],next=targetCounts[(shape+visibility+highlight)%4];
        for (int i=0;ok && i<n;++i) { short par=i?static_cast<short>((i-1)/2):-1,set=static_cast<short>(i%2); ok=parents->FAdd(&par) && sets->FAdd(&set); }
        for (int i=0;ok && i<next;++i) { short par=i?static_cast<short>(i-1):-1,set=static_cast<short>(i%2); ok=newParents->FAdd(&par) && newSets->FAdd(&set); }
        auto body=ok?Body::PbodyNew(parents,sets):nullptr; ok=ok && body;
        if (body)
        {
            if (n) body->SetPartSetMtrl(0,generic); if (n>1) body->SetPartSetCmtl(custom);
            for (int i=0;i<n;++i) { BMAT34 m; BrMatrix34Identity(&m); m.m[3][0]=BR_SCALAR(i+1); body->SetPartMatrix(i,&m); if (i%3!=1) body->SetPartModel(i,model); }
            BMAT34 root; BrMatrix34Identity(&root); root.m[3][2]=BR_SCALAR(37); body->LocateOrient(BR_SCALAR(1),BR_SCALAR(2),BR_SCALAR(3),&root);
            body->SetRenderCallbacks(&host,BodyEditAttach,BodyEditChanged);
            if (highlight) body->Hilite();
            if (!visibility) body->Show(); else if (visibility==2) { body->Hide(); body->Hide(); }
            BodyEditFixtureAccess::Seed(body);
        }
        auto refs=[&]() { LegacyLong r[]={model->CactRef(),generic->CactRef(),material->CactRef(),custom->CactRef(),host.attached,host.changed}; return record(r,sizeof(r)); };
        LegacyLong mode[]={shape,visibility,highlight,operation,kind,failure,next}; ok=record(mode,sizeof(mode)) && refs() && ok;
        PBody copy=nullptr; bool result=false;
        if (ok)
        {
#ifdef DEBUG
            vmutxMem.Enter(); auto allocator=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
            if (failure) { allocator->cactDo=failure; allocator->cactFail=1; } vmutxMem.Leave();
#endif
            if (operation) result=body->FChangeShape(newParents,newSets);
            else { copy=body->PbodyDup(); result=copy!=nullptr; }
#ifdef DEBUG
            vmutxMem.Enter(); allocator->cactFail=0; vmutxMem.Leave();
#endif
            if (result) ++successes; else ++failures;
            LegacyLong value=result; ok=record(&value,sizeof(value)) && refs() && BodyEditFixtureAccess::Record(body,snapshot,host.root) && BodyEditFixtureAccess::Record(copy,snapshot,host.root) && ok;
            if (copy) { body->Restore(copy); ReleasePpo(&copy); }
            ok=refs() && BodyEditFixtureAccess::Record(body,snapshot,host.root) && ok;
        }
        ReleasePpo(&copy); ReleasePpo(&body);
        ok=ok && host.root->children==nullptr && model->CactRef()==1 && generic->CactRef()==1 && material->CactRef()==7 && custom->CactRef()==1;
        ReleasePpo(&parents); ReleasePpo(&sets); ReleasePpo(&newParents); ReleasePpo(&newSets); BrActorFree(host.root);
#ifdef DEBUG
        ok=ok && vdmglob.dmaglBase.cv==beforeBase && vdmglob.dmaglHq.cv==beforeHq && vdmglob.dmaglPv.cv==beforePv;
#endif
        vpers->Clear();
        if (!ok) std::fprintf(stderr,"Body editing failed: %d %d %d %d %d %d\n",shape,visibility,highlight,operation,kind,failure);
    }
    std::fprintf(stderr,"[body editing] successes=%d failures=%d\n",successes,failures);
#ifdef DEBUG
    ok=ok && successes==1278 && failures==126;
#else
    ok=ok && successes==1404 && failures==0;
#endif
    ReleasePpo(&custom); ReleasePpo(&model); ReleasePpo(&generic); ReleasePpo(&material);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && successes>0;
}
#endif
