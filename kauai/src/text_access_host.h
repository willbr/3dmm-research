#ifndef TEXT_ACCESS_HOST_H
#define TEXT_ACCESS_HOST_H
#include "mm/text_access.h"
struct TextDocumentBase::AccessHost : DocumentHost
{
    explicit AccessHost(TextDocumentBase *p) : DocumentHost(p)
    {
    }
    MMTextAccessServices Services()
    {
        static_assert(fchBreak == MM_TEXT_CHAR_BREAK && fchMayBreak == MM_TEXT_CHAR_MAY_BREAK &&
                          fchIgnore == MM_TEXT_CHAR_IGNORE,
                      "text character flags");
        MMTextAccessServices s = {};
        s.context = this;
        s.state = &doc->_textCache;
        s.cache = [](void *v, MMI32 first, MMI32 end) -> int {
            auto &h = *static_cast<AccessHost *>(v);
            return h.Guard([&] {
                h.doc->_CacheRange(first, end);
                return 1;
            });
        };
        s.character = [](void *v, MMI32 cp, MMU32 *out) -> int {
            auto &h = *static_cast<AccessHost *>(v);
            return h.Guard([&] {
                *out = static_cast<uchar>(h.doc->_ChFetch(cp));
                return 1;
            });
        };
        s.classify = [](void *, MMU32 ch) -> unsigned { return GrfchFromCh(static_cast<achar>(ch)); };
        s.lower = [](void *v, MMU32 ch, MMU32 *out) -> int {
            auto &h = *static_cast<AccessHost *>(v);
            return h.Guard([&] {
                *out = static_cast<uchar>(ChLower(static_cast<achar>(ch)));
                return 1;
            });
        };
        s.lower_pattern = [](void *v, void *p, MMI32 count) -> int {
            auto &h = *static_cast<AccessHost *>(v);
            return h.Guard([&] {
                LowerRgch(static_cast<achar *>(p), count);
                return 1;
            });
        };
        s.diagnostic = [](void *, int kind) {
            if (kind == MM_TEXT_ISOLATED_LF)
            {
                Warn("isolated line feeds");
            }
            else
            {
                Assert(false, "last character is not a return character");
            }
        };
        return s;
    }
};
#endif
