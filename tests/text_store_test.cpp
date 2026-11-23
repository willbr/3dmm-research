#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "soc_core.h"
#include "text_box_archive_fixture.h"
#undef size
ASSERTNAME
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%ld: %s\n", file, long(line), message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
static void CheckAt(bool ok, int line)
{
    if (!ok)
    {
        std::fprintf(stderr, "Text store check failed at %d\n", line);
        std::exit(1);
    }
}
#define Check(ok) CheckAt((ok), __LINE__)
struct Trace
{
    FILE *file;
    unsigned bytes = 0, hash = 2166136261U;
    Trace()
    {
        const char *p = std::getenv("MM_TEXT_STORE_SNAPSHOT");
        file = p ? std::fopen(p, "wb") : std::tmpfile();
        Check(file != nullptr);
    }
    void Raw(const void *v, std::size_t n)
    {
        auto p = static_cast<const byte *>(v);
        for (std::size_t i = 0; i < n; ++i)
            hash = (hash ^ p[i]) * 16777619U;
        bytes += unsigned(n);
        Check(std::fwrite(v, 1, n, file) == n);
    }
    void Word(unsigned n)
    {
        Raw(&n, 4);
    }
    void Chunk(PChunkyFile f, ChunkIdentification id)
    {
        DataBlock b;
        Check(f->FFind(id.ctg, id.cno, &b) && b.FUnpackData());
        std::vector<byte> data(b.Cb());
        if (!data.empty())
            Check(b.FReadRgb(data.data(), b.Cb(), 0));
        Word(id.ctg);
        Word(id.cno);
        Word(unsigned(data.size()));
        Raw(data.data(), data.size());
    }
    void Tree(PChunkyFile f, ChunkIdentification id)
    {
        Chunk(f, id);
        for (LegacyLong i = 0; i < f->Ckid(id.ctg, id.cno); ++i)
        {
            ChildChunkIdentification child;
            Check(f->FGetKid(id.ctg, id.cno, i, &child));
            Word(child.chid);
            Chunk(f, child.cki);
        }
    }
    void State(RichTextData &s)
    {
        const unsigned values[] = {
            unsigned(s.defaults.bo),      unsigned(s.defaults.oskFont),   unsigned(s.defaults.dxpDef),
            unsigned(s.defaults.dypFont), unsigned(s.defaults.lwAcrBack), unsigned(s.text.cb),
            unsigned(s.textOsk),          unsigned(s.propertyBo),         unsigned(s.propertyOsk),
            unsigned(s.argumentBo),       unsigned(s.argumentOsk)};
        Raw(values, sizeof(values));
        Word(unsigned(s.fontName.Cch()));
        Raw(s.fontName.Prgch(), s.fontName.Cch() * sizeof(achar));
        std::vector<byte> text(s.text.cb);
        if (!text.empty())
            Check(s.text.FReadRgb(text.data(), s.text.cb, 0));
        Raw(text.data(), text.size());
        Word(mm_text_collection_count(s.properties));
        for (MMU32 i = 0; i < mm_text_collection_count(s.properties); ++i)
        {
            MMU32 p[2];
            Check(mm_text_collection_property(s.properties, i, p));
            if (s.propertyBo == kboOther)
                SwapBytesRglw(p, 2);
            Raw(p, sizeof(p));
        }
        Word(mm_text_collection_count(s.arguments));
        for (MMU32 i = 0; i < mm_text_collection_count(s.arguments); ++i)
        {
            MMI32 bytes;
            const void *p = mm_text_collection_argument(s.arguments, i, &bytes);
            Word(!p);
            if (p)
            {
                Word(bytes);
                Raw(p, bytes);
                MMU32 fixed = mm_text_collection_argument_ref(s.arguments, i);
                if (s.argumentBo == kboOther)
                    SwapBytesRglw(&fixed, 1);
                Raw(&fixed, sizeof(fixed));
            }
        }
    }
    void Finish()
    {
        Check(std::fclose(file) == 0);
#ifdef _WIN64
        Check(bytes == 4998 && hash == 0x7D32092FU);
#endif
        std::printf("Text store trace: bytes=%u fingerprint=%08X\n", bytes, hash);
    }
};
static void Source(RichTextData &s, int foreign)
{
    s.defaults = {kboOther, koskCur, 240, 16, int32_t(0xfe001122)};
    s.fontName.SetSzs(PszLit("Arial"));
    MMTextCollectionConfig config = {};
    config.swapped_payload = foreign;
    s.properties = mm_text_collection_new(MM_TEXT_PROPERTIES, &config);
    s.arguments = mm_text_collection_new(MM_TEXT_ARGUMENTS, &config);
    const MMU32 fixed = 0x02000001;
    byte argument[] = {6, 0, 4, 0, 2, 0, 9, 8, 7, 0, 0, 0};
    if (foreign)
    {
        s.propertyBo = s.argumentBo = kboOther;
        s.propertyOsk = s.argumentOsk = koskSbMac;
    }
    Check(s.FAddProperty({0x03000002, 0x010000ff}));
    Check(mm_text_collection_add_argument(s.arguments, argument, sizeof(argument), fixed, nullptr));
    MMU32 hole;
    Check(mm_text_collection_add_argument(s.arguments, nullptr, 0, fixed, &hole));
    Check(mm_text_collection_delete(s.arguments, hole));
    s.text.pfil = FileObject::PfilCreateTemp();
    Check(s.text.pfil != nullptr);
    const byte text[] = {'A', 0, 'B', '\r'};
    s.text.cb = sizeof(text);
    s.textOsk = 0x1111;
    Check(s.text.pfil->FSetFpMac(s.text.cb) && s.text.FWriteRgb(const_cast<byte *>(text), s.text.cb, 0));
}
static ChunkIdentification Child(PChunkyFile f, ChunkIdentification root, ChunkTagOrType type)
{
    ChildChunkIdentification child;
    Check(f->FGetKidChidCtg(root.ctg, root.cno, 0, type, &child));
    return child.cki;
}
static void RichCases(Trace &trace)
{
    for (int kind = 0; kind < 22; ++kind)
    {
        auto file = ChunkyFile::PcflCreateTemp();
        Check(file != nullptr);
        RichTextData source;
        Source(source, kind == 1);
        ChunkIdentification root;
        Check(source.FWrite(file, &root));
        DataBlock b;
        Check(file->FFind(root.ctg, root.cno, &b));
        std::vector<byte> header(b.Cb());
        Check(b.FReadRgb(header.data(), b.Cb(), 0));
        if (kind == 1)
        {
            SwapBytesRgsw(header.data(), 2);
            SwapBytesRglw(header.data() + 4, 3);
        }
        if (kind == 2)
            header[0] = 2;
        if (kind == 3)
            reinterpret_cast<RichTextProperties *>(header.data())->dypFont = 3;
        if (kind == 4)
            reinterpret_cast<RichTextProperties *>(header.data())->dypFont = 256;
        if (kind == 5)
            reinterpret_cast<RichTextProperties *>(header.data())->dxpDef = 0;
        if (kind == 6)
            reinterpret_cast<RichTextProperties *>(header.data())->dxpDef = kcbMax;
        if (kind == 7)
            header.resize(15);
        if (kind == 8)
        {
            header.resize(17);
            header[16] = 0xff;
        }
        if (kind == 9)
            header.resize(16);
        if (kind == 10)
        {
            String empty;
            header.resize(16 + empty.CbData());
            empty.GetData(header.data() + 16);
        }
        if (kind == 11)
            header.insert(header.end(), {1, 2, 3, 4});
        Check(file->FPutPv(header.data(), LegacyLong(header.size()), root.ctg, root.cno));
        if (kind == 12 || kind == 13 || kind == 14 || kind == 15 || kind == 16 || kind == 17 || kind == 18 ||
            kind == 19 || kind == 20 || kind == 21)
        {
            auto type = kind <= 13 ? kctgText : kind <= 16 ? kctgTxtProps : kctgTxtPropArgs;
            auto child = Child(file, root, type);
            if (kind == 12 || kind == 14)
                file->DeleteChild(root.ctg, root.cno, child.ctg, child.cno, 0);
            else if (kind == 13)
            {
                byte shortText = 3;
                Check(file->FPutPv(&shortText, 1, child.ctg, child.cno));
            }
            else if (kind == 15)
            {
                auto p = DynamicArray::PglNew(4);
                Check(p && file->FPut(p->CbOnFile(), child.ctg, child.cno, &b));
                Check(p->FWrite(&b));
                ReleasePpo(&p);
            }
            else if (kind == 16)
            {
                Check(file->FFind(child.ctg, child.cno, &b));
                byte bad[2] = {2, 0};
                Check(b.FWriteRgb(bad, 2, 0));
            }
            else if (kind == 17 || kind == 18)
            {
                auto g = AllocatedGroup::PagNew(kind == 17 ? 8 : 0);
                Check(g && file->FPut(g->CbOnFile(), child.ctg, child.cno, &b));
                Check(g->FWrite(&b));
                ReleasePpo(&g);
            }
            else if (kind == 19)
                file->DeleteChild(root.ctg, root.cno, child.ctg, child.cno, 0);
            else if (kind == 20)
            {
                Check(file->FFind(child.ctg, child.cno, &b));
                byte bad[2] = {2, 0};
                Check(b.FWriteRgb(bad, 2, 0));
            }
            else if (kind == 21)
            {
                Check(mm_text_collection_delete(source.arguments, 0));
                ChunkIdentification emptyRoot;
                Check(source.FWrite(file, &emptyRoot));
                root = emptyRoot;
            }
        }
        RichTextData loaded;
        Source(loaded, 0);
        auto before = loaded.text.pfil;
        LegacyLong refs = before->CactRef();
        bool ok = loaded.FRead(file, root.ctg, root.cno);
        const bool expected = kind == 0 || kind == 1 || (kind >= 8 && kind <= 11) || kind == 19 || kind == 21;
        if (ok != expected)
        {
            std::fprintf(stderr, "rich case %d returned %d\n", kind, ok);
            std::exit(1);
        }
        trace.Word(kind);
        trace.Word(ok);
        if (!ok)
            Check(loaded.text.pfil == before && before->CactRef() == refs);
        trace.State(loaded);
        if (ok)
        {
            ChunkIdentification copy;
            Check(loaded.FWrite(file, &copy));
            trace.Tree(file, copy);
            RichTextData reread;
            Check(reread.FRead(file, copy.ctg, copy.cno));
            trace.State(reread);
        }
        ReleasePpo(&file);
    }
    for (int kind = 0; kind < 7; ++kind)
    {
        auto file = ChunkyFile::PcflCreateTemp();
        RichTextData s;
        Source(s, 0);
        ChunkIdentification output = {0x12345678, 0x87654321};
        if (kind == 0)
            s.text.cb = -1;
        if (kind == 1)
            s.text.cb = kcbMax - 1;
        if (kind == 2)
        {
            mm_text_collection_dispose(s.properties);
            s.properties = nullptr;
        }
        if (kind == 3)
        {
            mm_text_collection_dispose(s.properties);
            s.properties = mm_text_collection_new(MM_TEXT_ARGUMENTS, nullptr);
            Check(s.properties != nullptr);
        }
        if (kind == 4)
        {
            mm_text_collection_dispose(s.arguments);
            s.arguments = mm_text_collection_new(MM_TEXT_PROPERTIES, nullptr);
            Check(s.arguments != nullptr);
        }
        if (kind == 5)
            s.defaults.dypFont = 256;
        if (kind == 6)
            s.defaults.dxpDef = 0;
        Check(!s.FWrite(file, &output));
        trace.Word(output.ctg);
        trace.Word(output.cno);
        trace.Word(file->Ccki());
        ReleasePpo(&file);
    }
}
static void BoxCases(Trace &trace)
{
    for (int kind = 0; kind < 12; ++kind)
    {
        auto file = ChunkyFile::PcflCreateTemp();
        RichTextData text;
        Source(text, 0);
        ChunkIdentification rich;
        Check(text.FWrite(file, &rich));
        ChunkNumber number;
        Check(file->FAdd(0, kctgTbox, &number));
        MMTextBoxState s = {};
        s.first = -2;
        s.end = 18;
        s.story = true;
        s.child = 7;
        TextBoxArchiveFixture::SetBounds(s, 10, 20, 150, 75);
        Check(file->FAdoptChild(kctgTbox, number, rich.ctg, rich.cno, 7) &&
              TextBoxArchiveFixture::Write(file, number, s));
        DataBlock block;
        Check(file->FFind(kctgTbox, number, &block));
        byte header[MM_TEXT_BOX_HEADER_BYTES + 1] = {};
        Check(block.FReadRgb(header, MM_TEXT_BOX_HEADER_BYTES, 0));
        if (kind == 1)
        {
            SwapBytesRgsw(header, 2);
            SwapBytesRglw(header + 4, 7);
        }
        if (kind == 2)
        {
            const std::uint16_t invalidOrder = 2;
            std::memcpy(header, &invalidOrder, sizeof(invalidOrder));
        }
        if (kind == 3 || kind == 8)
        {
            const MMI32 first = kind == 3 ? INT32_MIN : INT32_MAX;
            std::memcpy(header + 4, &first, sizeof(first));
        }
        if (kind == 4)
            header[32] = 2; // Invalid story flag.
        if (kind == 5 || kind == 6)
        {
            const MMU32 missingChild = 8;
            std::memcpy(header + 28, &missingChild, sizeof(missingChild));
        }
        if (kind == 7 || kind == 8)
        {
            const MMI32 end = kind == 7 ? -8 : INT32_MIN;
            std::memcpy(header + 8, &end, sizeof(end));
        }
        if (kind == 9)
        {
            header[2] = header[3] = 0x11; // Unknown system marker and nonzero padding.
            header[33] = header[34] = header[35] = 0xff;
        }
        Check(file->FPutPv(header, MM_TEXT_BOX_HEADER_BYTES - (kind == 10 ? 1 : 0), kctgTbox, number));
        if (kind == 11)
            Check(file->FPutPv(header, sizeof(header), kctgTbox, number));
        MMTextBoxState out = {};
        out.first = 99;
        MMTextChunk child = {22, 33};
        bool ok = TextBoxArchiveFixture::Read(file, number, out, kind == 6 ? nullptr : &child);
        bool expected = kind == 0 || kind == 1 || (kind >= 6 && kind <= 9);
        Check(ok == expected);
        const unsigned v[] = {ok,
                              unsigned(out.first),
                              unsigned(out.end),
                              unsigned(out.current),
                              unsigned(out.story),
                              unsigned(out.left),
                              unsigned(out.top),
                              unsigned(out.right),
                              unsigned(out.bottom),
                              out.child,
                              child.type,
                              child.number};
        trace.Raw(v, sizeof(v));
        if (ok)
        {
            Check(TextBoxArchiveFixture::Write(file, number, out));
            trace.Chunk(file, {kctgTbox, number});
        }
        ReleasePpo(&file);
    }
}
int main()
{
    Trace trace;
    RichCases(trace);
    BoxCases(trace);
    trace.Finish();
    std::puts(
        "Text persistence: wire bytes, malformed input, transactional reads, font fallback and round trips passed.");
    return 0;
}
