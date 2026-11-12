// A real host supplies assertion reporting; no GUI/application replacements.
#if defined(TEST_ENGINE_CORE)
#include "soc_core.h"
#elif defined(TEST_BRENDER_CORE)
#include "bren_core.h"
#else
#include "kauai_core.h"
#endif
#if defined(FRAME_H) || defined(GOB_H) || defined(APPB_H) || defined(BWLD_H)
#error Core headers must not import GUI or World definitions
#endif
#include "rich_text_data.h"
#include "cmd.h"
#include "clok.h"
#include "cmd_hooks.h"
#include "platform_files.h"
#include "stream.h"
#include "codec.h"
#if defined(TEST_ENGINE_CORE)
#include "actor_frame_fixture.h"
#include "three_mm/actor_motion.h"
#include "three_mm/actor_pose.h"
#include "three_mm/actor_effect.h"
#include "actor_sound.h"
#include "studio_preferences.h"
#endif
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Public core headers must not import Windows SDK headers
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>

void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
ASSERTNAME

static void CheckAt(bool condition, LegacyLong line)
{
    if (!condition)
    {
        std::fprintf(stderr, "Core check failed at %s:%" LEGACY_PRId "\n", __FILE__, line);
        std::exit(1);
    }
}
#define Check(condition) CheckAt((condition), __LINE__)

class ReplayTarget : public CommandHandler
{
  public:
    LegacyLong calls = 0;
    LegacyLong sum = 0;
    ReplayTarget() : CommandHandler(4242)
    {
    }
    bool FDoCmd(PCommand command) override
    {
        Check(command->cid == 0x10001 && command->pcmh == this);
        ++calls;
        sum += command->rglw[0];
        return true;
    }
};

static ReplayTarget *s_replayTarget;
static PCommandHandler ReplayLookup(LegacyLong hid)
{
    return s_replayTarget != pvNil && hid == s_replayTarget->Hid() ? s_replayTarget : pvNil;
}

static void TestReadOnlyEmptyChunkSave()
{
    Filename source, destination;
    Check(source.FGetTemp() && destination.FGetTemp());
    const ChunkTagOrType type = 'TEST';
    byte payload[] = {1, 2, 3, 4};
    ChunkNumber first, hole, last, empty;
    PChunkyFile file = ChunkyFile::PcflCreate(&source, fcflWriteEnable);
    Check(file != pvNil);
    Check(file->FAddPv(payload, sizeof(payload), type, &first));
    Check(file->FAddPv(payload, sizeof(payload), type, &hole));
    Check(file->FAddPv(payload, sizeof(payload), type, &last));
    file->Delete(type, hole); // Persist a nonempty free-space map between live chunks.
    Check(file->FSave(type));
    ReleasePpo(&file);

    file = ChunkyFile::PcflOpen(&source, fcflNil);
    Check(file != pvNil);
    Check(file->FAdd(0, type, &empty)); // No storage allocation or primary-file write.
    Check(file->FSave(type, &destination));
    ReleasePpo(&file);

    file = ChunkyFile::PcflOpen(&destination, fcflNil);
    DataBlock block;
    byte actual[sizeof(payload)];
    Check(file != pvNil && file->FFind(type, empty, &block));
    Check(block.Cb() == 0);
    Check(file->FFind(type, last, &block));
    Check(block.FReadRgb(actual, sizeof(actual), 0));
    Check(std::memcmp(actual, payload, sizeof(payload)) == 0);
    file->SetTemp(true);
    ReleasePpo(&file);

    file = ChunkyFile::PcflOpen(&source, fcflNil);
    Check(file != pvNil && !file->FFind(type, empty));
    Check(file->FFind(type, first, &block));
    Check(block.FReadRgb(actual, sizeof(actual), 0));
    Check(std::memcmp(actual, payload, sizeof(payload)) == 0);
    Check(file->FFind(type, last));
    file->SetTemp(true);
    ReleasePpo(&file);
}

static void TestChunkPersistence()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_CHUNK_SAVE_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb"); Check(snapshot != nullptr);
    }
    byte payload[1024], bytes[16384];
    for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<byte>(i * 37 + i / 13);
    auto capture = [&](Filename &name) {
        auto raw = FileObject::PfilOpen(&name);
        Check(raw && raw->FpMac() < sizeof(bytes));
        const LegacyLong count = raw->FpMac();
        Check(raw->FReadRgb(bytes, count, 0));
        if (snapshot)
        {
            Check(std::fwrite(&count, sizeof(count), 1, snapshot) == 1);
            Check(std::fwrite(bytes, 1, count, snapshot) == static_cast<size_t>(count));
        }
        ReleasePpo(&raw);
    };
    auto captureWalk = [&](PChunkyFile file, ChunkTagOrType type, ChunkNumber root) {
        ChunkGraphEnumerator walk;
        for (int mode = 0; mode < 8; ++mode)
        {
            walk.Init(file, type, root);
            ChildChunkIdentification node; ChunkIdentification parent; LegacyUlong flags, input = fcgeNil;
            uint32_t step = 0;
            while (walk.FNextKid(&node, &parent, &flags, input))
            {
                Check(step < 1000 && !(flags & fcgeError));
                uint32_t record[] = {static_cast<uint32_t>(mode), step, static_cast<uint32_t>(flags), node.cki.ctg,
                    node.cki.cno, flags & fcgeRoot ? 0U : node.chid,
                    flags & fcgeRoot ? 0U : parent.ctg, flags & fcgeRoot ? 0U : parent.cno};
                if (snapshot) Check(std::fwrite(record, sizeof(record), 1, snapshot) == 1);
                input = mode && (step + mode) % (mode + 1) == 0 ? fcgeSkipToSib : fcgeNil;
                if (mode == 7 && step == 2) walk.Init(file, type, root); // Reuse an active enumerator.
                ++step;
            }
            if (snapshot) Check(std::fwrite(&step, sizeof(step), 1, snapshot) == 1);
        }
    };
    for (LegacyLong length : {0L,1L,3L,257L,1024L})
    {
        Filename source, copy, destination;
        Check(source.FGetTemp() && copy.FGetTemp() && destination.FGetTemp());
        auto file = ChunkyFile::PcflCreate(&source, fcflWriteEnable);
        ChunkNumber first, hole, last;
        Check(file && file->FAddPv(payload, length, 'TEST', &first));
        Check(file->FAddPv(payload, 17, 'TEST', &hole) && file->FAddPv(payload, 31, 'TEST', &last));
        String name(PszLit("Persistent chunk name"));
        Check(file->FSetName('TEST', first, &name));
        Check(file->FAdoptChild('TEST', first, 'TEST', last, 77, false));
        file->Delete('TEST', hole); // Interior free space, including an empty first chunk.
        Check(file->FSave('SAVE'));
        capture(source);
        Check(file->FSetName('TEST', first, nullptr) && file->FSave('NEXT'));
        capture(source); // Shorter index retains physical trailing bytes.
        Check(file->FSaveACopy('COPY', &copy));
        capture(copy);
        ReleasePpo(&file);
        file = ChunkyFile::PcflOpen(&source, fcflNil);
        Check(file && file->FSave('AS  ', &destination));
        capture(destination);
        Check(file->FPutPv(payload, 23, 'TEST', first));
        Check(file->FSave('SWAP')); // Read-only origin: compact extra data and swap names.
        capture(destination);
        Check(file->FSave('SAME', &destination)); // An explicit current name becomes a swap.
        capture(destination);
        file->SetTemp(true); ReleasePpo(&file);
        file = ChunkyFile::PcflOpen(&copy, fcflNil);
        DataBlock block;
        Check(file && file->FFind('TEST', first, &block) && block.Cb() == length);
        Check(block.FRead(bytes) && !std::memcmp(bytes, payload, length));
        file->SetTemp(true); ReleasePpo(&file);
        Check(source.FDelete());
    }
    for (int extra = 0; extra < 2; ++extra)
    {
        Filename source, copy;
        Check(source.FGetTemp() && copy.FGetTemp());
        auto file = ChunkyFile::PcflCreate(&source, fcflWriteEnable);
        Check(file && file->FSave('HEAP'));
        if (extra) { ReleasePpo(&file); file = ChunkyFile::PcflOpen(&source, fcflNil); Check(file); }
        ChunkNumber numbers[24] = {};
        LegacyLong sizes[24] = {};
        bool live[24] = {};
        LegacyUlong seed = 0x12345678;
        for (int step = 0; step < 600; ++step)
        {
            seed = seed * 1664525U + 1013904223U;
            const int slot = (seed >> 16) % 24;
            const LegacyLong length = (seed >> 24) % 97;
            if (!live[slot])
            {
                Check(file->FAddPv(payload, length, 'HEAP', &numbers[slot]));
                live[slot] = true; sizes[slot] = length;
            }
            else if (seed & 1)
            {
                file->Delete('HEAP', numbers[slot]); live[slot] = false;
            }
            else
            {
                Check(file->FPutPv(payload, length, 'HEAP', numbers[slot])); sizes[slot] = length;
            }
            for (int i = 0; i < 24; ++i) if (live[i])
            {
                FileLocation location;
                Check(file->FFindFlo('HEAP', numbers[i], &location) && location.cb == sizes[i]);
                Check(location.FRead(bytes) && !std::memcmp(bytes, payload, sizes[i]));
                LegacyLong record[] = {extra, step, i, static_cast<LegacyLong>(numbers[i]), location.fp, location.cb,
                    file->FOnExtra('HEAP', numbers[i])};
                if (snapshot) Check(std::fwrite(record, sizeof(record), 1, snapshot) == 1);
            }
        }
        Check(file->FSave('HEAP')); capture(source);
        Check(file->FSaveACopy('COPY', &copy)); capture(copy);
        file->SetTemp(true); ReleasePpo(&file);
        file = ChunkyFile::PcflOpen(&copy, fcflNil); Check(file); file->SetTemp(true); ReleasePpo(&file);
    }
    {
        Filename source; Check(source.FGetTemp());
        auto file = ChunkyFile::PcflCreate(&source, fcflWriteEnable);
        ChunkNumber nodes[8];
        for (int i = 0; i < 8; ++i)
        {
            Check(file->FAddPv(payload, 19 + i, 'GRPH', &nodes[i]));
            String name(PszLit("Named graph node")); Check(file->FSetName('GRPH', nodes[i], &name));
        }
        Check(file->FAdoptChild('GRPH', nodes[0], 'GRPH', nodes[1], 1));
        Check(file->FAdoptChild('GRPH', nodes[0], 'GRPH', nodes[3], 3));
        Check(file->FAdoptChild('GRPH', nodes[1], 'GRPH', nodes[2], 5));
        Check(file->FAdoptChild('GRPH', nodes[3], 'GRPH', nodes[2], 5));
        Check(file->FAdoptChild('GRPH', nodes[3], 'GRPH', nodes[2], 6));
        Check(file->FAdoptChild('GRPH', nodes[0], 'GRPH', nodes[4], 4, false));
        for (int i = 4; i < 7; ++i) Check(file->FAdoptChild('GRPH', nodes[i], 'GRPH', nodes[i+1], i));
        Check(file->FAdoptChild('GRPH', nodes[0], 'GRPH', nodes[4], 4, true));
        Check(file->FLoner('GRPH', nodes[4])); // Duplicate adoption does not clear loner.
        captureWalk(file, 'GRPH', nodes[0]);
        file->ChangeChid('GRPH', nodes[0], 'GRPH', nodes[1], 1, 9);
        captureWalk(file, 'GRPH', nodes[0]);
        file->ChangeChid('GRPH', nodes[0], 'GRPH', nodes[1], 9, 1);
        file->ChangeChid('GRPH', nodes[0], 'GRPH', nodes[4], 4, 3);
        captureWalk(file, 'GRPH', nodes[0]);
        file->ChangeChid('GRPH', nodes[0], 'GRPH', nodes[4], 3, 4);
        Check(file->FSave('GRPH')); capture(source);
        Check(file->FPutPv(payload, 37, 'GRPH', nodes[0]));
        Check(file->Ckid('GRPH', nodes[0]) == 3 && file->CckiRef('GRPH', nodes[2]) == 3);
        file->Delete('GRPH', nodes[0]);
        for (int i = 0; i < 8; ++i) Check(file->FFind('GRPH', nodes[i]) == (i >= 4));
        Check(file->CckiRef('GRPH', nodes[4]) == 0 && file->Ckid('GRPH', nodes[4]) == 1);
        Check(file->FSave('LEFT')); capture(source);
        file->DeleteChild('GRPH', nodes[4], 'GRPH', nodes[5], 4);
        for (int i = 5; i < 8; ++i) Check(!file->FFind('GRPH', nodes[i]));
        file->Delete('GRPH', nodes[4]);
        Check(file->FSave('NONE')); capture(source);
        file->SetTemp(true); ReleasePpo(&file);
    }
    {
        Filename source; Check(source.FGetTemp());
        auto file = ChunkyFile::PcflCreate(&source, fcflWriteEnable);
        ChunkNumber nodes[48];
        for (int i = 0; i < 48; ++i) Check(file->FAddPv(payload, 1, 'DEEP', &nodes[i]));
        for (int i = 0; i < 47; ++i) Check(file->FAdoptChild('DEEP', nodes[i], 'DEEP', nodes[i+1], i));
        captureWalk(file, 'DEEP', nodes[0]);
        Check(file->FSave('DEEP')); capture(source);
        file->Delete('DEEP', nodes[0]);
        Check(file->Ccki() == 0 && file->FSave('GONE')); capture(source);
        file->SetTemp(true); ReleasePpo(&file);
    }
    {
        Filename source, destination; Check(source.FGetTemp() && destination.FGetTemp());
        auto src = ChunkyFile::PcflCreate(&source, fcflWriteEnable);
        auto dst = ChunkyFile::PcflCreate(&destination, fcflWriteEnable);
        ChunkNumber nodes[4], conflict, first, again, clone, changed, local;
        for (int i = 0; i < 4; ++i)
        {
            Check(src->FAddPv(payload + i, 19 + i, 'COPY', &nodes[i]));
            String name(PszLit("Copied named node")); Check(src->FSetName('COPY', nodes[i], &name));
        }
        Check(src->FAdoptChild('COPY', nodes[0], 'COPY', nodes[2], 2));
        Check(src->FAdoptChild('COPY', nodes[0], 'COPY', nodes[1], 3));
        Check(src->FAdoptChild('COPY', nodes[2], 'COPY', nodes[1], 3));
        Check(dst->FAddPv(payload, 3, 'COPY', &conflict));
        Check(src->FCopy('COPY', nodes[0], dst, &first));
        Check(src->FCopy('COPY', nodes[0], dst, &again) && again == first && dst->Ccki() == 4);
        Check(src->FClone('COPY', nodes[0], dst, &clone) && clone != first && dst->Ccki() == 7);
        ChildChunkIdentification branch, direct, shared;
        Check(dst->FGetKid('COPY', clone, 0, &branch) && dst->FGetKid('COPY', clone, 1, &direct));
        Check(dst->FGetKid(branch.cki.ctg, branch.cki.cno, 0, &shared) && shared.cki.cno == direct.cki.cno);
        Check(dst->FSave('COPY')); capture(destination);
        Check(src->FPutPv(payload + 7, 31, 'COPY', nodes[1]));
        Check(src->FCopy('COPY', nodes[0], dst, &changed) && changed != first && changed != clone);
        Check(src->FCopy('COPY', nodes[0], dst, &again) && again == changed);
        Check(src->FClone('COPY', nodes[0], src, &local) && local != nodes[0]);
        Check(src->FCopy('COPY', nodes[1], src, &again) && again == nodes[1] && src->FLoner('COPY', nodes[1]));
        src->SwapData('COPY', nodes[1], 'COPY', nodes[3]);
        src->SwapChildren('COPY', nodes[0], 'COPY', nodes[3]);
        src->Move('COPY', nodes[1], 'MOVE', 40);
        src->Move('COPY', nodes[2], 'COPY', 20);
        src->Move('COPY', 20, 'COPY', nodes[2]);
        src->SwapData('COPY', nodes[0], 'COPY', nodes[0]);
        captureWalk(src, 'COPY', nodes[3]);
        Check(src->FSave('SWAP') && dst->FSave('MORE')); capture(source); capture(destination);
        uint32_t record[] = {first, again, clone, changed, local, static_cast<uint32_t>(src->Ccki()), static_cast<uint32_t>(dst->Ccki())};
        if (snapshot) Check(std::fwrite(record, sizeof(record), 1, snapshot) == 1);
        src->SetTemp(true); dst->SetTemp(true); ReleasePpo(&src); ReleasePpo(&dst);
    }
    if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestChunkTableBaseline()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_TABLE_SNAPSHOT"))
    { snapshot = std::fopen(path, "wb"); Check(snapshot != nullptr); }
    for (int width : {20, 32})
    {
        auto group = GeneralGroup::PggNew(width); Check(group);
        byte fixed[32], variable[32];
        auto observe = [&]() {
            DataBlock block; Check(block.FSetTemp(group->CbOnFile()) && group->FWrite(&block));
            byte wire[2048]; const auto count = block.Cb(); Check(count < sizeof(wire));
            Check(block.FReadRgb(wire, count, 0));
            if (snapshot) Check(std::fwrite(wire, 1, count, snapshot) == static_cast<size_t>(count));
        };
        for (int i = 0; i < 6; ++i)
        {
            for (int j = 0; j < width; ++j) fixed[j] = static_cast<byte>(j * 7 + i);
            for (int j = 0; j < 32; ++j) variable[j] = static_cast<byte>(j + i * 11);
            Check(group->FInsert(0, i * 4, variable, fixed));
        }
        observe();
        Check(group->FInsertRgb(2, 4, 4, const_cast<char *>("ABCD")));
        group->DeleteRgb(1, 0, 4); group->Move(0, 6);
        Check(group->FMoveRgb(0, 0, 3, 4, 4)); group->Delete(2); observe();
        for (int j = 0; j < width; ++j) fixed[j] = static_cast<byte>(200 + j);
        Check(group->FInsert(1, 8, const_cast<char *>("12345678"), fixed)); observe();
        auto bytes = static_cast<byte *>(group->PvLock(0)); bytes[0] ^= 0x5a; group->Unlock(); observe();
        ReleasePpo(&group);
    }
    if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestContainerRegistry()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_CONTEXT_SNAPSHOT"))
    { snapshot = std::fopen(path, "wb"); Check(snapshot != nullptr); }
    auto original = ChunkyFile::PcflFirst();
    auto a = ChunkyFile::PcflCreateTemp(), b = ChunkyFile::PcflCreateTemp(), c = ChunkyFile::PcflCreateTemp();
    Check(a && b && c);
    auto observe = [&](uint32_t event) {
        uint32_t row[5] = {event, 0, 0, 0, 0};
        auto current = ChunkyFile::PcflFirst();
        for (int i = 1; current != original; ++i, current = current->PcflNext())
        { Check(current && i < 5); row[i] = (current == a ? 10 : current == b ? 20 : 30) + current->CactRef(); }
        if (snapshot) Check(std::fwrite(row, sizeof(row), 1, snapshot) == 1);
    };
    Check(c->PcflNext() == b && b->PcflNext() == a && a->PcflNext() == original); observe(1);
    Check(b->FSetGrfcfl(fcflMark, fcflMark)); b->Release();
    Check(b->CactRef() == 0); observe(2);
    ReleasePpo(&a); Check(b->PcflNext() == original); observe(3);
    ChunkyFile::CloseUnmarked(); Check(c->PcflNext() == b); observe(4);
    ChunkyFile::ClearMarks(); ChunkyFile::CloseUnmarked(); b = nullptr;
    Check(c->PcflNext() == original); observe(5);
    ReleasePpo(&c); Check(ChunkyFile::PcflFirst() == original); observe(6);
    if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestContainerConstruction()
{
    FILE *snapshot=nullptr;
    if(const char *path=std::getenv("THREE_MM_CONTAINER_SNAPSHOT")){snapshot=std::fopen(path,"wb");Check(snapshot!=nullptr);}
    for(int mode=0;mode<3;++mode)
    {
        Filename reference,name;Check(reference.FGetTemp());
        auto file=mode==2?ChunkyFile::PcflCreateTemp(&reference):ChunkyFile::PcflCreate(&reference,mode==1?fcflTemp:fcflNil);
        Check(file!=nullptr);file->GetFni(&name);Check(file->FTemp()==(mode!=0));
        Check(file->FPutPv(const_cast<char *>("payload"),7,'CTOR',1) && file->FSave('CTOR'));
        auto raw=FileObject::PfilFromFni(&name);Check(raw!=nullptr);byte bytes[2048];LegacyLong count=raw->FpMac();
        Check(count<sizeof(bytes) && raw->FReadRgb(bytes,count,0));
        uint32_t record[]={static_cast<uint32_t>(mode),static_cast<uint32_t>(count),static_cast<uint32_t>(name.Ftg()),file->FTemp()};
        if(snapshot){Check(std::fwrite(record,sizeof(record),1,snapshot)==1);Check(std::fwrite(bytes,1,count,snapshot)==static_cast<size_t>(count));}
        ReleasePpo(&file);
        if(mode==0)
        {
            file=ChunkyFile::PcflOpen(&name,fcflNil);Check(file!=nullptr);auto same=ChunkyFile::PcflOpen(&name,fcflNil);Check(same==file);
            ReleasePpo(&same);DataBlock block;Check(file->FFind('CTOR',1,&block) && block.FReadRgb(bytes,7,0,true) && !std::memcmp(bytes,"payload",7));
            block.Free();ReleasePpo(&file);Check(name.FDelete());
        }
        else {raw=FileObject::PfilOpen(&name);Check(raw==nullptr);}
    }
    Filename bad;Check(bad.FGetTemp());auto raw=FileObject::PfilCreate(&bad);Check(raw && raw->FWriteRgb(const_cast<char *>("invalid"),7,0));ReleasePpo(&raw);
    auto rejected=ChunkyFile::PcflOpen(&bad,fcflNil);Check(!rejected && !ChunkyFile::PcflFromFni(&bad));Check(bad.FDelete());
    if(snapshot)Check(std::fclose(snapshot)==0);
}

static void TestContainerLifecycle()
{
    FILE *snapshot=nullptr;
    if(const char *path=std::getenv("THREE_MM_LIFECYCLE_SNAPSHOT")){snapshot=std::fopen(path,"wb");Check(snapshot!=nullptr);}
    for(int writable=0;writable<2;++writable)
    {
        Filename name;Check(name.FGetTemp());auto file=ChunkyFile::PcflCreate(&name,fcflWriteEnable);Check(file!=nullptr);
        byte payload[17]={};Check(file->FPutPv(payload,sizeof(payload),'LIFE',1) && file->FSave('LIFE'));ReleasePpo(&file);
        file=ChunkyFile::PcflOpen(&name,writable?fcflWriteEnable:fcflNil);auto copy=ChunkyFile::PcflCreateTemp();Check(file && copy);
        ChunkNumber first,same,packed,forest,reopened;
        Check(file->FCopy('LIFE',1,copy,&first));file->SetPacked('LIFE',1,false);file->SetForest('LIFE',1,false);
        Check(file->FCopy('LIFE',1,copy,&same) && same==first && copy->Ccki()==1);
        file->SetPacked('LIFE',1,true);Check(file->FCopy('LIFE',1,copy,&packed) && packed!=first && copy->Ccki()==2);
        file->SetForest('LIFE',1,true);Check(file->FCopy('LIFE',1,copy,&forest) && forest!=packed && copy->Ccki()==3);
        file->SetPacked('LIFE',1,true);file->SetForest('LIFE',1,true);Check(file->FCopy('LIFE',1,copy,&same) && same==forest);
        Check(file->FEnsureOnExtra('LIFE',1));const bool reopenedOk=file->FReopen();Check(reopenedOk==!writable);
        Check(file->FPacked('LIFE',1)==!!writable && file->FForest('LIFE',1)==!!writable && file->FOnExtra('LIFE',1)==!!writable);
        Check(file->FCopy('LIFE',1,copy,&reopened) && (writable?reopened==forest:reopened!=forest) && copy->Ccki()==(writable?3:4));
        Check(file->FSetGrfcfl(fcflMark,fcflMark));auto identity=file;ReleasePpo(&file);Check(ChunkyFile::PcflFromFni(&name)==identity);
        file=ChunkyFile::PcflOpen(&name,fcflNil);Check(file==identity);ReleasePpo(&file);ReleasePpo(&copy);
        ChunkyFile::ClearMarks();ChunkyFile::CloseUnmarked();Check(ChunkyFile::PcflFromFni(&name)==nullptr);
        uint32_t record[]={static_cast<uint32_t>(writable),first,same,packed,forest,reopened,reopenedOk};
        if(snapshot)Check(std::fwrite(record,sizeof(record),1,snapshot)==1);
        Check(name.FDelete());
    }
    if(snapshot)Check(std::fclose(snapshot)==0);
}

static void TestStorageRouting()
{
    FILE *snapshot=nullptr;
    if(const char *path=std::getenv("THREE_MM_STORAGE_ROUTING_SNAPSHOT")) {snapshot=std::fopen(path,"wb");Check(snapshot!=nullptr);}
    const LegacyUlong modes[]={fcflNil,fcflWriteEnable,fcflReadFromExtra,fcflWriteEnable|fcflReadFromExtra,fcflWriteEnable|fcflAddToExtra};
    byte payload[64];for(unsigned i=0;i<sizeof(payload);++i)payload[i]=static_cast<byte>(i*17+3);
    for(int mode=0;mode<5;++mode)
    {
        Filename name;Check(name.FGetTemp());auto file=ChunkyFile::PcflCreate(&name,fcflWriteEnable);Check(file!=nullptr);
        Check(file->FPutPv(payload,17,'DATA',1) && file->FPutPv(nullptr,0,'ZERO',2));
        file->SetPacked('DATA',1,true);file->SetForest('DATA',1,true);Check(file->FSave('BASE'));ReleasePpo(&file);
        file=ChunkyFile::PcflOpen(&name,modes[mode]);Check(file!=nullptr);
        Check(file->FEnsureOnExtra('ZERO',2) && !file->FOnExtra('ZERO',2));
        auto copied = ChunkyFile::PcflCreateTemp(); Check(copied != nullptr);
        ChunkNumber firstCopy, repeatedCopy;
        Check(file->FCopy('DATA',1,copied,&firstCopy));
        FileLocation before,after,zero;Check(file->FFindFlo('DATA',1,&before));
        const bool cachedBefore=file->FOnExtra('DATA',1);Check(cachedBefore==!!(modes[mode]&fcflReadFromExtra));
        Check(file->FEnsureOnExtra('DATA',1) && file->FEnsureOnExtra('DATA',1));
        Check(file->FCopy('DATA',1,copied,&repeatedCopy) && repeatedCopy == firstCopy && copied->Ccki() == 1);
        ReleasePpo(&copied);
        Check(file->FFindFlo('DATA',1,&after) && file->FFindFlo('ZERO',2,&zero));
        Check(file->FOnExtra('DATA',1) && !file->FOnExtra('ZERO',2) && file->FPacked('DATA',1) && file->FForest('DATA',1));
        byte actual[64];Check(after.FRead(actual) && !std::memcmp(actual,payload,17));
        Check(file->FPutPv(payload,23,'NEW ',3) && file->FPutPv(nullptr,0,'NIL ',4));
        FileLocation added,empty;Check(file->FFindFlo('NEW ',3,&added) && file->FFindFlo('NIL ',4,&empty));
        uint32_t record[]={static_cast<uint32_t>(mode),static_cast<uint32_t>(before.fp),static_cast<uint32_t>(after.fp),static_cast<uint32_t>(zero.fp),
            cachedBefore,before.pfil==after.pfil,zero.pfil==after.pfil,static_cast<uint32_t>(added.fp),file->FOnExtra('NEW ',3),static_cast<uint32_t>(empty.fp),file->FOnExtra('NIL ',4)};
        if(snapshot)Check(std::fwrite(record,sizeof(record),1,snapshot)==1);
        Check(file->FSave('DONE'));ReleasePpo(&file);
        auto raw=FileObject::PfilOpen(&name);Check(raw!=nullptr);LegacyLong count=raw->FpMac();byte bytes[2048];Check(count<sizeof(bytes) && raw->FReadRgb(bytes,count,0));
        if(snapshot){Check(std::fwrite(&count,sizeof(count),1,snapshot)==1);Check(std::fwrite(bytes,1,count,snapshot)==static_cast<size_t>(count));}
        ReleasePpo(&raw);Check(name.FDelete());
    }
    if(snapshot)Check(std::fclose(snapshot)==0);
}

static void TestFreeMapLoading()
{
    FILE *snapshot=nullptr;
    if(const char *path=std::getenv("THREE_MM_LOADING_SNAPSHOT")) { snapshot=std::fopen(path,"wb"); Check(snapshot!=nullptr); }
    Filename original; Check(original.FGetTemp());
    auto file=ChunkyFile::PcflCreate(&original,fcflWriteEnable); Check(file!=nullptr);
    byte payload[64]={};
    for(ChunkNumber i=1;i<=4;++i) Check(file->FPutPv(payload,11+i*6,'FMAP',i));
    file->Delete('FMAP',1); file->Delete('FMAP',3); Check(file->FSave('MAPS')); ReleasePpo(&file);
    auto raw=FileObject::PfilOpen(&original); Check(raw!=nullptr);
    byte baseline[4096]; LegacyLong length=raw->FpMac(); Check(length<sizeof(baseline) && raw->FReadRgb(baseline,length,0)); ReleasePpo(&raw);
    uint32_t mapOffset,mapBytes; std::memcpy(&mapOffset,baseline+28,4); std::memcpy(&mapBytes,baseline+32,4); Check(mapBytes==28);
    for(int mode=0;mode<6;++mode)
    {
        byte bytes[4096]; std::memcpy(bytes,baseline,length);
        if(mode==1) {
            for(unsigned i=0;i<4;i+=2) { byte b=bytes[mapOffset+i];bytes[mapOffset+i]=bytes[mapOffset+i+1];bytes[mapOffset+i+1]=b; }
            for(uint32_t i=4;i<mapBytes;i+=4) for(int j=0;j<2;++j) { byte b=bytes[mapOffset+i+j];bytes[mapOffset+i+j]=bytes[mapOffset+i+3-j];bytes[mapOffset+i+3-j]=b; }
        }
        if(mode==2) { uint32_t n=4; std::memcpy(bytes+mapOffset+4,&n,4); std::memcpy(bytes+mapOffset+8,&n,4); }
        if(mode==3) bytes[mapOffset]=0x7f;
        if(mode==4) { uint32_t n=mapBytes-1; std::memcpy(bytes+32,&n,4); n+=mapOffset; std::memcpy(bytes+16,&n,4); }
        if(mode==5) { std::memset(bytes+32,0,4); std::memcpy(bytes+16,&mapOffset,4); }
        Filename name; Check(name.FGetTemp()); raw=FileObject::PfilCreate(&name); Check(raw && raw->FWriteRgb(bytes,length,0)); ReleasePpo(&raw);
        file=ChunkyFile::PcflOpen(&name,fcflWriteEnable); Check(file!=nullptr);
        Check(file->FSetGrfcfl(fcflWriteEnable,fcflWriteEnable));
        Check(file->FPutPv(payload,12,'FMAP',5)); FileLocation location; Check(file->FFindFlo('FMAP',5,&location));
        Check(location.fp==(mode<2?128:232));
        LegacyLong record[]={mode,location.fp}; if(snapshot) Check(std::fwrite(record,sizeof(record),1,snapshot)==1);
        Check(file->FSave('LOAD')); ReleasePpo(&file);
        raw=FileObject::PfilOpen(&name); Check(raw!=nullptr); LegacyLong count=raw->FpMac(); Check(count<sizeof(bytes) && raw->FReadRgb(bytes,count,0)); ReleasePpo(&raw);
        if(snapshot) { Check(std::fwrite(&count,sizeof(count),1,snapshot)==1); Check(std::fwrite(bytes,1,count,snapshot)==static_cast<size_t>(count)); }
        Check(name.FDelete());
    }
    Check(original.FDelete()); if(snapshot) Check(std::fclose(snapshot)==0);
}

static void TestForestTransfer()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_FOREST_SNAPSHOT"))
    { snapshot = std::fopen(path, "wb"); Check(snapshot != nullptr); }
    auto source = ChunkyFile::PcflCreateTemp(); Check(source != nullptr);
    Check(source->FPutPv(const_cast<char *>("root"), 4, 'ROOT', 7));
    Check(source->FPutPv(const_cast<char *>("leaf-data"), 9, 'LEAF', 9));
    Check(source->FPutPv(nullptr, 0, 'ZERO', 2));
    Check(source->FAdoptChild('ROOT', 7, 'LEAF', 9, 30));
    Check(source->FAdoptChild('ROOT', 7, 'ZERO', 2, 40));
    Check(source->FAdoptChild('ZERO', 2, 'LEAF', 9, 50));
    source->SetPacked('LEAF', 9, true); source->SetForest('ROOT', 7, true); source->SetForest('LEAF', 9, true);
    auto output = FileObject::PfilCreateTemp(); Check(output != nullptr);
    LegacyLong computed, written;
    Check(source->FWriteChunkTree('ROOT', 7, nullptr, 0, &computed));
    Check(source->FWriteChunkTree('ROOT', 7, output, 7, &written) && computed == written && written < 1024);
    byte original[1024]; Check(output->FReadRgb(original, written, 7));
    if (snapshot) Check(std::fwrite(original, 1, written, snapshot) == static_cast<size_t>(written));
    for (int mode = 0; mode < 9; ++mode)
        for (int copy = 0; copy < 2; ++copy)
        {
            byte bytes[1024]; std::memcpy(bytes, original, written); LegacyLong count = written;
            if (mode == 1) // Opposite byte order on every descriptor.
            {
                for (LegacyLong pos = 0; pos < count;)
                {
                    int32_t payload; std::memcpy(&payload, bytes + pos + 12, 4);
                    for (int field = 0; field < 2; ++field) { byte b = bytes[pos + field * 2]; bytes[pos + field * 2] = bytes[pos + field * 2 + 1]; bytes[pos + field * 2 + 1] = b; }
                    for (int field = 4; field < 24; field += 4)
                        for (int j = 0; j < 2; ++j) { byte b = bytes[pos + field + j]; bytes[pos + field + j] = bytes[pos + field + 3 - j]; bytes[pos + field + 3 - j] = b; }
                    pos += 24 + payload;
                }
            }
            if (mode == 2) --count;
            if (mode == 3) bytes[0] = 0x7f;
            if (mode == 4) std::memset(bytes + 12, 0xff, 4); // Negative payload length.
            if (mode == 5) { std::memset(bytes + 16, 0, 4); bytes[16] = 8; } // Missing children.
            if (mode == 6) { count = 28; std::memset(bytes + 16, 0xff, 4); } // Negative child count is a legacy leaf.
            if (mode == 7) { count = 0; }
            if (mode == 8) { bytes[2] = 0x7f; bytes[3] = 0x7f; } // OSK is opaque.
            auto file = FileObject::PfilCreateTemp(); Check(file != nullptr);
            Check(file->FWriteRgb(bytes, count, 7)); FileLocation location = {file, 7, count};
            auto imported = ChunkyFile::PcflReadForestFromFlo(&location, copy != 0);
            Check(!!imported == (mode == 0 || mode == 1 || mode == 6 || mode == 7 || mode == 8));
            if (imported) Check(imported->Ccki() == (mode == 7 ? 0 : mode == 6 ? 1 : 4));
            LegacyLong record[] = {mode, copy, !!imported, imported ? imported->Ccki() : -1};
            if (snapshot) Check(std::fwrite(record, sizeof(record), 1, snapshot) == 1);
            if (imported)
                for (LegacyLong i = 0; i < imported->Ccki(); ++i)
                {
                    ChunkIdentification node; LegacyLong children; DataBlock block;
                    Check(imported->FGetCki(i, &node, &children, &block)); byte payload[64];
                    Check(imported->FPacked(node.ctg, node.cno) == (node.ctg == 'LEAF'));
                    Check(imported->FForest(node.ctg, node.cno) == (!copy && (node.ctg == 'ROOT' || node.ctg == 'LEAF')));
                    Check(children == (node.ctg == 'ROOT' && mode != 6 ? 2 : node.ctg == 'ZERO' ? 1 : 0));
                    Check(imported->FLoner(node.ctg, node.cno) == (node.ctg == 'ROOT'));
                    Check(block.Cb(true) <= sizeof(payload) && block.FReadRgb(payload, block.Cb(true), 0, true));
                    uint32_t row[] = {node.ctg, node.cno, static_cast<uint32_t>(children), imported->CckiRef(node.ctg,node.cno),
                        imported->FLoner(node.ctg,node.cno), imported->FPacked(node.ctg,node.cno), imported->FForest(node.ctg,node.cno), static_cast<uint32_t>(block.Cb(true))};
                    if (snapshot) { Check(std::fwrite(row,sizeof(row),1,snapshot)==1); Check(std::fwrite(payload,1,block.Cb(true),snapshot)==static_cast<size_t>(block.Cb(true))); }
                    for (LegacyLong j=0; j<children; ++j) { ChildChunkIdentification child; Check(imported->FGetKid(node.ctg,node.cno,j,&child)); if(snapshot) Check(std::fwrite(&child,sizeof(child),1,snapshot)==1); }
                }
            ReleasePpo(&imported); ReleasePpo(&file);
        }
    ReleasePpo(&source); ReleasePpo(&output);
    if (snapshot) Check(std::fclose(snapshot)==0);
}

static void TestStringWire()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_STRING_WIRE_SNAPSHOT"))
    { snapshot = std::fopen(path, "wb"); Check(snapshot != nullptr); }
    for (int kind = 0; kind < 4; ++kind)
        for (unsigned length : {0U,1U,31U,255U})
            for (int malformed = 0; malformed < 5; ++malformed)
                for (int readCount = 0; readCount < 2; ++readCount)
                    for (int path = 0; path < 3; ++path)
                    {
                        byte bytes[1024] = {}, stored[1024]; unsigned count = 0;
                        const byte system = kind == 0 ? 3 : kind == 1 ? 2 : kind == 2 ? 5 : 4;
                        bytes[count++] = system; bytes[count++] = system;
                        if (kind < 2) bytes[count++] = static_cast<byte>(length);
                        else { bytes[count++] = kind == 3 ? 0 : static_cast<byte>(length); bytes[count++] = kind == 3 ? static_cast<byte>(length) : 0; }
                        for (unsigned i = 0; i < length; ++i)
                        {
                            byte ch = i % 7 == 0 ? (kind == 1 ? 0x8e : 0xe9) : static_cast<byte>('a' + i % 26);
                            if (kind < 2) bytes[count++] = ch;
                            else { bytes[count++] = kind == 3 ? 0 : ch; bytes[count++] = kind == 3 ? ch : 0; }
                        }
                        bytes[count++] = 0; if (kind >= 2) bytes[count++] = 0;
                        if (malformed == 1) --count;
                        if (malformed == 2) bytes[count - 1] = 1;
                        if (malformed == 3) { if (length) bytes[kind < 2 ? 3 : 4] = bytes[kind < 2 ? 3 : 5] = 0; else bytes[0] = 0xff; }
                        if (malformed == 4) bytes[count++] = 0xaa;
                        String value(PszLit("initial")); LegacyLong consumed = 12345; bool ok;
                        if (!path) ok = value.FSetData(bytes, count, readCount ? &consumed : nullptr);
                        else
                        {
                            DataBlock block; Check(block.FSetTemp(7 + count, path == 2));
                            Check(block.FWriteRgb(bytes, count, 7));
                            ok = value.FRead(&block, 7, readCount ? &consumed : nullptr);
                        }
                        value.GetData(stored);
                        LegacyLong record[] = {kind, static_cast<LegacyLong>(length), malformed, readCount, path, ok,
                            ok && readCount ? consumed : -1, value.Cch(), value.CbData()};
                        if (snapshot)
                        {
                            Check(std::fwrite(record, sizeof(record), 1, snapshot) == 1);
                            Check(std::fwrite(stored, 1, value.CbData(), snapshot) == static_cast<size_t>(value.CbData()));
                        }
                    }
    if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestCommandReplay()
{
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    s_replayTarget = NewObj ReplayTarget();
    Check(file != pvNil && s_replayTarget != pvNil);
    SetCmdHooks(ReplayLookup, pvNil);
    vpcex->Record(file);
    Check(vpcex->FRecording());
    Command command = {};
    command.cid = 0x10001;
    command.pcmh = s_replayTarget;
    command.rglw[0] = 37;
    vpcex->RecordCmd(&command);
    vpcex->RecordCmd(&command); // Exercise the repeated-command record path too.
    vpcex->StopRecording();
    Check(!vpcex->FRecording() && s_replayTarget->calls == 0);
    vpcex->FlushCid(cidCexRecordDone);
    ChunkIdentification recorded;
    Check(file->FGetCkiCtg(kctgMacro, 0, &recorded));
    ReleasePpo(&s_replayTarget);
    s_replayTarget = NewObj ReplayTarget(); // Reconstruct destination from HID, not the recorded object pointer.
    Check(s_replayTarget != pvNil);
    vpcex->Play(file, recorded.cno);
    Check(vpcex->FPlaying());
    for (LegacyLong attempt = 0; attempt < 8 && vpcex->FPlaying(); ++attempt)
    {
        vpcex->EnqueueCid(cidIdle);
        vpcex->FDispatchNextCmd();
    }
    Check(!vpcex->FPlaying() && s_replayTarget->calls == 2 && s_replayTarget->sum == 74);
    vpcex->FlushCid(cidIdle);
    vpcex->FlushCid(cidCexPlayDone);
    SetCmdHooks(pvNil, pvNil);
    ReleasePpo(&s_replayTarget);
    // StopRecording persists the file. Return this test-owned temp file to
    // automatic cleanup before releasing its final owner.
    file->SetTemp(true);
    ReleasePpo(&file);
}

class MemoryStreamProbe : public MemoryByteStream
{
  public:
    ThreeMMU32 Capacity() const { return _buffer.storage.buffers[0].capacity; }
};

static void TestDataStreams()
{
    byte source[4096], actual[4100];
    for (unsigned i = 0; i < sizeof(source); ++i) source[i] = static_cast<byte>(i % 7);
    DataBlock block;
    for (LegacyLong format : {kcfmtKauai, kcfmtKauai2})
    {
        Check(block.FSetTemp(sizeof(source)) && block.FWrite(source));
        Check(block.FPackData(format) && block.FPacked());
        Check(block.FUnpackData() && !block.FPacked() && block.Cb() == sizeof(source));
        Check(block.FRead(actual) && !std::memcmp(actual, source, sizeof(source)));
    }
    LegacyLong untouchedCount = 123;
    Check(!vpcodmUtil->FCompress(source, 9, pvNil, 0, &untouchedCount) && untouchedCount == 123);
#ifdef DEBUG
    KauaiCodec codec;
    for (int failure = 0; failure < 2; ++failure)
    {
        const auto initialAllocations = vdmglob.dmaglPv.cv;
        vdmglob.dmaglPv.cactDo = failure;
        vdmglob.dmaglPv.cactFail = 1;
        LegacyLong count;
        const bool ok = codec.FConvert(true, kcfmtKauai2, source, sizeof(source), actual, sizeof(actual), &count);
        const bool consumed = vdmglob.dmaglPv.cactFail == 0;
        vdmglob.dmaglPv.cactFail = vdmglob.dmaglPv.cactDo = 0;
        Check(!ok && consumed && vdmglob.dmaglPv.cv == initialAllocations);
    }
#endif

    // Unknown packed format must retain the original owning handle on failure.
    byte invalid[] = {'n','o','n','e', 0,0,0,4, 1,2,3,4};
    HQ handle = pvNil;
    Check(FAllocHq(&handle, sizeof(invalid), fmemNil, mprNormal));
    CopyPb(invalid, PvLockHq(handle), sizeof(invalid));
    UnlockHq(handle);
    block.SetHq(&handle, true);
    Check(handle == pvNil && !block.FUnpackData() && block.FPacked());
    Check(block.Cb(true) == sizeof(invalid) && block.FRead(actual, true));
    Check(!std::memcmp(actual, invalid, sizeof(invalid)));
    Check(block.FMoveMin(1) && !block.FUnpackData() && block.FPacked());
    Check(block.Cb(true) == sizeof(invalid) - 1 && block.FRead(actual, true));
    Check(!std::memcmp(actual, invalid + 1, sizeof(invalid) - 1));

    auto file = FileObject::PfilCreateTemp();
    Check(file && file->FWriteRgb(invalid, sizeof(invalid), 0));
    block.Set(file, 0, sizeof(invalid), true);
    Check(!block.FUnpackData() && block.FPacked());
    FileLocation location;
    Check(block.FGetFlo(&location, true) && location.pfil == file && location.cb == sizeof(invalid));
    Check(block.FRead(actual, true) && !std::memcmp(actual, invalid, sizeof(invalid)));
    block.Free();
    Check(file->FWriteRgb(source, sizeof(source), 0));

    auto memory = NewObj MemoryStreamProbe();
    Check(memory != pvNil);
    Check(memory->PvLock(0) == pvNil);
    memory->Unlock();
    memory->SetMinGrow(16);
    Check(memory->FReplace(source, 4, 0, 0) && memory->Capacity() == 16);
    // Preserve the legacy nonzero shrink request's growth-hint behavior.
    Check(memory->FEnsureSpace(0, true) && memory->Capacity() == 32);
    Check(memory->FReplace(source + 4, 3, 1, 2) && memory->IbMac() == 5);
    const byte expected[] = {0,4,5,6,3};
    memory->FetchRgb(0, 5, actual);
    Check(!std::memcmp(actual, expected, sizeof(expected)));
    Check(*static_cast<byte *>(memory->PvLock(1)) == 4);
    memory->Unlock();
    Check(block.FSetTemp(3) && memory->FWriteRgb(&block, 2));
    Check(block.FRead(actual) && !std::memcmp(actual, expected + 2, 3));
    ReleasePpo(&memory);

    auto pieces = NewObj FileByteStream();
    Check(pieces && pieces->FReplace(source, 2, 0, 0));
    location = {file, 0, sizeof(source)};
    Check(pieces->FReplaceFlo(&location, false, 2, 0));
    Check(pieces->FReplace(source + 2, 2, pieces->IbMac(), 0));
    ReleasePpo(&file); // The stream owns its file piece independently.
    pieces->FetchRgb(0, pieces->IbMac(), actual);
    Check(pieces->IbMac() == 4100 && actual[0] == 0 && actual[1] == 1);
    Check(!std::memcmp(actual + 2, source, sizeof(source)) && actual[4098] == 2 && actual[4099] == 3);
    Check(pieces->FReplace(source + 4, 3, 1, 4));
    pieces->FetchRgb(0, 8, actual);
    Check(actual[0] == 0 && actual[1] == 4 && actual[2] == 5 && actual[3] == 6 && actual[4] == source[3]);
    Check(pieces->FCompact());
    pieces->FetchRgb(pieces->IbMac(), 0, actual);
    ReleasePpo(&pieces);
}

static void TestBlockCopies()
{
    byte bytes[8208], seed[8208], actual[8208];
    for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = static_cast<byte>(i * 29 + i / 256);
    std::memset(seed, 0xcc, sizeof(seed));
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_BLOCK_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb"); Check(snapshot != nullptr);
    }
    for (LegacyLong size : {0L, 1L, 1024L, 1025L, 4096L})
        for (int shape = 0; shape < 4; ++shape)
            for (int flags = 0; flags < 4; ++flags)
            {
                DataBlock source, target;
                PFileObject from = nullptr, to = nullptr;
                HQ memory = nullptr, destinationMemory = nullptr;
                if (shape & 1)
                {
                    from = FileObject::PfilCreateTemp();
                    Check(from && from->FWriteRgb(bytes, size + 10, 0));
                    source.Set(from, 3, size, flags & 1);
                }
                else
                {
                    Check(FAllocHq(&memory, size + 10, fmemNil, mprNormal));
                    CopyPb(bytes, PvLockHq(memory), size + 10); UnlockHq(memory);
                    source.SetHq(&memory, flags & 1);
                    Check(source.FMoveMin(3) && source.FMoveLim(-7));
                }
                if (shape & 2)
                {
                    to = FileObject::PfilCreateTemp();
                    Check(to && to->FWriteRgb(seed, size + 12, 0));
                    target.Set(to, 5, size, flags & 2);
                }
                else
                {
                    Check(FAllocHq(&memory, size + 12, fmemNil, mprNormal));
                    CopyPb(seed, PvLockHq(memory), size + 12); UnlockHq(memory);
                    destinationMemory = memory;
                    target.SetHq(&memory, flags & 2);
                    Check(target.FMoveMin(5) && target.FMoveLim(-7));
                }
                Check(source.FWriteToBlck(&target, true));
                Check(target.FPacked() == !!(flags & 2) && source.FPacked() == !!(flags & 1));
                Check(source.FReadHq(&memory, true));
                Check(memory && CbOfHq(memory) == size);
                Check(target.FWriteHq(memory, 0, true)); FreePhq(&memory);
                if (to) Check(to->FReadRgb(actual, size + 12, 0));
                else CopyPb(QvFromHq(destinationMemory), actual, size + 12);
                Check(!std::memcmp(actual, seed, 5));
                Check(!std::memcmp(actual + 5, bytes + 3, size));
                Check(!std::memcmp(actual + size + 5, seed, 7));
                if (snapshot) Check(std::fwrite(actual, 1, size + 12, snapshot) == static_cast<size_t>(size + 12));
                memory = source.HqFree(true);
                Check(memory && CbOfHq(memory) == size);
                Check(!std::memcmp(QvFromHq(memory), bytes + 3, size));
                FreePhq(&memory);
                Check(source.Cb(true) == ((shape & 1) ? size : 0));
                ReleasePpo(&from); ReleasePpo(&to);
            }
    if (snapshot) Check(std::fclose(snapshot) == 0);
    for (LegacyLong format : {kcfmtKauai, kcfmtKauai2})
    {
        HQ memory;
        Check(FAllocHq(&memory, 4096, fmemNil, mprNormal));
        CopyPb(bytes, QvFromHq(memory), 4096);
        Check(vpcodmUtil->FCompressPhq(&memory, format));
        Check(vpcodmUtil->FDecompressPhq(&memory) && CbOfHq(memory) == 4096);
        Check(!std::memcmp(QvFromHq(memory), bytes, 4096)); FreePhq(&memory);
        DataBlock slice;
        Check(slice.FSetTemp(4096) && slice.FWrite(bytes));
        Check(slice.FMoveMin(3) && slice.FMoveLim(-97) && slice.FPackData(format));
        Check(slice.FUnpackData() && slice.Cb() == 3996 && slice.FRead(actual));
        Check(!std::memcmp(actual, bytes + 3, 3996));
        DataBlock fileBlock;
        Check(fileBlock.FSetTemp(4096, true) && fileBlock.FWrite(bytes));
        Check(fileBlock.FPackData(format) && fileBlock.FUnpackData() && fileBlock.Cb() == 4096);
        Check(fileBlock.FRead(actual) && !std::memcmp(actual, bytes, 4096));
    }
    DataBlock shortSlice;
    Check(shortSlice.FSetTemp(32) && shortSlice.FWriteRgb(bytes, 32, 0));
    Check(shortSlice.FMoveMin(3) && shortSlice.FMoveLim(-21));
    Check(!shortSlice.FPackData() && !shortSlice.FPacked() && shortSlice.Cb() == 8);
    Check(shortSlice.CbMem() == 8 && shortSlice.FRead(actual) && !std::memcmp(actual, bytes + 3, 8));
    DataBlock shortFile;
    Check(shortFile.FSetTemp(8, true) && shortFile.FWriteRgb(bytes, 8, 0));
    Check(!shortFile.FPackData() && !shortFile.FPacked() && shortFile.Cb() == 8 && shortFile.CbMem() == 0);
#ifdef DEBUG
    HQ original, memory;
    Check(FAllocHq(&memory, 4096, fmemNil, mprNormal)); original = memory;
    CopyPb(bytes, QvFromHq(memory), 4096);
    vdmglob.dmaglHq.cactDo = 0; vdmglob.dmaglHq.cactFail = 1;
    const bool coded = vpcodmUtil->FCompressPhq(&memory);
    const bool consumed = vdmglob.dmaglHq.cactFail == 0;
    vdmglob.dmaglHq.cactFail = vdmglob.dmaglHq.cactDo = 0;
    Check(!coded && consumed && memory == original && CbOfHq(memory) == 4096);
    Check(!std::memcmp(QvFromHq(memory), bytes, 4096)); FreePhq(&memory);
    auto from = FileObject::PfilCreateTemp(), to = FileObject::PfilCreateTemp();
    Check(from && to && from->FWriteRgb(bytes, 4096, 0));
    FileLocation source = {from, 0, 4096}, destination = {to, 0, 4096};
    vdmglob.dmaglPv.cactDo = 0; vdmglob.dmaglPv.cactFail = 1;
    const bool copied = source.FCopy(&destination);
    const bool fallbackConsumed = vdmglob.dmaglPv.cactFail == 0;
    vdmglob.dmaglPv.cactFail = vdmglob.dmaglPv.cactDo = 0;
    Check(copied && fallbackConsumed && to->FReadRgb(actual, 4096, 0));
    Check(!std::memcmp(actual, bytes, 4096)); ReleasePpo(&from); ReleasePpo(&to);
#endif
}

static void TestBlockMaterialization()
{
    const LegacyLong threshold = 1L << 23;
    byte bytes[4096], actual[4096];
    for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = static_cast<byte>(i * 19);
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_MATERIALIZE_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb"); Check(snapshot != nullptr);
    }
    for (LegacyLong size : {0L, 1L, 4096L, threshold - 1, threshold, threshold + 1})
        for (int force = 0; force < 2; ++force)
            for (int packed = 0; packed < 2; ++packed)
            {
                HQ seed;
                Check(FAllocHq(&seed, 8, fmemClear, mprNormal));
                DataBlock block(&seed, packed != 0);
                Check(block.FSetTemp(size, force != 0) && block.Cb(true) == size && block.FPacked() == !!packed);
                const bool fileExpected = force || size >= threshold;
                Check(block.CbMem() == (fileExpected ? 0 : size));
                FileLocation materialized = {};
                if (size <= 4096)
                {
                    Check(block.FWriteRgb(bytes, size, 0, true));
                    if (size == 4096) Check(block.FMoveMin(1) && block.FMoveLim(-2));
                }
                LegacyLong physical = -1;
                if (size <= 4096 || fileExpected)
                {
                    Check(block.FGetFlo(&materialized, true));
                    Check(!!materialized.pfil == (fileExpected || size != 0));
                    if (materialized.pfil)
                    {
                        physical = materialized.pfil->FpMac();
                        Check(materialized.cb == block.Cb(true));
                        if (size <= 4096) Check(materialized.FRead(actual) && !std::memcmp(actual, bytes + (size == 4096), materialized.cb));
                    }
                }
                LegacyLong record[] = {size, force, packed, block.Cb(true), block.CbMem(), !!materialized.pfil,
                                       materialized.fp, materialized.cb, physical};
                if (snapshot)
                {
                    Check(std::fwrite(record, sizeof(record), 1, snapshot) == 1);
                    if (materialized.pfil && size <= 4096)
                        Check(std::fwrite(actual, 1, materialized.cb, snapshot) == static_cast<size_t>(materialized.cb));
                }
                block.Free();
                if (materialized.pfil)
                {
                    if (size <= 4096) Check(materialized.FRead(actual)); // Returned reference survives Free.
                    ReleasePpo(&materialized.pfil);
                }
            }
    if (snapshot) Check(std::fclose(snapshot) == 0);
#ifdef DEBUG
    DataBlock fallback;
    Check(fallback.FSetTemp(4) && fallback.FWriteRgb(bytes, 4, 0));
    vdmglob.dmaglHq.cactDo = 0; vdmglob.dmaglHq.cactFail = 1;
    const bool created = fallback.FSetTemp(17);
    const bool consumed = vdmglob.dmaglHq.cactFail == 0;
    vdmglob.dmaglHq.cactFail = vdmglob.dmaglHq.cactDo = 0;
    Check(created && consumed && fallback.Cb() == 17 && fallback.CbMem() == 0);
    FileLocation region;
    Check(fallback.FGetFlo(&region) && region.pfil->FpMac() == 0); // Temp selection does not extend the file.
    ReleasePpo(&region.pfil);
#endif
}

static void TestFilePolicies()
{
    byte input[8192], actual[8192], original[8192];
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_FILE_POLICY_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb"); Check(snapshot != nullptr);
    }
    for (int kind = 0; kind < 6; ++kind)
        for (LegacyLong units : {0L,1L,2L,127L,256L,257L,768L,1025L})
            for (short requested : {short(oskNil),koskSbMac,koskSbWin,koskUniMac,koskUniWin})
            {
                LegacyLong size = 0;
                if (kind == 2 || kind == 3 || kind == 5)
                {
                    input[size++] = kind == 3 ? 0xfe : 0xff;
                    input[size++] = kind == 3 ? 0xff : 0xfe;
                }
                for (LegacyLong i = 0; i < units; ++i)
                {
                    byte value = i % 7 == 0 ? (kind == 1 ? 0x8e : 0xe9) : static_cast<byte>('A' + i % 26);
                    if (kind < 2) input[size++] = value;
                    else if (kind == 3) { input[size++] = 0; input[size++] = value; }
                    else { input[size++] = value; input[size++] = 0; }
                }
                if (kind == 5) input[size++] = 0x7f; // Odd total: the legacy BOM probe is skipped.
                auto file = FileObject::PfilCreateTemp();
                Check(file && file->FWriteRgb(PszLit("prefix-"), 7, 0) && file->FWriteRgb(input, size, 7));
                FileLocation location = {file, 7, size};
                file->AddRef(); // location owns a reference that FTranslate may replace.
                Check(location.FTranslate(requested));
                Check(location.cb < sizeof(actual) && location.FRead(actual));
                Check(file->FReadRgb(original, size, 7) && !std::memcmp(original, input, size));
                LegacyLong record[] = {kind, units, requested, location.fp, location.cb, location.pfil == file, location.pfil->FpMac()};
                if (snapshot)
                {
                    Check(std::fwrite(record, sizeof(record), 1, snapshot) == 1);
                    Check(std::fwrite(actual, 1, location.cb, snapshot) == static_cast<size_t>(location.cb));
                }
                ReleasePpo(&location.pfil); ReleasePpo(&file);
            }
    auto file = FileObject::PfilCreateTemp();
    auto stream = NewObj FileByteStream();
    Check(file && stream);
    for (LegacyLong i = 0; i < 4096; ++i) input[i] = static_cast<byte>(i * 11);
    Check(file->FWriteRgb(input, 4096, 0));
    FileLocation piece = {file, 0, 4096};
    Check(stream->FReplace(PszLit("front"), 5, 0, 0) && stream->FReplaceFlo(&piece, false, 5, 0));
    Check(stream->FReplace(PszLit("endings"), 7, stream->IbMac(), 0));
    byte model[4108]; stream->FetchRgb(0, sizeof(model), model);
    struct Range { LegacyLong offset, count; };
    for (const auto &range : {Range{0,4108}, Range{2,4106}, Range{5,4096}, Range{1,3}, Range{12,1024}, Range{0,5}, Range{4101,7}, Range{3,4098}})
        for (int force = 0; force < 2; ++force)
        {
            DataBlock target;
            std::memset(input, 0xcc, range.count + 17);
            Check(target.FSetTemp(range.count + 17, force != 0) && target.FWrite(input));
            Check(target.FMoveMin(11) && target.FMoveLim(-6));
            Check(stream->FWriteRgb(&target, range.offset) && target.Cb() == range.count);
            Check(target.FMoveMin(-11) && target.FMoveLim(6) && target.FRead(actual));
            Check(!std::memcmp(actual, input, 11) && !std::memcmp(actual + 11, model + range.offset, range.count));
            Check(!std::memcmp(actual + 11 + range.count, input, 6));
            LegacyLong record[] = {range.offset, range.count, force, target.Cb()};
            if (snapshot)
            {
                Check(std::fwrite(record, sizeof(record), 1, snapshot) == 1);
                Check(std::fwrite(actual, 1, target.Cb(), snapshot) == static_cast<size_t>(target.Cb()));
            }
        }
    ReleasePpo(&stream); ReleasePpo(&file);
    if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestPieceEdits()
{
    byte fileBytes[8192], model[30000], actual[30000], insert[16];
    for (unsigned i = 0; i < sizeof(fileBytes); ++i) fileBytes[i] = static_cast<byte>(i * 37 + i / 256);
    auto file = FileObject::PfilCreateTemp();
    auto stream = NewObj FileByteStream();
    auto source = NewObj FileByteStream();
    Check(file && stream && source && file->FWriteRgb(fileBytes, sizeof(fileBytes), 0));
    FileLocation whole = {file, 0, sizeof(fileBytes)};
    FileLocation small = {file, 47, 31};
    Check(source->FReplace(fileBytes, 13, 0, 0) && source->FReplaceFlo(&whole, false, 13, 0));
    Check(source->FReplace(fileBytes + 20, 11, source->IbMac(), 0));
    byte sourceBytes[sizeof(fileBytes) + 24];
    source->FetchRgb(0, source->IbMac(), sourceBytes);
    LegacyLong length = 0;
    unsigned state = 12345;
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_PIECE_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        Check(snapshot != nullptr);
    }
    for (int step = 0; step < 180; ++step)
    {
        state = state * 1664525u + 1013904223u;
        LegacyLong offset = state % (length + 1);
        LegacyLong remove = length ? (state >> 16) % (length - offset + 1) : 0;
        LegacyLong count;
        const byte *bytes;
        if (step % 7 == 0)
        {
            FileLocation piece = {file, static_cast<LegacyLong>(step % 256), 2048};
            count = piece.cb; bytes = fileBytes + piece.fp;
            Check(stream->FReplaceFlo(&piece, step % 2 != 0, offset, remove));
        }
        else if (step % 7 == 1)
        {
            count = small.cb; bytes = fileBytes + small.fp;
            Check(stream->FReplaceFlo(&small, true, offset, remove));
        }
        else if (step % 7 == 2)
        {
            LegacyLong from = step % 17, amount = 4096 + step;
            count = amount; bytes = sourceBytes + from;
            Check(stream->FReplaceBsf(source, from, amount, offset, remove));
        }
        else
        {
            count = step % 16;
            for (LegacyLong i = 0; i < count; ++i) insert[i] = static_cast<byte>(step + i);
            bytes = insert;
            Check(stream->FReplace(insert, count, offset, remove));
        }
        Check(length - remove + count < sizeof(model));
        std::memmove(model + offset + count, model + offset + remove, length - offset - remove);
        std::memcpy(model + offset, bytes, count);
        length += count - remove;
        Check(stream->IbMac() == length);
        stream->FetchRgb(0, length, actual);
        Check(!std::memcmp(model, actual, length));
        if (snapshot)
        {
            Check(std::fwrite(&length, sizeof(length), 1, snapshot) == 1);
            Check(std::fwrite(actual, 1, length, snapshot) == static_cast<size_t>(length));
        }
    }
    if (snapshot) Check(std::fclose(snapshot) == 0);
    Check(stream->FCompact());
    stream->FetchRgb(0, length, actual);
    Check(!std::memcmp(model, actual, length));
    Check(stream->FCompact()); // Already a single file piece.
    Check(source->FCompact() && source->FCompact());
    source->FetchRgb(0, source->IbMac(), actual);
    Check(!std::memcmp(sourceBytes, actual, sizeof(sourceBytes)));
    auto self = NewObj FileByteStream();
    byte selfBytes[] = {'a','b','c','d','e','f'};
    Check(self && self->FReplace(selfBytes, 6, 0, 0) && self->FReplaceBsf(self, 0, 3, 6, 0));
    self->FetchRgb(0, 9, actual);
    Check(!std::memcmp(actual, "abcdefabc", 9));
    ReleasePpo(&self);
    ReleasePpo(&stream); ReleasePpo(&source); ReleasePpo(&file);
}

static void TestFailedResize()
{
#ifdef DEBUG
    byte *bytes = pvNil;
    Check(FAllocPv(reinterpret_cast<void **>(&bytes), 16, fmemClear, mprNormal));
    bytes[0] = 17;
    vmutxMem.Enter();
    vdmglob.dmaglPv.cactDo = 0;
    vdmglob.dmaglPv.cactFail = 1;
    vmutxMem.Leave();
    bool resized = _FResizePpvDebug(reinterpret_cast<void **>(&bytes), 32, 16, fmemClear, mprNormal, &vdmglob.dmaglPv);
    vmutxMem.Enter();
    vdmglob.dmaglPv.cactFail = 0;
    vmutxMem.Leave();
    Check(!resized && bytes[0] == 17);
    AssertPvAlloced(bytes, 16);
    FreePpv(reinterpret_cast<void **>(&bytes));
#endif
}

class CollectionStorageProbe : public DynamicArray
{
  public:
    CollectionStorageProbe() : DynamicArray(1) {}
    bool Ensure(LegacyLong first, LegacyLong second, LegacyUlong flags = fgrpNil)
    {
        return _FEnsureSizes(first, second, flags);
    }
    LegacyLong Capacity(unsigned section) { return section ? _Cb2() : _Cb1(); }
    byte *Bytes(unsigned section) { return section ? _Qb2(0) : _Qb1(0); }
    void Growth(LegacyLong first, LegacyLong second) { _cbMinGrow1 = first; _cbMinGrow2 = second; }
};

static void TestCollectionStorage()
{
#ifdef DEBUG
    const auto initialAllocations = vdmglob.dmaglHq.cv;
#endif
    auto probe = NewObj CollectionStorageProbe();
    Check(probe != pvNil);
    probe->Growth(16, 8);
    Check(probe->Ensure(4, 2));
    Check(probe->Capacity(0) == 16 && probe->Capacity(1) == 8);
    probe->Bytes(0)[0] = 37;
    probe->Bytes(1)[0] = 91;
#ifdef DEBUG
    vdmglob.dmaglHq.cactDo = 0;
    vdmglob.dmaglHq.cactFail = 1;
    const bool fallback = probe->Ensure(20, 2);
    const bool fallbackConsumed = vdmglob.dmaglHq.cactFail == 0;
    vdmglob.dmaglHq.cactFail = 0;
    Check(fallback && fallbackConsumed && probe->Capacity(0) == 20);
    probe->Growth(0, 0);
    vdmglob.dmaglHq.cactDo = 1;
    vdmglob.dmaglHq.cactFail = 1;
    const bool secondFailed = !probe->Ensure(40, 12);
    const bool failureConsumed = vdmglob.dmaglHq.cactFail == 0;
    vdmglob.dmaglHq.cactFail = vdmglob.dmaglHq.cactDo = 0;
    Check(secondFailed && failureConsumed);
    Check(probe->Capacity(0) == 40 && probe->Capacity(1) == 8);
#endif
    Check(probe->Bytes(0)[0] == 37 && probe->Bytes(1)[0] == 91);
    probe->Lock();
    Check(probe->Bytes(0)[0] == 37);
    probe->Unlock();
    Check(probe->Ensure(4, 2, fgrpShrink));
    Check(probe->Capacity(0) == 4 && probe->Capacity(1) == 2);
    ReleasePpo(&probe);

    auto group = GeneralGroup::PggNew(sizeof(LegacyLong));
    Check(group != pvNil);
    LegacyLong fixed = 17;
    byte variable[] = {1, 2, 3};
    Check(group->FAdd(sizeof(variable), pvNil, variable, &fixed));
    auto duplicate = group->PggDup();
    Check(duplicate != pvNil);
    byte changed[] = {8, 9, 10, 11, 12};
    Check(duplicate->FPut(0, sizeof(changed), changed));
    Check(group->Cb(0) == 3 && duplicate->Cb(0) == 5);
    byte observed[3];
    group->Get(0, observed);
    Check(!std::memcmp(observed, variable, sizeof(variable)));
    duplicate->GetFixed(0, &fixed);
    Check(fixed == 17);
    ReleasePpo(&duplicate);
    ReleasePpo(&group);
#ifdef DEBUG
    Check(vdmglob.dmaglHq.cv == initialAllocations);
#endif
}

static void TestCollectionWireAndMutations()
{
    auto file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    auto list = DynamicArray::PglNew(sizeof(LegacyLong));
    auto allocated = AllocatedArray::PalNew(sizeof(LegacyLong));
    auto group = GeneralGroup::PggNew(1);
    Check(list && allocated && group);
    LegacyLong values[] = {11, 22, 33, 44};
    Check(list->FInsert(0, values, 4));
    list->Move(0, 4);
    list->Delete(1, 2);
    LegacyLong value;
    list->Get(0, &value); Check(value == 22);
    list->Get(1, &value); Check(value == 11);
    for (LegacyLong i = 0; i < 12; ++i) Check(allocated->FAdd(&i));
    allocated->Delete(8);
    allocated->Delete(3);
    LegacyLong position;
    Check(allocated->FAdd(values, &position) && position == 3);
    byte first = 1, second = 2;
    byte sourceBytes[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    byte targetBytes[] = {'1', '2', '3'};
    Check(group->FAdd(sizeof(sourceBytes), pvNil, sourceBytes, &first));
    Check(group->FAdd(sizeof(targetBytes), pvNil, targetBytes, &second));
    Check(group->FMoveRgb(0, 1, 1, 1, 5));
    byte bytes[12];
    Check(group->Cb(0) == 3 && group->Cb(1) == 8);
    group->Get(0, bytes); Check(!std::memcmp(bytes, "agh", 3));
    group->Get(1, bytes); Check(!std::memcmp(bytes, "1bcdef23", 8));
    group->Merge(1, 0);
    Check(group->IvMac() == 1 && group->Cb(0) == 11);
    group->Get(0, bytes); Check(!std::memcmp(bytes, "agh1bcdef23", 11));
    for (int foreign = 0; foreign < 2; ++foreign)
    {
        const short order = foreign ? kboOther : kboCur;
        for (int kind = 0; kind < 3; ++kind)
        {
            PGroupBase source = kind == 0 ? static_cast<PGroupBase>(list) :
                                kind == 1 ? static_cast<PGroupBase>(allocated) : static_cast<PGroupBase>(group);
            DataBlock block;
            ChunkNumber number;
            Check(file->FAdd(source->CbOnFile(), 'COLL', &number, &block));
            Check(source->FWrite(&block, order, koskSbMac));
            short observedOrder, observedSystem;
            if (kind == 0)
            {
                auto copy = DynamicArray::PglRead(&block, &observedOrder, &observedSystem);
                Check(copy != pvNil && copy->IvMac() == 2);
                copy->Get(1, &value); Check(value == 11);
                ReleasePpo(&copy);
            }
            else if (kind == 1)
            {
                auto copy = AllocatedArray::PalRead(&block, &observedOrder, &observedSystem);
                Check(copy != pvNil && copy->IvMac() == 12 && copy->FFree(8));
                copy->Get(3, &value); Check(value == 11);
                ReleasePpo(&copy);
            }
            else
            {
                auto copy = GeneralGroup::PggRead(&block, &observedOrder, &observedSystem);
                Check(copy != pvNil && copy->Cb(0) == 11);
                copy->Get(0, bytes); Check(!std::memcmp(bytes, "agh1bcdef23", 11));
                copy->GetFixed(0, &first); Check(first == 1);
                ReleasePpo(&copy);
            }
            Check(observedOrder == order && observedSystem == koskSbMac);
        }
    }
    // Foreign-endian header, opaque payload, and deliberately non-invariant OS
    // marker: the legacy API returns its raw native interpretation before swap.
    byte wire[] = {0,1,0x12,0x34, 0,0,0,1, 0,0,0,1, 0x7f};
    ChunkNumber number;
    Check(file->FAddPv(wire, sizeof(wire), 'COLL', &number));
    DataBlock block;
    Check(file->FFind('COLL', number, &block));
    short order, system;
    auto decoded = DynamicArray::PglRead(&block, &order, &system);
    Check(decoded != pvNil);
    short expectedSystem;
    std::memcpy(&expectedSystem, wire + 2, sizeof(expectedSystem));
    Check(system == expectedSystem);
    ReleasePpo(&decoded);
    ReleasePpo(&list); ReleasePpo(&allocated); ReleasePpo(&group); ReleasePpo(&file);
}

static uint32_t s_systemTicks;
static uint32_t ControlledSystemTicks()
{
    return s_systemTicks;
}

static void TestPortableClockAndMemory()
{
    auto previous = KauaiPlatform::SetTickSource(ControlledSystemTicks);
    s_systemTicks = 0xfffffff0;
    {
        UniversalScalableApplicationClock clock;
        Check(clock.TsCur() == 0);
        s_systemTicks = 0x10; // Preserve unsigned 32-bit wraparound.
        Check(clock.TsCur() == 32);
        clock.Scale(kluTimeScaleNormal / 2);
        s_systemTicks += 20;
        Check(clock.TsCur() == 42);
        clock.Jump(7);
        Check(clock.TsCur() == 49);
    }
    KauaiPlatform::SetTickSource(previous);

    HQ handle;
    Check(FAllocHq(&handle, 4, fmemClear, mprNormal));
    byte *bytes = static_cast<byte *>(PvLockHq(handle));
    Check(bytes[0] == 0 && bytes[3] == 0);
    bytes[0] = 27;
    UnlockHq(handle);
    Check(FResizePhq(&handle, 16, fmemClear, mprNormal));
    bytes = static_cast<byte *>(PvLockHq(handle));
    Check(bytes[0] == 27 && bytes[4] == 0 && bytes[15] == 0);
    UnlockHq(handle);
    Check(FResizePhq(&handle, 0, fmemNil, mprNormal) && CbOfHq(handle) == 0);
    FreePhq(&handle);
    Check(handle == hqNil);
}

static void TestRichTextPersistence()
{
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    RichTextData source;
    source.fontName.SetSzs(PszLit("Arial"));
    source.defaults.dxpDef = 240;
    source.defaults.dypFont = 16;
    source.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
    source.arguments = AllocatedGroup::PagNew(sizeof(int32_t));
    Check(source.properties != pvNil && source.arguments != pvNil);
    RichTextPropertyEntry entry = {0x02000000, 21};
    Check(source.properties->FAdd(&entry));
    int32_t fixed = 3;
    byte argument[] = {3, 1, 4};
    Check(source.arguments->FAdd(sizeof(argument), pvNil, argument, &fixed));
    source.text.pfil = FileObject::PfilCreateTemp();
    Check(source.text.pfil != pvNil);
    byte caption[] = {'H', 'e', 'l', 'l', 'o'};
    source.text.cb = sizeof(caption);
    Check(source.text.pfil->FSetFpMac(source.text.cb));
    Check(source.text.FWriteRgb(caption, sizeof(caption), 0));
    for (int foreign = 0; foreign < 2; ++foreign)
    {
        if (foreign)
        {
            source.propertyBo = source.argumentBo = kboOther;
            source.propertyOsk = koskSbMac;
            SwapBytesRglw(&entry, 2);
            source.properties->Put(0, &entry);
            SwapBytesRglw(&fixed, 1);
            source.arguments->PutFixed(0, &fixed);
        }
        ChunkIdentification root;
        Check(source.FWrite(file, &root));
        if (foreign)
        {
            DataBlock block;
            RichTextProperties header;
            Check(file->FFind(root.ctg, root.cno, &block));
            Check(block.FReadRgb(&header, sizeof(header), 0));
            SwapBytesRgsw(&header, 2);
            SwapBytesRglw(reinterpret_cast<byte *>(&header) + 4, 3);
            Check(block.FWriteRgb(&header, sizeof(header), 0));
        }
        RichTextData loaded;
        Check(loaded.FRead(file, root.ctg, root.cno));
        Check(loaded.defaults.dxpDef == 240 && loaded.defaults.dypFont == 16);
        Check(loaded.fontName.FEqualSz(PszLit("Arial")));
        Check(loaded.propertyOsk == source.propertyOsk);
        byte text[5];
        Check(loaded.text.FReadRgb(text, sizeof(text), 0) && std::memcmp(text, caption, sizeof(text)) == 0);
        RichTextPropertyEntry got;
        loaded.properties->Get(0, &got);
        if (loaded.propertyBo == kboOther)
            SwapBytesRglw(&got, 2);
        Check(got.spcp == 0x02000000 && got.lw == 21);
        int32_t gotFixed;
        loaded.arguments->GetFixed(0, &gotFixed);
        if (loaded.argumentBo == kboOther)
            SwapBytesRglw(&gotFixed, 1);
        Check(gotFixed == 3);
        byte gotArg[3];
        loaded.arguments->Get(0, gotArg);
        Check(std::memcmp(gotArg, argument, sizeof(argument)) == 0);
        ChunkIdentification copy;
        Check(loaded.FWrite(file, &copy));
        RichTextData reread;
        Check(reread.FRead(file, copy.ctg, copy.cno) && reread.text.cb == 5);
        // Missing TEXT child must leave existing loaded data intact.
        Check(!loaded.FRead(file, root.ctg, root.cno + 100000));
        Check(loaded.text.cb == 5 && loaded.fontName.FEqualSz(PszLit("Arial")));
        // Fail after acquiring the text file reference, then verify cleanup.
        ChildChunkIdentification properties;
        Check(file->FGetKidChidCtg(root.ctg, root.cno, 0, kctgTxtProps, &properties));
        file->DeleteChild(root.ctg, root.cno, properties.cki.ctg, properties.cki.cno, 0);
        LegacyLong references = loaded.text.pfil->CactRef();
        Check(!loaded.FRead(file, root.ctg, root.cno));
        Check(loaded.text.pfil->CactRef() == references && loaded.text.cb == 5);
    }
#ifdef DEBUG
    bool observedFailure = false;
    for (LegacyLong attempt = 0; attempt < 32; ++attempt)
    {
        ChunkNumber unrelated;
        Check(file->FAdd(0, 'TEST', &unrelated));
        LegacyLong chunks = file->Ccki();
        vmutxMem.Enter();
        vdmglob.dmaglPv.cactDo = vdmglob.dmaglHq.cactDo = vdmglob.dmaglBase.cactDo = 0;
        vdmglob.dmaglPv.cactFail = vdmglob.dmaglHq.cactFail = vdmglob.dmaglBase.cactFail = 1000;
        vmutxMem.Leave();
        ChunkIdentification written;
        bool saved = source.FWrite(file, &written);
        vmutxMem.Enter();
        vdmglob.dmaglPv.cactFail = vdmglob.dmaglHq.cactFail = vdmglob.dmaglBase.cactFail = 0;
        vmutxMem.Leave();
        vpers->Clear();
        if (!saved)
        {
            observedFailure = true;
            Check(file->Ccki() == chunks && written.cno == cnoNil);
        }
    }
    Check(observedFailure);
#endif
    ReleasePpo(&file);
}

#ifdef TEST_ENGINE_CORE
struct RenderHost
{
    br_actor *root;
    int attachments;
    int changes;
};
static void Attach(void *context, Body *body)
{
    RenderHost *host = static_cast<RenderHost *>(context);
    BrActorAdd(host->root, body->PbactRoot());
    ++host->attachments;
}
static void Changed(void *context)
{
    ++static_cast<RenderHost *>(context)->changes;
}
static FILE *sourceSnapshot = nullptr;
static int missingSourcePrompts = 0;
static bool disabledSourcePrompt = false;
static PFilename retrySourceDirectory = nullptr, retrySourceFile = nullptr;
static bool MissingSource(const MissingSourceRequest &request)
{
    Check(request.disabled == disabledSourcePrompt);
    Check(request.sid == 42 && request.title && request.folder && request.contentRoot);
    Check(request.asset && request.asset->sid == 42 && request.asset->ctg == kctgTmpl && request.asset->cno == 1);
    ++missingSourcePrompts;
    if(sourceSnapshot){uint32_t row[]={static_cast<uint32_t>(request.sid),request.disabled,request.asset->ctg,request.asset->cno,retrySourceDirectory!=nullptr};Check(std::fwrite(row,sizeof(row),1,sourceSnapshot)==1);}

    if (request.disabled)
        return false;
    if (retrySourceDirectory && missingSourcePrompts == 1)
    {
        Check(retrySourceDirectory->FBuildFromPath(request.contentRoot, kftgDir));
        Check(retrySourceDirectory->FDownDir(request.folder, ffniCreateDir | ffniMoveToDir));
        String path;
        retrySourceDirectory->GetStnPath(&path);
        Check(path.FAppendSz(PszLit("retry.3cn")) && retrySourceFile->FBuildFromPath(&path));
        auto file = ChunkyFile::PcflCreate(retrySourceFile, fcflWriteEnable);
        Check(file && file->FSave(kctgMvie));
        ReleasePpo(&file);
        return true;
    }
    return false;
}

static void TestMoviePersistence()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_MOVIE_OWNER_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        Check(snapshot);
    }
    const auto record = [&](MovieData &movie) {
        if (!snapshot) return;
        uint32_t row[] = {movie.movieNumber, static_cast<uint32_t>(movie.actorIdLimit),
                          static_cast<uint32_t>(movie.sceneCount), static_cast<uint32_t>(movie.Actors()->IvMac()),
                          static_cast<uint32_t>(movie.Resources()->CactRef()), static_cast<uint32_t>(movie.title.count)};
        Check(std::fwrite(row, sizeof(row), 1, snapshot) == 1);
        Check(std::fwrite(three_mm_movie_title_text(&movie.title), sizeof(achar), movie.title.count, snapshot) == static_cast<size_t>(movie.title.count));
        ChildChunkIdentification child;
        DataBlock block;
        auto file = movie.Resources()->Pcfl();
        if (file->FGetKidChidCtg(kctgMvie, movie.movieNumber, 0, kctgGst, &child))
        {
            Check(file->FFind(kctgGst, child.cki.cno, &block));
            byte bytes[2048]; uint32_t size = block.Cb();
            Check(size <= sizeof(bytes) && block.FReadRgb(bytes, size, 0));
            Check(std::fwrite(&size, sizeof(size), 1, snapshot) == 1 && std::fwrite(bytes, 1, size, snapshot) == size);
        }
        else { uint32_t size = 0; Check(std::fwrite(&size, sizeof(size), 1, snapshot) == 1); }
    };
    Filename destination;
    Check(destination.FGetTemp());
    {
        MovieData movie;
        Check(movie.FInitEmpty());
        record(movie);
        PChunkyFile file = movie.Resources()->Pcfl();
        String movieTitle(PszLit("Headless movie")); movie.SetTitle(&movieTitle);
        // Preserve an actual graph: a scene references an embedded actor
        // template; persistence treats their payloads as opaque, never UI.
        byte payload[] = {1, 3, 5, 7, 9};
        ChunkNumber scene, actorTemplate;
        DataBlock block;
        SceneData sceneState;
        sceneState.lastFrame = 12;
        String sceneName(PszLit("Headless scene")); sceneState.SetName(&sceneName);
        Check(sceneState.FCreate(file, &scene));
        Check(file->FAdd(sizeof(payload), kctgTmpl, &actorTemplate, &block));
        Check(block.FWriteRgb(payload, sizeof(payload), 0));
        Check(file->FAdoptChild(kctgScen, scene, kctgTmpl, actorTemplate, 0));
        Check(file->FAdoptChild(kctgMvie, movie.movieNumber, kctgScen, scene, 0));
        RollCallActorEntry actor = {};
        actor.arid = 42;
        actor.cactRef = 1;
        actor.grfbrws = 5;
        actor.tagTmpl.sid = ksidUseCrf;
        actor.tagTmpl.ctg = kctgTmpl;
        actor.tagTmpl.cno = actorTemplate;
        Check(TagManager::FOpenTag(&actor.tagTmpl, movie.Resources()));
        String name;
        name.SetSzs(PszLit("Actor"));
        Check(movie.Actors()->FAddStn(&name, &actor));
        Check(movie.FSave(&destination));
        record(movie);
        Check(movie.FSave(&destination)); // Replacing the roll call retains graph children.
        record(movie);
    }
    {
        MovieData loaded;
        Check(loaded.FLoad(&destination));
        record(loaded);
        Check(loaded.sceneCount == 1 && loaded.actorIdLimit == 43 && loaded.Actors()->IvMac() == 1);
        ChunkNumber explicitRoot = loaded.movieNumber;
        Check(MovieData::FVerifyVersion(loaded.Resources()->Pcfl(), &explicitRoot));
        RollCallActorEntry actor;
        loaded.Actors()->GetExtra(0, &actor);
        Check(actor.arid == 42 && actor.cactRef == 1 && actor.grfbrws == 5);
        Check(actor.tagTmpl.pcrf == loaded.Resources());
        DataBlock block;
        Check(loaded.Resources()->Pcfl()->FFind(kctgTmpl, actor.tagTmpl.cno, &block));
        byte bytes[5];
        Check(block.FReadRgb(bytes, sizeof(bytes), 0));
        Check(bytes[0] == 1 && bytes[4] == 9);
        String movieTitle(PszLit("Saved after load")); loaded.SetTitle(&movieTitle);
        Check(loaded.FSave(&destination)); // Save back to the currently open file.
        record(loaded);
        Check(loaded.FLoad(&destination, explicitRoot));
        record(loaded);
        // A failed load leaves already-open state intact.
        Check(!loaded.FLoad(&destination, explicitRoot + 10000));
        record(loaded);
        Check(loaded.Actors()->IvMac() == 1 && loaded.actorIdLimit == 43);
    }
    {
        MovieData reopened; // All prior archive handles have been released.
        Check(reopened.FLoad(&destination));
        record(reopened);
        String movieTitle; reopened.GetTitle(&movieTitle); Check(movieTitle.FEqualSz(PszLit("Saved after load")));
        Check(reopened.sceneCount == 1 && reopened.Actors()->IvMac() == 1);
    }
    // Reject mismatched wire layouts before GetExtra can overrun a row.
    {
        PChunkyFile malformed = ChunkyFile::PcflCreateTemp();
        Check(malformed != pvNil);
        ChunkNumber number, table;
        Check(MovieData::FCreateRoot(malformed, &number));
        PStringTable_GST wrong = StringTable_GST::PgstNew(4);
        DataBlock block;
        Check(wrong != pvNil);
        Check(malformed->FAdd(wrong->CbOnFile(), kctgGst, &table, &block));
        Check(wrong->FWrite(&block));
        Check(malformed->FAdoptChild(kctgMvie, number, kctgGst, table, 0));
        ReleasePpo(&wrong);
        PChunkyResourceFile resource = ChunkyResourceFile::PcrfNew(malformed, 0);
        Check(resource != pvNil);
        PRollCallTable output = pvNil;
        Check(!MovieData::FReadRollCall(resource, number, &output));
        Check(output == pvNil);
        ReleasePpo(&resource);
        ReleasePpo(&malformed);
    }
    Check(destination.FDelete());
    if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestRollCallStorageBaseline()
{
    auto table = RollCallTable::New();
    Check(table);
    RollCallActorEntry a = {};
    a.tagTmpl.sid = 1; a.tagTmpl.ctg = kctgTmpl; a.tagTmpl.cno = 77;
    String name;
    for (int i = 0; i < 5; ++i)
    {
        a.arid = 10 + i; a.cactRef = i - 2; a.grfbrws = i * 3;
        name.SetSzs(PszLit("original name"));
        Check(table->FAddStn(&name, &a));
    }
    name.SetSzs(PszLit("x")); Check(table->FPutStn(1, &name));
    name.SetSzs(PszLit("a much longer replacement name")); Check(table->FPutStn(3, &name));
    table->GetExtra(2, &a); a.arid = -1234; a.grfbrws = 0x87654321; table->PutExtra(2, &a);
    table->Delete(0); table->Delete(2);
    achar text[] = {ChLit('A'), ChLit('/'), ChLit('B')}; name.SetRgch(text, 3);
    Check(table->FAddStn(&name, &a));
    name.SetNil(); Check(table->FAddStn(&name, &a));
    auto file = ChunkyFile::PcflCreateTemp(); ChunkNumber root, number;
    Check(file && MovieData::FCreateRoot(file, &root));
    Check(MovieData::FWriteRollCall(file, root, table, &number));
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_ROLLCALL_STORAGE_SNAPSHOT")) { snapshot = std::fopen(path, "wb"); Check(snapshot); }
    DataBlock block; Check(file->FFind(kctgGst, number, &block));
    byte bytes[2048]; auto length = block.Cb(); Check(length <= sizeof(bytes) && block.FReadRgb(bytes, length, 0));
    if (snapshot) Check(std::fwrite(bytes, 1, length, snapshot) == static_cast<size_t>(length));
    auto resources = ChunkyResourceFile::PcrfNew(file, 0);
    PRollCallTable loaded = nullptr; LegacyLong limit = 9;
    Check(resources && MovieData::FReadRollCall(resources, root, &loaded, &limit));
    for (LegacyLong i = 0; i < loaded->IvMac(); ++i)
    {
        loaded->GetStn(i, &name); loaded->GetExtra(i, &a);
        uint32_t fields[] = {static_cast<uint32_t>(a.arid), static_cast<uint32_t>(a.cactRef), a.grfbrws,
                             static_cast<uint32_t>(a.tagTmpl.sid), a.tagTmpl.ctg, a.tagTmpl.cno,
                             static_cast<uint32_t>(name.Cch()), static_cast<uint32_t>(limit)};
        if (snapshot) { Check(std::fwrite(fields, sizeof(fields), 1, snapshot) == 1); Check(std::fwrite(name.Prgch(), sizeof(achar), name.Cch(), snapshot) == static_cast<size_t>(name.Cch())); }
    }
    ReleasePpo(&loaded); ReleasePpo(&resources); ReleasePpo(&table); ReleasePpo(&file);
    if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestMovieRootBaseline()
{
    auto file = ChunkyFile::PcflCreateTemp(); ChunkNumber root;
    Check(file && MovieData::FCreateRoot(file, &root));
    DataBlock block; Check(file->FFind(kctgMvie, root, &block));
    MovieFilePrefix prefix; Check(block.FReadRgb(&prefix, sizeof(prefix), 0));
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_MOVIE_ROOT_SNAPSHOT")) { snapshot = std::fopen(path, "wb"); Check(snapshot); }
    if (snapshot) Check(std::fwrite(&prefix, sizeof(prefix), 1, snapshot) == 1);
    const auto record = [&](ChunkNumber requested, bool optional) {
        ThreeMMMovieHeader header = {-77, -77, -77, -77};
        bool ok = MovieData::FVerifyVersion(file, optional ? nullptr : &requested, &header);
        uint32_t row[] = {ok, requested, static_cast<uint32_t>(header.system), static_cast<uint32_t>(header.current_version),
                          static_cast<uint32_t>(header.backward_version), static_cast<uint32_t>(header.big_endian)};
        if (snapshot) Check(std::fwrite(row, sizeof(row), 1, snapshot) == 1);
        vpers->Clear();
    };
    record(cnoNil, false); record(root, false); record(cnoNil, true); record(root + 10000, false);
    prefix.dver.Set(0, 2); Check(block.FWrite(&prefix)); record(cnoNil, false);
    prefix.dver.Set(2, 3); Check(block.FWrite(&prefix)); record(cnoNil, false);
    prefix.dver.Set(3, -2); Check(block.FWrite(&prefix)); record(cnoNil, false);
    prefix.dver.Set(2, 2); SwapBytesBom(&prefix, kbomMfp); Check(block.FWrite(&prefix)); record(cnoNil, false);
    prefix.bo = 0; Check(block.FWrite(&prefix)); record(cnoNil, false);
    ReleasePpo(&file); if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestMovieWireFormats()
{
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    ChunkNumber root;
    Check(MovieData::FCreateRoot(file, &root));
    DataBlock block;
    Check(file->FFind(kctgMvie, root, &block));
    MovieFilePrefix prefix;
    Check(block.FReadRgb(&prefix, sizeof(prefix), 0));
    SwapBytesBom(&prefix, kbomMfp);
    Check(block.FWriteRgb(&prefix, sizeof(prefix), 0));
    Check(MovieData::FVerifyVersion(file, &root));

    PChunkyResourceFile resources = ChunkyResourceFile::PcrfNew(file, 0);
    Check(resources != pvNil);
    PStringTable_GST wire = StringTable_GST::PgstNew(sizeof(RollCallActorEntryOnFile));
    Check(wire != pvNil);
    RollCallActorEntryOnFile row = {};
    row.arid = 27;
    row.cactRef = 2;
    row.grfbrws = 3;
    row.tagTmpl.sid = ksidUseCrf;
    row.tagTmpl.ctg = kctgTmpl;
    row.tagTmpl.cno = 123;
    SwapBytesRglw(&row, 7);
    String name;
    name.SetSzs(PszLit("Foreign endian actor"));
    Check(wire->FAddStn(&name, &row));
    ChunkNumber number;
    Check(file->FAdd(wire->CbOnFile(), kctgGst, &number, &block));
    Check(wire->FWrite(&block, kboOther));
    Check(file->FAdoptChild(kctgMvie, root, kctgGst, number, 0));
    PRollCallTable decoded = pvNil;
    LegacyLong limit = 0;
    LegacyLong refs = resources->CactRef();
    Check(MovieData::FReadRollCall(resources, root, &decoded, &limit));
    Check(limit == 28 && resources->CactRef() == refs + 1);
    RollCallActorEntry actor;
    decoded->GetExtra(0, &actor);
    Check(actor.arid == 27 && actor.cactRef == 2 && actor.grfbrws == 3);
    Check(actor.tagTmpl.cno == 123 && actor.tagTmpl.pcrf == resources);
    TagManager::CloseTag(&actor.tagTmpl);
    ReleasePpo(&decoded);
    Check(resources->CactRef() == refs);

    // Historical scene merges can produce signed actor IDs below zero. They
    // remain stable identifiers and must not be mistaken for the -1 sentinel.
    row = {};
    row.arid = -749027345;
    row.cactRef = 1;
    row.tagTmpl.sid = ksidUseCrf;
    row.tagTmpl.ctg = kctgTmpl;
    row.tagTmpl.cno = 123;
    SwapBytesRglw(&row, 7);
    Check(wire->FAddStn(&name, &row));
    Check(file->FAdd(wire->CbOnFile(), kctgGst, &number, &block));
    Check(wire->FWrite(&block, kboOther));
    ChildChunkIdentification old;
    Check(file->FGetKidChidCtg(kctgMvie, root, 0, kctgGst, &old));
    Check(file->FAdoptChild(kctgMvie, root, kctgGst, number, 0));
    file->DeleteChild(kctgMvie, root, kctgGst, old.cki.cno, 0);
    limit = 0;
    Check(MovieData::FReadRollCall(resources, root, &decoded, &limit));
    Check(limit == 28 && decoded->IvMac() == 2);
    decoded->GetExtra(1, &actor);
    Check(actor.arid == -749027345 && actor.tagTmpl.pcrf == resources);
    for (LegacyLong index = 0; index < decoded->IvMac(); ++index)
    {
        decoded->GetExtra(index, &actor);
        TagManager::CloseTag(&actor.tagTmpl);
    }
    ReleasePpo(&decoded);
    Check(resources->CactRef() == refs);

    // An invalid sentinel row fails after the embedded TAGs were opened.
    // Both resource references and caller-visible state must be restored.
    row = {};
    row.arid = -1;
    SwapBytesRglw(&row, 7);
    Check(wire->FAddStn(&name, &row));
    Check(file->FAdd(wire->CbOnFile(), kctgGst, &number, &block));
    Check(wire->FWrite(&block, kboOther));
    Check(file->FGetKidChidCtg(kctgMvie, root, 0, kctgGst, &old));
    Check(file->FAdoptChild(kctgMvie, root, kctgGst, number, 0));
    file->DeleteChild(kctgMvie, root, kctgGst, old.cki.cno, 0);
    limit = 8;
    Check(!MovieData::FReadRollCall(resources, root, &decoded, &limit));
    Check(decoded == pvNil && limit == 8 && resources->CactRef() == refs);
    ReleasePpo(&wire);
    ReleasePpo(&resources);
    ReleasePpo(&file);
}

static void TestScenePersistence()
{
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    SceneData scene;
    scene.firstFrame = 3;
    scene.lastFrame = 18;
    scene.transition = transDissolve;
    String sceneName(PszLit("Scene data")); scene.SetName(&sceneName);
    ChunkNumber number;
    Check(scene.FCreate(file, &number));
    FILE *sceneSnapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_SCENE_DATA_SNAPSHOT")) { sceneSnapshot = std::fopen(path, "wb"); Check(sceneSnapshot); }
    const auto recordScene = [&](SceneData &state) {
        if (!sceneSnapshot) return;
        uint32_t fields[] = {static_cast<uint32_t>(state.firstFrame), static_cast<uint32_t>(state.lastFrame),
                             static_cast<uint32_t>(state.currentFrame), static_cast<uint32_t>(state.transition),
                             static_cast<uint32_t>(state.name.count)};
        Check(std::fwrite(fields, sizeof(fields), 1, sceneSnapshot) == 1);
        Check(std::fwrite(three_mm_movie_title_text(&state.name), sizeof(achar), state.name.count, sceneSnapshot) == static_cast<size_t>(state.name.count));
        DataBlock saved;
        for (int part = 0; part < 3; ++part)
        {
            if (part == 0) Check(file->FFind(kctgScen, number, &saved));
            else { ChildChunkIdentification child; Check(file->FGetKidChidCtg(kctgScen, number, part - 1, part == 1 ? kctgFrmGg : kctgStartGg, &child)); Check(file->FFind(child.cki.ctg, child.cki.cno, &saved)); }
            byte bytes[2048]; uint32_t length = saved.Cb(); Check(length <= sizeof(bytes) && saved.FReadRgb(bytes, length, 0));
            Check(std::fwrite(&length, sizeof(length), 1, sceneSnapshot) == 1 && std::fwrite(bytes, 1, length, sceneSnapshot) == length);
        }
    };
    recordScene(scene);
    PGeneralGroup frames = GeneralGroup::PggNew(size(SceneEvent));
    PGeneralGroup starts = GeneralGroup::PggNew(size(SceneEvent));
    Check(frames != pvNil && starts != pvNil);
    int32_t camera = 2;
    SceneEvent event = {3, sevtChngCamera};
    Check(starts->FInsert(0, sizeof(camera), &camera, &event));
    Check(frames->FInsert(0, sizeof(camera), &camera, &event));
    SceneEventPause pause = {witForTime, 500};
    event = {8, sevtPause};
    Check(frames->FInsert(1, sizeof(pause), &pause, &event));
    byte sound[36] = {};
    int32_t volume = 123, style = 1, tags = 1;
    ChildChunkID soundChild = 7;
    TAGOnFile soundTag = {};
    soundTag.sid = 1;
    soundTag.ctg = kctgMsnd;
    soundTag.cno = 99;
    CopyPb(&volume, sound, sizeof(volume));
    CopyPb(&style, sound + 4, sizeof(style));
    CopyPb(&tags, sound + 12, sizeof(tags));
    CopyPb(&soundChild, sound + 16, sizeof(soundChild));
    CopyPb(&soundTag, sound + 20, sizeof(soundTag));
    event = {9, sevtPlaySnd};
    Check(frames->FInsert(2, sizeof(sound), sound, &event));
    Check(SceneData::FWriteEvents(file, number, frames, starts));
    SceneData loaded;
    Check(loaded.FRead(file, number));
    Check(loaded.firstFrame == 3 && loaded.lastFrame == 18 && loaded.currentFrame == 2);
    loaded.GetName(&sceneName); Check(loaded.transition == transDissolve && sceneName.FEqualSz(PszLit("Scene data")));
    recordScene(loaded);
    ChildChunkIdentification child;
    Check(file->FGetKidChidCtg(kctgScen, number, 0, kctgFrmGg, &child));
    DataBlock block;
    Check(file->FFind(kctgFrmGg, child.cki.cno, &block));
    short bo;
    PGeneralGroup decoded = SceneData::PggReadEvents(&block, &bo, false);
    Check(decoded != pvNil && decoded->IvMac() == 3 && bo == kboCur);
    SceneEventPause recovered;
    decoded->Get(1, &recovered);
    Check(recovered.wit == witForTime && recovered.dts == 500);
    byte recoveredSound[36];
    decoded->Get(2, recoveredSound);
    Check(std::memcmp(recoveredSound, sound, sizeof(sound)) == 0);
    ReleasePpo(&decoded);

    // Reject a malformed sound's variable-array length without overwriting
    // the previously saved event children.
    byte truncatedSound[16] = {};
    int32_t count = 100;
    CopyPb(&count, truncatedSound + 12, sizeof(count));
    event = {9, sevtPlaySnd};
    Check(frames->FInsert(3, sizeof(truncatedSound), truncatedSound, &event));
    Check(!SceneData::FWriteEvents(file, number, frames, starts));
    ChildChunkIdentification unchanged;
    Check(file->FGetKidChidCtg(kctgScen, number, 0, kctgFrmGg, &unchanged));
    Check(unchanged.cki.cno == child.cki.cno);

    frames->Delete(3);

#ifdef DEBUG
    // Grow the archive while forcing allocation failures; a failed event-list
    // replacement must retain both previously saved child links.
    bool observedFailure = false;
    for (LegacyLong attempt = 0; attempt < 40; ++attempt)
    {
        ChunkNumber unrelated;
        Check(file->FAdd(0, 'TEST', &unrelated));
        ChildChunkIdentification oldFrame, oldStart;
        Check(file->FGetKidChidCtg(kctgScen, number, 0, kctgFrmGg, &oldFrame));
        Check(file->FGetKidChidCtg(kctgScen, number, 1, kctgStartGg, &oldStart));
        vmutxMem.Enter();
        vdmglob.dmaglPv.cactDo = vdmglob.dmaglHq.cactDo = vdmglob.dmaglBase.cactDo = 0;
        vdmglob.dmaglPv.cactFail = vdmglob.dmaglHq.cactFail = vdmglob.dmaglBase.cactFail = 1000;
        vmutxMem.Leave();
        LegacyLong chunksBefore = file->Ccki();
        LegacyLong linksBefore = file->Ckid(kctgScen, number);
        bool saved = SceneData::FWriteEvents(file, number, frames, starts);
        vmutxMem.Enter();
        vdmglob.dmaglPv.cactFail = vdmglob.dmaglHq.cactFail = vdmglob.dmaglBase.cactFail = 0;
        vmutxMem.Leave();
        vpers->Clear();
        if (!saved)
        {
            observedFailure = true;
            ChildChunkIdentification keptFrame, keptStart;
            Check(file->FGetKidChidCtg(kctgScen, number, 0, kctgFrmGg, &keptFrame));
            Check(file->FGetKidChidCtg(kctgScen, number, 1, kctgStartGg, &keptStart));
            Check(keptFrame.cki.cno == oldFrame.cki.cno && keptStart.cki.cno == oldStart.cki.cno);
            Check(file->Ccki() == chunksBefore && file->Ckid(kctgScen, number) == linksBefore);
        }
    }
    Check(observedFailure);
#endif

    // Scene metadata and event-group headers accept opposite byte order.
    Check(file->FFind(kctgScen, number, &block));
    SceneOnFile prefix;
    Check(block.FReadRgb(&prefix, sizeof(prefix), 0));
    SwapBytesBom(&prefix, kbomScenh);
    Check(block.FWriteRgb(&prefix, sizeof(prefix), 0));
    Check(loaded.FRead(file, number) && loaded.transition == transDissolve);
    loaded.transition = transBlack;
    Check(loaded.FWrite(file, number));
    recordScene(loaded);
    event = {3, sevtChngCamera};
    SwapBytesBom(&event, kbomSev);
    SwapBytesBom(&camera, kbomSceneLong);
    PGeneralGroup foreign = GeneralGroup::PggNew(size(SceneEvent));
    Check(foreign != pvNil && foreign->FInsert(0, sizeof(camera), &camera, &event));
    Check(block.FSetTemp(foreign->CbOnFile()));
    Check(foreign->FWrite(&block, kboOther));
    decoded = SceneData::PggReadEvents(&block, &bo, false);
    Check(decoded != pvNil && bo == kboOther && decoded->IvMac() == 1);
    ReleasePpo(&decoded);
    ReleasePpo(&foreign);
    ReleasePpo(&frames);
    ReleasePpo(&starts);
    ReleasePpo(&file);
    if (sceneSnapshot) Check(std::fclose(sceneSnapshot) == 0);
}

static void TestActorPersistence()
{
    FILE *actorSnapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_ACTOR_DATA_SNAPSHOT")) { actorSnapshot = std::fopen(path, "wb"); Check(actorSnapshot); }
    const auto recordBlock = [&](DataBlock &value) {
        if (!actorSnapshot) return;
        uint32_t length = value.Cb(); byte bytes[8192]; Check(length <= sizeof(bytes) && value.FReadRgb(bytes, length, 0));
        Check(std::fwrite(&length, sizeof(length), 1, actorSnapshot) == 1 && std::fwrite(bytes, 1, length, actorSnapshot) == length);
    };
    const auto recordHeader = [&](DataBlock &value) {
        ActorChunkOnFile output; std::memset(&output, 0xa5, sizeof(output));
        uint32_t ok = ActorData::FReadHeader(&value, &output);
        if (actorSnapshot) Check(std::fwrite(&ok, sizeof(ok), 1, actorSnapshot) == 1 && std::fwrite(&output, sizeof(output), 1, actorSnapshot) == 1);
    };
    ActorChunkOnFile header = {};
    header.bo = kboCur;
    header.osk = koskCur;
    header.arid = 17;
    header.nfrmFirst = -3;
    header.nfrmLast = 25;
    header.dxyzFullRte.dxr = BR_SCALAR(2.5);
    header.tagTmpl.sid = 2;
    header.tagTmpl.ctg = kctgTmpl;
    header.tagTmpl.cno = 11;
    DataBlock block;
    Check(block.FSetTemp(sizeof(header)) && block.FWriteRgb(&header, sizeof(header), 0));
    recordHeader(block);
    ActorChunkOnFile read;
    Check(ActorData::FReadHeader(&block, &read));
    Check(read.arid == 17 && read.nfrmFirst == -3 && read.nfrmLast == 25);
    SwapBytesRgsw(&header.bo, 2);
    SwapBytesRglw(&header.dxyzFullRte, 10);
    Check(block.FWriteRgb(&header, sizeof(header), 0));
    recordHeader(block);
    Check(ActorData::FReadHeader(&block, &read));
    Check(read.tagTmpl.sid == 2 && read.dxyzFullRte.dxr == BR_SCALAR(2.5));
    SwapBytesRgsw(&header.bo, 2);
    SwapBytesRglw(&header.dxyzFullRte, 10);
    // The original 40-byte ACTR header omits nfrmLast.
    byte legacy[sizeof(header) - sizeof(int32_t)];
    const size_t lastOffset = offsetof(ActorChunkOnFile, nfrmLast);
    std::memcpy(legacy, &header, lastOffset);
    std::memcpy(legacy + lastOffset, &header.tagTmpl, sizeof(header.tagTmpl));
    Check(block.FSetTemp(sizeof(legacy)) && block.FWriteRgb(legacy, sizeof(legacy), 0));
    recordHeader(block);
    {
        auto bad = header; bad.bo = 3;
        DataBlock input; Check(input.FSetTemp(sizeof(bad)) && input.FWrite(&bad)); recordHeader(input);
        std::memcpy(legacy, &bad, lastOffset); std::memcpy(legacy + lastOffset, &bad.tagTmpl, sizeof(bad.tagTmpl));
        Check(input.FSetTemp(sizeof(legacy)) && input.FWriteRgb(legacy, sizeof(legacy), 0)); recordHeader(input);
        Check(input.FSetTemp(20)); recordHeader(input);
    }
    Check(ActorData::FReadHeader(&block, &read));
    Check(read.nfrmLast == knfrmInvalid && read.tagTmpl.cno == 11);
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    ChunkNumber number;
    Check(file->FAdd(sizeof(header), kctgActr, &number));
    Check(ActorData::FWriteHeader(file, number, &header));
    Check(ActorData::FAdjustId(file, number, 3));
    Check(file->FFind(kctgActr, number, &block) && ActorData::FReadHeader(&block, &read));
    Check(read.arid == 20);
    recordHeader(block);
    ReleasePpo(&file);

    PDynamicArray route = DynamicArray::PglNew(sizeof(RouteDistancePoint));
    Check(route != pvNil);
    RouteDistancePoint point = {};
    point.xyz.dyr = BR_SCALAR(1.25);
    point.dwr = BR_SCALAR(7);
    Check(route->FAdd(&point));
    Check(block.FSetTemp(route->CbOnFile()) && route->FWrite(&block));
    PDynamicArray readRoute = ActorData::PglReadRoute(&block);
    Check(readRoute != pvNil && readRoute->IvMac() == 1);
    RouteDistancePoint readPoint;
    readRoute->Get(0, &readPoint);
    Check(readPoint.xyz.dyr == point.xyz.dyr && readPoint.dwr == point.dwr);
    { DataBlock saved; Check(saved.FSetTemp(readRoute->CbOnFile()) && readRoute->FWrite(&saved)); recordBlock(saved); }
    ReleasePpo(&readRoute);
    ReleasePpo(&route);

    PGeneralGroup events = GeneralGroup::PggNew(sizeof(ActorEvent::Base));
    Check(events != pvNil);
    ActorEvent::Base event = {};
    event.aet = ActorEvent::aetCost;
    event.nfrm = 5;
    event.rtel.irpt = 2;
    event.rtel.dwrOffset = BR_SCALAR(1.25);
    event.rtel.dnfrm = 7;
    ActorEvent::Costume costume = {};
    costume.ibset = 1;
    costume.cmid = 3;
    costume.fCmtl = tYes;
    costume.tag.sid = 1;
    costume.tag.ctg = kctgCmtl;
    costume.tag.cno = 7;
    Check(events->FInsert(0, sizeof(costume), &costume, &event));
    event.aet = ActorEvent::aetSnd;
    ActorEvent::Sound sound = {};
    sound.fQueue = tYes;
    sound.vlm = 234;
    sound.chid = 9;
    sound.tag.sid = 2;
    sound.tag.ctg = kctgMsnd;
    sound.tag.cno = 13;
    Check(events->FInsert(1, sizeof(sound), &sound, &event));
    event.aet = ActorEvent::aetAdd;
    ActorEvent::Add addition = {};
    addition.dxr = BR_SCALAR(2.5);
    addition.dyr = BR_SCALAR(3.25);
    addition.dzr = BR_SCALAR(-1);
    addition.xa = BR_ANGLE_DEG(30);
    addition.ya = BR_ANGLE_DEG(45);
    addition.za = BR_ANGLE_DEG(60);
    Check(events->FInsert(2, sizeof(addition), &addition, &event));
    byte guarded[100], expected[100];
    for (int i = 0; i < 100; ++i)
        guarded[i] = static_cast<byte>(i);
    std::memcpy(guarded + 16, &addition, sizeof(addition));
    std::memcpy(expected, guarded, sizeof(guarded));
    SwapBytesRglw(expected + 16, 3);
    SwapBytesRgsw(expected + 28, 3);
    SwapBytesBom(guarded + 16, ActorEvent::kbomAevadd);
    Check(std::memcmp(guarded, expected, sizeof(guarded)) == 0);

    PGeneralGroup wire = ActorData::PggWriteEvents(events);
    Check(wire != pvNil && wire->Cb(0) == 28 && wire->Cb(1) == 44);
    for (int foreign = 0; foreign < 2; ++foreign)
    {
        if (foreign)
        {
            for (LegacyLong i = 0; i < wire->IvMac(); ++i)
            {
                ActorEvent::Base e;
                wire->GetFixed(i, &e);
                // Construct foreign bytes from field widths independently of
                // the production masks, including the Add record's 2-byte angles.
                if (e.aet == ActorEvent::aetAdd)
                {
                    SwapBytesRglw(wire->QvGet(i), 3);
                    SwapBytesRgsw(static_cast<byte *>(wire->QvGet(i)) + 12, 3);
                }
                else
                    SwapBytesRglw(wire->QvGet(i), e.aet == ActorEvent::aetCost ? 7 : 11);
                SwapBytesRglw(&e, 5);

                wire->PutFixed(i, &e);
            }
        }
        Check(block.FSetTemp(wire->CbOnFile()) && wire->FWrite(&block, foreign ? kboOther : kboCur));
        PGeneralGroup decoded = ActorData::PggReadEvents(&block);
        Check(decoded != pvNil && decoded->IvMac() == 3);
        ActorEvent::Costume gotCostume;
        ActorEvent::Sound gotSound;
        decoded->Get(0, &gotCostume);
        decoded->Get(1, &gotSound);
        ActorEvent::Add gotAdd;
        decoded->Get(2, &gotAdd);
        ActorEvent::Base gotEvent;
        decoded->GetFixed(2, &gotEvent);
        Check(gotEvent.rtel.irpt == 2 && gotEvent.rtel.dnfrm == 7 && gotEvent.rtel.dwrOffset == BR_SCALAR(1.25));
        Check(gotAdd.dxr == addition.dxr && gotAdd.dyr == addition.dyr && gotAdd.dzr == addition.dzr);
        Check(gotAdd.xa == addition.xa && gotAdd.ya == addition.ya && gotAdd.za == addition.za);
        Check(gotCostume.cmid == 3 && gotCostume.tag.cno == 7 && gotCostume.tag.pcrf == pvNil);
        Check(gotSound.vlm == 234 && gotSound.chid == 9 && gotSound.tag.cno == 13 && gotSound.tag.pcrf == pvNil);
        ReleasePpo(&decoded);
    }
    ReleasePpo(&wire);
    // A wrong runtime payload width is rejected before copying TAG data.
    events->Delete(0);
    event.aet = ActorEvent::aetCost;
    int32_t shortPayload = 0;
    Check(events->FInsert(0, sizeof(shortPayload), &shortPayload, &event));
    Check(ActorData::PggWriteEvents(events) == pvNil);
    ReleasePpo(&events);
    // Every event kind, including ignored cache identity and mixed-width Add.
    events = GeneralGroup::PggNew(sizeof(ActorEvent::Base)); Check(events);
    const LegacyLong widths[] = {sizeof(ActorEvent::Add), sizeof(ActorEvent::Action), sizeof(ActorEvent::Costume),
        sizeof(BMAT34), sizeof(ActorEvent::Stretch), 4, sizeof(ActorEvent::Sound), 12, 4, 12, 4, 0, sizeof(BMAT34)};
    for (int kind = 0; kind < ActorEvent::aetLim; ++kind)
    {
        byte payload[64]; std::memset(payload, 0x31 + kind, sizeof(payload));
        ActorEvent::Base e = {}; e.aet = kind; e.nfrm = -10 + kind; e.rtel.irpt = kind; e.rtel.dnfrm = -7;
        if (kind == ActorEvent::aetCost)
        {
            ActorEvent::Costume c = {}; c.ibset = -7; c.cmid = 99; c.fCmtl = static_cast<tribool>(2);
            c.tag = {1, reinterpret_cast<PChunkyResourceFile>(uintptr_t(1)), kctgMtrl, 42}; std::memcpy(payload, &c, sizeof(c));
        }
        if (kind == ActorEvent::aetSnd)
        {
            ActorEvent::Sound s = {}; s.fLoop = static_cast<tribool>(2); s.fQueue = tMaybe; s.vlm = -99; s.chid = 73;
            s.tag = {1, reinterpret_cast<PChunkyResourceFile>(uintptr_t(1)), kctgMsnd, 41}; std::memcpy(payload, &s, sizeof(s));
        }
        Check(events->FInsert(kind, widths[kind], payload, &e));
    }
    wire = ActorData::PggWriteEvents(events); Check(wire);
    for (int foreign = 0; foreign < 2; ++foreign)
    {
        if (foreign) for (LegacyLong i = 0; i < wire->IvMac(); ++i)
        {
            ActorEvent::Base e; wire->GetFixed(i, &e);
            if (e.aet == ActorEvent::aetAdd) { SwapBytesRglw(wire->QvGet(i), 3); SwapBytesRgsw(static_cast<byte *>(wire->QvGet(i)) + 12, 3); }
            else if (wire->Cb(i)) SwapBytesRglw(wire->QvGet(i), wire->Cb(i) / 4);
            SwapBytesRglw(&e, 5); wire->PutFixed(i, &e);
        }
        Check(block.FSetTemp(wire->CbOnFile()) && wire->FWrite(&block, foreign ? kboOther : kboCur)); recordBlock(block);
        auto decoded = ActorData::PggReadEvents(&block); Check(decoded && decoded->IvMac() == ActorEvent::aetLim);
        auto canonical = ActorData::PggWriteEvents(decoded); Check(canonical);
        DataBlock saved; Check(saved.FSetTemp(canonical->CbOnFile()) && canonical->FWrite(&saved)); recordBlock(saved);
        ReleasePpo(&canonical); ReleasePpo(&decoded);
    }
    ReleasePpo(&wire); ReleasePpo(&events);
    if (actorSnapshot) Check(std::fclose(actorSnapshot) == 0);
}

static void TestActorTrackOwnership()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_ACTOR_TRACK_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        Check(snapshot != nullptr);
    }
    auto record = [&](const void *p, size_t n) {
        if (snapshot) Check(std::fwrite(p, 1, n, snapshot) == n);
    };
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    PChunkyResourceFile resource = ChunkyResourceFile::PcrfNew(file, 0);
    Check(resource != pvNil);
    const LegacyLong initialRefs = resource->CactRef();
    {
        ActorTrack source, copy;
        Check(source.FCreate() && copy.FCreate());
        RouteDistancePoint point = {{BR_SCALAR(3), BR_SCALAR(4), BR_SCALAR(5)}, rZero};
        Check(source.Route()->FInsert(0, &point));
        ActorEvent::Base event = {};
        event.aet = ActorEvent::aetSnd;
        ActorEvent::Sound sound = {};
        sound.tag.sid = ksidUseCrf;
        sound.tag.ctg = kctgMsnd;
        sound.tag.cno = 7;
        sound.tag.pcrf = resource;
        Check(source.Events()->FInsert(0, sizeof(sound), &sound, &event));
        TagManager::DupTag(&sound.tag); // Reference transferred to the inserted row.
        event.aet = ActorEvent::aetCost;
        ActorEvent::Costume costume = {};
        costume.tag = sound.tag;
        Check(source.Events()->FInsert(1, sizeof(costume), &costume, &event));
        TagManager::DupTag(&costume.tag);
        // A custom costume has no TAG, even if its unused bytes resemble one.
        costume.fCmtl = tYes;
        Check(source.Events()->FInsert(2, sizeof(costume), &costume, &event));
        Check(resource->CactRef() == initialRefs + 2);
        Check(copy.FCloneFrom(source));
        Check(resource->CactRef() == initialRefs + 4);
        Check(copy.Events() != source.Events() && copy.Route() != source.Route());
        point.xyz.dxr = BR_SCALAR(9);
        copy.Route()->Put(0, &point);
        RouteDistancePoint original;
        source.Route()->Get(0, &original);
        Check(original.xyz.dxr == BR_SCALAR(3));
        Check(copy.FCloneFrom(copy));
        Check(resource->CactRef() == initialRefs + 4);

        PGeneralGroup sourceEvents = source.Events();
        PDynamicArray sourceRoute = source.Route();
#ifdef DEBUG
        bool failed = false, succeeded = false;
        for (LegacyLong skip = 0; skip < 24; ++skip)
        {
            PGeneralGroup oldEvents = copy.Events();
            PDynamicArray oldRoute = copy.Route();
            vmutxMem.Enter();
            vdmglob.dmaglPv.cactDo = vdmglob.dmaglHq.cactDo = vdmglob.dmaglBase.cactDo = skip;
            vdmglob.dmaglPv.cactFail = vdmglob.dmaglHq.cactFail = vdmglob.dmaglBase.cactFail = 1000;
            vmutxMem.Leave();
            bool ok = copy.FCloneFrom(source);
            vmutxMem.Enter();
            vdmglob.dmaglPv.cactFail = vdmglob.dmaglHq.cactFail = vdmglob.dmaglBase.cactFail = 0;
            vmutxMem.Leave();
            vpers->Clear();
            if (!ok)
            {
                failed = true;
                Check(copy.Events() == oldEvents && copy.Route() == oldRoute);
            }
            else
                succeeded = true;
            Check(resource->CactRef() == initialRefs + 4);
            Check(source.Events() == sourceEvents && source.Route() == sourceRoute);
        }
        Check(failed && succeeded);
#endif
        // Clone serializes identically, using the real runtime-to-wire codec.
        PGeneralGroup first = ActorData::PggWriteEvents(source.Events());
        PGeneralGroup second = ActorData::PggWriteEvents(copy.Events());
        Check(first != pvNil && second != pvNil && first->IvMac() == second->IvMac());
        for (LegacyLong index = 0; index < first->IvMac(); ++index)
        {
            Check(first->Cb(index) == second->Cb(index));
            Check(std::memcmp(first->QvFixedGet(index), second->QvFixedGet(index), sizeof(ActorEvent::Base)) == 0);
            Check(std::memcmp(first->QvGet(index), second->QvGet(index), first->Cb(index)) == 0);
            LegacyLong bytes = first->Cb(index);
            record(&bytes, sizeof(bytes));
            record(first->QvFixedGet(index), sizeof(ActorEvent::Base));
            record(first->QvGet(index), bytes);
        }
        ReleasePpo(&first);
        ReleasePpo(&second);
        copy.Swap(source);
        Check(copy.Events() == sourceEvents && copy.Route() == sourceRoute);
        source.Clear();
        Check(resource->CactRef() == initialRefs + 2);
        copy.CloseTags();
        copy.CloseTags(); // Safe for both already-closed and initially-unopened TAGs.
        Check(resource->CactRef() == initialRefs);
        ActorTrack unopened;
        Check(unopened.FCloneFrom(copy));
        Check(resource->CactRef() == initialRefs);
        Check(copy.FCloneFrom(ActorTrack()));
        Check(copy.Route() == pvNil && copy.Events() == pvNil);
        LegacyLong referenceDelta = resource->CactRef() - initialRefs;
        record(&referenceDelta, sizeof(referenceDelta));
        record(&original, sizeof(original));
    }
    Check(resource->CactRef() == initialRefs);
    ReleasePpo(&resource);
    ReleasePpo(&file);
    if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestActorRouteEvaluation()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_ACTOR_ROUTE_SNAPSHOT")) { snapshot = std::fopen(path, "wb"); Check(snapshot); }
    auto record = [&](const void *p, size_t n) { if (snapshot) Check(std::fwrite(p, 1, n, snapshot) == n); };
    auto advance = [&](const ActorTrack &track, BRS step, RouteLocation *location, LegacyLong next, LegacyLong frame,
                       LegacyLong first, bool clamp, bool *end = nullptr) {
        bool ended = false;
        ActorRouteEvaluator::Advance(track, step, location, next, frame, first, clamp, &ended);
        if (end) *end = ended;
        RoutePoint position;
        ActorRouteEvaluator::Position(track, location, &position);
        record(location, sizeof(*location)); record(&position, sizeof(position));
        int flag = ended; record(&flag, sizeof(flag));
    };
    RoutePoint firstPoint = {BR_SCALAR(2), BR_SCALAR(4), BR_SCALAR(6)};
    RoutePoint secondPoint = {BR_SCALAR(6), BR_SCALAR(8), BR_SCALAR(10)};
    ActorRouteEvaluator::Interpolate(&firstPoint, &secondPoint, BR_SCALAR(0.5), &firstPoint);
    Check(firstPoint.dxr == BR_SCALAR(4) && firstPoint.dyr == BR_SCALAR(6) && firstPoint.dzr == BR_SCALAR(8));
    ActorRouteEvaluator::Interpolate(&firstPoint, &firstPoint, BR_SCALAR(0.5), &firstPoint);
    Check(firstPoint.dxr == BR_SCALAR(4) && firstPoint.dyr == BR_SCALAR(6) && firstPoint.dzr == BR_SCALAR(8));
    ActorRouteEvaluator::Interpolate(&firstPoint, &secondPoint, BR_SCALAR(0.5), &secondPoint);
    Check(secondPoint.dxr == BR_SCALAR(5) && secondPoint.dyr == BR_SCALAR(7) && secondPoint.dzr == BR_SCALAR(9));
    ActorTrack track;
    Check(track.FCreate());
    RouteDistancePoint points[] = {{{rZero, rZero, rZero}, BR_SCALAR(4)},
                                   {{BR_SCALAR(4), rZero, rZero}, BR_SCALAR(4)},
                                   {{BR_SCALAR(4), BR_SCALAR(4), rZero}, rZero},
                                   {{BR_SCALAR(100), rZero, rZero}, rZero}};
    for (LegacyLong index = 0; index < 4; ++index)
        Check(track.Route()->FInsert(index, &points[index]));
    RouteLocation location = {99, BR_SCALAR(99), 99};
    bool end = true;
    advance(track, BR_SCALAR(2), &location, 0, -3, -3, true, &end);
    Check(location.irpt == 0 && location.dwrOffset == rZero && location.dnfrm == 0 && !end);
    advance(track, BR_SCALAR(2), &location, 0, -2, -3, true, &end);
    RoutePoint position;
    ActorRouteEvaluator::Position(track, &location, &position);
    Check(position.dxr == BR_SCALAR(2) && position.dyr == rZero && !end);
    advance(track, BR_SCALAR(4), &location, 0, -1, -3, true, &end);
    ActorRouteEvaluator::Position(track, &location, &position);
    Check(location.irpt == 1 && position.dxr == BR_SCALAR(4) && position.dyr == BR_SCALAR(2) && !end);
    advance(track, BR_SCALAR(2), &location, 0, 0, -3, true, &end);
    Check(location.irpt == 2 && location.dwrOffset == rZero && !end);
    advance(track, BR_SCALAR(1), &location, 0, 1, -3, true, &end);
    Check(location.irpt == 2 && location.dnfrm == 1 && end);
    advance(track, rZero, &location, 0, 2, -3, true, &end);
    Check(location.irpt == 2 && location.dnfrm == 2 && !end);
    ActorRouteEvaluator::Position(track, &location, &position);
    Check(position.dxr == BR_SCALAR(4) && position.dyr == BR_SCALAR(4));

    // Sound events do not clamp movement. The next Step or Action does.
    ActorEvent::Base event = {};
    event.aet = ActorEvent::aetSnd;
    event.rtel = {0, BR_SCALAR(1), 0};
    ActorEvent::Sound sound = {};
    sound.tag.sid = ksidInvalid;
    Check(track.Events()->FInsert(0, sizeof(sound), &sound, &event));
    event.aet = ActorEvent::aetStep;
    event.rtel.dwrOffset = BR_SCALAR(3);
    BRS step = BR_SCALAR(1);
    Check(track.Events()->FInsert(1, sizeof(step), &step, &event));
    event.aet = ActorEvent::aetActn;
    event.rtel = {1, BR_SCALAR(1), 0};
    ActorEvent::Action action = {};
    Check(track.Events()->FInsert(2, sizeof(action), &action, &event));
    location = {0, rZero, 0};
    advance(track, BR_SCALAR(6), &location, 0, 2, 1, true, &end);
    Check(location.irpt == 0 && location.dwrOffset == BR_SCALAR(3) && !end);
    advance(track, BR_SCALAR(3), &location, 2, 3, 1, true, &end);
    Check(location.irpt == 1 && location.dwrOffset == BR_SCALAR(1) && !end);
    location = {0, rZero, 0};
    advance(track, BR_SCALAR(6), &location, 0, 2, 1, false, &end);
    Check(location.irpt == 1 && location.dwrOffset == BR_SCALAR(2) && !end);
    location = {0, rZero, 0};
    advance(track, BR_SCALAR(100), &location, 3, 2, 1, true, &end);
    Check(location.irpt == 2 && location.dwrOffset == rZero && !end); // Never crosses to the next subroute.

    // Replaying the same explicit steps reproduces locations including dwell time.
    RouteLocation expected[6];
    BRS steps[] = {rZero, BR_SCALAR(2), rZero, BR_SCALAR(4), BR_SCALAR(2), BR_SCALAR(1)};
    for (LegacyLong pass = 0; pass < 2; ++pass)
    {
        location = {0, rZero, 0};
        for (LegacyLong frame = 0; frame < 6; ++frame)
        {
            advance(track, steps[frame], &location, 3, frame + 1, 1, true);
            if (pass == 0)
                expected[frame] = location;
            else
                Check(location == expected[frame]);
        }
    }
    ThreeMMU32 seed = 1234567;
    for (int i = 0; i < 4096; ++i)
    {
        seed = seed * 1664525u + 1013904223u; BRS a; std::memcpy(&a, &seed, 4);
        seed = seed * 1664525u + 1013904223u; BRS b; std::memcpy(&b, &seed, 4);
        // Explicit small divisors exercise reciprocal and saturation paths.
        if (i < 17) b = i - 8;
        BRS values[] = {BrsMul(a, b), BrsDiv(a, b)};
        Check(three_mm_fixed_mul(a,b)==values[0] && three_mm_fixed_div(a,b)==values[1]);
        record(values, sizeof(values));
    }
    if (snapshot) Check(std::fclose(snapshot) == 0);
}

static void TestActorValueEvaluation()
{
    ActorFrameState state;
    FILE *stateSnapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_ACTOR_STATE_SNAPSHOT")) { stateSnapshot = std::fopen(path, "wb"); Check(stateSnapshot); }
    auto recordState = [&]() {
        if (!stateSnapshot) return;
        auto bytes = [&](const void *p, size_t n) { Check(std::fwrite(p, 1, n, stateSnapshot) == n); };
        bytes(&state.routeTranslation, sizeof(state.routeTranslation)); bytes(&state.subrouteTranslation, sizeof(state.subrouteTranslation));
        int flags[] = {state.onStage, state.frozen, state.useCurrentRotation}; bytes(flags, sizeof(flags));
        BRS step = state.step; bytes(&step, sizeof(step));
        LegacyLong values[] = {state.action,state.celCount,state.cel,state.frame,state.nextEvent,state.firstEventInFrame,state.actionEvent,state.addEvent}; bytes(values,sizeof(values));
        bytes(&state.actionFlags,sizeof(state.actionFlags)); bytes(&state.location,sizeof(state.location)); bytes(&state.position,sizeof(state.position));
        bytes(&state.transform.bmat34Fwd,48); bytes(&state.transform.aevpull,12); bytes(&state.transform.rScaleStep,4); bytes(&state.transform.bmat34Cur,48);
        bytes(&state.transform.xaPath,2); bytes(&state.transform.yaPath,2); bytes(&state.transform.zaPath,2);
    };
    recordState();
    BMAT34 identity;
    BrMatrix34Identity(&identity);
    Check(state.frame == knfrmInvalid && state.actionEvent == ivNil && !state.frozen && !state.onStage);
    Check(std::memcmp(&state.transform.bmat34Fwd, &identity, sizeof(identity)) == 0);
    Check(state.transform.aevpull.rScaleX == rOne && state.transform.rScaleStep == rOne);
    ActorTrack track;
    Check(track.FCreate());
    RouteDistancePoint point = {{BR_SCALAR(1), BR_SCALAR(2), BR_SCALAR(3)}, BR_SCALAR(4)};
    Check(track.Route()->FInsert(0, &point));
    point = {{BR_SCALAR(5), BR_SCALAR(6), BR_SCALAR(7)}, rZero};
    Check(track.Route()->FInsert(1, &point));
    RoutePoint origin = {BR_SCALAR(10), BR_SCALAR(20), BR_SCALAR(30)};
    RouteLocation eventLocation = {0, BR_SCALAR(2), 3};
    auto apply = [&](LegacyLong type, void *payload, LegacyLong bytes) {
        ActorEvent::Base event = {static_cast<int32_t>(type), 277, eventLocation};
        LegacyLong index = track.Events()->IvMac();
        Check(track.Events()->FInsert(index, bytes, payload, &event));
        ActorValueEventResult result = state.ApplyValueEvent(track, index, origin);
        recordState();
        ActorEvent::Base unchanged;
        track.Events()->GetFixed(index, &unchanged);
        Check(unchanged.nfrm == 277); // Core evaluation never mutates the authored cache.
        return result;
    };
    state.frame = 9;
    state.onStage = true;
    state.celCount = 6;
    state.actionFlags = 3;
    state.frozen = true;
    state.action = state.cel = state.nextEvent = 4;
    state.RewindRoute(track);
    recordState();
    Check(state.frame == 9 && state.onStage && state.celCount == 6 && state.actionFlags == 3);
    Check(!state.frozen && state.action == 0 && state.cel == 0 && state.nextEvent == 0 && state.location.dnfrm == -1);
    Check(state.position.dxr == BR_SCALAR(1) && state.position.dyr == BR_SCALAR(2));
    state.UpdateOrigin(origin);

    BMAT34 rotation = identity;
    rotation.m[0][1] = BR_SCALAR(0.25);
    Check(apply(ActorEvent::aetRotH, &rotation, sizeof(rotation)) == ActorValueEventResult::Applied);
    Check(state.useCurrentRotation && state.transform.bmat34Cur.m[0][1] == BR_SCALAR(0.25));
    Check(apply(ActorEvent::aetRotF, &rotation, sizeof(rotation)) == ActorValueEventResult::Applied);
    Check(!state.useCurrentRotation && state.transform.bmat34Fwd.m[0][1] == BR_SCALAR(0.25));
    ActorEvent::Stretch pull = {BR_SCALAR(2), BR_SCALAR(3), BR_SCALAR(4)};
    Check(apply(ActorEvent::aetPull, &pull, sizeof(pull)) == ActorValueEventResult::Applied);
    Check(state.transform.aevpull.rScaleY == BR_SCALAR(3));
    BRS scalar = BR_SCALAR(2);
    Check(apply(ActorEvent::aetSize, &scalar, sizeof(scalar)) == ActorValueEventResult::Applied);
    Check(state.transform.rScaleStep == BR_SCALAR(2));
    Check(apply(ActorEvent::aetStep, &scalar, sizeof(scalar)) == ActorValueEventResult::Applied);
    Check(state.step == scalar && state.location.dnfrm == -1);
    scalar = rZero;
    Check(apply(ActorEvent::aetStep, &scalar, sizeof(scalar)) == ActorValueEventResult::Applied);
    Check(state.location.dnfrm == 3 && state.position.dxr == BR_SCALAR(3) && state.position.dyr == BR_SCALAR(4));
    int32_t freeze = -7;
    Check(apply(ActorEvent::aetFreeze, &freeze, sizeof(freeze)) == ActorValueEventResult::Applied && state.frozen);
    freeze = 0;
    Check(apply(ActorEvent::aetFreeze, &freeze, sizeof(freeze)) == ActorValueEventResult::Applied && !state.frozen);
    RoutePoint tweak = {BR_SCALAR(8), BR_SCALAR(9), BR_SCALAR(10)};
    Check(apply(ActorEvent::aetTweak, &tweak, sizeof(tweak)) == ActorValueEventResult::Applied);
    Check(state.position.dxr == BR_SCALAR(8));
    eventLocation.dnfrm = 4;
    tweak.dxr = BR_SCALAR(99);
    Check(apply(ActorEvent::aetTweak, &tweak, sizeof(tweak)) == ActorValueEventResult::Applied);
    Check(state.position.dxr == BR_SCALAR(8)); // Different dwell frame: ignore tweak.
    RoutePoint delta = {BR_SCALAR(1), BR_SCALAR(-2), BR_SCALAR(3)};
    Check(apply(ActorEvent::aetMove, &delta, sizeof(delta)) == ActorValueEventResult::Applied);
    Check(apply(ActorEvent::aetMove, &delta, sizeof(delta)) == ActorValueEventResult::Applied);
    Check(state.subrouteTranslation.dxr == BR_SCALAR(2) && state.routeTranslation.dyr == BR_SCALAR(16) &&
          state.routeTranslation.dzr == BR_SCALAR(36));
    ActorFrameState snapshot = state;
    ActorEvent::Add addition = {};
    Check(apply(ActorEvent::aetAdd, &addition, sizeof(addition)) == ActorValueEventResult::NeedsRuntime);
    Check(ActorFrameTest::SameState(state, snapshot));
    Check(apply(ActorEvent::aetRotF, &scalar, sizeof(scalar)) == ActorValueEventResult::Invalid);
    Check(ActorFrameTest::SameState(state, snapshot));
    eventLocation.irpt = 20;
    Check(apply(ActorEvent::aetStep, &scalar, sizeof(scalar)) == ActorValueEventResult::Invalid);
    Check(ActorFrameTest::SameState(state, snapshot));
    Check(state.ApplyValueEvent(track, -1, origin) == ActorValueEventResult::Invalid);
    Check(apply(ActorEvent::aetLim, &scalar, sizeof(scalar)) == ActorValueEventResult::Invalid);
    Check(ActorFrameTest::SameState(state, snapshot));
    state.InitTransform();
    Check(state.transform.rScaleStep == rOne && state.transform.aevpull.rScaleY == rOne);
    state = snapshot;
    Check(ActorFrameTest::SameState(state, snapshot)); // Value snapshots restore independently of track ownership.
    recordState();
    if (stateSnapshot) Check(std::fclose(stateSnapshot) == 0);
}

static void RecordActorFrameState(const ActorFrameState &,FILE *);
static void TestActorSoundStorage()
{
    FILE *before = nullptr, *after = nullptr;
    if (const char *p = std::getenv("THREE_MM_ACTOR_SOUND_EXPECTED")) { before = std::fopen(p, "wb"); Check(before); }
    if (const char *p = std::getenv("THREE_MM_ACTOR_SOUND_SNAPSHOT")) { after = std::fopen(p, "wb"); Check(after); }
    auto native = DynamicArray::PglNew(sizeof(ActorEvent::SoundMotionMatch), 2);
    Check(native);
    ActorSoundList list;
    Check(list.FCreate());
    ThreeMMU32 seed = 67119;
    auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
    auto fields = [](const ActorEvent::SoundMotionMatch &r, ThreeMMU32 *v) {
        const ThreeMMU32 data[] = {static_cast<ThreeMMU32>(r.aev.aet), static_cast<ThreeMMU32>(r.aev.nfrm),
            static_cast<ThreeMMU32>(r.aev.rtel.irpt), static_cast<ThreeMMU32>(r.aev.rtel.dwrOffset),
            static_cast<ThreeMMU32>(r.aev.rtel.dnfrm), static_cast<ThreeMMU32>(r.aevsnd.fLoop),
            static_cast<ThreeMMU32>(r.aevsnd.fQueue), static_cast<ThreeMMU32>(r.aevsnd.vlm),
            static_cast<ThreeMMU32>(r.aevsnd.celn), static_cast<ThreeMMU32>(r.aevsnd.sty),
            static_cast<ThreeMMU32>(r.aevsnd.fNoSound), r.aevsnd.chid, static_cast<ThreeMMU32>(r.aevsnd.tag.sid),
            r.aevsnd.tag.pcrf != nullptr, r.aevsnd.tag.ctg, r.aevsnd.tag.cno};
        std::memcpy(v, data, sizeof(data));
    };
    for (int i = 0; i < 256; ++i)
    {
        ActorEvent::SoundMotionMatch row = {};
        row.aev.aet = ActorEvent::aetSnd; row.aev.nfrm = i; row.aev.rtel.dnfrm = i * 3;
        row.aevsnd.celn = static_cast<int>(next() % 6); row.aevsnd.sty = static_cast<int>(next() % 4);
        row.aevsnd.tag.sid = 2; row.aevsnd.tag.ctg = kctgMsnd; row.aevsnd.tag.cno = next();
        row.aevsnd.vlm = static_cast<int>(next() % 1000); row.aevsnd.fLoop = static_cast<tribool>(i & 1);
        row.aevsnd.fQueue = static_cast<tribool>((i >> 1) & 1); row.aevsnd.chid = i + 50;
        // Retained DynamicArray implementation of _FInsertSmm is the oracle.
        LegacyLong replace = -1;
        for (LegacyLong j = 0; j < native->IvMac(); ++j)
        {
            ActorEvent::SoundMotionMatch old;
            native->Get(j, &old);
            if (old.aevsnd.celn == row.aevsnd.celn && old.aevsnd.sty == row.aevsnd.sty) { replace = j; break; }
        }
        if (replace >= 0) native->Put(replace, &row); else Check(native->FAdd(&row));
        ThreeMMActorMotionSound input;
        std::memcpy(&input, &row, sizeof(input));
        Check(three_mm_actor_sounds_insert(list.Core(), &input.event, &input.sound));
        if (i % 13 == 0 && native->IvMac()) { native->Delete(0); list.Delete(0); }
        if (i % 17 == 0)
        {
            ActorSoundList clone;
            Check(clone.FCloneFrom(list)); clone.Swap(list);
            auto duplicate = native->PglDup(); Check(duplicate); ReleasePpo(&native); native = duplicate;
        }
        if (i % 61 == 0) { Check(native->FSetIvMac(0)); list.Clear(); }
        Check(native->IvMac() == list.IvMac() && list.Valid());
        const ThreeMMU32 count = static_cast<ThreeMMU32>(list.IvMac());
        if (before) Check(std::fwrite(&count, 1, sizeof(count), before) == sizeof(count));
        if (after) Check(std::fwrite(&count, 1, sizeof(count), after) == sizeof(count));
        for (LegacyLong j = 0; j < list.IvMac(); ++j)
        {
            ActorEvent::SoundMotionMatch a, b;
            native->Get(j, &a); list.Get(j, &b);
            ThreeMMU32 expected[16], actual[16]; fields(a, expected); fields(b, actual);
            Check(!std::memcmp(expected, actual, sizeof(actual)));
            if (before) Check(std::fwrite(expected, 1, sizeof(expected), before) == sizeof(expected));
            if (after) Check(std::fwrite(actual, 1, sizeof(actual), after) == sizeof(actual));
        }
    }
    ReleasePpo(&native);
    if (before) Check(std::fclose(before) == 0);
    if (after) Check(std::fclose(after) == 0);
}
static void TestActorEntranceOrientation()
{
    FILE *before = nullptr, *after = nullptr;
    if (const char *path = std::getenv("THREE_MM_ACTOR_ENTRANCE_EXPECTED"))
    {
        before = std::fopen(path, "wb");
        Check(before);
    }
    if (const char *path = std::getenv("THREE_MM_ACTOR_ENTRANCE_SNAPSHOT"))
    {
        after = std::fopen(path, "wb");
        Check(after);
    }
    ThreeMMU32 seed = 88917;
    auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
    for (int i = 0; i < 256; ++i)
    {
        ThreeMMActorState value;
        three_mm_actor_state_init(&value);
        value.onStage = value.useCurrentRotation = 1;
        value.transform.aevpull.rScaleY = 131072;
        value.transform.rScaleStep = 196608;
        for (auto &row : value.transform.bmat34Cur.m)
            for (auto &cell : row)
                cell = static_cast<ThreeMMI32>(next() & 0x3fffff) - 0x200000;
        ThreeMMActorEntrance add = {};
        add.x = i < 4 ? 0 : static_cast<unsigned short>(next());
        add.y = i < 4 ? 0 : static_cast<unsigned short>(next());
        add.z = i < 4 ? 0 : static_cast<unsigned short>(next());
        auto expected = value;
        BMAT34 matrix;
        std::memcpy(&matrix, &value.transform.bmat34Cur, sizeof(matrix));
        // Retained native _LoadAddOrien arithmetic is the independent oracle.
        if (!(i & 1)) BrMatrix34Identity(&matrix);
        BrMatrix34PostRotateX(&matrix, add.x);
        BrMatrix34PostRotateY(&matrix, add.y);
        BrMatrix34PostRotateZ(&matrix, add.z);
        std::memcpy(&expected.transform.bmat34Cur, &matrix, sizeof(matrix));
        expected.transform.xaPath = add.x;
        expected.transform.yaPath = add.y;
        expected.transform.zaPath = add.z;
        three_mm_actor_add_orientation(&value, &add, i & 1);
        Check(std::memcmp(&expected, &value, sizeof(value)) == 0);
        auto record = [](FILE *file, const ThreeMMActorState &state) {
            if (!file) return;
            Check(std::fwrite(&state.transform.bmat34Cur, 1, 48, file) == 48);
            const unsigned short angles[] = {state.transform.xaPath, state.transform.yaPath, state.transform.zaPath};
            Check(std::fwrite(angles, 1, sizeof(angles), file) == sizeof(angles));
        };
        record(before, expected);
        record(after, value);
    }
    if (before) Check(std::fclose(before) == 0);
    if (after) Check(std::fclose(after) == 0);
}
static void TestActorMathBaseline()
{
    FILE *file=nullptr;
    if(const char *path=std::getenv("THREE_MM_ACTOR_MATH_SNAPSHOT")) { file=std::fopen(path,"wb"); Check(file); }
    auto record=[&](const void *p,size_t n) { if(file) Check(std::fwrite(p,1,n,file)==n); };
    for(unsigned a=0;a<65536;++a) { BRS values[]={BR_SIN(static_cast<BRA>(a)),BR_COS(static_cast<BRA>(a))}; Check(values[0]==three_mm_angle_sin(static_cast<unsigned short>(a)) && values[1]==three_mm_angle_cos(static_cast<unsigned short>(a))); record(values,sizeof(values)); }
    ThreeMMU32 seed=77123;
    auto scalar=[&]() -> BRS { seed=seed*1664525u+1013904223u; return static_cast<BRS>(seed&0x3fffff)-0x200000; };
    for(int i=0;i<4096;++i) {
        BRS v[6]; for(auto &x:v) x=scalar();
        BRS mac=BR_MAC3(v[0],v[1],v[2],v[3],v[4],v[5]); Check(mac==three_mm_fixed_mac3(v[0],v[1],v[2],v[3],v[4],v[5])); record(&mac,4);
        BRA angle=BR_ATAN2(v[0],v[1]); Check(angle==three_mm_angle_atan2(v[0],v[1])); record(&angle,2);
    }
    for(int i=0;i<128;++i) {
        BMAT34 a,b,out; for(auto &row:a.m) for(auto &x:row) x=scalar(); for(auto &row:b.m) for(auto &x:row) x=scalar();
        ThreeMMActorMatrix ca,cb,co;
        std::memcpy(&ca,&a,sizeof(ca)); std::memcpy(&cb,&b,sizeof(cb));
        three_mm_matrix_multiply(&co,&ca,&cb);
        BrMatrix34Mul(&out,&a,&b); Check(std::memcmp(&out,&co,sizeof(co))==0); record(&out,sizeof(out));
        BMAT34 alias=a; ThreeMMActorMatrix calias=ca;
        BrMatrix34Mul(&alias,&alias,&b); three_mm_matrix_multiply(&calias,&calias,&cb); Check(std::memcmp(&alias,&calias,sizeof(calias))==0);
        alias=b; calias=cb;
        BrMatrix34Mul(&alias,&a,&alias); three_mm_matrix_multiply(&calias,&ca,&calias); Check(std::memcmp(&alias,&calias,sizeof(calias))==0);
        BRA angle=static_cast<BRA>(scalar());
        BrMatrix34PostRotateX(&out,angle); three_mm_matrix_rotate(&co,THREE_MM_MATRIX_X,angle,0); Check(std::memcmp(&out,&co,sizeof(co))==0);
        BrMatrix34PostRotateY(&out,static_cast<BRA>(angle+1234)); three_mm_matrix_rotate(&co,THREE_MM_MATRIX_Y,static_cast<unsigned short>(angle+1234),0); Check(std::memcmp(&out,&co,sizeof(co))==0);
        BrMatrix34PostRotateZ(&out,static_cast<BRA>(angle+4567)); three_mm_matrix_rotate(&co,THREE_MM_MATRIX_Z,static_cast<unsigned short>(angle+4567),0); Check(std::memcmp(&out,&co,sizeof(co))==0);
        BrMatrix34PreRotateX(&out,static_cast<BRA>(angle+9876)); three_mm_matrix_rotate(&co,THREE_MM_MATRIX_X,static_cast<unsigned short>(angle+9876),1); Check(std::memcmp(&out,&co,sizeof(co))==0);
        BrMatrix34PostScale(&out,BR_SCALAR(1.5),BR_SCALAR(-0.75),BR_SCALAR(0.5));
        three_mm_matrix_scale(&co,BR_SCALAR(1.5),BR_SCALAR(-0.75),BR_SCALAR(0.5)); Check(std::memcmp(&out,&co,sizeof(co))==0); record(&out,sizeof(out));
    }
    if(file) Check(std::fclose(file)==0);
}

static void TestActorPoseEvaluation()
{
    FILE *snapshot=nullptr;
    if(const char *path=std::getenv("THREE_MM_ACTOR_POSE_SNAPSHOT")) { snapshot=std::fopen(path,"wb"); Check(snapshot); }
    auto record=[&](const void *p,size_t n) { if(snapshot) Check(std::fwrite(p,1,n,snapshot)==n); };
    auto outputs=[&](BMAT34 *m,BRA *x,BRA *y,BRA *z,LegacyUlong *axes) {
        LegacyLong present[]={m!=nullptr,x!=nullptr,y!=nullptr,z!=nullptr,axes!=nullptr}; record(present,sizeof(present));
        if(m) record(m,sizeof(*m)); if(x) record(x,2); if(y) record(y,2); if(z) record(z,2); if(axes) record(axes,4);
    };
    auto rotate=[&](LegacyUlong flags,RoutePoint *p,BMAT34 *m,BRA *x=nullptr,BRA *y=nullptr,BRA *z=nullptr,LegacyUlong *axes=nullptr) {
        ActorPoseEvaluator::ApplyVectorRotation(flags,p,m,x,y,z,axes); outputs(m,x,y,z,axes); record(p,sizeof(*p));
    };
    auto orient=[&](const ActorFrameState &state,const ActorTrack &t,BMAT34 *m,BRA *x=nullptr,BRA *y=nullptr,BRA *z=nullptr,LegacyUlong *axes=nullptr) {
        ActorPoseEvaluator::RouteOrientation(state,t,m,x,y,z,axes); outputs(m,x,y,z,axes); RecordActorFrameState(state,snapshot);
    };
    auto update=[&](ActorFrameState &state,const ActorTrack &t,const ActorPoseInputs &in,const RoutePoint *p,BMAT34 *m) {
        ActorPoseEvaluator::Update(state,t,in,p,m); record(m,sizeof(*m)); RecordActorFrameState(state,snapshot);
    };
    RoutePoint vector = {rOne, rOne, rZero};
    BMAT34 matrix;
    BrMatrix34Identity(&matrix);
    BRA x = 123, y = 123, z = 123;
    LegacyUlong axes = 0;
    rotate(factnRotateX, &vector, &matrix, &x, &y, &z, &axes);
    Check(x == static_cast<BRA>(0xe000) && y == aZero && z == aZero && axes == factnRotateX);
    // Each output is independently optional; requesting bank must not write pitch.
    z = 123;
    rotate(factnRotateX, &vector, pvNil, pvNil, pvNil, &z);
    Check(z == aZero);
    rotate(factnRotateY, &vector, pvNil, &x, &y, &z, &axes);
    Check(x == aZero && y == static_cast<BRA>(0x4000) && z == aZero && axes == factnRotateY);
    rotate(factnRotateX | factnRotateY, &vector, pvNil, &x, &y, &z, &axes);
    Check(x == static_cast<BRA>(0xe000) && y == static_cast<BRA>(0x4000) && z == aZero &&
          axes == (factnRotateX | factnRotateY));
    vector = {rZero, rZero, rZero};
    rotate(factnRotateX | factnRotateY, &vector, pvNil, &x, &y, &z, &axes);
    Check(x == aZero && y == aZero && z == aZero && axes == 0);

    RoutePoint aliasVector={rOne,rOne,rOne};
    BrMatrix34Identity(&matrix);
    rotate(factnRotateX|factnRotateY,&aliasVector,&matrix,&x,&y,&z,reinterpret_cast<LegacyUlong *>(&aliasVector.dyr));
    aliasVector={rOne,rOne,rOne}; BrMatrix34Identity(&matrix);
    rotate(factnRotateX|factnRotateY,&aliasVector,&matrix,&x,&y,&z,reinterpret_cast<LegacyUlong *>(&matrix.m[0][0]));
    ActorTrack track;
    Check(track.FCreate());
    RouteDistancePoint point = {{rZero, rZero, rZero}, BR_SCALAR(4)};
    Check(track.Route()->FInsert(0, &point));
    point = {{BR_SCALAR(4), rZero, rZero}, rZero};
    Check(track.Route()->FInsert(1, &point));
    ActorEvent::Base event = {ActorEvent::aetAdd, 1, {0, rZero, 0}};
    ActorEvent::Add addition = {};
    addition.ya = BR_ANGLE_DEG(45);
    Check(track.Events()->FInsert(0, sizeof(addition), &addition, &event));
    ActorFrameState state;
    state.addEvent = 0;
    state.actionFlags = factnRotateY;
    state.frame = 2;
    state.location = {0, BR_SCALAR(2), 0};
    RoutePoint position = {BR_SCALAR(2), rZero, rZero};
    ActorPoseInputs inputs;
    update(state, track, inputs, &position, &matrix);
    Check(state.position.dxr == BR_SCALAR(2) && state.transform.yaPath == static_cast<BRA>(0x4000));
    Check(BrsAbs(matrix.m[0][0]) <= BR_SCALAR(0.001) &&
          BrsAbs(BrsSub(BrsAbs(matrix.m[0][2]), rOne)) <= BR_SCALAR(0.001));
    BMAT34 moving = matrix;
    state.useCurrentRotation = true;
    update(state, track, inputs, &position, &matrix);
    Check(std::memcmp(&matrix, &moving, sizeof(matrix)) == 0); // Static frames keep the last full rotation.

    state.useCurrentRotation = false;
    inputs.fixedPathOrientation = true;
    update(state, track, inputs, &position, &matrix);
    Check(state.transform.yaPath == BR_ANGLE_DEG(45));
    inputs.fixedPathOrientation = false;
    state.location = {0, rZero, 0};
    position = {rZero, rZero, rZero};
    update(state, track, inputs, &position, &matrix);
    Check(state.transform.yaPath == BR_ANGLE_DEG(45)); // Authored orientation at the entrance.
    state.location = {1, rZero, 0};
    position = {BR_SCALAR(4), rZero, rZero};
    update(state, track, inputs, &position, &matrix);
    Check(state.transform.yaPath == static_cast<BRA>(0x4000)); // End node retains the route tangent.

    state.InitTransform();
    state.useCurrentRotation = true;
    state.transform.aevpull = {BR_SCALAR(3), rOne, rOne};
    state.transform.rScaleStep = BR_SCALAR(2);
    update(state, track, inputs, &position, &matrix);
    Check(BrsAbs(BrsSub(matrix.m[0][0], BR_SCALAR(6))) <= BR_SCALAR(0.001) &&
          BrsAbs(BrsSub(matrix.m[1][1], BR_SCALAR(2))) <= BR_SCALAR(0.001) &&
          BrsAbs(BrsSub(matrix.m[2][2], BR_SCALAR(2))) <= BR_SCALAR(0.001));
    state.InitTransform();
    inputs.restY = BR_ANGLE_DEG(90);
    update(state, track, inputs, &position, &matrix);
    Check(BrsAbs(matrix.m[0][0]) <= BR_SCALAR(0.001) &&
          BrsAbs(BrsSub(BrsAbs(matrix.m[0][2]), rOne)) <= BR_SCALAR(0.001));

    // Noncommuting combination: rest Z quarter-turn, stretch (2,3,4),
    // then Y quarter-turn. Unit basis vectors map to (0,3,0), (0,0,2), (4,0,0).
    inputs.restY = aZero;
    inputs.restZ = static_cast<BRA>(0x4000);
    addition.ya = aZero;
    track.Events()->Put(0, &addition);
    state.location = {0, rZero, 0};
    BRS expected[3][3] = {{rZero, BR_SCALAR(3), rZero}, {rZero, rZero, BR_SCALAR(2)}, {BR_SCALAR(4), rZero, rZero}};
    for (LegacyLong mode = 0; mode < 2; ++mode)
    {
        state.InitTransform();
        state.useCurrentRotation = mode == 0;
        state.transform.aevpull = {BR_SCALAR(2), BR_SCALAR(3), BR_SCALAR(4)};
        BrMatrix34RotateY(&state.transform.bmat34Cur, static_cast<BRA>(0x4000));
        BrMatrix34RotateY(&state.transform.bmat34Fwd, static_cast<BRA>(0x4000));
        update(state, track, inputs, &position, &matrix);
        for (LegacyLong row = 0; row < 3; ++row)
            for (LegacyLong column = 0; column < 3; ++column)
                Check(BrsAbs(BrsSub(matrix.m[row][column], expected[row][column])) <= BR_SCALAR(0.002));
    }

    {
        ActorTrack curved;
        Check(curved.FCreate());
        RouteDistancePoint curve[] = {{{BR_SCALAR(100), rZero, BR_SCALAR(100)}, rZero},
                                      {{rZero, rZero, rZero}, BR_SCALAR(4)},
                                      {{rZero, rZero, BR_SCALAR(4)}, BR_SCALAR(4)},
                                      {{BR_SCALAR(4), rZero, BR_SCALAR(4)}, BR_SCALAR(4)},
                                      {{BR_SCALAR(4), rZero, rZero}, rZero}};
        for (LegacyLong index = 0; index < 5; ++index)
            Check(curved.Route()->FInsert(index, &curve[index]));
        event.rtel.irpt = 1; // Ignore the preceding, unrelated subroute.
        Check(curved.Events()->FInsert(0, sizeof(addition), &addition, &event));
        state.location = {3, BR_SCALAR(2), 0};
        BrMatrix34Identity(&matrix);
        orient(state, curved, &matrix, &x, &y, &z, &axes);
        // Weighted tangent = 4*(-Z) + 2*(+X) + 1*(+Z) = (2,0,-3).
        Check(y > BR_ANGLE_DEG(145) && y < BR_ANGLE_DEG(148) && x == aZero && z == aZero && axes == factnRotateY);
        state.location = {2, BR_SCALAR(2), 0};
        BrMatrix34Identity(&matrix);
        orient(state, curved, &matrix, &x, &y, &z, &axes);
        // At the second segment only 4*(+X) + 2*(+Z) contributes.
        Check(y > BR_ANGLE_DEG(62) && y < BR_ANGLE_DEG(65));
    }

    // A single point has no route tangent and leaves the caller's matrix intact.
    point.dwr = rZero;
    track.Route()->Put(0, &point);
    state.location = {0, rZero, 0};
    BrMatrix34Identity(&matrix);
    orient(state, track, &matrix, &x, &y, &z, &axes);
    Check(x == aZero && y == aZero && z == aZero && axes == 0 && matrix.m[0][0] == rOne);
    point={{rZero,rZero,rZero},BR_SCALAR(4)}; track.Route()->Put(0,&point);
    // Exact matrices and field records across varied rest/path/scale combinations.
    ThreeMMU32 seed=12345;
    auto next=[&]() { seed=seed*1664525u+1013904223u; return seed; };
    for(int i=0;i<256;++i) {
        RouteDistancePoint endpoint={{static_cast<BRS>(next()%0x60000)-0x30000,static_cast<BRS>(next()%0x60000)-0x30000,static_cast<BRS>(next()%0x60000)-0x30000},rZero};
        track.Route()->Put(1,&endpoint);
        RoutePoint direction=endpoint.xyz; BrMatrix34Identity(&matrix);
        rotate(i&7,&direction,&matrix,&x,&y,&z,&axes);
        ActorFrameState sample; sample.addEvent=0; sample.actionFlags=i&7; sample.frame=i+2;
        sample.location={0,BR_SCALAR(2),0}; sample.useCurrentRotation=(i&1)!=0;
        BrMatrix34RotateY(&sample.transform.bmat34Fwd,static_cast<BRA>(next()));
        BrMatrix34RotateX(&sample.transform.bmat34Cur,static_cast<BRA>(next()));
        sample.transform.aevpull={static_cast<BRS>(next()%0x50000)-0x10000,static_cast<BRS>(next()%0x50000)-0x10000,static_cast<BRS>(next()%0x50000)-0x10000};
        sample.transform.rScaleStep=static_cast<BRS>(next()%0x30000)-0x10000;
        ActorPoseInputs rest; rest.restX=static_cast<BRA>(next()); rest.restY=static_cast<BRA>(next()); rest.restZ=static_cast<BRA>(next()); rest.fixedPathOrientation=(i&2)!=0;
        RoutePoint p={BR_SCALAR(i%5),BR_SCALAR(i%3),BR_SCALAR(i%7)};
        update(sample,track,rest,&p,&matrix);
        if((i&15)==0) update(sample,track,rest,&sample.position,&sample.transform.bmat34Cur);
        if((i&15)==1 || (i&15)==2) update(sample,track,rest,&sample.position,&sample.transform.bmat34Fwd);
        if((i&15)==3) update(sample,track,rest,&sample.position,&sample.transform.bmat34Cur);
    }
    if(snapshot) Check(std::fclose(snapshot)==0);
}

struct FrameTrace
{
    ActorFrameCommand commands[128];
    LegacyLong count = 0;
    LegacyLong failIndex = ivNil;
    LegacyLong failures = 0;
    LegacyLong ordinarySounds = 0;
    LegacyLong motionRegistrations = 0;
    LegacyLong refreshEntrance = ivNil;
    LegacyLong refreshed = 0;
};

static FILE *actorFrameSnapshot = nullptr;
static bool actorFrameSnapshotInitialized = false;
static void RecordActorFrameBytes(const void *p, size_t n)
{
    if (actorFrameSnapshot) Check(std::fwrite(p, 1, n, actorFrameSnapshot) == n);
}
static void RecordActorFrameState(const ActorFrameState &s, FILE *snapshot = actorFrameSnapshot)
{
    if (!snapshot) return;
    auto bytes = [&](const void *p,size_t n) { Check(std::fwrite(p,1,n,snapshot)==n); };
    int flags[] = {s.onStage,s.frozen,s.useCurrentRotation}; bytes(flags,sizeof(flags));
    LegacyLong values[] = {s.step,s.action,s.celCount,s.cel,s.frame,s.nextEvent,s.firstEventInFrame,s.actionEvent,s.addEvent};
    bytes(values,sizeof(values)); bytes(&s.actionFlags,4);
    bytes(&s.routeTranslation,12); bytes(&s.subrouteTranslation,12);
    bytes(&s.location,12); bytes(&s.position,12);
    bytes(&s.transform.bmat34Fwd,48); bytes(&s.transform.aevpull,12);
    bytes(&s.transform.rScaleStep,4); bytes(&s.transform.bmat34Cur,48);
    bytes(&s.transform.xaPath,2); bytes(&s.transform.yaPath,2); bytes(&s.transform.zaPath,2);
}

// A headless resource/effect adapter with fixed action facts. No renderer,
// audio device or application is involved; sound requests are counted only.
static ActorFrameResult RunCoreFrame(ActorFrameState &state, ActorTrack &track, const ActorFrameInputs &inputs,
                                     bool dirty, FrameTrace &trace)
{
    if (!actorFrameSnapshotInitialized) {
        actorFrameSnapshotInitialized = true;
        if (const char *path = std::getenv("THREE_MM_ACTOR_FRAME_SNAPSHOT")) { actorFrameSnapshot = std::fopen(path,"wb"); Check(actorFrameSnapshot); }
    }
    LegacyLong inputValues[] = {inputs.firstFrame,inputs.step,inputs.stepSucceeded,inputs.recording,inputs.rejoining,inputs.displayFrame,inputs.suppressSounds,dirty};
    RecordActorFrameBytes(inputValues,sizeof(inputValues)); RecordActorFrameState(state);
    ActorFrameEvaluator evaluator(state, track, inputs, dirty);
    for (;;)
    {
        ActorFrameCommand command = evaluator.Next();
        LegacyLong commandValues[] = {static_cast<LegacyLong>(command.kind),command.eventIndex};
        RecordActorFrameBytes(commandValues,sizeof(commandValues)); RecordActorFrameState(state);
        if (command.kind == ActorFrameCommandKind::Finished)
            break;
        Check(trace.count < 128);
        trace.commands[trace.count++] = command;
        ActorEvent::Base event;
        track.Events()->GetFixed(command.eventIndex, &event);
        if (command.kind == ActorFrameCommandKind::CheckEntrance)
        {
            if (command.eventIndex == trace.refreshEntrance)
            {
                event.nfrm = static_cast<int32_t>(state.frame);
                track.Events()->PutFixed(command.eventIndex, &event);
                ++trace.refreshed;
            }
            evaluator.Complete(event.nfrm <= state.frame);
            continue;
        }
        event.nfrm = static_cast<int32_t>(state.frame);
        track.Events()->PutFixed(command.eventIndex, &event);
        if (command.eventIndex == trace.failIndex && trace.failures > 0)
        {
            --trace.failures;
            evaluator.Complete(false);
            continue;
        }
        RoutePoint origin = {};
        ActorValueEventResult applied = state.ApplyValueEvent(track, command.eventIndex, origin);
        if (applied == ActorValueEventResult::NeedsRuntime)
        {
            switch (event.aet)
            {
            case ActorEvent::aetAdd: {
                ActorEvent::Add addition;
                track.Events()->Get(command.eventIndex, &addition);
                state.subrouteTranslation = {addition.dxr, addition.dyr, addition.dzr};
                state.UpdateOrigin(origin);
                state.InitTransform();
                BrMatrix34PostRotateX(&state.transform.bmat34Cur, addition.xa);
                BrMatrix34PostRotateY(&state.transform.bmat34Cur, addition.ya);
                BrMatrix34PostRotateZ(&state.transform.bmat34Cur, addition.za);
                state.transform.xaPath = addition.xa;
                state.transform.yaPath = addition.ya;
                state.transform.zaPath = addition.za;
                state.firstEventInFrame = state.addEvent = command.eventIndex;
                state.location = event.rtel;
                ActorRouteEvaluator::Position(track, &state.location, &state.position);
                state.onStage = true;
                break;
            }
            case ActorEvent::aetActn: {
                ActorEvent::Action action;
                track.Events()->Get(command.eventIndex, &action);
                state.action = action.anid;
                state.cel = action.celn;
                state.celCount = 4;
                state.actionFlags = factnRotateY;
                state.actionEvent = command.eventIndex;
                state.location = event.rtel;
                ActorRouteEvaluator::Position(track, &state.location, &state.position);
                break;
            }
            case ActorEvent::aetRem:
                state.onStage = false;
                break;
            case ActorEvent::aetSnd: {
                ActorEvent::Sound sound;
                track.Events()->Get(command.eventIndex, &sound);
                if (sound.celn == ivNil)
                    ++trace.ordinarySounds;
                else
                    ++trace.motionRegistrations;
                break;
            }
            default:
                Check(false);
            }
        }
        evaluator.Complete(applied != ActorValueEventResult::Invalid);
    }
    ActorFrameResult result = evaluator.Result();
    if (!result.aborted && (result.positionBody || result.updatePose))
    {
        ActorPoseInputs poseInputs;
        BMAT34 matrix;
        ActorPoseEvaluator::Update(state, track, poseInputs, &state.position, &matrix);
    }
    int flags[] = {result.success,result.aborted,result.positionDirty,result.soundInFrame,result.considerMotionSounds,result.positionBody,result.updatePose,result.selectCel};
    RecordActorFrameBytes(flags,sizeof(flags)); RecordActorFrameState(state);
    LegacyLong effects[] = {trace.ordinarySounds,trace.motionRegistrations,trace.failures,trace.refreshed};
    RecordActorFrameBytes(effects,sizeof(effects));
    return result;
}

static void TestActorFrameSequencing()
{
    ActorTrack track;
    Check(ActorFrameTest::FCreateTrack(track));
    ActorFrameState sequential;
    sequential.RewindRoute(track);
    bool dirty = false;
    LegacyLong sounds = 0;
    for (LegacyLong frame = 1; frame <= 9; ++frame)
    {
        sequential.frame = frame;
        sequential.firstEventInFrame = sequential.nextEvent;
        ActorFrameInputs inputs;
        inputs.firstFrame = 1;
        inputs.step = BrsMul(sequential.step, sequential.transform.rScaleStep);
        FrameTrace trace;
        ActorFrameResult result = RunCoreFrame(sequential, track, inputs, dirty, trace);
        Check(result.success && !result.aborted);
        sounds += trace.ordinarySounds;
        dirty = result.positionDirty && !result.positionBody;
        if (frame == 1)
            Check(sequential.cel == 0 && sequential.onStage);
        if (frame == 2)
            Check(sequential.cel == 1 && result.soundInFrame && trace.ordinarySounds == 1 &&
                  trace.motionRegistrations == 1);
        if (frame == 3)
            Check(sequential.cel == 2 && sequential.frozen && sequential.position.dyr == rOne);
        if (frame == 4)
            Check(sequential.position.dxr == BR_SCALAR(6) && sequential.position.dyr == rZero &&
                  sequential.step == rZero);
        if (frame == 6)
            Check(!sequential.onStage && !result.selectCel && !result.considerMotionSounds);
        if (frame == 8)
            Check(sequential.onStage && sequential.position.dxr == BR_SCALAR(20) &&
                  sequential.routeTranslation.dxr == BR_SCALAR(100));

        // Fresh seek to the same frame: intermediate ordinary sounds are skipped,
        // while state and motion-match registrations remain deterministic.
        ActorFrameState seek;
        seek.RewindRoute(track);
        bool seekDirty = false;
        LegacyLong seekSounds = 0;
        for (LegacyLong seekFrame = 1; seekFrame <= frame; ++seekFrame)
        {
            seek.frame = seekFrame;
            seek.firstEventInFrame = seek.nextEvent;
            inputs.displayFrame = seekFrame == frame;
            inputs.step = BrsMul(seek.step, seek.transform.rScaleStep);
            FrameTrace seekTrace;
            ActorFrameResult seekResult = RunCoreFrame(seek, track, inputs, seekDirty, seekTrace);
            Check(seekResult.success);
            seekDirty = seekResult.positionDirty && !seekResult.positionBody;
            seekSounds += seekTrace.ordinarySounds;
        }
        Check(ActorFrameTest::SameState(sequential, seek));
        Check(seekSounds == (frame == 2 ? 1 : 0));
    }
    Check(sounds == 1);
    ActorFrameInputs inputs;
    inputs.firstFrame = 1;
    inputs.displayFrame = true;
    inputs.suppressSounds = true;
    ActorFrameState suppressed;
    suppressed.RewindRoute(track);
    for (LegacyLong frame = 1; frame <= 2; ++frame)
    {
        suppressed.frame = frame;
        inputs.step = suppressed.step;
        FrameTrace trace;
        ActorFrameResult result = RunCoreFrame(suppressed, track, inputs, false, trace);
        Check(!result.considerMotionSounds && trace.ordinarySounds == 0);
        if (frame == 2)
            Check(result.soundInFrame && trace.motionRegistrations == 1);
    }

    // Multiple due entrances are consumed before ordinary events. A future
    // entrance is checked by both the pre-scan and ordered pass.
    ActorTrack entrances;
    Check(entrances.FCreate());
    ActorEvent::Add addition = {};
    for (LegacyLong index = 0; index < 3; ++index)
    {
        RouteDistancePoint point = {{BR_SCALAR(index), rZero, rZero}, rZero};
        ActorEvent::Base event = {ActorEvent::aetAdd,
                                  index == 2 ? 10 : static_cast<int32_t>(index + 1),
                                  {static_cast<int32_t>(index), rZero, 0}};
        Check(entrances.Route()->FInsert(index, &point));
        Check(entrances.Events()->FInsert(index, sizeof(addition), &addition, &event));
    }
    ActorFrameState state;
    state.frame = 3;
    FrameTrace trace;
    inputs = ActorFrameInputs();
    inputs.firstFrame = 1;
    ActorFrameResult result = RunCoreFrame(state, entrances, inputs, false, trace);
    Check(trace.count == 6 && trace.commands[0].kind == ActorFrameCommandKind::CheckEntrance &&
          trace.commands[1].kind == ActorFrameCommandKind::ApplyEvent && trace.commands[1].eventIndex == 0 &&
          trace.commands[3].eventIndex == 1 && trace.commands[4].eventIndex == 2 && trace.commands[5].eventIndex == 2);
    Check(state.nextEvent == 2 && state.cel == 1 && result.selectCel);
    state = ActorFrameState();
    state.frame = 3;
    FrameTrace failedEntrance;
    failedEntrance.failIndex = 0;
    failedEntrance.failures = 1;
    result = RunCoreFrame(state, entrances, inputs, true, failedEntrance);
    Check(result.aborted && !result.success && state.cel == 0 && state.nextEvent == 0 && !result.positionBody &&
          !result.selectCel);
    state = ActorFrameState();
    state.frame = 3;
    FrameTrace refreshedEntrance;
    refreshedEntrance.refreshEntrance = 2;
    result = RunCoreFrame(state, entrances, inputs, false, refreshedEntrance);
    Check(result.success && refreshedEntrance.refreshed == 1 && state.nextEvent == 3 && result.considerMotionSounds);

    ActorTrack toggles;
    Check(toggles.FCreate());
    RouteDistancePoint stationary = {{rZero, rZero, rZero}, rZero};
    Check(toggles.Route()->FInsert(0, &stationary));
    ActorEvent::Base toggleEvent = {ActorEvent::aetFreeze, 1, {0, rZero, 0}};
    int32_t freeze = 1;
    Check(toggles.Events()->FInsert(0, sizeof(freeze), &freeze, &toggleEvent));
    state = ActorFrameState();
    state.frame = 2;
    FrameTrace freezeTrace;
    result = RunCoreFrame(state, toggles, inputs, false, freezeTrace);
    Check(result.success && state.frozen && state.cel == 1); // Initial state controls this frame's cel.
    freeze = 0;
    toggles.Events()->Put(0, &freeze);
    state.nextEvent = 0;
    ++state.frame;
    FrameTrace unfreezeTrace;
    result = RunCoreFrame(state, toggles, inputs, false, unfreezeTrace);
    Check(result.success && !state.frozen && state.cel == 1);
    toggles.Events()->Delete(0);
    toggleEvent.aet = ActorEvent::aetActn;
    ActorEvent::Action action = {0, 2};
    Check(toggles.Events()->FInsert(0, sizeof(action), &action, &toggleEvent));
    state.nextEvent = 0;
    ++state.frame;
    FrameTrace actionTrace;
    result = RunCoreFrame(state, toggles, inputs, false, actionTrace);
    Check(result.success && state.cel == 2); // New action suppresses the normal increment.

    ActorTrack failures;
    Check(failures.FCreate());
    RouteDistancePoint point = {{rZero, rZero, rZero}, rZero};
    Check(failures.Route()->FInsert(0, &point));
    ActorEvent::Base event = {ActorEvent::aetSize, 1, {0, rZero, 0}};
    BRS scale = BR_SCALAR(2);
    Check(failures.Events()->FInsert(0, sizeof(scale), &scale, &event));
    event.aet = ActorEvent::aetMove;
    RoutePoint delta = {rOne, rZero, rZero};
    Check(failures.Events()->FInsert(1, sizeof(delta), &delta, &event));
    event.aet = ActorEvent::aetSnd;
    ActorEvent::Sound sound = {};
    sound.tag.sid = ksidInvalid;
    sound.celn = ivNil;
    Check(failures.Events()->FInsert(2, sizeof(sound), &sound, &event));
    state = ActorFrameState();
    state.frame = 2;
    inputs.displayFrame = false;
    FrameTrace ordinaryFailure;
    ordinaryFailure.failIndex = 0;
    ordinaryFailure.failures = 1;
    result = RunCoreFrame(state, failures, inputs, false, ordinaryFailure);
    Check(!result.success && !result.aborted && ordinaryFailure.count == 2 &&
          ordinaryFailure.commands[0].eventIndex == 0 && ordinaryFailure.commands[1].eventIndex == 0 &&
          state.nextEvent == 2 && state.subrouteTranslation.dxr == rZero);

    // Recording skips event commands; a failed step lookup still completes the
    // frame, and accumulated dirty state survives until the final display frame.
    state = ActorFrameState();
    state.frame = 2;
    inputs.recording = true;
    inputs.stepSucceeded = false;
    FrameTrace recording;
    result = RunCoreFrame(state, failures, inputs, true, recording);
    Check(recording.count == 0 && !result.success && !result.aborted && result.positionDirty && result.updatePose);
    inputs.displayFrame = true;
    inputs.stepSucceeded = true;
    FrameTrace finalFrame;
    result = RunCoreFrame(state, failures, inputs, result.positionDirty, finalFrame);
    Check(result.success && result.positionBody && !result.updatePose);
}

static FILE *actorTimelineSnapshot = nullptr;
static FILE *actorSeekSnapshot = nullptr;
static void RecordActorTimelineBytes(FILE *file,const void *p,size_t n) { if(file) Check(std::fwrite(p,1,n,file)==n); }
struct StepProbe
{
    LegacyLong calls = 0;
    LegacyLong failOn = 0;
    LegacyLong cels[32] = {};
    ActorFrameState *observed = nullptr;
    LegacyLong *observedLast = nullptr;
    static bool Resolve(void *context, LegacyLong action, LegacyLong cel, BRS *step)
    {
        auto &probe = *(StepProbe *)context;
        Check(action == 0 && probe.calls < 32);
        Check(*step == rZero);
        probe.cels[probe.calls++] = cel;
        if (probe.observedLast) Check(*probe.observedLast == probe.calls - 2);
        bool success = probe.calls != probe.failOn;
        if (success) *step = BR_SCALAR(((cel % 4 + 4) % 4) + 1);
        FILE *record = actorTimelineSnapshot ? actorTimelineSnapshot : actorSeekSnapshot;
        LegacyLong values[] = {-12345,action,cel,success,*step}; RecordActorTimelineBytes(record,values,sizeof(values));
        if (probe.observed) {
            Check(probe.observed->cel == cel);
            RecordActorFrameState(*probe.observed,record);
        }
        return success;
    }
    ActorStepSource Source()
    {
        return {this, Resolve};
    }
};

static void TestActorTimeline()
{
    if (const char *path=std::getenv("THREE_MM_ACTOR_TIMELINE_SNAPSHOT")) { actorTimelineSnapshot=std::fopen(path,"wb"); Check(actorTimelineSnapshot); }
    auto record=[&](const void *p,size_t n) { RecordActorTimelineBytes(actorTimelineSnapshot,p,n); };
    auto cache=[&](const ActorTrack &t) { LegacyLong count=t.Events()->IvMac(); record(&count,4); for(LegacyLong i=0;i<count;++i) { ActorEvent::Base e; t.Events()->GetFixed(i,&e); record(&e,sizeof(e)); } };
    auto lifetime=[&](const ActorTrack &t,LegacyLong first,bool rejoining,const ActorStepSource &steps,LegacyLong *last,LegacyLong bound=klwMax) {
        bool ok=ActorTimeline::FComputeLifetime(t,first,rejoining,steps,last,bound);
        LegacyLong values[]={101,first,rejoining,bound,ok,*last}; record(values,sizeof(values)); cache(t); return ok;
    };
    auto stalled=[&](const ActorTrack &t,LegacyLong first,RouteLocation *p,LegacyLong *last=nullptr) {
        bool ok=ActorTimeline::FIsStalled(t,first,p,last); LegacyLong values[]={102,first,ok,last?*last:-987}; record(values,sizeof(values)); record(p,sizeof(*p)); return ok;
    };
    auto back=[&](ActorFrameState &state,const ActorTrack &t,const ActorStepSource &steps,RouteLocation *p,bool update) {
        bool ok=ActorTimeline::FStepBack(state,t,steps,p,update); LegacyLong values[]={103,ok,update}; record(values,sizeof(values)); record(p,sizeof(*p)); RecordActorFrameState(state,actorTimelineSnapshot); return ok;
    };
    auto backup=[&](ActorFrameState &state,const ActorTrack &t,LegacyLong last,LegacyLong target,const ActorStepSource &steps) {
        auto result=ActorTimeline::QuickBackup(state,t,last,target,steps); LegacyLong values[]={104,last,target,result.success,result.valid,result.applyEffects}; record(values,sizeof(values)); RecordActorFrameState(state,actorTimelineSnapshot); return result;
    };
    ActorTrack track;
    Check(ActorFrameTest::FCreateTrack(track));
    // Explicit authored steps require no metadata resolver. Rebuild stale
    // non-entrance frame caches, retaining the scheduled entrance frames.
    for (LegacyLong index = 0; index < track.Events()->IvMac(); ++index)
    {
        ActorEvent::Base event;
        track.Events()->GetFixed(index, &event);
        if (event.aet != ActorEvent::aetAdd)
        {
            event.nfrm = -777;
            track.Events()->PutFixed(index, &event);
        }
    }
    LegacyLong last = -999;
    Check(lifetime(track, 1, false, ActorStepSource(), &last) && last == 9);
    LegacyLong frames[] = {1, 1, 1, 2, 2, 3, 3, 4, 6, 8, 9};
    Check(track.Events()->IvMac() == 11);
    for (LegacyLong index = 0; index < 11; ++index)
    {
        ActorEvent::Base event;
        track.Events()->GetFixed(index, &event);
        Check(event.nfrm == frames[index]);
    }
    ActorTrack empty;
    Check(empty.FCreate());
    Check(lifetime(empty, -5, false, ActorStepSource(), &last) && last == -5);

    ActorTrack metadata;
    Check(ActorFrameTest::FCreateMetadataTrack(metadata, false));
    StepProbe probe;
    Check(lifetime(metadata, -2, false, probe.Source(), &last) && last == 0);
    Check(probe.calls == 3 && probe.cels[0] == 1 && probe.cels[1] == 2 && probe.cels[2] == 3);
    Check(ActorFrameTest::FCreateMetadataTrack(metadata, true));
    probe = StepProbe();
    Check(lifetime(metadata, -2, false, probe.Source(), &last) && last == 0);
    Check(probe.calls == 3 && probe.cels[0] == 1 && probe.cels[1] == 2 && probe.cels[2] == 2);
    Check(ActorFrameTest::FCreateMetadataTrack(metadata, true));
    probe = StepProbe();
    probe.failOn = 2;
    Check(!lifetime(metadata, -2, false, probe.Source(), &last) && last == 0);
    ActorEvent::Base event;
    metadata.Events()->GetFixed(1, &event);
    Check(event.nfrm == -2);
    metadata.Events()->GetFixed(4, &event);
    Check(event.nfrm == -1); // Earlier successful frames remain cached after failure.
    RouteLocation stopped = {0, rZero, 0};
    LegacyLong lastEvent = -99;
    Check(!stalled(metadata, 0, &stopped, &lastEvent) && lastEvent == -99);
    Check(stalled(metadata, 3, &stopped, &lastEvent) && lastEvent == 4);

    ActorTrack route;
    Check(route.FCreate());
    for (LegacyLong index = 0; index < 4; ++index)
    {
        RouteDistancePoint point = {{BR_SCALAR(index * 4), rZero, rZero}, index == 3 ? rZero : BR_SCALAR(4)};
        Check(route.Route()->FInsert(index, &point));
    }
    event = {ActorEvent::aetAdd, 1, {0, rZero, 0}};
    ActorEvent::Add addition = {};
    Check(route.Events()->FInsert(0, sizeof(addition), &addition, &event));
    ActorFrameState initial;
    initial.frame = 10;
    initial.cel = 3;
    initial.step = kdwrNil;
    initial.transform.rScaleStep = BR_SCALAR(2);
    initial.addEvent = 0;
    initial.nextEvent = 1;
    initial.location = {2, BR_SCALAR(2), 0};
    initial.position = {BR_SCALAR(10), rZero, rZero};
    ActorFrameState state = initial;
    RouteLocation output = {99, BR_SCALAR(99), 99};
    probe = StepProbe();
    Check(back(state, route, probe.Source(), &output, false));
    Check(output.irpt == 1 && output.dwrOffset == rZero && probe.cels[0] == 2);
    Check(ActorFrameTest::SameState(state, initial));
    probe = StepProbe();
    probe.failOn = 1;
    output = {99, BR_SCALAR(99), 99};
    Check(!back(state, route, probe.Source(), &output, false));
    Check(output.irpt == 99 && ActorFrameTest::SameState(state, initial));

    probe = StepProbe();
    probe.failOn = 2;
    Check(back(state, route, probe.Source(), &state.location, true));
    Check(state.frame == 9 && state.cel == 2 && state.location.irpt == 1 && state.location.dwrOffset == rZero);
    ActorFrameState afterFirst = state;
    Check(!back(state, route, probe.Source(), &state.location, true));
    Check(ActorFrameTest::SameState(state, afterFirst));
    state = initial;
    probe = StepProbe();
    probe.failOn = 2;
    ActorQuickSeekResult quick = backup(state, route, 20, 8, probe.Source());
    Check(!quick.success && !quick.valid && !quick.applyEffects);
    Check(ActorFrameTest::SameState(state, afterFirst)); // Individual steps, not the entire seek, roll back.

    state = initial;
    state.step = rOne;
    state.transform.rScaleStep = rOne;
    state.cel = 9;
    quick = backup(state, route, 20, 8, ActorStepSource());
    Check(quick.success && quick.valid && quick.applyEffects && state.frame == 8 && state.cel == 7);
    Check(state.position.dxr == BR_SCALAR(8));
    state = initial;
    state.frame = 13;
    state.location = {3, rZero, 4};
    quick = backup(state, route, 9, 12, ActorStepSource());
    Check(quick.success && quick.valid && !quick.applyEffects && state.frame == 12 && state.location.dnfrm == 3);

    ActorTrack vertical;
    Check(vertical.FCreate());
    RouteDistancePoint points[] = {{{rZero, rZero, rZero}, BR_SCALAR(4)},
                                   {{rZero, BR_SCALAR(4), rZero}, BR_SCALAR(4)},
                                   {{BR_SCALAR(4), BR_SCALAR(4), rZero}, rZero}};
    for (LegacyLong index = 0; index < 3; ++index)
        Check(vertical.Route()->FInsert(index, &points[index]));
    Check(vertical.Events()->FInsert(0, sizeof(addition), &addition, &event));
    state = initial;
    state.step = BR_SCALAR(2);
    state.transform.rScaleStep = rOne;
    state.location = {1, BR_SCALAR(2), 0};
    state.position = {BR_SCALAR(2), BR_SCALAR(4), rZero};
    quick = backup(state, vertical, 20, 9, ActorStepSource());
    Check(quick.success && !quick.valid && !quick.applyEffects && state.frame == 9);
    Check(state.position.dxr == rZero && state.position.dyr == BR_SCALAR(4));

    state = initial;
    state.step = rZero;
    probe = StepProbe();
    probe.failOn = 1;
    Check(back(state, route, probe.Source(), &state.location, true));
    Check(probe.calls == 0 && state.frame == 9 && state.location.dnfrm == -1);
    state=initial; probe=StepProbe(); probe.observed=&state;
    Check(back(state,route,probe.Source(),&output,false)); Check(ActorFrameTest::SameState(state,initial));
    probe=StepProbe(); probe.observed=&state; probe.failOn=1;
    Check(!back(state,route,probe.Source(),&state.location,true)); Check(ActorFrameTest::SameState(state,initial));
    ActorTrack bounded; Check(ActorFrameTest::FCreateMetadataTrack(bounded,false)); probe=StepProbe();
    Check(!lifetime(bounded,-2,false,probe.Source(),&last,0) && last==-1);
    Check(!lifetime(bounded,klwMin,false,probe.Source(),&last,-1) && last==klwMin);
    Check(!lifetime(bounded,klwMax,false,probe.Source(),&last) && last==klwMax);
    if(actorTimelineSnapshot) Check(std::fclose(actorTimelineSnapshot)==0);
    actorTimelineSnapshot=nullptr;
    ActorTrack observation; Check(ActorFrameTest::FCreateMetadataTrack(observation,false));
    StepProbe publication; publication.observedLast=&last;
    Check(ActorTimeline::FComputeLifetime(observation,-2,false,publication.Source(),&last) && last==0);
}

struct SeekTrace
{
    ActorSeekCommand commands[128];
    LegacyLong frames[128];
    LegacyLong count = 0;
    LegacyLong failFrame = knfrmInvalid;
    bool failQuickEffects = false;
    bool recording = false;
    LegacyLong ordinarySounds = 0;
    LegacyLong motionRegistrations = 0;
};

static bool RunCoreSeek(ActorFrameState &state, ActorTrack &track, LegacyLong first, LegacyLong last, LegacyLong target,
                        const ActorStepSource &steps, SeekTrace &trace)
{
    if (!actorSeekSnapshot) if (const char *path=std::getenv("THREE_MM_ACTOR_SEEK_SNAPSHOT")) { actorSeekSnapshot=std::fopen(path,"wb"); Check(actorSeekSnapshot); }
    auto record=[&](const void *p,size_t n) { RecordActorTimelineBytes(actorSeekSnapshot,p,n); };
    LegacyLong inputs[]={201,first,last,target}; record(inputs,sizeof(inputs)); RecordActorFrameState(state,actorSeekSnapshot);
    ActorSeekEvaluator evaluator(state, track, first, last, target, steps);
    bool dirty = false;
    for (;;)
    {
        ActorSeekCommand command = evaluator.Next();
        LegacyLong values[]={202,static_cast<LegacyLong>(command.kind),command.displayFrame,evaluator.Success()}; record(values,sizeof(values)); RecordActorFrameState(state,actorSeekSnapshot);
        if (command.kind == ActorSeekCommandKind::Finished) {
            LegacyLong effects[]={trace.count,trace.recording,trace.ordinarySounds,trace.motionRegistrations}; record(effects,sizeof(effects));
            return evaluator.Success();
        }
        Check(trace.count < 128);
        trace.commands[trace.count] = command;
        trace.frames[trace.count++] = state.frame;
        switch (command.kind)
        {
        case ActorSeekCommandKind::Hide:
            state.onStage = false;
            evaluator.Complete(true);
            break;
        case ActorSeekCommandKind::ResetCostume:
            trace.recording = false;
            evaluator.Complete(false); // An unavailable costume reset remains best-effort.
            break;
        case ActorSeekCommandKind::ApplyQuickEffects: {
            ActorPoseInputs inputs;
            BMAT34 matrix;
            ActorPoseEvaluator::Update(state, track, inputs, &state.position, &matrix);
            evaluator.Complete(!trace.failQuickEffects);
            break;
        }
        case ActorSeekCommandKind::EvaluateFrame: {
            ActorFrameInputs inputs;
            inputs.firstFrame = first;
            inputs.displayFrame = command.displayFrame;
            inputs.recording = trace.recording;
            inputs.stepSucceeded = steps.FPlaybackStep(state, &inputs.step);
            FrameTrace frameTrace;
            ActorFrameResult result = RunCoreFrame(state, track, inputs, dirty, frameTrace);
            dirty = result.positionDirty && !result.positionBody;
            trace.ordinarySounds += frameTrace.ordinarySounds;
            trace.motionRegistrations += frameTrace.motionRegistrations;
            evaluator.Complete(result.success && state.frame != trace.failFrame);
            break;
        }
        case ActorSeekCommandKind::Finished:
            Check(false);
        }
    }
}

static void TestActorSeekControl()
{
    ActorTrack track;
    Check(ActorFrameTest::FCreateTrack(track));
    ActorFrameState sequential;
    sequential.RewindRoute(track);
    ActorFrameState expected[14];
    bool dirty = false;
    for (LegacyLong frame = 1; frame <= 13; ++frame)
    {
        sequential.frame = frame;
        sequential.firstEventInFrame = sequential.nextEvent;
        ActorFrameInputs inputs;
        inputs.firstFrame = 1;
        inputs.step = sequential.step;
        FrameTrace trace;
        ActorFrameResult result = RunCoreFrame(sequential, track, inputs, dirty, trace);
        Check(result.success);
        dirty = result.positionDirty && !result.positionBody;
        expected[frame] = sequential;
    }
    ActorFrameState seek;
    LegacyLong order[] = {13, 12, 4, 8, 3, 2, 7, 1, 9, 13};
    for (LegacyLong target : order)
    {
        SeekTrace trace;
        Check(RunCoreSeek(seek, track, 1, 9, target, ActorStepSource(), trace));
        Check(ActorFrameTest::SameState(seek, expected[target]));
        Check(trace.ordinarySounds == (target == 2 ? 1 : 0));
        if (target == 12)
            Check(trace.count == 0); // Beyond-lifetime numeric backup needs no effects.
    }
    SeekTrace unchanged;
    Check(RunCoreSeek(seek, track, 1, 9, 13, ActorStepSource(), unchanged) && unchanged.count == 0);
    SeekTrace beforeFirst;
    beforeFirst.recording = true;
    Check(RunCoreSeek(seek, track, 1, 9, -5, ActorStepSource(), beforeFirst));
    Check(beforeFirst.count == 2 && beforeFirst.commands[0].kind == ActorSeekCommandKind::Hide &&
          beforeFirst.commands[1].kind == ActorSeekCommandKind::ResetCostume && !beforeFirst.recording &&
          !seek.onStage);
    Check(seek.frame == -5);
    SeekTrace beforeFirstForward;
    Check(RunCoreSeek(seek, track, 1, 9, -2, ActorStepSource(), beforeFirstForward));
    Check(beforeFirstForward.count == 0 && seek.frame == -4); // Pin inherited before-first compatibility behavior.
    ActorTrack empty;
    Check(empty.FCreate());
    ActorFrameState emptyState;
    SeekTrace emptyTrace;
    Check(RunCoreSeek(emptyState, empty, knfrmInvalid, knfrmInvalid, 5, ActorStepSource(), emptyTrace));
    Check(emptyState.frame == 5 && emptyTrace.count == 1 &&
          emptyTrace.commands[0].kind == ActorSeekCommandKind::ResetCostume);

    seek = ActorFrameState();
    SeekTrace failedFrame;
    failedFrame.failFrame = 3;
    Check(!RunCoreSeek(seek, track, 1, 9, 5, ActorStepSource(), failedFrame));
    Check(seek.frame == 5 && failedFrame.count == 6 && failedFrame.frames[failedFrame.count - 1] == 5);
    Check(ActorFrameTest::SameState(seek, expected[5]));
    seek = expected[4];
    SeekTrace recording;
    recording.recording = true;
    Check(RunCoreSeek(seek, track, 1, 9, 6, ActorStepSource(), recording));
    Check(recording.recording && seek.onStage && recording.count == 2); // Forward recording skips removal events.
    SeekTrace rewind;
    rewind.recording = true;
    Check(RunCoreSeek(seek, track, 1, 9, 3, ActorStepSource(), rewind));
    Check(!rewind.recording && rewind.commands[0].kind == ActorSeekCommandKind::ResetCostume);
    Check(ActorFrameTest::SameState(seek, expected[3]));

    ActorTrack straight;
    Check(straight.FCreate());
    for (LegacyLong index = 0; index < 4; ++index)
    {
        RouteDistancePoint point = {{BR_SCALAR(index * 4), rZero, rZero}, index == 3 ? rZero : BR_SCALAR(4)};
        Check(straight.Route()->FInsert(index, &point));
    }
    ActorEvent::Base event = {ActorEvent::aetAdd, 1, {0, rZero, 0}};
    ActorEvent::Add addition = {};
    Check(straight.Events()->FInsert(0, sizeof(addition), &addition, &event));
    ActorFrameState quickStart;
    quickStart.frame = 10;
    quickStart.cel = 9;
    quickStart.step = rOne;
    quickStart.location = {2, BR_SCALAR(2), 0};
    quickStart.position = {BR_SCALAR(10), rZero, rZero};
    quickStart.addEvent = 0;
    quickStart.nextEvent = 1;
    quickStart.actionFlags = factnRotateY;
    seek = quickStart;
    SeekTrace quick;
    Check(RunCoreSeek(seek, straight, 1, 20, 8, ActorStepSource(), quick));
    Check(quick.count == 1 && quick.commands[0].kind == ActorSeekCommandKind::ApplyQuickEffects && seek.frame == 8);
    seek = quickStart;
    SeekTrace failedQuick;
    failedQuick.failQuickEffects = true;
    Check(!RunCoreSeek(seek, straight, 1, 20, 8, ActorStepSource(), failedQuick));
    Check(failedQuick.count == 1 && seek.frame == 8); // No replay after host effects fail.
    seek = quickStart;
    seek.step = kdwrNil;
    ActorFrameState beforeFailure = seek;
    StepProbe resolverFailure;
    resolverFailure.failOn = 1;
    SeekTrace numericFailure;
    Check(!RunCoreSeek(seek, straight, 1, 20, 8, resolverFailure.Source(), numericFailure));
    Check(numericFailure.count == 0 && ActorFrameTest::SameState(seek, beforeFailure));

    ActorTrack vertical;
    Check(vertical.FCreate());
    RouteDistancePoint points[] = {{{rZero, rZero, rZero}, BR_SCALAR(4)},
                                   {{rZero, BR_SCALAR(4), rZero}, BR_SCALAR(4)},
                                   {{BR_SCALAR(4), BR_SCALAR(4), rZero}, rZero}};
    for (LegacyLong index = 0; index < 3; ++index)
        Check(vertical.Route()->FInsert(index, &points[index]));
    Check(vertical.Events()->FInsert(0, sizeof(addition), &addition, &event));
    event.aet = ActorEvent::aetStep;
    BRS step = BR_SCALAR(2);
    Check(vertical.Events()->FInsert(1, sizeof(step), &step, &event));
    seek = quickStart;
    seek.frame = 4;
    seek.cel = 4;
    seek.celCount = 4;
    seek.step = step;
    seek.location = {1, BR_SCALAR(2), 0};
    seek.position = {BR_SCALAR(2), BR_SCALAR(4), rZero};
    seek.nextEvent = 2;
    SeekTrace fallback;
    Check(RunCoreSeek(seek, vertical, 1, 20, 3, ActorStepSource(), fallback));
    Check(fallback.count == 4 && fallback.commands[0].kind == ActorSeekCommandKind::ResetCostume &&
          fallback.commands[1].kind == ActorSeekCommandKind::EvaluateFrame && fallback.commands[3].displayFrame);
    Check(seek.frame == 3 && seek.position.dxr == rZero && seek.position.dyr == BR_SCALAR(4));
}

class QueuedSoundToken : public BASE
{
    LegacyLong *_deaths;

  public:
    QueuedSoundToken(LegacyLong *deaths) : _deaths(deaths)
    {
    }
    ~QueuedSoundToken()
    {
        ++*_deaths;
    }
};

static void TestSoundQueueOwnership()
{
    LegacyLong deaths = 0;
    BASE *first = NewObj QueuedSoundToken(&deaths);
    BASE *second = NewObj QueuedSoundToken(&deaths);
    BASE *third = NewObj QueuedSoundToken(&deaths);
    Check(first != pvNil && second != pvNil && third != pvNil);
    SoundPlaybackQueue queue;
    SoundPlaybackRequest request;
    request.sound = first;
    request.style = stySfx;
    request.objectId = 7;
    request.actor = true;
    SoundPlaybackRequest output;
    output.objectId = 99;
    Check(!queue.FEnqueue(request) && first->CactRef() == 1);
    Check(!queue.FDequeue(&output) && output.objectId == 99);
    Check(queue.FInit() && queue.Dispatch() == SoundQueueDispatch::Empty);
    Check(queue.FEnqueue(request) && queue.Count() == 1 && first->CactRef() == 2);
    Check(queue.FInit() && queue.Count() == 1); // Initialization is idempotent.
    request.sound = second;
    Check(queue.FEnqueue(request, true) && first->CactRef() == 2 && second->CactRef() == 1);
    Check(queue.FEnqueue(request) && first->CactRef() == 1 && second->CactRef() == 2);
    request.sound = first;
    request.style = stySpeech;
    Check(queue.FEnqueue(request));
    request.sound = third;
    request.style = stySfx;
    request.actor = false;
    Check(queue.FEnqueue(request));
    request.sound = first;
    request.actor = true;
    request.queue = true;
    Check(queue.FEnqueue(request));
    request.sound = third;
    request.style = styMidi;
    request.objectId = 9;
    request.loop = true;
    request.volume = 123;
    request.priority = 6;
    request.startOffset = 456;
    Check(queue.FEnqueue(request, true) && queue.Count() == 5);
    BASE *order[] = {third, second, first, third, first};
    for (LegacyLong index = 0; index < 5; ++index)
    {
        LegacyLong before = order[index]->CactRef();
        Check(queue.FDequeue(&output) && output.sound == order[index]);
        Check(output.sound->CactRef() == before); // The reference transfers, rather than being released.
        if (index == 0)
            Check(output.style == styMidi && output.objectId == 9 && output.actor && output.queue && output.loop &&
                  output.volume == 123 && output.priority == 6 && output.startOffset == 456);
        ReleasePpo(&output.sound);
    }
    Check(first->CactRef() == 1 && second->CactRef() == 1 && third->CactRef() == 1);
    request = SoundPlaybackRequest();
    request.sound = first;
    request.style = stySfx;
    request.queue = true;
    Check(queue.FEnqueue(request));
    request.sound = second;
    Check(queue.FEnqueue(request));
    request.sound = third;
    request.queue = false;
    Check(queue.FEnqueue(request) && second->CactRef() == 1 && queue.Count() == 2);
    Check(queue.FDequeue(&output) && output.sound == first); // Replace the first stored match, not every match.
    ReleasePpo(&output.sound);
    Check(queue.FDequeue(&output) && output.sound == third);
    ReleasePpo(&output.sound);
    Check(queue.FEnqueue(request));
    queue.duration = kdtimOffMsq;
    request.sound = first;
    Check(queue.FEnqueue(request) && first->CactRef() == 1 && queue.Count() == 1);
    Check(queue.Dispatch() == SoundQueueDispatch::Flush);
    queue.Clear();
    queue.Clear();
    Check(third->CactRef() == 1 && queue.Dispatch() == SoundQueueDispatch::Empty);
    LegacyLong interval = -99;
    Check(queue.FTimeout(&interval) && interval == 0);
    queue.duration = kdtim2Msq;
    Check(queue.FTimeout(&interval) && interval == kdtim2Msq);
    Check(queue.FEnqueue(request) && queue.Dispatch() == SoundQueueDispatch::Play);
    queue.duration = kdtimLongMsq;
    interval = -99;
    Check(!queue.FTimeout(&interval) && interval == -99);
    queue.Clear();
    Check(SoundPlaybackQueue::QueueNumber(stySfx, 7, true) == 0x10020007);
    Check(SoundPlaybackQueue::QueueNumber(stySpeech, 7, false) == 0x20030007);
    Check(SoundPlaybackQueue::QueueNumber(stySfx, 0x18001, true) == static_cast<LegacyLong>(static_cast<short>(0x8001)));

#ifdef DEBUG
    {
        SoundPlaybackQueue failed;
        vmutxMem.Enter();
        vdmglob.dmaglBase.cactDo = 0;
        vdmglob.dmaglBase.cactFail = 1;
        vmutxMem.Leave();
        bool initialized = failed.FInit();
        vmutxMem.Enter();
        vdmglob.dmaglBase.cactFail = 0;
        vmutxMem.Leave();
        vpers->Clear();
        Check(!initialized && failed.Count() == 0 && failed.FInit());
    }
    request.queue = true;
    bool observedFailure = false;
    for (LegacyLong attempt = 0; attempt < 128; ++attempt)
    {
        LegacyLong count = queue.Count();
        LegacyLong refs = first->CactRef();
        vmutxMem.Enter();
        vdmglob.dmaglPv.cactDo = vdmglob.dmaglHq.cactDo = vdmglob.dmaglBase.cactDo = 0;
        vdmglob.dmaglPv.cactFail = vdmglob.dmaglHq.cactFail = vdmglob.dmaglBase.cactFail = 1000;
        vmutxMem.Leave();
        bool enqueued = queue.FEnqueue(request);
        vmutxMem.Enter();
        vdmglob.dmaglPv.cactFail = vdmglob.dmaglHq.cactFail = vdmglob.dmaglBase.cactFail = 0;
        vmutxMem.Leave();
        vpers->Clear();
        if (!enqueued)
        {
            observedFailure = true;
            Check(queue.Count() == count && first->CactRef() == refs);
            break;
        }
    }
    Check(observedFailure);
    queue.Clear();
#endif
    ReleasePpo(&first);
    ReleasePpo(&second);
    ReleasePpo(&third);
    Check(deaths == 3);
    {
        SoundPlaybackQueue owned;
        Check(owned.FInit());
        BASE *token = NewObj QueuedSoundToken(&deaths);
        Check(token != pvNil);
        request.sound = token;
        Check(owned.FEnqueue(request));
        ReleasePpo(&token);
        Check(deaths == 3); // Only the queue now owns the resource.
    }
    Check(deaths == 4);
}

struct PlaybackTrace
{
    MoviePlaybackCommand commands[64];
    bool stopAtCommand[64] = {};
    bool scrollingAtCommand[64] = {};
    LegacyLong count = 0;
    MoviePlaybackCommandKind fail = MoviePlaybackCommandKind::Finished;
    bool stillScrolling = false;
    bool needsScroll = false;
    bool soundPlaying = false;
    WaitReason pauseOnPresent = witNil;
    LegacyLong volume = 100;
    LegacyUlong now = 1001;
    MoviePlaybackPosition position = {0, 2, 1, 3, transDissolve};
};

static void RunPlayback(MoviePlaybackState &state, PlaybackTrace &trace)
{
    MoviePlaybackEvaluator evaluator(state, 1000);
    for (;;)
    {
        MoviePlaybackCommand command = evaluator.Next();
        if (command.kind == MoviePlaybackCommandKind::Finished)
            return;
        Check(trace.count < 64);
        trace.stopAtCommand[trace.count] = state.stopRequested;
        trace.scrollingAtCommand[trace.count] = state.scrolling;
        trace.commands[trace.count++] = command;
        MoviePlaybackReply reply;
        reply.success = command.kind != trace.fail;
        switch (command.kind)
        {
        case MoviePlaybackCommandKind::PresentFrame:
            if (trace.pauseOnPresent != witNil)
                state.wait = trace.pauseOnPresent;
            break;
        case MoviePlaybackCommandKind::ScrollText:
            reply.value = trace.stillScrolling;
            break;
        case MoviePlaybackCommandKind::QueryScrolling:
            reply.value = trace.needsScroll;
            break;
        case MoviePlaybackCommandKind::QuerySound:
            reply.value = trace.soundPlaying;
            break;
        case MoviePlaybackCommandKind::ReadVolume:
            reply.number = trace.volume;
            break;
        case MoviePlaybackCommandKind::ReadTime:
            reply.timestamp = trace.now;
            break;
        case MoviePlaybackCommandKind::ReadPosition:
            reply.position = trace.position;
            break;
        default:
            break;
        }
        // Stop is only recorded here: these traces establish emission/order,
        // while runtime window/device cleanup remains a host responsibility.
        evaluator.Complete(reply);
    }
}

static void TestMoviePlaybackControl()
{
    StudioPreferences preferences;
    Check(preferences.PlaybackFps() < 0);
    for (int choice = 1; choice <= 3; ++choice)
    {
        preferences.playbackRate = choice;
        const auto fps = preferences.PlaybackFps();
        Check(fps == (choice == 1 ? 6 : choice == 2 ? 12 : 24));
        MovieFramePacer pacer;
        pacer.Reset(fps);
        LegacyUlong now = 0;
        for (int frame = 0; frame < static_cast<int>(fps); ++frame)
        {
            pacer.Schedule(now);
            Check(!pacer.TakeDue(pacer.due - 1));
            now = pacer.due;
            Check(pacer.TakeDue(now) && !pacer.TakeDue(now));
        }
        Check(now >= 999 && now <= 1000); // Fractional milliseconds do not accumulate drift.
    }
    preferences.playbackRate = 2;
    preferences.playbackModifier = 1;
    Check(preferences.PlaybackFps() > 16.29 && preferences.PlaybackFps() < 16.30);
    preferences.playbackModifier = 2;
    Check(preferences.PlaybackFps() > 11.02 && preferences.PlaybackFps() < 11.03);
    preferences.playbackRate = 4;
    Check(preferences.PlaybackFps() == 0);
    MovieFramePacer pacer;
    pacer.Reset(0);
    pacer.Schedule(1234);
    Check(pacer.TakeDue(1234));
    pacer.Reset(24);
    pacer.Schedule(0xfffffff0U);
    Check(!pacer.TakeDue(0xfffffff1U) && pacer.TakeDue(25));
    pacer.Schedule(1000);
    pacer.Pause(1010);
    pacer.Resume(2010);
    Check(!pacer.TakeDue(2040) && pacer.TakeDue(2042));
    pacer.Schedule(2100);
    pacer.Reset(12);
    Check(!pacer.TakeDue(10000));
    for (const auto reason : {witUntilClick, witForTime, witUntilSnd})
    {
        MoviePlaybackState uncapped;
        uncapped.framePacer.Reset(0);
        PlaybackTrace paused;
        paused.pauseOnPresent = reason;
        paused.soundPlaying = true;
        RunPlayback(uncapped, paused);
        Check(uncapped.pausing && uncapped.renderedFrames == 0 && !uncapped.skippedClickPauses &&
              !uncapped.skippedTimedPauses && !uncapped.skippedSoundPauses);
        MoviePlaybackState accelerated;
        accelerated.unpaced = true;
        PlaybackTrace trace;
        trace.pauseOnPresent = reason;
        trace.soundPlaying = true;
        RunPlayback(accelerated, trace);
        Check(accelerated.renderedFrames == 1 && !accelerated.pausing && accelerated.wait == witNil);
        Check(trace.count == 8 && trace.commands[4].kind == MoviePlaybackCommandKind::SetAlarm &&
              trace.commands[4].value == 0);
        Check(accelerated.skippedClickPauses == (reason == witUntilClick) &&
              accelerated.skippedTimedPauses == (reason == witForTime) &&
              accelerated.skippedSoundPauses == (reason == witUntilSnd));
    }
    MoviePlaybackState state;
    Check(!state.FShouldRender(false));
    state.idleSeen = true;
    Check(state.FShouldRender(false) && !state.idleSeen);
    Check(state.FShouldRender(true) && !state.idleSeen);
    Check(MoviePlaybackEvaluator::FrameDelay(1000, 1000) == kPlaybackFrameTicks);
    Check(MoviePlaybackEvaluator::FrameDelay(1000, 1001) == kPlaybackFrameTicks - 1);
    Check(MoviePlaybackEvaluator::FrameDelay(1000, 1017) == kPlaybackFrameTicks - 2);
    Check(MoviePlaybackEvaluator::FrameDelay(1000, 1200) == 0);
    Check(MoviePlaybackEvaluator::FrameDelay(0xfffffff0UL, 4) == kPlaybackFrameTicks - 2);
    PlaybackTrace normal;
    RunPlayback(state, normal);
    MoviePlaybackCommandKind expected[] = {
        MoviePlaybackCommandKind::PresentFrame,   MoviePlaybackCommandKind::DisplayFrame,
        MoviePlaybackCommandKind::QueryScrolling, MoviePlaybackCommandKind::ReadTime,
        MoviePlaybackCommandKind::SetAlarm,       MoviePlaybackCommandKind::ReadPosition,
        MoviePlaybackCommandKind::AdvanceFrame,   MoviePlaybackCommandKind::RenderWorld};
    Check(normal.count == 8 && state.renderedFrames == 1);
    for (LegacyLong index = 0; index < 8; ++index)
        Check(normal.commands[index].kind == expected[index]);
    Check(normal.commands[4].value == kPlaybackFrameTicks - 1 && normal.commands[4].flag &&
          normal.commands[6].value == 2);

    state = MoviePlaybackState();
    PlaybackTrace click;
    click.pauseOnPresent = witUntilClick;
    RunPlayback(state, click);
    Check(click.count == 4 && click.commands[3].kind == MoviePlaybackCommandKind::SetClickPause &&
          click.commands[3].flag);
    Check(state.pausing && state.wait == witNil && state.renderedFrames == 0);
    PlaybackTrace resumeClick;
    RunPlayback(state, resumeClick);
    Check(resumeClick.commands[0].kind == MoviePlaybackCommandKind::SetClickPause && !resumeClick.commands[0].flag);
    Check(!state.pausing && state.renderedFrames == 1);

    state = MoviePlaybackState();
    PlaybackTrace scrollFirst;
    scrollFirst.pauseOnPresent = witUntilClick;
    scrollFirst.needsScroll = true;
    RunPlayback(state, scrollFirst);
    Check(scrollFirst.count == 4 && scrollFirst.commands[3].value == kPlaybackScrollTicks && state.scrolling &&
          !state.pausing);
    Check(!scrollFirst.scrollingAtCommand[3]); // Initial scrolling becomes active after alarm success.
    PlaybackTrace scrollAgain;
    scrollAgain.stillScrolling = true;
    RunPlayback(state, scrollAgain);
    Check(scrollAgain.count == 2 && scrollAgain.commands[0].kind == MoviePlaybackCommandKind::ScrollText &&
          scrollAgain.commands[1].value == kPlaybackScrollTicks && scrollAgain.scrollingAtCommand[1]);
    PlaybackTrace scrollDone;
    RunPlayback(state, scrollDone);
    Check(scrollDone.count == 2 && scrollDone.commands[1].kind == MoviePlaybackCommandKind::SetClickPause &&
          scrollDone.commands[1].flag && state.pausing && !state.scrolling);

    state = MoviePlaybackState();
    PlaybackTrace sound;
    sound.pauseOnPresent = witUntilSnd;
    sound.soundPlaying = true;
    RunPlayback(state, sound);
    Check(state.pausing && state.wait == witUntilSnd && sound.count == 5 && sound.commands[4].value == 0);
    PlaybackTrace soundAgain;
    soundAgain.soundPlaying = true;
    RunPlayback(state, soundAgain);
    Check(soundAgain.count == 2 && soundAgain.commands[0].kind == MoviePlaybackCommandKind::QuerySound);
    PlaybackTrace soundDone;
    RunPlayback(state, soundDone);
    Check(soundDone.commands[0].kind == MoviePlaybackCommandKind::QuerySound && !state.pausing &&
          state.wait == witNil && state.renderedFrames == 1);

    state = MoviePlaybackState();
    state.pauseTicks = 42;
    PlaybackTrace timed;
    timed.pauseOnPresent = witForTime;
    timed.fail = MoviePlaybackCommandKind::SetAlarm;
    RunPlayback(state, timed);
    Check(timed.count == 4 && timed.commands[3].value == 42 && !timed.commands[3].flag);
    Check(state.pausing && state.wait == witNil && !state.stopRequested && state.renderedFrames == 0);
    PlaybackTrace timedResume;
    RunPlayback(state, timedResume);
    Check(timedResume.commands[0].kind == MoviePlaybackCommandKind::SetClickPause && !state.pausing &&
          state.renderedFrames == 1);

    state = MoviePlaybackState();
    PlaybackTrace presentationFailure;
    presentationFailure.fail = MoviePlaybackCommandKind::PresentFrame;
    RunPlayback(state, presentationFailure);
    Check(presentationFailure.count == 2 && presentationFailure.commands[1].kind == MoviePlaybackCommandKind::Stop);
    state = MoviePlaybackState();
    PlaybackTrace frameAlarmFailure;
    frameAlarmFailure.fail = MoviePlaybackCommandKind::SetAlarm;
    RunPlayback(state, frameAlarmFailure);
    Check(frameAlarmFailure.count == 6 && frameAlarmFailure.commands[5].kind == MoviePlaybackCommandKind::Stop);
    state = MoviePlaybackState();
    state.scrolling = true;
    PlaybackTrace scrollAlarmFailure;
    scrollAlarmFailure.stillScrolling = true;
    scrollAlarmFailure.fail = MoviePlaybackCommandKind::SetAlarm;
    RunPlayback(state, scrollAlarmFailure);
    Check(scrollAlarmFailure.count == 3 && scrollAlarmFailure.commands[2].kind == MoviePlaybackCommandKind::Stop &&
          !state.scrolling);
    state = MoviePlaybackState();
    PlaybackTrace startScrollFailure;
    startScrollFailure.needsScroll = true;
    startScrollFailure.fail = MoviePlaybackCommandKind::SetAlarm;
    RunPlayback(state, startScrollFailure);
    Check(startScrollFailure.count == 5 && startScrollFailure.commands[4].kind == MoviePlaybackCommandKind::Stop &&
          !state.scrolling);
    state = MoviePlaybackState();
    state.pausing = true;
    state.wait = witUntilSnd;
    PlaybackTrace soundAlarmFailure;
    soundAlarmFailure.soundPlaying = true;
    soundAlarmFailure.fail = MoviePlaybackCommandKind::SetAlarm;
    RunPlayback(state, soundAlarmFailure);
    Check(soundAlarmFailure.count == 3 && soundAlarmFailure.commands[2].kind == MoviePlaybackCommandKind::Stop);

    state = MoviePlaybackState();
    PlaybackTrace nextScene;
    nextScene.position.frame = 3;
    RunPlayback(state, nextScene);
    Check(nextScene.count == 9 && nextScene.commands[6].kind == MoviePlaybackCommandKind::SwitchScene &&
          nextScene.commands[6].value == 1 && nextScene.commands[7].kind == MoviePlaybackCommandKind::ReplayScene &&
          nextScene.commands[8].kind == MoviePlaybackCommandKind::RenderWorld && state.transition == transDissolve);
    MoviePlaybackCommandKind deferredFailures[] = {MoviePlaybackCommandKind::SwitchScene,
                                                   MoviePlaybackCommandKind::ReplayScene,
                                                   MoviePlaybackCommandKind::AdvanceFrame};
    for (auto failure : deferredFailures)
    {
        state = MoviePlaybackState();
        PlaybackTrace failed;
        failed.position.frame = failure == MoviePlaybackCommandKind::AdvanceFrame ? 1 : 3;
        failed.fail = failure;
        RunPlayback(state, failed);
        Check(state.stopRequested && state.renderedFrames == 0 && failed.commands[failed.count - 1].kind == failure);
        PlaybackTrace stop;
        RunPlayback(state, stop);
        Check(stop.count == 1 && stop.commands[0].kind == MoviePlaybackCommandKind::Stop);
    }
    state = MoviePlaybackState();
    PlaybackTrace lastScene;
    lastScene.position.scene = 1;
    lastScene.position.frame = 3;
    RunPlayback(state, lastScene);
    Check(lastScene.count == 6 && state.stopRequested && state.renderedFrames == 0);

    state = MoviePlaybackState();
    state.fadeOriginalVolume = 120;
    PlaybackTrace fading;
    RunPlayback(state, fading);
    Check(fading.count == 3 && fading.commands[0].kind == MoviePlaybackCommandKind::ReadVolume &&
          fading.commands[1].kind == MoviePlaybackCommandKind::SetVolume && fading.commands[1].value == 90 &&
          fading.commands[2].value == kPlaybackFadeTicks);
    PlaybackTrace fadeDone;
    fadeDone.volume = 5;
    RunPlayback(state, fadeDone);
    Check(fadeDone.count == 3 && fadeDone.commands[1].value == 0 && fadeDone.stopAtCommand[1] &&
          fadeDone.commands[2].kind == MoviePlaybackCommandKind::Stop);
    state = MoviePlaybackState();
    state.fadeOriginalVolume = 120;
    PlaybackTrace fadeAlarmFailure;
    fadeAlarmFailure.fail = MoviePlaybackCommandKind::SetAlarm;
    RunPlayback(state, fadeAlarmFailure);
    Check(fadeAlarmFailure.count == 4 && fadeAlarmFailure.commands[3].kind == MoviePlaybackCommandKind::Stop);

    // Suppressing device fades must retain the fade's stop timing even though
    // each host read continues to report the unchanged output volume.
    state = MoviePlaybackState();
    state.fadeOriginalVolume = 120;
    state.suppressAudioFades = true;
    for (LegacyLong tick = 0; tick < kPlaybackFadeSeconds * 4; ++tick)
    {
        PlaybackTrace heldVolume;
        heldVolume.volume = 120;
        RunPlayback(state, heldVolume);
        Check(heldVolume.count == 2 && heldVolume.commands[0].kind == MoviePlaybackCommandKind::ReadVolume);
        Check(heldVolume.commands[1].kind == (tick == kPlaybackFadeSeconds * 4 - 1
                                                  ? MoviePlaybackCommandKind::Stop
                                                  : MoviePlaybackCommandKind::SetAlarm));
        Check(state.fadeVolume == 120 - (tick + 1) * 10 && !state.fadeChangedVolume);
    }
    Check(state.stopRequested);
}

struct PlayTrace
{
    MoviePlayCommand commands[32];
    bool soundAtCommand[32] = {};
    bool stopAtCommand[32] = {};
    LegacyLong count = 0;
    MoviePlayCommandKind fail = MoviePlayCommandKind::Finished;
    bool scene = true;
    bool atEnd = false;
    bool opening = false;
    bool muteBackground = false;
    LegacyLong played = 0;
    LegacyLong flushed = 0;
    LegacyLong stopped = 0;
};

static void RunPlay(MoviePlaybackState &state, PlayTrace &trace)
{
    MoviePlayEvaluator evaluator(state);
    for (;;)
    {
        MoviePlayCommand command = evaluator.Next();
        if (command.kind == MoviePlayCommandKind::Finished)
            return;
        Check(trace.count < 32);
        trace.soundAtCommand[trace.count] = state.soundsEnabled;
        trace.stopAtCommand[trace.count] = state.stopRequested;
        trace.commands[trace.count++] = command;
        MoviePlaybackReply reply;
        reply.success = command.kind != trace.fail;
        switch (command.kind)
        {
        case MoviePlayCommandKind::CheckScene:
            reply.value = trace.scene;
            break;
        case MoviePlayCommandKind::QueryEnd:
            reply.value = trace.atEnd;
            break;
        case MoviePlayCommandKind::QueryOpening:
            reply.value = trace.opening;
            break;
        case MoviePlayCommandKind::ReadTime:
            reply.timestamp = 1234;
            break;
        case MoviePlayCommandKind::StartClock:
            state.playing = true; // Native adapter publishes playing before its initial alarm.
            break;
        case MoviePlayCommandKind::StartupFailed:
            ++trace.stopped;
            if (command.flag)
                state.stopRequested = true;
            else
                state.playing = false;
            ++trace.played;
            state.soundsEnabled = state.oldSoundsEnabled;
            break;
        case MoviePlayCommandKind::RenderStop:
            // Model the host's state changes; this is not a window-cleanup test.
            state.playing = state.pausing = state.stopRequested = false;
            state.soundsEnabled = state.oldSoundsEnabled;
            break;
        case MoviePlayCommandKind::NotifyStopped:
            ++trace.stopped;
            break;
        case MoviePlayCommandKind::BackgroundSound:
            if (trace.muteBackground)
                state.soundsEnabled = false;
            break;
        case MoviePlayCommandKind::DispatchSounds:
            if (command.flag)
                ++trace.played;
            else
                ++trace.flushed;
            break;
        default:
            break;
        }
        evaluator.Complete(reply);
    }
}

static void TestMoviePlayStartup()
{
    MoviePlaybackState state;
    state.renderedFrames = 9;
    state.stopRequested = true;
    PlayTrace normal;
    RunPlay(state, normal);
    MoviePlayCommandKind expected[] = {MoviePlayCommandKind::CheckScene,
                                       MoviePlayCommandKind::ResetSounds,
                                       MoviePlayCommandKind::QueryEnd,
                                       MoviePlayCommandKind::ReadTime,
                                       MoviePlayCommandKind::StartClock,
                                       MoviePlayCommandKind::PrepareScene,
                                       MoviePlayCommandKind::DisableAccelerators,
                                       MoviePlayCommandKind::QueryOpening,
                                       MoviePlayCommandKind::BackgroundSound,
                                       MoviePlayCommandKind::DispatchSounds};
    Check(normal.count == 10);
    for (LegacyLong index = 0; index < 10; ++index)
        Check(normal.commands[index].kind == expected[index]);
    Check(normal.soundAtCommand[3] && normal.stopAtCommand[3] && !normal.stopAtCommand[4]);
    Check(state.playing && !state.stopRequested && state.soundsEnabled && !state.oldSoundsEnabled &&
          state.renderedFrames == 0 && state.startTimestamp == 1234 && normal.played == 1);
    state = MoviePlaybackState();
    PlayTrace rewind;
    rewind.atEnd = rewind.opening = true;
    RunPlay(state, rewind);
    Check(rewind.count == 12 && rewind.commands[3].kind == MoviePlayCommandKind::Rewind &&
          rewind.commands[9].kind == MoviePlayCommandKind::RefreshOpening &&
          rewind.commands[10].kind == MoviePlayCommandKind::BackgroundSound && rewind.commands[11].flag);
    state = MoviePlaybackState();
    state.renderedFrames = 9;
    PlayTrace absent;
    absent.scene = false;
    RunPlay(state, absent);
    Check(absent.count == 2 && absent.commands[1].kind == MoviePlayCommandKind::NotifyStopped && absent.stopped == 1);
    Check(!state.playing && !state.soundsEnabled && state.renderedFrames == 9 && absent.played == 0);
    state = MoviePlaybackState();
    PlayTrace rewindFailure;
    rewindFailure.atEnd = true;
    rewindFailure.fail = MoviePlayCommandKind::Rewind;
    RunPlay(state, rewindFailure);
    Check(rewindFailure.count == 5 && rewindFailure.commands[4].kind == MoviePlayCommandKind::NotifyStopped &&
          !state.playing);

    state = MoviePlaybackState();
    PlayTrace clockFailure;
    clockFailure.fail = MoviePlayCommandKind::StartClock;
    RunPlay(state, clockFailure);
    Check(clockFailure.count == 6 && clockFailure.commands[5].kind == MoviePlayCommandKind::StartupFailed &&
          !clockFailure.commands[5].flag && clockFailure.stopped == 1 && clockFailure.played == 1);
    Check(!state.playing && !state.stopRequested && !state.soundsEnabled);
    state = MoviePlaybackState();
    PlayTrace prepareFailure;
    prepareFailure.fail = MoviePlayCommandKind::PrepareScene;
    RunPlay(state, prepareFailure);
    Check(prepareFailure.count == 7 && prepareFailure.commands[6].kind == MoviePlayCommandKind::StartupFailed &&
          prepareFailure.commands[6].flag && prepareFailure.stopped == 1 && prepareFailure.played == 1);
    Check(state.playing && state.stopRequested && !state.soundsEnabled);
    PlayTrace alreadyStopping;
    RunPlay(state, alreadyStopping);
    Check(alreadyStopping.count == 0);

    state = MoviePlaybackState();
    state.playing = true;
    state.pausing = true;
    state.soundsEnabled = true;
    state.oldSoundsEnabled = false;
    PlayTrace pausedToggle;
    RunPlay(state, pausedToggle);
    Check(pausedToggle.count == 2 && pausedToggle.commands[0].kind == MoviePlayCommandKind::RenderStop &&
          pausedToggle.commands[1].kind == MoviePlayCommandKind::DispatchSounds && !pausedToggle.commands[1].flag);
    Check(pausedToggle.flushed == 1 && !state.playing && !state.soundsEnabled);
    state = MoviePlaybackState();
    state.playing = true;
    PlayTrace toggle;
    RunPlay(state, toggle);
    Check(toggle.count == 1 && toggle.commands[0].kind == MoviePlayCommandKind::DispatchSounds && toggle.flushed == 1);
    Check(state.playing && state.stopRequested);
    state = MoviePlaybackState();
    state.soundsEnabled = true;
    PlayTrace changedByHost;
    changedByHost.muteBackground = true;
    RunPlay(state, changedByHost);
    Check(state.oldSoundsEnabled && changedByHost.flushed == 1 &&
          !changedByHost.commands[changedByHost.count - 1].flag);
}

static void TestTextBoxPersistence()
{
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    RichTextData content;
    content.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
    Check(content.properties != pvNil);
    ChunkIdentification richText;
    Check(content.FWrite(file, &richText));
    ChunkNumber number;
    Check(file->FAdd(0, kctgTbox, &number));
    TextBoxData defaults;
    Check(defaults.FWrite(file, number));
    TextBoxData defaultRead;
    Check(defaultRead.FRead(file, number));
    RC zeroBounds(0, 0, 0, 0);
    Check(defaultRead.bounds == zeroBounds);
    TextBoxData caption;
    caption.firstFrame = -2;
    caption.maxFrame = 18;
    caption.story = true;
    caption.textChild = 7;
    caption.bounds.Set(10, 20, 150, 75);
    Check(file->FAdoptChild(kctgTbox, number, richText.ctg, richText.cno, caption.textChild));
    Check(caption.FWrite(file, number));
    TextBoxData loaded;
    ChunkIdentification child;
    Check(loaded.FRead(file, number, &child));
    Check(child.cno == richText.cno && loaded.story && loaded.firstFrame == -2 && loaded.maxFrame == 18);
    Check(loaded.bounds == caption.bounds);
    Check(!loaded.FVisibleAt(-3) && loaded.FVisibleAt(-2) && !loaded.FVisibleAt(18));
    DataBlock block;
    TextBoxOnFile header;
    Check(file->FFind(kctgTbox, number, &block) && block.FReadRgb(&header, sizeof(header), 0));
    SwapBytesRgsw(&header, 2);
    SwapBytesRglw(reinterpret_cast<byte *>(&header) + 4, 7);
    Check(block.FWriteRgb(&header, sizeof(header), 0));
    Check(loaded.FRead(file, number, &child) && loaded.textChild == 7 && loaded.story);
    Check(loaded.bounds == caption.bounds && loaded.maxFrame == 18);
    loaded.firstFrame = INT32_MIN;
    Check(!loaded.FWrite(file, number));
    ReleasePpo(&file);
}

static void TestMaterialHeaders()
{
    // Third-party MTRLPACK1-2 MTRL 0x50004: the Windows marker is zero,
    // while the other fields describe an ordinary solid palette material.
    const byte legacy[] = {0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 21, 7, 0, 0, 50, 0};
    const byte little[] = {1, 0, 3, 3, 0x33, 0x22, 0x11, 0, 0, 0x20, 0, 0xa0, 0, 0x10, 21, 7, 0, 0, 50, 0};
    const byte big[] = {0, 1, 3, 3, 0, 0x11, 0x22, 0x33, 0x20, 0, 0xa0, 0, 0x10, 0, 21, 7, 0, 50, 0, 0};
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    PChunkyResourceFile resource = ChunkyResourceFile::PcrfNew(file, 0);
    Check(resource != pvNil);
    for (int index = 0; index < 7; ++index)
    {
        byte bytes[sizeof(MaterialOnFile)];
        std::memcpy(bytes, index == 0 ? legacy : index == 2 ? big : little, sizeof(bytes));
        if (index == 3)
            bytes[0] = 0x34; // Unknown byte-order marker.
        if (index == 4)
        {
            bytes[0] = 0; // Zero marker without the known Windows OS kind.
            bytes[2] = bytes[3] = 2;
        }
        if (index == 5)
        {
            bytes[2] = bytes[3] = 0;
            bytes[0] = 0;
        }
        ChunkNumber number;
        Check(file->FAddPv(bytes, sizeof(bytes) - (index == 6 ? 1 : 0), kctgMtrl, &number));
        PMaterial_MTRL material = (PMaterial_MTRL)resource->PbacoFetch(kctgMtrl, number, Material_MTRL::FReadMtrl);
        if (index < 3)
        {
            Check(material != pvNil);
            PBMTL rendered = material->Pbmtl();
            Check(rendered->index_base == 21 && rendered->index_range == 7 && rendered->power == BR_SCALAR(50));
            Check(rendered->colour == (index == 0 ? 0 : 0x00112233));
            Check(rendered->ka == (index == 0 ? 0 : (br_ufraction)0x2000));
            Check(rendered->kd == (index == 0 ? (br_ufraction)0xffff : (br_ufraction)0xa000));
            Check(rendered->ks == (index == 0 ? 0 : (br_ufraction)0x1000));
            ChunkNumber written;
            Check(material->FWrite(file, kctgMtrl, &written));
            MaterialOnFile header;
            DataBlock block;
            Check(file->FFind(kctgMtrl, written, &block) && block.FReadRgb(&header, sizeof(header), 0));
            Check(header.bo == kboCur); // Saving normalizes the legacy marker.
        }
        else
            Check(material == pvNil);
        ReleasePpo(&material);
    }
    ReleasePpo(&resource);
    ReleasePpo(&file);
}

static void TestSourceNamesBaseline()
{
    FILE *snapshot = nullptr;
    if (const char *path = std::getenv("THREE_MM_SOURCE_NAMES_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        Check(snapshot);
    }
    Filename root;
    String path(PszLit("."));
    Check(root.FBuildFromPath(&path));
    auto manager = TagManager::PtagmNew(&root, MissingSource, 0);
    Check(manager);
    const char *names[] = {"Long Alpha/ALPHA", "Solo", "Other/ALPHA", "ignored", "/Tail", "Head/", "A/B/C"};
    const LegacyLong ids[] = {1, 2, 3, 1, 4, 5, 6};
    for (int i = 0; i < 7; ++i)
    {
        String name;
        name.SetSzs(const_cast<char *>(names[i]));
        Check(manager->FAddStnSource(&name, ids[i]));
    }
    for (auto query : {"long alpha", "ALPHA", "Solo", "Other", "Tail", "Head", "B/C", "missing", ""})
    {
        String name;
        name.SetSzs(const_cast<char *>(query));
        LegacyLong sid = 99;
        const bool found = manager->FGetSid(&name, &sid);
        uint32_t row[] = {found, static_cast<uint32_t>(sid)};
        if (snapshot)
            Check(std::fwrite(row, sizeof(row), 1, snapshot) == 1);
    }
    DataBlock block;
    Check(block.FSetTemp(manager->CbSourceNames()) && manager->FWriteSourceNames(&block));
    byte bytes[2048];
    auto count = block.Cb();
    Check(count < sizeof(bytes) && block.FReadRgb(bytes, count, 0));
    if (snapshot)
        Check(std::fwrite(bytes, 1, count, snapshot) == static_cast<size_t>(count));
    ReleasePpo(&manager);
    if (snapshot)
        Check(std::fclose(snapshot) == 0);
}

static void TestTagOwnershipBaseline()
{
    FILE *snapshot=nullptr;
    if(const char *path=std::getenv("THREE_MM_TAG_SNAPSHOT")){snapshot=std::fopen(path,"wb");Check(snapshot);}
    auto a=ChunkyFile::PcflCreateTemp(),b=ChunkyFile::PcflCreateTemp();Check(a&&b);
    Check(a->FPutPv(nullptr,0,'TAGR',7)&&a->FPutPv(nullptr,0,'TAGC',8)&&a->FAdoptChild('TAGR',7,'TAGC',8,9));
    auto ca=ChunkyResourceFile::PcrfNew(a,128),cb=ChunkyResourceFile::PcrfNew(b,128);Check(ca&&cb);
    auto observe=[&](int event,const TAG &tag){
        uint32_t row[]={static_cast<uint32_t>(event),static_cast<uint32_t>(tag.sid),tag.ctg,tag.cno,
            tag.sid?0U:tag.pcrf==ca?1U:tag.pcrf==cb?2U:0U,static_cast<uint32_t>(ca->CactRef()),static_cast<uint32_t>(cb->CactRef())};
        if(snapshot)Check(std::fwrite(row,sizeof(row),1,snapshot)==1);
    };
    TAG tag={};tag.ctg='TAGR';tag.cno=7;
    Check(TagManager::FOpenTag(&tag,ca));observe(1,tag);
    auto duplicate=tag;TagManager::DupTag(&duplicate);observe(2,duplicate);TagManager::CloseTag(&duplicate);observe(3,duplicate);
    Check(TagManager::FSaveTag(&tag,cb,false));observe(4,tag);
    Check(TagManager::FSaveTag(&tag,cb,true));observe(5,tag);
    Check(TagManager::FSaveTag(&tag,cb,true));observe(6,tag);TagManager::CloseTag(&tag);observe(7,tag);
    tag={};tag.ctg='TAGR';tag.cno=7;Check(TagManager::FOpenTag(&tag,cb,a));observe(8,tag);TagManager::CloseTag(&tag);
    tag.sid=42;tag.pcrf=ca;Check(TagManager::FOpenTag(&tag,cb));Check(TagManager::FSaveTag(&tag,cb,true));
    TagManager::DupTag(&tag);TagManager::CloseTag(&tag);observe(9,tag);
    auto list=TagList::PtaglNew();Check(list);
    TAG tags[4]={{2,ca,9,3},{1,ca,8,4},{2,cb,9,3},{0,ca,7,1}};
    for(auto &value:tags)Check(list->FInsertTag(&value,false));
    Check(list->FInsertTag(tags+2,true)&&list->FInsertChild(tags,2,9)&&list->FInsertChild(tags,1,8)&&list->FInsertChild(tags,2,9));
    Check(list->Ctag()==3);
    for(int i=0;i<list->Ctag();++i){TAG value;list->GetTag(i,&value);observe(10+i,value);}
    Check(TagManager::FcmpCompareTags(tags,tags+2)==fcmpEq);ReleasePpo(&list);
    Check(ca->CactRef()==1&&cb->CactRef()==1);ReleasePpo(&ca);ReleasePpo(&cb);ReleasePpo(&a);ReleasePpo(&b);
    if(snapshot)Check(std::fclose(snapshot)==0);
}

static void TestEngineServices()
{
    if(const char *path=std::getenv("THREE_MM_SOURCE_SNAPSHOT")){sourceSnapshot=std::fopen(path,"wb");Check(sourceSnapshot);}

    // Clear caches without an Application or native window existing.
    Filename root;
    String path;
    path.SetSzs(PszLit("."));
    Check(root.FBuildFromPath(&path));
    PTagManager manager = TagManager::PtagmNew(&root, MissingSource, 0);
    Check(manager != pvNil);
    String missingName;
    missingName.SetSzs(PszLit("3dmm-test-missing-source-5316"));
    Check(manager->FAddStnSource(&missingName, 42));
    TAG missingTag = {};
    missingTag.sid = 42;
    missingTag.ctg = kctgTmpl;
    missingTag.cno = 1;
    missingSourcePrompts = 0;
    // Portfolio/document probes can miss without asserting or requesting
    // media. An explicitly permitted media lookup must honor cancellation.
    Check(manager->PbacoFetch(&missingTag, Template::FReadTmpl) == pvNil);
    Check(manager->PbacoFetch(&missingTag, Template::FReadTmpl) == pvNil);
    Check(missingSourcePrompts == 0);
    Check(manager->PbacoFetch(&missingTag, Template::FReadTmpl, fTrue) == pvNil);
    Check(missingSourcePrompts == 1);
    Filename retryDirectory, retryFile;
    retrySourceDirectory = &retryDirectory;
    retrySourceFile = &retryFile;
    missingSourcePrompts = 0;
    // Installing a local source while the prompt is open must invalidate the
    // cached HD miss. The empty pack has no template, so fetch still misses;
    // a second CD prompt would mean Retry never discovered the new folder.
    Check(manager->PbacoFetch(&missingTag, Template::FReadTmpl, fTrue) == pvNil);
    Check(missingSourcePrompts == 1);
    manager->ClearCache();
    // Host policy is checked before cached resource managers and path/CD
    // lookup. Movie source-table merges cannot bypass this resolver.
    manager->SetSourceResolver([](PString, PString, PFilename) { return TagSourceAccess::Disabled; });
    disabledSourcePrompt = true;
    Check(manager->SourceDirectory(42) == TagSourceAccess::Disabled);
    auto merged = StringTable_GST::PgstNew(sizeof(LegacyLong));
    LegacyLong sourceId = 42;
    Check(merged && merged->FAddStn(&missingName, &sourceId));
    Check(manager->FMergeGstSource(merged, kboCur, koskCur));
    ReleasePpo(&merged);
    missingSourcePrompts = 0;
    Check(manager->PbacoFetch(&missingTag, Template::FReadTmpl) == pvNil && missingSourcePrompts == 0);
    Check(manager->PbacoFetch(&missingTag, Template::FReadTmpl, fTrue) == pvNil && missingSourcePrompts == 1);
    String leaf(PszLit("retry.3cn"));
    Filename denied;
    Check(!manager->FFindFile(42, &leaf, &denied, false));
    disabledSourcePrompt = false;
    manager->SetSourceResolver([](PString, PString, PFilename path) {
        *path = *retrySourceDirectory;
        return TagSourceAccess::Available;
    });
    Check(manager->FFindFile(42, &leaf, &denied, false));
    ReleasePpo(&manager);
    String directoryPath;
    retryDirectory.GetStnPath(&directoryPath);
    Check(retryFile.FDelete() && KauaiFiles::DeleteDirectory(directoryPath.Psz()));
    retrySourceDirectory = retrySourceFile = nullptr;
    if(sourceSnapshot){Check(std::fclose(sourceSnapshot)==0);sourceSnapshot=nullptr;}

    BrBegin();
    BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
    // Build a tiny real shade-table chunk; solid material validation expects
    // the same asset service to be initialized as in the application.
    byte *pixels = pvNil;
    Check(FAllocPv(reinterpret_cast<void **>(&pixels), 256, fmemClear, mprNormal));
    PTextureMap shade = TextureMap::PtmapNew(pixels, 256, 1);
    PChunkyFile fixture = ChunkyFile::PcflCreateTemp();
    Check(shade != pvNil && fixture != pvNil);
    ChunkNumber shadeNumber;
    Check(shade->FWrite(fixture, kctgTmap, &shadeNumber));
    Check(Material_MTRL::FSetShadeTable(fixture, kctgTmap, shadeNumber));
    ReleasePpo(&shade);
    ReleasePpo(&fixture);
    TestMaterialHeaders();
    RenderHost first = {BrActorAllocate(BR_ACTOR_NONE, 0), 0, 0};
    RenderHost second = {BrActorAllocate(BR_ACTOR_NONE, 0), 0, 0};
    Check(first.root != 0 && second.root != 0);
    PDynamicArray parents = DynamicArray::PglNew(sizeof(short));
    PDynamicArray sets = DynamicArray::PglNew(sizeof(short));
    Check(parents != pvNil && sets != pvNil);
    short parent = ivNil, set = 0;
    Check(parents->FAdd(&parent) && sets->FAdd(&set));
    PBody body = Body::PbodyNew(parents, sets);
    Check(body != pvNil);
    PMaterial_MTRL material = Material_MTRL::PmtrlNew();
    Check(material != pvNil);
    body->SetPartSetMtrl(0, material);
    ReleasePpo(&material);
    body->SetRenderCallbacks(&first, Attach, Changed);
    body->Show();
    Check(first.attachments == 1 && first.changes == 1);
    BMAT34 matrix;
    BrMatrix34Identity(&matrix);
    body->SetPartMatrix(0, &matrix);
    Check(first.changes == 2);
    body->Hide();
    Check(first.changes == 3 && body->PbactRoot()->parent == 0);
    body->SetPartMatrix(0, &matrix);
    Check(first.changes == 3); // Hidden edits do not invalidate a renderer.
    PBody copy = body->PbodyDup();
    Check(copy != pvNil);
    copy->Show();
    Check(first.attachments == 2 && first.changes == 4);
    copy->Hide();
    ReleasePpo(&copy);
    Check(first.changes == 5);
    body->SetRenderCallbacks(&second, Attach, Changed);
    body->Show();
    body->SetPartMatrix(0, &matrix);
    body->Hide();
    Check(second.attachments == 1 && second.changes == 3 && first.changes == 5);
    body->Show();
    PBody snapshot = body->PbodyDup(); // Original is hidden and reattached.
    Check(snapshot != pvNil && body->PbactRoot()->parent == second.root);
    Check(snapshot->PbactRoot()->parent == 0);
    body->Restore(snapshot);
    ReleasePpo(&snapshot); // Releases the displaced, detached tree.
    Check(body->PbactRoot()->parent == second.root);
    Check(body->FChangeShape(parents, sets));
    Check(body->PbactRoot()->parent == second.root);
    material = Material_MTRL::PmtrlNew();
    Check(material != pvNil);
    body->SetPartSetMtrl(0, material);
    ReleasePpo(&material);
    body->Hide();
    // An unbound body can also change visibility without any GUI stubs.
    body->SetRenderCallbacks(0, 0, 0);
    body->Show();
    body->Hide();
    body->SetRenderCallbacks(&second, Attach, Changed);
    body->Show();
    ReleasePpo(&body); // Visible destruction must detach its root from host.
    Check(second.root->children == 0);
    ReleasePpo(&parents);
    ReleasePpo(&sets);
    BrActorFree(first.root);
    BrActorFree(second.root);
    BrZbEnd();
    BrEnd();
}
#endif

int main()
{
    TestPortableClockAndMemory();
    TestFailedResize();
    TestCollectionStorage();
    TestCollectionWireAndMutations();
    TestDataStreams();
    TestBlockCopies();
    TestBlockMaterialization();
    TestFilePolicies();
    TestChunkPersistence();
    TestStringWire();
    TestForestTransfer();
    TestFreeMapLoading();
    TestStorageRouting();
    TestContainerLifecycle();
    TestContainerConstruction();
    TestContainerRegistry();
    TestChunkTableBaseline();
    TestPieceEdits();
    TestRichTextPersistence();
    TestReadOnlyEmptyChunkSave();
    vpcex = CommandExecutionManager::PcexNew(20, 20);
    Check(vpcex != pvNil);
    TestCommandReplay();
    PClock clock = NewObj Clock(1000);
    Check(clock != pvNil);
    clock->Start(0);
    PCommandHandler handler = NewObj CommandHandler(1001);
    Check(handler != pvNil);
    Check(vpcex->FAddCmh(handler, 0));
    vpcex->EnqueueCid(cidIdle, handler);
    Check(clock->FSetAlarm(0, handler));
    ReleasePpo(&handler); // Must remove queue, registration and alarm targets.
    Check(!vpcex->FDispatchNextCmd());
    vpcex->EnqueueCid(cidIdle);
    while (vpcex->FDispatchNextCmd())
    {
    }
    ReleasePpo(&clock); // Running clocks must also be removed automatically.
    vpcex->EnqueueCid(cidIdle);
    Check(vpcex->FDispatchNextCmd());
    Check(!vpcex->FDispatchNextCmd());
    ReleasePpo(&vpcex);
#ifdef TEST_ENGINE_CORE
    TestSourceNamesBaseline();
    TestTagOwnershipBaseline();
    TestEngineServices();
    TestMoviePersistence();
    TestRollCallStorageBaseline();
    TestMovieRootBaseline();
    TestMovieWireFormats();
    TestScenePersistence();
    TestActorPersistence();
    TestActorTrackOwnership();
    TestActorRouteEvaluation();
    TestActorValueEvaluation();
    TestActorMathBaseline();
    TestActorEntranceOrientation();
    TestActorSoundStorage();
    TestActorPoseEvaluation();
    TestActorFrameSequencing();
    TestActorTimeline();
    TestActorSeekControl();
    TestSoundQueueOwnership();
    TestMoviePlaybackControl();
    TestMoviePlayStartup();
    TestTextBoxPersistence();
    if (actorFrameSnapshot) Check(std::fclose(actorFrameSnapshot) == 0);
    if (actorSeekSnapshot) Check(std::fclose(actorSeekSnapshot) == 0);
#endif
    return 0;
}
