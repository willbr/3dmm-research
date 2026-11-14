#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "kauai_core.h"
#include "mbmp.h"
#undef size
ASSERTNAME
void WarnProc(schar *f, LegacyLong n, schar *s)
{
    std::fprintf(stderr, "%s:%ld: %s\n", f, (long)n, s);
}
bool FAssertProc(schar *f, LegacyLong n, schar *s, void *, LegacyLong)
{
    WarnProc(f, n, s);
    std::exit(1);
}
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "bitmap file: %d\n", __LINE__);                                                       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static unsigned hash = 2166136261U, recorded;
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
static void Put(std::vector<byte> &b, int at, unsigned n, int count)
{
    while (count--)
    {
        b[at++] = byte(n);
        n >>= 8;
    }
}
static void Inject(int kind, int n)
{
#ifdef DEBUG
    vmutxMem.Enter();
    auto &a = kind ? vdmglob.dmaglPv : vdmglob.dmaglBase;
    a.cactDo = n ? n - 1 : 0;
    a.cactFail = n ? 1 : 0;
    vmutxMem.Leave();
#else
    (void)kind;
    (void)n;
#endif
}
static std::vector<byte> Source(int variant)
{
    std::vector<byte> payload;
    int width = variant == 1 ? 3 : 4, height = 3, row = (width + 3) & ~3;
    if (variant == 4)
        payload = {4, 7, 0, 0, 0, 3, 1, 2, 3, 0, 0, 0, 0, 1};
    else if (variant == 5)
        payload = {1, 9, 0, 2, 1, 1, 2, 8, 0, 1};
    else if (variant == 6)
        payload = {0, 1};
    else
    {
        payload.resize(row * height);
        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] = byte(i * 19);
    }
    std::vector<byte> b(1078 + payload.size(), 0);
    b[0] = 'B';
    b[1] = 'M';
    Put(b, 2, b.size(), 4);
    Put(b, 10, 1078, 4);
    Put(b, 14, 40, 4);
    Put(b, 18, width, 4);
    Put(b, 22, variant == 2 ? unsigned(-height) : height, 4);
    Put(b, 26, 1, 2);
    Put(b, 28, 8, 2);
    Put(b, 30, variant >= 4 && variant <= 6 ? 1 : 0, 4);
    Put(b, 34, variant == 3 ? 0 : payload.size(), 4);
    Put(b, 46, 256, 4);
    for (int i = 0; i < 1024; ++i)
        b[54 + i] = byte(i * 7);
    std::memcpy(b.data() + 1078, payload.data(), payload.size());
    if (variant == 7)
        Put(b, 28, 24, 2);
    if (variant == 8)
        Put(b, 46, 17, 4); // Valid unless palette output is requested.
    if (variant == 9)
        Put(b, 2, b.size() + 1, 4);
    if (variant == 10)
    {
        Put(b, 10, 54, 4);
        Put(b, 34, 0, 4);
    }
    if (variant == 11)
    {
        b.insert(b.end(), 7, 0xab);
        Put(b, 2, b.size(), 4);
        Put(b, 34, 0, 4);
    }
    return b;
}
static void Reads()
{
    for (int variant = 0; variant < 12; ++variant)
    {
        auto raw = Source(variant);
        Filename name;
        CHECK(name.FGetTemp());
        auto f = FileObject::PfilCreate(&name);
        CHECK(f && f->FWriteRgb(raw.data(), raw.size(), 0));
        ReleasePpo(&f);
        for (int mask = 0; mask < 8; ++mask)
            for (int kind = 0; kind < 2; ++kind)
                for (int failure = 0; failure < 9; ++failure)
                {
                    byte *pixels = nullptr;
                    PDynamicArray palette = nullptr;
                    LegacyLong w = 123, h = 456;
                    bool up = false;
                    Inject(kind, failure);
                    bool ok =
                        FReadBitmap(&name, mask & 1 ? &pixels : nullptr, mask & 2 ? &palette : nullptr,
                                    mask & 4 ? &w : nullptr, mask & 4 ? &h : nullptr, mask & 4 ? &up : nullptr, 73);
                    Inject(kind, 0);
                    Value(ok);
                    Value(pixels != nullptr);
                    Value(palette != nullptr);
                    if (ok)
                    {
                        if (mask & 4)
                        {
                            Value(w);
                            Value(h);
                            Value(up);
                        }
                        if (palette)
                        {
                            auto p = palette->PvLock(0);
                            Record(p, 1024);
                            palette->Unlock();
                        }
                        if (pixels)
                            Record(pixels, 12);
                    }
                    FreePpv((void **)&pixels);
                    ReleasePpo(&palette);
                }
        CHECK(name.FDelete());
    }
}
static void Writes()
{
    auto pal = DynamicArray::PglNew(sizeof(Color));
    CHECK(pal);
    for (int i = 0; i < 256; ++i)
    {
        Color c = {};
        c.bRed = byte(i * 3);
        c.bGreen = byte(i * 7);
        c.bBlue = byte(i * 11);
        c.bZero = byte(i * 13);
        CHECK(pal->FAdd(&c));
    }
    for (int w : {1, 3, 4, 5, 8})
        for (int h : {1, 2, 5})
            for (bool flip : {false, true})
            {
                std::vector<byte> pixels(((w + 3) & ~3) * h);
                for (size_t i = 0; i < pixels.size(); ++i)
                    pixels[i] = byte(i * 17 + 5);
                Filename name;
                CHECK(name.FGetTemp());
                CHECK(FWriteBitmap(&name, pixels.data(), pal, w, h, flip));
                auto f = FileObject::PfilOpen(&name);
                CHECK(f);
                std::vector<byte> b(f->FpMac());
                CHECK(f->FReadRgb(b.data(), b.size(), 0));
                Value(b.size());
                Record(b.data(), b.size());
                ReleasePpo(&f);
                CHECK(name.FDelete());
            }
    ReleasePpo(&pal);
}
int main()
{
    if (auto path = std::getenv("THREE_MM_BITMAP_FILE_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        CHECK(snapshot);
    }
    Reads();
    Writes();
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("Bitmap file: bytes=%u fingerprint=%08X\n", recorded, hash);
#ifdef DEBUG
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
        CHECK(recorded == 633064 && hash == 0xC71364BEU);
#endif
}
