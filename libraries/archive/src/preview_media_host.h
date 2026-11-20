#ifndef MM_PREVIEW_MEDIA_HOST_H
#define MM_PREVIEW_MEDIA_HOST_H
// Typed rich-text/file/output services. Included after the display FontName helper.
struct CaptionOutput
{
    String &font;
    PreviewCaption &output;
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
    MMPreviewCaptionServices Services()
    {
        MMPreviewCaptionServices s = {};
        s.context = this;
        s.publish = [](void *v, const MMPreviewCaptionData *p) {
            auto &o = static_cast<CaptionOutput *>(v)->output;
            o.firstFrame = p->first;
            o.endFrame = p->end;
            o.left = p->left;
            o.top = p->top;
            o.right = p->right;
            o.bottom = p->bottom;
            o.story = p->story != 0;
            o.fontSize = p->font_size;
            o.background = p->background;
        };
        s.font = [](void *v, void *) -> int {
            auto &h = *static_cast<CaptionOutput *>(v);
            return h.Guard([&] {
                h.output.font = FontName(h.font);
                return 1;
            });
        };
        s.utf16 = [](void *v, const void *p, MMU32 n, int encoding, unsigned short *out, MMU32 capacity,
                     MMU32 *count) -> int {
            auto &h = *static_cast<CaptionOutput *>(v);
            return h.Guard([&] {
                auto value = KauaiText::ToUtf16(p, n, static_cast<KauaiText::Encoding>(encoding), out, capacity);
                *count = static_cast<MMU32>(value);
                return 1;
            });
        };
        s.text_buffer = [](void *v, MMU32 n, unsigned short **out) -> int {
            auto &h = *static_cast<CaptionOutput *>(v);
            return h.Guard([&] {
                h.output.text.resize(n);
                *out = h.output.text.data();
                return 1;
            });
        };
        s.append = [](void *v, const MMPreviewTextProperty *p, const void *font, MMU32 n) -> int {
            auto &h = *static_cast<CaptionOutput *>(v);
            return h.Guard([&] {
                PreviewTextProperty property;
                property.kind = p->kind;
                property.position = p->position;
                property.value = p->value;
                if (font)
                {
                    String name;
                    if (!name.FSetData(const_cast<void *>(font), n))
                        return false;
                    property.font = FontName(name);
                }
                h.output.properties.push_back(std::move(property));
                return true;
            });
        };
        s.position = [](void *v, MMU32 i) { return static_cast<CaptionOutput *>(v)->output.properties[i].position; };
        s.swap = [](void *v, MMU32 a, MMU32 b) {
            auto &o = static_cast<CaptionOutput *>(v)->output;
            std::swap(o.properties[a], o.properties[b]);
        };
        return s;
    }

    int LoadFont(const MMReadStream &stream, MMU32 offset)
    {
        return Guard([&] {
            unsigned char bytes[kcbMaxDataStn];
            const auto length = stream.length - offset;
            const bool read = length <= sizeof(bytes) && mm_read_exact(&stream, offset, bytes, length);
            font.FSetData(bytes, read ? static_cast<LegacyLong>(length) : 0);
            return 1;
        });
    }
};

#endif
