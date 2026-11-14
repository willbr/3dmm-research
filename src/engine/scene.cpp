/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

  scene.cpp

  Author: Sean Selitrennikoff

  Date: August, 1994

  This file contains all functionality for scene manipulation.

  THIS IS A CODE REVIEWED FILE

    Basic scene private classes:

        Scene Chop Undo Object (SceneUndoChop)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoChop

        Scene Background Undo Object (SceneUndoBackground)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoBackground

        Scene Pause Undo Object (SceneUndoPause)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoPause

        Scene Text box Undo Object (SceneUndoText)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoText

        Scene Sound Undo Object (SceneUndoSound)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoSound

        Scene Title Undo Object (SceneUndoTitle)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoTitle

        Scene Transition Undo Object (SceneUndoTransition)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoTransition

***************************************************************************/

#include "soc.h"
#include "body_world.h"
#include "scene_sound.h"
#include "scene_sound_undo.h"
#include "scene_edit_undo.h"
#include "scene_content_undo.h"
#include <cstring>

ASSERTNAME

//
// Scene event types
//
//
// Scene thumbnails
//
const auto kdxpThumbnail = 144;
const auto kdypThumbnail = 81;
const auto kbTransparent = 250;

//
// Scene event
//

static ThreeMMSceneSoundPacketServices ScenePacketServices(PMovie movie = nullptr)
{
    ThreeMMSceneSoundPacketServices s = {};
    s.context = movie;
    s.tag_bytes = size(TagChildPair);
    s.allocate = [](void *, ThreeMMI32 bytes) -> void * {
        void *p = nullptr;
        return FAllocPv(&p, bytes, fmemNil, mprNormal) ? p : nullptr;
    };
    s.copy_record = [](void *, void *destination, const void *source) {
        *static_cast<PTagChildPair>(destination) = *static_cast<const TagChildPair *>(source);
    };
    s.sid = [](void *, const void *record) -> ThreeMMI32 { return static_cast<const TagChildPair *>(record)->tag.sid; };
    s.retain = [](void *, void *record) { TagManager::DupTag(&static_cast<PTagChildPair>(record)->tag); };
    s.clear_cache = [](void *, void *record) { static_cast<PTagChildPair>(record)->tag.pcrf = nullptr; };
    s.read_tag = [](void *, const void *record, ThreeMMU32 *child, ThreeMMTag *out) {
        auto value = static_cast<const TagChildPair *>(record);
        *child = value->chid;
        *out = {value->tag.sid, value->tag.pcrf, value->tag.ctg, value->tag.cno};
    };
    s.write_tag = [](void *, void *record, ThreeMMU32 child, const ThreeMMTag *value) {
        auto out = static_cast<PTagChildPair>(record);
        out->chid = child;
        TagFromCore(&out->tag, *value);
    };
    s.group_data = [](void *, void *group, ThreeMMI32 index) -> void * {
        return static_cast<PGeneralGroup>(group)->QvGet(index);
    };
    s.group_size = [](void *, void *group, ThreeMMI32 index) -> ThreeMMI32 {
        return static_cast<PGeneralGroup>(group)->Cb(index);
    };
    s.resolve = [](void *p, void *record) -> int {
        auto tag = static_cast<PTagChildPair>(record);
        return static_cast<PMovie>(p)->FResolveSndTag(&tag->tag, tag->chid);
    };
    s.fetch = [](void *, void *record) -> void * {
        return vptagm->PbacoFetch(&static_cast<PTagChildPair>(record)->tag, MovieSoundMSND::FReadMsnd);
    };
    s.priority = [](void *, void *sound, int loopTool) -> ThreeMMI32 {
        return static_cast<PMovieSoundMSND>(sound)->Spr(loopTool ? toolLooper : toolSounder);
    };
    s.enqueue = [](void *p, void *sound, int loop, int queue, ThreeMMI32 volume, ThreeMMI32 priority,
                   ThreeMMU32 start) {
        static_cast<PMovie>(p)->Pmsq()->FEnqueue(static_cast<PMovieSoundMSND>(sound), 0, loop != 0, queue != 0, volume,
                                                 priority, fFalse, start);
    };
    s.release = [](void *, void *sound) {
        auto p = static_cast<PMovieSoundMSND>(sound);
        ReleasePpo(&p);
    };
    return s;
}

static ThreeMMSceneSoundValueServices SceneSoundValues(PMovie movie = nullptr)
{
    ThreeMMSceneSoundValueServices v = {};
    v.packets = ScenePacketServices(movie);
    v.release = [](void *, ThreeMMSceneSound *sound) {
        auto p = static_cast<PSceneSoundEvent>(sound);
        ReleasePpsse(&p);
    };
    return v;
}
PSceneSoundEvent SceneSoundFromWire(void *bytes, long byteCount, bool swap)
{
    auto services = ScenePacketServices();
    return static_cast<PSceneSoundEvent>(three_mm_scene_sound_decode(bytes, byteCount, swap, &services));
}
void *SceneSoundToWire(PSceneSoundEvent sound, long *bytes)
{
    auto services = ScenePacketServices();
    ThreeMMI32 byteCount;
    void *out = three_mm_scene_sound_encode(sound, &byteCount, &services);
    *bytes = byteCount;
    return out;
}

//
// Undo object for chopping operation.
//
typedef class SceneUndoChop *PSceneUndoChop;

#define SceneUndoChop_PAR MovieUndo
#define kclsSceneUndoChop 'SUNC'
class SceneUndoChop : public SceneUndoChop_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    ChunkNumber _cno;
    PChunkyResourceFile _pcrf;
    SceneUndoChop(void)
    {
    }

  public:
    static PSceneUndoChop PsuncNew(void);
    ~SceneUndoChop(void);

    bool FSave(PScene pscen);

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

//
// Undo object for background operations
//


//
// Undo object for transition operations
//
typedef class SceneUndoTransition *PSceneUndoTransition;

#define SceneUndoTransition_PAR MovieUndo
#define kclsSceneUndoTransition 'SUNR'
class SceneUndoTransition : public SceneUndoTransition_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    TRANS _trans;
    SceneUndoTransition(void)
    {
    }

  public:
    static PSceneUndoTransition PsunrNew(void);
    ~SceneUndoTransition(void);

    void SetTrans(TRANS trans)
    {
        _trans = trans;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

//
// Undo object for pause operations
//


//
// Undo object for text box operations
//


//
// Undo object for sound operations
//
//
// Undo object for title operations
//
typedef class SceneUndoTitle *PSceneUndoTitle;

#define SceneUndoTitle_PAR MovieUndo
#define kclsSceneUndoTitle 'SUNT'
class SceneUndoTitle : public SceneUndoTitle_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    String _stn;
    SceneUndoTitle(void)
    {
    }

  public:
    static PSceneUndoTitle PsuntNew(void);
    ~SceneUndoTitle(void);

    void SetName(PString pstn)
    {
        AssertPo(pstn, 0);
        _stn = *pstn;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

RTCLASS(Scene)
RTCLASS(SceneUndoTitle)
RTCLASS(SceneUndoSound)
RTCLASS(SceneActorUndo)
RTCLASS(SceneUndoBackground)
RTCLASS(SceneUndoPause)
RTCLASS(SceneUndoText)
RTCLASS(SceneUndoChop)
RTCLASS(SceneUndoTransition)

static void SceneOwnerDiagnostic(void *, int issue)
{
    switch (issue)
    {
    case THREE_MM_SCENE_BAD_FRAME:
        Assert(false, "Invalid event in event stream.");
        break;
    case THREE_MM_SCENE_BAD_START:
        Bug("Invalid event in event stream.");
        break;
    case THREE_MM_SCENE_UNKNOWN_EVENT:
        Bug("Unknown event type");
        break;
    case THREE_MM_SCENE_BAD_SOUND:
        Assert(false, "Invalid SceneSoundEvent");
        break;
    case THREE_MM_SCENE_BAD_PAYLOAD:
        Bug("Wrong size for scene event payload");
        break;
    case THREE_MM_SCENE_TEXT_HIDE_FAILED:
        Assert(false, "Could not remove a text box");
        break;
    }
}
static ThreeMMSceneSoundServices SceneSoundServices()
{
    static_assert(int(styNil) == 0 && int(styLim) == 5, "Scene sound style bounds");
    ThreeMMSceneSoundServices s = {};
    s.count = [](void *, void *p) -> ThreeMMI32 { return static_cast<PSceneSoundEvent>(p)->ctagc; };
    s.style = [](void *, void *p) -> ThreeMMI32 { return static_cast<PSceneSoundEvent>(p)->sty; };
    s.open = [](void *, void *p, ThreeMMI32 i) -> int {
        return static_cast<PSceneSoundEvent>(p)->Ptag(i)->pcrf != nullptr;
    };
    s.close = [](void *, void *p, ThreeMMI32 i) { TagManager::CloseTag(static_cast<PSceneSoundEvent>(p)->Ptag(i)); };
    s.free = [](void *, void *p) { FreePpv(&p); };
    s.diagnostic = SceneOwnerDiagnostic;
    return s;
}
void Scene::_PublishOwners()
{
    _pmvie = static_cast<PMovie>(_owner.movie);
    _pggsevFrm = static_cast<PGeneralGroup>(_owner.owned[THREE_MM_SCENE_FRAMES]);
    _pggsevStart = static_cast<PGeneralGroup>(_owner.owned[THREE_MM_SCENE_STARTS]);
    _pglpactr = static_cast<PDynamicArray>(_owner.owned[THREE_MM_SCENE_ACTORS]);
    _pglptbox = static_cast<PDynamicArray>(_owner.owned[THREE_MM_SCENE_TEXTS]);
    _pglpactrSelExtra = static_cast<PDynamicArray>(_owner.owned[THREE_MM_SCENE_SELECTION]);
    _pbkgd = static_cast<PBackground>(_owner.owned[THREE_MM_SCENE_BACKGROUND]);
    _pmbmp = static_cast<PMaskedBitmapMBMP>(_owner.owned[THREE_MM_SCENE_THUMBNAIL]);
    _psseBkgd = static_cast<PSceneSoundEvent>(_owner.owned[THREE_MM_SCENE_BACKGROUND_SOUND]);
}
void Scene::_SetOwner(int slot, void *object)
{
    Assert(_owner.owned[slot] == nullptr, "Scene owner slot must be empty");
    auto services = _OwnerServices();
    three_mm_scene_owner_set(&_owner, this, slot, object, &services);
}
void Scene::_DropOwner(int slot)
{
    auto services = _OwnerServices();
    three_mm_scene_owner_drop(&_owner, this, slot, &services);
}
ThreeMMSceneOwnerServices Scene::_OwnerServices()
{
    static_assert(int(fscenPauses) == THREE_MM_SCENE_DEFAULT_DISABLED, "Scene defaults");
    ThreeMMSceneOwnerServices s = {};
    s.pointer_bytes = sizeof(void *);
    s.sound_header_bytes = sizeof(SceneSoundEvent);
    s.sound_tag_bytes = sizeof(TagChildPair);
    s.create_scene = [](void *, void *movie) -> void * { return NewObj Scene(static_cast<PMovie>(movie)); };
    s.create_slot = [](void *, int kind) -> void * {
        if (kind == THREE_MM_SCENE_FRAMES || kind == THREE_MM_SCENE_STARTS)
            return GeneralGroup::PggNew(sizeof(SceneEvent));
        return DynamicArray::PglNew(sizeof(void *), 0);
    };
    s.state = [](void *, void *p) { return &static_cast<PScene>(p)->_owner; };
    s.publish = [](void *, void *p, const ThreeMMSceneOwner *) { static_cast<PScene>(p)->_PublishOwners(); };
    s.reset_cursor = [](void *, void *p) { static_cast<PScene>(p)->_runtime.next_event = 0; };
    s.can_notify = [](void *) -> int { return vpcex != nullptr; };
    s.loaded = [](void *, void *) { vpcex->EnqueueCid(cidSceneLoaded); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.count = [](void *, void *p) -> ThreeMMI32 { return static_cast<GroupBase *>(p)->IvMac(); };
    s.kind = [](void *, void *p, ThreeMMI32 i) -> ThreeMMI32 {
        SceneEvent event;
        static_cast<PGeneralGroup>(p)->GetFixed(i, &event);
        return event.sevt;
    };
    s.data = [](void *, void *p, ThreeMMI32 i, ThreeMMI32 *bytes) -> void * {
        LegacyLong length;
        auto value = static_cast<PGeneralGroup>(p)->QvGet(i, &length);
        *bytes = length;
        return value;
    };
    s.object = [](void *, void *p, ThreeMMI32 i) -> void * {
        auto group = static_cast<PGeneralGroup>(p);
        SceneEvent event;
        group->GetFixed(i, &event);
        if (event.sevt == sevtAddActr)
        {
            PActor actor;
            group->Get(i, &actor);
            AssertPo(actor, 0);
            return actor;
        }
        PTBOX text;
        group->Get(i, &text);
        AssertPo(text, 0);
        return text;
    };
    s.list_at = [](void *, void *p, ThreeMMI32 i) -> void * {
        void *value;
        static_cast<PDynamicArray>(p)->Get(i, &value);
        return value;
    };
    s.actor_visibility = [](void *, void *p, int show) {
        auto actor = static_cast<PActor>(p);
        AssertPo(actor, 0);
        if (show)
            actor->Show();
        else
            actor->Hide();
    };
    s.lights_off = [](void *, void *p) { static_cast<PBackground>(p)->TurnOffLights(); };
    s.text_lifetime = [](void *, void *p, ThreeMMI32 *first) -> int {
        auto text = static_cast<PTBOX>(p);
        AssertPo(text, 0);
        long start, last;
        bool ok = text->FGetLifetime(&start, &last);
        if (ok)
            *first = start;
        return ok;
    };
    s.text_seek = [](void *, void *p, ThreeMMI32 frame) -> int { return static_cast<PTBOX>(p)->FGotoFrame(frame); };
    s.diagnostic = SceneOwnerDiagnostic;
    s.sound = SceneSoundServices();
    return s;
}

/****************************************************
 *
 * Constructor for scenes.  This function is private, use PscenNew()
 * for public construction.
 *
 * Parameters:
 *  pmvie - The movie this scene belongs to.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
Scene::Scene(PMovie pmvie)
{
    AssertNilOrPo(pmvie, 0);
    three_mm_scene_owner_init(&_owner, pmvie);
    _PublishOwners();
    three_mm_scene_runtime_init(&_runtime);
}

/****************************************************
 *
 * Exported constructor for scenes.
 *
 * Parameters:
 *	pmvie - The movie this scene belongs to.
 *
 *
 * Returns:
 *  pvNil, on failure, else a pointer to an allocated Scene object.
 *
 ****************************************************/
PScene Scene::PscenNew(PMovie pmvie)
{
    AssertNilOrPo(pmvie, 0);
    auto services = _OwnerServices();
    auto scene = static_cast<PScene>(three_mm_scene_owner_new(pmvie, &services));
    AssertNilOrPo(scene, 0);
    return scene;
}

/****************************************************
 *
 * Destructor for scenes.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
Scene::~Scene(void)
{
    AssertBaseThis(0);
    auto services = _OwnerServices();
    three_mm_scene_owner_dispose(&_owner, this, &services);
}

/****************************************************
 *
 * Destructor for scenes.  This method is used to not only
 * destruct a scene, but to remove all lights, actors and
 * text boxes from the rendering area.  This is necessary
 * because Undo will hold references to actors, etc, causing
 * their destructors to not be called.
 *
 * Parameters:
 *  ppscen - A pointer to the scene to destroy.
 *
 * Returns:
 *  None.
 *
 ****************************************************/

void Scene::Close(PScene *ppscen)
{
    AssertPo(*ppscen, 0);
    void *scene = *ppscen;
    auto services = _OwnerServices();
    three_mm_scene_owner_close(&scene, &services);
    *ppscen = static_cast<PScene>(scene);
}

#ifdef DEBUG

/****************************************************
 * Mark memory used by the Scene
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::MarkMem(void)
{
    AssertThis(0);

    long iactr;
    long itbox;
    PActor pactr;
    PTBOX ptbox;

    Scene_PAR::MarkMem();

    MarkMemObj(_pggsevStart);
    MarkMemObj(_pggsevFrm);
    MarkMemObj(_pglpactr);
    MarkMemObj(_pglptbox);
    MarkMemObj(_pglpactrSelExtra);
    MarkMemObj(_pmbmp);

    for (iactr = 0; iactr < _pglpactr->IvMac(); iactr++)
    {
        _pglpactr->Get(iactr, &pactr);
        MarkMemObj(pactr);
    }

    for (itbox = 0; itbox < _pglptbox->IvMac(); itbox++)
    {
        _pglptbox->Get(itbox, &ptbox);
        MarkMemObj(ptbox);
    }

    if (_psseBkgd != pvNil)
        MarkPv(_psseBkgd);
}

/***************************************************************************
    Assert the validity of the Scene.
***************************************************************************/
void Scene::AssertValid(LegacyUlong grf)
{
    long isev;
    SceneEvent sev;

    Scene_PAR::AssertValid(fobjAllocated);

    Assert(_data.name.count <= 255 && _data.name.unit == sizeof(achar), "bad scene name");
    AssertNilOrPo(_pactrSelected, 0);
    AssertNilOrPo(_pglpactrSelExtra, 0);
    if (_pglpactrSelExtra != pvNil)
    {
        for (long iactr = 0; iactr < _pglpactrSelExtra->IvMac(); iactr++)
        {
            PActor pactrEntry;
            _pglpactrSelExtra->Get(iactr, &pactrEntry);
            AssertPo(pactrEntry, 0);
        }
    }
    AssertNilOrPo(_pbkgd, 0);
    AssertNilOrPo(_pmbmp, 0);
    AssertPo(_pglpactr, 0);
    AssertPo(_pglptbox, 0);
    AssertPo(_pggsevFrm, 0);
    AssertPo(_pggsevStart, 0);
    AssertPo(_pmvie, 0);

    for (isev = 0; isev < _pggsevFrm->IvMac(); isev++)
    {
        sev = *(PSEV)_pggsevFrm->QvFixedGet(isev);
        switch (sev.sevt)
        {
        case sevtPlaySnd:
        case sevtPause:
        case sevtChngCamera:
            break;
        default:
            Bug("Unknown event type");
        }
    }

    for (isev = 0; isev < _pggsevStart->IvMac(); isev++)
    {
        sev = *(PSEV)_pggsevStart->QvFixedGet(isev);
        switch (sev.sevt)
        {
        case sevtAddActr:
        case sevtSetBkgd:
        case sevtAddTbox:
            break;
        default:
            Bug("Unknown event type");
        }
    }
}

#endif

/****************************************************
 *
 * Sets the transition of the scene and creates an undo object.
 *
 * Parameters:
 *	trans - Transition type.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FSetTransition(TRANS trans)
{
    AssertThis(0);

    PSceneUndoTransition psunr;

    psunr = SceneUndoTransition::PsunrNew();

    if (psunr == pvNil)
    {
        return (fFalse);
    }

    if (!_pmvie->FAddUndo(psunr))
    {
        return (fFalse);
    }

    ReleasePpo(&psunr);

    SetTransitionCore(trans);

    return (fTrue);
}

/****************************************************
 *
 * Sets the name of the scene and creates an undo object.
 *
 * Parameters:
 *	psz - Null terminated string.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FSetName(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    PSceneUndoTitle psunt;

    psunt = SceneUndoTitle::PsuntNew();

    if (psunt != pvNil)
    {
        String oldName; _data.GetName(&oldName); psunt->SetName(&oldName);
    }

    if (psunt == pvNil)
    {
        return (fFalse);
    }

    if (!_pmvie->FAddUndo(psunt))
    {
        return (fFalse);
    }

    ReleasePpo(&psunt);

    SetNameCore(pstn);

    return (fTrue);
}

ThreeMMSceneRuntimeServices Scene::_RuntimeServices()
{
    static_assert(fscenSounds == THREE_MM_SCENE_EFFECT_SOUNDS && fscenPauses == THREE_MM_SCENE_EFFECT_PAUSES &&
                      fscenActrs == THREE_MM_SCENE_EFFECT_ACTORS && fscenTboxes == THREE_MM_SCENE_EFFECT_TEXTS &&
                      fscenCams == THREE_MM_SCENE_EFFECT_CAMERAS,
                  "Scene effect flags");
    ThreeMMSceneRuntimeServices s = {};
    s.context = this;
    s.event_count = [](void *p) -> ThreeMMI32 { return static_cast<PScene>(p)->_pggsevFrm->IvMac(); };
    s.event = [](void *p, ThreeMMI32 i, ThreeMMI32 *frame, int *kind) {
        SceneEvent event;
        static_cast<PScene>(p)->_pggsevFrm->GetFixed(i, &event);
        *frame = event.nfrm;
        *kind = event.sevt;
    };
    s.play = [](void *p, ThreeMMI32 i, ThreeMMU32 disabled) -> int {
        auto scene = static_cast<PScene>(p);
        SceneEvent event;
        scene->_pggsevFrm->GetFixed(i, &event);
        return scene->_FPlaySev(&event, scene->_pggsevFrm->QvGet(i), disabled);
    };
    s.unplay = [](void *p, ThreeMMI32 i) -> int {
        auto scene = static_cast<PScene>(p);
        SceneEvent event;
        scene->_pggsevFrm->GetFixed(i, &event);
        return scene->_FUnPlaySev(&event, scene->_pggsevFrm->QvGet(i));
    };
    s.move_camera = [](void *p, ThreeMMI32 i, ThreeMMI32 frame) {
        auto group = static_cast<PScene>(p)->_pggsevFrm;
        SceneEvent event;
        group->GetFixed(i, &event);
        event.nfrm = frame;
        group->PutFixed(i, &event);
        group->Move(i, 0);
    };
    s.object_count = [](void *p, int text) -> ThreeMMI32 {
        auto scene = static_cast<PScene>(p);
        return (text ? scene->_pglptbox : scene->_pglpactr)->IvMac();
    };
    s.object = [](void *p, int text, ThreeMMI32 i) -> void * {
        auto scene = static_cast<PScene>(p);
        if (text)
        {
            PTBOX box;
            scene->_pglptbox->Get(i, &box);
            return box;
        }
        PActor actor;
        scene->_pglpactr->Get(i, &actor);
        return actor;
    };
    s.seek = [](void *, int text, void *object, ThreeMMI32 frame, int *sound) -> int {
        if (text)
            return static_cast<PTBOX>(object)->FGotoFrame(frame);
        bool value = sound ? *sound != 0 : false;
        bool result = static_cast<PActor>(object)->FGotoFrame(frame, sound ? &value : nullptr);
        if (sound)
            *sound = value;
        return result;
    };
    s.lifetime = [](void *, int text, void *object, ThreeMMI32 *first, ThreeMMI32 *last) -> int {
        long begin, end;
        bool result = text ? static_cast<PTBOX>(object)->FGetLifetime(&begin, &end)
                           : static_cast<PActor>(object)->FGetLifetime(&begin, &end);
        if (result)
        {
            *first = begin;
            *last = end;
        }
        return result;
    };
    s.replay_actor = [](void *, void *actor, ThreeMMU32 enabled) -> int {
        return static_cast<PActor>(actor)->FReplayFrame(enabled);
    };
    s.playing = [](void *p) -> int { return static_cast<PScene>(p)->Pmvie()->FPlaying(); };
    s.recording = [](void *p) -> int {
        auto view = static_cast<PMovieView>(static_cast<PScene>(p)->Pmvie()->PddgGet(0));
        return view && view->Tool() == toolRecordSameAction;
    };
    s.must_render = [](void *, void *actor, ThreeMMI32 until) -> int {
        return static_cast<PActor>(actor)->FMustRender(until);
    };
    s.prerendered = [](void *, void *actor) -> int { return static_cast<PActor>(actor)->FPrerendered(); };
    s.show = [](void *, void *actor, int visible) {
        if (visible)
            static_cast<PActor>(actor)->Show();
        else
            static_cast<PActor>(actor)->Hide();
    };
    s.set_prerendered = [](void *, void *actor, int value) { static_cast<PActor>(actor)->SetPrerendered(value != 0); };
    s.snapshot = [](void *p, int begin) {
        auto world = static_cast<PScene>(p)->Pmvie()->Pbwld();
        if (begin)
            world->Prerender();
        else
            world->Unprerender();
    };
    s.notice = [](void *p, int notice, int value) {
        auto scene = static_cast<PScene>(p);
        switch (notice)
        {
        case THREE_MM_SCENE_RESET_PAUSE:
            scene->Pmvie()->Pmcc()->PauseType(witNil);
            break;
        case THREE_MM_SCENE_RECORD_SOUND:
            scene->Pmvie()->Pmcc()->PlayUISound(toolRecordSameAction);
            break;
        case THREE_MM_SCENE_DIRTY:
            scene->_MarkMovieDirty();
            break;
        case THREE_MM_SCENE_INVALIDATE:
            scene->Pmvie()->InvalViews();
            break;
        case THREE_MM_SCENE_SOUND_INDICATOR:
            scene->Pmvie()->Pmcc()->SetSndFrame(value != 0);
            break;
        case THREE_MM_SCENE_GOTO_FAILURE:
            PushErc(ercSocGotoFrameFailure);
            break;
        case THREE_MM_SCENE_PRERENDER_ASSERT:
            Assert(false, "no actor should be marked prerendered here");
            break;
        }
    };
    return s;
}

/****************************************************
 *
 * This routine jumps to an arbitrary frame, updating actors, and adding
 * new frames to the scene if needed.
 *
 * Parameters:
 *	nFrm - The frame number to jump to.  This may be positive or negative
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/


bool Scene::FGotoFrm(long nfrm)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    return three_mm_scene_goto(&_runtime, &_data, nfrm, &services) != 0;
}

/****************************************************
 *
 * This function is an optimization which could be completely
 * skipped if desired.  The idea is to not render actors frame
 * after frame if they're not moving or changing at all.  So
 * when we hit a camera change, we look ahead to see if any
 * actors are unchanging before the next camera change.  If
 * there are, we hide the changing actors (so only the unchanging
 * ones are visible), then "take a snapshot" of the world with
 * the unchanging actors (via World::Prerender()), and use the
 * snapshot as the background RGB and Z buffer until the next
 * camera view change.  We only prerender if the movie is
 * playing.
 *
 * Parameters:
 *	fStartNow - if fTrue, start prerendering even if there's no
 *              camera change in this frame.  If fFalse, only
 *              start prerendering if there is a camera view
 *              change in this frame, or if this is the first
 *              frame of the scene.
 *
 * Returns:
 *  none
 *
 ****************************************************/
void Scene::_DoPrerenderingWork(bool fStartNow)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    three_mm_scene_prerender(&_runtime, &_data, fStartNow, &services);
}

/****************************************************
 *
 * 	Ends any current prerendering by restoring the background
 *  RGB and Z buffers of the World, showing all previously
 *  hidden actors, and marking all actors as not prerendered.
 *
 * Parameters:
 *	none
 *
 * Returns:
 *  none
 *
 ****************************************************/
void Scene::_EndPrerendering(void)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    three_mm_scene_end_prerender(&services);
}

/****************************************************
 *
 * This routine replays all the events, filtered by
 * grfscen, in the current frame.
 *
 * Parameters:
 *	grfscen - Events to play.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FReplayFrm(ulong grfscen)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    return three_mm_scene_replay(&_runtime, &_data, grfscen, &services) != 0;
}

/****************************************************
 *
 * This routine recalculates the length of the movie.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *	None.
 *
 ****************************************************/
void Scene::InvalFrmRange(void)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    three_mm_scene_extend_range(&_data, &services);
}

ThreeMMSceneEventServices Scene::_EventServices()
{
    static_assert(styMidi == THREE_MM_SCENE_EVENT_MIDI && transNil == THREE_MM_SCENE_EVENT_NIL_TRANSITION &&
                      transCut == THREE_MM_SCENE_EVENT_CUT_TRANSITION,
                  "Scene event constants");
    static_assert(sevtAddActr == THREE_MM_SCENE_ADD_ACTOR && sevtPlaySnd == THREE_MM_SCENE_PLAY_SOUND &&
                      sevtAddTbox == THREE_MM_SCENE_ADD_TEXT && sevtChngCamera == THREE_MM_SCENE_CHANGE_CAMERA &&
                      sevtSetBkgd == THREE_MM_SCENE_SET_BACKGROUND && sevtPause == THREE_MM_SCENE_PAUSE,
                  "Scene event kinds");
    ThreeMMSceneEventServices s = {};
    s.context = s.scene = this;
    s.runtime = &_runtime;
    s.data = &_data;
    s.owner = &_owner;
    s.owners = _OwnerServices();
    s.event = [](void *p, void *group, ThreeMMI32 i, ThreeMMI32 *frame, int *kind) {
        SceneEvent event;
        static_cast<PGeneralGroup>(group)->GetFixed(i, &event);
        *kind = event.sevt;
        // Start-event callers need not initialize the unused frame field.
        *frame = group == static_cast<PScene>(p)->_pggsevStart && event.sevt != sevtPlaySnd ? 0 : event.nfrm;
    };
    s.payload = [](void *, void *group, ThreeMMI32 i) -> void * { return static_cast<PGeneralGroup>(group)->QvGet(i); };
    s.sound_style = [](void *, void *payload) -> int { return static_cast<PSceneSoundEvent>(payload)->sty; };
    s.duplicate_sound = [](void *, void *payload) -> void * {
        return static_cast<PSceneSoundEvent>(payload)->PsseDup();
    };
    s.play_sound = [](void *p, void *payload) {
        static_cast<PSceneSoundEvent>(payload)->PlayAllSounds(static_cast<PScene>(p)->Pmvie());
    };
    s.read_tag = [](void *, void *payload, ThreeMMTag *out) {
        auto tag = static_cast<PTAG>(payload);
        // Preserve even the unused cache field of non-cache source tags.
        *out = {tag->sid, tag->pcrf, tag->ctg, tag->cno};
    };
    s.fetch_background = [](void *, ThreeMMTag *value) -> void * {
        TAG tag;
        TagFromCore(&tag, *value);
        auto background = vptagm->PbacoFetch(&tag, Background::FReadBkgd);
        *value = {tag.sid, tag.pcrf, tag.ctg, tag.cno};
        return background;
    };
    s.set_camera = [](void *p, void *background, ThreeMMI32 camera) -> int {
        return static_cast<PBackground>(background)->FSetCamera(static_cast<PScene>(p)->_pmvie->Pbwld(), camera);
    };
    s.transition = [](void *p) -> int { return static_cast<PScene>(p)->Pmvie()->Trans(); };
    s.set_transition = [](void *p, int value) { static_cast<PScene>(p)->Pmvie()->SetTrans(static_cast<TRANS>(value)); };
    s.publish_tag = [](void *p, const ThreeMMTag *value) { TagFromCore(&static_cast<PScene>(p)->_tagBkgd, *value); };
    s.read_object = [](void *, void *payload) -> void * {
        void *object;
        std::memcpy(&object, payload, sizeof(object));
        return object;
    };
    s.append = [](void *p, int text, void *object) -> int {
        auto scene = static_cast<PScene>(p);
        if (text)
        {
            auto box = static_cast<PTBOX>(object);
            AssertPo(box, 0);
            return scene->_pglptbox->FInsert(scene->_pglptbox->IvMac(), &box);
        }
        auto actor = static_cast<PActor>(object);
        AssertPo(actor, 0);
        return scene->_pglpactr->FPush(&actor);
    };
    s.remove_last = [](void *p, int text) {
        auto scene = static_cast<PScene>(p);
        if (text)
            scene->_pglptbox->Delete(scene->_pglptbox->IvMac() - 1);
        else
        {
            PActor actor;
            scene->_pglpactr->FPop(&actor);
        }
    };
    s.bind_actor = [](void *p, void *actor) { static_cast<PActor>(actor)->SetPscen(static_cast<PScene>(p)); };
    s.seek = [](void *, int text, void *object, ThreeMMI32 frame) -> int {
        return text ? static_cast<PTBOX>(object)->FGotoFrame(frame) : static_cast<PActor>(object)->FGotoFrame(frame);
    };
    s.pause_reason = [](void *, void *payload) -> int { return static_cast<const SceneEventPause *>(payload)->wit; };
    s.pause_delay = [](void *, void *payload) -> ThreeMMI32 {
        return static_cast<const SceneEventPause *>(payload)->dts;
    };
    s.pause_type = [](void *p, int reason) {
        static_cast<PScene>(p)->Pmvie()->Pmcc()->PauseType(static_cast<WaitReason>(reason));
    };
    s.pause = [](void *p, int reason, ThreeMMI32 delay) {
        static_cast<PScene>(p)->Pmvie()->DoPause(static_cast<WaitReason>(reason), delay);
    };
    s.camera = [](void *, void *payload) -> ThreeMMI32 { return *static_cast<const int32_t *>(payload); };
    s.diagnostic = [](void *, int issue) {
        switch (issue)
        {
        case THREE_MM_SCENE_EVENT_NO_BACKGROUND:
            Assert(false, "No background in the scene");
            break;
        case THREE_MM_SCENE_EVENT_UNKNOWN:
            Bug("Unhandled sevt");
            break;
        case THREE_MM_SCENE_EVENT_RESTORE_FAILED:
            Assert(false, "Should not fail.");
            break;
        case THREE_MM_SCENE_EVENT_FIRST_CAMERA_FAILED:
            Assert(false, "Should never happen");
            break;
        case THREE_MM_SCENE_EVENT_OWNER_OCCUPIED:
            Assert(false, "Scene owner slot must be empty");
            break;
        }
    };
    return s;
}

/****************************************************
 *
 * This routine plays a single event.
 *
 * Be careful not to modify _pggsevFrm or _pggsevStart from
 * within this routine.
 *
 * Parameters:
 *	psev - Pointer to the scene event to play.
 *	qvVar- Pointer to the variable part of the event.
 *	grfscen - Flags of currently disabled event types.
 *
 * Returns:
 *	fTrue if the event was played, fFalse in the case of failure.
 *
 ****************************************************/
bool Scene::_FPlaySev(PSEV psev, void *qvVar, ulong grfscen)
{
    AssertThis(0);
    auto services = _EventServices();
    AssertVarMem(psev);
    // Only sound dispatch consumes the event's frame. Other callers may leave it unset.
    return three_mm_scene_play_event(psev->sevt == sevtPlaySnd ? psev->nfrm : 0, psev->sevt, qvVar, grfscen,
                                    &services) != 0;
}

/****************************************************
 *
 * This routine undoes a single event.
 *
 * Parameters:
 *	psev - Pointer to the scene event to unplay.
 *	qvVar- Pointer to the variable part of the event.
 *
 *
 * Returns:
 *	fTrue if the unplay worked, else fFalse.
 *
 ****************************************************/
bool Scene::_FUnPlaySev(PSEV psev, void *qvVar)
{
    AssertThis(0);
    auto services = _EventServices();
    AssertVarMem(psev);
    return three_mm_scene_unplay_event(psev->sevt, &services) != 0;
}

/****************************************************
 *
 * This routine moves back stuff that is currently
 * in the first frame of the movie to the given frame.
 *
 * Parameters:
 *	nfrm - New first frame.
 *
 * Returns:
 *	None.
 *
 ****************************************************/
void Scene::_MoveBackFirstFrame(long nfrm)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    Assert(nfrm < _data.firstFrame, "Can only be called to extend scene back.");
    three_mm_scene_move_first(&_data, nfrm, &services);
}

ThreeMMSceneSoundEditServices Scene::_SoundEditServices()
{
    ThreeMMSceneSoundEditServices s = {};
    s.context = this;
    s.events = _EventServices();
    s.values = SceneSoundValues(Pmvie());
    s.insert = [](void *p, ThreeMMI32 index, ThreeMMI32 frame, int kind, ThreeMMI32 bytes, const void *payload) -> int {
        SceneEvent event = {frame, static_cast<SceneEventType>(kind)};
        return static_cast<PScene>(p)->_pggsevFrm->FInsert(index, bytes, const_cast<void *>(payload), &event);
    };
    s.put = [](void *p, ThreeMMI32 index, ThreeMMI32 bytes, const void *payload) -> int {
        return static_cast<PScene>(p)->_pggsevFrm->FPut(index, bytes, const_cast<void *>(payload));
    };
    s.erase = [](void *p, ThreeMMI32 index) { static_cast<PScene>(p)->_pggsevFrm->Delete(index); };
    s.allocate_records = [](void *, ThreeMMI32 bytes) -> void * {
        void *p = nullptr;
        return FAllocPv(&p, bytes, fmemClear, mprNormal) ? p : nullptr;
    };
    s.free_raw = [](void *, void *p) { FreePpv(&p); };
    s.input_tag = [](void *, void *tags, ThreeMMI32 index, ThreeMMTag *out) {
        auto p = &static_cast<PTAG>(tags)[index];
        *out = {p->sid, p->pcrf, p->ctg, p->cno};
    };
    s.child = [](void *p, ThreeMMU32 number, ThreeMMU32 *child) -> int {
        ChildChunkID value = *child;
        bool ok = static_cast<PScene>(p)->Pmvie()->FChidFromUserSndCno(number, &value);
        *child = value;
        return ok;
    };
    s.unused_child = [](void *) -> ThreeMMU32 {
        ChildChunkID value = 0;
        TrashVar(&value);
        return value;
    };
    s.resolve_input = [](void *p, void *tags, ThreeMMI32 index, ThreeMMU32 child) -> int {
        return static_cast<PScene>(p)->Pmvie()->FResolveSndTag(&static_cast<PTAG>(tags)[index], child);
    };
    s.fetch_input = [](void *, void *tags, ThreeMMI32 index) -> void * {
        return vptagm->PbacoFetch(&static_cast<PTAG>(tags)[index], MovieSoundMSND::FReadMsnd);
    };
    s.create_tag_list = [](void *, ThreeMMI32 count) -> void * { return DynamicArray::PglNew(size(TAG), count); };
    s.resize_tag_list = [](void *, void *list, ThreeMMI32 count) -> int {
        return static_cast<PDynamicArray>(list)->FSetIvMac(count);
    };
    s.put_list_tag = [](void *, void *list, ThreeMMI32 index, const ThreeMMTag *value) {
        TAG tag;
        TagFromCore(&tag, *value);
        static_cast<PDynamicArray>(list)->Put(index, &tag);
    };
    s.release_list = [](void *, void *list) {
        auto p = static_cast<PDynamicArray>(list);
        ReleasePpo(&p);
    };
    s.actor_count = [](void *p) -> ThreeMMI32 { return static_cast<PScene>(p)->_pglpactr->IvMac(); };
    s.actor_sound = [](void *p, ThreeMMI32 index) -> int {
        PActor actor;
        static_cast<PScene>(p)->_pglpactr->Get(index, &actor);
        AssertPo(actor, 0);
        return actor->FSoundInFrm();
    };
    s.dirty = [](void *p) { static_cast<PScene>(p)->_MarkMovieDirty(); };
    s.sound_frame = [](void *p, int value) { static_cast<PScene>(p)->Pmvie()->Pmcc()->SetSndFrame(value != 0); };
    s.new_undo = [](void *) -> void * { return SceneUndoSound::PsunsNew(); };
    s.undo_state = [](void *, void *undo) { return static_cast<PSceneUndoSound>(undo)->CoreState(); };
    s.commit_undo = [](void *p, void *undo) -> int {
        return static_cast<PScene>(p)->Pmvie()->FAddUndo(static_cast<PSceneUndoSound>(undo));
    };
    s.release_undo = [](void *, void *undo) {
        auto p = static_cast<PSceneUndoSound>(undo);
        ReleasePpo(&p);
    };
    s.clear_undo = [](void *p) { static_cast<PScene>(p)->Pmvie()->ClearUndo(); };
    s.diagnostic = [](void *, int issue) {
        switch (issue)
        {
        case THREE_MM_SCENE_SOUND_STYLE_MISSING:
            Bug("sty not found");
            break;
        case THREE_MM_SCENE_SOUND_MISSING:
            Bug("No such sound");
            break;
        case THREE_MM_SCENE_SOUND_QUEUE_RESTORE:
            Bug("Should never queue when undoing");
            break;
        case THREE_MM_SCENE_SOUND_OFFSET_OVERFLOW:
            Assert(false, "overflow in background sound offset");
            break;
        case THREE_MM_SCENE_SOUND_LIST_RESIZE:
            Assert(false, "PglNew should have ensured space");
            break;
        }
    };
    return s;
}
ThreeMMSceneSoundValueServices SceneUndoSound::_Values()
{
    return SceneSoundValues();
}
ThreeMMSceneSoundUndoServices SceneUndoSound::_Services()
{
    ThreeMMSceneSoundUndoServices s = {};
    s.context = this;
    s.values = SceneSoundValues(_pmvie);
    s.switch_scene = [](void *p) -> int {
        auto u = static_cast<PSceneUndoSound>(p);
        return u->_pmvie->FSwitchScen(u->_iscen);
    };
    s.seek_frame = [](void *p) -> int {
        auto u = static_cast<PSceneUndoSound>(p);
        return u->_pmvie->Pscen()->FGotoFrm(u->_nfrm);
    };
    s.flush = [](void *p) { static_cast<PSceneUndoSound>(p)->_pmvie->Pmsq()->FlushMsq(); };
    s.play = [](void *p) { static_cast<PSceneUndoSound>(p)->_pmvie->Pmsq()->PlayMsq(); };
    s.clear = [](void *p) { static_cast<PSceneUndoSound>(p)->_pmvie->ClearUndo(); };
    s.get = [](void *p, ThreeMMI32 style, int *found, ThreeMMSceneSound **out) -> int {
        auto u = static_cast<PSceneUndoSound>(p);
        bool hit;
        PSceneSoundEvent sound = nullptr;
        bool ok = u->_pmvie->Pscen()->FGetSnd(style, &hit, &sound);
        *found = hit;
        if (!ok || hit)
            *out = sound;
        return ok;
    };
    s.remove = [](void *p, ThreeMMI32 style) { static_cast<PSceneUndoSound>(p)->_pmvie->Pscen()->RemSndCore(style); };
    s.restore = [](void *p, const ThreeMMSceneSound *value) -> int {
        auto u = static_cast<PSceneUndoSound>(p);
        auto sound = static_cast<PSceneSoundEvent>(const_cast<ThreeMMSceneSound *>(value));
        return u->_pmvie->Pscen()->FAddSndCoreTagc(sound->fLoop != 0, false, sound->vlm, sound->sty, sound->ctagc,
                                                   sound->Ptagc(0));
    };
    s.diagnostic = [](void *, int) { Bug("where's the sound event?"); };
    return s;
}

/****************************************************
 *
 * This routine adds a sound to the event list for this frame.
 *
 * Parameters:
 *  fLoop - should the sound loop?
 *  fQueue - queue after existing sounds, or replace them?
 *  vlm - volume to play this sound at
 *  sty - sound type (midi, speech, or SFX)
 *  ctag - number of sounds
 *  prgtag - array of MovieSoundMSND tags
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddSndCore(bool fLoop, bool fQueue, long vlm, long sty, long ctag, PTAG prgtag)
{
    AssertThis(0);
    AssertPvCb(prgtag, LwMul(ctag, size(TAG)));
    Assert(!fQueue || ctag == 1, "if queueing, add only one sound");
    Assert(!fQueue || !fLoop, "can't both queue and loop");
    AssertIn(sty, 0, styLim);
    auto services = _SoundEditServices();
    return three_mm_scene_sound_add(fLoop, fQueue, vlm, sty, ctag, prgtag, &services) != 0;
}

/****************************************************
 *
 * This routine adds a sound to the event list for this frame.
 * Note: The chid's in the tagc are current
 *
 * Parameters:
 *  fLoop - should the sound loop?
 *  fQueue - queue after existing sounds, or replace them?
 *  vlm - volume to play this sound at
 *  sty - sound type (midi, speech, or SFX)
 *  ctag - number of sounds
 *  prgtagc - array of MovieSoundMSND tags
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddSndCoreTagc(bool fLoop, bool fQueue, long vlm, long sty, long ctagc, PTagChildPair prgtagc)
{
    AssertThis(0);
    AssertPvCb(prgtagc, LwMul(ctagc, size(TagChildPair)));
    Assert(!fQueue || ctagc == 1, "if queueing, add only one sound");
    Assert(!fQueue || !fLoop, "can't both queue and loop");
    AssertIn(sty, 0, styLim);
    auto services = _SoundEditServices();
    return three_mm_scene_sound_restore(fLoop, fQueue, vlm, sty, ctagc, prgtagc, &services) != 0;
}

/****************************************************
 *
 * This routine adds a sound to the event list for this frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *	ptag - pointer to the tag for the sound.
 *  fLoop - whether to loop the sound
 *  fQueue - queue after existing sounds, or replace them?
 *  vlm - volume to play this sound at
 *  sty - sound type (midi, speech, or SFX)
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddSnd(PTAG ptag, bool fLoop, bool fQueue, long vlm, long sty)
{
    AssertThis(0);
    AssertVarMem(ptag);
    Assert(!fQueue || !fLoop, "can't both queue and loop");
    AssertIn(sty, 0, styLim);
    auto services = _SoundEditServices();
    return three_mm_scene_sound_add_undo(fLoop, fQueue, vlm, sty, ptag, &services) != 0;
}

/****************************************************
 *
 * This routine removes a sound from the event list for this frame.
 *
 * Parameters:
 *	sty - the type of sound to remove
 *
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::RemSndCore(long sty)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    auto services = _SoundEditServices();
    three_mm_scene_sound_remove(sty, &services);
}

/****************************************************
 *
 * This routine removes a sound from the event list for this frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *	sty - the type of sound to remove
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FRemSnd(long sty)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    auto services = _SoundEditServices();
    return three_mm_scene_sound_remove_undo(sty, &services) != 0;
}

/****************************************************
 *
 * This routine finds a specific sound in the current frame.
 *
 * Parameters:
 *	sty - sound type to search for
 *  pfFound - set to fTrue if a sound is found, else fFalse
 *  ppsse - gets a pointer to the SceneSoundEvent if it is found
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FGetSnd(long sty, bool *pfFound, PSceneSoundEvent *ppsse)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertVarMem(ppsse);
    *pfFound = false;
    auto services = _SoundEditServices();
    int found;
    ThreeMMSceneSound *sound = nullptr;
    bool ok = three_mm_scene_sound_get(sty, &found, &sound, &services) != 0;
    if (!ok || found)
        *ppsse = static_cast<PSceneSoundEvent>(sound);
    *pfFound = found != 0;
    return ok;
}

/****************************************************
 *
 * This routine plays the background sound, if any.
 * If the sound really started at an earlier frame,
 * this function tries to resume the sound at the
 * appropriate offset into the sound (dtsStart).
 *
 * Parameters:
 *  none
 *
 * Returns:
 *  none
 *
 ****************************************************/
void Scene::PlayBkgdSnd(void)
{
    AssertThis(0);
    static_assert(kdtsSecond == 1000 && kfps == 6, "Scene background clock");
    auto services = _SoundEditServices();
    three_mm_scene_sound_background_play(&services);
}

/****************************************************
 *
 * This routine queries what sounds are attched to the
 * scene at the current frame
 *
 * Parameters:
 *  sty - the sound type to query
 *
 * Returns:
 *  fFalse if an error occurs
 *
 ****************************************************/
bool Scene::FQuerySnd(long sty, PDynamicArray *ppgltagSnd, long *pvlm, bool *pfLoop)
{
    AssertThis(0);
    AssertVarMem(ppgltagSnd);
    AssertVarMem(pvlm);
    AssertVarMem(pfLoop);
    auto services = _SoundEditServices();
    struct Output
    {
        PDynamicArray *list;
        long *volume;
        bool *loop;
    } output = {ppgltagSnd, pvlm, pfLoop};
    services.query_context = &output;
    services.publish_query = [](void *p, void *list, int metadata, ThreeMMI32 volume, int loop) {
        auto out = static_cast<Output *>(p);
        *out->list = static_cast<PDynamicArray>(list);
        if (metadata)
        {
            *out->volume = volume;
            *out->loop = loop != 0;
        }
    };
    void *list = nullptr;
    ThreeMMI32 volume = 0;
    int loop = 0;
    return three_mm_scene_sound_query(sty, &list, &volume, &loop, &services) != 0;
}

/****************************************************
 *
 * This routine changes the volume of the sound of
 * type sty at the current frame to vlmNew
 *
 * Parameters:
 *  sty - the sound type to query
 *  vlmNew - the new volume
 *
 * Returns:
 *  none
 *
 ****************************************************/
void Scene::SetSndVlmCore(long sty, long vlmNew)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertIn(vlmNew, 0, kvlmFull + 1);
    auto services = _SoundEditServices();
    three_mm_scene_sound_volume(sty, vlmNew, &services);
}

/******************************************************************************
    UpdateSndFrame
        Enumerates all scene events for the current frame, and asks all actors
        to enumerate all of their actor events for the current frame, looking
        for a sound event.  Has the movie's MovieClientCallbacks update the frame-sound state
        based on the results of the search.
************************************************************ PETED ***********/
void Scene::UpdateSndFrame(void)
{
    auto services = _SoundEditServices();
    three_mm_scene_sound_update_indicator(&services);
}

/****************************************************
 *
 * This routine adds an event to the current frame of the current scene.
 *
 * Parameters:
 *  psev - Pointer to the event to add.
 *	cbVar- Size of pvVar buffer, in bytes.
 *  pvVar- Pointer to the variable part of the event.
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool Scene::_FAddSev(PSEV psev, long cbVar, void *pvVar)
{
    AssertThis(0);
    AssertVarMem(psev);
    AssertIn(cbVar, 0, klwMax);
    auto services = _SoundEditServices();
    return three_mm_scene_edit_insert(psev->nfrm, psev->sevt, cbVar, pvVar, &services) != 0;
}

/****************************************************
 *
 * This routine sets the selected actor to the given one.
 *
 * Parameters:
 *  pactr - Pointer to the actr to select.  pvNil is a
 *  	valid value and deselects the current actor and tbox.
 *
 * Returns:
 *	None
 *
 ****************************************************/
ThreeMMSceneSelectionServices Scene::_SelectionServices()
{
    ThreeMMSceneSelectionServices s = {};
    s.context = this;
    s.state = &_selection;
    s.publish = [](void *p, int text, void *object) {
        auto scene = static_cast<Scene *>(p);
        if (text)
            scene->_ptboxSelected = static_cast<PTBOX>(object);
        else
            scene->_pactrSelected = static_cast<PActor>(object);
    };
    s.view_mode = [](void *p) {
        auto view = static_cast<PMovieView>(static_cast<Scene *>(p)->Pmvie()->PddgGet(0));
        return view ? int(view->FTextMode()) : -1;
    };
    s.count = [](void *p, int extras) -> ThreeMMI32 {
        auto scene = static_cast<Scene *>(p);
        auto list = extras ? scene->_pglpactrSelExtra : scene->_pglpactr;
        return list ? list->IvMac() : 0;
    };
    s.at = [](void *p, int extras, ThreeMMI32 index) -> void * {
        auto scene = static_cast<Scene *>(p);
        PActor actor;
        (extras ? scene->_pglpactrSelExtra : scene->_pglpactr)->Get(index, &actor);
        return actor;
    };
    s.ensure_extras = [](void *p) {
        auto scene = static_cast<Scene *>(p);
        if (!scene->_pglpactrSelExtra)
            scene->_SetOwner(THREE_MM_SCENE_SELECTION, DynamicArray::PglNew(sizeof(PActor)));
        return int(scene->_pglpactrSelExtra != nullptr);
    };
    s.append_extra = [](void *p, void *object) {
        auto actor = static_cast<PActor>(object);
        return int(static_cast<Scene *>(p)->_pglpactrSelExtra->FAdd(&actor));
    };
    s.erase_extra = [](void *p, ThreeMMI32 index) { static_cast<Scene *>(p)->_pglpactrSelExtra->Delete(index); };
    s.clear_extras = [](void *p) {
        auto list = static_cast<Scene *>(p)->_pglpactrSelExtra;
        if (list)
            list->FSetIvMac(0);
    };
    s.hilite = [](void *, void *object, int selected) {
        if (selected)
            static_cast<PActor>(object)->Hilite();
        else
            static_cast<PActor>(object)->Unhilite();
    };
    s.text_selected = [](void *, void *object) { return int(static_cast<PTBOX>(object)->FSelected()); };
    s.select_text = [](void *, void *object, int selected) { static_cast<PTBOX>(object)->Select(selected != 0); };
    s.invalidate = [](void *p) { static_cast<Scene *>(p)->_pmvie->InvalViews(); };
    s.action_menu = [](void *p) { static_cast<Scene *>(p)->_pmvie->BuildActionMenu(); };
    s.text_notice = [](void *p) { static_cast<Scene *>(p)->_pmvie->Pmcc()->TboxSelected(); };
    s.name = [](void *, void *object, unsigned short *out) -> ThreeMMI32 {
        static_assert(kcchMaxStn <= 256, "Selection name buffer");
        String name;
        static_cast<PActor>(object)->GetName(&name);
        for (long i = 0; i < name.Cch(); ++i)
            out[i] = sizeof(achar) == 1 ? static_cast<unsigned char>(name.Prgch()[i])
                                        : static_cast<unsigned short>(name.Prgch()[i]);
        return name.Cch();
    };
    s.visible_position = [](void *, void *object, ThreeMMActorPoint *out) {
        auto actor = static_cast<PActor>(object);
        if (!actor->Pbody() || !actor->FIsInView())
            return 0;
        BRS x, y, z;
        actor->Pbody()->GetPosition(&x, &y, &z);
        *out = {x, y, z};
        return 1;
    };
    return s;
}

void Scene::SelectActr(Actor *pactr)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);
    auto s = _SelectionServices();
    three_mm_scene_select_actor(pactr, &s);
}

/****************************************************
 *
 * Clears the entire selection (primary and extras).
 *
 ****************************************************/
void Scene::ClearSelection(void)
{
    AssertThis(0);
    SelectActr(pvNil);
}

/****************************************************
 *
 * Selects every actor in the current scene's actor list.
 * The first actor becomes the primary; the rest are added
 * as extras. Empty scene clears any existing selection.
 *
 * Returns fFalse if a mid-add allocation fails (selection
 * may end up partial in that case).
 *
 ****************************************************/
bool Scene::FSelectAllActrs(void)
{
    AssertThis(0);
    auto s = _SelectionServices();
    return three_mm_scene_selection_all(&s) != 0;
}

/****************************************************
 *
 * Select pactrSeed and every other scene actor that shares any #tag
 * with it. If pactrSeed has no tags, this is just SelectActr(pactrSeed).
 *
 ****************************************************/
bool Scene::FSelectActrsSharingTagsWith(PActor pactrSeed)
{
    AssertThis(0);
    AssertPo(pactrSeed, 0);
    auto s = _SelectionServices();
    return three_mm_scene_selection_shared(pactrSeed, &s) != 0;
}

/****************************************************
 *
 * Replace the current selection with every actor in the current scene
 * whose display name contains '#pszTag' as a whole tag token.
 *
 ****************************************************/
bool Scene::FSelectActrsByTag(PZString pszTag)
{
    AssertThis(0);
    auto s = _SelectionServices();
    return three_mm_scene_selection_tag(pszTag, CchSz(pszTag), sizeof(achar) == 2, &s) != 0;
}

/****************************************************
 *
 * Mean of currently-visible selected actors' world positions.
 * Group rotate / scale tools freeze this at mousedown as the
 * pivot. Skips actors that aren't FIsInView() at the current
 * frame so a stale offstage position can't drag the pivot
 * away from what the user sees.
 *
 * Returns fFalse (and leaves outputs untouched) if no selected
 * actor is currently visible.
 *
 ****************************************************/
bool Scene::FXyzSelectionCentroid(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);
    auto s = _SelectionServices();
    ThreeMMActorPoint point;
    if (!three_mm_scene_selection_centroid(&point, &s))
        return fFalse;
    *pxr = point.dxr;
    *pyr = point.dyr;
    *pzr = point.dzr;
    return fTrue;
}

/****************************************************
 *
 * Returns the total number of selected actors:
 * 0 if primary is pvNil, else 1 + extras count.
 *
 ****************************************************/
long Scene::CactrSelected(void)
{
    AssertThis(0);
    auto s = _SelectionServices();
    return three_mm_scene_selection_count(&s);
}

/****************************************************
 *
 * Returns the iactr-th selected actor: 0 = primary,
 * 1..N = extras in insertion order.
 *
 ****************************************************/
PActor Scene::PactrSelectedAt(long iactr)
{
    AssertThis(0);
    AssertIn(iactr, 0, CactrSelected());
    auto s = _SelectionServices();
    return static_cast<PActor>(three_mm_scene_selection_at(iactr, &s));
}

/****************************************************
 *
 * Returns fTrue if pactr is the primary or in the extras list.
 *
 ****************************************************/
bool Scene::FIsActrSelected(PActor pactr)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);
    auto s = _SelectionServices();
    return three_mm_scene_selection_contains(pactr, &s) != 0;
}

/****************************************************
 *
 * Shift-click entry point. Toggles pactr's membership in the
 * selection set: adds it if absent, removes it if present.
 * Returns fFalse on allocation failure.
 *
 ****************************************************/
bool Scene::FToggleActrSelected(PActor pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);
    auto s = _SelectionServices();
    return three_mm_scene_selection_toggle(pactr, &s) != 0;
}

/****************************************************
 *
 * This routine sets the selected text box to the given one.
 *
 * Parameters:
 *  ptbox - Pointer to the tbox to select.  pvNil is a
 *  	valid value and deselects the current actor and tbox.
 *
 * Returns:
 *	None
 *
 ****************************************************/
void Scene::SelectTbox(PTBOX ptbox)
{
    AssertThis(0);
    AssertNilOrPo(ptbox, 0);
    auto s = _SelectionServices();
    three_mm_scene_select_text(ptbox, &s);
}

/****************************************************
 *
 * This routine adds an actor to the scene at the current frame,
 * if the arid is aridNil, else it replaces the actor in the
 * scene with the same arid.
 *
 * Parameters:
 *  pactr - Pointer to the actr to add.
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
ThreeMMSceneContentServices Scene::_ContentServices()
{
    ThreeMMSceneContentServices s = {};
    s.context = this;
    s.events = _EventServices();
    s.retain = [](void *, void *p) { static_cast<BASE *>(p)->AddRef(); };
    s.actor_id = [](void *, void *p) -> ThreeMMI32 { return static_cast<PActor>(p)->Arid(); };
    s.set_actor_id = [](void *, void *p, ThreeMMI32 id) { static_cast<PActor>(p)->SetArid(id); };
    s.actor_ready = [](void *, void *p) -> int { return static_cast<PActor>(p)->Pbody() != nullptr; };
    s.actor_onstage = [](void *, void *p) -> int { return static_cast<PActor>(p)->FAddOnStageCore(); };
    s.actor_paste = [](void *p, void *a, ThreeMMI32 frame) -> int {
        return static_cast<PActor>(a)->FPaste(frame, static_cast<PScene>(p));
    };
    s.hide_actor = [](void *, void *p) {
        auto actor = static_cast<PActor>(p);
        if (actor->Pbody())
            actor->Hide();
    };
    s.insert_start = [](void *p, ThreeMMI32 i, int kind, void *object) -> int {
        SceneEvent event = {0, static_cast<SceneEventType>(kind)};
        return static_cast<PScene>(p)->_pggsevStart->FInsert(i, sizeof(object), &object, &event);
    };
    s.put_start = [](void *p, ThreeMMI32 i, void *object) { static_cast<PScene>(p)->_pggsevStart->Put(i, &object); };
    s.put_list = [](void *, void *p, ThreeMMI32 i, void *object) { static_cast<PDynamicArray>(p)->Put(i, &object); };
    s.erase = [](void *, void *p, ThreeMMI32 i) { static_cast<GroupBase *>(p)->Delete(i); };
    s.selected = [](void *p, int text) -> void * {
        auto scene = static_cast<Scene *>(p);
        return text ? scene->_selection.text : scene->_selection.actor;
    };
    s.set_selected = [](void *p, int text, void *object) {
        auto scene = static_cast<Scene *>(p);
        if (text)
        {
            scene->_selection.text = object;
            scene->_ptboxSelected = static_cast<PTBOX>(object);
        }
        else
        {
            scene->_selection.actor = object;
            scene->_pactrSelected = static_cast<PActor>(object);
        }
    };
    s.select = [](void *p, int text, void *object) {
        auto scene = static_cast<PScene>(p);
        if (text)
            scene->SelectTbox(static_cast<PTBOX>(object));
        else
            scene->SelectActr(static_cast<PActor>(object));
    };
    s.end_placement = [](void *p) {
        auto view = static_cast<PMovieView>(static_cast<PScene>(p)->_pmvie->PddgGet(0));
        if (view)
            view->EndPlaceActor();
    };
    s.add_roll = [](void *p, void *object, int templateName) -> int {
        auto a = static_cast<PActor>(object);
        String name;
        if (templateName)
            a->Ptmpl()->GetName(&name);
        else
            a->GetName(&name);
        return static_cast<PScene>(p)->_pmvie->FAddToRollCall(a, &name);
    };
    s.remove_roll = [](void *p, void *a, int only) {
        static_cast<PScene>(p)->_pmvie->RemFromRollCall(static_cast<PActor>(a), only != 0);
    };
    s.dirty = [](void *p) { static_cast<PScene>(p)->_MarkMovieDirty(); };
    s.invalidate = [](void *p) { static_cast<PScene>(p)->InvalFrmRange(); };
    s.indicator = [](void *p) { static_cast<PScene>(p)->UpdateSndFrame(); };
    s.update_roll = [](void *p) { static_cast<PScene>(p)->_pmvie->Pmcc()->UpdateRollCall(); };
    s.bind_text = [](void *p, void *text) { static_cast<PTBOX>(text)->SetScen(static_cast<PScene>(p)); };
    s.text_select = [](void *, void *text, int selected) { static_cast<PTBOX>(text)->Select(selected != 0); };
    s.text_lifetime = [](void *, void *text, ThreeMMI32 *first, ThreeMMI32 *last) -> int {
        long a, b;
        bool ok = static_cast<PTBOX>(text)->FGetLifetime(&a, &b);
        if (ok)
        {
            *first = a;
            *last = b;
        }
        return ok;
    };
    s.hide_text = [](void *, void *text) { static_cast<PTBOX>(text)->HideCore(); };
    s.new_undo = [](void *, int text) -> void * {
        return text ? static_cast<void *>(SceneUndoText::PsunxNew()) : static_cast<void *>(SceneActorUndo::PsunaNew());
    };
    s.actor_state = [](void *, void *u) { return static_cast<PSceneActorUndo>(u)->CoreState(); };
    s.text_state = [](void *, void *u) { return static_cast<PSceneUndoText>(u)->CoreState(); };
    s.commit_undo = [](void *p, void *u) -> int {
        return static_cast<PScene>(p)->_pmvie->FAddUndo(static_cast<PMovieUndo>(u));
    };
    s.release_undo = [](void *, void *u) { static_cast<PMovieUndo>(u)->Release(); };
    s.clear_undo = [](void *p) { static_cast<PScene>(p)->_pmvie->ClearUndo(); };
    s.not_undoable = [](void *) { PushErc(ercSocNotUndoable); };
    s.diagnostic = [](void *, int issue) {
        switch (issue)
        {
        case THREE_MM_SCENE_CONTENT_ACTOR_MISSING:
            Bug("Actor does not exist in scene");
            break;
        case THREE_MM_SCENE_CONTENT_ACTOR_ROLL_MISSING:
            Bug("Actor does not exist in roll call");
            break;
        case THREE_MM_SCENE_CONTENT_TEXT_MISSING:
            Bug("Could not find text box");
            break;
        case THREE_MM_SCENE_CONTENT_TEXT_ROLL_MISSING:
            Bug("Text box not found in DynamicArray");
            break;
        case THREE_MM_SCENE_CONTENT_TEXT_HIDE_FAILED:
            Bug("Could not hide text box");
            break;
        default:
            Bug("Invalid scene content state");
            break;
        }
    };
    return s;
}
bool Scene::FAddActrCore(Actor *actor)
{
    AssertThis(0);
    auto s = _ContentServices();
    return three_mm_scene_actor_add(actor, &s) != 0;
}

/****************************************************
 *
 * This routine adds an actor to the scene at the current frame.
 * This auto magically selects the actor and places them on stage.
 *
 * Parameters:
 *  pactr - Pointer to the actr to add.
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddActr(Actor *actor)
{
    AssertThis(0);
    auto s = _ContentServices();
    return three_mm_scene_actor_add_onstage(actor, &s) != 0;
}

/****************************************************
 *
 * This routine removes an actor from the scene.
 *
 * Parameters:
 *  arid - The actor id to remove.
 *
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::RemActrCore(long id)
{
    AssertThis(0);
    auto s = _ContentServices();
    three_mm_scene_actor_remove(id, &s);
}

/****************************************************
 *
 * This routine removes an actor from the scene, and
 * creates an undo object for the action.
 *
 * Parameters:
 *  arid - The actor id to remove.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FRemActr(long id)
{
    AssertThis(0);
    auto s = _ContentServices();
    return three_mm_scene_actor_remove_undo(id, &s) != 0;
}

/****************************************************
 *
 * This routine returns the actor pointed at by the mouse.
 *
 * Parameters:
 *	xp - X position of the mouse within the display space.
 *	yp - Y position of the mouse within the display space.
 *  pibset - Place to store the index of the group of body parts hit.
 *
 * Returns:
 *  Pointer to the actor, pvNil if none.
 *
 ****************************************************/
Actor *Scene::PactrFromPt(long xp, long yp, long *pibset)
{
    AssertThis(0);
    AssertVarMem(pibset);

    Actor *pactr;
    Body *pbody;
    long ipactr;

    pbody = BodyWorld::PbodyClicked(xp, yp, Pmvie()->Pbwld(), pibset);
    if (pvNil == pbody)
    {
        return pvNil;
    }

    //
    // loop through actors, call FIsMyBody()
    //
    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        if (pactr->FIsMyBody(pbody))
        {
            return pactr;
        }
    }

    Bug("weird...we clicked a pbody, but we don't know whose it is!");

    return pvNil;
}

/****************************************************
 *
 * This routine adds a text box to the scene at the current frame
 *
 * Parameters:
 *  ptbox - Pointer to the text box to add.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddTboxCore(PTBOX text)
{
    AssertThis(0);
    auto s = _ContentServices();
    return three_mm_scene_text_add(text, &s) != 0;
}

/****************************************************
 *
 * This routine adds a text box to the scene at the current frame
 * creates an undo object for the action.
 *
 * Parameters:
 *  ptbox - Pointer to the text box to add.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddTbox(PTBOX text)
{
    AssertThis(0);
    auto s = _ContentServices();
    return three_mm_scene_text_add_undo(text, &s) != 0;
}

/****************************************************
 *
 * This routine removes a text box from the scene.
 *
 * Parameters:
 *  ptbox - Pointer to the text box to remove.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FRemTboxCore(PTBOX text)
{
    AssertThis(0);
    auto s = _ContentServices();
    return three_mm_scene_text_remove(text, &s) != 0;
}

/****************************************************
 *
 * This routine removes a text box from the scene,
 * creates an undo object for the action.
 *
 * Parameters:
 *  ptbox - Pointer to the text box to remove.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FRemTbox(PTBOX text)
{
    AssertThis(0);
    auto s = _ContentServices();
    return three_mm_scene_text_remove_undo(text, &s) != 0;
}

/****************************************************
 *
 * This routine gets the ith text box.
 *
 * Parameters:
 *  itbox - Index of the text box to get.
 *
 * Returns:
 *  Pointer to the text box if itbox is valid, else pvNil.
 *
 ****************************************************/
TextBox *Scene::PtboxFromItbox(long i)
{
    AssertThis(0);
    auto s = _ContentServices();
    return static_cast<PTBOX>(three_mm_scene_text_at(i, &s));
}

/****************************************************
 *
 * This routine adds/removes a pause to the scene at the current frame.
 *
 * Parameters:
 *	pwit - The pause type, witNil removes a pause, returns old pause type.
 *  pdts - Valid only if wit==witForTime, dts is in clock ticks, returns
 *		old wait time if old wit was witForTime.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
static ThreeMMTag SceneEditTag(const TAG &t)
{
    return {t.sid, t.pcrf, t.ctg, t.cno};
}
static void SceneEditPublishTag(void *p, const ThreeMMTag *tag, int first)
{
    auto out = static_cast<PTAG>(p);
    TagFromCore(out, *tag);
    if (first)
        TrashVar(out);
}
ThreeMMSceneEditServices Scene::_EditServices()
{
    ThreeMMSceneEditServices s = {};
    s.context = this;
    s.sound = _SoundEditServices();
    s.put = [](void *, void *g, ThreeMMI32 i, const void *p) {
        static_cast<PGeneralGroup>(g)->Put(i, const_cast<void *>(p));
    };
    s.erase = [](void *, void *g, ThreeMMI32 i) { static_cast<PGeneralGroup>(g)->Delete(i); };
    s.put_background = [](void *p, ThreeMMI32 i, const ThreeMMTag *value) -> int {
        TAG tag;
        TagFromCore(&tag, *value);
        return static_cast<PScene>(p)->_pggsevStart->FPut(i, sizeof(tag), &tag);
    };
    s.insert_background = [](void *p, ThreeMMI32 frame, const ThreeMMTag *value) -> int {
        TAG tag;
        TagFromCore(&tag, *value);
        SceneEvent event = {frame, sevtSetBkgd};
        return static_cast<PScene>(p)->_pggsevStart->FInsert(0, sizeof(tag), &tag, &event);
    };
    s.play_background = [](void *p, ThreeMMI32 frame, const ThreeMMTag *value, ThreeMMU32 mask) -> int {
        TAG tag;
        TagFromCore(&tag, *value);
        SceneEvent event = {frame, sevtSetBkgd};
        return static_cast<PScene>(p)->_FPlaySev(&event, &tag, mask);
    };
    s.background_name = [](void *p) {
        auto scene = static_cast<PScene>(p);
        String name;
        scene->_pbkgd->GetName(&name);
        scene->_data.SetName(&name);
    };
    s.scene_change = [](void *p) { static_cast<PScene>(p)->_pmvie->Pmcc()->SceneChange(); };
    s.can_notify = [](void *) -> int { return vpcex != nullptr; };
    s.loaded = [](void *) { vpcex->EnqueueCid(cidSceneLoaded); };
    s.current_camera = [](void *p) -> ThreeMMI32 { return static_cast<PScene>(p)->_pbkgd->Icam(); };
    s.new_undo = [](void *, int kind) -> void * {
        return kind == THREE_MM_SCENE_UNDO_PAUSE ? static_cast<void *>(SceneUndoPause::PsunpNew())
                                                 : static_cast<void *>(SceneUndoBackground::PsunkNew());
    };
    s.pause_state = [](void *, void *u) { return static_cast<PSceneUndoPause>(u)->CoreState(); };
    s.background_state = [](void *, void *u) { return static_cast<PSceneUndoBackground>(u)->CoreState(); };
    s.commit_undo = [](void *p, void *u) -> int {
        return static_cast<PScene>(p)->_pmvie->FAddUndo(static_cast<PMovieUndo>(u));
    };
    s.release_undo = [](void *, void *u) { static_cast<PMovieUndo>(u)->Release(); };
    s.clear_undo = [](void *p) { static_cast<PScene>(p)->_pmvie->ClearUndo(); };
    s.not_undoable = [](void *) { PushErc(ercSocNotUndoable); };
    s.camera_tag = [](void *p, ThreeMMI32 camera, ThreeMMTag *out) -> int {
        TAG tag = {};
        bool ok = vptagm->FBuildChildTag(&static_cast<PScene>(p)->_tagBkgd, camera, kctgCam, &tag);
        if (ok)
            *out = SceneEditTag(tag);
        return ok;
    };
    s.cache_tag = [](void *, const ThreeMMTag *value) -> int {
        TAG tag;
        TagFromCore(&tag, *value);
        return vptagm->FCacheTagToHD(&tag);
    };
    s.close_tag = [](void *, ThreeMMTag *value) {
        TAG tag;
        TagFromCore(&tag, *value);
        TagManager::CloseTag(&tag);
        *value = SceneEditTag(tag);
    };
    s.diagnostic = [](void *, int issue) {
        if (issue == THREE_MM_SCENE_EDIT_FIRST_BACKGROUND)
            Bug("Use CORE function to set first background");
        else
            Bug("No background event found");
    };
    return s;
}
bool Scene::FPauseCore(WaitReason *pwit, long *pdts)
{
    AssertThis(0);
    AssertIn(*pwit, witNil, witLim);
    AssertIn(*pdts, 0, klwMax);
    ThreeMMI32 reason = *pwit, delay = *pdts;
    auto s = _EditServices();
    bool ok = three_mm_scene_pause_edit(&reason, &delay, &s) != 0;
    *pwit = static_cast<WaitReason>(reason);
    *pdts = delay;
    return ok;
}

/****************************************************
 *
 * This routine adds/removes a pause to the scene at the current frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *	wit - The pause type, removes pause if wit==witNil.
 *  dts - Valid only if wit==witForTime, dts is in clock ticks.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FPause(WaitReason wit, long dts)
{
    auto s = _EditServices();
    return three_mm_scene_pause_undo_edit(wit, dts, &s) != 0;
}

/****************************************************
 *
 * This routine sets the background for the scene.
 *
 * Parameters:
 *  ptag - Pointer to the background tag to put on the scene.
 *  ptagOld - Place to store the old background tag.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FSetBkgdCore(PTAG ptag, PTAG ptagOld)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertVarMem(ptagOld);
    auto value = SceneEditTag(*ptag);
    ThreeMMTag old;
    auto s = _EditServices();
    s.output_context = ptagOld;
    s.publish_tag = SceneEditPublishTag;
    return three_mm_scene_background_edit(&value, &old, &s) != 0;
}

/****************************************************
 *
 * This routine sets the background for the scene.
 *
 * Parameters:
 *  pbkgd - Pointer to the background to put on the scene.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FSetBkgd(PTAG ptag)
{
    AssertThis(0);
    AssertVarMem(ptag);
    auto value = SceneEditTag(*ptag);
    auto s = _EditServices();
    s.output_context = ptag;
    s.publish_tag = SceneEditPublishTag;
    return three_mm_scene_background_undo_edit(&value, &s) != 0;
}

/****************************************************
 *
 * This routine returns if a scene is currently empty
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  fTrue if the scene contains only camera changes and sounds
 *    and is only 1 frame long, else fFalse.
 *
 ****************************************************/
bool Scene::FIsEmpty(void)
{
    AssertThis(0);
    auto s = _EditServices();
    return three_mm_scene_is_empty(&s) != 0;
}

/****************************************************
 *
 * This routine changes the camera view point at this frame.
 *
 * Parameters:
 *  icam - The camera number in the Background to switch to.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FChangeCamCore(long icam, long *picamOld)
{
    AssertThis(0);
    AssertIn(icam, 0, 500);
    AssertVarMem(picamOld);
    ThreeMMI32 old;
    auto s = _EditServices();
    s.output_context = picamOld;
    s.publish_camera = [](void *p, ThreeMMI32 value) { *static_cast<long *>(p) = value; };
    return three_mm_scene_camera_edit(icam, &old, &s) != 0;
}

/****************************************************
 *
 * This routine changes the camera view point at this frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *  icam - The camera number in the Background to switch to.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FChangeCam(long icam)
{
    auto s = _EditServices();
    return three_mm_scene_camera_undo_edit(icam, &s) != 0;
}

/****************************************************
 *
 * This routine reads in a scene from a chunky file.
 *
 * Parameters:
 *  pcrf - Pointer to the chunky file to read from.
 *  cno  - Cno within the chunky file to read.
 *
 * Returns:
 *  pvNil, if failure, else a pointer to the scene.
 *
 ****************************************************/
Scene *Scene::PscenRead(PMovie pmvie, PChunkyResourceFile pcrf, ChunkNumber cno)
{
    AssertPo(pmvie, 0);
    AssertPo(pcrf, 0);
    struct Host
    {
        PChunkyResourceFile resources;
        PChunkyFile file;
    } host = {pcrf, pcrf->Pcfl()};
    ThreeMMSceneLoadServices s = {};
    s.context = &host;
    s.owners = _OwnerServices();
    s.packets = ScenePacketServices(pmvie);
    s.metadata = [](void *p, ThreeMMU32 number, ThreeMMSceneState *out) -> int {
        SceneData value;
        if (!value.FRead(static_cast<Host *>(p)->file, number))
            return 0;
        *out = value;
        return 1;
    };
    s.publish_metadata = [](void *, void *scene, const ThreeMMSceneState *value) {
        static_cast<ThreeMMSceneState &>(static_cast<PScene>(scene)->_data) = *value;
    };
    s.child = [](void *p, ThreeMMU32 scene, int kind, ThreeMMU32 child, ThreeMMU32 *number) -> int {
        static const ChunkTagOrType types[] = {kctgFrmGg, kctgStartGg, kctgActr, kctgTbox, 0, 0, kctgThumbMbmp};
        ChildChunkIdentification kid;
        if (!static_cast<Host *>(p)->file->FGetKidChidCtg(kctgScen, scene, child, types[kind], &kid))
            return 0;
        *number = kid.cki.cno;
        return 1;
    };
    s.thumbnail = [](void *p, ThreeMMU32 number) -> void * {
        DataBlock block;
        return static_cast<Host *>(p)->file->FFind(kctgThumbMbmp, number, &block) ? MaskedBitmapMBMP::PmbmpRead(&block)
                                                                                  : nullptr;
    };
    s.read_group = [](void *p, ThreeMMU32 number, int start, int *swap) -> void * {
        DataBlock block;
        short bo;
        if (!static_cast<Host *>(p)->file->FFind(start ? kctgStartGg : kctgFrmGg, number, &block))
            return nullptr;
        auto group = SceneData::PggReadEvents(&block, &bo, start != 0);
        if (group)
            *swap = bo == kboOther;
        return group;
    };
    s.fixed = [](void *, void *g, ThreeMMI32 index, unsigned char *bytes) {
        static_cast<PGeneralGroup>(g)->GetFixed(index, bytes);
    };
    s.object = [](void *p, ThreeMMU32 number, int kind, void *scene) -> void * {
        auto resources = static_cast<Host *>(p)->resources;
        return kind == THREE_MM_SCENE_ADD_ACTOR
                   ? static_cast<void *>(Actor::PactrRead(resources, number))
                   : static_cast<void *>(TextBox::PtboxRead(resources, number, static_cast<PScene>(scene)));
    };
    s.insert = [](void *, void *g, ThreeMMI32 index, ThreeMMI32 frame, int kind, ThreeMMI32 bytes,
                  const void *payload) -> int {
        SceneEvent event = {frame, static_cast<SceneEventType>(kind)};
        return static_cast<PGeneralGroup>(g)->FInsert(index, bytes, const_cast<void *>(payload), &event);
    };
    s.insert_tag = [](void *, void *g, ThreeMMI32 index, ThreeMMI32 frame, const ThreeMMTag *value) -> int {
        TAG tag;
        TagFromCore(&tag, *value);
        SceneEvent event = {frame, sevtSetBkgd};
        return static_cast<PGeneralGroup>(g)->FInsert(index, sizeof(tag), &tag, &event);
    };
    s.open_tag = [](void *p, ThreeMMSceneSound *sound, ThreeMMI32 index) -> int {
        auto h = static_cast<Host *>(p);
        return TagManager::FOpenTag(static_cast<PSceneSoundEvent>(sound)->Ptag(index), h->resources, h->file);
    };
    s.load_name = [](void *p, void *scene, ThreeMMU32 number) {
        static_cast<PScene>(scene)->_data.LoadName(static_cast<Host *>(p)->file, number);
    };
    return static_cast<PScene>(three_mm_scene_load(pmvie, cno, sizeof(achar), &s));
}

/****************************************************
 *
 * This routine plays all the starting events for a scene.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FPlayStartEvents(bool fActorsOnly)
{
    AssertThis(0);
    auto services = _EventServices();
    return three_mm_scene_start_events(fActorsOnly, &services) != 0;
}

/****************************************************
 *
 * This routine returns the bkgd tag in *ptag
 *
 ****************************************************/
bool Scene::FGetTagBkgd(PTAG ptag)
{
    AssertThis(0);
    AssertVarMem(ptag);

    SceneEvent sev;
    long isevStart;

    for (isevStart = 0; isevStart < _pggsevStart->IvMac(); isevStart++)
    {
        sev = *(PSEV)_pggsevStart->QvFixedGet(isevStart);
        if (sevtSetBkgd == sev.sevt)
        {
            *ptag = *(PTAG)_pggsevStart->QvGet(isevStart);
            return fTrue;
        }
    }
    return fFalse;
}

/****************************************************
 *
 * This routine writes a scene into a chunky file.
 *
 * Parameters:
 *  pcrf - Pointer to the chunky file to write to.
 *  pcno  - Cno within the chunky file written to.
 *
 * Returns:
 *  fFalse if it fails, else fTrue.
 *
 ****************************************************/
static ThreeMMSceneStoreGroups SceneStoreGroups()
{
    ThreeMMSceneStoreGroups g = {};
    g.count = [](void *, void *p) -> ThreeMMI32 { return static_cast<GroupBase *>(p)->IvMac(); };
    g.fixed = [](void *, void *p, ThreeMMI32 i, unsigned char *bytes) {
        static_cast<PGeneralGroup>(p)->GetFixed(i, bytes);
    };
    g.data = [](void *, void *p, ThreeMMI32 i, ThreeMMI32 *bytes) -> void * {
        LegacyLong cb;
        auto data = static_cast<PGeneralGroup>(p)->QvGet(i, &cb);
        *bytes = cb;
        return data;
    };
    g.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    g.free_raw = [](void *, void *p) { FreePpv(&p); };
    return g;
}
bool Scene::FWrite(PChunkyResourceFile pcrf, ChunkNumber *pcno)
{
    AssertThis(0);
    AssertPo(pcrf, 0);
    struct Host
    {
        PScene scene;
        PChunkyResourceFile resources;
        PChunkyFile file;
        DataBlock block;
    } host = {this, pcrf, pcrf->Pcfl()};
    ThreeMMSceneStoreServices s = {};
    s.context = &host;
    s.owner = &_owner;
    s.groups = SceneStoreGroups();
    s.values = SceneSoundValues();
    s.add_scene = [](void *p, ThreeMMU32 *n) -> int { return static_cast<Host *>(p)->file->FAdd(0, kctgScen, n); };
    s.delete_scene = [](void *p, ThreeMMU32 n) { static_cast<Host *>(p)->file->Delete(kctgScen, n); };
    s.new_group = [](void *) -> void * { return GeneralGroup::PggNew(sizeof(SceneEvent)); };
    s.insert = [](void *, void *p, ThreeMMI32 i, ThreeMMI32 frame, int kind, ThreeMMI32 bytes,
                  const void *data) -> int {
        SceneEvent event = {frame, static_cast<SceneEventType>(kind)};
        return static_cast<PGeneralGroup>(p)->FInsert(i, bytes, const_cast<void *>(data), &event);
    };
    s.sound_child = [](void *p, ThreeMMU32 scene, ThreeMMU32 child) -> int {
        ChildChunkIdentification kid;
        return static_cast<Host *>(p)->file->FGetKidChidCtg(kctgScen, scene, child, kctgMsnd, &kid);
    };
    s.sound_exists = [](void *p, ThreeMMU32 n) -> int { return static_cast<Host *>(p)->file->FFind(kctgMsnd, n); };
    s.adopt_sound = [](void *p, ThreeMMU32 scene, ThreeMMU32 n, ThreeMMU32 child) -> int {
        return static_cast<Host *>(p)->file->FAdoptChild(kctgScen, scene, kctgMsnd, n, child);
    };
    s.add_child = [](void *p, ThreeMMU32 scene, int kind, ThreeMMU32 child, ThreeMMU32 *n) -> int {
        return static_cast<Host *>(p)->file->FAddChild(kctgScen, scene, child, 0,
                                                       kind == THREE_MM_SCENE_ADD_ACTOR ? kctgActr : kctgTbox, n);
    };
    s.object = [](void *, void *p, ThreeMMI32 i) -> void * {
        void *object;
        static_cast<PGeneralGroup>(p)->Get(i, &object);
        return object;
    };
    s.write_object = [](void *p, void *object, int kind, ThreeMMU32 n, ThreeMMU32 scene) -> int {
        auto file = static_cast<Host *>(p)->file;
        return kind == THREE_MM_SCENE_ADD_ACTOR ? static_cast<PActor>(object)->FWrite(file, n, scene)
                                                : static_cast<PTBOX>(object)->FWrite(file, n);
    };
    s.save_background = [](void *p, void *g, ThreeMMI32 i, ThreeMMTag *out) -> int {
        auto tag = static_cast<PTAG>(static_cast<PGeneralGroup>(g)->QvGet(i));
        if (!TagManager::FSaveTag(tag, static_cast<Host *>(p)->resources, false))
            return 0;
        *out = TagToCore(*tag);
        return 1;
    };
    s.write_events = [](void *p, ThreeMMU32 scene, void *frames, void *starts) -> int {
        return SceneData::FWriteEvents(static_cast<Host *>(p)->file, scene, static_cast<PGeneralGroup>(frames),
                                       static_cast<PGeneralGroup>(starts));
    };
    s.update_thumbnail = [](void *p) { static_cast<Host *>(p)->scene->_UpdateThumbnail(); };
    s.thumbnail_size = [](void *, void *p) -> ThreeMMI32 { return static_cast<PMaskedBitmapMBMP>(p)->CbOnFile(); };
    s.add_thumbnail = [](void *p, ThreeMMI32 bytes, ThreeMMU32 *n) -> int {
        auto h = static_cast<Host *>(p);
        return h->file->FAdd(bytes, kctgThumbMbmp, n, &h->block);
    };
    s.write_thumbnail = [](void *p, void *thumbnail) -> int {
        return static_cast<PMaskedBitmapMBMP>(thumbnail)->FWrite(&static_cast<Host *>(p)->block);
    };
    s.adopt_thumbnail = [](void *p, ThreeMMU32 scene, ThreeMMU32 n) -> int {
        return static_cast<Host *>(p)->file->FAdoptChild(kctgScen, scene, kctgThumbMbmp, n, 0);
    };
    s.delete_thumbnail = [](void *p, ThreeMMU32 n) { static_cast<Host *>(p)->file->Delete(kctgThumbMbmp, n); };
    s.clear_loner = [](void *p, ThreeMMU32 n) { static_cast<Host *>(p)->file->SetLoner(kctgThumbMbmp, n, false); };
    s.write_metadata = [](void *p, ThreeMMU32 n) -> int {
        auto h = static_cast<Host *>(p);
        return h->scene->_data.FWrite(h->file, n);
    };
    s.diagnostic = [](void *, int) { Bug("Bad event in frame event list"); };
    return three_mm_scene_store(pcno, &s) != 0;
}

/****************************************************
 *
 * This routine resolves all sound tags
 *
 * Parameters:
 *  None
 * Returns:
 *  None
 *
 ****************************************************/
bool Scene::FResolveAllSndTags(ChunkNumber cnoScen)
{
    AssertThis(0);
    ThreeMMSceneResolveServices s = {};
    s.context = this;
    s.owner = &_owner;
    s.packets = ScenePacketServices(_pmvie);
    auto owners = _OwnerServices();
    s.count = owners.count;
    s.kind = owners.kind;
    s.actor = [](void *p, ThreeMMI32 index, ThreeMMU32 scene) -> int {
        PActor actor;
        static_cast<PScene>(p)->_pglpactr->Get(index, &actor);
        return actor->FResolveAllSndTags(scene);
    };
    s.lock = [](void *, void *g) { static_cast<PGeneralGroup>(g)->Lock(); };
    s.unlock = [](void *, void *g) { static_cast<PGeneralGroup>(g)->Unlock(); };
    s.sound = [](void *, void *g, ThreeMMI32 index) -> ThreeMMSceneSound * {
        return static_cast<PSceneSoundEvent>(static_cast<PGeneralGroup>(g)->QvGet(index));
    };
    s.resolve = [](void *p, void *record, ThreeMMU32 scene) -> int {
        auto tag = static_cast<PTagChildPair>(record);
        return static_cast<PScene>(p)->_pmvie->FResolveSndTag(&tag->tag, tag->chid, scene);
    };
    return three_mm_scene_resolve_sounds(cnoScen, &s) != 0;
}

/****************************************************
 *
 * This routine removes all actors from movie's roll call.
 *
 * Parameters:
 *  fDelIfOnlyRef  --  fTrue indicates to remove the roll-call entry
 *        if a given actr is only referenced by this scene.
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::RemActrsFromRollCall(bool only)
{
    AssertThis(0);
    auto s = _ContentServices();
    three_mm_scene_roll_remove(only, &s);
}

/****************************************************
 *
 * This routine adds all actors to movie's roll call.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddActrsToRollCall(void)
{
    AssertThis(0);
    auto s = _ContentServices();
    return three_mm_scene_roll_add(&s) != 0;
}

/****************************************************
 *
 * This routine returns the scene's thumbnail.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  None
 *
 ****************************************************/
PMaskedBitmapMBMP Scene::PmbmpThumbnail(void)
{
    AssertThis(0);

    _UpdateThumbnail();
    return (_pmbmp);
}

/****************************************************
 *
 * This routine updates the mbmp associated with the
 * thumbnail for the scene.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::_UpdateThumbnail(void)
{
    AssertThis(0);

    long nfrmCur;
    PGraphicsPort pgpt, pgptThumb;
    PMovieView pmvu;
    PTBOX ptbox = PtboxSelected();
    PActor pactr = PactrSelected();
    RC rc, rcThumb;
    long grfscenSave;
    long dtimSnd;

    dtimSnd = Pmvie()->Pmsq()->DtimSnd();
    Pmvie()->Pmsq()->SndOff();

    pmvu = (PMovieView)Pmvie()->PddgGet(0);
    if ((_pbkgd == pvNil) || (pmvu == pvNil))
    {
        goto LEnd;
    }
    AssertPo(pmvu, 0);

    if (!Pmvie()->FDirty())
    {
        goto LEnd;
    }

    rc.Set(0, 0, Pmvie()->Pmcc()->Dxp(), Pmvie()->Pmcc()->Dyp());
    pgpt = GraphicsPort::PgptNewOffscreen(&rc, 8);

    if (pgpt == pvNil)
    {
        goto LEnd;
    }

    AssertPo(pgpt, 0);

    rcThumb.Set(0, 0, kdxpThumbnail, kdypThumbnail);
    pgptThumb = GraphicsPort::PgptNewOffscreen(&rcThumb, 8);

    if (pgptThumb == pvNil)
    {
        ReleasePpo(&pgpt);
        goto LEnd;
    }

    AssertPo(pgptThumb, 0);

    pgptThumb->SetOffscreenColors(Pmvie()->PglclrThumbPalette());

    grfscenSave = _runtime.disabled;
    Disable(fscenPauses | fscenSounds);

    nfrmCur = _data.currentFrame;
    if ((_data.currentFrame != _data.firstFrame) && !FGotoFrm(_data.firstFrame))
    {
        ReleasePpo(&pgpt);
        ReleasePpo(&pgptThumb);
        _runtime.disabled = grfscenSave;
        goto LEnd;
    }

    if (pmvu->FTextMode())
    {
        SelectTbox(pvNil);
    }
    else
    {
        SelectActr(pvNil);
    }
    pmvu->DrawTree(pgpt, pvNil, &rc, fgobNoVis);
    if (pmvu->FTextMode())
    {
        SelectTbox(ptbox);
    }
    else
    {
        SelectActr(pactr);
    }

    BLOCK
    {
        GraphicsEnvironment gnv(pgpt);
        GraphicsEnvironment gnvThumb(pgptThumb);
        gnvThumb.CopyPixels(&gnv, &rc, &rcThumb);

        _DropOwner(THREE_MM_SCENE_THUMBNAIL);

        _SetOwner(THREE_MM_SCENE_THUMBNAIL, MaskedBitmapMBMP::PmbmpNew(pgptThumb->PrgbLockPixels(), pgptThumb->CbRow(), kdypThumbnail, &rcThumb, 0, 0,
                                kbTransparent));
        pgptThumb->Unlock();

        ReleasePpo(&pgpt);
        ReleasePpo(&pgptThumb);
        if ((nfrmCur > _data.firstFrame) && (nfrmCur <= _data.lastFrame))
        {
            FGotoFrm(nfrmCur);
        }

        _runtime.disabled = grfscenSave;
    }

LEnd:

    Pmvie()->Pmsq()->SndOnDtim(dtimSnd);
    return;
}

/****************************************************
 *
 * This routine marks the movie as dirty.
 *
 * Parameters:
 *  fDirty - Set dirty or not.
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::MarkDirty(bool fDirty)
{
    AssertThis(0);
    if (fDirty)
    {
        _MarkMovieDirty();
    }
}

/****************************************************
 *
 * This routine pastes an actor into the current scene
 *
 * Parameters:
 *  pactr - Pointer to the actor to paste
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FPasteActrCore(PActor actor)
{
    AssertThis(0);
    auto s = _ContentServices();
    return three_mm_scene_actor_paste(actor, &s) != 0;
}

/****************************************************
 *
 * This routine pastes an actor into the current scene
 * and creates an undo object for the action.
 *
 * NOTE: This does not need an undo object, since the
 * tool is getting set to a "place" tool, which will
 * create the appropriate undo type.
 *
 * Parameters:
 *  pactr - Pointer to the actor to paste
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FPasteActr(PActor pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    if (!FPasteActrCore(pactr))
    {
        return (fFalse);
    }

    return (fTrue);
}

/****************************************************
 *
 * This routine makes all actors go to a specific frame.
 *
 * Parameters:
 *  nfrm - Frame number to go to.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::_FForceActorsToFrm(long nfrm, bool *pfSoundInFrame)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    int sound = pfSoundInFrame ? *pfSoundInFrame : 0;
    bool result = three_mm_scene_force(nfrm, 0, pfSoundInFrame ? &sound : nullptr, &services) != 0;
    if (pfSoundInFrame) *pfSoundInFrame = sound != 0;
    return result;
}

/****************************************************
 *
 * This routine makes all text boxes go to a specific frame.
 *
 * Parameters:
 *  nfrm - Frame number to go to.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::_FForceTboxesToFrm(long nfrm)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    return three_mm_scene_force(nfrm, 1, nullptr, &services) != 0;
}

/****************************************************
 *
 * Hides all text boxes in this scene
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::HideTboxes(void)
{
    AssertThis(0);
    auto services = _OwnerServices();
    three_mm_scene_owner_hide_texts(&_owner, &services);
}

/****************************************************
 *
 * Hides all actors in this scene
 *
 * Parameters:
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::HideActors(void)
{
    AssertThis(0);
    auto services = _OwnerServices();
    three_mm_scene_owner_actors(&_owner, 0, &services);
}

/****************************************************
 *
 * Shows all actors in this scene
 *
 * Parameters:
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::ShowActors(void)
{
    AssertThis(0);
    auto services = _OwnerServices();
    three_mm_scene_owner_actors(&_owner, 1, &services);
}

/****************************************************
 *
 * This routine marks the movie dirty.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::_MarkMovieDirty()
{
    AssertThis(0);

    if (_pmvie != pvNil)
    {
        _pmvie->SetDirty();
    }
}

/****************************************************
 *
 * This routine sets the scene to the given movie
 *
 * Parameters:
 *  pmvie - The new parent movie
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::SetMvie(PMovie movie)
{
    AssertThis(0);
    auto s = _ContentServices();
    three_mm_scene_content_bind_movie(movie, &s);
}

/****************************************************
 *
 * This routine reads from a chunky file and creates
 * a list of all the tags in the scene.
 *
 * Parameters:
 *  pcfl - Pointer to the chunky file to read from.
 *  cno  - Cno within the chunky file to read.
 *  ptagl - tag list to put tags in
 *
 * Returns:
 *  fFalse if an error occurred, else fTrue
 *
 ****************************************************/
bool Scene::FAddTagsToTagl(PChunkyFile pcfl, ChunkNumber cno, PTagList ptagl)
{
    AssertPo(pcfl, 0);
    AssertPo(ptagl, 0);
    struct Host
    {
        PChunkyFile file;
        PTagList tags;
    } host = {pcfl, ptagl};
    ThreeMMSceneCollectServices s = {};
    s.context = &host;
    s.groups = SceneStoreGroups();
    s.packets = ScenePacketServices();
    s.camera_type = kctgCam;
    s.read_group = [](void *p, ThreeMMU32 scene, int start, int *swap) -> void * {
        auto file = static_cast<Host *>(p)->file;
        ChildChunkIdentification kid;
        DataBlock block;
        short bo;
        if (!file->FGetKidChidCtg(kctgScen, scene, start ? 1 : 0, start ? kctgStartGg : kctgFrmGg, &kid) ||
            !file->FFind(kid.cki.ctg, kid.cki.cno, &block))
            return nullptr;
        auto group = GeneralGroup::PggRead(&block, &bo);
        if (group && group->CbFixed() != sizeof(SceneEvent))
        {
            ReleasePpo(&group);
            return nullptr;
        }
        if (group)
            *swap = bo == kboOther;
        return group;
    };
    s.actor_child = [](void *p, ThreeMMU32 scene, ThreeMMU32 child, ThreeMMU32 *n) -> int {
        ChildChunkIdentification kid;
        if (!static_cast<Host *>(p)->file->FGetKidChidCtg(kctgScen, scene, child, kctgActr, &kid))
            return 0;
        *n = kid.cki.cno;
        return 1;
    };
    s.actor_tags = [](void *p, ThreeMMU32 n, int *error) -> void * {
        bool failed;
        auto tags = Actor::PgltagFetch(static_cast<Host *>(p)->file, n, &failed);
        *error = failed;
        return tags;
    };
    s.list_tag = [](void *, void *p, ThreeMMI32 i, ThreeMMTag *out) {
        TAG tag;
        static_cast<PDynamicArray>(p)->Get(i, &tag);
        *out = TagToCore(tag);
    };
    s.insert_tag = [](void *p, const ThreeMMTag *value) -> int {
        TAG tag;
        TagFromCore(&tag, *value);
        return static_cast<Host *>(p)->tags->FInsertTag(&tag);
    };
    s.background = [](void *p, ThreeMMTag *value) -> int {
        TAG tag;
        TagFromCore(&tag, *value);
        bool ok = Background::FAddTagsToTagl(&tag, static_cast<Host *>(p)->tags);
        *value = TagToCore(tag);
        return ok;
    };
    s.insert_child = [](void *p, const ThreeMMTag *value, ThreeMMU32 child, ThreeMMU32 type) -> int {
        TAG tag;
        TagFromCore(&tag, *value);
        return static_cast<Host *>(p)->tags->FInsertChild(&tag, child, type);
    };
    s.diagnostic = [](void *, int start, int missing) {
        if (missing)
            Bug("no background event!?");
        else if (start)
            Bug("Bad event in start event list");
        else
            Bug("Bad event in frame event list");
    };
    return three_mm_scene_collect_tags(cno, &s) != 0;
}

/****************************************************
 *
 * This returns the actor in the current scene with
 * the given arid.
 *
 * Parameters:
 *   Arid to look for.
 *
 * Returns:
 *   pvNil if failure, else the actor.
 *
 ****************************************************/
PActor Scene::PactrFromArid(long id)
{
    AssertThis(0);
    auto s = _ContentServices();
    return static_cast<PActor>(three_mm_scene_actor_find(id, &s));
}

/****************************************************
 *
 * This routine chops off the rest of the scene.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool Scene::FChopCore()
{
    AssertThis(0);

    PTBOX ptbox;
    PActor pactr;
    bool fAlive;
    long ipo;
    long nfrmStart, nfrmLast;

    if (_data.currentFrame == _data.lastFrame)
    {
        return (fTrue);
    }

    //
    // Chop all actors off
    //
    for (ipo = 0; ipo < _pglpactr->IvMac();)
    {
        _pglpactr->Get(ipo, &pactr);
        AssertPo(pactr, 0);
        pactr->DeleteFwdCore(fTrue, &fAlive);

        if (!fAlive)
        {
            RemActrCore(pactr->Arid());
        }
        else
        {
            ipo++;
        }
    }

    //
    // Chop all text boxes off
    //
    for (ipo = 0; ipo < _pglptbox->IvMac();)
    {
        _pglptbox->Get(ipo, &ptbox);
        AssertPo(ptbox, 0);

        if (ptbox->FGetLifetime(&nfrmStart, &nfrmLast) && (nfrmStart > _data.currentFrame))
        {
            //
            // Ok if this fails.
            //
            FRemTboxCore(ptbox);
        }
        else
        {
            if (nfrmLast >= _data.currentFrame)
            {

                //
                // Here we extend the lifetime of the text box to
                // infinite, cuz we removed the frame with the
                // Hide().
                //
                if (ptbox->FGotoFrame(klwMax - 1))
                {
                    ptbox->FShowCore();
                    ptbox->FGotoFrame(_data.currentFrame);
                }
            }

            ipo++;
        }
    }

    //
    // Remove all scene events from here forward
    //
    for (; _runtime.next_event < _pggsevFrm->IvMac();)
    {
        _pggsevFrm->Delete(_runtime.next_event);
    }

    //
    // Set new limit
    //
    if (_data.lastFrame != _data.currentFrame)
    {
        _data.lastFrame = _data.currentFrame;
        Pmvie()->SetDirty();
    }

    return (fTrue);
}

/****************************************************
 *
 * This routine chops off the rest of the scene and
 * creates an undo object as well.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool Scene::FChop()
{
    PSceneUndoChop psunc;
    bool fValid;

    if (_data.currentFrame == _data.lastFrame)
    {
        return (fTrue);
    }

    psunc = SceneUndoChop::PsuncNew();
    if (psunc != pvNil)
    {
        fValid = psunc->FSave(this);
    }
    else
    {
        fValid = fFalse;
    }

    if (!fValid || !Pmvie()->FAddUndo(psunc))
    {
        ReleasePpo(&psunc);
        return (fFalse);
    }

    ReleasePpo(&psunc);

    if (!FChopCore())
    {
        Pmvie()->ClearUndo();
        return (fFalse);
    }

    return (fTrue);
}

/****************************************************
 *
 * This routine chops off the rest of the scene, backwards
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool Scene::FChopBackCore()
{
    AssertThis(0);

    PTBOX ptbox;
    PActor pactr;
    SceneEvent sev;
    bool fAlive;
    bool fCopyCam;
    long ipo;
    long nfrmStart, nfrmLast;

    if (_data.currentFrame == _data.firstFrame)
    {
        return (fTrue);
    }

    //
    // Chop all actors off
    //
    for (ipo = 0; ipo < _pglpactr->IvMac();)
    {
        _pglpactr->Get(ipo, &pactr);
        AssertPo(pactr, 0);
        pactr->DeleteBackCore(&fAlive);

        if (!fAlive)
        {
            RemActrCore(pactr->Arid());
        }
        else
        {
            ipo++;
        }
    }

    //
    // Chop all text boxes off
    //
    for (ipo = 0; ipo < _pglptbox->IvMac();)
    {
        _pglptbox->Get(ipo, &ptbox);
        AssertPo(ptbox, 0);

        if (ptbox->FGetLifetime(&nfrmStart, &nfrmLast) && (nfrmLast < _data.currentFrame))
        {
            //
            // Ok if this fails.
            //
            FRemTboxCore(ptbox);
        }
        else
        {
            if (nfrmStart < _data.currentFrame)
            {
                Assert(ptbox->FIsVisible(), "Bad tbox");
                ptbox->SetStartFrame(_data.currentFrame);
            }

            ipo++;
        }
    }

    //
    // Remove all scene events from here backward, except camera
    // changes, keep the most recent camera change.
    //
    fCopyCam = fTrue;
    for (; _runtime.next_event > 0;)
    {
        _runtime.next_event--;

        _pggsevFrm->GetFixed(_runtime.next_event, &sev);

        if ((sev.sevt == sevtChngCamera) && (fCopyCam))
        {

            fCopyCam = fFalse;
            sev.nfrm = _data.currentFrame;
            _pggsevFrm->PutFixed(_runtime.next_event, &sev);
        }
        else
        {

            if (sev.nfrm == _data.currentFrame)
            {
                continue;
            }

            _pggsevFrm->Delete(_runtime.next_event);
        }
    }

    _runtime.next_event = 0;
    for (; _runtime.next_event < _pggsevFrm->IvMac(); _runtime.next_event++)
    {

        _pggsevFrm->GetFixed(_runtime.next_event, &sev);
        if (sev.nfrm != _data.currentFrame)
        {
            break;
        }
    }

    //
    // Set new limit
    //
    if (_data.firstFrame != _data.currentFrame)
    {
        _data.firstFrame = _data.currentFrame;
        Pmvie()->SetDirty();
    }

    //
    // If _psseBkgd got chopped, get rid of it
    //
    if (_psseBkgd != pvNil && _runtime.background_frame < _data.firstFrame)
    {
        _DropOwner(THREE_MM_SCENE_BACKGROUND_SOUND);
        TrashVar(&_runtime.background_frame);
    }

    return (fTrue);
}

/****************************************************
 *
 * This routine chops off the rest of the scene, backwards,
 * and creates an undo object as well.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool Scene::FChopBack()
{
    PSceneUndoChop psunc;
    bool fValid;

    if (_data.currentFrame == _data.firstFrame)
    {
        return (fTrue);
    }

    psunc = SceneUndoChop::PsuncNew();
    if (psunc != pvNil)
    {
        fValid = psunc->FSave(this);
    }
    else
    {
        fValid = fFalse;
    }

    if (!fValid || !Pmvie()->FAddUndo(psunc))
    {
        ReleasePpo(&psunc);
        return (fFalse);
    }

    ReleasePpo(&psunc);

    if (!FChopBackCore())
    {
        Pmvie()->ClearUndo();
        return (fFalse);
    }

    return (fTrue);
}

/****************************************************
 *
 * This routine does any startup for playback
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   None.
 *
 ****************************************************/
bool Scene::FStartPlaying()
{
    AssertThis(0);

    long ipactr, iptbox;
    PActor pactr;
    PTBOX ptbox;
    PTextBoxGobject ptbxg;
    long nfrmFirst, nfrmLast;

    //
    // Make sure all actors have state variables updated
    // (they might not be in the theater right after
    // loading the movie)
    //
    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        if (!pactr->FGetLifetime(&nfrmFirst, &nfrmLast))
            return fFalse;
    }

    for (iptbox = 0; iptbox < _pglptbox->IvMac(); iptbox++)
    {
        _pglptbox->Get(iptbox, &ptbox);
        if ((ptbox->FStory() && ptbox->FIsVisible()) ||
            (!ptbox->FStory() && ptbox->FGetLifetime(&nfrmFirst, &nfrmLast) && (nfrmFirst == _data.currentFrame)))
        {
            ptbxg = (PTextBoxGobject)ptbox->PddgGet(0);
            ptbxg->Scroll(scaNil);
        }
    }

    //
    // Start prerendering at this frame, even if there's
    // no camera view change.
    //
    _DoPrerenderingWork(fTrue);
    return fTrue;
}

/****************************************************
 *
 * This routine cleans up after a playback has stopped.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   None.
 *
 ****************************************************/
void Scene::StopPlaying()
{
    AssertThis(0);

    long itbox;
    PTBOX ptbox;

    _EndPrerendering(); // Stop prerendering

    for (itbox = 0; itbox < _pglptbox->IvMac(); itbox++)
    {
        _pglptbox->Get(itbox, &ptbox);
        AssertPo(ptbox, 0);
        ptbox->CleanDdg();
    }
}

/******************************************************************************
    FTransOnFile
        For a given Scene chunk on a given ChunkyResourceFile, get the scene transition
        state for the scene.

    Arguments:
        PChunkyResourceFile pcrf     -- the chunky resource file the Scene lives on
        ChunkNumber cno       -- the ChunkNumber of the Scene chunk
        TRANS *ptrans -- pointer to memory to take the transition setting

    Returns: fTrue if it was able to set *ptrans, fFalse if something failed

************************************************************ PETED ***********/
bool Scene::FTransOnFile(PChunkyResourceFile pcrf, ChunkNumber cno, TRANS *ptrans)
{
    SceneData metadata;
    if (!metadata.FRead(pcrf->Pcfl(), cno))
        return fFalse;
    *ptrans = static_cast<TRANS>(metadata.transition);
    return fTrue;
}

/******************************************************************************
    FSetTransOnFile
        For a given Scene chunk on a given ChunkyResourceFile, set the scene transition
        state for the scene.

    Arguments:
        PChunkyResourceFile pcrf   -- the chunky resource file the Scene lives on
        ChunkNumber cno     -- the ChunkNumber of the Scene chunk
        TRANS trans -- the transition state to use

    Returns: fTrue if the routine could guarantee that the scene has the
        given transition state.

************************************************************ PETED ***********/
bool Scene::FSetTransOnFile(PChunkyResourceFile pcrf, ChunkNumber cno, TRANS trans)
{
    SceneData metadata;
    if (!metadata.FRead(pcrf->Pcfl(), cno))
        return fFalse;
    metadata.transition = trans;
    return metadata.FWrite(pcrf->Pcfl(), cno);
}

//
//
//
// UNDO STUFF
//
//
//

/****************************************************
 *
 * Public constructor for scene undo objects for name
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoTitle SceneUndoTitle::PsuntNew()
{
    PSceneUndoTitle psunt;
    psunt = NewObj SceneUndoTitle();
    return (psunt);
}

/****************************************************
 *
 * Destructor for scene naming undo objects
 *
 ****************************************************/
SceneUndoTitle::~SceneUndoTitle(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoTitle::FDo(PDocumentBase pdocb)
{
    AssertThis(0);

    String stn;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        _pmvie->Pmsq()->FlushMsq();
        _pmvie->ClearUndo();
        return (fFalse);
    }

    _pmvie->Pscen()->GetName(&stn);
    _pmvie->Pscen()->SetNameCore(&_stn);
    _stn = stn;

    _pmvie->Pmsq()->FlushMsq();

    return (fTrue);
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoTitle::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);

    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoTitle
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoTitle::MarkMem(void)
{
    AssertThis(0);
    SceneUndoTitle_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the SceneUndoTitle.
***************************************************************************/
void SceneUndoTitle::AssertValid(LegacyUlong grf)
{
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for sound
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoSound SceneUndoSound::PsunsNew()
{
    PSceneUndoSound psuns;
    psuns = NewObj SceneUndoSound();
    return (psuns);
}

/****************************************************
 *
 * Destructor for scene sound undo objects
 *
 ****************************************************/
SceneUndoSound::~SceneUndoSound(void)
{
    AssertBaseThis(0);
    auto values = _Values();
    three_mm_scene_sound_undo_dispose(&_sound, &values);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoSound::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    auto services = _Services();
    return three_mm_scene_sound_undo_swap(&_sound, &services) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoSound::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    return FDo(pdocb);
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoSound
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoSound::MarkMem(void)
{
    AssertThis(0);
    SceneUndoSound_PAR::MarkMem();
    if (_sound.sound != pvNil)
        MarkPv(_sound.sound);
}

/***************************************************************************
    Assert the validity of the SceneUndoSound.
***************************************************************************/
void SceneUndoSound::AssertValid(LegacyUlong grf)
{
    SceneUndoSound_PAR::AssertValid(grf);
    if (_sound.sound != pvNil)
    {
        AssertPvCb(_sound.sound, size(SceneSoundEvent));
        AssertPvCb(_sound.sound, three_mm_scene_sound_size(_sound.sound->ctagc, size(TagChildPair)));
        Assert(_sound.style == _sound.sound->sty, "sty's don't match");
    }
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for actor
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneActorUndo SceneActorUndo::PsunaNew()
{
    PSceneActorUndo psuna;
    psuna = NewObj SceneActorUndo();
    return (psuna);
}

/****************************************************
 *
 * Destructor for scene actor undo objects
 *
 ****************************************************/
static ThreeMMSceneContentServices SceneContentReleaseServices()
{
    ThreeMMSceneContentServices s = {};
    s.events.owners.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    return s;
}
struct SceneContentUndoHost {PMovie movie;long scene,frame;};
static ThreeMMSceneContentUndoServices SceneContentUndoServices(SceneContentUndoHost *h)
{
    ThreeMMSceneContentUndoServices s = {};
    s.context = h;
    s.frame = h->frame;
    s.switch_scene = [](void *p) -> int {
        auto h = static_cast<SceneContentUndoHost *>(p);
        return h->movie->FSwitchScen(h->scene);
    };
    s.seek = [](void *p, ThreeMMI32 frame) -> int {
        return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->FGotoFrm(frame);
    };
    s.duplicate = [](void *, void *p) -> void * {
        PActor copy = nullptr;
        return static_cast<PActor>(p)->FDup(&copy, true) ? copy : nullptr;
    };
    s.flush = [](void *p) { static_cast<SceneContentUndoHost *>(p)->movie->Pmsq()->FlushMsq(); };
    s.clear = [](void *p) { static_cast<SceneContentUndoHost *>(p)->movie->ClearUndo(); };
    s.update_roll = [](void *p) { static_cast<SceneContentUndoHost *>(p)->movie->Pmcc()->UpdateRollCall(); };
    return s;
}
SceneActorUndo::~SceneActorUndo(void)
{
    AssertBaseThis(0);
    auto s = SceneContentReleaseServices();
    three_mm_scene_actor_undo_dispose(&_content, &s);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneActorUndo::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneContentUndoHost host = {_pmvie, _iscen, _nfrm};
    auto s = SceneContentUndoServices(&host);
    s.scene = [](void *p) { return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->_ContentServices(); };
    return three_mm_scene_actor_undo(&_content, 1, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneActorUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneContentUndoHost host = {_pmvie, _iscen, _nfrm};
    auto s = SceneContentUndoServices(&host);
    s.scene = [](void *p) { return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->_ContentServices(); };
    return three_mm_scene_actor_undo(&_content, 0, &s) != 0;
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneActorUndo
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneActorUndo::MarkMem(void)
{
    AssertThis(0);
    SceneActorUndo_PAR::MarkMem();
    MarkMemObj(static_cast<PActor>(_content.actor));
}

/***************************************************************************
    Assert the validity of the SceneActorUndo.
***************************************************************************/
void SceneActorUndo::AssertValid(LegacyUlong grf)
{
    AssertPo(static_cast<PActor>(_content.actor), 0);
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for text box
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoText SceneUndoText::PsunxNew()
{
    PSceneUndoText psunx;
    psunx = NewObj SceneUndoText();
    return (psunx);
}

/****************************************************
 *
 * Destructor for scene text box undo objects
 *
 ****************************************************/
SceneUndoText::~SceneUndoText(void)
{
    AssertBaseThis(0);
    auto s = SceneContentReleaseServices();
    three_mm_scene_text_undo_dispose(&_content, &s);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoText::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneContentUndoHost host = {_pmvie, _iscen, _nfrm};
    auto s = SceneContentUndoServices(&host);
    s.scene = [](void *p) { return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->_ContentServices(); };
    return three_mm_scene_text_undo(&_content, 1, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoText::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneContentUndoHost host = {_pmvie, _iscen, _nfrm};
    auto s = SceneContentUndoServices(&host);
    s.scene = [](void *p) { return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->_ContentServices(); };
    return three_mm_scene_text_undo(&_content, 0, &s) != 0;
}
#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoText
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoText::MarkMem(void)
{
    AssertThis(0);
    SceneUndoText_PAR::MarkMem();
    MarkMemObj(static_cast<PTBOX>(_content.text));
}

/***************************************************************************
    Assert the validity of the SceneUndoText.
***************************************************************************/
void SceneUndoText::AssertValid(LegacyUlong grf)
{
    AssertPo(static_cast<PTBOX>(_content.text), 0);
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for transition
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoTransition SceneUndoTransition::PsunrNew()
{
    PSceneUndoTransition psunr;
    psunr = NewObj SceneUndoTransition();
    return (psunr);
}

/****************************************************
 *
 * Destructor for scene transition undo objects
 *
 ****************************************************/
SceneUndoTransition::~SceneUndoTransition(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoTransition::FDo(PDocumentBase pdocb)
{
    AssertThis(0);

    TRANS trans;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        goto LFail;
    }

    if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
    {
        goto LFail;
    }

    trans = _pmvie->Pscen()->Trans();
    _pmvie->Pscen()->SetTransitionCore(_trans);
    _trans = trans;

    _pmvie->Pmsq()->FlushMsq();

    return (fTrue);

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    return (fFalse);
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoTransition::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoTransition
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoTransition::MarkMem(void)
{
    AssertThis(0);
    SceneUndoTransition_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the SceneUndoTransition.
***************************************************************************/
void SceneUndoTransition::AssertValid(LegacyUlong grf)
{
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for pause
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoPause SceneUndoPause::PsunpNew()
{
    PSceneUndoPause psunp;
    psunp = NewObj SceneUndoPause();
    return (psunp);
}

/****************************************************
 *
 * Destructor for scene pause undo objects
 *
 ****************************************************/
SceneUndoPause::~SceneUndoPause(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
struct SceneEditUndoHost {PMovie movie;long scene,frame;};
static ThreeMMSceneEditUndoServices SceneEditUndoServices(SceneEditUndoHost *host)
{
    ThreeMMSceneEditUndoServices s = {};
    s.context = host;
    s.switch_scene = [](void *p) -> int {
        auto h = static_cast<SceneEditUndoHost *>(p);
        return h->movie->FSwitchScen(h->scene);
    };
    s.seek_frame = [](void *p) -> int {
        auto h = static_cast<SceneEditUndoHost *>(p);
        return h->movie->Pscen()->FGotoFrm(h->frame);
    };
    s.pause = [](void *p, ThreeMMI32 *reason, ThreeMMI32 *delay) -> int {
        auto h = static_cast<SceneEditUndoHost *>(p);
        WaitReason r = static_cast<WaitReason>(*reason);
        long d = *delay;
        bool ok = h->movie->Pscen()->FPauseCore(&r, &d);
        *reason = r;
        *delay = d;
        return ok;
    };
    s.background_present = [](void *p) -> int {
        return static_cast<SceneEditUndoHost *>(p)->movie->Pscen()->Pbkgd() != nullptr;
    };
    s.current_camera = [](void *p) -> ThreeMMI32 {
        return static_cast<SceneEditUndoHost *>(p)->movie->Pscen()->Pbkgd()->Icam();
    };
    s.background = [](void *p, const ThreeMMTag *value, ThreeMMTag *old) -> int {
        TAG tag, previous;
        TagFromCore(&tag, *value);
        bool ok = static_cast<SceneEditUndoHost *>(p)->movie->Pscen()->FSetBkgdCore(&tag, &previous);
        if (ok)
            *old = SceneEditTag(previous);
        return ok;
    };
    s.camera = [](void *p, ThreeMMI32 camera, ThreeMMI32 *old) -> int {
        long previous;
        bool ok = static_cast<SceneEditUndoHost *>(p)->movie->Pscen()->FChangeCamCore(camera, &previous);
        *old = previous;
        return ok;
    };
    s.pause_type = [](void *p, ThreeMMI32 reason) {
        static_cast<SceneEditUndoHost *>(p)->movie->Pmcc()->PauseType(static_cast<WaitReason>(reason));
    };
    s.flush = [](void *p) { static_cast<SceneEditUndoHost *>(p)->movie->Pmsq()->FlushMsq(); };
    s.clear = [](void *p) { static_cast<SceneEditUndoHost *>(p)->movie->ClearUndo(); };
    return s;
}
bool SceneUndoPause::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneEditUndoHost host = {_pmvie, _iscen, _nfrm};
    auto s = SceneEditUndoServices(&host);
    return three_mm_scene_pause_undo(&_edit, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoPause::FUndo(PDocumentBase pdocb)
{
    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoPause
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoPause::MarkMem(void)
{
    AssertThis(0);
    SceneUndoPause_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the SceneUndoPause.
***************************************************************************/
void SceneUndoPause::AssertValid(LegacyUlong grf)
{
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for background
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoBackground SceneUndoBackground::PsunkNew()
{
    PSceneUndoBackground psunk;
    psunk = NewObj SceneUndoBackground();
    return (psunk);
}

/****************************************************
 *
 * Destructor for scene background undo objects
 *
 ****************************************************/
SceneUndoBackground::~SceneUndoBackground(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoBackground::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneEditUndoHost host = {_pmvie, _iscen, _nfrm};
    auto s = SceneEditUndoServices(&host);
    return three_mm_scene_background_undo(&_edit, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoBackground::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoBackground
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoBackground::MarkMem(void)
{
    AssertThis(0);
    SceneUndoBackground_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the SceneUndoBackground.
***************************************************************************/
void SceneUndoBackground::AssertValid(LegacyUlong grf)
{
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for background
 * chop commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoChop SceneUndoChop::PsuncNew()
{
    PSceneUndoChop psunc;
    psunc = NewObj SceneUndoChop();
    return (psunc);
}

/****************************************************
 *
 * Destructor for scene pause undo objects
 *
 ****************************************************/
SceneUndoChop::~SceneUndoChop(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pcrf);
}

/****************************************************
 *
 * This function saves away a copy of the scene.
 *
 * Parameters:
 *  pscen - The scene to save away.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoChop::FSave(PScene pscen)
{
    PChunkyFile pcfl;
    bool fRet;

    pcfl = ChunkyFile::PcflCreateTemp();
    if (pcfl == pvNil)
    {
        return (fFalse);
    }

    _pcrf = ChunkyResourceFile::PcrfNew(pcfl, 0);
    if (_pcrf == pvNil)
    {
        ReleasePpo(&pcfl);
        return (fFalse);
    }
    ReleasePpo(&pcfl);

    vpappb->BeginLongOp();
    fRet = pscen->FWrite(_pcrf, &_cno);
    vpappb->EndLongOp();

    return (fRet);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoChop::FDo(PDocumentBase pdocb)
{
    AssertThis(0);

    PScene pscen;
    PScene pscenSave;

    vpappb->BeginLongOp();

    pscen = Scene::PscenRead(_pmvie, _pcrf, _cno);

    if (pscen == pvNil)
    {
        goto LFail;
    }

    if (!pscen->FPlayStartEvents())
    {
        Scene::Close(&pscen);
        goto LFail;
    }

    if (!_pmvie->FSwitchScen(_iscen))
    {
        Scene::Close(&pscen);
        goto LFail;
    }

    pscenSave = _pmvie->Pscen();
    pscenSave->AddRef();

    if (!_pmvie->FInsScenCore(_iscen, pscen))
    {
        ReleasePpo(&pscenSave);
        Scene::Close(&pscen);
        goto LFail;
    }

    Scene::Close(&pscen);

    _pcrf->Pcfl()->Delete(kctgScen, _cno);
    pscenSave->SetNfrmCur(pscenSave->NfrmFirst() - 1);
    if (!pscenSave->FWrite(_pcrf, &_cno))
    {
        _pmvie->FRemScenCore(_iscen + 1);
        Scene::Close(&pscenSave);
        goto LFail;
    }

    Scene::Close(&pscenSave);

    if (!_pmvie->FRemScenCore(_iscen + 1))
    {
        goto LFail;
    }

    //
    // We don't care if we can't switch, everything was restored.
    //
    if (_pmvie->FSwitchScen(_iscen))
    {
        _pmvie->Pscen()->FGotoFrm(_nfrm);
        _pmvie->InvalViewsAndScb();
    }

    vpappb->EndLongOp();

    return (fTrue);

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    vpappb->EndLongOp();
    return (fFalse);
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoChop::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoChop
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoChop::MarkMem(void)
{
    AssertThis(0);
    SceneUndoChop_PAR::MarkMem();
    MarkMemObj(_pcrf);
}

/***************************************************************************
    Assert the validity of the SceneUndoChop.
***************************************************************************/
void SceneUndoChop::AssertValid(LegacyUlong grf)
{
    AssertNilOrPo(_pcrf, grf);
}
#endif

/***************************************************************************
    Static function to allocate a SceneSoundEvent with room for ctag TAGs.
***************************************************************************/
PSceneSoundEvent SceneSoundEvent::PsseNew(long ctag)
{
    Assert(ctag > 0, 0);
    auto services = ScenePacketServices();
    return static_cast<PSceneSoundEvent>(three_mm_scene_sound_allocate(ctag, &services));
}

/***************************************************************************
    Creates a new SceneSoundEvent
***************************************************************************/
PSceneSoundEvent SceneSoundEvent::PsseNew(long vlm, long sty, bool fLoop, long ctagc, TagChildPair *prgtagc)
{
    Assert(ctagc > 0, 0);
    AssertPvCb(prgtagc, LwMul(ctagc, size(TagChildPair)));
    auto services = ScenePacketServices();
    return static_cast<PSceneSoundEvent>(three_mm_scene_sound_make(vlm, sty, fLoop, ctagc, prgtagc, &services));
}

/***************************************************************************
    Properly cleans up and frees a SceneSoundEvent
***************************************************************************/
void ReleasePpsse(PSceneSoundEvent *ppsse)
{
    AssertVarMem(ppsse);
    void *sound = *ppsse;
    auto services = SceneSoundServices();
    three_mm_scene_sound_release(&sound, &services);
    *ppsse = static_cast<PSceneSoundEvent>(sound);
}

/***************************************************************************
    Static function to allocate and read a SceneSoundEvent from a GeneralGroup.  This is tricky
    because I can't do a pgg->Get() since the SceneSoundEvent is variable-sized, and
    I need to do QvGet twice since I'm allocating memory in this function.
***************************************************************************/
PSceneSoundEvent SceneSoundEvent::PsseDupFromGg(PGeneralGroup pgg, long iv, bool fDupTags)
{
    AssertPo(pgg, 0);
    AssertIn(iv, 0, pgg->IvMac());
    Assert(pgg->Cb(iv) >= size(SceneSoundEvent), "variable part too small");
    auto services = ScenePacketServices();
    return static_cast<PSceneSoundEvent>(three_mm_scene_sound_group(pgg, iv, fDupTags, &services));
}

/***************************************************************************
    Returns a PSceneSoundEvent just like this SceneSoundEvent except with ptag & chid added
***************************************************************************/
PSceneSoundEvent SceneSoundEvent::PsseAddTagChid(PTAG ptag, long chid)
{
    AssertVarMem(ptag);
    TagChildPair tag;
    tag.tag = *ptag; tag.chid = chid;
    auto services = ScenePacketServices();
    return static_cast<PSceneSoundEvent>(three_mm_scene_sound_append(this, &tag, &services));
}

/***************************************************************************
    Return a duplicate of this SceneSoundEvent
***************************************************************************/
PSceneSoundEvent SceneSoundEvent::PsseDup(void)
{
    auto services = ScenePacketServices();
    return static_cast<PSceneSoundEvent>(three_mm_scene_sound_duplicate(this, &services));
}

/***************************************************************************
    Play all sounds in this SceneSoundEvent	-> Enqueue the sounds in the SceneSoundEvent
***************************************************************************/
void SceneSoundEvent::PlayAllSounds(PMovie pmvie, ulong dtsStart)
{
    auto services = ScenePacketServices(pmvie);
    three_mm_scene_sound_play(this, dtsStart, &services);
}
