/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "rich_text_data.h"
ASSERTNAME

RichTextData::~RichTextData()
{
    Clear();
}
void RichTextData::Clear()
{
    ReleasePpo(&text.pfil);
    ReleasePpo(&properties);
    ReleasePpo(&arguments);
}

bool RichTextData::FRead(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    RichTextData loaded;
    DataBlock block;
    ChildChunkIdentification child;
    if (!file->FFind(type, number, &block) || !block.FUnpackData() || block.Cb() < size(RichTextProperties) ||
        !block.FReadRgb(&loaded.defaults, size(RichTextProperties), 0))
        return fFalse;
    if (loaded.defaults.bo == kboOther)
        SwapBytesBom(&loaded.defaults, kbomRdop);
    if (loaded.defaults.bo != kboCur || !FIn(loaded.defaults.dypFont, 4, 256) ||
        !FIn(loaded.defaults.dxpDef, 1, kcbMax))
        return fFalse;
    if (!loaded.fontName.FRead(&block, size(RichTextProperties)) || loaded.fontName.Cch() == 0)
    {
        loaded.defaults.oskFont = koskCur;
        loaded.fontName.SetNil();
    }
    FileLocation textSource = {};
    if (!file->FGetKidChidCtg(type, number, 0, kctgText, &child) ||
        !file->FFindFlo(child.cki.ctg, child.cki.cno, &textSource))
        return fFalse;
    // FFindFlo returns a borrowed reference and trashes its output on failure.
    loaded.text = textSource;
    loaded.text.pfil->AddRef();
    if (loaded.text.cb < size(short) || !loaded.text.FReadRgb(&loaded.textOsk, size(short), 0))
        return fFalse;
    loaded.text.fp += size(short);
    loaded.text.cb -= size(short);
    if (!file->FGetKidChidCtg(type, number, 0, kctgTxtProps, &child) ||
        !file->FFind(child.cki.ctg, child.cki.cno, &block))
        return fFalse;
    loaded.properties = DynamicArray::PglRead(&block, &loaded.propertyBo, &loaded.propertyOsk);
    if (loaded.properties == pvNil || loaded.properties->CbEntry() != size(RichTextPropertyEntry))
        return fFalse;
    if (file->FGetKidChidCtg(type, number, 0, kctgTxtPropArgs, &child))
    {
        if (!file->FFind(child.cki.ctg, child.cki.cno, &block))
            return fFalse;
        loaded.arguments = AllocatedGroup::PagRead(&block, &loaded.argumentBo, &loaded.argumentOsk);
        if (loaded.arguments == pvNil || loaded.arguments->CbFixed() != size(int32_t))
            return fFalse;
    }
    Clear();
    defaults = loaded.defaults;
    fontName = loaded.fontName;
    text = loaded.text;
    loaded.text.pfil = pvNil;
    textOsk = loaded.textOsk;
    properties = loaded.properties;
    loaded.properties = pvNil;
    propertyBo = loaded.propertyBo;
    propertyOsk = loaded.propertyOsk;
    arguments = loaded.arguments;
    loaded.arguments = pvNil;
    argumentBo = loaded.argumentBo;
    argumentOsk = loaded.argumentOsk;
    return fTrue;
}

bool RichTextData::FCreateRoot(PChunkyFile file, ChunkIdentification *written, RichTextProperties *defaults,
                               PString fontName)
{
    if (!FIn(defaults->dypFont, 4, 256) || !FIn(defaults->dxpDef, 1, kcbMax))
        return fFalse;
    RichTextProperties header = *defaults;
    header.bo = kboCur;
    DataBlock block;
    written->ctg = kctgRichText;
    if (!file->FAdd(size(header) + fontName->CbData(), written->ctg, &written->cno, &block))
    {
        written->cno = cnoNil;
        return fFalse;
    }
    if (block.FWriteRgb(&header, size(header), 0) && fontName->FWrite(&block, size(header)))
        return fTrue;
    file->Delete(written->ctg, written->cno);
    written->cno = cnoNil;
    return fFalse;
}

bool RichTextData::FWrite(PChunkyFile file, ChunkIdentification *written)
{
    if (text.cb < 0 || text.cb > kcbMax - size(short) || properties == pvNil ||
        properties->CbEntry() != size(RichTextPropertyEntry) ||
        (arguments != pvNil && arguments->CbFixed() != size(int32_t)))
        return fFalse;
    if (!FCreateRoot(file, written, &defaults, &fontName))
        return fFalse;
    ChunkNumber child;
    DataBlock block;
    if (!file->FAddChild(written->ctg, written->cno, 0, text.cb + size(short), kctgText, &child, &block) ||
        !block.FWriteRgb(&textOsk, size(short), 0))
        goto fail;
    if (text.cb > 0)
    {
        FileLocation destination;
        if (!block.FGetFlo(&destination))
            goto fail;
        destination.fp += size(short);
        destination.cb -= size(short);
        bool copied = text.FCopy(&destination);
        ReleasePpo(&destination.pfil);
        if (!copied)
            goto fail;
    }
    if (!file->FAddChild(written->ctg, written->cno, 0, properties->CbOnFile(), kctgTxtProps, &child, &block) ||
        !properties->FWrite(&block, propertyBo, propertyOsk))
        goto fail;
    if (arguments != pvNil && arguments->IvMac() > 0)
    {
        if (!file->FAddChild(written->ctg, written->cno, 0, arguments->CbOnFile(), kctgTxtPropArgs, &child, &block) ||
            !arguments->FWrite(&block, argumentBo, argumentOsk))
            goto fail;
    }
    return fTrue;
fail:
    file->Delete(written->ctg, written->cno);
    written->cno = cnoNil;
    return fFalse;
}
