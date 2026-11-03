/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "launch_context.h"
#include <string>
#include <vector>

namespace
{
struct LaunchContext
{
    std::basic_string<KauaiLaunch::NativeChar> text;
    std::vector<std::basic_string<KauaiLaunch::NativeChar>> arguments;
    std::vector<const KauaiLaunch::NativeChar *> pointers;
    bool hasArguments = false;
};
LaunchContext &Context()
{
    static LaunchContext context;
    return context;
}
} // namespace

const KauaiLaunch::NativeChar *KauaiLaunch::CommandLine()
{
    return Context().text.c_str();
}
bool KauaiLaunch::SetCommandLine(const NativeChar *text)
{
    if (!text)
        return false;
    try
    {
        std::basic_string<NativeChar> copy(text);
        auto &context = Context();
        context.text.swap(copy);
        context.arguments.clear();
        context.pointers.clear();
        context.hasArguments = false;
        return true;
    }
    catch (...)
    {
        return false;
    }
}
bool KauaiLaunch::SetArguments(int argc, const NativeChar *const *argv)
{
    if (argc < 0 || (argc && !argv))
        return false;
    try
    {
        LaunchContext next;
        next.arguments.reserve(argc);
        next.pointers.reserve(argc);
        for (int i = 0; i < argc; ++i)
        {
            if (!argv[i])
                return false;
            next.arguments.emplace_back(argv[i]);
        }
        for (const auto &argument : next.arguments)
            next.pointers.push_back(argument.c_str());
        auto &context = Context();
        context.arguments.swap(next.arguments);
        context.pointers.swap(next.pointers);
        context.text.clear();
        context.hasArguments = true;
        return true;
    }
    catch (...)
    {
        return false;
    }
}
bool KauaiLaunch::HasArguments()
{
    return Context().hasArguments;
}
int KauaiLaunch::ArgumentCount()
{
    return static_cast<int>(Context().arguments.size());
}
const KauaiLaunch::NativeChar *const *KauaiLaunch::Arguments()
{
    return Context().pointers.empty() ? nullptr : Context().pointers.data();
}
