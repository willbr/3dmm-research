/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_resources.h"
PStringTable_GST StudioResources::ReadStringTable(PDataBlock block)
{
    short byteOrder;
    auto table = StringTable_GST::PgstRead(block, &byteOrder);
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
