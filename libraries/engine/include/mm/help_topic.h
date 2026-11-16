/* Help topic display initialization and embedded-control construction policy. */
#ifndef MM_ENGINE_HELP_TOPIC_H
#define MM_ENGINE_HELP_TOPIC_H
#include "help_interaction.h"
#include "masked_bitmap.h"
#include "text_format.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMHelpTopicServices
    {
        void *context;
        int (*base)(void *);
        MMI32 (*characters)(void *);
        int (*scan)(void *, MMI32 cp, MMI32 *end, unsigned *group);
        int (*unique)(void *, MMI32 count, MMI32 *id), (*assign_base)(void *, MMI32);
        void (*cache)(void *);
        int (*fetch)(void *, MMI32 from, MMI32 *at, void **owned, MMI32 *bytes);
        void (*free)(void *, void *);
        int (*position)(void *, MMI32 cp, MMI32 *x, MMI32 *baseline);
        int (*character)(void *, MMI32 cp, MMTextCharacter *);
        int (*font)(void *, const MMTextCharacter *), (*measure)(void *, MMMaskedRect *);
        int (*group)(void *, MMI32 cp, unsigned *group, MMU32 *topic);
        int (*exists)(void *, MMI32 id);
        int (*edit)(void *, MMI32 id, const MMMaskedRect *, const MMTextCharacter *);
        int (*button)(void *, MMI32 id, MMU32 resource, unsigned group, MMU32 topic, MMI32 x, MMI32 baseline);
        void (*validate)(void *);
    } MMHelpTopicServices;
    /* Valid native values and progressing group/object ranges. Services return 1/0
     * or -1/-2 synchronously, never throwing across C. Primitive length/cache access,
     * free and validation never throw. Base initialization precedes the group scan;
     * base ID publishes before assignment. Scanning uses the captured initial document;
     * object/group callbacks use the current document. The button cache is captured once.
     * Ordinary scan/group false results are ignored; fetch false ends successfully.
     * Missing payloads are skipped. All private payload owners release on every exit,
     * before geometry/control callbacks. Other partial state/controls remain on failure.
     * Header words use native byte order; extras are ignored. Explicit button targets
     * override group fallback. Edit backgrounds substitute white for clear. IDs and
     * coordinates wrap at 32 bits. */
    int mm_help_topic_build(MMHelpInteraction *, int big, const MMHelpTopicServices *);
#ifdef __cplusplus
}
#endif
#endif
