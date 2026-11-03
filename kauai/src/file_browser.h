/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_FILE_BROWSER_H
#define KAUAI_FILE_BROWSER_H
#include <string>
#include <vector>
namespace KauaiFiles
{
struct BrowserEntry
{
    std::string name, path;
    bool directory = false;
};
struct BrowserFilter
{
    std::string label, patterns;
};
// UTF-8 presentation over platform_files. Never changes the process directory,
// opens a selected file for writing, or modifies the filesystem.
class Browser
{
  public:
    bool open = true;
    std::string directory, home, name, selection, error, defaultExtension;
    std::vector<BrowserEntry> entries;
    std::vector<BrowserFilter> filters;
    unsigned filter = 0;
    bool Navigate(const std::string &path);
    bool Refresh();
    bool Up();
    void Select(unsigned index);
    // A selected directory navigates and returns false; only files return true.
    bool Accept();
    static bool Matches(const std::string &name, const std::string &patterns);
};
} // namespace KauaiFiles
#endif
