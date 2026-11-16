/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_RICH_TEXT_LAYOUT_H
#define KAUAI_RICH_TEXT_LAYOUT_H
/* Typed document/font bindings for the shared C89 formatter. */
#include "mm/rich_text_layout.h"
#include <cassert>
#include <exception>
#include <stdexcept>
namespace KauaiTextLayout
{
enum { fchWhiteOverhang = MM_RICH_WHITE, fchBreak = MM_RICH_BREAK,
       fchMayBreak = MM_RICH_MAY_BREAK, fchIgnore = MM_RICH_IGNORE,
       fchTab = MM_RICH_TAB, kchObject = 1 };
enum { ndFirst = 1, ndRest = 2, ndAll = 3 };
const long kcchMaxChr = MM_RICH_BUFFER;
inline unsigned long Flags(unsigned long ch) { return mm_rich_text_flags(static_cast<MMU32>(ch)); }
struct Line
{
    long cpMin, dypTot;
    short ccp, xpLeft, dyp, dypAscent;
};
struct RunData { long cpLim, cpLimDraw, xpLim, xpLimDraw; };

template<class Host> struct Binding
{
    using Character = typename Host::Character;
    using CHP = typename Host::CharacterProperties;
    using PAP = typename Host::ParagraphProperties;
    using RC = typename Host::Rectangle;
    Host *host = nullptr, *initializingHost = nullptr;
    CHP fetched, style;
    const CHP *source = &fetched;
    Character characters[MM_RICH_BUFFER];
    Line *line = nullptr;
    std::exception_ptr error;
    static MMRichTextParagraph Paragraph(const PAP &pap)
    {
        MMRichTextParagraph value = {static_cast<MMI32>(pap.jc), static_cast<MMI32>(pap.nd),
            static_cast<MMI32>(pap.dxpTab), static_cast<MMI32>(pap.numLine),
            static_cast<MMI32>(pap.numAfter), static_cast<MMI32>(pap.dypExtraLine),
            static_cast<MMI32>(pap.dypExtraAfter)};
        return value;
    }
    MMRichTextProvider Provider()
    {
        static_assert(sizeof(Character) == 1 || sizeof(Character) == 2, "formatter requires 1/2-byte characters");
        MMRichTextProvider value = {this, Call, characters, sizeof(Character)};
        return value;
    }
    void Check(int status)
    {
        if (error) std::rethrow_exception(error);
        if (status < 0) throw std::runtime_error("invalid rich-text formatter provider");
    }
    static int Call(void *context, MMRichTextOperation op, MMRichTextRequest *q)
    {
        Binding &b = *static_cast<Binding *>(context);
        try
        {
            RC rc;
            switch (op)
            {
            case MM_RICH_COPY_STYLE:
                b.style = *b.source;
                if (b.initializingHost) b.host = b.initializingHost;
                break;
            case MM_RICH_SET_FONT: b.host->SetFont(b.style); break;
            case MM_RICH_WARM_FONT: b.host->WarmFont(); break;
            case MM_RICH_MEASURE:
                b.host->GetRcFromRgch(&rc, static_cast<Character *>(q->characters), q->count);
                q->left = static_cast<MMI32>(rc.xpLeft); q->right = static_cast<MMI32>(rc.xpRight);
                q->top = static_cast<MMI32>(rc.ypTop); q->bottom = static_cast<MMI32>(rc.ypBottom);
                break;
            case MM_RICH_OFFSET: q->value = static_cast<MMI32>(b.host->Offset(b.style)); break;
            case MM_RICH_FLAGS: q->flags = static_cast<MMU32>(b.host->Flags(static_cast<Character>(q->character))); break;
            case MM_RICH_LENGTH: q->value = static_cast<MMI32>(b.host->CpMac()); break;
            case MM_RICH_PARAGRAPH_START: q->value = b.host->FMinPara(q->cp); break;
            case MM_RICH_FETCH:
                b.host->FetchRgch(q->cp, q->count, static_cast<Character *>(q->characters)); break;
            case MM_RICH_OBJECT:
                q->value = b.host->FGetObjectRc(q->cp, &b.style, &rc);
                if (q->value)
                {
                    q->left = static_cast<MMI32>(rc.xpLeft); q->right = static_cast<MMI32>(rc.xpRight);
                    q->top = static_cast<MMI32>(rc.ypTop); q->bottom = static_cast<MMI32>(rc.ypBottom);
                }
                break;
            default: return -1;
            }
            return 0;
        }
        catch (...) { b.error = std::current_exception(); return -2; }
    }
};
template<class Host> struct LineBinding : Binding<Host>
{
    static int Call(void *context, MMRichTextOperation op, MMRichTextRequest *q)
    {
        LineBinding &b = *static_cast<LineBinding *>(context);
        try
        {
            long limit;
            switch (op)
            {
            case MM_RICH_FETCH_STYLE:
                b.host->FetchChp(q->cp, &b.fetched, &limit);
                q->value = static_cast<MMI32>(limit); break;
            case MM_RICH_FETCH_PARAGRAPH:
            {
                typename Host::ParagraphProperties pap;
                b.host->FetchPap(q->cp, &pap, &limit);
                q->paragraph = Binding<Host>::Paragraph(pap); q->value = static_cast<MMI32>(limit); break;
            }
            case MM_RICH_PUBLISH:
                if (q->published & 1)
                {
                    b.line->xpLeft = static_cast<short>(q->line.xpLeft);
                    b.line->ccp = static_cast<short>(q->line.ccp);
                    b.line->dyp = static_cast<short>(q->line.dyp);
                }
                if (q->published & 2)
                {
                    b.line->dypAscent = static_cast<short>(q->line.dypAscent);
                    b.line->cpMin = q->line.cpMin; b.line->dypTot = q->line.dypTot;
                }
                break;
            case MM_RICH_INVALID_STYLE: assert(false); break;
            default: return Binding<Host>::Call(&b, op, q);
            }
            return 0;
        }
        catch (...) { b.error = std::current_exception(); return -2; }
    }
};
template<class Host> class CharacterRun
{
    Binding<Host> _binding;
    MMRichTextRun _run = {};
    static void Copy(RunData *out, const MMRichTextRunData &value)
    {
        if (out) { out->cpLim = value.cpLim; out->cpLimDraw = value.cpLimDraw;
            out->xpLim = value.xpLim; out->xpLimDraw = value.xpLimDraw; }
    }
  public:
    using Character = typename Host::Character;
    using CHP = typename Host::CharacterProperties;
    using PAP = typename Host::ParagraphProperties;
    using RC = typename Host::Rectangle;
    void Init(CHP *chp, PAP *pap, Host *host, long cpMin, long cpLim,
              long xpBase, long width, long xpBreak)
    {
        _binding.initializingHost = host; _binding.source = chp; _binding.error = nullptr;
        MMRichTextProvider provider = _binding.Provider();
        MMRichTextParagraph paragraph = _binding.Paragraph(*pap);
        _binding.Check(mm_rich_text_run_init(&_run, &provider, &paragraph,
            static_cast<MMI32>(cpMin), static_cast<MMI32>(cpLim), static_cast<MMI32>(xpBase),
            static_cast<MMI32>(width), static_cast<MMI32>(xpBreak)));
    }
    void GetNextRun(bool mustAdvance = false)
    {
        _binding.error = nullptr;
        _run.provider = _binding.Provider();
        _binding.Check(mm_rich_text_run_next(&_run, mustAdvance));
    }
    bool FBreak() { return _run.lineBreak != 0; }
    bool FObject() { return _run.object != 0; }
    long DypAscent() { return _run.ascent; }
    long DypDescent() { return _run.descent; }
    long XpMin() { return _run.xpMin; }
    long XpBreak() { return _run.xpBreak; }
    long CpMin() { return _run.cpMin; }
    Character *Prgch() { return _binding.characters; }
    void GetChrd(RunData *data, RunData *opportunity = nullptr)
    { Copy(data, _run.data); Copy(opportunity, _run.opportunity); }
};
template<class Host> void CalculateLine(Host &host, long width, long cpMin, long y, Line *line)
{
    LineBinding<Host> binding;
    binding.host = &host; binding.line = line;
    MMRichTextProvider provider = binding.Provider();
    provider.call = LineBinding<Host>::Call;
    binding.Check(mm_rich_text_line(&provider, static_cast<MMI32>(width),
                                   static_cast<MMI32>(cpMin), static_cast<MMI32>(y)));
}
} // namespace KauaiTextLayout
#endif
