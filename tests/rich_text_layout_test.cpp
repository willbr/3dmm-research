#include "rich_text_layout.h"
#include <algorithm>
#include <cstdio>
#include <string>

struct Host
{
    using Character = std::uint16_t;
    struct CharacterProperties
    {
        long offset = 0;
        long ascent = 13, descent = 3;
    };
    struct ParagraphProperties
    {
        int jc = 0, nd = 0, dxpTab = 18, numLine = 256, numAfter = 256;
        int dypExtraLine = 0, dypExtraAfter = 0;
    } paragraph;
    struct Rectangle
    {
        long xpLeft = 0, xpRight = 0, ypTop = -13, ypBottom = 3;
        long Dxp() const
        {
            return xpRight - xpLeft;
        }
        void Offset(long x, long y)
        {
            xpLeft += x;
            xpRight += x;
            ypTop += y;
            ypBottom += y;
        }
    };
    std::u16string text;
    long largeAt = -1;
    mutable CharacterProperties font;
    long CpMac() const
    {
        return static_cast<long>(text.size());
    }
    unsigned long Flags(Character ch) const
    {
        return KauaiTextLayout::Flags(ch);
    }
    bool FMinPara(long cp) const
    {
        if (!cp)
            return true;
        if (cp >= CpMac() || text[cp] == '\n')
            return false;
        while (cp-- > 0)
        {
            if (text[cp] == '\r')
                return true;
            if (text[cp] != '\n')
                return false;
        }
        return true;
    }
    void FetchRgch(long cp, long count, Character *out) const
    {
        std::copy_n(text.data() + cp, count, out);
    }
    void FetchChp(long cp, CharacterProperties *out, long *limit) const
    {
        *out = {};
        *limit = CpMac();
        if (largeAt >= 0)
        {
            if (cp < largeAt)
                *limit = largeAt;
            else
            {
                out->ascent = 24;
                out->descent = 5;
            }
        }
    }
    void FetchPap(long, ParagraphProperties *out, long *limit) const
    {
        *out = paragraph;
        *limit = CpMac();
    }
    void SetFont(const CharacterProperties &value) const
    {
        font = value;
    }
    void WarmFont() const
    {
    }
    long Offset(const CharacterProperties &chp) const
    {
        return chp.offset;
    }
    void GetRcFromRgch(Rectangle *rect, Character *, long count) const
    {
        *rect = {};
        rect->xpRight = count * 6;
        rect->ypTop = -font.ascent;
        rect->ypBottom = font.descent;
    }
    bool FGetObjectRc(long, CharacterProperties *, Rectangle *) const
    {
        return false;
    }
};

int main()
{
    Host host;
    KauaiTextLayout::Line line;
    host.text = u"\r\n\r\nA\r";
    KauaiTextLayout::CalculateLine(host, 100, 0, 0, &line);
    if (line.ccp != 2 || line.dyp != 16 || line.dypAscent != 13)
        return 1;
    KauaiTextLayout::CalculateLine(host, 100, 2, 16, &line);
    if (line.ccp != 2 || line.dyp != 16 || line.dypTot != 16)
        return 2;
    KauaiTextLayout::CalculateLine(host, 100, 4, 32, &line);
    if (line.ccp != 2 || line.dyp != 16 || line.dypTot != 32)
        return 3;

    host.text = u"\tA\r";
    host.paragraph.jc = 2;
    KauaiTextLayout::CalculateLine(host, 100, 0, 0, &line);
    if (line.xpLeft != 38 || line.ccp != 3)
        return 4;
    host.paragraph.numLine = 128;
    host.paragraph.dypExtraLine = 3;
    host.paragraph.numAfter = 512;
    host.paragraph.dypExtraAfter = 1;
    KauaiTextLayout::CalculateLine(host, 100, 0, 0, &line);
    if (line.dyp != 23 || line.dypAscent != 13)
        return 5;

    host.paragraph = {};
    host.text = u"AA BB\r";
    KauaiTextLayout::CalculateLine(host, 18, 0, 0, &line);
    if (line.ccp != 3 || line.dyp != 16)
        return 6;
    KauaiTextLayout::CalculateLine(host, 18, 3, 16, &line);
    if (line.ccp != 3 || line.dyp != 16)
        return 7;

    // An EOP's style participates in the baseline even though it has no ink.
    host.text = u"A\r";
    host.largeAt = 1;
    KauaiTextLayout::CalculateLine(host, 100, 0, 0, &line);
    if (line.ccp != 2 || line.dyp != 29 || line.dypAscent != 24)
        return 10;
    host.largeAt = -1;

    host.text = u"\t" + std::u16string(200, u'\u03a9') + u"\r";
    Host::CharacterProperties style;
    KauaiTextLayout::CharacterRun<Host> run;
    run.Init(&style, &host.paragraph, &host, 0, host.CpMac(), 0, 10000, 10000);
    run.GetNextRun(true);
    run.GetNextRun(true);
    KauaiTextLayout::RunData position;
    run.GetChrd(&position);
    if (run.CpMin() != 1 || position.cpLimDraw != 129)
        return 8;
    for (int i = 0; i < 128; ++i)
        if (run.Prgch()[i] != 0x03a9)
            return 9;
    std::puts("Shared rich-text blank lines, CRLF, tabs, wrapping, paragraph spacing and UTF-16 buffer checks passed");
}
