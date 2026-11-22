#ifndef MARCUS_ACTOR_NAMES_H
#define MARCUS_ACTOR_NAMES_H

#include "mm/collection_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusActorNames
{
    MMCollectionStorage storage;
    MMU32 count, bytes;
} MarcusActorNames;

int marcus_actor_names_init(MarcusActorNames *, const MMStorageAllocator *);
void marcus_actor_names_dispose(MarcusActorNames *);
void marcus_actor_names_clear(MarcusActorNames *);
void marcus_actor_names_swap(MarcusActorNames *, MarcusActorNames *);
/* Copy into an initialized owner. Failure preserves the destination. */
int marcus_actor_names_copy(MarcusActorNames *destination, const MarcusActorNames *source);

/* Replace or insert a counted UTF-8 name. The input must have a trailing NUL
 * and must not alias the table. An empty stored name is distinct from absence. */
int marcus_actor_names_set(MarcusActorNames *, MMI32 actor, const char *name, size_t bytes);
/* The returned pointer is borrowed until the next mutation. */
const char *marcus_actor_names_get(const MarcusActorNames *, MMI32 actor, size_t *bytes);
void marcus_actor_display_name(const MarcusActorNames *, MMI32 actor, int text, int prop,
                              char *output, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
