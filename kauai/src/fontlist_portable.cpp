/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_host.h"
#include "font_names.h"
#include "frame.h"
#undef size
ASSERTNAME

bool FontList::_FInitPortable(void)
{
    PStringTable_GST table = pvNil;
    auto fail = [&]() {
        ReleasePpo(&table);
        PushErc(ercGfxNoFontList);
        return false;
    };
    try
    {
        std::vector<KauaiFonts::FamilyInfo> families;
        if (!KauaiFonts::StudioFontFamilies(families))
            return fail();
        table = StringTable_GST::PgstNew(sizeof(LegacyLong));
        if (!table)
            return fail();
        for (const auto &family : families)
        {
            std::vector<std::uint8_t> encoded;
            if (!KauaiFonts::NameFromUtf8(
                    family.name, sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes,
                    encoded))
                continue;
            const auto length = encoded.size() / sizeof(achar);
            if (!length || length > kcchMaxStn)
                continue;
            LegacyLong index;
            auto name = reinterpret_cast<achar *>(encoded.data());
            if (table->FFindRgch(name, static_cast<long>(length), &index, fgstUserSorted))
                continue;
            LegacyLong flags = family.fixedPitch ? 1 : 0;
            if (!table->FInsertRgch(index, name, static_cast<long>(length), &flags))
                return fail();
        }
        LegacyLong system;
        achar systemName[] = {'S', 'y', 's', 't', 'e', 'm', 0};
        String name(systemName);
        if (!table->FFindStn(&name, &system, fgstUserSorted))
            return fail();
        ReleasePpo(&_pgst);
        _pgst = table;
        _onnSystem = system;
        _fPortableFonts = true;
        KauaiFonts::RecordFontEnumeration(true);
        AssertThis(0);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return fail();
    }
}
