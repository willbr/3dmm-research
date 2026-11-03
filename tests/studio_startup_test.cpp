/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_startup.h"
#include "launch_context.h"
#include <cstdio>
#include <cstring>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Startup arguments must not import the Windows SDK
#endif
static bool ArgumentCases()
{
    StudioStartup::Options<char> options;
    const char *args[] = {"app", "-m", "-S", "-f", "-pMovie \"Maker\"", "-t3D", "/tmp/My \"movie\".3mm"};
    if (!StudioStartup::ParseArguments(7, args, options) || !options.dontMinimize || !options.hasSpeed ||
        options.slow || options.longName != "Movie \"Maker\"" || options.shortName != "3D" ||
        !options.documentIsLiteral || !options.document || std::strcmp(options.document, args[6]))
        return false;
    const char *emptyNames[] = {"app", "-pfirst", "-p", "-t", "--", "-f.3mm"};
    if (!StudioStartup::ParseArguments(6, emptyNames, options) || !options.hasLongName || !options.hasShortName ||
        !options.longName.empty() || !options.shortName.empty() || options.hasSpeed ||
        std::strcmp(options.document, "-f.3mm"))
        return false;
    const char *unknown[] = {"app", "--mcp-server", "-X", "movie.3mm"};
    if (!StudioStartup::ParseArguments(4, unknown, options) || options.unknownSwitches != 2)
        return false;
    const char *slash[] = {"app", "/s"};
    if (!StudioStartup::ParseArguments(2, slash, options) || options.hasSpeed || std::strcmp(options.document, "/s"))
        return false;
    const char *dash[] = {"app", "-"};
    if (!StudioStartup::ParseArguments(2, dash, options) || std::strcmp(options.document, "-"))
        return false;
    const auto before = options.document;
    const char *extra[] = {"app", "one.3mm", "two.3mm"};
    const char *empty[] = {"app", ""};
    const char *missing[] = {"app", nullptr};
    if (StudioStartup::ParseArguments(3, extra, options) || StudioStartup::ParseArguments(2, empty, options) ||
        StudioStartup::ParseArguments(2, missing, options) ||
        StudioStartup::ParseArguments<char>(-1, nullptr, options) ||
        StudioStartup::ParseArguments<char>(1, nullptr, options) || options.document != before)
        return false;
    if (!StudioStartup::ParseArguments<char>(0, nullptr, options) || options.document)
        return false;
    const char16_t *wideArgs[] = {u"app", u"-pCin\u00e9ma", u"/\u6620\u753b \"1\".3mm"};
    StudioStartup::Options<char16_t> wide;
    if (!StudioStartup::ParseArguments(3, wideArgs, wide) || wide.longName != u"Cin\u00e9ma" ||
        !wide.documentIsLiteral || std::u16string(wide.document) != wideArgs[2])
        return false;

    using NativeChar = KauaiLaunch::NativeChar;
    using NativeText = std::basic_string<NativeChar>;
    NativeText executable(1, static_cast<NativeChar>('a'));
    NativeText path;
    for (auto ch : "/a \"b\".3mm")
        if (ch)
            path += static_cast<NativeChar>(ch);
    const NativeText original = path;
    const NativeChar *nativeArgs[] = {executable.c_str(), path.c_str()};
    if (!KauaiLaunch::SetArguments(2, nativeArgs))
        return false;
    path.assign(1000, static_cast<NativeChar>('x'));
    StudioStartup::Options<NativeChar> native;
    if (!KauaiLaunch::HasArguments() || KauaiLaunch::ArgumentCount() != 2 || *KauaiLaunch::CommandLine() ||
        !StudioStartup::ParseArguments(KauaiLaunch::ArgumentCount(), KauaiLaunch::Arguments(), native) ||
        NativeText(native.document) != original)
        return false;
    const NativeChar *invalid[] = {executable.c_str(), nullptr};
    if (KauaiLaunch::SetArguments(-1, nullptr) || KauaiLaunch::SetArguments(1, nullptr) ||
        KauaiLaunch::SetArguments(2, invalid) || NativeText(native.document) != original)
        return false;
    if (!KauaiLaunch::SetArguments(KauaiLaunch::ArgumentCount(), KauaiLaunch::Arguments()) ||
        NativeText(KauaiLaunch::Arguments()[1]) != original ||
        !KauaiLaunch::SetCommandLine(KauaiLaunch::Arguments()[0]) || KauaiLaunch::HasArguments() ||
        KauaiLaunch::ArgumentCount() || KauaiLaunch::Arguments() ||
        NativeText(KauaiLaunch::CommandLine()) != executable)
        return false;
    return KauaiLaunch::SetArguments(0, nullptr) && KauaiLaunch::HasArguments() && !KauaiLaunch::ArgumentCount() &&
           !KauaiLaunch::Arguments() && !*KauaiLaunch::CommandLine();
}
int main()
{
    using NativeChar = KauaiLaunch::NativeChar;
    using NativeText = std::basic_string<NativeChar>;
    if (!KauaiLaunch::CommandLine() || *KauaiLaunch::CommandLine())
        return 1;
    NativeText input;
    for (const auto ch : "app -p\"Movie Maker\" \"a movie.3mm\"")
        if (ch)
            input += static_cast<NativeChar>(ch);
    const auto original = input;
    if (!KauaiLaunch::SetCommandLine(input.c_str()))
        return 1;
    input.assign(2048, static_cast<NativeChar>('x'));
    StudioStartup::Options<NativeChar> launch;
    if (NativeText(KauaiLaunch::CommandLine()) != original ||
        !StudioStartup::Parse(KauaiLaunch::CommandLine(), launch) || !launch.hasLongName || !launch.document)
        return 1;
    const NativeText document(launch.document);
    if (KauaiLaunch::SetCommandLine(nullptr) || NativeText(KauaiLaunch::CommandLine()) != original ||
        NativeText(launch.document) != document)
        return 1;
    // A source may be the currently exposed value; copying must not invalidate
    // that source before the replacement is ready. No fixed-size truncation.
    if (!KauaiLaunch::SetCommandLine(KauaiLaunch::CommandLine()) ||
        NativeText(KauaiLaunch::CommandLine()) != original || !KauaiLaunch::SetCommandLine(input.c_str()) ||
        NativeText(KauaiLaunch::CommandLine()) != input || !KauaiLaunch::SetCommandLine(NativeText{}.c_str()) ||
        *KauaiLaunch::CommandLine())
        return 1;
    StudioStartup::Options<char> options;
    if (!StudioStartup::Parse(
            "\"C:\\Movie Maker\\3dmovie.exe\" -m /S -f -p\"Movie Maker\" -t3DMM \"C:\\My Films\\one.3mm\"", options) ||
        !options.dontMinimize || !options.hasSpeed || options.slow || options.longName != "Movie Maker" ||
        options.shortName != "3DMM" || options.documentIsLiteral || !options.document ||
        std::strcmp(options.document, "\"C:\\My Films\\one.3mm\""))
        return 1;
    if (!StudioStartup::Parse("app -s -X --mcp-server my movie.3mm -f", options) || !options.slow ||
        options.unknownSwitches != 2 || std::strcmp(options.document, "my movie.3mm -f"))
        return 1;
    if (!StudioStartup::Parse("app -pfirst -p\"\" -tshort", options) || !options.hasLongName ||
        !options.longName.empty() || options.shortName != "short" || options.document)
        return 1;
    auto before = options;
    if (StudioStartup::Parse("app -p\"unfinished", options) || options.shortName != before.shortName)
        return 1;
    if (!StudioStartup::Parse("app -", options) || options.unknownSwitches != 1)
        return 1;
    if (!StudioStartup::Parse("app", options) || options.hasSpeed || options.document)
        return 1;
    StudioStartup::Options<char16_t> wide;
    if (!StudioStartup::Parse(u"app -p\"Cin\u00e9ma\" \"\u6620\u753b.3mm\"", wide) || wide.longName != u"Cin\u00e9ma" ||
        std::u16string(wide.document) != u"\"\u6620\u753b.3mm\"")
        return 1;
    if (!ArgumentCases())
        return 1;
    std::puts("Studio startup: owned raw/argv input, legacy options, literal POSIX paths and Unicode passed.");
    return 0;
}
