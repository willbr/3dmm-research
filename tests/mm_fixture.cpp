/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include <cstdio>
#include <cstdlib>
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
static void Check(bool value)
{
    if (!value)
        std::exit(1);
}
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    String path;
    path.SetSzs(argv[1]);
    Filename filename;
    Check(filename.FBuildFromPath(&path));
    PChunkyFile file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    Check(file != pvNil);
    const int variant = std::atoi(argv[2]);
    const ChunkNumber root = variant == 3 ? 99 : 7;
    const byte payload[] = {0, 34, 92, 255};
    // The same IDs/data, written in opposite physical order. Add a hole to the
    // second archive so equality cannot come from identical fixture files.
    if (variant == 1)
    {
        byte gap[8192] = {};
        Check(file->FPutPv(gap, sizeof(gap), 'GAP ', 1));
        Check(file->FPutPv(const_cast<byte *>(payload), sizeof(payload), 'ZZZZ', 42));
    }
    ChunkNumber created;
    Check(MovieData::FCreateRoot(file, &created));
    file->Move(kctgMvie, created, kctgMvie, root);
    SceneData scene;
    scene.lastFrame = variant == 2 ? 13 : 12;
    String sceneName;
    sceneName.SetSzs(PszLit("Scene \"one\"\\\n"));
    scene.SetName(&sceneName);
    Check(scene.FWrite(file, 5));
    ActorChunkOnFile actor = {};
    actor.bo = kboCur;
    actor.osk = koskCur;
    actor.arid = 23;
    actor.nfrmFirst = 1;
    actor.nfrmLast = 12;
    actor.tagTmpl.sid = 1;
    actor.tagTmpl.ctg = kctgTmpl;
    actor.tagTmpl.cno = 8;
    Check(ActorData::FWriteHeader(file, 6, &actor));
    String font;
    font.SetSzs(PszLit("Arial"));
    RichTextProperties defaults = {kboCur, koskCur, 200, 12, 0};
    ChunkIdentification richText;
    Check(RichTextData::FCreateRoot(file, &richText, &defaults, &font));
    TextBoxData caption;
    caption.firstFrame = 2;
    caption.maxFrame = 10;
    caption.bounds.Set(1, 2, 101, 52);
    caption.textChild = 9;
    Check(caption.FWrite(file, 8));
    if (variant != 6)
        Check(file->FAdoptChild(kctgTbox, 8, richText.ctg, richText.cno, caption.textChild));
    if (variant != 1)
        Check(file->FPutPv(const_cast<byte *>(payload), sizeof(payload), 'ZZZZ', 42));
    Check(file->FAdoptChild(kctgMvie, root, kctgScen, 5, 0));
    Check(file->FAdoptChild(kctgScen, 5, kctgActr, 6, 2));
    Check(file->FAdoptChild(kctgScen, 5, 'ZZZZ', 42, 1));
    Check(file->FAdoptChild(kctgScen, 5, kctgTbox, 8, 3));
    if (variant == 5)
    {
        byte repeated[1024];
        FillPb(repeated, sizeof(repeated), 'a');
        Check(file->FPutPv(repeated, sizeof(repeated), 'ZZZZ', 42));
        Check(file->FPackData('ZZZZ', 42));
    }
    if (variant == 1)
        file->Delete('GAP ', 1);
    if (variant == 4)
    {
        byte invalid = 0;
        Check(file->FPutPv(&invalid, 1, kctgScen, 5));
    }
    Check(file->FSave('TEST'));
    ReleasePpo(&file);
    return 0;
}
