/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/archive.h"
#include <cstdio>
#include <cstring>
#include <memory>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <codecvt>
#include <locale>
#include <vector>
#endif

namespace
{
using OutputFile = std::unique_ptr<FILE, decltype(&std::fclose)>;
void Hex32(FILE *out, std::uint32_t value)
{
    std::fprintf(out, "\"%08x\"", static_cast<unsigned>(value));
}
void Identity(FILE *out, const ThreeMM::Id &id)
{
    std::fputs("\"type\": ", out);
    Hex32(out, id.type);
    std::fputs(", \"number\": ", out);
    Hex32(out, id.number);
}
bool Fields(FILE *out, const ThreeMM::RecordHeader &record)
{
    using ThreeMM::RecordKind;
    switch (record.kind)
    {
    case RecordKind::movie:
        std::fprintf(out, "{\"kind\": \"movie\", \"version\": %d, \"back_version\": %d}",
                     static_cast<int>(record.movie.version), static_cast<int>(record.movie.backVersion));
        break;
    case RecordKind::scene:
        std::fprintf(out, "{\"kind\": \"scene\", \"first_frame\": %d, \"last_frame\": %d, \"transition\": %d}",
                     static_cast<int>(record.scene.firstFrame), static_cast<int>(record.scene.lastFrame),
                     static_cast<int>(record.scene.transition));
        break;
    case RecordKind::actor:
        std::fprintf(out, "{\"kind\": \"actor\", \"actor_id\": %d, \"first_frame\": %d, \"last_frame\": %d}",
                     static_cast<int>(record.actor.actorId), static_cast<int>(record.actor.firstFrame),
                     static_cast<int>(record.actor.lastFrame));
        break;
    case RecordKind::caption:
        std::fprintf(out,
                     "{\"kind\": \"caption\", \"first_frame\": %d, \"max_frame\": %d, \"story\": %s, \"bounds\": [%d, "
                     "%d, %d, %d], \"text\": {",
                     static_cast<int>(record.caption.firstFrame), static_cast<int>(record.caption.maxFrame),
                     record.caption.story ? "true" : "false", static_cast<int>(record.caption.bounds.left),
                     static_cast<int>(record.caption.bounds.top), static_cast<int>(record.caption.bounds.right),
                     static_cast<int>(record.caption.bounds.bottom));
        Identity(out, record.caption.text);
        std::fputs("}}", out);
        break;
    case RecordKind::opaque:
        std::fputs("null", out);
        break;
    }
    return !std::ferror(out);
}
bool Dump(FILE *out, ThreeMM::Archive &archive, ThreeMM::Error &error)
{
    std::fprintf(out, "{\n  \"schema\": \"3mm-dump-1\",\n  \"name_code_unit_bits\": %u,\n  \"chunks\": [",
                 archive.NameCodeUnitBits());
    bool first = true;
    const auto &chunks = archive.Chunks();
    for (std::size_t index = 0; index < chunks.size(); ++index)
    {
        const auto &chunk = chunks[index];
        ThreeMM::PayloadPage page;
        ThreeMM::RecordHeader record;
        if (!archive.ReadPayload(index, 0, 4096, page, error) || !archive.ReadRecord(index, record, error))
            return false;
        std::fputs(first ? "\n    {" : ",\n    {", out);
        first = false;
        Identity(out, chunk.id);
        std::fprintf(out, ", \"loner\": %s, \"packed\": %s, \"forest\": %s, \"name_units_hex\": \"",
                     chunk.loner ? "true" : "false", chunk.packed ? "true" : "false", chunk.forest ? "true" : "false");
        for (auto unit : chunk.nameUnits)
            std::fprintf(out, archive.NameCodeUnitBits() == 8 ? "%02x" : "%04x", static_cast<unsigned>(unit));
        std::fputs("\", \"children\": [", out);
        bool firstChild = true;
        for (const auto &child : chunk.children)
        {
            if (!child.present)
            {
                error.code = ThreeMM::ErrorCode::corrupt_archive;
                error.message = "Missing child target.";
                error.hasChunk = true;
                error.chunk = chunk.id;
                return false;
            }
            std::fputs(firstChild ? "{" : ", {", out);
            firstChild = false;
            std::fputs("\"id\": ", out);
            Hex32(out, child.slot);
            std::fputs(", ", out);
            Identity(out, child.target);
            std::fputc('}', out);
        }
        std::fprintf(out, "], \"unpacked_size\": %u, \"fields\": ", page.totalBytes);
        if (!Fields(out, record))
            return false;
        std::fputs(", \"payload_hex\": \"", out);
        for (;;)
        {
            for (auto byte : page.bytes)
                std::fprintf(out, "%02x", static_cast<unsigned>(byte));
            const auto next = page.offset + static_cast<std::uint32_t>(page.bytes.size());
            if (next >= page.totalBytes)
                break;
            if (!archive.ReadPayload(index, next, 4096, page, error))
                return false;
        }
        std::fputs("\"}", out);
    }
    std::fputs("\n  ]\n}\n", out);
    return !std::ferror(out);
}
} // namespace
static int Run(int argc, char **argv)
{
    if (argc != 4 || std::strcmp(argv[1], "dump") || std::strcmp(argv[2], "--json"))
    {
        std::fputs("usage: 3mm dump --json <movie-or-chunky-file>\n", stderr);
        return 2;
    }
    try
    {
        ThreeMM::Archive archive;
        ThreeMM::Error error;
        OutputFile staged(std::tmpfile(), &std::fclose);
        if (!archive.Open(argv[3], error) || !staged || !Dump(staged.get(), archive, error) ||
            std::fflush(staged.get()) || std::fseek(staged.get(), 0, SEEK_SET))
        {
            std::fprintf(stderr, "3mm: %s\n", error ? error.message.c_str() : "cannot stage archive dump");
            return 1;
        }
#ifdef _WIN32
        if (_setmode(_fileno(stdout), _O_BINARY) == -1)
            return 1;
#endif
        char buffer[8192];
        std::size_t count;
        while ((count = std::fread(buffer, 1, sizeof(buffer), staged.get())) != 0)
            if (std::fwrite(buffer, 1, count, stdout) != count)
                return 1;
        return std::ferror(staged.get()) || std::fflush(stdout) ? 1 : 0;
    }
    catch (...)
    {
        std::fputs("3mm: insufficient resources to dump archive\n", stderr);
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **wideArgv)
{
    try
    {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
        std::vector<std::string> arguments;
        for (int i = 0; i < argc; ++i)
            arguments.push_back(convert.to_bytes(wideArgv[i]));
        std::vector<char *> argv;
        for (auto &argument : arguments)
            argv.push_back(&argument[0]);
        return Run(argc, argv.data());
    }
    catch (...)
    {
        std::fputs("3mm: invalid command-line text\n", stderr);
        return 1;
    }
}
#else
int main(int argc, char **argv)
{
    return Run(argc, argv);
}
#endif
