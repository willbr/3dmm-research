#ifndef ENGINE_SOUND_RESOURCE_FIXTURE_H
#define ENGINE_SOUND_RESOURCE_FIXTURE_H
#include "three_mm/codec.h"
class SoundResourceFixtureAccess : public MovieSoundMSND
{
  public:
    static SoundResourceFixtureAccess *Create()
    {
        auto o=NewObj SoundResourceFixtureAccess;
        if (o) { o->_resource={0x1122,0x3344,nullptr,1,321}; o->_silent=tMaybe; o->_invalid=false; o->_stn.SetSzs(PszLit("old")); }
        return o;
    }
    bool Initialize(PChunkyFile file,ChunkNumber n) { return _FInit(file,kctgMsnd,n); }
    static void SetPlayback(PMovieSoundMSND sound,int style,bool silent,bool invalid,ChunkNumber number)
    {
        auto resource=&SoundResourceFixtureAccess::_resource; auto s=&SoundResourceFixtureAccess::_silent; auto i=&SoundResourceFixtureAccess::_invalid;
        sound->*resource={static_cast<ThreeMMU32>(style==styMidi?kctgMidi:kctgWave),number,nullptr,style,65536}; sound->*s=silent; sound->*i=invalid;
    }
    static void ResetMidi(PMovieSoundMSND sound)
    {
        SetPlayback(sound,styMidi,true,false,0); auto field=&SoundResourceFixtureAccess::_resource; (sound->*field).type=0;
        sound->Play(0,false,false,0,0,false,0);
    }
    static bool Record(PMovieSoundMSND o,FILE *out,PChunkyResourceFile cache)
    {
        if (!o) return true;
        auto record=[&](const void *p,size_t n) { return !out || std::fwrite(p,1,n,out)==n; };
        auto field=&SoundResourceFixtureAccess::_resource; auto &r=o->*field;
        auto silent=&SoundResourceFixtureAccess::_silent; auto invalid=&SoundResourceFixtureAccess::_invalid; auto name=&SoundResourceFixtureAccess::_stn;
        LegacyLong values[]={static_cast<LegacyLong>(r.type),static_cast<LegacyLong>(r.number),r.cache?(r.cache==cache?1:2):0,r.style,r.volume,o->*silent,o->*invalid,(o->*name).Cch()};
        return record(values,sizeof(values)) && record((o->*name).Psz(),(o->*name).Cch());
    }
};
class SoundResourceTestManager : public SoundManager
{
  public:
    int stops=0,plays=0,queries=0; bool playing=false;
    LegacyLong queue=0,volume=0,repeat=0,priority=0,soundClass=0,instance=0; LegacyUlong start=0;
    ChunkTagOrType type=0; ChunkNumber number=0;
    static SoundResourceTestManager *Create()
    { auto o=NewObj SoundResourceTestManager; if (o && !o->_FInit()) ReleasePpo(&o); return o; }
    void Reset() { stops=plays=queries=0; }
    void StopAll(LegacyLong sqn,LegacyLong scl) override { ++stops; queue=sqn; Assert(scl==sclNil,"unexpected stop class"); }
    bool FPlaying(LegacyLong sii) override { ++queries; instance=sii; return playing; }
    LegacyLong SiiPlay(PResourceCache cache,ChunkTagOrType ctg,ChunkNumber cno,LegacyLong sqn,LegacyLong vlm,LegacyLong count,LegacyUlong at,LegacyLong spr,LegacyLong scl) override
    { Assert(cache==nullptr,"unexpected cache"); ++plays; type=ctg; number=cno; queue=sqn; volume=vlm; repeat=count; start=at; priority=spr; soundClass=scl; return 9; }
};
static bool TestSoundResourceDispatch()
{
    auto manager=SoundResourceTestManager::Create(); auto sound=SoundResourceFixtureAccess::Create(); auto previous=vpsndm;
    bool ok=manager && sound; int cases=0; if (ok) vpsndm=manager;
    for (int style=stySfx;ok && style<=styMidi;++style) for (int loop=0;ok && loop<2;++loop) for (int queue=0;ok && queue<2;++queue)
    for (int silent=0;ok && silent<2;++silent) for (int invalid=0;ok && invalid<2;++invalid) for (int actor=0;ok && actor<2;++actor)
    {
        auto number=static_cast<ChunkNumber>(100+cases++); manager->Reset();
        SoundResourceFixtureAccess::SetPlayback(sound,style,silent!=0,invalid!=0,number);
        sound->Play(-65537,loop!=0,queue!=0,123,6,actor!=0,0xffffffffUL);
        ok=manager->stops==(!invalid && (!queue || silent)) && manager->plays==(!invalid && !silent) && !manager->queries && sound->FValid()==!invalid;
        if (manager->stops || manager->plays) ok=manager->queue==SoundPlaybackQueue::QueueNumber(style,-65537,actor!=0) && ok;
        if (manager->plays) ok=manager->type==(style==styMidi?kctgMidi:kctgWave) && manager->number==number && manager->volume==123 && manager->repeat==(loop?klwMax:1) &&
            manager->start==0xffffffffUL && manager->priority==6 && manager->soundClass==(loop?(style==styMidi?sclLoopMidi:sclLoopWav):sclNonLoop) && ok;
        ok=sound->Scl(loop!=0)==(loop?(style==styMidi?sclLoopMidi:sclLoopWav):sclNonLoop) && ok;
        for (auto tool:{toolSounder,toolLooper,toolMatcher}) if (style!=styMidi || tool!=toolMatcher)
        { const int expected=style==styMidi?1:style==stySpeech?(tool==toolSounder?1:tool==toolMatcher?5:3):(tool==toolSounder?2:tool==toolMatcher?6:4); ok=sound->Spr(tool)==expected && ok; }
    }
    if (ok)
    {
        SoundResourceFixtureAccess::SetPlayback(sound,styMidi,false,false,9999); manager->playing=false;
        sound->Play(1,true,false,123,6,true,0); manager->Reset(); manager->playing=true;
        sound->Play(1,true,false,123,6,true,0); ok=manager->queries==1 && manager->instance==9 && !manager->stops && !manager->plays;
        manager->Reset(); manager->playing=false; sound->Play(1,true,false,123,6,true,0); ok=manager->queries==1 && manager->stops==1 && manager->plays==1 && ok;
    }
    if (manager && sound)
    {
        // Restore the initially empty process-wide MIDI suppression state.
        SoundResourceFixtureAccess::ResetMidi(sound);
    }
    vpsndm=previous; ReleasePpo(&sound); ReleasePpo(&manager);
    std::fprintf(stderr,"[sound dispatch] cases=%d\n",cases); return ok && cases==96;
}
static bool TestSoundResources()
{
    FILE *snapshot=nullptr; if (const char *path=std::getenv("THREE_MM_SOUND_RESOURCE_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    auto inject=[](int kind,int failure,bool enabled) {
#ifdef DEBUG
        vmutxMem.Enter(); auto a=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
        a->cactDo=failure-1; a->cactFail=enabled && failure?1:0; vmutxMem.Leave();
#else
        (void)kind; (void)failure; (void)enabled;
#endif
    };
    auto graph=[&](PChunkyFile file,bool payload) {
        for (auto type:{kctgMsnd,kctgMidi,kctgWave})
        {
            LegacyLong count=file->CckiCtg(type); if (!record(&count,sizeof(count))) return false;
            for (int i=0;i<count;++i)
            {
                ChunkIdentification chunk; DataBlock b; LegacyLong children;
                if (!file->FGetCkiCtg(type,i,&chunk,&children,nullptr) || !file->FFind(type,chunk.cno,&b)) return false;
                String name; bool named=file->FGetName(type,chunk.cno,&name);
                LegacyLong fields[]={static_cast<LegacyLong>(type),static_cast<LegacyLong>(chunk.cno),children,b.Cb(),named,name.Cch()};
                if (!record(fields,sizeof(fields)) || !record(name.Psz(),name.Cch())) return false;
                for (int j=0;j<children;++j) { ChildChunkIdentification child; if (!file->FGetKid(type,chunk.cno,j,&child) || !record(&child,sizeof(child))) return false; }
                if (payload) { std::vector<byte> bytes(b.Cb()); if (!b.FReadRgb(bytes.data(),b.Cb(),0)) return false;
                    // The final three MSND bytes are undefined native struct padding.
                    if (!record(bytes.data(),type==kctgMsnd?13:(bytes.size)())) return false; }
            }
        }
        return true;
    };
    bool ok=true; int attempts=0,loaded=0,written=0,copied=0,invalidated=0;
    for (int variant=0;ok && variant<8;++variant) for (int stage=0;ok && stage<3;++stage)
    for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<12;++failure)
    {
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
        MovieSoundFile header={kboCur,koskCur,variant==1?0:stySfx,0x12345678,variant==2};
        if (variant==1) { SwapBytesBom(&header,kbomMsndf); header.fInvalid=false; }
        byte bytes[17]={}; std::memcpy(bytes,&header,sizeof(header)); ChunkNumber number=0,child=0;
        ok=cache && file->FAddPv(bytes,variant==5?17:16,kctgMsnd,&number);
        String name; name.SetSzs(PszLit("Sound fixture")); if (ok && variant!=6) ok=file->FSetName(kctgMsnd,number,&name);
        if (ok && variant!=3) ok=file->FAddChildPv(kctgMsnd,number,0,bytes,3,kctgWave,&child);
        if (ok && variant==7) { byte packed[128]={}; ThreeMMI32 count=0;
            ok=three_mm_codec_encode(THREE_MM_CODEC_KCDC,bytes,16,packed+8,120,&count,nullptr)==THREE_MM_DATA_OK;
            std::memcpy(packed,"KCDC\0\0\0\20",8); ok=file->FPutPv(packed,count+8,kctgMsnd,number) && ok; file->SetPacked(kctgMsnd,number,true); }
        DataBlock block; ok=file->FFind(kctgMsnd,number,&block) && ok;
        if (variant==4) file->Delete(kctgMsnd,number);
        LegacyLong estimate=-77,style=-77,volume=-88; bool invalid=true,success=false;
        PMovieSoundMSND sound=stage==0?SoundResourceFixtureAccess::Create():nullptr;
        if (stage==1) ok=MovieSoundMSND::FReadMsnd(cache,kctgMsnd,number,&block,nullptr,&estimate) && ok;
        inject(kind,failure,true);
        if (!stage) success=static_cast<SoundResourceFixtureAccess *>(sound)->Initialize(file,number);
        else if (stage==1) { PBaseCacheableObject out=nullptr; success=MovieSoundMSND::FReadMsnd(cache,kctgMsnd,number,&block,&out,&estimate); if (success) sound=static_cast<PMovieSoundMSND>(out); }
        else success=MovieSoundMSND::FGetMsndInfo(file,kctgMsnd,number,failure&1?&invalid:nullptr,failure&2?&style:nullptr,failure&4?&volume:nullptr);
        inject(kind,failure,false); ++attempts; loaded+=success;
        LegacyLong fields[]={variant,stage,kind,failure,success,estimate,invalid,style,volume,sound!=nullptr,cache->CactRef()};
        ok=record(fields,sizeof(fields)) && SoundResourceFixtureAccess::Record(sound,snapshot,cache) && ok;
        ReleasePpo(&sound); block.Free(); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
#ifdef DEBUG
        ok=vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv && ok;
#endif
        if (!ok) std::fprintf(stderr,"Sound loading failed: %d %d %d %d\n",variant,stage,kind,failure);
    }
    const byte midiBytes[]={'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k',0,0,0,12,0,0x90,60,64,96,0x80,60,0,0,0xff,0x2f,0};
    auto source=FileObject::PfilCreateTemp(); Filename filename; if (source) source->GetFni(&filename);
    ok=source && source->FWriteRgb(const_cast<byte *>(midiBytes),sizeof(midiBytes),0) && ok;
    auto midi=ok?MidiStream::PmidsReadNative(&filename):nullptr; ok=midi && ok;
    for (int kindOfSound=0;ok && kindOfSound<2;++kindOfSound) for (int named=0;ok && named<2;++named)
    for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<16;++failure)
    {
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        auto target=ChunkyFile::PcflCreateTemp(); String name; if (named) name.SetSzs(PszLit("Saved sound")); ChunkNumber number=0x13579;
        inject(kind,failure,true); bool success=kindOfSound?MovieSoundMSND::FWriteWave(source,target,stySpeech,&name,&number):MovieSoundMSND::FWriteMidi(target,midi,&name,&number); inject(kind,failure,false);
        ++attempts; written+=success; LegacyLong fields[]={kindOfSound,named,kind,failure,success,static_cast<LegacyLong>(number)};
        ok=record(fields,sizeof(fields)) && graph(target,success) && ok; ReleasePpo(&target); vpers->Clear();
#ifdef DEBUG
        ok=vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv && ok;
#endif
        if (!ok) std::fprintf(stderr,"Sound writing failed: %d %d %d %d\n",kindOfSound,named,kind,failure);
    }
    for (int malformed=0;ok && malformed<2;++malformed) for (int named=0;ok && named<2;++named)
    for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<16;++failure)
    {
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        ok=source->FSetFpMac(malformed?3:sizeof(midiBytes)) && source->FWriteRgb(const_cast<byte *>(midiBytes),malformed?3:sizeof(midiBytes),0) && ok;
        auto target=ChunkyFile::PcflCreateTemp(); String name,expected; name.SetSzs(PszLit("Imported sound")); if (named) expected=name; else filename.GetLeaf(&expected);
        ChunkNumber number=0x13579; inject(kind,failure,true); bool success=MovieSoundMSND::FCopyMidi(source,target,&number,named?&name:nullptr); inject(kind,failure,false);
        ++attempts; copied+=success; String actual; bool rightName=success && target->FGetName(kctgMsnd,number,&actual) && actual.FEqual(&expected);
        LegacyLong fields[]={malformed,named,kind,failure,success,static_cast<LegacyLong>(number),rightName,target->CckiCtg(kctgMsnd),target->CckiCtg(kctgMidi)};
        ok=record(fields,sizeof(fields)) && (!success || rightName) && ok; ReleasePpo(&target); vpers->Clear();
#ifdef DEBUG
        ok=vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv && ok;
#endif
        if (!ok) std::fprintf(stderr,"Sound copying failed: %d %d %d %d\n",malformed,named,kind,failure);
    }
    ReleasePpo(&midi); ReleasePpo(&source);
    for (int childPresent=0;ok && childPresent<2;++childPresent) for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<12;++failure)
    {
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr;
        MovieSoundFile header={kboCur,koskCur,stySfx,65536,false}; ChunkNumber number=0,child=0;
        String name; name.SetSzs(PszLit("Invalidate")); ok=cache && file->FAddPv(&header,sizeof(header),kctgMsnd,&number) && file->FSetName(kctgMsnd,number,&name);
        if (childPresent) ok=file->FAddChildPv(kctgMsnd,number,0,&header,3,kctgWave,&child) && ok;
        auto sound=ok?static_cast<PMovieSoundMSND>(cache->PbacoFetch(kctgMsnd,number,MovieSoundMSND::FReadMsnd)):nullptr; ok=sound && ok;
        inject(kind,failure,true); bool success=sound->FInvalidate(); inject(kind,failure,false); ++attempts; invalidated+=success;
        LegacyLong fields[]={childPresent,kind,failure,success}; ok=record(fields,sizeof(fields)) && SoundResourceFixtureAccess::Record(sound,snapshot,cache) && graph(file,true) && ok;
        ReleasePpo(&sound); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
#ifdef DEBUG
        ok=vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv && ok;
#endif
        if (!ok) std::fprintf(stderr,"Sound invalidation failed: %d %d %d\n",childPresent,kind,failure);
    }
    // The converted reader rejects a short envelope without the native read assertion.
    auto shortFile=ChunkyFile::PcflCreateTemp(); auto shortSound=SoundResourceFixtureAccess::Create();
    byte shortHeader[15]={}; ChunkNumber shortNumber=0;
    ok=shortFile && shortSound && shortFile->FAddPv(shortHeader,15,kctgMsnd,&shortNumber) && !shortSound->Initialize(shortFile,shortNumber) && ok;
    ReleasePpo(&shortSound); ReleasePpo(&shortFile); vpers->Clear();
    std::fprintf(stderr,"[sound resources] attempts=%d loaded=%d written=%d copied=%d invalidated=%d\n",attempts,loaded,written,copied,invalidated);
#ifdef DEBUG
    ok=ok && loaded==524 && written==192 && copied==84 && invalidated==36;
#else
    ok=ok && loaded==540 && written==192 && copied==96 && invalidated==36;
#endif
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && attempts==1320 && loaded && written && copied && invalidated;
}
#endif
