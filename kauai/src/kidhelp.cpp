/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Code for implementing help balloons in kidspace.

***************************************************************************/
#include <exception>
#include <new>
#include "kidframe.h"
ASSERTNAME
#include "rich_text_live_store_host.h"
#include "text_document_host.h"
#include "text_properties_host.h"

namespace Help {
#include "help_text_host.h"
#include "help_display_host.h"
#include "help_interaction_host.h"
#include "help_balloon_host.h"
#include "help_topic_host.h"

BEGIN_CMD_MAP(TopicGraphicsObject, RichTextDocumentGraphicsObject)
ON_CID_GEN(cidSelIdle, pvNil, pvNil)
ON_CID_ME(cidActivateSel, pvNil, pvNil)
END_CMD_MAP_NIL()

RTCLASS(TextDocument)
RTCLASS(TopicGraphicsObject)
RTCLASS(Balloon)
RTCLASS(BalloonButton)

/***************************************************************************
    Constructor for a help text document.
***************************************************************************/
TextDocument::TextDocument(PResourceCache prca, PDocumentBase pdocb, ulong grfdoc) : TextDocument_PAR(pdocb, grfdoc)
{
    AssertPo(prca, 0);
    _prca = prca;
    _prca->AddRef();
    mm_help_topic_init(&_topic);
    mm_help_display_init(&_display);
}

/***************************************************************************
    Destructor for a help text document.
***************************************************************************/
TextDocument::~TextDocument(void)
{
    ReleasePpo(&_prca);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a TextDocument.
***************************************************************************/
void TextDocument::AssertValid(LegacyUlong grf)
{
    TextDocument_PAR::AssertValid(0);
    AssertPo(_prca, 0);
}

/***************************************************************************
    Mark memory for the TextDocument.
***************************************************************************/
void TextDocument::MarkMem(void)
{
    AssertValid(0);
    TextDocument_PAR::MarkMem();
    MarkMemObj(_prca);
}
#endif // DEBUG

/***************************************************************************
    Static method to read a help text document from the given (pcfl, ctg, cno)
    and using the given prca as the source for pictures and buttons.
***************************************************************************/
PTextDocument TextDocument::PtxhdReadChunk(PResourceCache prca, PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, PStringRegistry pstrg, ulong grftxhd)
{
    AssertPo(prca, 0);
    AssertPo(pcfl, 0);
    struct Factory : TextDocumentFactoryHost
    {
        PResourceCache cache;
        PChunkyFile file;
        ChunkTagOrType type;
        ChunkNumber number;
        PStringRegistry strings;
        ulong flags;
    } host;
    host.cache = prca;
    host.file = pcfl;
    host.type = ctg;
    host.number = cno;
    host.strings = pstrg;
    host.flags = grftxhd;
    MMTextFactoryServices services = {
        &host,
        [](void *v, void **out) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard([&] {
                *out = NewObj TextDocument(h.cache);
                return *out != nullptr;
            });
        },
        [](void *v, void *p) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard([&] {
                return static_cast<PTextDocument>(p)->_FReadChunk(h.file, h.type, h.number, h.strings, h.flags);
            });
        },
        [](void *, void *p) { static_cast<BASE *>(p)->Release(); }, [](void *) { PushErc(ercHelpReadFailed); }};
    void *owned;
    mm_text_document_create(&owned, 1, &services);
    host.Rethrow();
    auto result = static_cast<PTextDocument>(owned);
    AssertNilOrPo(result, fobjAssertFull);
    return result;
}

/***************************************************************************
    Read the given chunk into this RichTextDocument.
***************************************************************************/
bool TextDocument::_FReadChunk(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, PStringRegistry pstrg, ulong grftxhd)
{
    AssertPo(pcfl, 0);
    AssertNilOrPo(pstrg, 0);
    HelpHost host(this);
    host.strings = pstrg;
    auto services = host.Read();
    auto config = host.Config();
    int result = mm_help_read(&_topic, pcfl, ctg, cno, grftxhd, pstrg != nullptr, config.unit, config.big, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Do any necessary munging of the AllocatedGroup entry on open.  Return false if
    we don't recognize this argument type.
***************************************************************************/
bool TextDocument::_FOpenArg(long icact, byte sprm, short bo, short osk)
{
    HelpHost host(this);
    auto services = host.Argument();
    int result = mm_help_open_argument(icact, sprm, bo, osk, host.Config().big, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Save a help topic to the given chunky file.  Fill in *pcki with where
    we put the root chunk.
***************************************************************************/
bool TextDocument::FSaveToChunk(PChunkyFile pcfl, ChunkIdentification *pcki, bool fRedirectText)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertVarMem(pcki);
    HelpHost host(this);
    host.file = pcfl;
    host.output = pcki;
    host.flags = fRedirectText;
    auto services = host.Save();
    auto config = host.Config();
    MMTextChunk root = {};
    int result = mm_help_save(&_topic, &root, config.system, config.big, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Get the bounding rectangle for the given object.
***************************************************************************/
bool TextDocument::_FGetObjectRc(long icact, byte sprm, PGraphicsEnvironment pgnv, PCHP pchp, RC *prc)
{
    AssertThis(0);
    AssertIn(icact, 0, _pagcact->IvMac());
    AssertIn(sprm, sprmMinObj, 0x100);
    AssertPo(pgnv, 0);
    AssertVarMem(pchp);
    AssertVarMem(prc);
    DisplayHost host(this, pgnv, pchp);
    host.bounds = prc;
    auto services = host.Display();
    MMMaskedRect bounds;
    int result = mm_help_object_bounds(icact, sprm, host.Config().big, &bounds, &services);
    host.Rethrow();
    if (result > 0)
        *prc = DisplayHost::Rectangle(bounds);
    return result > 0;
}

/***************************************************************************
    Draw the given object.
***************************************************************************/
bool TextDocument::_FDrawObject(long icact, byte sprm, PGraphicsEnvironment pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip)
{
    AssertIn(icact, 0, _pagcact->IvMac());
    Assert(sprm >= sprmObject, 0);
    AssertPo(pgnv, 0);
    AssertVarMem(pxp);
    AssertVarMem(pchp);
    AssertVarMem(prcClip);
    DisplayHost host(this, pgnv, pchp);
    host.cursor = pxp;
    auto services = host.Display();
    int result = mm_help_object_draw(&_display, icact, sprm, yp, host.Config().big, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Insert a picture into the help text document.
***************************************************************************/
bool TextDocument::FInsertPicture(ChunkNumber cno, void *pvExtra, long cbExtra, long cp, long ccpDel, PCHP pchp, ulong grfdoc)
{
    AssertThis(0);
    AssertPvCb(pvExtra, cbExtra);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    AssertNilOrVarMem(pchp);
    HelpHost host(this);
    host.chp = pchp;
    host.flags = grfdoc;
    auto services = host.Edit();
    int result = mm_help_insert(0, cno, 0, pvExtra, cbExtra, cp, ccpDel, host.Config().big, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Insert a new button
***************************************************************************/
bool TextDocument::FInsertButton(ChunkNumber cno, ChunkNumber cnoTopic, void *pvExtra, long cbExtra, long cp, long ccpDel, PCHP pchp,
                         ulong grfdoc)
{
    AssertThis(0);
    AssertPvCb(pvExtra, cbExtra);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    AssertNilOrVarMem(pchp);
    HelpHost host(this);
    host.chp = pchp;
    host.flags = grfdoc;
    auto services = host.Edit();
    int result = mm_help_insert(1, cno, cnoTopic, pvExtra, cbExtra, cp, ccpDel, host.Config().big, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Group the given text into the given group.  lw == 0 indicates no group.
    Any non-zero number is a group.
***************************************************************************/
bool TextDocument::FGroupText(long cp1, long cp2, byte bGroup, ChunkNumber cnoTopic, PString pstnTopic)
{
    AssertThis(0);
    AssertNilOrPo(pstnTopic, 0);
    AssertIn(cp1, 0, CpMac());
    AssertIn(cp2, 0, CpMac());
    HelpHost host(this);
    host.groupName = pstnTopic;
    auto services = host.Edit();
    int result = mm_help_group(cp1, cp2, bGroup, cnoTopic, host.Config().big, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Determine if the given cp is in a grouped text range.
***************************************************************************/
bool TextDocument::FGrouped(long cp, long *pcpMin, long *pcpLim, byte *pbGroup, ChunkNumber *pcnoTopic, PString pstnTopic)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac());
    AssertNilOrVarMem(pcpMin);
    AssertNilOrVarMem(pcpLim);
    AssertNilOrVarMem(pbGroup);
    AssertNilOrVarMem(pcnoTopic);
    AssertNilOrPo(pstnTopic, 0);
    HelpHost host(this);
    host.first = pcpMin;
    host.end = pcpLim;
    host.group = pbGroup;
    host.target = pcnoTopic;
    host.groupName = pstnTopic;
    auto services = host.Edit();
    int result = mm_help_grouped(cp, host.Config().big, pstnTopic != nullptr, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Get the help topic information.
***************************************************************************/
void TextDocument::GetHtop(PTopic phtop)
{
    AssertThis(0);
    AssertVarMem(phtop);

    *phtop = HelpHost::ToTopic(_topic);
}

/***************************************************************************
    Set the topic info.
***************************************************************************/
void TextDocument::SetHtop(PTopic phtop)
{
    AssertThis(0);
    AssertVarMem(phtop);

    HelpHost host(this);
    auto services = host.Edit();
    auto topic = HelpHost::FromTopic(*phtop);
    mm_help_topic_set(&_topic, &topic, &services);
    host.Rethrow();
}

/***************************************************************************
    Constructor for a TopicGraphicsObject.
***************************************************************************/
TopicGraphicsObject::TopicGraphicsObject(PWorldOfKidspace pwoks, PTextDocument ptxhd, PGraphicsObjectBlock pgcb) : RichTextDocumentGraphicsObject(ptxhd, pgcb)
{
    AssertBaseThis(0);
    AssertPo(pwoks, 0);

    _pwoks = pwoks;
    mm_help_interaction_init(&_interaction);
}

/***************************************************************************
    Create a new help topic display gob.
***************************************************************************/
PTopicGraphicsObject TopicGraphicsObject::PtxhgNew(PWorldOfKidspace pwoks, PTextDocument ptxhd, PGraphicsObjectBlock pgcb)
{
    struct Factory : TextDocumentFactoryHost
    {
        PWorldOfKidspace world;
        PTextDocument document;
        PGraphicsObjectBlock block;
    } host;
    host.world = pwoks;
    host.document = ptxhd;
    host.block = pgcb;
    MMTextFactoryServices services = {&host,
                                      [](void *v, void **out) -> int {
                                          auto &h = *static_cast<Factory *>(v);
                                          return h.Guard([&] {
                                              *out = NewObj TopicGraphicsObject(h.world, h.document, h.block);
                                              return *out != nullptr;
                                          });
                                      },
                                      [](void *v, void *p) -> int {
                                          auto &h = *static_cast<Factory *>(v);
                                          return h.Guard(
                                              [&] { return static_cast<PTopicGraphicsObject>(p)->_FInit(); });
                                      },
                                      [](void *, void *p) { static_cast<BASE *>(p)->Release(); }, nullptr};
    void *node;
    mm_text_document_create(&node, 0, &services);
    host.Rethrow();
    return static_cast<PTopicGraphicsObject>(node);
}

/***************************************************************************
    Inititalize the display gob for a help balloon topic.
***************************************************************************/
bool TopicGraphicsObject::_FInit(void)
{
    AssertBaseThis(0);
    BuildHost host(this);
    auto services = host.Services();
    int result = mm_help_topic_build(&_interaction, RichTextStoreHost::NativeBig(), &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Return whether the point is over hot (marked text).
***************************************************************************/
bool TopicGraphicsObject::FPtIn(long xp, long yp)
{
    AssertThis(0);
    InteractionHost host(this);
    auto services = host.Commands();
    int result = mm_help_point(xp, yp, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Track the mouse.
***************************************************************************/
bool TopicGraphicsObject::FCmdTrackMouse(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    InteractionHost host(this);
    host.mouse = pcmd;
    auto services = host.Commands();
    int result = mm_help_track(&_interaction, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    An edit control got a bad key.
***************************************************************************/
bool TopicGraphicsObject::FCmdBadKey(PCMD_BADKEY pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    InteractionHost host(this);
    host.key = pcmd;
    auto services = host.Commands();
    int result = mm_help_bad_key(&_interaction, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Return the number of the group text that the given point is in.
***************************************************************************/
bool TopicGraphicsObject::FGroupFromPt(long xp, long yp, byte *pbGroup, ChunkNumber *pcnoTopic)
{
    AssertThis(0);
    AssertNilOrVarMem(pbGroup);
    AssertNilOrVarMem(pcnoTopic);
    InteractionHost host(this);
    host.groupOutput = pbGroup;
    host.topicOutput = pcnoTopic;
    auto services = host.Commands();
    int result = mm_help_group_point(xp, yp, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    A child button was hit, take action.
***************************************************************************/
void TopicGraphicsObject::DoHit(byte bGroup, ChunkNumber cnoTopic, ulong grfcust, long hidHit)
{
    AssertThis(0);
    InteractionHost host(this);
    auto services = host.Commands();
    mm_help_hit(bGroup, cnoTopic, grfcust, hidHit, &services);
    host.Rethrow();
}

/***************************************************************************
    Run the script. Returns false iff the TopicGraphicsObject doesn't exist after
    running the script.
***************************************************************************/
bool TopicGraphicsObject::_FRunScript(byte bGroup, ulong grfcust, long hidHit, achar ch, ChunkNumber cnoTopic, LegacyLong *plwRet)
{
    AssertThis(0);
    AssertNilOrVarMem(plwRet);
    InteractionHost host(this);
    host.returnOutput = plwRet;
    auto services = host.Script();
    int result = mm_help_script(bGroup, grfcust, hidHit, MMU32(ch), cnoTopic, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    This handles cidMouseMove.
***************************************************************************/
bool TopicGraphicsObject::FCmdMouseMove(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    InteractionHost host(this);
    host.mouse = pcmd;
    auto services = host.Commands();
    int result = mm_help_mouse_move(&services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Set the cursor for this TopicGraphicsObject and the given cursor state.
***************************************************************************/
void TopicGraphicsObject::SetCursor(ulong grfcust)
{
    AssertThis(0);
    InteractionHost host(this);
    auto services = host.Commands();
    mm_help_cursor(this, grfcust, &services);
    host.Rethrow();
}

/***************************************************************************
    Create a new help topic balloon based on the given topic number.
***************************************************************************/
PBalloon Balloon::PhbalCreate(PWorldOfKidspace pwoks, PGraphicsObject pgobPar, PResourceCache prca, ChunkNumber cnoTopic, PTopic phtop)
{
    AssertPo(pwoks, 0);
    AssertPo(pgobPar, 0);
    AssertPo(prca, 0);
    AssertNilOrVarMem(phtop);
    BalloonHost host;
    host.world = pwoks;
    host.parent = pgobPar;
    host.cache = prca;
    host.input = phtop;
    auto services = host.Read();
    void *node;
    mm_help_balloon_read(&node, cnoTopic, &services);
    host.Rethrow();
    return static_cast<PBalloon>(node);
}

/***************************************************************************
    Static method to create a new help balloon based on the given help
    topic document and htop.
***************************************************************************/
PBalloon Balloon::PhbalNew(PWorldOfKidspace pwoks, PGraphicsObject pgobPar, PResourceCache prca, PTextDocument ptxhd, PTopic phtop)
{
    AssertPo(pwoks, 0);
    AssertPo(pgobPar, 0);
    AssertPo(ptxhd, 0);
    AssertPo(prca, 0);
    AssertNilOrVarMem(phtop);
    BalloonHost host;
    host.world = pwoks;
    host.parent = pgobPar;
    host.cache = prca;
    host.document = ptxhd;
    MMHelpTopic overrideData;
    if (phtop)
        overrideData = BalloonTopicData(*phtop);
    auto services = host.Factory();
    void *node;
    mm_help_balloon_new(&node, phtop ? &overrideData : nullptr, &services);
    host.Rethrow();
    return static_cast<PBalloon>(node);
}

/***************************************************************************
    Constructor for a help balloon.
***************************************************************************/
Balloon::Balloon(GraphicsObjectBlock *pgcb) : Balloon_PAR(pgcb)
{
    mm_help_balloon_init(&_balloon);
}

/***************************************************************************
    Initialize the help balloon.
***************************************************************************/
bool Balloon::_FInit(PWorldOfKidspace pwoks, PTextDocument ptxhd, Topic *phtop, PResourceCache prca)
{
    AssertBaseThis(0);
    AssertPo(ptxhd, 0);
    AssertVarMem(phtop);
    AssertPo(prca, 0);
    BalloonHost host(this);
    host.world = pwoks;
    host.document = ptxhd;
    host.input = phtop;
    host.cache = prca;
    auto services = host.Stages();
    int result = mm_help_balloon_apply(1, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Set the topic for this balloon.  Returns false if setting the topic
    fails or if the balloon is instantly killed by a script.
***************************************************************************/
bool Balloon::FSetTopic(PTextDocument ptxhd, PTopic phtop, PResourceCache prca)
{
    AssertThis(0);
    AssertPo(ptxhd, 0);
    AssertVarMem(phtop);
    AssertPo(prca, 0);
    BalloonHost host(this);
    host.document = ptxhd;
    host.input = phtop;
    host.cache = prca;
    auto services = host.Stages();
    int result = mm_help_balloon_apply(0, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Set the topic in the help balloon.  Don't enter the initial state.
***************************************************************************/
bool Balloon::_FSetTopic(PTextDocument ptxhd, PTopic phtop, PResourceCache prca)
{
    AssertBaseThis(0);
    AssertPo(ptxhd, 0);
    AssertVarMem(phtop);
    AssertPo(prca, 0);
    BalloonHost host(this);
    host.document = ptxhd;
    host.input = phtop;
    host.cache = prca;
    auto services = host.TopicServices();
    int result = mm_help_balloon_topic(&_balloon, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Our representation is changing, so make sure we stay inside our parent
    and reposition the TopicGraphicsObject.
***************************************************************************/
void Balloon::_SetGorp(PGraphicalObjectRepresentation pgorp, long dxp, long dyp)
{
    BalloonHost host(this);
    host.representation = pgorp;
    auto services = host.Layout();
    mm_help_balloon_layout(dxp, dyp, &services);
    host.Rethrow();
}

/***************************************************************************
    Constructor for a help balloon button.
***************************************************************************/
BalloonButton::BalloonButton(GraphicsObjectBlock *pgcb) : BalloonButton_PAR(pgcb)
{
    mm_help_button_init(&_button);
}

/***************************************************************************
    Create a new help balloon button
***************************************************************************/
PBalloonButton BalloonButton::PhbtnNew(PWorldOfKidspace pwoks, PGraphicsObject pgobPar, long hid, ChunkNumber cno, PResourceCache prca, byte bGroup, ChunkNumber cnoTopic, long xpLeft,
                     long ypBottom)
{
    AssertPo(pwoks, 0);
    AssertNilOrPo(pgobPar, 0);
    Assert(hid != hidNil, "nil ID");
    AssertPo(prca, 0);
    ButtonHost host;
    host.world = pwoks;
    host.parent = pgobPar;
    host.cache = prca;
    host.number = cno;
    auto services = host.Factory();
    void *node;
    mm_help_button_new(&node, hid, bGroup, cnoTopic, xpLeft, ypBottom, &services);
    host.Rethrow();
    return static_cast<PBalloonButton>(node);
}

/***************************************************************************
    Test whether the given point is in this button or its related text.
***************************************************************************/
bool BalloonButton::FPtIn(long xp, long yp)
{
    AssertThis(0);
    ButtonHost host(this);
    auto services = host.Interaction();
    int result = mm_help_button_point(&_button, xp, yp, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    The button has been clicked on.  Tell the TopicGraphicsObject to do its thing.
***************************************************************************/
bool BalloonButton::FCmdClicked(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    ButtonHost host(this);
    host.command = pcmd;
    auto services = host.Interaction();
    int result = mm_help_button_click(&_button, &services);
    host.Rethrow();
    return result > 0;
}

} // end of namespace Help
