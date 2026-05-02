/*
 * movie-save-load-test  --  reproduce + regression-test memory access errors
 * in the Movie save/load round-trip.
 *
 * Steps:
 *   1. Initialize the bare minimum app environment (vpappb, vptagm).
 *   2. PmvieNew(pmcc, pvNil)             -- create a blank movie.
 *   3. _FMakeCrfValid()                  -- build the temp autosave file.
 *      (called indirectly via the first FNewScenInsCore.)
 *   4. FNewScenInsCore(0)                -- give it one empty scene.
 *   5. FSaveToFni(<temp>.3mm, fTrue)     -- save to a real on-disk movie.
 *   6. ReleasePpo(&pmvie).
 *   7. PmvieNew(pmcc, &fni)              -- reopen.
 *   8. ReleasePpo(&pmvie) again.
 *
 * If any step segfaults or fires an assert, we print where and exit non-zero.
 *
 * Links engine + kauai (gui side too -- ApplicationBase lives there).
 */

#include "soc.h"
ASSERTNAME

#include <stdio.h>

/* vptagm is normally defined in src/studio/utest.cpp. Headless test
 * provides its own. */
PTagManager vptagm;

/* WinMain (in kauai/src/appbwin.cpp) references FrameMain -- but we
 * never run WinMain since we provide our own main() and link as a
 * console subsystem. The symbol must still resolve. */
void __cdecl FrameMain(void) {}

/* Placeholder for the studio MCP-server check; appbwin.cpp's WinMain
 * calls it, but again WinMain is unreachable here. */
extern "C" int Mcp_FEnabledFromCmdLine(const char *) { return 0; }

/* TagManager wants a callback for asking the user to insert a CD when
 * source content is on a missing volume. For a blank movie there is
 * no source content, so this callback is never actually invoked. */
static bool FInsertCdStub(PString)
{
    return fFalse;
}

/* Minimal MovieClientCallbacks. All virtuals on the base class have
 * default no-op implementations, so we only need to construct it. */
class TestMcc : public MovieClientCallbacks
{
  public:
    TestMcc() : MovieClientCallbacks(640, 480, 0) {}
    /* GetFniSave is the one virtual the save path consults: it asks
     * the client for a filename when the user does Save-As. We never
     * hit Save-As here (we pass an explicit pfni to FSaveToFni), so
     * the inherited fFalse return is fine. */
};

/* Minimal ApplicationBase: BeginLongOp/EndLongOp is the only thing
 * Movie save/load reaches into. ApplicationBase's own ctor sets
 * vpappb = this, which is what Movie code uses. */
class TestApp : public ApplicationBase
{
  public:
    TestApp() : ApplicationBase() {}
};

#define LOG(msg) do { fprintf(stderr, "[step] %s\n", msg); fflush(stderr); } while (0)
#define BAIL(msg) do { fprintf(stderr, "[FAIL] %s\n", msg); fflush(stderr); rc = 1; goto LDone; } while (0)

int __cdecl main(int argc, char **argv)
{
    int rc = 0;
    PMovie pmvie = pvNil;
    TestMcc *pmcc = pvNil;
    TestApp *papp = pvNil;
    Filename fniMovie;
    Filename fniHDRoot;
    String stnPath;
    bool fSaved = fFalse;

    LOG("constructing TestApp (sets vpappb)");
    papp = NewObj TestApp();
    if (papp == pvNil)
        BAIL("new TestApp failed");

    LOG("initializing vpcex (CommandExecutionManager) -- needed by Clock::Stop");
    /* vpcex is normally created by ApplicationBase::_FInit. Clock::Stop
     * (called transitively from MovieSoundQueue::~MovieSoundQueue ->
     * StopAll -> _pclok->Stop) does vpcex->RemoveCmh(this, ...) so we
     * have to provide a real CommandExecutionManager. */
    vpcex = CommandExecutionManager::PcexNew(20, 20);
    if (vpcex == pvNil)
        BAIL("CommandExecutionManager::PcexNew failed");

    LOG("initializing vpsndm (SoundManager) -- needed by FSwitchScen");
    /* vpsndm is normally created by ApplicationBase::_FInit. Our test
     * skips _FInit, so we have to set it up by hand. Movie::FSwitchScen
     * calls vpsndm->StopAll() unconditionally; without this the scene
     * insert path null-derefs. */
    vpsndm = SoundManager::PsndmNew();
    if (vpsndm == pvNil)
        BAIL("SoundManager::PsndmNew failed");

    LOG("initializing vptagm");
    /* TagManager wants a directory that exists. Use the current
     * directory; on Windows that's wherever the test is invoked
     * from. */
    if (argc >= 2)
    {
        stnPath.SetSzs(argv[1]);
    }
    else
    {
        stnPath.SetSzs(PszLit("."));
    }
    if (!fniHDRoot.FBuildFromPath(&stnPath))
        BAIL("FBuildFromPath(hd-root) failed");
    vptagm = TagManager::PtagmNew(&fniHDRoot, FInsertCdStub, 0);
    if (vptagm == pvNil)
        BAIL("PtagmNew failed");

    LOG("constructing TestMcc");
    pmcc = NewObj TestMcc();
    if (pmcc == pvNil)
        BAIL("new TestMcc failed");

    LOG("PmvieNew(pmcc, pvNil) -- blank movie");
    pmvie = Movie::PmvieNew(fFalse, pmcc, pvNil, cnoNil);
    if (pmvie == pvNil)
        BAIL("PmvieNew(blank) returned pvNil");

    LOG("FNewScenInsCore(0) -- give the movie one empty scene");
    if (!pmvie->FNewScenInsCore(0))
        BAIL("FNewScenInsCore(0) failed");

    LOG("computing temp output path");
    {
        String stnOut;
        if (argc >= 3)
        {
            stnOut.SetSzs(argv[2]);
        }
        else
        {
            stnOut.SetSzs(PszLit("movie-save-load-test.3mm"));
        }
        if (!fniMovie.FBuildFromPath(&stnOut))
            BAIL("FBuildFromPath(out) failed");
    }

    LOG("FSaveToFni(<out>, fTrue) -- save");
    if (!pmvie->FSaveToFni(&fniMovie, fTrue))
        BAIL("FSaveToFni failed");
    fSaved = fTrue;

    LOG("ReleasePpo(&pmvie) -- close");
    ReleasePpo(&pmvie);

    LOG("PmvieNew(pmcc, &fni) -- reopen");
    pmvie = Movie::PmvieNew(fFalse, pmcc, &fniMovie, cnoNil);
    if (pmvie == pvNil)
        BAIL("PmvieNew(reopen) returned pvNil");

    LOG("ReleasePpo(&pmvie) -- close (after reopen)");
    ReleasePpo(&pmvie);

    fprintf(stderr, "[ OK ] save -> close -> reopen -> close round-trip clean\n");

LDone:
    ReleasePpo(&pmvie);
    if (pmcc != pvNil)
        ReleasePpo(&pmcc);
    ReleasePpo(&vptagm);
    ReleasePpo(&vpsndm);
    ReleasePpo(&vpcex);
    if (papp != pvNil)
        delete papp;
    /* If save succeeded but the test failed later, leave the file on
     * disk so the user can inspect it. Otherwise clean up. */
    if (fSaved && rc == 0)
    {
        String stnPathDelete;
        fniMovie.GetStnPath(&stnPathDelete);
        fprintf(stderr, "[note] saved movie left at %s\n", stnPathDelete.Psz());
    }
    return rc;
}
