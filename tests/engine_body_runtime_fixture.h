#ifndef ENGINE_BODY_RUNTIME_FIXTURE_H
#define ENGINE_BODY_RUNTIME_FIXTURE_H
class BodyRuntimeCustomAccess : public CustomMaterial_CMTL
{
  public:
    static void SetModel(PCustomMaterial_CMTL c,int i,PModel model)
    { auto member=&BodyRuntimeCustomAccess::_prgpmodl; (c->*member)[i]=model; model->AddRef(); }
};
static bool TestBodyRuntime()
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("THREE_MM_BODY_RUNTIME_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    BRV vertices[3]={}; BRF face={}; vertices[1].p.v[0]=BR_SCALAR(1); vertices[2].p.v[1]=BR_SCALAR(1); face.vertices[1]=1; face.vertices[2]=2;
    auto a=Model::PmodlNew(3,vertices,1,&face),b=Model::PmodlNew(3,vertices,1,&face);
    auto ma=Material_MTRL::PmtrlNew(),mb=Material_MTRL::PmtrlNew();
    PMaterial_MTRL parts[]={ma,mb,ma,mb,ma,mb};
    auto ca=ma && mb?CustomMaterial_CMTL::PcmtlNew(0,6,parts):nullptr;
    auto cb=ma && mb?CustomMaterial_CMTL::PcmtlNew(1,6,parts):nullptr;
    bool ok=a && b && ma && mb && ca && cb; int captures=0,failed=0;
    if (ok) { BodyRuntimeCustomAccess::SetModel(ca,0,a); BodyRuntimeCustomAccess::SetModel(ca,2,b);
        BodyRuntimeCustomAccess::SetModel(cb,0,b); BodyRuntimeCustomAccess::SetModel(cb,2,a); }
    const int counts[]={0,2,6};
    for (int shape=0;ok && shape<3;++shape) for (int visibility=0;ok && visibility<3;++visibility)
    for (int accessory=0;ok && accessory<2;++accessory) for (int failure=0;ok && failure<5;++failure)
    {
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        BodyEditRenderHost host={BrActorAllocate(BR_ACTOR_NONE,0),0,0};
        auto parents=DynamicArray::PglNew(sizeof(short)),sets=DynamicArray::PglNew(sizeof(short));
        auto newParents=DynamicArray::PglNew(sizeof(short)),newSets=DynamicArray::PglNew(sizeof(short));
        ok=host.root && parents && sets && newParents && newSets;
        int n=counts[shape],next=counts[(shape+visibility+1)%3];
        for (int i=0;ok && i<n;++i) { short par=i?static_cast<short>((i-1)/2):-1,set=static_cast<short>(i%2); ok=parents->FAdd(&par) && sets->FAdd(&set); }
        for (int i=0;ok && i<next;++i) { short par=i?static_cast<short>(i-1):-1,set=static_cast<short>(i%3); ok=newParents->FAdd(&par) && newSets->FAdd(&set); }
        auto body=ok?Body::PbodyNew(parents,sets):nullptr; auto costume=NewObj BodyCostume;
        ok=ok && body && costume;
        auto state=[&](int stage) {
            LegacyLong values[]={shape,visibility,accessory,failure,stage,a->CactRef(),b->CactRef(),ma->CactRef(),mb->CactRef(),ca->CactRef(),cb->CactRef(),host.attached,host.changed};
            return record(values,sizeof(values)) && BodyEditFixtureAccess::Record(body,snapshot,host.root);
        };
        if (ok)
        {
            for (int i=0;i<body->Cbset();++i) body->SetPartSetMtrl(i,ma);
            body->SetRenderCallbacks(&host,BodyEditAttach,BodyEditChanged);
            if (!visibility) body->Show(); else if (visibility==2) { body->Hide(); body->Hide(); }
            BodyEditFixtureAccess::Seed(body); ok=state(0);
            for (int i=0;i<n;++i) { body->SetPartModel(i,a); body->SetPartModel(i,a); body->SetPartModel(i,b); BMAT34 m; BrMatrix34Identity(&m); m.m[3][1]=BR_SCALAR(i+3); body->SetPartMatrix(i,&m); }
            BMAT34 root; BrMatrix34Identity(&root); root.m[3][0]=BR_SCALAR(9); body->LocateOrient(BR_SCALAR(2),BR_SCALAR(-3),BR_SCALAR(4),&root);
            body->Hilite(); body->Hilite(); body->Unhilite(); ok=state(1) && ok;
            ok=costume->FGet(body) && state(2) && ok;
            for (int i=0;i<body->Cbset();++i) { body->SetPartSetMtrl(i,mb); body->SetPartSetMtrl(i,mb); }
            ok=state(3) && ok;
            if (accessory) { if (body->Cbset()) { body->SetPartSetCmtl(ca); body->SetPartSetCmtl(ca); } if (body->Cbset()>1) { body->SetPartSetCmtl(cb); body->SetPartSetCmtl(cb); } }
            ok=state(4) && ok;
            for (int i=0;i<body->Cbset();++i)
            {
                bool generic=false; PMaterial_MTRL material=reinterpret_cast<PMaterial_MTRL>(0x1357); PCustomMaterial_CMTL custom=nullptr;
                body->GetPartSetMaterial(i,&generic,&material,&custom);
                LegacyLong identity=generic?(material==ma?1:material==mb?2:0):(custom==ca?3:custom==cb?4:0);
                ok=identity!=0 && record(&identity,sizeof(identity)) &&
                    (generic?record(&custom,sizeof(custom)):record(&material,sizeof(material))) && ok;
            }
#ifdef DEBUG
            if (failure) { vmutxMem.Enter(); vdmglob.dmaglPv.cactDo=failure-1; vdmglob.dmaglPv.cactFail=1; vmutxMem.Leave(); }
#endif
            bool captured=costume->FGet(body);
#ifdef DEBUG
            vmutxMem.Enter(); vdmglob.dmaglPv.cactFail=0; vmutxMem.Leave();
#endif
            if (captured) ++captures; else ++failed;
            LegacyLong result=captured; ok=record(&result,sizeof(result)) && state(5) && ok;
            if (captured) { costume->Set(body); ok=state(6) && ok; }
            ok=body->FChangeShape(newParents,newSets) && ok;
            for (int i=0;i<body->Cbset();++i) body->SetPartSetMtrl(i,ma);
            if (captured) costume->Set(body,true);
            ok=state(7) && ok;
            body->Hide(); body->Hide(); body->Show(); body->Show(); ok=state(8) && ok;
        }
        ReleasePpo(&costume); ReleasePpo(&body);
        ok=ok && host.root->children==nullptr && a->CactRef()==3 && b->CactRef()==3 && ma->CactRef()==7 && mb->CactRef()==7 && ca->CactRef()==1 && cb->CactRef()==1;
        ReleasePpo(&parents); ReleasePpo(&sets); ReleasePpo(&newParents); ReleasePpo(&newSets); BrActorFree(host.root);
#ifdef DEBUG
        ok=ok && vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv;
#endif
        vpers->Clear();
        if (!ok) std::fprintf(stderr,"Body runtime failed: %d %d %d %d\n",shape,visibility,accessory,failure);
    }
    std::fprintf(stderr,"[body runtime] captures=%d failures=%d\n",captures,failed);
#ifdef DEBUG
    ok=ok && captures==72 && failed==18;
#else
    ok=ok && captures==90 && failed==0;
#endif
    ReleasePpo(&ca); ReleasePpo(&cb); ReleasePpo(&a); ReleasePpo(&b); ReleasePpo(&ma); ReleasePpo(&mb);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && captures>0;
}
#endif
