#include "movie_browser_preferences.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_byte(SDL_IOStream *input)
{
    unsigned char value;
    return SDL_ReadIO(input, &value, 1) == 1 ? value : EOF;
}

/* Retain the existing quoted-token format, including escaped backslashes,
 * escaped quotes and whitespace inside a quoted path or member name. */
static char *read_token(SDL_IOStream *input, int allow_quoted)
{
    char *text, *grown;
    size_t used, capacity;
    int value, quoted;
    do { value = read_byte(input); } while (value != EOF && SDL_isspace(value));
    if (value == EOF) return NULL;
    quoted = allow_quoted && value == '"';
    if (quoted) value = read_byte(input);
    capacity = 64;
    text = SDL_malloc(capacity);
    if (!text) return NULL;
    used = 0;
    while (value != EOF && (quoted ? value != '"' : !SDL_isspace(value)))
    {
        if (quoted && value == '\\') value = read_byte(input);
        if (value == EOF) break;
        if (used == capacity - 1)
        {
            if (capacity > (size_t)-1 / 2) goto failed;
            capacity *= 2;
            grown = SDL_realloc(text, capacity);
            if (!grown) goto failed;
            text = grown;
        }
        text[used++] = (char)value;
        value = read_byte(input);
    }
    if (quoted && value != '"') goto failed;
    text[used] = '\0';
    return text;
failed:
    SDL_free(text);
    return NULL;
}

static int read_int(SDL_IOStream *input, int *value)
{
    char *token, *end;
    long number;
    int valid;
    token = read_token(input, 0);
    if (!token) return 0;
    errno = 0;
    number = strtol(token, &end, 10);
    valid = !errno && end != token && !*end && number >= INT_MIN && number <= INT_MAX;
    if (valid) *value = (int)number;
    SDL_free(token);
    return valid;
}

void marcus_browser_preferences_load(MarcusBrowserPreferences *prefs, const char *path)
{
    SDL_IOStream *input;
    char *key, *value, *member;
    int valid;
    if (!path || !(input = SDL_IOFromFile(path, "rb"))) return;
    while ((key = read_token(input, 0)) != NULL)
    {
        value = NULL;
        member = NULL;
        valid = 1;
        if (!strcmp(key, "view"))
            valid = read_int(input, &prefs->grid) && read_int(input, &prefs->sort_date) && read_int(input, &prefs->filter);
        else if (!strcmp(key, "selected") || !strcmp(key, "recent"))
        {
            value = read_token(input, 1);
            member = value ? read_token(input, 1) : NULL;
            valid = value && member;
            if (valid && !strcmp(key, "selected"))
            {
                SDL_free(prefs->last_path);
                SDL_free(prefs->last_member);
                prefs->last_path = value;
                prefs->last_member = member;
                value = member = NULL;
            }
            else if (valid && prefs->recent_count < MARCUS_BROWSER_RECENT_LIMIT)
            {
                prefs->recent[prefs->recent_count].path = value;
                prefs->recent[prefs->recent_count++].member = member;
                value = member = NULL;
            }
        }
        else if (!strcmp(key, "folder") || !strcmp(key, "movie-folder") || !strcmp(key, "favourite"))
        {
            value = read_token(input, 1);
            valid = value != NULL;
            if (valid && !strcmp(key, "folder"))
            {
                SDL_free(prefs->location);
                prefs->location = value;
                value = NULL;
            }
            else if (valid && !strcmp(key, "favourite") && prefs->favourite_count < MARCUS_BROWSER_FAVOURITE_LIMIT)
            {
                prefs->favourites[prefs->favourite_count++] = value;
                value = NULL;
            }
        }
        else valid = 0;
        SDL_free(key);
        SDL_free(value);
        SDL_free(member);
        if (!valid) break;
    }
    SDL_CloseIO(input);
    prefs->grid = prefs->grid != 0;
    prefs->sort_date = prefs->sort_date != 0;
    if (prefs->filter < MARCUS_BROWSER_OTHER) prefs->filter = MARCUS_BROWSER_OTHER;
    if (prefs->filter > MARCUS_BROWSER_ZIP) prefs->filter = MARCUS_BROWSER_ZIP;
}

static int write_text(SDL_IOStream *output, const char *text)
{
    size_t length;
    length = strlen(text);
    return SDL_WriteIO(output, text, length) == length;
}

static int write_quoted(SDL_IOStream *output, const char *text)
{
    if (!write_text(output, "\"")) return 0;
    if (text)
        for (; *text; ++text)
        {
            if ((*text == '"' || *text == '\\') && !write_text(output, "\\")) return 0;
            if (SDL_WriteIO(output, text, 1) != 1) return 0;
        }
    return write_text(output, "\"");
}

void marcus_browser_preferences_save(const MarcusBrowserPreferences *prefs, const char *path)
{
    SDL_IOStream *output;
    char *temporary;
    char view[64];
    int valid, i;
    if (!path || SDL_asprintf(&temporary, "%s.tmp", path) < 0) return;
    output = SDL_IOFromFile(temporary, "wb");
    if (output)
    {
        SDL_snprintf(view, sizeof(view), "\nview %d %d %d\nselected ", prefs->grid, prefs->sort_date, prefs->filter);
        valid = write_text(output, "folder ") && write_quoted(output, prefs->location) && write_text(output, view) &&
                write_quoted(output, prefs->last_path) && write_text(output, " ") &&
                write_quoted(output, prefs->last_member) && write_text(output, "\n");
        for (i = 0; valid && i < prefs->favourite_count; ++i)
            valid = write_text(output, "favourite ") && write_quoted(output, prefs->favourites[i]) && write_text(output, "\n");
        for (i = 0; valid && i < prefs->recent_count; ++i)
            valid = write_text(output, "recent ") && write_quoted(output, prefs->recent[i].path) && write_text(output, " ") &&
                    write_quoted(output, prefs->recent[i].member) && write_text(output, "\n");
        if (SDL_CloseIO(output) && valid) SDL_RenamePath(temporary, path);
    }
    SDL_free(temporary);
}

void marcus_browser_preferences_clear(MarcusBrowserPreferences *prefs)
{
    int i;
    SDL_free(prefs->location);
    SDL_free(prefs->last_path);
    SDL_free(prefs->last_member);
    for (i = 0; i < prefs->favourite_count; ++i) SDL_free(prefs->favourites[i]);
    for (i = 0; i < prefs->recent_count; ++i)
    {
        SDL_free(prefs->recent[i].path);
        SDL_free(prefs->recent[i].member);
    }
    memset(prefs, 0, sizeof(*prefs));
}
