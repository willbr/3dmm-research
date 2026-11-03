/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "file_browser.h"
#include "platform_files.h"
#include <cstdio>
#include <cstdlib>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error File browser tests must not import native SDK headers
#endif
using namespace KauaiFiles;
static void Check(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "File browser check failed at %d\n", line);
        std::exit(1);
    }
}
#define CHECK(value) Check(value, __LINE__)
static void Create(const std::string &path)
{
    OpenOptions options;
    options.write = true;
    options.creation = Creation::CreateAlways;
    auto file = Open(path.c_str(), options);
    CHECK(file);
    Close(file);
}
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const std::string root = argv[1];
    MakeDirectory(root.c_str());
    CHECK(MakeDirectory((root + "/sub").c_str()));
    Create(root + "/A.3mm");
    Create(root + "/b.WAV");
    Create(root + "/bare");
    Browser browser;
    browser.filters = {{"Movie", "*.3mm"}, {"Media", "*.3mm;*.wav"}, {"All", "*.*"}};
    CHECK(browser.Navigate(root));
    const auto resolved = browser.directory;
    browser.home = resolved;
    CHECK(browser.entries.size() == 2 && browser.entries[0].directory && browser.entries[1].name == "A.3mm");
    browser.Select(1);
    CHECK(browser.Accept() && browser.selection.find("A.3mm") != std::string::npos);
    browser.Select(0);
    CHECK(!browser.Accept() && browser.directory != resolved && browser.entries.empty());
    CHECK(browser.Up() && browser.directory == resolved);
    CHECK(!browser.Navigate(root + "/missing") && browser.directory == resolved);
    browser.filter = 1;
    CHECK(browser.Refresh() && browser.entries.size() == 3);
    browser.filter = 2;
    CHECK(browser.Refresh() && browser.entries.size() == 4);
    for (const auto &pattern : {"*.wav", "*.3mm;*.wav", "*.*", "b.?av", "*b*W*"})
        CHECK(Browser::Matches("b.WAV", pattern));
    CHECK(!Browser::Matches("b.WAV", "*.3mm") && !Browser::Matches("long.wav", "?.wav"));
    browser.name = "missing.3mm";
    CHECK(!browser.Accept());
    browser.open = false;
    browser.defaultExtension = "3mm";
    browser.name = "new.other";
    CHECK(browser.Accept() && browser.selection.find("new.other.3mm") != std::string::npos);
    CHECK(Info(browser.selection.c_str(), nullptr) == Status::Missing);
    browser.name = "new.3MM";
    CHECK(browser.Accept() && browser.selection.find("new.3MM") != std::string::npos);
    browser.name = "new";
    CHECK(browser.Accept() && browser.selection.find("new.3mm") != std::string::npos);
    browser.name = "";
    CHECK(!browser.Accept());
    browser.name = "missing/new";
    CHECK(!browser.Accept());
    browser.name = "retained";
    browser.Select(0);
    CHECK(browser.name == "retained");
    CHECK(Delete((root + "/A.3mm").c_str()) && Delete((root + "/b.WAV").c_str()) && Delete((root + "/bare").c_str()));
    CHECK(DeleteDirectory((root + "/sub").c_str()) && DeleteDirectory(root.c_str()));
    std::puts("File browser: filters, directory ordering/navigation, file selection, save suffixes, missing paths and "
              "no file writes passed.");
}
