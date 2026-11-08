/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PACK_OPTIONS_H
#define STUDIO_PACK_OPTIONS_H
#include <memory>
#include <string>
namespace StudioPacks
{
std::string UTF8(PString text);
bool SetText(PString text, const std::string &utf8);
TagSourceAccess ResolveSource(PString longName, PString shortName, PFilename path);
class Options
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Options();
    ~Options();
    void Initialize(PDialog dialog);
    bool Handle(PDialog dialog, LegacyLong item, bool &handled);
};
} // namespace StudioPacks
#endif
