#ifndef REVIEW_STORE_H
#define REVIEW_STORE_H
#include <sqlite3.h>
#include <stddef.h>
typedef struct ReviewStore
{
    sqlite3 *results;
    sqlite3 *notes;
    char error[1024];
} ReviewStore;
int review_open(ReviewStore *s, const char *path);
void review_close(ReviewStore *s);
int review_note_load(ReviewStore *s, int run, int sequence, char *body, size_t capacity, int *resolved);
int review_note_save(ReviewStore *s, int run, int sequence, const char *body, int resolved);
/* Pair presentations by scene/frame and time since that authored frame commit.
   Sequence numbers are local to each renderer and must not be equated. */
int review_presentation_pair(ReviewStore *s, int reference, int candidate, int sequence, double *delta_ms);
#endif
