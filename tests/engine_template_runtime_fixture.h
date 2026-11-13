#ifndef ENGINE_TEMPLATE_RUNTIME_FIXTURE_H
#define ENGINE_TEMPLATE_RUNTIME_FIXTURE_H
class TemplateRuntimeActionAccess : public ActionDefinition
{
  public:
    static void Sounds(PActionDefinition action,PDynamicArray sounds)
    { auto member=&TemplateRuntimeActionAccess::_action; (action->*member).sounds=sounds; }
};
class TemplateRuntimeFixture : public Template
{
  public:
    PActionDefinition action=nullptr;
    PModel models[2]={};
    int mode=0,trace[64]={},used=0;
    static TemplateRuntimeFixture *Create(int parts)
    {
        auto t=NewObj TemplateRuntimeFixture; if (!t) return nullptr;
        t->_cactn=1; t->_cbset=parts?2:0;
        t->_pglibactPar=DynamicArray::PglNew(sizeof(short)); t->_pglibset=DynamicArray::PglNew(sizeof(short));
        t->_pggcmid=GeneralGroup::PggNew(sizeof(int32_t));
        bool ok=t->_pglibactPar && t->_pglibset && t->_pggcmid;
        for (int i=0;ok && i<parts;++i) { short p=i?static_cast<short>((i-1)/2):-1,s=static_cast<short>(i%2); ok=t->_pglibactPar->FAdd(&p) && t->_pglibset->FAdd(&s); }
        for (int i=0;ok && i<t->_cbset;++i) { int32_t id=i,count=1; ok=t->_pggcmid->FAdd(sizeof(id),nullptr,&id,&count); }
        if (!ok) { ReleasePpo(&t); return nullptr; } return t;
    }
    PActionDefinition _PactnFetch(LegacyLong id) override
    { trace[used++]=100+id; if (mode==1) return nullptr; action->AddRef(); return action; }
    PModel _PmodlFetch(ChildChunkID id) override
    { trace[used++]=200+id; if (mode==2 && id==1) return nullptr; models[id]->AddRef(); return models[id]; }
};
static bool TestTemplateRuntime()
{
    FILE *snapshot=nullptr; if (const char *path=std::getenv("THREE_MM_TEMPLATE_RUNTIME_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    BRV vertices[3]={}; BRF face={}; vertices[1].p.v[0]=BR_SCALAR(1); vertices[2].p.v[1]=BR_SCALAR(1); face.vertices[1]=1; face.vertices[2]=2;
    auto a=Model::PmodlNew(3,vertices,1,&face),b=Model::PmodlNew(3,vertices,1,&face); auto material=Material_MTRL::PmtrlNew();
    bool ok=a && b && material; int successes=0,failures=0;
    const int counts[]={0,2,6},cels[]={-7,-1,0,2,7};
    for (int shape=0;ok && shape<3;++shape) for (int mode=0;ok && mode<3;++mode) for (int cn=0;ok && cn<5;++cn)
    for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<3;++failure)
    {
        int n=counts[shape]; auto t=TemplateRuntimeFixture::Create(n); auto group=GeneralGroup::PggNew(sizeof(AnimationCel));
        auto matrices=DynamicArray::PglNew(sizeof(BMAT34)); ok=t && group && matrices;
        for (int i=0;ok && i<3;++i)
        {
            AnimationCel cel={static_cast<ChildChunkID>(i%2),BR_SCALAR(i+1)}; CelPartSpec parts[6]={};
            for (int j=0;j<n;++j) { parts[j].chidModl=j%3==0?-1:static_cast<short>((i+j)%2); parts[j].imat34=static_cast<short>(i); }
            BMAT34 matrix; BrMatrix34Identity(&matrix); matrix.m[3][0]=BR_SCALAR(i+5);
            ok=group->FAdd(n*sizeof(CelPartSpec),nullptr,parts,&cel) && matrices->FAdd(&matrix);
        }
        auto action=ok?ActionDefinition::PactnNew(group,matrices,13):nullptr;
        auto sounds=DynamicArray::PglNew(sizeof(TAG));
        for (int i=0;ok && i<2;++i) { TAG sound={123+i,nullptr,kctgMsnd,static_cast<ChunkNumber>(456+i)}; ok=sounds && sounds->FAdd(&sound); }
        if (action && cn%2) TemplateRuntimeActionAccess::Sounds(action,sounds);
        PBody body=nullptr;
        if (t) { t->action=action; t->models[0]=a; t->models[1]=b; t->mode=mode; }
        auto parents=DynamicArray::PglNew(sizeof(short)),sets=DynamicArray::PglNew(sizeof(short));
        for (int i=0;ok && i<n;++i) { short p=i?static_cast<short>((i-1)/2):-1,s=static_cast<short>(i%2); ok=parents->FAdd(&p) && sets->FAdd(&s); }
        body=ok?Body::PbodyNew(parents,sets):nullptr; ok=ok && action && body;
        if (body) { for (int i=0;i<body->Cbset();++i) body->SetPartSetMtrl(i,material); for (int i=0;i<n;++i) body->SetPartModel(i,a); }
        BRS distance=BR_SCALAR(-33); bool applied=false;
        if (ok)
        {
#ifdef DEBUG
            vmutxMem.Enter(); auto allocator=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
            allocator->cactDo=failure-1; allocator->cactFail=failure?1:0; vmutxMem.Leave();
#endif
            applied=t->FSetActnCel(body,0,cels[cn],cn%2?nullptr:&distance);
#ifdef DEBUG
            vmutxMem.Enter(); allocator->cactFail=0; vmutxMem.Leave();
#endif
            if (applied) ++successes; else ++failures;
            LegacyLong values[]={shape,mode,cn,kind,failure,applied,distance,a->CactRef(),b->CactRef(),action->CactRef(),t->used};
            ok=record(values,sizeof(values)) && record(t->trace,t->used*sizeof(int)) && BodyEditFixtureAccess::Record(body,snapshot,nullptr) && ok;
            for (int i=0;i<n;++i) { LegacyLong identity=body->PbactPart(i)->model==a->Pbmdl()?1:body->PbactPart(i)->model==b->Pbmdl()?2:0; ok=record(&identity,sizeof(identity)) && ok; }
            LegacyLong count=-77; LegacyUlong flags=0x1357; bool exists=true; TAG tag={123,nullptr,0x12345678,456};
            BRS d=BR_SCALAR(-99); bool dOk=t->FGetDwrActnCel(0,cels[cn],&d),countOk=t->FGetCcelActn(0,&count),flagsOk=t->FGetGrfactn(0,&flags),soundOk=t->FGetSndActnCel(0,cels[cn],&exists,&tag);
            LegacyLong queries[]={dOk,d,countOk,count,flagsOk,static_cast<LegacyLong>(flags),soundOk,exists,tag.sid,static_cast<LegacyLong>(tag.ctg),static_cast<LegacyLong>(tag.cno)};
            ok=record(queries,sizeof(queries)) && action->CactRef()==1 && ok;
        }
        if (action) TemplateRuntimeActionAccess::Sounds(action,nullptr);
        ReleasePpo(&sounds); ReleasePpo(&body); ReleasePpo(&t); ReleasePpo(&action); ReleasePpo(&group); ReleasePpo(&matrices); ReleasePpo(&parents); ReleasePpo(&sets); vpers->Clear();
        ok=ok && a->CactRef()==1 && b->CactRef()==1 && material->CactRef()==1;
        if (!ok) std::fprintf(stderr,"Template runtime failed: %d %d %d %d %d\n",shape,mode,cn,kind,failure);
    }
    for (int variant=0;ok && variant<5;++variant)
    {
        auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr; ChunkNumber number=0;
        ok=cache && TemplateCreateFixtureAccess::Author(file,&number,6,0);
        auto t=ok?static_cast<PTemplate>(cache->PbacoFetch(kctgTmpl,number,Template::FReadTmpl)):nullptr;
        ChildChunkIdentification costumes[4]={}; ChunkNumber modelA=0,modelB=0; byte dummy=0;
        ok=ok && t && file->FAddPv(&dummy,1,kctgBmdl,&modelA) && file->FAddPv(&dummy,1,kctgBmdl,&modelB);
        for (int i=0;ok && i<4;++i) ok=file->FGetKidChidCtg(kctgTmpl,number,i,kctgCmtl,&costumes[i]);
        if (ok && variant)
        {
            ChildChunkID slot=variant==3?2:0;
            ok=file->FAdoptChild(kctgCmtl,costumes[1].cki.cno,kctgBmdl,modelA,slot) &&
                file->FAdoptChild(kctgCmtl,costumes[3].cki.cno,kctgBmdl,variant==2?modelB:modelA,slot);
            if (variant==4) { file->SetLoner(kctgCmtl,costumes[1].cki.cno,true); file->DeleteChild(kctgTmpl,number,kctgCmtl,costumes[1].cki.cno,1); }
        }
        for (int set=0;ok && set<2;++set)
        {
            LegacyLong accessory=-77; bool has=t->FBsetIsAccessory(set),found=t->FIbsetAccOfIbset(set,&accessory);
            LegacyLong values[]={variant,set,has,found,accessory,t->CcmidOfBset(set),t->CmidOfBset(set,0),t->CmidOfBset(set,1)};
            ok=record(values,sizeof(values));
        }
        for (int i=0;ok && i<4;++i) for (int j=0;ok && j<4;++j)
        {
            LegacyLong values[]={variant,i,j,t->FSameAccCmids(i,j),CustomMaterial_CMTL::FHasModels(file,kctgCmtl,costumes[i].cki.cno),CustomMaterial_CMTL::FEqualModels(file,costumes[i].cki.cno,costumes[j].cki.cno)};
            ok=record(values,sizeof(values));
        }
        String name,expectedName; expectedName.SetSzs(PszLit("Runtime action")); ChildChunkIdentification actionChild;
        ok=file->FGetKidChidCtg(kctgTmpl,number,0,kctgActn,&actionChild) && file->FSetName(kctgActn,actionChild.cki.cno,&expectedName) && ok;
        LegacyLong named=t && t->FGetActnName(0,&name) && name.FEqualSz(PszLit("Runtime action")); ok=named && record(&named,sizeof(named)) && ok;
        ReleasePpo(&t); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
    }
    std::fprintf(stderr,"[template runtime] successes=%d failures=%d\n",successes,failures);
#ifdef DEBUG
    ok=ok && successes==168 && failures==237;
#else
    ok=ok && successes==189 && failures==216;
#endif
    ReleasePpo(&a); ReleasePpo(&b); ReleasePpo(&material);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && successes && failures;
}
#endif
