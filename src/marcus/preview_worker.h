#ifndef MARCUS_PREVIEW_WORKER_H
#define MARCUS_PREVIEW_WORKER_H
#include "browser_preview.h"
#include "preview_wire.h"
#include <functional>
#include <iosfwd>

// Bounded binary protocol between the browser worker and its isolated decoder.
bool WritePreview(std::ostream &output, const BrowserPreview &preview);
bool WritePreview(FILE *output, const BrowserPreview &preview);
BrowserPreview ReadPreview(const std::vector<std::uint8_t> &bytes);
BrowserPreview PreviewFromPacket(const MarcusPreviewPacket &);
struct SDL_Process;
BrowserPreview RequestPreviewWorker(SDL_Process *, const std::string &request, bool text,
                                    const std::function<bool()> &cancelled);
BrowserPreview RunPreviewWorker(const std::string &movie, const std::string &resources,
                               const std::function<bool()> &cancelled, const std::string &content = {}, bool duration = false,
                               const std::string &fonts = {}, bool background = false, bool prop = false, bool text = false);
#endif
