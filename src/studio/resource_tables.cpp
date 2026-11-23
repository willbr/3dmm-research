/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_resources.h"
#include "handle_services.h"

static PStringTable_GST NormalizeExtras(PStringTable_GST table, short byteOrder)
{
    if (!table)
        return pvNil;
    if (table->CbExtra() != 0 && table->CbExtra() != sizeof(LegacyLong))
    {
        ReleasePpo(&table);
        return pvNil;
    }
    if (byteOrder != kboCur && table->CbExtra() != 0)
    {
        for (LegacyLong index = 0; index < table->IvMac(); ++index)
        {
            LegacyLong extra;
            table->GetExtra(index, &extra);
            SwapBytesRglw(&extra, 1);
            table->PutExtra(index, &extra);
        }
    }
    return table;
}

PStringTable_GST StudioResources::ReadStringTable(MMResourceCache *cache, ChunkNumber number, bool convertExtras)
{
    struct Table { PStringTable_GST table; short order; } result = {nullptr, kboCur};
    int read;
    mm_resource_cache_visit_payload(cache, 'GST ', number, nullptr, nullptr,
        [](void *out, const MMReadStream *stream) -> int {
            auto result = static_cast<Table *>(out);
            result->table = StringTable_GST::PgstReadStream(*stream, &result->order, nullptr);
            return result->table != nullptr;
        }, &result, &read);
    return convertExtras ? NormalizeExtras(result.table, result.order) : result.table;
}

PDynamicArray StudioResources::ReadPalette(MMResourceCache *cache, ChunkNumber number)
{
    PDynamicArray palette = nullptr;
    int read;
    mm_resource_cache_visit_payload(cache, 'GLCR', number, nullptr, nullptr,
        [](void *out, const MMReadStream *stream) -> int {
            auto &palette = *static_cast<PDynamicArray *>(out);
            palette = DynamicArray::PglReadStream(*stream, nullptr, nullptr);
            return palette != nullptr;
        }, &palette, &read);
    return palette;
}
