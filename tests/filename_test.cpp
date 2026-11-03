/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
ASSERTNAME
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
static void CheckAt(bool ok, int line)
{
    if (!ok)
    {
        std::fprintf(stderr, "Filename check failed at line %d\n", line);
        std::exit(1);
    }
}
#define Check(ok) CheckAt((ok), __LINE__)

int main()
{
    Filename root;
    Check(root.FGetTemp());
    String path;
    root.GetStnPath(&path);
    Check(root.FBuildFromPath(&path, kftgDir));
    root.GetStnPath(&path);
    Check(KauaiFiles::MakeDirectory(path.Psz()));
    Filename child = root;
    String leaf(PszLit("child"));
    Check(child.FDownDir(&leaf, ffniCreateDir | ffniMoveToDir));
    Filename parent = child;
    Check(parent.FUpDir(&leaf, ffniMoveToDir));
    Check(leaf.FEqualSz(PszLit("child")) && parent.FEqual(&root));

    Filename first = root;
    leaf = PszLit("first");
    Check(first.FSetLeaf(&leaf, 'TXT') && first.Ftg() == 'TXT' && first.TExists() == tNo);
    first.GetLeaf(&leaf);
    Check(leaf.FEqualSz(PszLit("first.TXT")));
    Filename second = child;
    leaf = PszLit("second.bin");
    Check(second.FSetLeaf(&leaf) && second.Ftg() == 'BIN' && !first.FSameDir(&second));
    PFileObject file = FileObject::PfilCreate(&first);
    Check(file != pvNil && file->FWriteRgb((void *)"abcdef", 6, 0));
    Check(file->FpMac() == 6);
    // Force the real OS sharing failure, rather than finding the same cached
    // FileObject. The restored original handle must remain readable.
    ReleasePpo(&file);
    file = FileObject::PfilOpen(&first);
    Check(file != pvNil);
    first.GetStnPath(&path);
    KauaiFiles::FileHandle other = KauaiFiles::Open(path.Psz(), KauaiFiles::OpenOptions{});
    Check(other != nullptr);
    Check(!file->FSetGrffil(ffilWriteEnable));
    char bytes[6];
    Check(file->FReadRgb(bytes, 6, 0) && !std::memcmp(bytes, "abcdef", 6));
    Check((file->GrffilCur() & ffilWriteEnable) == 0);
    KauaiFiles::Close(other);
    Check(file->FSetGrffil(ffilWriteEnable));
    Check(file->FWriteRgb((void *)"XY", 2, 2));
    Check(file->FSetFpMac(4) && file->FpMac() == 4);
    Filename renamed = first;
    leaf = PszLit("renamed.txt");
    Check(renamed.FSetLeaf(&leaf) && file->FRename(&renamed));
    Check(first.TExists() == tNo && renamed.TExists() == tYes);
    Check(file->FReadRgb(bytes, 4, 0) && !std::memcmp(bytes, "abXY", 4));
    ReleasePpo(&file);
    first = renamed;

    file = FileObject::PfilCreate(&second);
    Check(file != pvNil && file->FWriteRgb((void *)"other", 5, 0));
    ReleasePpo(&file);
    Filename sameDirectory = root;
    leaf = PszLit("other.bin");
    Check(sameDirectory.FSetLeaf(&leaf));
    PFileObject otherFile = FileObject::PfilCreate(&sameDirectory);
    Check(otherFile && otherFile->FWriteRgb((void *)"other", 5, 0));
    file = FileObject::PfilOpen(&first, ffilWriteEnable | ffilDenyWrite);
    Check(file && file->FSwapNames(otherFile));
    Filename swapped;
    file->GetFni(&swapped);
    Check(swapped.FEqual(&sameDirectory));
    Check(file->FReadRgb(bytes, 4, 0) && !std::memcmp(bytes, "abXY", 4));
    ReleasePpo(&file);
    ReleasePpo(&otherFile);

    FileNameEnumerator enumerator;
    Check(enumerator.FInit(&root, pvNil, 0, ffneRecurse));
    Filename found;
    LegacyUlong flags;
    unsigned files = 0, pre = 0, post = 0;
    while (enumerator.FNextFni(&found, &flags))
    {
        if (found.FDir())
        {
            Check(found.FEqual(&child));
            if (flags == ffnePre)
                ++pre;
            else
            {
                Check(flags == ffnePost && pre == 1);
                ++post;
            }
        }
        else
        {
            Check(flags == (ffnePre | ffnePost));
            ++files;
        }
    }
    Check(files == 3 && pre == 1 && post == 1);
    FileType types[] = {kftgDir, 'TXT'};
    Check(enumerator.FInit(&root, types, 2, ffneRecurse));
    files = 0;
    while (enumerator.FNextFni(&found, &flags))
        if (!found.FDir())
        {
            Check(found.Ftg() == 'TXT');
            ++files;
        }
    Check(files == 1);
    Check(enumerator.FInit(&root, pvNil, 0, ffneRecurse));
    files = 0;
    LegacyUlong next = ffneNil;
    while (enumerator.FNextFni(&found, &flags, next))
    {
        next = found.FDir() && flags == ffnePre ? ffneSkipDir : ffneNil;
        if (!found.FDir())
            ++files;
    }
    Check(files == 2);
    // Reinitialize with an active directory stack, then finish normally.
    Check(enumerator.FInit(&root, pvNil, 0, ffneRecurse));
    do
    {
        Check(enumerator.FNextFni(&found, &flags));
    } while (!found.FDir());
    Check(enumerator.FInit(&child, pvNil, 0));
    Check(enumerator.FNextFni(&found) && found.FEqual(&second));
    Check(!enumerator.FNextFni(&found));
    Check(enumerator.FInit(pvNil, pvNil, 0));
    Check(enumerator.FNextFni(&found) && found.FDir()); // Native roots, not synthetic drive paths.
    Check(enumerator.FInit(&child, pvNil, 0));
    while (enumerator.FNextFni(&found))
    {
    }

    PFileObject temporary = FileObject::PfilCreateTemp(&first);
    Check(temporary != pvNil && temporary->FTemp());
    Filename temporaryName;
    temporary->GetFni(&temporaryName);
    Check(temporaryName.TExists() == tYes);
    ReleasePpo(&temporary);
    Check(temporaryName.TExists() == tNo);
    Check(first.FDelete() && second.FDelete() && sameDirectory.FDelete());
    child.GetStnPath(&path);
    Check(KauaiFiles::DeleteDirectory(path.Psz()));
    root.GetStnPath(&path);
    Check(KauaiFiles::DeleteDirectory(path.Psz()));
    return 0;
}
