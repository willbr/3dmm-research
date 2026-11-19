/* Preview caption and audio loader control over typed storage/file services. */
#ifndef MM_ENGINE_PREVIEW_MEDIA_H
#define MM_ENGINE_PREVIEW_MEDIA_H
#include "preview_metadata.h"
#include "movie_session.h"
#include "midi.h"
#include "sound_resource.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMPreviewCaptionData
    {
        MMI32 first, end, left, top, right, bottom, font_size, bytes, encoding;
        MMU32 background, properties, arguments;
        int story, has_arguments, property_swap;
    } MMPreviewCaptionData;
    typedef struct MMPreviewTextProperty
    {
        MMU32 position, kind, value;
    } MMPreviewTextProperty;
    typedef struct MMPreviewCaptionServices
    {
        void *context, *input;
        const MMStorageAllocator *allocator;
        int (*create)(void *, void **);
        int (*box)(void *, void *, MMU32, MMPreviewCaptionData *);
        int (*rich_text)(void *, void *, MMPreviewCaptionData *);
        void (*release)(void *, void *);
        void (*publish)(void *, const MMPreviewCaptionData *);
        int (*font)(void *, void *);
        int (*read_text)(void *, void *, void *, MMU32);
        int (*utf16)(void *, const void *, MMU32, int, unsigned short *, MMU32, MMU32 *count);
        int (*text_buffer)(void *, MMU32 count, unsigned short **);
        void (*property)(void *, void *, MMU32 index, MMU32 words[2]);
        void (*argument_info)(void *, void *, MMU32 index, MMI32 *bytes, int *free_entry);
        void (*argument)(void *, void *, MMU32 index, void *);
        int (*append)(void *, const MMPreviewTextProperty *, const void *font_data, MMU32 font_bytes);
        MMU32 (*position)(void *, MMU32 index);
        void (*swap)(void *, MMU32, MMU32);
    } MMPreviewCaptionServices;
    typedef struct MMPreviewCaptionArchive
    {
        const MMMovieSessionGraph *graph;
        MMMovieGraphBytes text; /* TEXT uses the stored extent, even when flagged packed. */
        int (*load_font)(void *, const MMReadStream *, MMU32 offset);
    } MMPreviewCaptionArchive;
    typedef struct MMPreviewAudioServices
    {
        void *context;
        const MMStorageAllocator *allocator;
        void (*reset)(void *), (*silent)(void *);
        void (*publish)(void *, MMU32 type, MMU32 number, int midi);
        int (*wave_buffer)(void *, MMU32, unsigned char **);
        int (*event)(void *, MMU32 timestamp, MMU32 message);
    } MMPreviewAudioServices;
    typedef struct MMPreviewSoundServices
    {
        void *context;
        void (*source)(void *, MMI32);
        int (*resolve)(void *, MMI32 source, MMU32 type, MMU32 number, MMMovieSessionGraph *);
    } MMPreviewSoundServices;
    enum
    {
        MM_PREVIEW_SOUND_NOT_READY = -3,
        MM_PREVIEW_SOUND_MISSING = -4
    };
    /* Calls are synchronous and non-reentrant. Fallible effects return 1/0, -1 for
     * allocation exceptions, -2 for other exceptions; typed getters never throw.
     * C releases caption input and temporary buffers on every exit. Output is a fresh
     * native descriptor, published progressively; failures retain completed prefixes.
     * Text is capped at 1 MiB; UTF-16 positions and original argument/ternary policies
     * are retained. Property sorting is stable and does not fail when scratch fails.
     * Audio admits one MSND envelope, up to 64 MiB decoded payload, and the shared C
     * MIDI parser. Parser exhaustion (including malformed tails) remains success.
     * Event/time budgets precede channel filtering. MIDI scratch allocation failure
     * returns decode failure like the original SDK payload factory; caption scratch
     * failure returns -1 like its former output vectors. Public sound remapping gives
     * scene children precedence over the first movie child and publishes source only
     * after decoding succeeds. Silent cues bypass readiness and source publication. */
    int mm_preview_caption(MMU32 number, const MMPreviewCaptionServices *);
    /* The archive supplies input callbacks. load_font falls back to an empty
     * name on invalid wire text and reports only hard failures. */
    int mm_preview_caption_graph(MMU32 number, const MMPreviewCaptionArchive *, const MMPreviewCaptionServices *);
    int mm_preview_audio(const MMMovieSessionGraph *, MMU32 type, MMU32 number, const MMPreviewAudioServices *);
    int mm_preview_sound(const MMPreviewCue *, int ready, MMU32 current_scene, const MMChunkAccess *movie,
                         MMU32 *resolved_number, const MMPreviewSoundServices *, const MMPreviewAudioServices *);
#ifdef __cplusplus
}
#endif
#endif
