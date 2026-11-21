#ifndef MM_PREVIEW_DRAFT_HOST_H
#define MM_PREVIEW_DRAFT_HOST_H
#include "preview_draft.h"
#include "private_archive_host.h"
#include <memory>
#include <string>

inline bool PreviewDraftPath(const std::string &input, String &path)
{
    Filename filename;
    path.SetSzs(const_cast<char *>(input.c_str()));
    if (!filename.FBuildFromPath(&path))
        return false;
    filename.GetStnPath(&path);
    return true;
}

using PreviewDraft = std::unique_ptr<MMPrivateArchive, decltype(&mm_private_archive_close)>;

inline PreviewDraft OpenPreviewDraft(String &path)
{
    const auto profile = KauaiPrivateArchiveProfile();
    return PreviewDraft(mm_native_private_archive_open(reinterpret_cast<const MMNativePathChar *>(path.Psz()),
                                                        MM_PRIVATE_ARCHIVE_EDIT, &profile), mm_private_archive_close);
}
#endif
