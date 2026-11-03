/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <three_mm/archive.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#if defined(size) || defined(NewObj) || defined(WIN) || defined(STRICT)
#error The public archive API must not expose legacy macros or definitions.
#endif
static void Verify(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "Archive API check failed at line %d\n", line);
        std::exit(1);
    }
}
#define Check(value) Verify((value), __LINE__)
static std::size_t Find(const ThreeMM::Archive &archive, const char *type)
{
    for (std::size_t i = 0; i < archive.Chunks().size(); ++i)
        if (ThreeMM::TypeLabel(archive.Chunks()[i].id.type) == type)
            return i;
    Check(false);
    return 0;
}
int main(int argc, char **argv)
{
    using namespace ThreeMM;
    Archive archive;
    Error error;
    PayloadPage payload;
    RecordHeader record;
    StringTablePage strings;
    Check(!archive.Open("", error) && error.code == ErrorCode::invalid_path && !error.hasChunk);
    Check(!archive.ReadPayload(0, 0, 16, payload, error) && error.code == ErrorCode::invalid_argument);
    Check(payload.bytes.empty());
    if (argc == 1)
        return 0; // Independent consumer needs no fixture or host callbacks.
    Check(argc == 5);
    Check(archive.Open(argv[1], error) && !error);
    Check(archive.Chunks().size() == 6 && archive.NameCodeUnitBits() == 8);
    const auto originalPath = archive.Path();
    const auto scene = Find(archive, "SCEN");
    Check(archive.ReadRecord(scene, record, error) && record.kind == RecordKind::scene);
    Check(record.scene.firstFrame == 1 && record.scene.lastFrame == 12);
    Check(archive.Chunks()[scene].name.find('\n') != std::string::npos);
    Check(!archive.Chunks()[scene].nameUnits.empty());
    Check(archive.Chunks()[scene].children[0].slot == 1);
    const auto caption = Find(archive, "TBOX");
    Check(archive.ReadRecord(caption, record, error) && record.kind == RecordKind::caption);
    Check(record.caption.firstFrame == 2 && record.caption.bounds.right == 101);
    Check(!archive.ReadStrings(scene, 0, 128, strings, error) && error.code == ErrorCode::unsupported_format &&
          error.hasChunk);
    Check(error.chunk == archive.Chunks()[scene].id);
    Check(!archive.Open("", error) && archive.Path() == originalPath && archive.Chunks().size() == 6);
    Check(archive.ReadRecord(Find(archive, "MVIE"), record, error) && !error && record.kind == RecordKind::movie);
    Check(archive.Open(argv[2], error));
    Check(!archive.ReadRecord(Find(archive, "SCEN"), record, error) && error.code == ErrorCode::corrupt_archive &&
          error.hasChunk);
    Check(record.kind == RecordKind::opaque);
    Check(archive.ReadPayload(Find(archive, "SCEN"), 0, 16, payload, error) && payload.bytes.size() == 1 && !error);
    Options limitedOptions;
    limitedOptions.maxDecodedBytes = 16;
    Archive limited(limitedOptions);
    Check(limited.Open(argv[3], error));
    Check(!limited.ReadPayload(Find(limited, "ZZZZ"), 0, 16, payload, error) &&
          error.code == ErrorCode::resource_limit);
    Check(payload.bytes.empty() && error.hasChunk);
    Check(archive.Open(argv[3], error));
    Check(archive.ReadPayload(Find(archive, "ZZZZ"), 1000, 4096, payload, error));
    Check(payload.totalBytes == 1024 && payload.bytes.size() == 24);
    Check(std::all_of(payload.bytes.begin(), payload.bytes.end(), [](std::uint8_t b) { return b == 'a'; }));
    Check(archive.Open(argv[4], error) && archive.Chunks().size() == 10);
    Check(archive.ReadStrings(0, 0, 128, strings, error) && strings.totalEntries == 5);
    Check(strings.entries[1].text.empty() && strings.entries[2].text == "Line\nbreak");
    Check(strings.entries[0].text == strings.entries[4].text && strings.entries[4].index == 4);
    Check(archive.ReadStrings(1, 0, 128, strings, error) && strings.totalEntries == 32 && strings.extraBytes == 4);
    Check(strings.entries[0].extra == std::vector<std::uint8_t>({0, 0x7f, 0x80, 0xff}));
    Check(archive.ReadStrings(2, 0, 128, strings, error) && strings.entries.empty() && !error);
    for (auto i : {3u, 5u, 7u, 8u})
    {
        Check(!archive.ReadStrings(i, 0, 128, strings, error) && error.code == ErrorCode::corrupt_archive);
        Check(error.hasChunk && error.chunk == archive.Chunks()[i].id && strings.entries.empty());
        Check(archive.ReadPayload(i, 0, 4096, payload, error) && !payload.bytes.empty());
    }
    Check(archive.ReadStrings(4, 128, 128, strings, error) && strings.entries.size() == 128);
    Check(strings.entries[0].index == 128 && strings.entries[0].text == "Entry 128");
    Check(archive.ReadStrings(4, 256, 128, strings, error) && strings.entries.size() == 44);
    Check(archive.ReadStrings(4, 999, 128, strings, error) && strings.firstEntry == 300 && strings.entries.empty());
    for (auto i : {6u, 9u})
        Check(archive.ReadStrings(i, 0, 128, strings, error) && strings.entries[0].text == "Wide");
    Check(!archive.ReadStrings(0, 0, 129, strings, error) && error.code == ErrorCode::invalid_argument);
    Check(!archive.ReadPayload(0, 0, 1024 * 1024 + 1, payload, error) && error.code == ErrorCode::invalid_argument);
    archive.Close();
    Check(archive.Path().empty() && archive.Chunks().empty());
    return 0;
}
