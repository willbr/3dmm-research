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
            std::fprintf(stderr, "scene sound: %d\n", __LINE__);                                                       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static unsigned hash = 2166136261U, recorded;
static bool recording = true;
static PChunkyResourceFile observed;
static void Record(const void *p, size_t n)
{
    if (!recording)
        return;
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

static void State(PSceneSoundEvent p)
{
    Value(p != nullptr);
    Value(observed->CactRef());
    if (!p)
        return;
    Value(p->vlm);
    Value(p->sty);
    Value(p->fLoop);
    Value(p->ctagc);
    Value(p->Cb());
    for (int i = 0; i < p->ctagc; ++i)
    {
        auto tag = p->Ptag(i);
        Value(*p->Pchid(i));
        Value(tag->sid);
        Value(tag->ctg);
        Value(tag->cno);
        Value(tag->pcrf != nullptr);
    }
}
static void Inject(bool fail)
{
#ifdef DEBUG
    vmutxMem.Enter();
    vdmglob.dmaglPv.cactDo = 0;
    vdmglob.dmaglPv.cactFail = fail ? 1 : 0;
    vmutxMem.Leave();
#else
    (void)fail;
#endif
}
static bool Missing(const MissingSourceRequest &)
{
    return false;
}
int main()
{
    auto app = NewObj TestApp;
    CHECK(app);
    if (auto name = std::getenv("THREE_MM_SCENE_SOUND_SNAPSHOT"))
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
    for (int count : {1, 2, 4})
        for (int loop : {0, 1})
            for (int failure : {0, 1})
            {
                std::vector<TagChildPair> records(count);
                for (int i = 0; i < count; ++i)
                {
                    records[i].chid = 17 + i;
                    records[i].tag.sid = i % 2 ? 3 : 0;
                    records[i].tag.pcrf = observed;
                    records[i].tag.ctg = kctgMsnd;
                    records[i].tag.cno = 42 + i;
                }
                Inject(failure != 0);
                auto sound = SceneSoundEvent::PsseNew(83, loop ? styMidi : stySfx, loop != 0, count, records.data());
                Inject(false);
                State(sound);
                vpers->Clear();
                if (!sound)
                {
                    sound = SceneSoundEvent::PsseNew(83, loop ? styMidi : stySfx, loop != 0, count, records.data());
                    CHECK(sound);
                    State(sound);
                }
                Inject(failure != 0);
                auto duplicate = sound->PsseDup();
                Inject(false);
                State(duplicate);
                vpers->Clear();
                ReleasePpsse(&duplicate);
                Value(observed->CactRef());
                auto group = GeneralGroup::PggNew(sizeof(SceneEvent));
                CHECK(group);
                SceneEvent event = {7, sevtPlaySnd};
                CHECK(group->FInsert(0, sound->Cb(), sound, &event));
                for (bool tags : {false, true})
                {
                    auto copy = SceneSoundEvent::PsseDupFromGg(group, 0, tags);
                    CHECK(copy);
                    State(copy);
                    ReleasePpsse(&copy);
                    Value(observed->CactRef());
                }
                ReleasePpo(&group);
                for (int swapped : {0, 1})
                {
                    long bytes = -1;
                    Inject(failure != 0);
                    auto wire = SceneSoundToWire(sound, &bytes);
                    Inject(false);
                    Value(bytes);
                    Value(wire != nullptr);
                    vpers->Clear();
                    if (!wire)
                    {
                        wire = SceneSoundToWire(sound, &bytes);
                        CHECK(wire);
                    }
                    Record(wire, bytes);
                    if (swapped)
                    {
                        auto words = static_cast<uint32_t *>(wire);
                        for (int i = 0; i < bytes / 4; ++i)
                            words[i] = ((words[i] & 255) << 24) | ((words[i] & 65280) << 8) |
                                       ((words[i] >> 8) & 65280) | (words[i] >> 24);
                    }
                    Inject(failure != 0);
                    auto decoded = SceneSoundFromWire(wire, bytes, swapped != 0);
                    Inject(false);
                    State(decoded);
                    Record(wire, bytes);
                    vpers->Clear();
                    FreePpv(reinterpret_cast<void **>(&decoded));
                    FreePpv(&wire);
                }
                TAG tag = {0, observed, kctgMsnd, 91};
                Inject(failure != 0);
                auto added = sound->PsseAddTagChid(&tag, 99);
                Inject(false);
                State(added);
                vpers->Clear();
                if (added)
                {
                    FreePpv(reinterpret_cast<void **>(&sound));
                    sound = added;
                }
                ReleasePpsse(&sound);
                Value(observed->CactRef());
                CHECK(observed->CactRef() == 1);
            }
#ifdef THREE_MM_ENGINE_SCENE_SOUND_H
    // Supplemental bounded-failure cases; the native group helper crashed on
    // allocation failure, so these deliberately sit outside the frozen trace.
    recording = false;
    TagChildPair one = {};
    one.tag = {0, observed, kctgMsnd, 1};
    auto packet = SceneSoundEvent::PsseNew(100, styMidi, false, 1, &one);
    CHECK(packet);
    auto group = GeneralGroup::PggNew(sizeof(SceneEvent));
    CHECK(group);
    SceneEvent event = {1, sevtPlaySnd};
    CHECK(group->FInsert(0, packet->Cb(), packet, &event));
#ifdef DEBUG
    Inject(true);
    auto failed = SceneSoundEvent::PsseDupFromGg(group, 0);
    Inject(false);
    CHECK(!failed && observed->CactRef() == 2);
    vpers->Clear();
#endif
    long wireBytes;
    auto wire = SceneSoundToWire(packet, &wireBytes);
    CHECK(wire && !SceneSoundFromWire(wire, wireBytes - 1, false));
    FreePpv(&wire);
    ReleasePpo(&group);
    ReleasePpsse(&packet);
    CHECK(observed->CactRef() == 1);
#endif
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

    std::printf("Scene sound: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short endian = 1;
    if (*reinterpret_cast<const byte *>(&endian) && sizeof(TagChildPair) == 32)
    {
#ifdef DEBUG
        CHECK(recorded == 8232 && hash == 0x7C5AE3ADU);
#else
        CHECK(recorded == 9904 && hash == 0x01174345U);
#endif
    }
}
