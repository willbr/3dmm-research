#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "soc.h"
#include "scene_sound.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
class TestApp : public ApplicationBase
{
  public:
#ifdef DEBUG
    void WarnProcApp(PSZS f, long n, PSZS s) override
    {
        std::fprintf(stderr, "%s:%ld: %s\n", f, n, s);
    }
    bool FAssertProcApp(PSZS f, long n, PSZS s, void *, long) override
    {
        WarnProcApp(f, n, s);
        std::exit(1);
    }
#endif
};
class Client : public MovieClientCallbacks
{
  public:
    Client() : MovieClientCallbacks(32, 24, 0)
    {
    }
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "scene store: %d\n", __LINE__);                                                       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static unsigned hash = 2166136261U, recorded;
static PChunkyResourceFile observed;
static void Record(const void *p, size_t n)
{
    auto b = static_cast<const byte *>(p);
    for (size_t i = 0; i < n; ++i)
        hash = (hash ^ b[i]) * 16777619U;
    recorded += unsigned(n);
    if (snapshot)
        CHECK(std::fwrite(p, 1, n, snapshot) == n);
}
static void Value(LegacyLong n)
{
    Record(&n, 4);
}

static void Reverse(void *p, long bytes)
{
    auto b = static_cast<byte *>(p);
    for (long i = 0; i < bytes; i += 4)
    {
        auto x = b[i];
        b[i] = b[i + 3];
        b[i + 3] = x;
        x = b[i + 1];
        b[i + 1] = b[i + 2];
        b[i + 2] = x;
    }
}

struct Tags : TagList
{
    static void State(PTagList p)
    {
        auto &list = p->*&Tags::_list;
        Value(list.count);
        MMGroup group = {&list.storage, sizeof(MMTagCached), list.count, list.bytes, -1, 0};
        for (MMU32 i = 0; i < list.count; ++i)
        {
            MMTagCached value;
            CHECK(mm_tag_list_get(&list, i, &value) == MM_STORAGE_OK);
            Value(value.tag.sid);
            Value(value.tag.cache != nullptr);
            Value(value.tag.type);
            Value(value.tag.number);
            Value(value.children);
            MMGroupLocation loc;
            CHECK(mm_group_location(&group, i, &loc) == MM_STORAGE_OK);
            auto count = (loc.bytes - sizeof(value)) / sizeof(MMTagChild);
            Value(count);
            for (size_t j = 0; j < count; ++j)
            {
                MMTagChild child;
                std::memcpy(&child, list.storage.buffers[0].data + loc.offset + sizeof(value) + j * sizeof(child),
                            sizeof(child));
                Value(child.slot);
                Value(child.type);
            }
        }
    }
};
static void Written(PChunkyFile file, ChunkNumber number)
{
    SceneData data;
    CHECK(data.FRead(file, number));
    Value(data.firstFrame);
    Value(data.lastFrame);
    Value(data.transition);
    Value(data.name.count);
    Value(file->Ckid(kctgScen, number));
    for (int start = 0; start < 2; ++start)
    {
        ChildChunkIdentification kid;
        DataBlock block;
        CHECK(file->FGetKidChidCtg(kctgScen, number, start, start ? kctgStartGg : kctgFrmGg, &kid) &&
              file->FFind(kid.cki.ctg, kid.cki.cno, &block));
        auto g = GeneralGroup::PggRead(&block);
        CHECK(g);
        Value(g->IvMac());
        for (long i = 0; i < g->IvMac(); ++i)
        {
            SceneEvent e;
            g->GetFixed(i, &e);
            Value(e.nfrm);
            Value(e.sevt);
            Value(g->Cb(i));
            Record(g->QvGet(i), g->Cb(i));
        }
        ReleasePpo(&g);
    }
}
static ChunkNumber Fixture(PChunkyFile file, int scenario, bool swap)
{
    ChunkNumber number;
    CHECK(file->FAdd(0, kctgScen, &number));
    SceneData d;
    d.firstFrame = 3;
    d.lastFrame = 19;
    d.transition = transDissolve;
    String name;
    name.SetSzs(PszLit("Load fixture"));
    d.SetName(&name);
    CHECK(d.FWrite(file, number));
    auto frames = GeneralGroup::PggNew(sizeof(SceneEvent)), starts = GeneralGroup::PggNew(sizeof(SceneEvent));
    CHECK(frames && starts);
    auto add = [&](PGeneralGroup g, SceneEventType kind, void *p, long bytes) {
        SceneEvent e = {3, kind};
        std::vector<byte> v(bytes);
        std::memcpy(v.data(), p, bytes);
        if (swap)
        {
            Reverse(&e, sizeof(e));
            Reverse(v.data(), bytes);
        }
        CHECK(g->FAdd(bytes, nullptr, v.data(), &e));
    };
    int32_t sound[] = {
        73, 2, 1, 2, 7, 0, 0, static_cast<int32_t>(kctgMsnd), 123, 8, 1, 0, static_cast<int32_t>(kctgMsnd), 321};
    add(frames, sevtPlaySnd, sound, scenario == 5 ? 16 : sizeof(sound));
    add(frames, sevtPlaySnd, sound, scenario == 5 ? 16 : sizeof(sound));
    int32_t camera = 0x12345678;
    add(frames, sevtChngCamera, &camera, 4);
    int32_t pause[] = {witForTime, 230};
    add(frames, sevtPause, pause, 8);
    int32_t background[] = {1, 0, static_cast<int32_t>(kctgBkgd), 15};
    add(starts, sevtSetBkgd, background, 16);
    if (scenario == 3 || scenario == 4)
    {
        int32_t child = 17;
        add(starts, scenario == 3 ? sevtAddActr : sevtAddTbox, &child, 4);
        if (scenario == 3)
        {
            ChunkNumber actor;
            CHECK(file->FAddChild(kctgScen, number, 17, 0, kctgActr, &actor));
            ActorChunkOnFile header = {};
            header.bo = kboCur;
            header.osk = koskCur;
            header.nfrmFirst = 3;
            header.nfrmLast = 19;
            header.tagTmpl = {2, 0, kctgTmpl, 77};
            CHECK(ActorData::FWriteHeader(file, actor, &header));
            auto events = GeneralGroup::PggNew(sizeof(ActorEvent::Base));
            CHECK(events);
            ChunkNumber ae;
            DataBlock block;
            CHECK(file->FAddChild(kctgActr, actor, 0, events->CbOnFile(), kctgGgae, &ae, &block) &&
                  events->FWrite(&block));
            ReleasePpo(&events);
        }
    }
    if (scenario == 6)
    {
        RichTextData text;
        text.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
        text.fontName.SetSzs(PszLit("Arial"));
        text.text.pfil = FileObject::PfilCreateTemp();
        text.text.cb = 2;
        byte chars[] = {'H', 'i'};
        CHECK(text.properties && text.text.pfil && text.text.pfil->FSetFpMac(2) && text.text.FWriteRgb(chars, 2, 0));
        ChunkIdentification rich;
        CHECK(text.FWrite(file, &rich));
        ChunkNumber child;
        CHECK(file->FAddChild(kctgScen, number, 4, 0, kctgTbox, &child));
        TextBoxData meta;
        meta.state.first = 3;
        meta.state.end = 19;
        meta.state.story = true;
        meta.SetBounds(1, 2, 100, 80);
        CHECK(meta.FWrite(file, child) && file->FAdoptChild(kctgTbox, child, rich.ctg, rich.cno, 0));
        int32_t id = 4;
        add(starts, sevtAddTbox, &id, 4);
    }
    auto write = [&](PGeneralGroup g, ChunkTagOrType type, ChildChunkID id) {
        ChunkNumber child;
        DataBlock block;
        CHECK(file->FAddChild(kctgScen, number, id, g->CbOnFile(), type, &child, &block) &&
              g->FWrite(&block, swap ? kboOther : kboCur));
    };
    if (scenario != 1)
        write(frames, kctgFrmGg, 0);
    if (scenario != 2)
        write(starts, kctgStartGg, 1);
    ReleasePpo(&frames);
    ReleasePpo(&starts);
    return number;
}
static bool Missing(const MissingSourceRequest &)
{
    return false;
}
int main()
{
    auto app = NewObj TestApp;
    CHECK(app);
    if (auto name = std::getenv("MM_SCENE_STORE_SNAPSHOT"))
    {
        snapshot = std::fopen(name, "wb");
        CHECK(snapshot);
    }
#ifndef KAUAI_SDL3
    vwig.hdcApp = GetDC(nullptr);
    CHECK(vwig.hdcApp);
#endif
    CHECK(vntl.FInit());
    auto palette = DynamicArray::PglNew(sizeof(Color));
    CHECK(palette);
    for (int i = 0; i < 256; ++i)
    {
        Color c = {byte(i), byte(i), byte(i), 0};
        CHECK(palette->FAdd(&c));
    }
    GraphicsPort::SetActiveColors(palette, fpalIdentity);
    ReleasePpo(&palette);
    vpcex = CommandExecutionManager::PcexNew(20, 20);
    CHECK(vpcex);
    vpsndm = SoundManager::PsndmNew();
    CHECK(vpsndm);
    String path;
    path.SetSzs(PszLit("."));
    Filename root;
    CHECK(root.FBuildFromPath(&path));
    vptagm = TagManager::PtagmNew(&root, Missing, 0);
    CHECK(vptagm);
    auto client = NewObj Client;
    CHECK(client);
    auto movie = Movie::PmvieNew(false, client, nullptr, cnoNil);
    CHECK(movie);

    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    observed = ChunkyResourceFile::PcrfNew(file, 1 << 20);
    CHECK(observed);

    for (int swap = 0; swap < 2; ++swap)
        for (int scenario = 0; scenario < 7; ++scenario)
        {
            auto number = Fixture(file, scenario, swap != 0);
            for (int kind = 0; kind < 3; ++kind)
                for (int failure = 0; failure <= 22; ++failure)
                {
                    auto tags = TagList::PtaglNew();
                    CHECK(tags);
#ifdef DEBUG
                    vmutxMem.Enter();
                    auto &a = kind == 0 ? vdmglob.dmaglBase : kind == 1 ? vdmglob.dmaglHq : vdmglob.dmaglPv;
                    a.cactDo = failure ? failure - 1 : 0;
                    a.cactFail = failure ? 1 : 0;
                    vmutxMem.Leave();
#endif
                    bool ok = Scene::FAddTagsToTagl(file, number, tags);
#ifdef DEBUG
                    vmutxMem.Enter();
                    a.cactDo = a.cactFail = 0;
                    vmutxMem.Leave();
#endif
                    Value(ok);
                    Tags::State(tags);
                    ReleasePpo(&tags);
                    CHECK(observed->CactRef() == 1);
                    vpers->Clear();
                }
            if (scenario == 0 || scenario == 6)
            {
                auto scene = Scene::PscenRead(movie, observed, number);
                CHECK(scene);
                auto refs = observed->CactRef();
                for (int kind = 0; kind < 3; ++kind)
                    for (int failure = 0; failure <= 26; ++failure)
                    {
                        auto target = ChunkyFile::PcflCreateTemp();
                        CHECK(target);
                        auto resources = ChunkyResourceFile::PcrfNew(target, 1 << 20);
                        CHECK(resources);
                        ChunkNumber sound;
                        CHECK(target->FAdd(0, kctgMsnd, &sound));
                        target->Move(kctgMsnd, sound, kctgMsnd, 123);
                        ChunkNumber output;
#ifdef DEBUG
                        vmutxMem.Enter();
                        auto &a = kind == 0 ? vdmglob.dmaglBase : kind == 1 ? vdmglob.dmaglHq : vdmglob.dmaglPv;
                        a.cactDo = failure ? failure - 1 : 0;
                        a.cactFail = failure ? 1 : 0;
                        vmutxMem.Leave();
#endif
                        bool ok = scene->FWrite(resources, &output);
#ifdef DEBUG
                        vmutxMem.Enter();
                        a.cactDo = a.cactFail = 0;
                        vmutxMem.Leave();
#endif
                        Value(ok);
                        Value(output);
                        Value(observed->CactRef());
                        Value(resources->CactRef());
                        Value(target->FFind(kctgScen, output));
                        if (ok)
                            Written(target, output);
                        CHECK(observed->CactRef() == refs);
                        ReleasePpo(&resources);
                        ReleasePpo(&target);
                        vpers->Clear();
                    }
                ReleasePpo(&scene);
                CHECK(observed->CactRef() == 1);
            }
            file->Delete(kctgScen, number);
        }
    ReleasePpo(&observed);
    ReleasePpo(&file);
    ReleasePpo(&movie);
    ReleasePpo(&client);
    ReleasePpo(&vptagm);
    ReleasePpo(&vpsndm);
    ReleasePpo(&vpcex);
    BRender::World::CloseBRender();
#ifndef KAUAI_SDL3
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
#endif
    delete app;
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("Scene store: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one) && sizeof(void *) == 8 && sizeof(achar) == 1)
#ifdef DEBUG
        // Scene sound tags are read directly from the event bytes.
        CHECK(recorded == 165600 && hash == 0xBCD9A5A9U);
#else
        CHECK(recorded == 171264 && hash == 0x2F550AE5U);
#endif
}
