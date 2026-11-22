#ifndef HELP_DISPLAY_HOST_H
#define HELP_DISPLAY_HOST_H
struct TextDocument::DisplayHost : HelpHost
{
    PGraphicsEnvironment graphics;
    PCHP character;
    long *cursor = nullptr;
    RC *bounds = nullptr;
    DisplayHost(TextDocument *p, PGraphicsEnvironment g, PCHP c) : HelpHost(p), graphics(g), character(c)
    {
    }
    static MMMaskedRect Rectangle(const RC &p)
    {
        return {p.xpLeft, p.ypTop, p.xpRight, p.ypBottom};
    }
    static RC Rectangle(const MMMaskedRect &p)
    {
        return {p.left, p.top, p.right, p.bottom};
    }
    MMHelpDisplayServices Display()
    {
        MMHelpDisplayServices s = {};
        s.context = this;
        s.arguments = [](void *v) { return static_cast<DisplayHost *>(v)->ArgumentRows(); };
        s.find = [](void *v, const MMTextChunk *p, void **out) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            return h.Guard([&] {
                const auto file = h.help->_prca->PcrfFindChunk(p->type, p->number);
                *out = file ? file->CoreCache() : nullptr;
                return file != nullptr;
            });
        };
        s.child = [](void *v, void *cache, const MMTextChunk *p, MMU32 id, MMTextChunk *out) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            return h.Guard([&] {
                const auto chunks = mm_resource_cache_chunks(static_cast<MMResourceCache *>(cache));
                MMU32 bitmap;
                if (mm_chunk_link_number(&chunks, p->type, p->number, id, kctgMbmp, &bitmap) != MM_CHUNK_OK)
                    return false;
                *out = {kctgMbmp, bitmap};
                return true;
            });
        };
        s.fetch = [](void *v, void *cache, const MMTextChunk *p, void **out) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            return h.Guard([&] {
                *out = cache ? FetchMaskedBitmap(static_cast<MMResourceCache *>(cache), p->type, p->number)
                             : FetchMaskedBitmap(h.help->_prca, p->type, p->number);
                return *out != nullptr;
            });
        };
        s.rectangle = [](void *v, void *bitmap, MMMaskedRect *out) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            RC rc;
            auto target = h.bounds ? h.bounds : &rc;
            GetMaskedBitmapBounds(static_cast<PMaskedBitmapMBMP>(bitmap), target);
            *out = Rectangle(*target);
            return 1;
        };
        s.release = [](void *, void *bitmap) { mm_masked_owner_release(static_cast<PMaskedBitmapMBMP>(bitmap)); };
        s.font = [](void *v) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            return h.Guard([&] {
                h.graphics->SetFont(h.character->onn, h.character->grfont, h.character->dypFont, tahLeft, tavBaseline);
                return 1;
            });
        };
        s.measure = [](void *v, MMI32 baseline, MMMaskedRect *out) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            return h.Guard([&] {
                RC rc;
                auto target = h.bounds ? h.bounds : &rc;
                h.graphics->GetRcFromRgch(target, nullptr, 0, 0, baseline);
                *out = Rectangle(*target);
                return 1;
            });
        };
        s.color = [](void *v, int foreground) -> MMU32 {
            auto c = static_cast<DisplayHost *>(v)->character;
            return foreground ? c->acrFore.LwGet() : c->acrBack.LwGet();
        };
        s.style = [](void *v) -> MMU32 { return static_cast<DisplayHost *>(v)->character->grfont; };
        s.x = [](void *v) -> MMI32 { return *static_cast<DisplayHost *>(v)->cursor; };
        s.publish_x = [](void *v, MMI32 x) { *static_cast<DisplayHost *>(v)->cursor = x; };
        s.fill = [](void *v, const MMMaskedRect *p, MMU32 color) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            return h.Guard([&] {
                auto rc = Rectangle(*p);
                AbstractColor c;
                c.SetFromLw(mm_signed_word(color));
                h.graphics->FillRc(&rc, c);
                return 1;
            });
        };
        s.draw = [](void *v, void *bitmap, const MMMaskedRect *p) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            return h.Guard([&] {
                auto rc = Rectangle(*p);
                h.graphics->DrawMbmp(static_cast<PMaskedBitmapMBMP>(bitmap), &rc);
                return 1;
            });
        };
        s.pen = [](void *v) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            return h.Guard([&] {
                h.graphics->SetPenSize(1, 1);
                return 1;
            });
        };
        s.frame = [](void *v, const MMMaskedRect *p, int patterned, MMU32 color) -> int {
            auto &h = *static_cast<DisplayHost *>(v);
            return h.Guard([&] {
                auto rc = Rectangle(*p);
                AbstractColor c;
                c.SetFromLw(mm_signed_word(color));
                if (patterned)
                    h.graphics->FrameRcApt(&rc, &vaptGray, c, kacrClear);
                else
                    h.graphics->FrameRc(&rc, c);
                return 1;
            });
        };
        return s;
    }
};
#endif
