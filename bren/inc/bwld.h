/***************************************************************************

    bwld.h: BRender world class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> World

***************************************************************************/
#ifndef BWLD_H
#define BWLD_H
#include "mm/world_buffers.h"
#include "mm/world_camera.h"
#include "mm/world_render.h"
#include "mm/world_export.h"

namespace BRender {

// Callback function per BACT when it's rendered, passing the 2D bounds
typedef void FNBACTREND(PBACT pbact, RC *prc);
typedef FNBACTREND *PFNBACTREND;

// Callback function per root BACT when we begin rendering
typedef void FNBEGINREND(PBACT pbact);
typedef FNBEGINREND *PFNBEGINREND;

// Callback function per root BACT to get the rendered rectangle
typedef void FNGETRECT(PBACT pbact, RC *prc);
typedef FNGETRECT *PFNGETRECT;

/****************************************
    The BRender world class
****************************************/
typedef class World *PWorld;
#define World_PAR BASE
#define kclsWorld 'BWLD'
class World : public World_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static bool _fBRenderInited; // Whether BrBegin() has been called
    MMWorldBuffers _buffers;
    MMWorldBufferServices _BufferServices();
    MMWorldCamera _camera; // authoritative camera and closest-pick state
    MMWorldCameraServices _CameraServices();
    MMWorldRender _render;
    MMWorldRenderServices _RenderServices(void *context, MMMovieSession *movie = nullptr,
                                          const MMMovieLiveFrame *position = nullptr);
    BACT _bactWorld;             // The world root actor
    BACT _bactCamera;            // The camera actor
    BCAM _bcam;                  // The camera data
    BPMP _bpmpRGB, _bpmpZ; // BRender views refreshed from the C89 buffers before use.
    static long _defaultRenderScale;
    PFNBEGINREND _pfnbeginrend;  // Callback to each actor before rendering
    int (*_pfnreadbody)(PBACT, MMRenderBody *) = nullptr;
    void (*_pfnpreparebody)(const MMRenderBody *) = nullptr;
    PFNBACTREND _pfnbactrend;    // Callback when an actor is rendered
    PFNGETRECT _pfngetrect;      // Callback to get an actor's bounding rect

  protected:
    World(void)
    {
    }
    bool _FInit(long dxp, long dyp, long scale, bool fHalfX, bool fHalfY);
    bool _FSetBufferMode(bool fHalfX, bool fHalfY, long scale);
    void _SyncPixelmaps();
#ifdef DEBUG
    MMWorldExportServices _ExportServices(void *context);
#endif // DEBUG
    static int BR_CALLBACK _FFilter(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *pbvec3RayPos, BVEC3 *pbvec3RayDir,
                                    BRS dzpNear, BRS dzpFar, void *pbwld);
    static void BR_CALLBACK _ActorRendered(PBACT pbact, PBMDL pbmdl, PBMTL pbmtl, br_uint_8 bStyle,
                                           br_matrix4 *pbmat4ModelToScreen, br_int_32 bounds[4]);

  public:
    // Constructors and destructors
    static PWorld PbwldNew(long dxp, long dyp, bool fHalfX = fFalse, bool fhalfY = fFalse);
    ~World();
    static void CloseBRender(void);

    // Dirtying the BRender world and bitmap
    void MarkDirty(void)
    {
        mm_world_render_dirty(&_render);
    }
    void MarkRenderedRegn(PGraphicsObject pgob, long dxp, long dyp);

    // Background stuff
    bool FSetBackground(PChunkyResourceFile pcrf, ChunkTagOrType ctgRGB, ChunkNumber cnoRGB, ChunkTagOrType ctgZ, ChunkNumber cnoZ);
    void SetCamera(BMAT34 *pbmat34, BRS zrHither, BRS zrYon, BRA aFov);
    void GetCamera(BMAT34 *pbmat34, BRS *pzrHither = pvNil, BRS *pzrYon = pvNil, BRA *paFov = pvNil);

    // Actor stuff
    void AddActor(BACT *pbact);
    bool FClickedActor(long xp, long yp, BACT **ppbact);
    void IterateActorsInPt(br_pick2d_cbfn *pfnCallback, void *pvArg, long xp, long yp);
    void SetBeginRenderCallback(PFNBEGINREND pfnbeginrend)
    {
        _pfnbeginrend = pfnbeginrend;
    }
    void SetBodyRenderCallbacks(int (*read)(PBACT, MMRenderBody *), void (*prepare)(const MMRenderBody *))
    {
        _pfnreadbody = read;
        _pfnpreparebody = prepare;
    }
    void SetActorRenderedCallback(PFNBACTREND pfnbactrend)
    {
        _pfnbactrend = pfnbactrend;
    }
    void SetGetRcCallback(PFNGETRECT pfngetrect)
    {
        _pfngetrect = pfngetrect;
    }

    // Rendering stuff
    static bool _fRenderWireframe;
    static bool _fFlatShading;
    static void SetFlatShading(bool enabled)
    {
        _fFlatShading = enabled;
    }
    static bool FlatShading()
    {
        return _fFlatShading;
    }
    static bool RenderWireframe(void)
    {
        return _fRenderWireframe;
    }
    static void SetRenderWireframe(bool fOn)
    {
        _fRenderWireframe = fOn;
    }
    static bool _fNoTexture;
    static bool NoTexture(void)
    {
        return _fNoTexture;
    }
    static void SetNoTexture(bool fOn);
    bool FSetHalfMode(bool fHalfX, bool fHalfY);
    // Runtime preference only; camera and movie-file coordinates stay native.
    bool FSetRenderScale(long scale);
    bool FSetRenderResolution(long scale, bool reduced);
    long RenderScale() const
    {
        return _buffers.scale;
    }
    static long DefaultRenderScale()
    {
        return _defaultRenderScale;
    }
    static void SetDefaultRenderScale(long scale)
    {
        if (scale == 1 || scale == 2 || scale == 4)
            _defaultRenderScale = scale;
    }
    void GetRenderSize(long *width, long *height) const
    {
        *width = _buffers.buffer_width;
        *height = _buffers.buffer_height;
    }
    bool FHalfX(void)
    {
        return _buffers.half_x != 0;
    }
    bool FHalfY(void)
    {
        return _buffers.half_y != 0;
    }
    void Render(MMMovieSession *movie = nullptr, const MMMovieLiveFrame *position = nullptr);
    void Prerender(MMMovieSession *movie = nullptr, const MMMovieLiveFrame *position = nullptr);
    void Unprerender(void);
    void Draw(PGraphicsEnvironment pgnv, RC *prcClip, long dxp, long dyp);

#ifdef DEBUG
    bool FWriteBmp(PFilename pfni);
    bool FWriteBmpScaled(PFilename pfni, long scale);
#endif // DEBUG
};

} // end of namespace BRender

#endif BWLD_H
