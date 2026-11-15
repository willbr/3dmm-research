#ifndef TEXT_BOX_STORE_HOST_H
#define TEXT_BOX_STORE_HOST_H
#include "mm/text_live.h"
// Native object/file primitives for the C text-box save transaction.
struct TextBoxStoreHost
{
    TextBox *box;
    TextBoxData *data;
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
    MMTextBoxStoreServices Services()
    {
        MMTextBoxStoreServices s = {};
        s.context = this;
        s.reset_child = [](void *v) { static_cast<TextBoxStoreHost *>(v)->data->textChild = 0; };
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
        s.adopt = [](void *v, MMU32 number, const MMTextChunk *p) -> int {
            auto &h = *static_cast<TextBoxStoreHost *>(v);
            return h.Guard([&] { return h.file->FAdoptChild(kctgTbox, number, p->type, p->number, 0); });
        };
        s.metadata = [](void *v, MMU32 number) -> int {
            auto &h = *static_cast<TextBoxStoreHost *>(v);
            return h.Guard([&] { return h.data->FWrite(h.file, number); });
        };
        s.remove = [](void *v, MMU32 number, const MMTextChunk *p, int linked) {
            auto file = static_cast<TextBoxStoreHost *>(v)->file;
            if (linked)
                file->DeleteChild(kctgTbox, number, p->type, p->number, 0);
            else
                file->Delete(p->type, p->number);
        };
        return s;
    }
};
#endif
