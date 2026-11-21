#include "browser_preview.h"
#include "browser_poster.h"
#include "platform_text.h"
#include <new>

BrowserPreview ReadBrowserPreview(const std::string &movie, const std::string &resources)
{
    BrowserPreview result;
    MarcusBrowserPoster poster;
    marcus_browser_poster(movie.c_str(), resources.c_str(), KauaiText::Services(), &poster, &result,
        [](void *context, MMU32 width, MMU32 height) -> unsigned char * {
            auto &preview = *static_cast<BrowserPreview *>(context);
            try
            {
                preview.rgba.resize(static_cast<std::size_t>(width) * height * 4);
            }
            catch (const std::bad_alloc &)
            {
                return nullptr;
            }
            preview.width = static_cast<int>(width);
            preview.height = static_cast<int>(height);
            return preview.rgba.data();
        });
    result.title = poster.title;
    result.details = poster.details;
    if (poster.error)
        result.error = poster.error;
    return result;
}
