/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Geometry math tests for kauai's PT, RC, and Region.

    These types are the ones most likely to be silently miscompiled under
    LP64 (long becomes 64-bit on Linux/macOS x86_64) -- on-disk struct
    layouts and arithmetic overflow boundaries both shift. This suite is
    the regression net for Project 1's sized-types audit.

    Pure unit tests: no I/O, no GUI, no fixtures. Models after ut.cpp.

***************************************************************************/
#include "kauai_core.h"
#include "region.h"
#include <stdio.h>
ASSERTNAME

/* CLI assertion stubs (gui-side appb.cpp is not linked here). */
void WarnProc(schar *f, long l, schar *m)
{
    fprintf(stderr, "WARN %s:%ld %s\n", f ? f : "?", l, m ? m : "");
}
bool FAssertProc(schar *f, long l, schar *m, void *, long)
{
    fprintf(stderr, "ASSERT %s:%ld %s\n", f ? f : "?", l, m ? m : "");
    return false;
}

static int g_pass = 0;
static int g_fail = 0;
static int g_local_fail = 0;

#define EXPECT_TRUE(cond)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            printf("    FAIL %s:%d  EXPECT_TRUE(%s)\n", __FILE__, __LINE__, #cond);                                    \
            g_local_fail++;                                                                                            \
        }                                                                                                              \
    } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(actual, expected)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        long _a = (long)(actual), _e = (long)(expected);                                                               \
        if (_a != _e)                                                                                                  \
        {                                                                                                              \
            printf("    FAIL %s:%d  EXPECT_EQ(%s, %s)  got=%ld  expected=%ld\n", __FILE__, __LINE__, #actual,          \
                   #expected, _a, _e);                                                                                 \
            g_local_fail++;                                                                                            \
        }                                                                                                              \
    } while (0)

#define EXPECT_RC(rc, l, t, r, b)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((rc).xpLeft != (l) || (rc).ypTop != (t) || (rc).xpRight != (r) || (rc).ypBottom != (b))                    \
        {                                                                                                              \
            printf("    FAIL %s:%d  EXPECT_RC  got=(%ld,%ld,%ld,%ld)  expected=(%ld,%ld,%ld,%ld)\n", __FILE__,         \
                   __LINE__, (rc).xpLeft, (rc).ypTop, (rc).xpRight, (rc).ypBottom, (long)(l), (long)(t), (long)(r),    \
                   (long)(b));                                                                                         \
            g_local_fail++;                                                                                            \
        }                                                                                                              \
    } while (0)

#define EXPECT_PT(pt, x, y)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((pt).xp != (x) || (pt).yp != (y))                                                                          \
        {                                                                                                              \
            printf("    FAIL %s:%d  EXPECT_PT  got=(%ld,%ld)  expected=(%ld,%ld)\n", __FILE__, __LINE__, (pt).xp,      \
                   (pt).yp, (long)(x), (long)(y));                                                                     \
            g_local_fail++;                                                                                            \
        }                                                                                                              \
    } while (0)

#define RUN(test)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        g_local_fail = 0;                                                                                              \
        test();                                                                                                        \
        if (g_local_fail == 0)                                                                                         \
        {                                                                                                              \
            printf("PASS  %s\n", #test);                                                                               \
            g_pass++;                                                                                                  \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            printf("FAIL  %s  (%d assertion%s)\n", #test, g_local_fail, g_local_fail == 1 ? "" : "s");                 \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

/****************************************
    PT tests
****************************************/

static void TestPtCtor()
{
    PT pt(3, 7);
    EXPECT_EQ(pt.xp, 3);
    EXPECT_EQ(pt.yp, 7);
}

static void TestPtEquality()
{
    PT a(1, 2), b(1, 2), c(1, 3), d(2, 2);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

static void TestPtArithmetic()
{
    PT a(10, 20), b(3, 5);
    PT sum = a + b;
    EXPECT_PT(sum, 13, 25);
    PT diff = a - b;
    EXPECT_PT(diff, 7, 15);

    PT acc(10, 20);
    acc += b;
    EXPECT_PT(acc, 13, 25);
    acc -= b;
    EXPECT_PT(acc, 10, 20);

    acc.Offset(-100, 200);
    EXPECT_PT(acc, -90, 220);
}

static void TestPtMap()
{
    // Map (50, 50) from a (0, 0, 100, 100) source rect to a (200, 300, 600, 700)
    // dest rect: should land at the center of the dest -> (400, 500).
    RC src(0, 0, 100, 100);
    RC dst(200, 300, 600, 700);
    PT pt(50, 50);
    pt.Map(&src, &dst);
    EXPECT_PT(pt, 400, 500);
}

static void TestPtMapPreservesCorners()
{
    // Mapping a source corner to its dest counterpart must be exact.
    RC src(10, 20, 110, 220);
    RC dst(0, 0, 200, 400);
    PT tl(10, 20), br(110, 220);
    tl.Map(&src, &dst);
    br.Map(&src, &dst);
    EXPECT_PT(tl, 0, 0);
    EXPECT_PT(br, 200, 400);
}

/****************************************
    RC tests
****************************************/

static void TestRcCtorAndAccessors()
{
    RC rc(10, 20, 110, 220);
    EXPECT_EQ(rc.xpLeft, 10);
    EXPECT_EQ(rc.ypTop, 20);
    EXPECT_EQ(rc.xpRight, 110);
    EXPECT_EQ(rc.ypBottom, 220);
    EXPECT_EQ(rc.Dxp(), 100);
    EXPECT_EQ(rc.Dyp(), 200);
    EXPECT_EQ(rc.XpCenter(), 60);
    EXPECT_EQ(rc.YpCenter(), 120);
}

static void TestRcSetAndZero()
{
    RC rc(1, 2, 3, 4);
    rc.Set(10, 20, 30, 40);
    EXPECT_RC(rc, 10, 20, 30, 40);
    rc.Zero();
    EXPECT_RC(rc, 0, 0, 0, 0);
    EXPECT_TRUE(rc.FEmpty());
}

static void TestRcEmpty()
{
    RC empty(0, 0, 0, 0);
    EXPECT_TRUE(empty.FEmpty());
    RC degenerate_x(10, 20, 10, 30);
    EXPECT_TRUE(degenerate_x.FEmpty());
    RC degenerate_y(10, 20, 30, 20);
    EXPECT_TRUE(degenerate_y.FEmpty());
    RC nonempty(0, 0, 1, 1);
    EXPECT_FALSE(nonempty.FEmpty());
    RC inverted(20, 20, 10, 30);
    EXPECT_TRUE(inverted.FEmpty());
}

static void TestRcMax()
{
    RC rc;
    rc.Max();
    EXPECT_TRUE(rc.FMax());
    EXPECT_FALSE(rc.FEmpty());
    EXPECT_TRUE(rc.Dxp() > 0);
    EXPECT_TRUE(rc.Dyp() > 0);
}

static void TestRcEquality()
{
    RC a(1, 2, 3, 4), b(1, 2, 3, 4), c(1, 2, 3, 5);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

static void TestRcOffsetByPt()
{
    PT delta(5, -3);
    RC rc(10, 20, 30, 40);
    RC offset = rc + delta;
    EXPECT_RC(offset, 15, 17, 35, 37);
    rc += delta;
    EXPECT_RC(rc, 15, 17, 35, 37);
    rc -= delta;
    EXPECT_RC(rc, 10, 20, 30, 40);
}

static void TestRcCornerAccessors()
{
    RC rc(10, 20, 30, 40);
    PT tl = rc.PtTopLeft();
    EXPECT_PT(tl, 10, 20);
    PT br = rc.PtBottomRight();
    EXPECT_PT(br, 30, 40);
    PT tr = rc.PtTopRight();
    EXPECT_PT(tr, 30, 20);
    PT bl = rc.PtBottomLeft();
    EXPECT_PT(bl, 10, 40);
}

static void TestRcIntersect()
{
    RC a(10, 10, 50, 50), b(30, 30, 70, 70), result;
    EXPECT_TRUE(result.FIntersect(&a, &b));
    EXPECT_RC(result, 30, 30, 50, 50);

    // Touching but not overlapping = empty intersection
    RC c(10, 10, 30, 30), d(30, 10, 50, 30);
    EXPECT_FALSE(result.FIntersect(&c, &d));
    EXPECT_TRUE(result.FEmpty());

    // Disjoint
    RC e(10, 10, 20, 20), f(100, 100, 200, 200);
    EXPECT_FALSE(result.FIntersect(&e, &f));
    EXPECT_TRUE(result.FEmpty());

    // a fully contains b -> intersection is b
    RC outer(0, 0, 100, 100), inner(20, 30, 40, 50);
    EXPECT_TRUE(result.FIntersect(&outer, &inner));
    EXPECT_RC(result, 20, 30, 40, 50);
}

static void TestRcIntersectInPlace()
{
    RC a(10, 10, 50, 50), b(30, 30, 70, 70);
    EXPECT_TRUE(a.FIntersect(&b));
    EXPECT_RC(a, 30, 30, 50, 50);
}

static void TestRcUnion()
{
    RC a(10, 10, 50, 50), b(30, 30, 70, 70), result;
    result.Union(&a, &b);
    EXPECT_RC(result, 10, 10, 70, 70);

    // Empty input rects are dropped, not folded into the bounding box --
    // (0,0,0,0) does NOT pull the union back to the origin. See the
    // explicit FEmpty check in RC::Union at utilint.cpp.
    RC c(0, 0, 0, 0), d(10, 10, 20, 20);
    result.Union(&c, &d);
    EXPECT_RC(result, 10, 10, 20, 20);

    // Both inputs empty -> result stays empty (assignment from prc1 then
    // the FEmpty guard skips prc2).
    RC e1(0, 0, 0, 0), e2(50, 50, 50, 50);
    result.Union(&e1, &e2);
    EXPECT_TRUE(result.FEmpty());

    // Disjoint -> bounding box covers both
    RC e(0, 0, 10, 10), f(100, 100, 110, 110);
    result.Union(&e, &f);
    EXPECT_RC(result, 0, 0, 110, 110);
}

static void TestRcUnionInPlace()
{
    RC a(10, 10, 50, 50), b(30, 30, 70, 70);
    a.Union(&b);
    EXPECT_RC(a, 10, 10, 70, 70);
}

static void TestRcInset()
{
    RC rc(10, 20, 110, 220);
    RC inset;
    inset.InsetCopy(&rc, 5, 10);
    EXPECT_RC(inset, 15, 30, 105, 210);
    EXPECT_RC(rc, 10, 20, 110, 220); // source unchanged

    rc.Inset(5, 10);
    EXPECT_RC(rc, 15, 30, 105, 210);

    // Negative inset enlarges
    rc.Inset(-5, -10);
    EXPECT_RC(rc, 10, 20, 110, 220);
}

static void TestRcOffset()
{
    RC rc(10, 20, 110, 220);
    RC offset;
    offset.OffsetCopy(&rc, 100, 200);
    EXPECT_RC(offset, 110, 220, 210, 420);
    EXPECT_RC(rc, 10, 20, 110, 220);

    rc.Offset(100, 200);
    EXPECT_RC(rc, 110, 220, 210, 420);
}

static void TestRcOffsetToOrigin()
{
    RC rc(10, 20, 110, 220);
    rc.OffsetToOrigin();
    EXPECT_RC(rc, 0, 0, 100, 200);

    RC neg(-50, -100, -10, -20);
    neg.OffsetToOrigin();
    EXPECT_RC(neg, 0, 0, 40, 80);
}

static void TestRcFPtIn()
{
    RC rc(10, 20, 110, 220);
    EXPECT_TRUE(rc.FPtIn(10, 20));   // top-left corner: in
    EXPECT_TRUE(rc.FPtIn(50, 50));   // interior: in
    EXPECT_TRUE(rc.FPtIn(109, 219)); // just inside bottom-right: in
    EXPECT_FALSE(rc.FPtIn(110, 220));   // bottom-right corner is exclusive: out
    EXPECT_FALSE(rc.FPtIn(9, 50));   // left of left edge: out
    EXPECT_FALSE(rc.FPtIn(50, 19));  // above top edge: out
    EXPECT_FALSE(rc.FPtIn(110, 50)); // on right edge (exclusive): out
    EXPECT_FALSE(rc.FPtIn(50, 220)); // on bottom edge (exclusive): out
}

static void TestRcFContains()
{
    RC outer(0, 0, 100, 100), inner(10, 20, 50, 60), overlap(50, 50, 150, 150);
    EXPECT_TRUE(outer.FContains(&inner));
    EXPECT_FALSE(outer.FContains(&overlap));
    EXPECT_TRUE(outer.FContains(&outer));
}

static void TestRcLwArea()
{
    RC rc(0, 0, 100, 200);
    EXPECT_EQ(rc.LwArea(), 20000);
    RC empty(0, 0, 0, 0);
    EXPECT_EQ(empty.LwArea(), 0);
    RC negative(50, 50, 10, 10);
    EXPECT_EQ(negative.LwArea(), 0);
}

static void TestRcCenterOnPt()
{
    RC rc(0, 0, 20, 40);
    rc.CenterOnPt(100, 200);
    EXPECT_EQ(rc.XpCenter(), 100);
    EXPECT_EQ(rc.YpCenter(), 200);
    EXPECT_EQ(rc.Dxp(), 20);
    EXPECT_EQ(rc.Dyp(), 40);
}

static void TestRcCenterOnRc()
{
    RC inner(0, 0, 20, 40);
    RC outer(100, 200, 300, 400);
    inner.CenterOnRc(&outer);
    EXPECT_EQ(inner.XpCenter(), outer.XpCenter());
    EXPECT_EQ(inner.YpCenter(), outer.YpCenter());
    EXPECT_EQ(inner.Dxp(), 20);
    EXPECT_EQ(inner.Dyp(), 40);
}

static void TestRcPinPt()
{
    RC rc(10, 20, 110, 220);
    PT pt;

    pt = PT(50, 50);
    rc.PinPt(&pt);
    EXPECT_PT(pt, 50, 50); // inside, unchanged

    pt = PT(0, 0);
    rc.PinPt(&pt);
    EXPECT_PT(pt, 10, 20); // pinned to top-left

    pt = PT(500, 500);
    rc.PinPt(&pt);
    EXPECT_PT(pt, 109, 219); // pinned to inclusive bottom-right
}

/****************************************
    Region tests
****************************************/

static void TestRegionEmptyOnNew()
{
    PRegion pregn = Region::PregnNew();
    EXPECT_TRUE(pregn != pvNil);
    EXPECT_TRUE(pregn->FEmpty());
    EXPECT_TRUE(pregn->FIsRc());
    ReleasePpo(&pregn);
}

static void TestRegionFromRect()
{
    RC rc(10, 20, 110, 220), rcOut;
    PRegion pregn = Region::PregnNew(&rc);
    EXPECT_TRUE(pregn != pvNil);
    EXPECT_FALSE(pregn->FEmpty());
    EXPECT_TRUE(pregn->FIsRc(&rcOut));
    EXPECT_RC(rcOut, 10, 20, 110, 220);
    ReleasePpo(&pregn);
}

static void TestRegionSetRc()
{
    PRegion pregn = Region::PregnNew();
    RC rc(0, 0, 50, 50), rcOut;
    pregn->SetRc(&rc);
    EXPECT_TRUE(pregn->FIsRc(&rcOut));
    EXPECT_RC(rcOut, 0, 0, 50, 50);

    // SetRc(pvNil) makes the region empty
    pregn->SetRc(pvNil);
    EXPECT_TRUE(pregn->FEmpty());
    ReleasePpo(&pregn);
}

static void TestRegionOffset()
{
    RC rc(0, 0, 100, 100), rcOut;
    PRegion pregn = Region::PregnNew(&rc);
    pregn->Offset(50, -20);
    EXPECT_TRUE(pregn->FIsRc(&rcOut));
    EXPECT_RC(rcOut, 50, -20, 150, 80);
    ReleasePpo(&pregn);
}

static void TestRegionUnionRc()
{
    RC a(0, 0, 50, 50);
    PRegion pregn = Region::PregnNew(&a);
    RC b(40, 40, 100, 100);
    EXPECT_TRUE(pregn->FUnionRc(&b));
    EXPECT_FALSE(pregn->FEmpty());
    // Two overlapping rects unioned no longer fits a single RC
    EXPECT_FALSE(pregn->FIsRc());
    ReleasePpo(&pregn);
}

static void TestRegionIntersectRc()
{
    RC a(10, 10, 50, 50), rcOut;
    PRegion pregn = Region::PregnNew(&a);
    RC b(30, 30, 70, 70);
    EXPECT_TRUE(pregn->FIntersectRc(&b));
    EXPECT_TRUE(pregn->FIsRc(&rcOut));
    EXPECT_RC(rcOut, 30, 30, 50, 50);
    ReleasePpo(&pregn);
}

static void TestRegionDiffRc()
{
    // Subtract a fully-contained rect -> result is no longer a single RC
    RC outer(0, 0, 100, 100);
    PRegion pregn = Region::PregnNew(&outer);
    RC hole(40, 40, 60, 60);
    EXPECT_TRUE(pregn->FDiffRc(&hole));
    EXPECT_FALSE(pregn->FEmpty());
    EXPECT_FALSE(pregn->FIsRc());
    ReleasePpo(&pregn);
}

static void TestRegionDiffEqual()
{
    // A region minus itself is empty
    RC rc(10, 10, 50, 50);
    PRegion pregn = Region::PregnNew(&rc);
    EXPECT_TRUE(pregn->FDiffRc(&rc));
    EXPECT_TRUE(pregn->FEmpty());
    ReleasePpo(&pregn);
}

/****************************************
    Main + assert hooks
****************************************/

void __cdecl main(long, char **)
{
    printf("=== kauai geometry tests ===\n");

    RUN(TestPtCtor);
    RUN(TestPtEquality);
    RUN(TestPtArithmetic);
    RUN(TestPtMap);
    RUN(TestPtMapPreservesCorners);

    RUN(TestRcCtorAndAccessors);
    RUN(TestRcSetAndZero);
    RUN(TestRcEmpty);
    RUN(TestRcMax);
    RUN(TestRcEquality);
    RUN(TestRcOffsetByPt);
    RUN(TestRcCornerAccessors);
    RUN(TestRcIntersect);
    RUN(TestRcIntersectInPlace);
    RUN(TestRcUnion);
    RUN(TestRcUnionInPlace);
    RUN(TestRcInset);
    RUN(TestRcOffset);
    RUN(TestRcOffsetToOrigin);
    RUN(TestRcFPtIn);
    RUN(TestRcFContains);
    RUN(TestRcLwArea);
    RUN(TestRcCenterOnPt);
    RUN(TestRcCenterOnRc);
    RUN(TestRcPinPt);

    RUN(TestRegionEmptyOnNew);
    RUN(TestRegionFromRect);
    RUN(TestRegionSetRc);
    RUN(TestRegionOffset);
    RUN(TestRegionUnionRc);
    RUN(TestRegionIntersectRc);
    RUN(TestRegionDiffRc);
    RUN(TestRegionDiffEqual);

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    exit(g_fail == 0 ? 0 : 1);
}

// Stub: kauai's appbwin.cpp WinMain references FrameMain. We never reach
// WinMain (subsystem:console enters at main), but the symbol must resolve.
void FrameMain(void)
{
}
