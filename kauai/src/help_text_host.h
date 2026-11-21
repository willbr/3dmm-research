#ifndef HELP_TEXT_HOST_H
#define HELP_TEXT_HOST_H
struct TextDocument::HelpHost : RichTextDocument::PropertyHost
{
    TextDocument *help;
    PChunkyFile file = nullptr;
    PStringRegistry strings = nullptr;
    PString groupName = nullptr;
    PCHP chp = nullptr;
    ulong flags = 0;
    ChunkIdentification *output = nullptr;
    DataBlock block;
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
        s.arguments = [](void *v) { return static_cast<HelpHost *>(v)->help->_pagcact->CoreRows(); };
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
        s.reserve = [](void *v, MMI32 n) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] { return h.help->_pglmpe->FEnsureSpace(n); });
        };
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
        s.lock = [](void *v, MMI32 i, MMI32 *n) -> const void * {
            LegacyLong bytes;
            auto data = static_cast<HelpHost *>(v)->help->_pagcact->PvLock(i, &bytes);
            *n = bytes;
            return data;
        };
        s.unlock = [](void *v) { static_cast<HelpHost *>(v)->help->_pagcact->Unlock(); };
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
            return h.Guard([&] { return static_cast<PChunkyFile>(f)->FForest(t, n); });
        };
        s.open_forest = [](void *v, void *f, MMU32 t, MMU32 n, void **out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                *out = static_cast<PChunkyFile>(f)->PcflReadForest(t, n, false);
                return *out != nullptr;
            });
        };
        s.first = [](void *v, void *f, MMU32 t, MMU32 *out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                ChunkIdentification id;
                if (!static_cast<PChunkyFile>(f)->FGetCkiCtg(t, 0, &id))
                    return false;
                *out = id.cno;
                return true;
            });
        };
        s.retain = [](void *, void *f) { static_cast<PChunkyFile>(f)->AddRef(); };
        s.release = [](void *, void *f) { static_cast<PChunkyFile>(f)->Release(); };
        s.header = [](void *v, void *f, MMU32 t, MMU32 n, void *out, MMI32 *bytes) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                if (!static_cast<PChunkyFile>(f)->FFind(t, n, &h.block) || !h.block.FUnpackData())
                    return false;
                *bytes = h.block.Cb();
                if (*bytes != 28 && *bytes != 36)
                    return true;
                return h.block.FRead(out);
            });
        };
        s.child = [](void *v, void *f, MMU32 t, MMU32 n, MMTextChunk *out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                ChildChunkIdentification kid;
                if (!static_cast<PChunkyFile>(f)->FGetKidChidCtg(t, n, 0, kctgRichText, &kid))
                    return false;
                *out = {kid.cki.ctg, kid.cki.cno};
                return true;
            });
        };
        s.rich = [](void *v, void *f, const MMTextChunk *p, int copy) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                return h.help->RichTextDocument::_FReadChunk(static_cast<PChunkyFile>(f), p->type, p->number,
                                                             copy != 0);
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
    MMHelpSaveServices Save()
    {
        MMHelpSaveServices s = {};
        s.context = this;
        s.publish_type = [](void *v, MMU32 t) { static_cast<HelpHost *>(v)->output->ctg = t; };
        s.add = [](void *v, MMU32 t, MMU32 *out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                if (!h.file->FAdd(36, t, &h.output->cno, &h.block))
                    return false;
                *out = h.output->cno;
                return true;
            });
        };
        s.write = [](void *v, const unsigned char *p) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] { return h.block.FWrite(const_cast<unsigned char *>(p)); });
        };
        s.rich = [](void *v, MMTextChunk *out) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                ChunkIdentification id;
                if (!h.help->RichTextDocument::FSaveToChunk(h.file, &id, h.flags != 0))
                    return false;
                *out = {id.ctg, id.cno};
                return true;
            });
        };
        s.adopt = [](void *v, const MMTextChunk *p, const MMTextChunk *c) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] { return h.file->FAdoptChild(p->type, p->number, c->type, c->number); });
        };
        s.erase = [](void *v, const MMTextChunk *p) -> int {
            auto &h = *static_cast<HelpHost *>(v);
            return h.Guard([&] {
                h.file->Delete(p->type, p->number);
                return 1;
            });
        };
        s.error = [](void *) { PushErc(ercHelpSaveFailed); };
        return s;
    }
};
#endif
