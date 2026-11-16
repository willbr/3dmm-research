#ifndef MM_PREVIEW_MEDIA_HOST_H
#define MM_PREVIEW_MEDIA_HOST_H
// Typed rich-text/file/output services. Included after the display FontName helper.
struct CaptionHost
{
    struct Input
    {
        TextBoxData box;
        ChunkIdentification child;
        RichTextData text;
    };
    PChunkyFile file;
    PreviewCaption *output;
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
        s.create = [](void *v, void **out) -> int {
            auto &h = *static_cast<CaptionHost *>(v);
            return h.Guard([&] {
                *out = new Input;
                return 1;
            });
        };
        s.box = [](void *v, void *p, MMU32 number, MMPreviewCaptionData *out) -> int {
            auto &h = *static_cast<CaptionHost *>(v);
            return h.Guard([&] {
                auto &i = *static_cast<Input *>(p);
                if (!i.box.FRead(h.file, number, &i.child))
                    return false;
                out->first = i.box.state.first;
                out->end = i.box.state.end;
                out->left = i.box.state.left;
                out->top = i.box.state.top;
                out->right = i.box.state.right;
                out->bottom = i.box.state.bottom;
                out->story = i.box.state.story;
                return true;
            });
        };
        s.rich_text = [](void *v, void *p, MMPreviewCaptionData *out) -> int {
            auto &h = *static_cast<CaptionHost *>(v);
            return h.Guard([&] {
                auto &i = *static_cast<Input *>(p);
                if (!i.text.FRead(h.file, i.child.ctg, i.child.cno))
                    return false;
                out->bytes = i.text.text.cb;
                out->encoding = i.text.textOsk;
                out->font_size = i.text.defaults.dypFont;
                out->background = i.text.defaults.lwAcrBack;
                out->properties = i.text.properties->IvMac();
                out->property_swap = i.text.propertyBo == kboOther;
                out->has_arguments = i.text.arguments != nullptr;
                out->arguments = i.text.arguments ? i.text.arguments->IvMac() : 0;
                return true;
            });
        };
        s.release = [](void *, void *p) { delete static_cast<Input *>(p); };
        s.publish = [](void *v, const MMPreviewCaptionData *p) {
            auto &o = *static_cast<CaptionHost *>(v)->output;
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
        s.font = [](void *v, void *p) -> int {
            auto &h = *static_cast<CaptionHost *>(v);
            return h.Guard([&] {
                h.output->font = FontName(static_cast<Input *>(p)->text.fontName);
                return 1;
            });
        };
        s.read_text = [](void *v, void *p, void *out, MMU32 n) -> int {
            auto &h = *static_cast<CaptionHost *>(v);
            return h.Guard([&] { return static_cast<Input *>(p)->text.text.FReadRgb(out, n, 0); });
        };
        s.utf16 = [](void *v, const void *p, MMU32 n, int encoding, unsigned short *out, MMU32 capacity,
                     MMU32 *count) -> int {
            auto &h = *static_cast<CaptionHost *>(v);
            return h.Guard([&] {
                auto value = KauaiText::ToUtf16(p, n, static_cast<KauaiText::Encoding>(encoding), out, capacity);
                *count = static_cast<MMU32>(value);
                return 1;
            });
        };
        s.text_buffer = [](void *v, MMU32 n, unsigned short **out) -> int {
            auto &h = *static_cast<CaptionHost *>(v);
            return h.Guard([&] {
                h.output->text.resize(n);
                *out = h.output->text.data();
                return 1;
            });
        };
        s.property = [](void *, void *p, MMU32 i, MMU32 *out) {
            static_cast<Input *>(p)->text.properties->Get(i, out);
        };
        s.argument_info = [](void *, void *p, MMU32 i, MMI32 *bytes, int *free_entry) {
            auto g = static_cast<Input *>(p)->text.arguments;
            *free_entry = g->FFree(i);
            *bytes = *free_entry ? 0 : g->Cb(i);
        };
        s.argument = [](void *, void *p, MMU32 i, void *out) { static_cast<Input *>(p)->text.arguments->Get(i, out); };
        s.append = [](void *v, const MMPreviewTextProperty *p, const void *font, MMU32 n) -> int {
            auto &h = *static_cast<CaptionHost *>(v);
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
                h.output->properties.push_back(std::move(property));
                return true;
            });
        };
        s.position = [](void *v, MMU32 i) { return static_cast<CaptionHost *>(v)->output->properties[i].position; };
        s.swap = [](void *v, MMU32 a, MMU32 b) {
            auto &o = *static_cast<CaptionHost *>(v)->output;
            std::swap(o.properties[a], o.properties[b]);
        };
        return s;
    }
};
struct AudioHost
{
    PChunkyFile file;
    PreviewAudio *output;
    std::exception_ptr exception;
    DataBlock block;
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
    MMPreviewAudioServices Services()
    {
        static_assert(kctgMsnd == MM_SOUND_TYPE && kctgMidi == MM_SOUND_MIDI, "Preview sound types");
        MMPreviewAudioServices s = {};
        s.context = this;
        s.reset = [](void *v) { *static_cast<AudioHost *>(v)->output = {}; };
        s.silent = [](void *v) { static_cast<AudioHost *>(v)->output->silent = true; };
        s.find = [](void *v, MMU32 type, MMU32 number) -> int {
            auto &h = *static_cast<AudioHost *>(v);
            return h.Guard([&] { return h.file->FFind(type, number, &h.block); });
        };
        s.unpack = [](void *v) -> int {
            auto &h = *static_cast<AudioHost *>(v);
            return h.Guard([&] { return h.block.FUnpackData(); });
        };
        s.size = [](void *v) -> MMI32 { return static_cast<AudioHost *>(v)->block.Cb(); };
        s.read = [](void *v, void *out, MMU32 n) -> int {
            auto &h = *static_cast<AudioHost *>(v);
            return h.Guard([&] { return h.block.FReadRgb(out, n, 0); });
        };
        s.child = [](void *v, MMU32 parent, MMU32 *type, MMU32 *number) -> int {
            auto &h = *static_cast<AudioHost *>(v);
            ChildChunkIdentification child;
            if (!h.file->FGetKidChid(kctgMsnd, parent, 0, &child))
                return 0;
            *type = child.cki.ctg;
            *number = child.cki.cno;
            return 1;
        };
        s.publish = [](void *v, MMU32 type, MMU32 number, int midi) {
            auto &o = *static_cast<AudioHost *>(v)->output;
            o.asset = {type, number};
            o.midi = midi != 0;
        };
        s.wave_buffer = [](void *v, MMU32 n, unsigned char **out) -> int {
            auto &h = *static_cast<AudioHost *>(v);
            return h.Guard([&] {
                h.output->wave.resize(n);
                *out = h.output->wave.data();
                return 1;
            });
        };
        s.event = [](void *v, MMU32 timestamp, MMU32 message) -> int {
            auto &h = *static_cast<AudioHost *>(v);
            return h.Guard([&] {
                h.output->events.push_back({timestamp, message});
                return 1;
            });
        };
        return s;
    }
};
#endif
