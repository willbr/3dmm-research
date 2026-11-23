#include "mm/preview.h"

namespace MM
{
bool Preview::Open(Archive &&archive, const std::string &resourcesPath, Error &error, bool preserveFraming)
{
    if (!archive.CanPreview())
        return OpenCore(nullptr, nullptr, resourcesPath, error, preserveFraming, true);
    return OpenCore(&archive, [](void *p, Internal::ArchiveInput &output) {
        static_cast<Archive *>(p)->TransferTo(output);
    }, resourcesPath, error, preserveFraming, true);
}
} // namespace MM
