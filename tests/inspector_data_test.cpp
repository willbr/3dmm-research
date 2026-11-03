/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "archive.h"
#include <cstdio>
#include <cstdlib>
static void Check(bool value)
{
    if (!value)
        std::exit(1);
}
int main(int argc, char **argv)
{
    Check(argc == 4);
    Inspector::Archive archive;
    std::string error;
    Check(archive.Open(argv[1], error) && error.empty());
    const auto original = archive.Path();
    const auto count = archive.Chunks().size();
    Check(count >= 6);
    bool scene = false, packed = false, child = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto &chunk = archive.Chunks()[i];
        for (const auto &link : chunk.children)
        {
            Check(link.present);
            child = true;
        }
        Inspector::Detail detail;
        Check(archive.ReadDetail(i, 0, detail, error));
        if (Inspector::TypeLabel(chunk.id.type) == "SCEN")
        {
            Check(!detail.fields.empty() && detail.fields[0] == "Scene frames: 1 to 12");
            scene = true;
        }
        if (chunk.packed)
        {
            Check(detail.totalBytes == 1024 && detail.bytes.size() == 1024);
            for (auto byte : detail.bytes)
                Check(byte == 'a');
            Check(archive.ReadDetail(i, 1000, detail, error) && detail.bytes.size() == 24);
            Check(archive.ReadDetail(i, 99999, detail, error) && detail.bytes.empty());
            packed = true;
        }
    }
    Check(scene && packed && child);
    Check(!archive.Open(argv[2], error) && !error.empty());
    Check(archive.Path() == original && archive.Chunks().size() == count);
    Inspector::Detail detail;
    Check(!archive.ReadDetail(count, 0, detail, error));
    archive.Close();
    Check(archive.Chunks().empty() && archive.Path().empty());
    Check(archive.Open(argv[3], error));
    Check(archive.Chunks().size() == 10);
    for (std::size_t i = 0; i < 10; ++i)
    {
        Check(archive.Chunks()[i].id.number == i);
        Check(archive.ReadDetail(i, 0, detail, error));
        const auto &page = detail.strings;
        Check(page.present && !detail.bytes.empty());
        if (i == 3 || i == 5 || i == 7 || i == 8)
        {
            Check(!page.error.empty() && page.entries.empty());
            continue;
        }
        Check(page.error.empty());
        if (i == 0)
        {
            Check(page.totalEntries == 5 && page.extraBytes == 0 && page.entries.size() == 5);
            Check(page.entries[0].text == "Alpha" && page.entries[1].text.empty());
            Check(page.entries[2].text == "Line\nbreak" && page.entries[3].text == "Tab\tvalue");
            Check(page.entries[4].text == "Alpha" && page.entries[4].index == 4);
        }
        else if (i == 1)
        {
            Check(page.totalEntries == 32 && page.extraBytes == 4 && archive.Chunks()[i].packed);
            Check(page.entries[0].text == "Entry 0");
            Check(page.entries[0].extra == std::vector<std::uint8_t>({0, 0x7f, 0x80, 0xff}));
        }
        else if (i == 2)
            Check(page.totalEntries == 0 && page.entries.empty());
        else if (i == 4)
        {
            Check(page.totalEntries == 300 && page.entries.size() == 128);
            Check(archive.ReadDetail(i, 0, detail, error, 128));
            Check(detail.strings.firstEntry == 128 && detail.strings.entries.size() == 128);
            Check(detail.strings.entries[0].index == 128 && detail.strings.entries[0].text == "Entry 128");
            Check(archive.ReadDetail(i, 4096, detail, error, 256));
            Check(detail.strings.entries.size() == 44 && detail.strings.entries.back().index == 299);
            Check(archive.ReadDetail(i, 0, detail, error, 999));
            Check(detail.strings.firstEntry == 300 && detail.strings.entries.empty());
        }
        else if (i == 6 || i == 9)
            Check(page.totalEntries == 1 && page.entries[0].text == "Wide");
    }
    return 0;
}
