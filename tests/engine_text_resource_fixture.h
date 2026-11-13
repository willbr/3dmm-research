#ifndef ENGINE_TEXT_RESOURCE_FIXTURE_H
#define ENGINE_TEXT_RESOURCE_FIXTURE_H
static PThreeDFont TextResourceNoFont(void *context,const TAG &)
{ ++*static_cast<int *>(context); return nullptr; }
class TextResourceFixtureAccess : public ThreeDText
{
  public:
    static TextResourceFixtureAccess *Create() { return NewObj TextResourceFixtureAccess; }
    bool Initialize(PChunkyFile file,ChunkNumber n) { return _FInit(file,kctgTmpl,n); }
    static bool Provider(PThreeDText t,void *context)
    { auto c=&TextResourceFixtureAccess::_fontContext; auto f=&TextResourceFixtureAccess::_fontResolver; return t->*c==context && t->*f==TextResourceNoFont; }
    static PStringTable_GST DetachNames() { auto t=static_cast<PStringTable_GST>(_actionNames.table); _actionNames.table=nullptr; return t; }
    static PActionDefinition Seed(PThreeDText t)
    {
        auto cels=GeneralGroup::PggNew(sizeof(AnimationCel)); auto matrices=DynamicArray::PglNew(sizeof(BMAT34));
        AnimationCel cel={}; BMAT34 matrix; BrMatrix34Identity(&matrix);
        bool ok=cels && matrices && cels->FAdd(0,nullptr,nullptr,&cel) && matrices->FAdd(&matrix);
        auto action=ok?ActionDefinition::PactnNew(cels,matrices,0):nullptr;
        ReleasePpo(&cels); ReleasePpo(&matrices);
        if (action) { auto field=&TextResourceFixtureAccess::_generated; auto &state=t->*field; state.action=action; state.action_id=tdaRest; action->AddRef(); }
        return action;
    }
    static bool Record(PThreeDText t,FILE *out,PChunkyResourceFile cache)
    {
        if (!t) return true;
        auto record=[&](const void *p,size_t n) { return !out || std::fwrite(p,1,n,out)==n; };
        auto text=&TextResourceFixtureAccess::_stn; auto descriptor=&TextResourceFixtureAccess::_descriptor;
        auto generated=&TextResourceFixtureAccess::_generated; auto &g=t->*generated; auto &d=t->*descriptor; auto &font=d.font;
        auto parents=&TextResourceFixtureAccess::_pglibactPar; auto sets=&TextResourceFixtureAccess::_pglibset; auto costumes=&TextResourceFixtureAccess::_pggcmid;
        auto count=&TextResourceFixtureAccess::_cbset; auto custom=&TextResourceFixtureAccess::_ccmid; auto actions=&TextResourceFixtureAccess::_cactn;
        auto flags=&TextResourceFixtureAccess::_grftmpl;
        LegacyLong fields[]={d.shape,(t->*text).Cch(),font.sid,static_cast<LegacyLong>(font.type),static_cast<LegacyLong>(font.number),font.cache?(font.cache==cache?1:2):0,
            t->*count,t->*custom,t->*actions,static_cast<LegacyLong>(t->*flags),t->*parents!=nullptr,t->*sets!=nullptr,t->*costumes!=nullptr,
            g.material!=nullptr,g.action!=nullptr,g.action_id,g.action?static_cast<BASE *>(g.action)->CactRef():0};
        if (!record(fields,sizeof(fields)) || !record((t->*text).Psz(),(t->*text).Cch())) return false;
        if (t->*parents) for (int i=0;i<(t->*parents)->IvMac();++i) { short v; (t->*parents)->Get(i,&v); if (!record(&v,sizeof(v))) return false; }
        if (t->*sets) for (int i=0;i<(t->*sets)->IvMac();++i) { short v; (t->*sets)->Get(i,&v); if (!record(&v,sizeof(v))) return false; }
        return true;
    }
};
static bool TestTextResources()
{
    FILE *snapshot=nullptr; if (const char *path=std::getenv("THREE_MM_TEXT_RESOURCE_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    auto inject=[](int kind,int failure,bool enabled) {
#ifdef DEBUG
        vmutxMem.Enter(); auto a=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
        a->cactDo=failure-1; a->cactFail=enabled && failure?1:0; vmutxMem.Leave();
#else
        (void)kind; (void)failure; (void)enabled;
#endif
    };
    auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
    bool ok=cache!=nullptr; int attempts=0,successful=0;
    TAG oldTag={ksidUseCrf,cache,kctgTdf,17},newTag={123,reinterpret_cast<PChunkyResourceFile>(0x1357),kctgTdf,29};
    for (int text=0;ok && text<2;++text) for (int shape=0;ok && shape<2;++shape) for (int operation=0;ok && operation<3;++operation)
    for (int mask=0;ok && mask<(operation==1?8:1);++mask) for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<12;++failure)
    {
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        String source,changed; source.SetSzs(text?"Old\xe9":""); changed.SetSzs(text?"":"New"); int calls=0;
        auto t=operation?ThreeDText::PtdtNew(&source,shape?9:0,&oldTag):nullptr; PThreeDText copy=nullptr; PActionDefinition held=nullptr;
        if (t) { t->SetFontResolver(&calls,TextResourceNoFont); held=TextResourceFixtureAccess::Seed(t); ok=held!=nullptr; }
        inject(kind,failure,true); bool success=false;
        if (!operation) { t=ThreeDText::PtdtNew(&source,shape?9:0,&newTag); success=t!=nullptr; }
        else if (operation==1) success=t->FChange(mask&1?&changed:nullptr,mask&2?(shape?0:9):tdtsNil,mask&4?&newTag:nullptr);
        else { copy=t->PtdtDup(); success=copy!=nullptr; }
        inject(kind,failure,false); ++attempts; successful+=success;
        bool provider=copy && TextResourceFixtureAccess::Provider(copy,&calls);
        if (provider) { LegacyLong cels; ok=!copy->FGetCcelActn(tdaRest,&cels) && calls==1 && ok; }
        LegacyLong fields[]={text,shape,operation,mask,kind,failure,success,t!=nullptr,copy!=nullptr,held?held->CactRef():0,provider,calls,cache->CactRef()};
        ok=record(fields,sizeof(fields)) && TextResourceFixtureAccess::Record(t,snapshot,cache) && TextResourceFixtureAccess::Record(copy,snapshot,cache) && ok;
        ReleasePpo(&copy); ReleasePpo(&t); ReleasePpo(&held); vpers->Clear();
#ifdef DEBUG
        ok=vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv && ok;
#endif
        if (!ok) std::fprintf(stderr,"Text lifecycle failed: %d %d %d %d %d %d\n",text,shape,operation,mask,kind,failure);
    }
    for (int variant=0;ok && variant<6;++variant) for (int stage=0;ok && stage<3;++stage)
    for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<16;++failure)
    {
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        auto target=ChunkyFile::PcflCreateTemp(); auto crf=target?ChunkyResourceFile::PcrfNew(target,1024*1024):nullptr;
        String name; name.SetSzs(PszLit("wire")); auto authored=ThreeDText::PtdtNew(&name,6,&newTag); ChunkNumber number=0;
        ok=crf && authored && authored->FWrite(target,kctgTmpl,&number); ReleasePpo(&authored);
        ChildChunkIdentification child; DataBlock block;
        ok=ok && target->FGetKidChidCtg(kctgTmpl,number,0,kctgTdt,&child) && target->FFind(child.cki.ctg,child.cki.cno,&block);
        std::vector<byte> wire(variant==5?536:24); ok=ok && block.FReadRgb(wire.data(),24,0); block.Free();
        if (variant==1) { std::swap(wire[0],wire[1]); std::swap(wire[2],wire[3]); SwapBytesRglw(wire.data()+4,5); }
        if (variant==1 || variant==2 || variant==5) ok=target->FPutPv(wire.data(),variant==2?23:static_cast<LegacyLong>((wire.size)()),kctgTdt,child.cki.cno) && ok;
        if (variant==3) target->DeleteChild(kctgTmpl,number,kctgTdt,child.cki.cno,0);
        if (variant==4) { String empty; ok=target->FSetName(kctgTmpl,number,&empty) && ok; }
        if (variant==5) ok=target->FPackData(kctgTdt,child.cki.cno) && ok;
        PThreeDText t=stage==0?TextResourceFixtureAccess::Create():nullptr; PDynamicArray tags=nullptr;
        LegacyLong estimate=-77; bool error=true,success=false;
        if (stage==1) ok=target->FFind(kctgTmpl,number,&block) && Template::FReadTmpl(crf,kctgTmpl,number,&block,nullptr,&estimate) && ok;
        inject(kind,failure,true);
        if (!stage) success=static_cast<TextResourceFixtureAccess *>(t)->Initialize(target,number);
        else if (stage==1) { PBaseCacheableObject out=nullptr; success=Template::FReadTmpl(crf,kctgTmpl,number,&block,&out,&estimate); if (success) t=static_cast<PThreeDText>(out); }
        else { tags=ThreeDText::PgltagFetch(target,kctgTmpl,number,&error); success=!error; }
        inject(kind,failure,false); ++attempts; successful+=success;
        LegacyLong fields[]={variant,stage,kind,failure,success,error,estimate,t!=nullptr,tags?tags->IvMac():-1};
        ok=record(fields,sizeof(fields)) && TextResourceFixtureAccess::Record(t,snapshot,cache) && ok;
        if (tags) { TAG tag; tags->Get(0,&tag); LegacyLong values[]={tag.sid,static_cast<LegacyLong>(tag.ctg),static_cast<LegacyLong>(tag.cno),tag.pcrf!=nullptr}; ok=record(values,sizeof(values)) && ok; }
        ReleasePpo(&t); ReleasePpo(&tags); block.Free(); ReleasePpo(&crf); ReleasePpo(&target); vpers->Clear();
#ifdef DEBUG
        ok=vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv && ok;
#endif
        if (!ok) std::fprintf(stderr,"Text loading failed: %d %d %d %d\n",variant,stage,kind,failure);
    }
    for (int text=0;ok && text<2;++text) for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<16;++failure)
    {
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        String name; name.SetSzs(text?"Saved":""); auto t=ThreeDText::PtdtNew(&name,9,&newTag); auto target=ChunkyFile::PcflCreateTemp(); ChunkNumber number=0x13579;
        ok=t && target && ok; inject(kind,failure,true); bool success=t->FWrite(target,kctgTmpl,&number); inject(kind,failure,false); ++attempts; successful+=success;
        LegacyLong fields[]={text,kind,failure,success,static_cast<LegacyLong>(number),target->CckiCtg(kctgTmpl),target->CckiCtg(kctgTdt)}; ok=record(fields,sizeof(fields)) && ok;
        for (auto type:{kctgTmpl,kctgTdt}) for (int i=0;ok && i<target->CckiCtg(type);++i)
        {
            ChunkIdentification chunk; LegacyLong children; DataBlock b; ok=target->FGetCkiCtg(type,i,&chunk,&children,&b);
            String saved; bool named=target->FGetName(type,chunk.cno,&saved);
            LegacyLong values[]={static_cast<LegacyLong>(type),static_cast<LegacyLong>(chunk.cno),children,b.Cb(),named,saved.Cch()}; ok=record(values,sizeof(values)) && record(saved.Psz(),saved.Cch()) && ok;
            for (int j=0;ok && j<children;++j) { ChildChunkIdentification kid; ok=target->FGetKid(type,chunk.cno,j,&kid) && record(&kid,sizeof(kid)); }
            if (success) { std::vector<byte> bytes(b.Cb()); ok=b.FReadRgb(bytes.data(),b.Cb(),0) && record(bytes.data(),(bytes.size)()) && ok; }
        }
        ReleasePpo(&t); ReleasePpo(&target); vpers->Clear();
#ifdef DEBUG
        ok=vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv && ok;
#endif
        if (!ok) std::fprintf(stderr,"Text writing failed: %d %d %d\n",text,kind,failure);
    }
    String text; auto t=ThreeDText::PtdtNew(&text,0,&oldTag); text.SetSzs(PszLit("dirty"));
    ok=t && !t->FGetActnName(0,&text) && !text.Cch() && ok;
    auto names=StringTable_GST::PgstNew(4); LegacyLong ids[]={7,0,7}; const char *labels[]={"Pulse","Rest","Later duplicate"};
    for (int i=0;ok && i<3;++i) { text.SetSzs(const_cast<char *>(labels[i])); ok=names && names->FAddStn(&text,&ids[i]); }
    ok=ok && ThreeDText::FSetActionNames(names) && names->CactRef()==2;
    for (int id=0;ok && id<16;++id) { text.SetSzs(PszLit("dirty")); bool found=t->FGetActnName(id,&text); LegacyLong fields[]={id,found,text.Cch()}; ok=record(fields,sizeof(fields)) && record(text.Psz(),text.Cch()) && ok; }
    auto root=TextResourceFixtureAccess::DetachNames(); ReleasePpo(&root); ReleasePpo(&names); ReleasePpo(&t); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
    std::fprintf(stderr,"[text resources] attempts=%d successful=%d\n",attempts,successful);
#ifdef DEBUG
    ok=ok && successful==1843;
#else
    ok=ok && successful==2016;
#endif
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && attempts==2400 && successful;
}
#endif
