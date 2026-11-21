#ifndef TEXT_BOX_STORE_HOST_H
#define TEXT_BOX_STORE_HOST_H
#include "mm/text_live.h"
// Native object/file primitives for the C text-box save transaction.
struct TextBoxStoreHost
{
    TextBox *box;
    PChunkyFile file;
    std::exception_ptr exception;
    template <class F> int Guard(F f)
    {
        try
        {
            return f();
        }
        catch (const std::bad_alloc &)
        {
            exception = std::current_exception();
            return -1;
        }
        catch (...)
        {
            exception = std::current_exception();
            return -2;
        }
    }
    MMTextBoxStoreServices Services(MMTextBoxState *state, const MMMovieArchiveEditor *archive)
    {
        MMTextBoxStoreServices s = {};
        s.context = this;
        s.state = state;
        s.archive = archive;
        s.save = [](void *v, MMTextChunk *out) -> int {
            auto &h = *static_cast<TextBoxStoreHost *>(v);
            return h.Guard([&] {
                ChunkIdentification child;
                if (!h.box->FSaveToChunk(h.file, &child))
                    return false;
                *out = {child.ctg, child.cno};
                return true;
            });
        };
        s.assert_type = [](void *) { Assert(false, "bad rich-text type"); };
        return s;
    }
};
#endif
