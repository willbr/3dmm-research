#ifndef MARCUS_BROWSER_PREVIEW_H
#define MARCUS_BROWSER_PREVIEW_H
#include <string>
#include <vector>
#include <cstdint>
struct BrowserPreview
{
    std::string title, details, error;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
// Uses legacy archive globals. The browser calls this in an isolated helper
// process; never run it concurrently with the live player in the same process.
BrowserPreview ReadBrowserPreview(const std::string &movie, const std::string &resources);
#endif
