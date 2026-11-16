/* Help balloon and button construction, replacement and geometry policy. */
#ifndef MM_ENGINE_HELP_BALLOON_H
#define MM_ENGINE_HELP_BALLOON_H
#include "help_display.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMHelpBalloon
    {
        void *topic;
    } MMHelpBalloon;
    typedef struct MMHelpButton
    {
        unsigned group;
        MMU32 topic;
    } MMHelpButton;
    typedef struct MMHelpPoint
    {
        MMI32 x, y;
    } MMHelpPoint;
    enum
    {
        MM_HELP_DUPLICATE,
        MM_HELP_ENTER_DEAD,
        MM_HELP_HIT_DEAD,
        MM_HELP_BUTTON_DEAD,
        MM_HELP_BAD_PARENT
    };
    typedef struct MMHelpBalloonFactoryServices
    {
        void *context;
        int (*source)(void *, MMHelpTopic *), (*unique)(void *, MMI32 *);
        int (*find)(void *, MMI32 id, void **borrowed), (*is_balloon)(void *, void *);
        int review_reuse;
        int (*same_source)(void *, void *, MMU32 balloon), (*replace)(void *, void *, const MMHelpTopic *);
        int (*create)(void *, MMI32 id, void **owned);
        void (*snapshot)(void *, void *);
        int (*initialize)(void *, void *, const MMHelpTopic *), (*enter)(void *, void *), (*hit)(void *, void *),
            (*alive)(void *, void *);
        void (*release)(void *, void *), (*diagnostic)(void *, int), (*validate)(void *, void *);
    } MMHelpBalloonFactoryServices;
    typedef struct MMHelpBalloonReadServices
    {
        void *context;
        int (*find)(void *, MMU32 number, void **borrowed), (*read)(void *, void *, MMU32, void **owned);
        int (*hide)(void *, void *), (*build)(void *, void *, void **result);
        void (*release)(void *, void *);
    } MMHelpBalloonReadServices;
    typedef struct MMHelpBalloonTopicServices
    {
        void *context;
        int (*create)(void *, void **owned), (*assign)(void *, void *topic, int sound_number, MMU32);
        void (*info)(void *, MMHelpTopic *), (*release)(void *, void *);
        int (*natural)(void *, void *topic), (*find)(void *, MMI32 id, void **borrowed);
        void *(*parent)(void *);
        int (*kid)(void *, void *), (*registration)(void *, void *, MMHelpPoint *),
            (*bounds)(void *, void *, MMMaskedRect *);
        int (*map_target)(void *, void *, MMHelpPoint *), (*map_parent)(void *, MMHelpPoint *);
        int (*own_registration)(void *, MMHelpPoint *), (*representation)(void *, MMI32 x, MMI32 y);
    } MMHelpBalloonTopicServices;
    typedef struct MMHelpBalloonStages
    {
        void *context;
        int (*base)(void *), (*set)(void *), (*enter)(void *);
    } MMHelpBalloonStages;
    typedef struct MMHelpBalloonLayoutServices
    {
        void *context;
        int (*base)(void *, MMI32 x, MMI32 y), (*bounds)(void *, int which, MMMaskedRect *);
        int (*position)(void *, int child, const MMMaskedRect *);
        void (*preferred)(void *, MMI32 *width, MMI32 *height);
    } MMHelpBalloonLayoutServices;
    typedef struct MMHelpButtonFactoryServices
    {
        void *context;
        int (*find)(void *, MMI32 id), (*create)(void *, MMI32 id, void **owned);
        MMHelpButton *(*state)(void *, void *);
        int (*initialize)(void *, void *), (*enter)(void *, void *), (*bounds)(void *, void *, MMMaskedRect *);
        int (*position)(void *, void *, const MMMaskedRect *);
        void (*release)(void *, void *), (*diagnostic)(void *, int), (*validate)(void *, void *);
    } MMHelpButtonFactoryServices;
    typedef struct MMHelpButtonServices
    {
        void *context;
        int (*base_point)(void *, MMI32 x, MMI32 y), (*parent_topic)(void *, void **borrowed);
        int (*map)(void *, MMHelpPoint *), (*group)(void *, void *parent, const MMHelpPoint *, unsigned *, MMU32 *);
        MMI32 (*id)(void *);
        MMU32 (*flags)(void *);
        int (*hit)(void *, void *parent, unsigned, MMU32 topic, MMU32 flags, MMI32 id);
        void (*diagnostic)(void *, int);
    } MMHelpButtonServices;
    /* Valid native values. Synchronous fallible callbacks return 1/0 or -1/-2;
     * primitive state/info/hierarchy access, release, validation and diagnostics
     * never throw. Negative results retain public partial effects. Private document
     * and pre-initialization factory owners release before native rethrow. Nodes
     * become committed after initialization: enter/hit may destroy them, so a false
     * result never releases them again. Optional REVIEW reuse retains the old node. */
    void mm_help_balloon_init(MMHelpBalloon *);
    void mm_help_button_init(MMHelpButton *);
    void mm_help_balloon_merge(MMHelpTopic *, const MMHelpTopic *);
    int mm_help_balloon_new(void **out, const MMHelpTopic *overrides, const MMHelpBalloonFactoryServices *);
    int mm_help_balloon_read(void **out, MMU32 number, const MMHelpBalloonReadServices *);
    /* Ordinary create/assignment failure releases the currently published new topic
     * then restores the old one. Exceptions keep partial state without this rollback.
     * Success releases the old topic before natural-size/anchor work, which may fail.
     * Info, parent and preferred dimensions are read at their native stages. */
    int mm_help_balloon_topic(MMHelpBalloon *, const MMHelpBalloonTopicServices *);
    int mm_help_balloon_apply(int initialize, const MMHelpBalloonStages *);
    int mm_help_balloon_layout(MMI32 x, MMI32 y, const MMHelpBalloonLayoutServices *);
    int mm_help_button_new(void **out, MMI32 id, unsigned group, MMU32 topic, MMI32 x, MMI32 bottom,
                           const MMHelpButtonFactoryServices *);
    int mm_help_button_point(const MMHelpButton *, MMI32 x, MMI32 y, const MMHelpButtonServices *);
    /* A click snapshots ID before checking the parent and reads command flags late.
     * Hit may destroy the button: no state is accessed afterward. */
    int mm_help_button_click(const MMHelpButton *, const MMHelpButtonServices *);
#ifdef __cplusplus
}
#endif
#endif
