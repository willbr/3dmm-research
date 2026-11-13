#ifndef ENGINE_ACTION_DEFINITION_FIXTURE_H
#define ENGINE_ACTION_DEFINITION_FIXTURE_H
class ActionDefinitionFixtureAccess : public ActionDefinition
{
  public:
    static ActionDefinitionFixtureAccess *Create() { return NewObj ActionDefinitionFixtureAccess; }
    bool Initialize(PChunkyFile file,ChunkNumber number) { return _FInit(file,kctgActn,number); }
    static bool CheckNoSoundMarkers(PActionDefinition a)
    {
        auto field=&ActionDefinitionFixtureAccess::_action; auto group=static_cast<PGeneralGroup>((a->*field).cels);
        AnimationCel cel; group->GetFixed(0,&cel); auto original=cel.chidSnd; bool ok=true;
        const ChildChunkID markers[]={0xffffffff,0x80000000};
        for (auto marker:markers) { cel.chidSnd=marker; group->PutFixed(0,&cel); TAG tag={123,reinterpret_cast<PChunkyResourceFile>(0x1357),kctgMsnd,456}; a->GetSnd(0,&tag);
            ok=ok && tag.sid==123 && tag.pcrf==reinterpret_cast<PChunkyResourceFile>(0x1357) && tag.ctg==ctgNil && tag.cno==456; }
        cel.chidSnd=original; group->PutFixed(0,&cel); return ok;
    }
    static bool Record(PActionDefinition a,bool valid,FILE *out)
    {
        auto record=[&](const void *p,size_t n) { return !out || std::fwrite(p,1,n,out)==n; };
        if (!a) return true;
        auto field=&ActionDefinitionFixtureAccess::_action; auto &state=a->*field;
        auto cels=static_cast<PGeneralGroup>(state.cels); auto matrices=static_cast<PDynamicArray>(state.matrices); auto sounds=static_cast<PDynamicArray>(state.sounds);
        LegacyLong fields[]={static_cast<LegacyLong>(a->Grfactn()),cels!=nullptr,cels?cels->IvMac():0,matrices!=nullptr,matrices?matrices->IvMac():0,sounds!=nullptr,sounds?sounds->IvMac():0};
        if (!record(fields,sizeof(fields))) return false;
        for (int i=0;cels && i<cels->IvMac();++i)
        {
            AnimationCel cel; if (valid) a->GetCel(i,&cel); else cels->GetFixed(i,&cel);
            LegacyLong n=cels->Cb(i)/sizeof(CelPartSpec);
            if (!record(&cel,sizeof(cel)) || !record(&n,sizeof(n))) return false;
            for (int j=0;j<n;++j) { CelPartSpec part; if (valid) a->GetCps(i,j,&part); else part=static_cast<CelPartSpec *>(cels->QvGet(i))[j]; if (!record(&part,sizeof(part))) return false; }
            if (valid) { TAG tag={123,nullptr,0x12345678,456}; a->GetSnd(i,&tag); LegacyLong values[]={tag.sid,tag.pcrf!=nullptr,static_cast<LegacyLong>(tag.ctg),static_cast<LegacyLong>(tag.cno)}; if (!record(values,sizeof(values))) return false; }
        }
        for (int i=0;matrices && i<matrices->IvMac();++i) { BMAT34 m; if (valid) a->GetMatrix(i,&m); else matrices->Get(i,&m); if (!record(&m,sizeof(m))) return false; }
        for (int i=0;sounds && i<sounds->IvMac();++i) { TAG tag; sounds->Get(i,&tag); LegacyLong values[]={tag.sid,tag.pcrf!=nullptr,static_cast<LegacyLong>(tag.ctg),static_cast<LegacyLong>(tag.cno)}; if (!record(values,sizeof(values))) return false; }
        return true;
    }
};
static bool TestActionDefinition()
{
    FILE *snapshot=nullptr; if (const char *path=std::getenv("THREE_MM_ACTION_DEFINITION_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    bool ok=true; int attempts=0,loaded=0,copied=0;
    const int counts[]={0,1,3};
    for (int shape=0;ok && shape<3;++shape) for (int variant=0;ok && variant<8;++variant)
    for (int stage=0;ok && stage<3;++stage) for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<18;++failure)
    {
        if ((stage==0 && (variant==1 || variant==2 || variant==6)) || (stage==2 && variant)) continue;
#ifdef DEBUG
        const LegacyLong beforeBase=vdmglob.dmaglBase.cv,beforeHq=vdmglob.dmaglHq.cv,beforePv=vdmglob.dmaglPv.cv;
#endif
        auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
        auto cels=GeneralGroup::PggNew(sizeof(AnimationCel)); auto matrices=DynamicArray::PglNew(sizeof(BMAT34)); auto sounds=DynamicArray::PglNew(sizeof(TAGOnFile));
        ok=cache && cels && matrices && sounds;
        int n=counts[shape],parts=shape?4:0;
        for (int i=0;ok && i<n;++i)
        {
            AnimationCel cel={static_cast<ChildChunkID>(i%2),BR_SCALAR(i+1)}; CelPartSpec row[4]={};
            for (int j=0;j<parts;++j) { row[j].chidModl=j%2?-1:static_cast<short>(i+j); row[j].imat34=static_cast<short>(i); }
            BMAT34 matrix; BrMatrix34Identity(&matrix); matrix.m[3][1]=BR_SCALAR(i-3);
            ok=cels->FAdd(parts*sizeof(CelPartSpec),nullptr,row,&cel) && matrices->FAdd(&matrix);
        }
        for (int i=0;ok && i<2;++i) { TAGOnFile tag={i?0:123,0xfedcba98,kctgMsnd,static_cast<uint32_t>(100+i)}; ok=sounds->FAdd(&tag); }
        const bool other=variant==2 && n; ActionChunkOnFile header={kboCur,koskCur,0x8000000d};
        if (variant==2) SwapBytesBom(&header,kbomActnf);
        ChunkNumber number=0; ok=ok && file->FAddPv(&header,variant==3?7:sizeof(header),kctgActn,&number);
        if (other) { for (int i=0;i<n;++i) { SwapBytesRglw(cels->QvFixedGet(i),2); SwapBytesRgsw(cels->QvGet(i),parts*2); } SwapBytesRglw(matrices->QvGet(0),n*12); SwapBytesRglw(sounds->QvGet(0),8); }
        auto table=[&](ChunkTagOrType type,auto *data) { ChunkNumber child; DataBlock block; return file->FAddChild(kctgActn,number,0,data->CbOnFile(),type,&child,&block) && data->FWrite(&block,other?kboOther:kboCur); };
        if (ok && variant!=4) ok=table(kctgGgcl,cels);
        if (ok && variant!=5) ok=table(kctgGlxf,matrices);
        if (ok && (variant==1 || variant==2)) ok=table(kctgGlms,sounds);
        if (ok && variant==6) { byte invalid=0; ChunkNumber child; ok=file->FAddChildPv(kctgActn,number,0,&invalid,1,kctgGlms,&child); }
        if (other) { for (int i=0;i<n;++i) { SwapBytesRglw(cels->QvFixedGet(i),2); SwapBytesRgsw(cels->QvGet(i),parts*2); } SwapBytesRglw(matrices->QvGet(0),n*12); SwapBytesRglw(sounds->QvGet(0),8); }
        DataBlock block; LegacyLong estimate=-77; ok=ok && file->FFind(kctgActn,number,&block) && ActionDefinition::FReadActn(cache,kctgActn,number,&block,nullptr,&estimate);
        if (variant==7) { file->Delete(kctgActn,number); }
        PActionDefinition action=stage==1?ActionDefinitionFixtureAccess::Create():nullptr; bool initialized=false;
#ifdef DEBUG
        vmutxMem.Enter(); auto allocator=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
        allocator->cactDo=failure-1; allocator->cactFail=failure?1:0; vmutxMem.Leave();
#endif
        if (stage==0) { PBaseCacheableObject out=nullptr; initialized=ActionDefinition::FReadActn(cache,kctgActn,number,&block,&out,&estimate); if (initialized) action=static_cast<PActionDefinition>(out); }
        else if (stage==1) initialized=static_cast<ActionDefinitionFixtureAccess *>(action)->Initialize(file,number);
        else { action=ActionDefinition::PactnNew(cels,matrices,0x8000000d); initialized=action!=nullptr; }
#ifdef DEBUG
        vmutxMem.Enter(); allocator->cactFail=0; vmutxMem.Leave();
#endif
        ++attempts; if (initialized) { if (stage==2) ++copied; else ++loaded; }
        LegacyLong values[]={shape,variant,stage,kind,failure,initialized,action!=nullptr,estimate,cels->CactRef(),matrices->CactRef()};
        ok=record(values,sizeof(values)) && ActionDefinitionFixtureAccess::Record(action,initialized,snapshot) && ok;
        // The new C owner releases optional and partially filled sound lists.
        // Cleanup is outside the state trace; allocation balance verifies it.
        if (initialized && stage==1 && n && (variant==1 || variant==2))
            ok=ActionDefinitionFixtureAccess::CheckNoSoundMarkers(action) && ok;
        block.Free(); ReleasePpo(&action); ReleasePpo(&cels); ReleasePpo(&matrices); ReleasePpo(&sounds); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
#ifdef DEBUG
        ok=ok && vdmglob.dmaglBase.cv==beforeBase && vdmglob.dmaglHq.cv==beforeHq && vdmglob.dmaglPv.cv==beforePv;
#endif
        if (!ok) std::fprintf(stderr,"Action definition failed: %d %d %d %d %d\n",shape,variant,stage,kind,failure);
    }
    std::fprintf(stderr,"[action definition] attempts=%d loaded=%d copied=%d\n",attempts,loaded,copied);
#ifdef DEBUG
    ok=ok && loaded==597 && copied==152;
#else
    ok=ok && loaded==648 && copied==162;
#endif
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && attempts==2268 && loaded && copied;
}
#endif
