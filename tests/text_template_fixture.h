#ifndef TEXT_TEMPLATE_FIXTURE_H
#define TEXT_TEMPLATE_FIXTURE_H
#include "soc_core.h"

inline MMTemplateOwner *NewFixtureText(PString text, MMI32 shape, const TAG &tag)
{
    const MMStringValue name = {text->Pst(), sizeof(achar)};
    const MMTag font = TextFontTag(tag);
    return mm_brender_text_create(&name, shape, &font, &TagManager::TextFontProvider());
}

inline bool WriteFixtureText(MMTemplateOwner *text, const MMArchiveWriter &writer, ChunkNumber *number)
{
    return mm_text_write_owner(text, kctgTmpl, number, &writer) != 0;
}

#endif
