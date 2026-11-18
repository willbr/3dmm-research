#ifndef MARCUS_MOVIE_BROWSER_WORKER_H
#define MARCUS_MOVIE_BROWSER_WORKER_H
#include "player.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusBrowserWorker MarcusBrowserWorker;
typedef struct MarcusBrowserListing MarcusBrowserListing;
typedef struct MarcusBrowserResult MarcusBrowserResult;

enum { MARCUS_BROWSER_OTHER, MARCUS_BROWSER_3MM, MARCUS_BROWSER_VMM, MARCUS_BROWSER_ZIP };
enum { MARCUS_BROWSER_PREVIEW, MARCUS_BROWSER_OPEN, MARCUS_BROWSER_DURATION };

typedef struct MarcusBrowserRecent
{
    char *path, *member;
} MarcusBrowserRecent;

/* Borrowed immutable values, valid until the listing or result is destroyed. */
typedef struct MarcusBrowserFile
{
    const char *name, *path, *member, *basename;
    double bytes;
    unsigned long modified_high, modified_low;
    int entry, directory;
} MarcusBrowserFile;

typedef struct MarcusBrowserMedia
{
    const char *path, *title, *detail, *error;
    const unsigned char *rgba;
    int operation, row, entry, width, height;
} MarcusBrowserMedia;

int marcus_browser_format(const char *name);
MarcusBrowserWorker *marcus_browser_worker_create(void);
void marcus_browser_worker_destroy(MarcusBrowserWorker *);
int marcus_browser_worker_list(MarcusBrowserWorker *, const char *path, const MarcusBrowserRecent *, int count);
int marcus_browser_worker_prepare(MarcusBrowserWorker *, const MarcusBrowserFile *, int row, int operation,
                                  const char *resources, const char *content);
void marcus_browser_worker_cancel(MarcusBrowserWorker *, int listing);
/* Zero means pending, one transfers a new result, and -1 reports a failure via SDL_GetError. */
int marcus_browser_worker_take_listing(MarcusBrowserWorker *, MarcusBrowserListing **);
int marcus_browser_worker_take_result(MarcusBrowserWorker *, MarcusBrowserResult **);
void marcus_browser_listing_destroy(MarcusBrowserListing *);
const char *marcus_browser_listing_path(const MarcusBrowserListing *);
const char *marcus_browser_listing_folder(const MarcusBrowserListing *);
const char *marcus_browser_listing_error(const MarcusBrowserListing *);
int marcus_browser_listing_count(const MarcusBrowserListing *);
void marcus_browser_listing_file(const MarcusBrowserListing *, int row, MarcusBrowserFile *);
void marcus_browser_result_destroy(MarcusBrowserResult *);
void marcus_browser_result_media(const MarcusBrowserResult *, MarcusBrowserMedia *);
MarcusMovieInput *marcus_browser_result_take_input(MarcusBrowserResult *);

/* Path operations return SDL-allocated strings or NULL and an SDL error. */
char *marcus_browser_path_absolute(const char *, int normalize);
char *marcus_browser_path_parent(const char *);
char *marcus_browser_path_basename(const char *);
char *marcus_browser_path_join(const char *directory, const char *path);
char *marcus_browser_current_directory(void);
char *marcus_browser_movie_folder(void);

#ifdef __cplusplus
}
#endif
#endif
