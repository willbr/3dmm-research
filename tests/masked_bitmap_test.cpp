#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "kauai_core.h"
#include "mbmp.h"
#undef size
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *f, LegacyLong n, schar *s, void *, LegacyLong)
{
    std::fprintf(stderr, "%s:%ld: %s\n", f, (long)n, s);
    std::exit(1);
}
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "masked bitmap: %d\n", __LINE__);                                                     \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
struct Access : MaskedBitmapMBMP
{
    using Header = MaskedBitmapOnFile;
};
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
static std::vector<byte> Bytes(PMaskedBitmapMBMP m)
{
    DataBlock b;
    CHECK(b.FSetTemp(m->CbOnFile()) && m->FWrite(&b));
    std::vector<byte> data(b.Cb());
    CHECK(b.FRead(data.data()));
    return data;
}
static void State(PMaskedBitmapMBMP m)
{
    Value(m != nullptr);
    if (!m)
        return;
    RC r;
    m->GetRc(&r);
    Record(&r, sizeof(r));
    auto b = Bytes(m);
    Value(b.size());
    Record(b.data(), b.size());
    for (int y = r.ypTop - 1; y <= r.ypBottom; ++y)
        for (int x : {r.xpLeft - 1, r.xpLeft, r.xpLeft + 1, (r.xpLeft + r.xpRight) / 2, r.xpRight - 1, r.xpRight})
            Value(m->FPtIn(x, y));
}
static void Constructors()
{
    for (int w : {1, 3, 8, 256, 600})
        for (int h : {1, 5})
            for (int flags = 0; flags < 4; ++flags)
                for (int pattern = 0; pattern < 5; ++pattern)
                    for (int crop = 0; crop < 2; ++crop)
                        for (int ref = 0; ref < 2; ++ref)
                        {
                            std::vector<byte> data((w + 3) * h, 0);
                            for (int y = 0; y < h; ++y)
                                for (int x = 0; x < w; ++x)
                                {
                                    bool on = pattern == 1 || (pattern == 2 && x % 3 && y % 3) ||
                                              (pattern == 3 && x == w / 2 && y == h / 2) ||
                                              (pattern == 4 && (x == 0 || x == w - 1 || y == h - 1));
                                    if (on)
                                        data[y * (w + 3) + x] = byte(1 + (x * 11 + y * 17) % 254);
                                }
                            RC r(crop && w > 3 ? 1 : 0, crop && h > 1 ? 1 : 0, crop && w > 3 ? w - 1 : w, h);
                            auto m = MaskedBitmapMBMP::PmbmpNew(data.data(), w + 3, h, &r, ref ? 7 : 0, ref ? -3 : 0, 0,
                                                                flags, 37);
                            CHECK(m);
                            State(m);
                            ReleasePpo(&m);
                        }
}
static void Loads()
{
    byte pixels[192];
    std::memset(pixels, 17, sizeof(pixels));
    RC r(0, 0, 32, 6);
    auto m = MaskedBitmapMBMP::PmbmpNew(pixels, 32, 6, &r, 2, -1, 0);
    CHECK(m);
    auto raw = Bytes(m);
    ReleasePpo(&m);
    for (int variant = 0; variant < 6; ++variant)
        for (int file = 0; file < 2; ++file)
            for (int factory = 0; factory < 2; ++factory)
            {
                auto data = raw;
                auto header = reinterpret_cast<Access::Header *>(data.data());
                if (variant == 1)
                {
                    SwapBytesBom(header, kbomMbmph);
                    SwapBytesRgsw(data.data() + sizeof(Access::Header), 6);
                }
                if (variant == 2)
                    header->bo = 0x1234;
                if (variant == 3)
                    header->swReserved = 1;
                if (variant == 4)
                    header->cb++;
                DataBlock b;
                PChunkyFile cf = nullptr;
                ChunkNumber number = 0;
                if (file)
                {
                    cf = ChunkyFile::PcflCreateTemp();
                    CHECK(cf && cf->FAddPv(data.data(), data.size(), kctgMbmp, &number));
                    if (variant == 5)
                        CHECK(cf->FPackData(kctgMbmp, number));
                    CHECK(cf->FFind(kctgMbmp, number, &b));
                }
                else
                {
                    CHECK(b.FSetTemp(data.size()) && b.FWrite(data.data()));
                    if (variant == 5)
                        CHECK(b.FPackData());
                }
                auto cache = ChunkyResourceFile::PcrfNew(cf ? cf : (cf = ChunkyFile::PcflCreateTemp()), 0);
                CHECK(cache);
                LegacyLong estimate = -1;
                CHECK(MaskedBitmapMBMP::FReadMbmp(cache, kctgMbmp, number, &b, nullptr, &estimate));
                Value(estimate);
                PBaseCacheableObject object = nullptr;
                if (factory)
                {
                    bool ok = MaskedBitmapMBMP::FReadMbmp(cache, kctgMbmp, number, &b, &object, &estimate);
                    Value(ok);
                    m = ok ? static_cast<PMaskedBitmapMBMP>(object) : nullptr;
                    if (ok)
                        Value(estimate);
                }
                else
                    m = MaskedBitmapMBMP::PmbmpRead(&b);
                Value(b.Cb());
                State(m);
                ReleasePpo(&m);
                b.Free();
                ReleasePpo(&cache);
                ReleasePpo(&cf);
            }
}
static void FailureCleanup()
{
#ifdef DEBUG
    byte pixels[32];
    std::memset(pixels, 17, sizeof(pixels));
    RC r(0, 0, 8, 4);
    auto original = MaskedBitmapMBMP::PmbmpNew(pixels, 8, 4, &r, 0, 0, 0);
    CHECK(original);
    auto raw = Bytes(original);
    ReleasePpo(&original);
    for (int kind = 0; kind < 3; ++kind)
        for (int failure = 0; failure < 9; ++failure)
        {
            LegacyLong base = vdmglob.dmaglBase.cv, hq = vdmglob.dmaglHq.cv, pv = vdmglob.dmaglPv.cv;
            auto inject = [&](bool enabled) {
                vmutxMem.Enter();
                auto &a = kind == 0 ? vdmglob.dmaglBase : kind == 1 ? vdmglob.dmaglHq : vdmglob.dmaglPv;
                a.cactDo = enabled && failure ? failure - 1 : 0;
                a.cactFail = enabled && failure ? 1 : 0;
                vmutxMem.Leave();
            };
            inject(true);
            auto m = MaskedBitmapMBMP::PmbmpNew(pixels, 8, 4, &r, 0, 0, 0);
            inject(false);
            ReleasePpo(&m);
            {
                DataBlock block;
                CHECK(block.FSetTemp(raw.size()) && block.FWrite(raw.data()));
                inject(true);
                m = MaskedBitmapMBMP::PmbmpRead(&block);
                inject(false);
                ReleasePpo(&m);
            }
            vpers->Clear();
            CHECK(vdmglob.dmaglBase.cv == base && vdmglob.dmaglHq.cv == hq && vdmglob.dmaglPv.cv == pv);
        }
#endif
}
static void Imports()
{
    auto palette = DynamicArray::PglNew(sizeof(Color));
    CHECK(palette);
    for (int i = 0; i < 256; ++i)
    {
        Color c = {};
        CHECK(palette->FAdd(&c));
    }
    for (int w : {3, 5, 8})
        for (bool flip : {false, true})
            for (int flags = 0; flags < 4; ++flags)
            {
                int row = (w + 3) & ~3;
                std::vector<byte> pixels(row * 3, 0);
                for (size_t i = 0; i < pixels.size(); ++i)
                    if (i % 3)
                        pixels[i] = byte(i + 1);
                Filename name;
                CHECK(name.FGetTemp() && FWriteBitmap(&name, pixels.data(), palette, w, 3, flip));
                auto m = MaskedBitmapMBMP::PmbmpReadNative(&name, 0, 4, -2, flags, 37);
                CHECK(m);
                State(m);
                ReleasePpo(&m);
                CHECK(name.FDelete());
            }
    ReleasePpo(&palette);
}
int main()
{
    if (auto name = std::getenv("THREE_MM_MASKED_BITMAP_SNAPSHOT"))
    {
        snapshot = std::fopen(name, "wb");
        CHECK(snapshot);
    }
    Constructors();
    Loads();
    Imports();
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("Masked bitmap: bytes=%u fingerprint=%08X\n", recorded, hash);
#ifdef DEBUG
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
        CHECK(recorded == 210848 && hash == 0x65C24F63U);
#endif
    FailureCleanup();
}
