#ifndef ENGINE_MOVIE_SOUND_FIXTURE_H
#define ENGINE_MOVIE_SOUND_FIXTURE_H
#ifdef MM_TEST_NATIVE_AUDIOMAN
#include "audioman.h"
#include "sndampri.h"

static bool TestClassicWaveCache(std::vector<byte> &wave)
{
    auto file = ChunkyFile::PcflCreateTemp();
    auto cache = file ? ChunkyResourceFile::PcrfNew(file, 1024 * 1024) : nullptr;
    byte invalid[32] = {};
    ChunkNumber packed, broken;
    bool ok = cache && file->FAddPv(wave.data(), static_cast<LegacyLong>((wave.size)()), kctgWave, &packed) &&
              file->FPackData(kctgWave, packed) && file->FPacked(kctgWave, packed) &&
              file->FAddPv(invalid, sizeof(invalid), kctgWave, &broken);
    if (ok)
    {
        auto sound = static_cast<PCachedAudioManSound>(cache->PbacoFetch(kctgWave, packed, CachedAudioManSound::FReadCams));
        auto bad = cache->PbacoFetch(kctgWave, broken, CachedAudioManSound::FReadCams);
        ok = sound && sound->psnd && sound->psnd->GetSamples() == 4096 && !bad;
        ReleasePpo(&bad);
        ReleasePpo(&sound);
    }
    ReleasePpo(&cache);
    ReleasePpo(&file);
    if (!ok)
        std::fputs("Classic AudioMan cache fixture failed.\n", stderr);
    return ok;
}
#endif
class MovieSoundFixtureAccess : public Movie
{
  public:
    static MovieData *Data(PMovie movie) { auto member=&MovieSoundFixtureAccess::_data; return &(movie->*member); }
};
static bool TestPrivateSoundRoundTrip(PMovie parent, PFileObject input, long style, PString name)
{
    auto source = Movie::PmvieNew(false, parent->Pmcc());
    auto movie = Movie::PmvieNew(false, parent->Pmcc());
    bool ok = source && movie;
    for (auto document : {source, movie})
    {
        if (!ok)
            break;
        auto resources = NewPrivateMovieResources();
        MovieSoundFixtureAccess::Data(document)->resources = resources;
        document->SetDirty();
        ok = resources && !resources->services.legacy_adapter && document->FAutoSave() && document->FNewScenInsCore(0);
    }
    Filename path;
    const bool havePath = path.FGetTemp() && path.FChangeFtg(kftg3mm);
    ChunkNumber imported = 0, copied = 0;
    ChildChunkID child = 0;
    if (ok && havePath)
    {
        auto resources = MovieSoundFixtureAccess::Data(source)->Resources();
        ok = source->FCopySndFileToMvie(input, style, &imported, name) &&
             movie->FCopyMsndFromCrf(resources, imported, &copied);
        if (ok)
        {
            TAG tag{ksidUseCrf, resources, kctgMsnd, imported};
            TagManager::DupTag(&tag);
            ok = movie->FSaveTagSnd(&tag) && tag.cno == copied && movie->FChidFromUserSndCno(copied, &child);
            if (ok)
            {
                const MMSceneSoundTag cue{child, {tag.sid, tag.cache, tag.ctg, tag.cno}};
                ok = movie->Pscen()->FAddSndCoreTagc(false, false, kvlmFull, style, 1, &cue) &&
                     movie->FSaveToFni(&path, true);
            }
            TagManager::CloseTag(&tag);
        }
    }
    ReleasePpo(&movie);
    ReleasePpo(&source);
    if (ok && havePath)
    {
        MovieData loaded;
        ok = loaded.FLoad(&path) && loaded.sceneCount == 1;
        if (ok)
        {
            auto resources = loaded.Resources();
            const auto chunks = mm_resource_cache_chunks(resources);
            MMU32 scene, number;
            ok = mm_chunk_link_number(&chunks, kctgMvie, loaded.movieNumber, 0, kctgScen, &scene) == MM_CHUNK_OK &&
                 mm_chunk_link_number(&chunks, kctgScen, scene, child, kctgMsnd, &number) == MM_CHUNK_OK;
            if (ok)
            {
                auto sound = EngineSound::Fetch(resources, kctgMsnd, number);
                String title;
                ok = sound && EngineSound::Valid(sound) && EngineSound::Style(sound) == style &&
                     ReadResourceName(resources, kctgMsnd, number, &title) && title.FEqual(name);
                mm_sound_owner_release(sound);
            }
        }
    }
    if (havePath && path.TExists() != tNo)
        path.FDelete();
    return ok && havePath;
}

static bool TestMovieSoundIntegration(PMovie parent)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_MOVIE_SOUND_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    auto graph=[&](MMResourceCache *resources,ChunkTagOrType type,ChunkNumber number) {
        const auto chunks = mm_resource_cache_chunks(resources);
        MMU32 position;
        MMChunkEntry entry;
        if (mm_chunk_find(&chunks, type, number, &position) != MM_CHUNK_OK ||
            mm_chunk_at(&chunks, position, &entry) != MM_CHUNK_OK)
            return false;
        LegacyLong count = entry.child_count; if (!record(&count,sizeof(count))) return false;
        for (long i=0;i<count;++i)
        { MMChunkChild child; if (mm_chunk_child_at(&chunks,position,i,&child) != MM_CHUNK_OK) return false;
          LegacyLong row[]={static_cast<LegacyLong>(child.type),static_cast<LegacyLong>(child.number),static_cast<LegacyLong>(child.slot)};
          if (!record(row,sizeof(row))) return false; }
        return true;
    };
    bool ok=true;
    for (int mode=0;ok && mode<64;++mode)
    {
        auto movie=Movie::PmvieNew(false,parent->Pmcc(),nullptr,cnoNil); MMResourceCache *resource=nullptr;
        auto source=ChunkyFile::PcflCreateTemp(); auto external=source?ChunkyResourceFile::PcrfNew(source,0):nullptr;
        ok=movie && external && movie->FNewScenInsCore(0) && movie->FNewScenInsCore(1) && movie->FSwitchScen(0) && movie->FAutoSave() && movie->FEnsureAutosave(&resource);
        auto data=movie?MovieSoundFixtureAccess::Data(movie):nullptr;
        const MovieArchiveFixture archive(resource);
        ChunkNumber sounds[3]={},foreign[3]={},other=0,sub=0; byte dummy=0;
        ChunkNumber scene;
        ok=ok && archive.Link(kctgMvie,data->movieNumber,0,kctgScen,&scene);
        for (int i=0;ok && i<3;++i) ok=archive.AddBytes(&dummy,1,kctgMsnd,&sounds[i]) && source->FAddPv(&dummy,1,kctgMsnd,&foreign[i]);
        ok=ok && archive.AddBytes(&dummy,1,kctgMtrl,&other) && archive.Adopt(kctgScen,scene,kctgMtrl,other,1);
        if (ok && (mode&1)) ok=archive.Adopt(kctgScen,scene,kctgMsnd,sounds[0],0);
        if (ok && (mode&2)) ok=archive.Adopt(kctgMvie,data->movieNumber,kctgMsnd,sounds[0],2);
        if (ok && (mode&4)) ok=archive.Adopt(kctgScen,scene,kctgMsnd,sounds[1],2);
        if (ok && (mode&16)) ok=archive.AddChild(kctgMsnd,sounds[2],0,1,kctgWave,&sub);
        bool haveValid=false,unused=false,unusedFast=false; ChildChunkID child=777,repeat=888;
        if (ok)
        {
            unused=movie->FUnusedSndsUser(&haveValid); unusedFast=movie->FUnusedSndsUser();
            ok=unused && unusedFast && haveValid==((mode&16)!=0) && movie->FChidFromUserSndCno(sounds[2],&child) && movie->FChidFromUserSndCno(sounds[2],&repeat) && child==repeat;
        }
        TAG tag={((mode&32)?1:ksidUseCrf),(mode&4)?external->CoreCache():resource,kctgMsnd,(mode&8)?sounds[2]:999999};
        bool resolved=false,missing=false,adopted=false,saved=false,copied=false; ChunkNumber copyNumber=999999;
        if (ok)
        {
            TagManager::DupTag(&tag);
            resolved=movie->FResolveSndTag(&tag,child,(mode&2)?scene:cnoNil,(mode&1)?resource:nullptr);
            missing=movie->FResolveSndTag(&tag,999999,scene,resource);
            ok=resolved && missing==((mode&32)!=0);
            adopted=movie->_FAdoptMsndInMvie(resource,scene); ok=ok && adopted;
            TAG transfer={ksidUseCrf,external->CoreCache(),kctgMsnd,foreign[0]}; TagManager::DupTag(&transfer);
            saved=movie->FSaveTagSnd(&transfer); ok=ok && saved;
            copied=movie->FCopyMsndFromCrf(external->CoreCache(),foreign[1],&copyNumber); ok=ok && copied;
            LegacyLong row[]={mode,unused,haveValid,unusedFast,static_cast<LegacyLong>(child),static_cast<LegacyLong>(repeat),resolved,missing,adopted,saved,copied,
                static_cast<LegacyLong>(copyNumber),tag.sid,static_cast<LegacyLong>(tag.cno),tag.cache==resource,tag.cache==external->CoreCache(),
                static_cast<LegacyLong>(transfer.cno),transfer.cache==resource,resource->references->count,external->CactRef(),movie->FQueryGCSndsOnClose()};
            ok=record(row,sizeof(row)) && graph(resource,kctgMvie,data->movieNumber) && graph(resource,kctgScen,scene) && ok;
            TagManager::CloseTag(&transfer); TagManager::CloseTag(&tag);
        }
        ReleasePpo(&movie); ReleasePpo(&external); ReleasePpo(&source); vpers->Clear();
        if (!ok) std::fprintf(stderr,"Movie sound fixture failed at mode %d\n",mode);
    }
    for (int mode=0;ok && mode<4;++mode)
    {
        auto movie=Movie::PmvieNew(false,parent->Pmcc(),nullptr,cnoNil); MMResourceCache *resource=nullptr;
        Filename filename; auto temporary=FileObject::PfilCreateTemp(); if (temporary) temporary->GetFni(&filename);
        ok=movie && temporary; ReleasePpo(&temporary);
        ok=ok && filename.FChangeFtg((mode&1)?kftgMidi:kftgWave) && movie->FNewScenInsCore(0) && movie->FAutoSave() && movie->FEnsureAutosave(&resource);
        auto input=ok?FileObject::PfilCreate(&filename):nullptr; if (input) input->SetTemp();
        std::vector<byte> bytes;
        if (mode&1)
        {
            const byte midi[]={'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k',0,0,0,12,0,0x90,60,64,96,0x80,60,0,0,0xff,0x2f,0};
            bytes.assign(midi,midi+sizeof(midi));
        }
        else
        {
            bytes.resize(44+8192);
            auto word=[&](int at,unsigned value,int count) { for (int i=0;i<count;++i) bytes[at+i]=byte(value>>(i*8)); };
            std::memcpy(bytes.data(),"RIFF",4); word(4,static_cast<unsigned>((bytes.size)()-8),4); std::memcpy(bytes.data()+8,"WAVEfmt ",8);
            word(16,16,4); word(20,1,2); word(22,1,2); word(24,22050,4); word(28,44100,4); word(32,2,2); word(34,16,2);
            std::memcpy(bytes.data()+36,"data",4); word(40,8192,4);
            for (int i=0;i<4096;++i) word(44+i*2,unsigned((i%32)*128),2);
        }
#ifdef MM_TEST_NATIVE_AUDIOMAN
        if (mode == 0)
            ok = ok && TestClassicWaveCache(bytes);
#endif
        if (mode>=2) bytes.resize(3);
        ok=ok && input && input->FWriteRgb(bytes.data(),static_cast<long>((bytes.size)()),0);
        ChunkNumber output=0x12341234; String name; name.SetSzs(PszLit("Imported fixture"));
        if (ok)
        {
            const bool beforeGc=movie->FQueryGCSndsOnClose(); bool imported=movie->FCopySndFileToMvie(input,stySfx,&output,&name);
            bool valid=false,unused=movie->FUnusedSndsUser(&valid);
            LegacyLong row[]={100+mode,imported,beforeGc,movie->FQueryGCSndsOnClose(),static_cast<LegacyLong>(output),unused,valid};
            bool expected=mode<2;
#if defined(WIN) && !defined(KAUAI_SDL3) && !defined(MM_TEST_NATIVE_AUDIOMAN)
            expected=mode==1; /* The fallback AudioMan implementation is an E_NOTIMPL stub. */
#endif
            ok=imported==expected && record(row,sizeof(row)) && graph(resource,kctgMvie,MovieSoundFixtureAccess::Data(movie)->movieNumber);
            if (ok && expected)
                ok = TestPrivateSoundRoundTrip(parent, input, (mode & 1) ? styMidi : stySfx, &name);
        }
        ReleasePpo(&input); ReleasePpo(&movie); vpers->Clear();
        if (!ok) std::fprintf(stderr,"Physical sound import fixture failed at mode %d\n",mode);
    }
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}
#endif
