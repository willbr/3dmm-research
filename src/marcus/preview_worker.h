#ifndef MARCUS_PREVIEW_WORKER_H
#define MARCUS_PREVIEW_WORKER_H
#include "browser_preview.h"
#include <functional>
#include <iosfwd>

// Bounded binary protocol between the browser worker and its isolated decoder.
bool WritePreview(std::ostream &output, const BrowserPreview &preview);
BrowserPreview ReadPreview(const std::vector<std::uint8_t> &bytes);
BrowserPreview RunPreviewWorker(const std::string &movie, const std::string &resources,
                               const std::function<bool()> &cancelled);
#endif
