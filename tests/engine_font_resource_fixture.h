#ifndef ENGINE_FONT_RESOURCE_FIXTURE_H
#define ENGINE_FONT_RESOURCE_FIXTURE_H
class FontResourceFixtureAccess : public ThreeDFont
{
  public:
    static FontResourceFixtureAccess *Create() { return NewObj FontResourceFixtureAccess; }
    bool Initialize(PDataBlock block) { return _FInit(block); }
    static bool Record(PThreeDFont font,bool valid,FILE *out)
    {
        auto record=[&](const void *p,size_t n) { return !out || std::fwrite(p,1,n,out)==n; };
        if (!font) return true;
        auto field=&FontResourceFixtureAccess::_font; auto &state=font->*field;
        LegacyLong values[]={state.count,state.maximum_height,state.widths!=nullptr,state.heights!=nullptr};
        if (!record(values,sizeof(values))) return false;
        for (int i=0;i<state.count;++i)
        {
            if (state.widths) { BRS v; if (valid) v=font->DxrChar(i); else std::memcpy(&v,static_cast<byte *>(state.widths)+i*4,4); if (!record(&v,sizeof(v))) return false; }
            if (state.heights) { BRS v; if (valid) v=font->DyrChar(i); else std::memcpy(&v,static_cast<byte *>(state.heights)+i*4,4); if (!record(&v,sizeof(v))) return false; }
        }
        return true;
    }
};
static bool TestFontResources()
{
    FILE *snapshot=nullptr; if (const char *path=std::getenv("THREE_MM_FONT_RESOURCE_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    bool ok=true; int loaded=0,authored=0,attempts=0;
    const int counts[]={0,1,4,128};
    auto inject=[](int kind,int failure,bool enabled) {
#ifdef DEBUG
        vmutxMem.Enter(); auto a=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
        a->cactDo=failure-1; a->cactFail=enabled && failure?1:0; vmutxMem.Leave();
#else
        (void)kind; (void)failure; (void)enabled;
#endif
    };
    for (int shape=0;ok && shape<4;++shape) for (int variant=0;ok && variant<6;++variant)
    for (int stage=0;ok && stage<2;++stage) for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<8;++failure)
    {
        if (variant==5 && shape!=3) continue;
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        int n=counts[shape]; std::vector<byte> bytes(12+n*8+(variant==2?4:0));
        int16_t order=kboCur,system=koskCur; int32_t count=n+(variant==4?1:0); BRS maximum=BR_SCALAR(10);
        std::memcpy(bytes.data(),&order,2); std::memcpy(bytes.data()+2,&system,2); std::memcpy(bytes.data()+4,&count,4); std::memcpy(bytes.data()+8,&maximum,4);
        for (int i=0;i<n;++i) { BRS w=BR_SCALAR(i%4),h=BR_SCALAR(i%3+1); std::memcpy(bytes.data()+12+i*4,&w,4); std::memcpy(bytes.data()+12+n*4+i*4,&h,4); }
        if (variant==1) { std::swap(bytes[0],bytes[1]); std::swap(bytes[2],bytes[3]); SwapBytesRglw(bytes.data()+4,2+n*2); }
        DataBlock block; auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
        const LegacyLong size=variant==3?11:static_cast<LegacyLong>((bytes.size)());
        ok=cache && block.FSetTemp(size) && block.FWriteRgb(bytes.data(),size,0);
        if (variant==5) ok=block.FPackData() && ok;
        LegacyLong estimate=-77; ok=ok && ThreeDFont::FReadTdf(cache,kctgTdf,0,&block,nullptr,&estimate);
        PThreeDFont font=stage?nullptr:FontResourceFixtureAccess::Create(); bool initialized=false;
        inject(kind,failure,true);
        if (!stage) initialized=static_cast<FontResourceFixtureAccess *>(font)->Initialize(&block);
        else { PBaseCacheableObject out=nullptr; initialized=ThreeDFont::FReadTdf(cache,kctgTdf,0,&block,&out,&estimate); if (initialized) font=static_cast<PThreeDFont>(out); }
        inject(kind,failure,false); ++attempts; if (initialized) ++loaded;
        LegacyLong values[]={shape,variant,stage,kind,failure,estimate,initialized,font!=nullptr};
        ok=record(values,sizeof(values)) && FontResourceFixtureAccess::Record(font,initialized,snapshot) && ok;
        ReleasePpo(&font); block.Free(); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
#ifdef DEBUG
        ok=ok && vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv;
#endif
        if (!ok) std::fprintf(stderr,"Font load failed: %d %d %d %d %d\n",shape,variant,stage,kind,failure);
    }
    BRV vertices[3]={}; BRF face={}; face.vertices[1]=1; face.vertices[2]=2;
    auto space=Model::PmodlNew(3,vertices,1,&face); vertices[1].p.v[0]=BR_SCALAR(1); vertices[2].p.v[1]=BR_SCALAR(2);
    auto letter=Model::PmodlNew(3,vertices,1,&face); ok=ok && space && letter;
    for (int shape=0;ok && shape<3;++shape) for (int variant=0;ok && variant<2;++variant)
    for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<16;++failure)
    {
        if (!shape && variant) continue;
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
        auto kids=DynamicArray::PglNew(sizeof(ChildChunkIdentification)); ChunkNumber models[2]={};
        ok=cache && kids && file->FAdd(0,kctgBmdl,&models[0]) && space->FWrite(file,kctgBmdl,models[0]) && file->FAdd(0,kctgBmdl,&models[1]) && letter->FWrite(file,kctgBmdl,models[1]);
        if (shape) { ChildChunkIdentification child={{kctgBmdl,models[0]},32}; ok=kids->FAdd(&child) && ok;
            if (shape==1) { child={{kctgBmdl,models[1]},105}; ok=kids->FAdd(&child) && ok; }
            child={{kctgBmdl,variant?models[1]+1000:models[1]},65}; ok=kids->FAdd(&child) && ok; }
        String name; name.SetSzs(PszLit("Ignored font name")); ChunkIdentification created={};
        inject(kind,failure,true); bool written=ThreeDFont::FCreate(cache,kids,&name,&created); inject(kind,failure,false); ++attempts; if (written) ++authored;
        LegacyLong values[]={shape,variant,kind,failure,written,static_cast<LegacyLong>(created.ctg),static_cast<LegacyLong>(created.cno),file->CckiCtg(kctgTdf),file->CckiRef(kctgBmdl,models[0]),file->CckiRef(kctgBmdl,models[1])};
        ok=record(values,sizeof(values)) && ok;
        for (int i=0;ok && i<file->CckiCtg(kctgTdf);++i)
        {
            ChunkIdentification chunk; DataBlock b; LegacyLong children; ok=file->FGetCkiCtg(kctgTdf,i,&chunk,&children,&b);
            LegacyLong fields[]={static_cast<LegacyLong>(chunk.ctg),static_cast<LegacyLong>(chunk.cno),children,b.Cb()}; ok=record(fields,sizeof(fields)) && ok;
            for (int j=0;ok && j<children;++j) { ChildChunkIdentification child; ok=file->FGetKid(chunk.ctg,chunk.cno,j,&child) && record(&child,sizeof(child)); }
            if (written) { std::vector<byte> wire(b.Cb()); ok=b.FReadRgb(wire.data(),b.Cb(),0) && record(wire.data(),(wire.size)()) && ok;
                String ignored; LegacyLong named=file->FGetName(chunk.ctg,chunk.cno,&ignored); ok=!named && record(&named,sizeof(named)) && ok;
                // Additional converted-path checks, outside the frozen trace:
                // consume the authored font and resolve its actual glyph child.
                auto font=static_cast<PThreeDFont>(cache->PbacoFetch(chunk.ctg,chunk.cno,ThreeDFont::FReadTdf)); ok=font && ok;
                if (font && shape) { ok=font->DxrChar(65)==letter->Dxr() && font->DyrChar(65)==letter->Dyr()+BR_SCALAR(0.5) &&
                        font->DxrChar(32)==(shape==1?letter->Dxr():0) && font->DyrMax()==letter->Dyr()+BR_SCALAR(0.5) && ok;
                    auto glyph=font->PmodlFetch(65); ok=glyph && ok; ReleasePpo(&glyph); }
                if (font && !shape) ok=font->DxrChar(0)==0 && font->DyrChar(0)==0 && font->DyrMax()==0 && ok;
                ReleasePpo(&font); }
        }
        // The C author releases its fetched model even if adoption fails.
        for (auto number:models) { auto model=static_cast<PModel>(cache->PbacoFetch(kctgBmdl,number,Model::FReadModl)); ok=model && model->CactRef()==1 && ok; ReleasePpo(&model); }
        ReleasePpo(&kids); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
#ifdef DEBUG
        ok=ok && vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv;
#endif
        if (!ok) std::fprintf(stderr,"Font authoring failed: %d %d %d %d\n",shape,variant,kind,failure);
    }
    ReleasePpo(&space); ReleasePpo(&letter);
    std::fprintf(stderr,"[font resources] attempts=%d loaded=%d authored=%d\n",attempts,loaded,authored);
#ifdef DEBUG
    ok=ok && loaded==385 && authored==130;
#else
    ok=ok && loaded==432 && authored==144;
#endif
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && attempts==1248 && loaded && authored;
}
#endif
