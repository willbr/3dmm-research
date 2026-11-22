#ifndef HELP_TEXT_HOST_H
#define HELP_TEXT_HOST_H
struct TextDocument::HelpHost : RichTextDocument::PropertyHost
{
    TextDocument *help;
    PStringRegistry strings = nullptr;
    PString groupName = nullptr;
    PCHP chp = nullptr;
    ulong flags = 0;
    String text;
    long *first = nullptr, *end = nullptr;
    byte *group = nullptr;
    ChunkNumber *target = nullptr;
    explicit HelpHost(TextDocument *p) : PropertyHost(p), help(p)
    {
    }
    static MMHelpTopic FromTopic(const Topic &p)
    {
        return {p.cnoBalloon, p.hidThis, p.hidTarget, p.cnoScript, p.dxp, p.dyp, p.ckiSnd.ctg, p.ckiSnd.cno};
    }
    static Topic ToTopic(const MMHelpTopic &p)
    {
        return {p.balloon, p.hid, p.target, p.script, p.width, p.height, {p.sound_type, p.sound_number}};
    }
    MMHelpArgumentServices Argument()
    {
        MMHelpArgumentServices s = {};
        s.context = this;
        s.base = [](void *v, MMI32 i, unsigned kind, int order, int system) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard(
                [&] { return h.help->RichTextDocument::_FOpenArg(i, byte(kind), short(order), short(system)); });
        };
        s.arguments = [](void *v) { return static_cast<HelpHost *>(v)->ArgumentRows(); };
        return s;
    }
    MMHelpEditServices Edit()
    {
        MMHelpEditServices s = {};
        s.context = this;
        s.properties = Properties();
        s.characters = [](void *v) -> MMI32 { return static_cast<HelpHost *>(v)->help->CpMac(); };
        s.undo = [](void *v, MMI32 a, MMI32 b, MMI32 n) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] { return h.help->FSetUndo(a, b, n); });
        };
        s.reserve = [](void *v, MMI32 n) { return static_cast<HelpHost *>(v)->ReserveProperties(n); };
        s.cancel = [](void *v) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                h.help->CancelUndo();
                return 1;
            });
        };
        s.commit = [](void *v) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                h.help->CommitUndo();
                return 1;
            });
        };
        s.group_name = [](void *v, void *out, MMI32 capacity, MMI32 *n) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                *n = 0;
                if (h.groupName && h.groupName->Cch() > 0)
                {
                    Assert(h.groupName->CbData() <= capacity, "group name too large");
                    h.groupName->GetData(out);
                    *n = h.groupName->CbData();
                }
                return 1;
            });
        };
        s.arguments = [](void *v) { return static_cast<HelpHost *>(v)->ArgumentRows(); };
        s.read_name = [](void *v, const void *p, MMI32 n) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] { return h.groupName->FSetData(const_cast<void *>(p), n); });
        };
        s.clear_name = [](void *v) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                h.groupName->SetNil();
                return 1;
            });
        };
        s.publish = [](void *v, int kind, MMU32 value) {
            auto &h = *static_cast<HelpHost *>(v);
            if (kind == MM_HELP_END && h.end)
                *h.end = mm_signed_word(value);
            else if (kind == MM_HELP_FIRST && h.first)
                *h.first = mm_signed_word(value);
            else if (kind == MM_HELP_GROUP && h.group)
                *h.group = byte(value);
            else if (kind == MM_HELP_TARGET && h.target)
                *h.target = value;
        };
        s.allocate = [](void *v, MMI32 n, void **out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] { return FAllocPv(out, n, fmemNil, mprNormal); });
        };
        s.free = [](void *, void *p) { FreePpv(&p); };
        s.insert = [](void *v, const void *p, MMI32 n, MMI32 at, MMI32 del) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] { return h.help->FInsertObject(const_cast<void *>(p), n, at, del, h.chp, h.flags); });
        };
        s.validate = [](void *v) { AssertPo(static_cast<HelpHost *>(v)->help, 0); };
        s.bad_group = [](void *) { Bug("bad group data"); };
        s.notify = [](void *v, MMI32 at, MMI32 n) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                h.help->InvalAllDdg(at, n, n);
                return 1;
            });
        };
        s.dirty = [](void *v) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                h.help->SetDirty();
                return 1;
            });
        };
        return s;
    }
    MMHelpReadServices Read()
    {
        MMHelpReadServices s = {};
        s.context = this;
        s.forest = [](void *v, void *f, MMU32 t, MMU32 n) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                const auto chunks = mm_resource_cache_chunks(static_cast<MMResourceCache *>(f));
                MMU32 position;
                MMChunkEntry entry;
                return mm_chunk_find(&chunks, t, n, &position) == MM_CHUNK_OK &&
                       mm_chunk_at(&chunks, position, &entry) == MM_CHUNK_OK && (entry.flags & MM_CHUNK_FOREST) != 0;
            });
        };
        s.open_forest = [](void *v, void *f, MMU32 t, MMU32 n, void **out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                *out = OpenForestResourceCache(static_cast<MMResourceCache *>(f), t, n);
                return *out != nullptr;
            });
        };
        s.first = [](void *v, void *f, MMU32 t, MMU32 *out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                const auto chunks = mm_resource_cache_chunks(static_cast<MMResourceCache *>(f));
                MMChunkEntry entry;
                MMU32 position;
                if (mm_chunk_type_at(&chunks, t, 0, &entry, &position) != MM_CHUNK_OK)
                    return false;
                *out = entry.number;
                return true;
            });
        };
        s.retain = [](void *, void *f) { mm_resource_cache_retain(static_cast<MMResourceCache *>(f)); };
        s.release = [](void *, void *f) { mm_resource_cache_release(static_cast<MMResourceCache *>(f)); };
        s.header = [](void *v, void *f, MMU32 t, MMU32 n, void *out, MMI32 *bytes) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                struct Header
                {
                    void *output;
                    MMI32 *bytes;
                } header = {out, bytes};
                int read;
                return mm_resource_cache_visit_payload(static_cast<MMResourceCache *>(f), t, n,
                    KauaiDecodeBlockHandle, nullptr, [](void *context, const MMReadStream *stream) -> int {
                        auto header = static_cast<Header *>(context);
                        *header->bytes = stream->length;
                        return (stream->length != 28 && stream->length != 36) ||
                               mm_read_exact(stream, 0, header->output, stream->length);
                    }, &header, &read) == MM_CHUNK_OK && read;
            });
        };
        s.child = [](void *v, void *f, MMU32 t, MMU32 n, MMTextChunk *out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                const auto chunks = mm_resource_cache_chunks(static_cast<MMResourceCache *>(f));
                MMU32 number;
                if (mm_chunk_link_number(&chunks, t, n, 0, kctgRichText, &number) != MM_CHUNK_OK)
                    return false;
                *out = {kctgRichText, number};
                return true;
            });
        };
        s.rich = [](void *v, void *f, const MMTextChunk *p, int copy) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                const auto cache = static_cast<MMResourceCache *>(f);
                const auto reader = mm_resource_cache_reader(cache);
                if (!reader)
                    return false;
                MovieArchiveHost archive(reader, mm_resource_cache_writer(cache));
                return h.help->_FReadArchive(archive.Editor(), reader, p->type, p->number, copy != 0);
            });
        };
        s.internal = [](void *v) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                h.help->SetInternal();
                return 1;
            });
        };
        s.suspend = [](void *v) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                h.help->SuspendUndo();
                return 1;
            });
        };
        s.characters = [](void *v) -> MMI32 { return static_cast<HelpHost *>(v)->help->CpMac(); };
        s.character = [](void *v, MMI32 cp, MMU32 *out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                *out = h.help->_ChFetch(cp);
                return 1;
            });
        };
        s.string = [](void *v, MMI32 id, MMMovieTitle *out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                bool ok = h.strings->FGet(id, &h.text);
                *out = RichTextStoreHost::Font(h.text);
                return ok;
            });
        };
        s.replace = [](void *v, const MMMovieTitle *p, MMI32 at, MMI32 del) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                return h.help->FReplaceRgch(const_cast<void *>(mm_movie_title_text(p)), p->count, at, del, fdocNil);
            });
        };
        s.missing = [](void *) { Warn("string missing"); };
        s.validate = [](void *v) { AssertPo(static_cast<HelpHost *>(v)->help, 0); };
        return s;
    }
};
#endif
