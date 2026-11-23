#ifndef ENGINE_ACTION_DEFINITION_FIXTURE_H
#define ENGINE_ACTION_DEFINITION_FIXTURE_H
#include "engine_action_tables_fixture.h"
class ActionDefinitionFixtureAccess
{
  public:
    static bool Initialize(MMActionDefinition &definition, PChunkyResourceFile cache, ChunkNumber number)
    {
        const auto &host=*mm_kauai_action_host();
        const unsigned short one=1;
        MMActionDefinitionServices s={};
        s.host_big_endian=*reinterpret_cast<const unsigned char *>(&one)==0;
        s.table_allocator=*host.tables;
        s.decode=host.decode;
        s.bad_order=host.bad_order;
        return mm_action_initialize(&definition,cache->CoreCache(),kctgActn,number,nullptr,&s);
    }
    static void GetSound(const MMActionDefinition *action, int cel, TAG *out)
    {
        MMTag tag;
        out->ctg=ctgNil;
        if (mm_action_cel_sound(action,cel,&tag)) TagFromCore(out,tag);
    }
    static bool CheckNoSoundMarkers(const MMActionDefinition *action)
    {
        auto table=action->cels;
        MMGroup group={&table->storage,MM_ACTION_CEL_BYTES,table->header.count,table->header.first_bytes,-1,0};
        MMGroupLocation location;
        if (mm_group_location(&group,0,&location)!=MM_STORAGE_OK) return false;
        auto row=table->storage.buffers[0].data+location.offset;
        const unsigned short one=1;
        const bool other=table->header.big_endian!=(*reinterpret_cast<const unsigned char *>(&one)==0);
        AnimationCel cel;
        if (!mm_action_get_cel(action,0,&cel)) return false;
        const auto original=cel.chidSnd;
        bool ok=true;
        const ChildChunkID markers[]={0xffffffff,0x80000000};
        for (auto marker:markers) { cel.chidSnd=marker; mm_action_decode_cel(&cel,other,row); TAG tag={123,reinterpret_cast<MMResourceCache *>(0x1357),kctgMsnd,456}; GetSound(action,0,&tag);
            ok=ok && tag.sid==123 && tag.cache==reinterpret_cast<MMResourceCache *>(0x1357) && tag.ctg==ctgNil && tag.cno==456; }
        cel.chidSnd=original; mm_action_decode_cel(&cel,other,row); return ok;
    }
    static bool Record(const MMActionDefinition *action,bool valid,FILE *out)
    {
        auto record=[&](const void *p,size_t n) { return !out || std::fwrite(p,1,n,out)==n; };
        if (!action) return true;
        const auto &state=*action;
        const auto matrices=state.matrices,sounds=state.sounds;
        const auto celCount=mm_action_cel_count(&state);
        const auto matrixCount=matrices?static_cast<LegacyLong>(matrices->header.count):0;
        const auto soundCount=sounds?static_cast<LegacyLong>(sounds->header.count):0;
        LegacyLong fields[]={static_cast<LegacyLong>(state.flags),state.cels!=nullptr,celCount,matrices!=nullptr,matrixCount,sounds!=nullptr,soundCount};
        if (!record(fields,sizeof(fields))) return false;
        for (int i=0;i<celCount;++i)
        {
            AnimationCel cel; if (!mm_action_get_cel(action,i,&cel)) return false;
            LegacyLong n=mm_action_part_count(&state,i);
            if (!record(&cel,sizeof(cel)) || !record(&n,sizeof(n))) return false;
            for (int j=0;j<n;++j) { CelPartSpec part; if (!mm_action_get_part(action,i,j,&part) || !record(&part,sizeof(part))) return false; }
            if (valid) { TAG tag={123,nullptr,0x12345678,456}; GetSound(action,i,&tag); LegacyLong values[]={tag.sid,tag.cache!=nullptr,static_cast<LegacyLong>(tag.ctg),static_cast<LegacyLong>(tag.cno)}; if (!record(values,sizeof(values))) return false; }
        }
        for (int i=0;i<matrixCount;++i) { BMAT34 m; if (!mm_action_get_matrix(action,i,&m) || !record(&m,sizeof(m))) return false; }
        for (int i=0;i<soundCount;++i) { MMTag tag; if (!mm_action_sound_tag(sounds,i,&tag)) return false; LegacyLong values[]={tag.sid,tag.cache!=nullptr,static_cast<LegacyLong>(tag.type),static_cast<LegacyLong>(tag.number)}; if (!record(values,sizeof(values))) return false; }
        return true;
    }
};
static bool TestActionDefinition()
{
    FILE *snapshot=nullptr; if (const char *path=std::getenv("MM_ACTION_DEFINITION_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    bool ok=true; int attempts=0,loaded=0,adopted=0;
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
        DataBlock block; MMI32 estimate=-77; ok=ok && file->FFind(kctgActn,number,&block) && mm_brender_action_read(cache->CoreCache(),kctgActn,number,block.CoreBlock(),nullptr,&estimate);
        if (variant==7) { file->Delete(kctgActn,number); }
        MMActionDefinition direct={};
        MMActionResource *action=nullptr;
        bool initialized=false;
        FixtureActionTables tables;
        if (stage==2) ok=ok && tables.Copy(cels,matrices);
#ifdef DEBUG
        vmutxMem.Enter(); auto allocator=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
        allocator->cactDo=failure-1; allocator->cactFail=failure?1:0; vmutxMem.Leave();
#endif
        if (stage==0) { void *out=nullptr; initialized=mm_brender_action_read(cache->CoreCache(),kctgActn,number,block.CoreBlock(),&out,&estimate); if (initialized) action=static_cast<MMActionResource *>(out); }
        else if (stage==1) initialized=ActionDefinitionFixtureAccess::Initialize(direct,cache,number);
        else if (ok) { action=tables.Create(0x8000000d); initialized=action!=nullptr; }
#ifdef DEBUG
        vmutxMem.Enter(); allocator->cactFail=0; vmutxMem.Leave();
#endif
        ++attempts; if (initialized) { if (stage==2) ++adopted; else ++loaded; }
        const auto definition=stage==1?&direct:action?mm_action_resource_definition(action):nullptr;
        LegacyLong values[]={shape,variant,stage,kind,failure,initialized,definition!=nullptr,estimate,cels->CactRef(),matrices->CactRef()};
        ok=record(values,sizeof(values)) && ActionDefinitionFixtureAccess::Record(definition,initialized,snapshot) && ok;
        // The new C owner releases optional and partially filled sound lists.
        // Cleanup is outside the state trace; allocation balance verifies it.
        if (initialized && stage==1 && n && (variant==1 || variant==2))
            ok=ActionDefinitionFixtureAccess::CheckNoSoundMarkers(definition) && ok;
        block.Free(); mm_action_resource_release(action); mm_action_dispose(&direct); tables.Reset(); ReleasePpo(&cels); ReleasePpo(&matrices); ReleasePpo(&sounds); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
#ifdef DEBUG
        ok=ok && vdmglob.dmaglBase.cv==beforeBase && vdmglob.dmaglHq.cv==beforeHq && vdmglob.dmaglPv.cv==beforePv;
#endif
        if (!ok) std::fprintf(stderr,"Action definition failed: %d %d %d %d %d\n",shape,variant,stage,kind,failure);
    }
    std::fprintf(stderr,"[action definition] attempts=%d loaded=%d adopted=%d\n",attempts,loaded,adopted);
#ifdef DEBUG
    ok=ok && loaded==597 && adopted==159;
#else
    ok=ok && loaded==648 && adopted==162;
#endif
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && attempts==2268 && loaded && adopted;
}
#endif
