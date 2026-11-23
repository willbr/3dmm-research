#ifndef MM_PREVIEW_DRAFT_HOST_H
#define MM_PREVIEW_DRAFT_HOST_H
#include "preview_draft.h"
#include "private_archive_host.h"
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

#endif
