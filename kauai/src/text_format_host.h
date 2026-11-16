#ifndef TEXT_FORMAT_HOST_H
#define TEXT_FORMAT_HOST_H
#include "mm/text_format.h"
// Native value representations, font table and virtual edit effects for C policy.
struct RichTextDocument::FormatHost : PropertyHost
{
    long *first = nullptr, *end = nullptr;
    CHP *chp = nullptr, *oldChp = nullptr;
    PAP *pap = nullptr, *oldPap = nullptr;
    explicit FormatHost(RichTextDocument *p) : PropertyHost(p)
    {
    }
    static MMTextCharacter Character(const CHP &p)
    {
        return {p.grfont,
                p.onn,
                static_cast<MMI32>(p.dypFont),
                static_cast<MMI32>(p.dypOffset),
                static_cast<MMU32>(p.acrFore.LwGet()),
                static_cast<MMU32>(p.acrBack.LwGet())};
    }
    static CHP Character(const MMTextCharacter &p)
    {
        CHP value;
        value.grfont = p.style;
        value.onn = p.font;
        value.dypFont = p.height;
        value.dypOffset = p.offset;
        value.acrFore.SetFromLw(p.foreground);
        value.acrBack.SetFromLw(p.background);
        return value;
    }
    static MMTextParagraph Paragraph(const PAP &p)
    {
        return {p.jc, p.nd, p.dxpTab, p.numLine, p.dypExtraLine, p.numAfter, p.dypExtraAfter};
    }
    static PAP Paragraph(const MMTextParagraph &p)
    {
        return {static_cast<byte>(p.justification), static_cast<byte>(p.indent),      static_cast<short>(p.tab),
                static_cast<short>(p.line),         static_cast<short>(p.extra_line), static_cast<short>(p.after),
                static_cast<short>(p.extra_after)};
    }
    MMTextFormatConfig Configuration()
    {
        return {Config(),
                static_cast<MMI32>(rich->_onnDef),
                static_cast<MMI32>(rich->_dypFontDef),
                static_cast<MMU32>(kacrBlack.LwGet()),
                static_cast<MMU32>(kacrClear.LwGet()),
                kdxpTabDef,
                kdenLine,
                kdenAfter,
                static_cast<achar>(0x80) < 0};
    }
    MMTextCharacterCache CharacterCache()
    {
        MMTextCharacterCache c = {};
        c.first = rich->_cpMinChp;
        c.end = rich->_cpLimChp;
        if (c.first < c.end)
            c.value = Character(rich->_chp);
        return c;
    }
    MMTextParagraphCache ParagraphCache()
    {
        MMTextParagraphCache c = {};
        c.first = rich->_cpMinPap;
        c.end = rich->_cpLimPap;
        if (c.first < c.end)
            c.value = Paragraph(rich->_pap);
        return c;
    }
    void Publish(const MMTextCharacterCache &c)
    {
        rich->_chp = Character(c.value);
        rich->_cpMinChp = c.first;
        rich->_cpLimChp = c.end;
    }
    void Publish(const MMTextParagraphCache &c)
    {
        rich->_pap = Paragraph(c.value);
        rich->_cpMinPap = c.first;
        rich->_cpLimPap = c.end;
    }
    MMTextFormatServices Values()
    {
        MMTextFormatServices s = {};
        s.context = this;
        s.properties = Properties();
        s.font_name = [](void *v, MMI32 font, MMMovieTitle *out) -> int {
            auto &h = *static_cast<FormatHost *>(v);
            return h.Guard([&] {
                String name;
                vntl.GetStn(font, &name);
                *out = RichTextStoreHost::Font(name);
                return 1;
            });
        };
        s.valid_font = [](void *v, MMI32 font) -> int {
            auto &h = *static_cast<FormatHost *>(v);
            return h.Guard([&] {
                Assert(vntl.FValidOnn(font), "invalid onn");
                return 1;
            });
        };
        s.bad_font_record = [](void *) { Warn("bad group element"); };
        return s;
    }
    MMTextParagraphServices Paragraphs()
    {
        return {this, [](void *v, int kind, MMI32 cp, MMI32 *out) -> int {
                    auto &h = *static_cast<FormatHost *>(v);
                    return h.Guard([&] {
                        if (kind == MM_TEXT_PARA_START)
                            *out = h.rich->CpMinPara(cp);
                        else if (kind == MM_TEXT_PARA_END)
                            *out = h.rich->CpLimPara(cp);
                        else if (kind == MM_TEXT_PARA_IS_START)
                            *out = h.rich->FMinPara(cp);
                        else
                            *out = h.rich->CpNext(cp);
                        return 1;
                    });
                }};
    }
    MMTextFormatEditServices Edit()
    {
        MMTextFormatEditServices s = {};
        s.context = this;
        s.format = Values();
        s.paragraphs = Paragraphs();
        s.read_input = [](void *v, int paragraph, MMTextFormatConfig *config, void *value, void *old, int *hasOld) {
            auto &h = *static_cast<FormatHost *>(v);
            *config = h.Configuration();
            if (paragraph)
            {
                AssertVarMem(h.pap);
                *static_cast<MMTextParagraph *>(value) = Paragraph(*h.pap);
                *hasOld = h.oldPap != nullptr;
                if (h.oldPap)
                    *static_cast<MMTextParagraph *>(old) = Paragraph(*h.oldPap);
            }
            else
            {
                AssertVarMem(h.chp);
                *static_cast<MMTextCharacter *>(value) = Character(*h.chp);
                *hasOld = h.oldChp != nullptr;
                if (h.oldChp)
                    *static_cast<MMTextCharacter *>(old) = Character(*h.oldChp);
            }
        };
        s.publish_range = [](void *v, MMI32 first, MMI32 end) {
            auto &h = *static_cast<FormatHost *>(v);
            if (h.first)
                *h.first = first;
            if (h.end)
                *h.end = end;
        };
        s.bump = [](void *v) { static_cast<FormatHost *>(v)->rich->BumpCombineUndo(); };
        s.characters = [](void *v) -> MMI32 { return static_cast<FormatHost *>(v)->rich->CpMac(); };
        s.validate = [](void *v) { AssertPo(static_cast<FormatHost *>(v)->rich, fobjAssertFull); };
        s.undo = [](void *v, MMI32 first, MMI32 end, MMI32 inserted) -> int {
            auto &h = *static_cast<FormatHost *>(v);
            return h.Guard([&] { return h.rich->FSetUndo(first, end, inserted); });
        };
        s.cancel = [](void *v) -> int {
            auto &h = *static_cast<FormatHost *>(v);
            return h.Guard([&] {
                h.rich->CancelUndo();
                return 1;
            });
        };
        s.commit = [](void *v) -> int {
            auto &h = *static_cast<FormatHost *>(v);
            return h.Guard([&] {
                h.rich->CommitUndo();
                return 1;
            });
        };
        s.reserve = [](void *v, MMI32 count) -> int {
            auto &h = *static_cast<FormatHost *>(v);
            return h.Guard([&] { return h.rich->_pglmpe->FEnsureSpace(count); });
        };
        s.invalidate = [](void *v, MMI32 cp, MMI32 span, MMU32 flags) -> int {
            auto &h = *static_cast<FormatHost *>(v);
            return h.Guard([&] {
                h.rich->InvalAllDdg(cp, span, span, flags);
                return 1;
            });
        };
        return s;
    }
};
#endif
