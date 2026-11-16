/* Headless formatter oracle. Capture this against the old implementation before
 * changing it; callback order and partially published lines are observable. */
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace RichTextFixture
{
struct Trace
{
    std::vector<unsigned char> bytes;
    std::uint32_t fingerprint = 2166136261U;
    std::size_t size = 0;
    bool capture = std::getenv("MM_RICH_LAYOUT_SNAPSHOT") != nullptr;
    void Add(long value)
    {
        unsigned long word = static_cast<unsigned long>(value);
        for (int i = 0; i != 4; ++i, word >>= 8)
        {
            unsigned char byte = static_cast<unsigned char>(word);
            fingerprint = (fingerprint ^ byte) * 16777619U;
            if (capture) bytes.push_back(byte);
        }
        size += 4;
    }
    void Line(const KauaiTextLayout::Line &line)
    {
        Add(line.cpMin); Add(line.dypTot); Add(line.ccp);
        Add(line.xpLeft); Add(line.dyp); Add(line.dypAscent);
    }
};
template<class Ch> struct Provider
{
    using Character = Ch;
    using CharacterProperties = Host::CharacterProperties;
    using ParagraphProperties = Host::ParagraphProperties;
    using Rectangle = Host::Rectangle;
    Trace &trace;
    std::vector<Ch> text;
    ParagraphProperties paragraph;
    CharacterProperties font;
    int calls = 0, failAt = 0;
    bool mixed = false, objects = true;
    explicit Provider(Trace &value) : trace(value) {}
    void Event(int kind, long a = 0, long b = 0)
    {
        trace.Add(kind); trace.Add(a); trace.Add(b);
        if (++calls == failAt)
            throw std::runtime_error("formatter provider failure");
    }
    long CpMac() { Event(1); return static_cast<long>(text.size()); }
    unsigned long Flags(Ch ch) { Event(2, ch); return KauaiTextLayout::Flags(ch); }
    bool FMinPara(long cp)
    {
        Event(3, cp);
        if (!cp) return true;
        if (cp >= static_cast<long>(text.size()) || text[cp] == '\n') return false;
        while (cp-- > 0)
        {
            if (text[cp] == '\r') return true;
            if (text[cp] != '\n') return false;
        }
        return true;
    }
    void FetchRgch(long cp, long count, Ch *out)
    {
        Event(4, cp, count);
        if (cp < 0 || count < 0 || cp + count > static_cast<long>(text.size()))
            throw std::logic_error("invalid formatter fetch");
        std::copy_n(text.data() + cp, count, out);
    }
    void FetchChp(long cp, CharacterProperties *out, long *limit)
    {
        Event(5, cp);
        *out = {};
        *limit = static_cast<long>(text.size());
        if (mixed)
        {
            *limit = std::min(*limit, (cp / 3 + 1) * 3);
            out->ascent += cp % 7;
            out->descent += cp % 3;
            out->offset = cp % 5 - 2;
        }
    }
    void FetchPap(long cp, ParagraphProperties *out, long *limit)
    {
        Event(6, cp);
        *out = paragraph;
        *limit = static_cast<long>(text.size());
    }
    void SetFont(const CharacterProperties &value)
    {
        Event(7, value.ascent, value.offset); font = value;
    }
    void WarmFont() { Event(8); }
    long Offset(const CharacterProperties &value) { Event(9, value.offset); return value.offset; }
    void GetRcFromRgch(Rectangle *out, Ch *chars, long count)
    {
        Event(10, count);
        *out = {};
        out->xpLeft = -2;
        out->xpRight = -2;
        for (long i = 0; i < count; ++i)
        {
            trace.Add(chars[i]);
            out->xpRight += chars[i] == ' ' ? 3 : 4 + chars[i] % 5;
            if (i && chars[i-1] == 'A' && chars[i] == 'V') out->xpRight -= 2;
        }
        out->ypTop = -font.ascent;
        out->ypBottom = font.descent;
    }
    bool FGetObjectRc(long cp, CharacterProperties *style, Rectangle *out)
    {
        Event(11, cp);
        style->offset = 4; // mutation belongs to this run, even on a miss
        out->xpRight = cp % 2 ? 200 : 11;
        out->ypTop = -23; out->ypBottom = 8;
        return objects;
    }
};
template<class Ch> void Capture(Trace &trace)
{
    std::vector<std::vector<Ch>> texts;
    for (const char *s : {"\r", "\r\n\r\nAV\r", "AV AV AV\r", "\t\tA\tV\r",
                          "\1\nA\1\1\r", "\n\nAV\r", "AAAAAAAAAAAAAAAAAAAA\r"})
        texts.emplace_back(s, s + std::char_traits<char>::length(s));
    texts.emplace_back(127, static_cast<Ch>('A'));
    texts.back().insert(texts.back().end(), 130, static_cast<Ch>('\n'));
    texts.back().push_back('\r');
    texts.emplace_back(260, static_cast<Ch>(sizeof(Ch) == 2 ? 0x3a9 : 0xe9));
    texts.back().insert(texts.back().begin(), '\t'); texts.back().push_back('\r');
    trace.Add(sizeof(Ch));
    for (unsigned t = 0; t != texts.size(); ++t)
        for (int variant = 0; variant != 12; ++variant)
            for (long width : {0L, 1L, 17L, 36L, 1000L})
            {
                Provider<Ch> host(trace);
                host.text = texts[t]; host.mixed = variant % 2 != 0;
                host.objects = variant % 3 != 0;
                host.paragraph.jc = variant % 3;
                host.paragraph.nd = variant / 3;
                if (variant >= 6)
                {
                    host.paragraph.numLine = variant % 2 ? -129 : 385;
                    host.paragraph.numAfter = 511;
                    host.paragraph.dypExtraLine = variant % 2 ? 32760 : 3;
                    host.paragraph.dypExtraAfter = -7;
                }
                trace.Add(t); trace.Add(variant); trace.Add(width);
                long cp = 0, y = -17;
                for (unsigned lines = 0; cp < static_cast<long>(host.text.size()); ++lines)
                {
                    if (lines > host.text.size() + 1) throw std::logic_error("formatter did not progress");
                    KauaiTextLayout::Line line = {-99,-98,-97,-96,-95,-94};
                    KauaiTextLayout::CalculateLine(host, width, cp, y, &line);
                    trace.Line(line);
                    if (line.ccp <= 0) throw std::logic_error("empty formatter line");
                    cp += line.ccp; y += line.dyp;
                }
                typename Provider<Ch>::CharacterProperties style;
                KauaiTextLayout::CharacterRun<Provider<Ch>> run;
                run.Init(&style, &host.paragraph, &host, 0, static_cast<long>(host.text.size()), 0, width, width);
                for (unsigned n = 0; n < host.text.size() + 3; ++n)
                {
                    run.GetNextRun(true);
                    KauaiTextLayout::RunData data, bop;
                    run.GetChrd(&data, &bop);
                    trace.Add(run.CpMin()); trace.Add(run.XpMin()); trace.Add(run.XpBreak());
                    trace.Add(run.DypAscent()); trace.Add(run.DypDescent());
                    trace.Add(run.FBreak()); trace.Add(run.FObject());
                    trace.Add(data.cpLim); trace.Add(data.cpLimDraw); trace.Add(data.xpLim); trace.Add(data.xpLimDraw);
                    trace.Add(bop.cpLim); trace.Add(bop.cpLimDraw); trace.Add(bop.xpLim); trace.Add(bop.xpLimDraw);
                    for (long i = 0; i < data.cpLimDraw - run.CpMin(); ++i) trace.Add(run.Prgch()[i]);
                    if (run.CpMin() >= static_cast<long>(host.text.size())) break; // includes one EOF call
                }
            }
    // Inject exceptions at every callback of one representative line, including
    // the final paragraph query after some output fields have been published.
    int maximum = 0;
    for (int fail = 0; fail <= maximum || !fail; ++fail)
    {
        Provider<Ch> host(trace);
        host.text = {'A','V',' ',1,'\r','\n'}; host.mixed = true; host.failAt = fail;
        host.paragraph.nd = 1;
        KauaiTextLayout::Line line = {-99,-98,-97,-96,-95,-94};
        try { KauaiTextLayout::CalculateLine(host, 100, 0, 17, &line); trace.Add(0); }
        catch (const std::runtime_error &) { trace.Add(1); }
        if (!fail) maximum = host.calls;
        trace.Add(fail); trace.Line(line);
    }
}
inline void Run()
{
    Trace trace;
    Capture<unsigned char>(trace); Capture<unsigned short>(trace);
    // Frozen against the preconversion C++ formatter on all three hosts.
    if (trace.size != 112894528 || trace.fingerprint != 0xd3c462f6U)
        throw std::runtime_error("formatter differs from the original callback/run/line fixture");
    const char *path = std::getenv("MM_RICH_LAYOUT_SNAPSHOT");
    if (path && *path)
    {
        FILE *file = std::fopen(path, "wb");
        if (!file) throw std::runtime_error("cannot create formatter snapshot");
        bool ok = std::fwrite(trace.bytes.data(), 1, trace.bytes.size(), file) == trace.bytes.size();
        if (std::fclose(file) || !ok) throw std::runtime_error("cannot write formatter snapshot");
    }
    std::printf("Rich-text callback/run/line fixture: %zu bytes, FNV %08X\n",
                trace.size, static_cast<unsigned>(trace.fingerprint));
}
}
