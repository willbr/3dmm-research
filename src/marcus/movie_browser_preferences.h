#ifndef MARCUS_MOVIE_BROWSER_PREFERENCES_H
#define MARCUS_MOVIE_BROWSER_PREFERENCES_H
#include "movie_browser_worker.h"

enum { MARCUS_BROWSER_FAVOURITE_LIMIT = 12, MARCUS_BROWSER_RECENT_LIMIT = 24 };
typedef struct MarcusBrowserPreferences
{
    char *location, *last_path, *last_member;
    char *favourites[MARCUS_BROWSER_FAVOURITE_LIMIT];
    MarcusBrowserRecent recent[MARCUS_BROWSER_RECENT_LIMIT];
    int favourite_count, recent_count, grid, sort_date, filter;
} MarcusBrowserPreferences;

void marcus_browser_preferences_load(MarcusBrowserPreferences *, const char *path);
void marcus_browser_preferences_save(const MarcusBrowserPreferences *, const char *path);
void marcus_browser_preferences_clear(MarcusBrowserPreferences *);
#endif
