#ifndef TEXT_MATERIALIZE_HOST_H
#define TEXT_MATERIALIZE_HOST_H
#include "mm/text_materialize.h"
// Native representations and virtual extension hooks for C materialization.
struct RichTextDocument::MaterializeHost : DocumentHost
{
    RichTextStoreHost::Host stored;
    MaterializeHost(RichTextDocument *p, const MMMovieArchiveEditor &editor, MMChunkOwnerRead *reader)
        : DocumentHost(p), stored{editor, reader}
    {
        stored.UseLiveCollections();
    }
    void Rethrow()
    {
        stored.Rethrow();
        DocumentHost::Rethrow();
    }
    MMTextMaterializeServices Read()
    {
        MMTextMaterializeServices s = {};
        s.context = this;
        s.read = stored.Read();
        s.base = Base();
        s.init_base = [](void *v) -> int {
            auto &h = *static_cast<MaterializeHost *>(v);
            return h.Guard([&] { return h.rich->TextDocumentBase::_FInit(); });
        };
        s.defaults = [](void *v, const MMRichTextDefaults *p, const MMMovieTitle *font) -> int {
            auto &h = *static_cast<MaterializeHost *>(v);
            return h.Guard([&] {
                h.rich->_dxpDef = p->width;
                h.rich->_dypFontDef = p->height;
                h.rich->_acrBack.SetFromLw(p->background);
                RichTextStoreHost::Font(*font, &h.rich->_stnFontDef);
                h.rich->_oskFont = static_cast<short>(p->font_system);
                h.rich->_onnDef = vntl.OnnMapStn(&h.rich->_stnFontDef, h.rich->_oskFont);
                return 1;
            });
        };
        s.properties = [](void *p) {
            return *mm_text_collection_property_array(static_cast<MMTextCollection *>(p), nullptr);
        };
        s.arguments = [](void *p) {
            auto rows = mm_text_collection_argument_group(static_cast<MMTextCollection *>(p));
            rows->storage->min_grow[1] = 16 * sizeof(MMGroupLocation);
            return *rows;
        };
        s.open_argument = [](void *v, MMI32 i, unsigned kind, int order, int system) -> int {
            auto &h = *static_cast<MaterializeHost *>(v);
            return h.Guard([&] {
                return h.rich->_FOpenArg(i, static_cast<byte>(kind), static_cast<short>(order),
                                         static_cast<short>(system));
            });
        };
        return s;
    }
};
#endif
