#ifndef MM_ENGINE_CAPTION_EDIT_H
#define MM_ENGINE_CAPTION_EDIT_H
#include "movie_archive_edit.h"
#include "text.h"
#include "text_properties.h"
#include "text_store.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MM_CAPTION_STYLE = 1, MM_CAPTION_FOREGROUND = 3, MM_CAPTION_ALIGNMENT = 128 };
enum
{
    MM_CAPTION_EDIT_OK, MM_CAPTION_EDIT_SIZE, MM_CAPTION_EDIT_POSITION, MM_CAPTION_EDIT_RANGE,
    MM_CAPTION_EDIT_FONT_SIZE, MM_CAPTION_EDIT_ALIGNMENT, MM_CAPTION_EDIT_MEMORY,
    MM_CAPTION_EDIT_ADJUST, MM_CAPTION_EDIT_INHERIT, MM_CAPTION_EDIT_NO_CHANGE,
    MM_CAPTION_EDIT_TEXT_LIMIT, MM_CAPTION_EDIT_PROPERTIES, MM_CAPTION_EDIT_FONT,
    MM_CAPTION_EDIT_FONT_LENGTH, MM_CAPTION_EDIT_EXISTING_LIMIT, MM_CAPTION_EDIT_READ_TEXT,
    MM_CAPTION_EDIT_ENCODING, MM_CAPTION_EDIT_UTF8, MM_CAPTION_EDIT_APPLY_FORMAT,
    MM_CAPTION_EDIT_COPY_BOX, MM_CAPTION_EDIT_ADD_BOX, MM_CAPTION_EDIT_WRITE_BODY,
    MM_CAPTION_EDIT_WRITE_BOX, MM_CAPTION_EDIT_READ_BOX, MM_CAPTION_EDIT_PREVIEW, MM_CAPTION_EDIT_PREVIEW_MEMORY
};
enum { MM_CAPTION_EDIT_SET_SIZE = 1, MM_CAPTION_EDIT_SET_PAPER = 2, MM_CAPTION_EDIT_SET_WIDTH = 4 };

typedef struct MMCaptionEditOptions
{
    MMI32 x, y, width, height, first, last, through_end, scrolling;
    MMI32 font_size, bold, italic, alignment, transparent;
    unsigned long ink, paper;
} MMCaptionEditOptions;

typedef struct MMCaptionEditFormat
{
    unsigned defaults;
    MMI32 paper, count;
    MMTextPropertyChange changes[3];
} MMCaptionEditFormat;

/* The editor range is inclusive and one-based. A text box stores authored
 * frames with an exclusive end. An unchanged range keeps its authored values. */
int mm_caption_edit_read_box(const MMTextBoxState *, MMI32 scene_first, MMI32 scene_last, MMCaptionEditOptions *);
int mm_caption_edit_write_box(MMTextBoxState *, MMI32 scene_first, MMI32 scene_last,
                               const MMCaptionEditOptions *, const MMCaptionEditOptions *before);
int mm_caption_edit_format(const MMCaptionEditOptions *, const MMCaptionEditOptions *before, MMCaptionEditFormat *);

/* CR, LF and CRLF each become one separator. Offsets use UTF-16 code units. */
size_t mm_caption_edit_normalize(unsigned short *text, size_t count, unsigned short separator);
enum { MM_CAPTION_EDIT_MAX_UNITS = 65536 };
int mm_caption_edit_decode(const char *utf8, size_t length, unsigned short *, size_t capacity, size_t *count);
/* Buffer capacity must cover two bytes per unit. Returns the stored encoding
 * or zero on failure. ANSI is used only for a one-byte-per-unit round trip. */
int mm_caption_edit_encode(const MMTextHost *, const unsigned short *, size_t count,
                           unsigned char *, size_t capacity, size_t *bytes);
MMI32 mm_caption_edit_paragraph_end(const unsigned short *text, MMI32 count, MMI32 start);

/* Adjust a private property map for the changed span between two texts.
 * The paragraph provider reads the proposed text. Diagnostic callbacks set
 * *diagnosed and the caller discards the private map if the edit fails. */
int mm_caption_edit_replace(const unsigned short *old_text, MMI32 old_count,
                            const unsigned short *new_text, MMI32 new_count,
                            const MMTextPropertyServices *, const int *diagnosed);

typedef struct MMCaptionEditInput
{
    MMCaptionEditOptions options;
    const char *font, *text;
} MMCaptionEditInput;

typedef struct MMCaptionEditPlan
{
    const MMCaptionEditInput *value, *before;
    size_t text_bytes;
    int font_changed, text_changed;
} MMCaptionEditPlan;

/* Inputs are borrowed. A zero save capacity selects draft behavior, which
 * permits an empty new caption and still prepares a view of an unchanged box. */
int mm_caption_edit_plan(const MMCaptionEditInput *, const MMCaptionEditInput *before, size_t save_capacity,
                         MMCaptionEditPlan *);

typedef struct MMCaptionEditWords
{
    const unsigned short *text;
    size_t count;
} MMCaptionEditWords;

/* Owning UTF-16 text. Initialize to zero and dispose with this API. The
 * load/edit operations replace it only after success. */
typedef struct MMCaptionText
{
    unsigned short *text;
    size_t count;
} MMCaptionText;

void mm_caption_edit_dispose_text(MMCaptionText *);
int mm_caption_edit_load_text(const MMReadStream *, const MMTextHost *, int encoding, MMCaptionText *);
int mm_caption_edit_font(const char *utf8, size_t length, const MMTextHost *, MMMovieTitle *);
/* Borrow the prepared words during serialization. NULL keeps stored TEXT. */
int mm_caption_edit_write_body(const MMRichTextState *, const MMCaptionText *, const MMTextHost *,
                               MMTextChunk *, const MMRichTextArchive *);

typedef struct MMCaptionPropertyMap
{
    MMRichTextState *body;
    MMCaptionEditWords words;
    int diagnosed;
    MMTextPropertyBinding properties;
} MMCaptionPropertyMap;

/* The body owns C text collections. Words are the currently selected
 * paragraph source and remain borrowed throughout the edit. */
void mm_caption_edit_property_services(MMCaptionPropertyMap *, MMTextPropertyServices *);

typedef struct MMCaptionEditSource
{
    const MMTextHost *encoding;
    const MMReadStream *stored_text;
    int native_system;
} MMCaptionEditSource;

/* Mutates a private body, which the caller discards on failure. Publishes the
 * replacement words only after a successful text change. */
int mm_caption_edit_apply(const MMCaptionEditPlan *, MMI32 first, MMI32 last, MMTextBoxState *,
                          MMRichTextState *, const MMCaptionEditSource *, MMCaptionText *, int *body_changed);

typedef struct MMCaptionDraftSource
{
    const MMRichTextArchive *archive; /* An empty archive binding supplies the format for a new box. */
    const MMTextHost *encoding;
    MMI32 first, last;
    const MMU32 *original;
} MMCaptionDraftSource;

struct MMPreviewCaptionCodec;
struct MMPreviewOwnedCaption;
/* Read and edit a private text box, then decode its playback record. An
 * existing box supplies both original and before. The output starts empty
 * and receives ownership only when the draft succeeds. */
int mm_caption_edit_draft(const MMCaptionDraftSource *, const MMCaptionEditInput *, const MMCaptionEditInput *before,
                          const struct MMPreviewCaptionCodec *, struct MMPreviewOwnedCaption *);

typedef struct MMCaptionArchiveBox
{
    void *context;
    MMTextBoxState *box;
    int (*clone)(void *, MMU32 original, MMU32 *number);
    int (*read_body)(void *, const MMTextChunk *body);
    int (*apply)(void *, int *body_changed); /* Returns a caption edit status. */
    int (*write_body)(void *, MMTextChunk *body);
} MMCaptionArchiveBox;

/* Writes a replacement box in a private archive. A successful link to the
 * new body precedes unlinking the cloned reference to the old body. */
int mm_caption_edit_archive_box(const MMMovieArchiveEditor *, const MMSceneArchiveText *original,
                                const MMCaptionArchiveBox *, MMU32 *number);

typedef struct MMCaptionArchiveChange
{
    MMU32 scene;
    MMI32 first, last;
    const MMSceneArchiveText *original;
    const MMCaptionEditPlan *plan; /* NULL deletes the original box. */
    const MMTextHost *encoding;
} MMCaptionArchiveChange;

/* Apply to a private working copy. Return the scene edit status and publish
 * the caption diagnostic separately when building the replacement failed. */
struct MMPrivateArchive;
int mm_caption_edit_private_archive(struct MMPrivateArchive *, const MMCaptionArchiveChange *, int *caption_status);

#ifdef __cplusplus
}
#endif
#endif
