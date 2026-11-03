/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_STARTUP_H
#define STUDIO_STARTUP_H
#include <string>
#include <cstddef>
#include <utility>
namespace StudioStartup
{
template <class Char> struct Options
{
    bool dontMinimize = false, hasSpeed = false, slow = false;
    bool hasLongName = false, hasShortName = false;
    std::basic_string<Char> longName, shortName;
    const Char *document = nullptr; // Borrowed from the source text/argument list.
    bool documentIsLiteral = false; // argv path: do not strip or reinterpret quotes.
    std::size_t unknownSwitches = 0;
};
// Original Studio command-line grammar: spaces separate options, quotes group
// them, and the first non-option consumes the entire remaining document path.
// Filesystem resolution and the platform's command-line acquisition stay outside.
template <class Char> bool Parse(const Char *commandLine, Options<Char> &output)
{
    if (!commandLine)
        return false;
    Options<Char> result;
    const auto tokenEnd = [](const Char *p) {
        bool quoted = false;
        while (*p && (quoted || *p != Char(' ')))
        {
            if (*p == Char('"'))
                quoted = !quoted;
            ++p;
        }
        return p;
    };
    auto p = tokenEnd(commandLine); // Executable name.
    while (*p == Char(' '))
        ++p;
    while (*p)
    {
        if (*p != Char('-') && *p != Char('/'))
        {
            result.document = p;
            break;
        }
        const auto end = tokenEnd(p);
        Char option = p[1];
        if (option >= Char('a') && option <= Char('z'))
            option -= Char('a') - Char('A');
        switch (option)
        {
        case Char('M'):
            result.dontMinimize = true;
            break;
        case Char('S'):
            result.hasSpeed = true;
            result.slow = true;
            break;
        case Char('F'):
            result.hasSpeed = true;
            result.slow = false;
            break;
        case Char('P'):
        case Char('T'): {
            auto first = p + 2, last = end;
            if (*first == Char('"'))
            {
                if (last - first < 2 || last[-1] != Char('"'))
                    return false;
                ++first;
                --last;
            }
            if (option == Char('P'))
            {
                result.hasLongName = true;
                result.longName.assign(first, last);
            }
            else
            {
                result.hasShortName = true;
                result.shortName.assign(first, last);
            }
            break;
        }
        default:
            ++result.unknownSwitches;
            break;
        }
        p = end;
        while (*p == Char(' '))
            ++p;
    }
    output = std::move(result);
    return true;
}
// POSIX/SDL argument-list grammar: argv[0] is the executable; only '-' starts
// options, '--' ends options, and exactly one document argument is accepted.
// Product names are inline (-pName, -tName), as in the legacy grammar, but the
// shell has already grouped them. Spaces and quotes within an argument are
// literal. Invalid argument lists leave output unchanged.
template <class Char> bool ParseArguments(int argc, const Char *const *argv, Options<Char> &output)
{
    if (argc < 0 || (argc && !argv))
        return false;
    for (int i = 0; i < argc; ++i)
        if (!argv[i])
            return false;
    Options<Char> result;
    bool options = true;
    for (int i = 1; i < argc; ++i)
    {
        const auto p = argv[i];
        if (options && p[0] == Char('-') && p[1] == Char('-') && !p[2])
        {
            options = false;
            continue;
        }
        if (!options || p[0] != Char('-') || !p[1])
        {
            if (!*p || result.document || i + 1 != argc)
                return false;
            result.document = p;
            result.documentIsLiteral = true;
            continue;
        }
        Char option = p[1];
        if (option >= Char('a') && option <= Char('z'))
            option -= Char('a') - Char('A');
        switch (option)
        {
        case Char('M'):
            result.dontMinimize = true;
            break;
        case Char('S'):
            result.hasSpeed = true;
            result.slow = true;
            break;
        case Char('F'):
            result.hasSpeed = true;
            result.slow = false;
            break;
        case Char('P'):
            result.hasLongName = true;
            result.longName = p + 2;
            break;
        case Char('T'):
            result.hasShortName = true;
            result.shortName = p + 2;
            break;
        default:
            ++result.unknownSwitches;
            break;
        }
    }
    output = std::move(result);
    return true;
}
} // namespace StudioStartup
#endif
