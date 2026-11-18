#include "dialog_host.h"
#include "movie_browser.h"
#include "movie_browser_preferences.h"
#include "movie_browser_worker.h"
#include <stdlib.h>
#include <string.h>

enum { CARD_LIMIT = 128, SELECTION_DELAY_MS = 200, PREVIEW_DELAY_MS = 100 };

typedef struct BrowserRow
{
    MarcusBrowserFile file;
    char *search_key;
    int index, card, path_format, member_format;
} BrowserRow;

typedef struct BrowserCard
{
    int row;
    unsigned long used;
    SDL_Texture *texture;
    char description[2048], duration[2048];
} BrowserCard;

struct MarcusBrowser
{
    MarcusBrowserPreferences prefs;
    MarcusBrowserWorker *worker;
    MarcusBrowserListing *listing;
    BrowserRow *rows, **filtered;
    int row_count, count, selected;
    BrowserCard cards[CARD_LIMIT];
    int card_count, first_visible, end_visible;
    unsigned long selected_at, preview_at;
    int open, recent, loading, preparing, operation;
    char *address, *filename, *movie_folder, *state_path, *resources, *content;
    char search[512], error[1024];
    char **history;
    size_t history_count, history_capacity;
    MarcusMovieInput *chosen;
    char *chosen_path;
    int chosen_entry;
};

static const char *text_or_empty(const char *text) { return text ? text : ""; }

static int set_text(char **destination, const char *text)
{
    char *copy;
    text = text_or_empty(text);
    if (!strcmp(text_or_empty(*destination), text)) return 1;
    copy = *text ? SDL_strdup(text) : NULL;
    if (*text && !copy) return 0;
    SDL_free(*destination);
    *destination = copy;
    return 1;
}

static void copy_error(MarcusBrowser *browser)
{
    SDL_strlcpy(browser->error, SDL_GetError(), sizeof(browser->error));
}

static int ascii_fold(int c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; }

static void lower_ascii(char *text)
{
    for (; *text; ++text) *text = (char)ascii_fold((unsigned char)*text);
}

static int compare_ascii(const char *a, const char *b)
{
    int difference;
    do
    {
        difference = ascii_fold((unsigned char)*a) - ascii_fold((unsigned char)*b);
        if (difference || !*a) return difference;
        ++a;
        ++b;
    } while (1);
}

int marcus_browser_format(const char *path)
{
    const char *base, *separator, *extension;
    separator = strrchr(path, '/');
    base = separator ? separator + 1 : path;
#ifdef _WIN32
    separator = strrchr(base, '\\');
    if (separator) base = separator + 1;
    if (base == path && base[0] && base[1] == ':') base += 2;
#endif
    extension = strrchr(base, '.');
    if (!extension || extension == base) return MARCUS_BROWSER_OTHER;
    if (!compare_ascii(extension, ".3mm")) return MARCUS_BROWSER_3MM;
    if (!compare_ascii(extension, ".vmm")) return MARCUS_BROWSER_VMM;
    if (!compare_ascii(extension, ".zip")) return MARCUS_BROWSER_ZIP;
    return MARCUS_BROWSER_OTHER;
}

static int is_directory(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static BrowserRow *selected_row(const MarcusBrowser *browser)
{
    return browser->selected >= 0 && browser->selected < browser->count ? browser->filtered[browser->selected] : NULL;
}

static const char *filename_for(const BrowserRow *row)
{
    return row->file.entry >= 0 ? row->file.member : row->file.path;
}

static void cancel_preparation(MarcusBrowser *browser)
{
    marcus_browser_worker_cancel(browser->worker, 0);
    browser->preparing = 0;
}

static void select_row(MarcusBrowser *browser, int index, const BrowserRow *previous)
{
    BrowserRow *row;
    browser->selected = !browser->count ? -1 : index < 0 ? 0 : index >= browser->count ? browser->count - 1 : index;
    row = selected_row(browser);
    if (row != previous && browser->preparing) cancel_preparation(browser);
    browser->selected_at = (unsigned long)SDL_GetTicks();
    if (!set_text(&browser->filename, row ? filename_for(row) : NULL)) copy_error(browser);
}

static int compare_names(const void *left, const void *right)
{
    const BrowserRow *a, *b;
    int order;
    a = *(BrowserRow *const *)left;
    b = *(BrowserRow *const *)right;
    if (a->file.directory != b->file.directory) return b->file.directory - a->file.directory;
    order = compare_ascii(a->file.name, b->file.name);
    return order ? order : a->index - b->index;
}

static int compare_dates(const void *left, const void *right)
{
    const BrowserRow *a, *b;
    a = *(BrowserRow *const *)left;
    b = *(BrowserRow *const *)right;
    if (a->file.directory == b->file.directory)
    {
        if (a->file.modified_high != b->file.modified_high) return a->file.modified_high > b->file.modified_high ? -1 : 1;
        if (a->file.modified_low != b->file.modified_low) return a->file.modified_low > b->file.modified_low ? -1 : 1;
    }
    return compare_names(left, right);
}

static void filter_rows(MarcusBrowser *browser)
{
    const BrowserRow *previous;
    BrowserRow *row;
    char query[sizeof(browser->search)];
    int i, format;
    previous = selected_row(browser);
    memcpy(query, browser->search, sizeof(query));
    lower_ascii(query);
    browser->count = 0;
    for (i = 0; i < browser->row_count; ++i)
    {
        row = &browser->rows[i];
        format = browser->prefs.filter == MARCUS_BROWSER_ZIP || !row->file.member[0] ? row->path_format : row->member_format;
        if (!row->file.directory && browser->prefs.filter && format != browser->prefs.filter) continue;
        if (strstr(row->search_key, query)) browser->filtered[browser->count++] = row;
    }
    if (!browser->recent && browser->count > 1)
        qsort(browser->filtered, (size_t)browser->count, sizeof(*browser->filtered),
              browser->prefs.sort_date ? compare_dates : compare_names);
    select_row(browser, 0, previous);
}

static void clear_rows(MarcusBrowser *browser)
{
    int i;
    for (i = 0; i < browser->card_count; ++i)
    {
        dialog_retire_texture(browser->cards[i].texture);
        browser->cards[i].texture = NULL;
    }
    for (i = 0; i < browser->row_count; ++i) SDL_free(browser->rows[i].search_key);
    SDL_free(browser->rows);
    SDL_free(browser->filtered);
    marcus_browser_listing_destroy(browser->listing);
    browser->rows = NULL;
    browser->filtered = NULL;
    browser->listing = NULL;
    browser->row_count = browser->count = browser->card_count = 0;
    browser->selected = -1;
}

static void collect_listing(MarcusBrowser *browser, MarcusBrowserListing *listing)
{
    BrowserRow *row;
    int i, count;
    browser->listing = listing;
    count = marcus_browser_listing_count(listing);
    if (count)
    {
        browser->rows = SDL_calloc((size_t)count, sizeof(*browser->rows));
        browser->filtered = SDL_calloc((size_t)count, sizeof(*browser->filtered));
        if (!browser->rows || !browser->filtered) goto failed;
    }
    browser->row_count = count;
    for (i = 0; i < count; ++i)
    {
        row = &browser->rows[i];
        marcus_browser_listing_file(listing, i, &row->file);
        if (SDL_asprintf(&row->search_key, "%s %s", row->file.name, row->file.basename) < 0) goto failed;
        lower_ascii(row->search_key);
        row->index = i;
        row->card = -1;
        row->path_format = marcus_browser_format(row->file.basename);
        row->member_format = marcus_browser_format(row->file.member);
    }
    SDL_strlcpy(browser->error, marcus_browser_listing_error(listing), sizeof(browser->error));
    if (!browser->recent && !set_text(&browser->prefs.location, marcus_browser_listing_folder(listing))) copy_error(browser);
    filter_rows(browser);
    for (i = 0; i < browser->count; ++i)
    {
        row = browser->filtered[i];
        if (!strcmp(row->file.path, text_or_empty(browser->prefs.last_path)) &&
            !strcmp(row->file.member, text_or_empty(browser->prefs.last_member)))
        {
            select_row(browser, i, selected_row(browser));
            break;
        }
    }
    return;
failed:
    clear_rows(browser);
    copy_error(browser);
}

static int push_history(MarcusBrowser *browser, const char *path)
{
    char **grown;
    char *copy;
    size_t capacity;
    if (browser->history_count == browser->history_capacity)
    {
        if (browser->history_capacity > (size_t)-1 / 2 / sizeof(*grown)) return 0;
        capacity = browser->history_capacity ? browser->history_capacity * 2 : 8;
        grown = SDL_realloc(browser->history, capacity * sizeof(*grown));
        if (!grown) return 0;
        browser->history = grown;
        browser->history_capacity = capacity;
    }
    copy = SDL_strdup(path);
    if (!copy) return 0;
    browser->history[browser->history_count++] = copy;
    return 1;
}

static void clear_history(MarcusBrowser *browser)
{
    while (browser->history_count) SDL_free(browser->history[--browser->history_count]);
}

static int navigate(MarcusBrowser *browser, const char *path, int remember)
{
    char *address;
    address = SDL_strdup(path);
    if (!address) { copy_error(browser); return 0; }
    if (remember && !push_history(browser, browser->listing ? marcus_browser_listing_path(browser->listing) : ""))
    {
        SDL_free(address);
        copy_error(browser);
        return 0;
    }
    if (!marcus_browser_worker_list(browser->worker, address, browser->prefs.recent, browser->prefs.recent_count))
    {
        if (remember) SDL_free(browser->history[--browser->history_count]);
        SDL_free(address);
        copy_error(browser);
        return 0;
    }
    browser->recent = !*address;
    SDL_free(browser->address);
    browser->address = address;
    set_text(&browser->filename, NULL);
    browser->search[0] = browser->error[0] = '\0';
    browser->loading = 1;
    browser->preparing = 0;
    if (marcus_browser_format(address) == MARCUS_BROWSER_ZIP && browser->prefs.filter == MARCUS_BROWSER_ZIP)
        browser->prefs.filter = MARCUS_BROWSER_OTHER;
    clear_rows(browser);
    return 1;
}

static void close_browser(MarcusBrowser *browser)
{
    const BrowserRow *row;
    row = selected_row(browser);
    if (row)
    {
        set_text(&browser->prefs.last_path, row->file.path);
        set_text(&browser->prefs.last_member, row->file.member);
    }
    marcus_browser_worker_cancel(browser->worker, 1);
    browser->preparing = browser->loading = browser->open = 0;
    marcus_browser_preferences_save(&browser->prefs, browser->state_path);
}

static void prepare(MarcusBrowser *browser, const MarcusBrowserFile *file, int row, int operation)
{
    if (!marcus_browser_worker_prepare(browser->worker, file, row, operation,
                                       text_or_empty(browser->resources), text_or_empty(browser->content)))
    {
        copy_error(browser);
        return;
    }
    browser->preparing = 1;
    browser->operation = operation;
    browser->error[0] = '\0';
}

static int is_container(const MarcusBrowserFile *file)
{
    return file->directory || (!file->member[0] && marcus_browser_format(file->path) == MARCUS_BROWSER_ZIP);
}

static void choose(MarcusBrowser *browser, const MarcusBrowserFile *file, int row)
{
    if (browser->preparing && browser->operation == MARCUS_BROWSER_OPEN) return;
    if (is_container(file)) navigate(browser, file->path, 1);
    else prepare(browser, file, row, MARCUS_BROWSER_OPEN);
}

static void choose_path(MarcusBrowser *browser, const char *path)
{
    MarcusBrowserFile file;
    memset(&file, 0, sizeof(file));
    file.path = path;
    file.member = "";
    file.entry = -1;
    file.directory = is_directory(path);
    choose(browser, &file, -1);
}

static void open_location(MarcusBrowser *browser, const char *text, int normalize)
{
    char *path;
    if (!text || !*text) return;
    path = marcus_browser_path_absolute(text, normalize);
    if (!path) { copy_error(browser); return; }
    if (is_directory(path) || marcus_browser_format(path) == MARCUS_BROWSER_ZIP) navigate(browser, path, 1);
    else choose_path(browser, path);
    SDL_free(path);
}

static void accept(MarcusBrowser *browser)
{
    const BrowserRow *row;
    char *path;
    row = selected_row(browser);
    if (!browser->filename || (row && !strcmp(browser->filename, filename_for(row))))
    {
        if (row) choose(browser, &row->file, row->index);
        return;
    }
    path = marcus_browser_path_join(text_or_empty(browser->prefs.location), browser->filename);
    if (!path) { copy_error(browser); return; }
    choose_path(browser, path);
    SDL_free(path);
}

static void go_up(MarcusBrowser *browser)
{
    char *parent;
    if (browser->recent) { navigate(browser, text_or_empty(browser->prefs.location), 1); return; }
    parent = marcus_browser_path_parent(browser->listing ? marcus_browser_listing_path(browser->listing) :
                                                         text_or_empty(browser->address));
    if (!parent) { copy_error(browser); return; }
    if (*parent) navigate(browser, parent, 1);
    SDL_free(parent);
}

static BrowserCard *card_for(MarcusBrowser *browser, const BrowserRow *row)
{
    BrowserCard *card;
    if (row->card < 0) return NULL;
    card = &browser->cards[row->card];
    card->used = (unsigned long)SDL_GetTicks();
    return card;
}

static void store_card(MarcusBrowser *browser, SDL_Renderer *renderer, const MarcusBrowserMedia *media)
{
    BrowserRow *row;
    BrowserCard *card;
    unsigned long now;
    int slot, i;
    row = &browser->rows[media->row];
    now = (unsigned long)SDL_GetTicks();
    slot = row->card;
    if (slot < 0)
    {
        if (browser->card_count < CARD_LIMIT) slot = browser->card_count++;
        else
        {
            slot = 0;
            for (i = 1; i < browser->card_count; ++i)
                if (now - browser->cards[i].used > now - browser->cards[slot].used) slot = i;
            browser->rows[browser->cards[slot].row].card = -1;
        }
    }
    card = &browser->cards[slot];
    dialog_retire_texture(card->texture);
    memset(card, 0, sizeof(*card));
    row->card = slot;
    card->row = media->row;
    card->used = now;
    card->texture = dialog_preview_texture(renderer, media->rgba, media->width, media->height, media->width * 4);
    if (card->texture) SDL_SetTextureScaleMode(card->texture, SDL_SCALEMODE_NEAREST);
    SDL_snprintf(card->description, sizeof(card->description), "\n%s\n%s%s%s", media->title, media->detail,
                 media->error[0] ? "\n" : "", media->error);
}

static void collect_result(MarcusBrowser *browser, SDL_Renderer *renderer, MarcusBrowserResult *result)
{
    MarcusBrowserMedia media;
    BrowserCard *card;
    char *path;
    marcus_browser_result_media(result, &media);
    if (media.operation == MARCUS_BROWSER_OPEN)
    {
        if (media.error[0]) SDL_strlcpy(browser->error, media.error, sizeof(browser->error));
        else if ((path = SDL_strdup(media.path)) == NULL) copy_error(browser);
        else
        {
            marcus_input_destroy(browser->chosen);
            SDL_free(browser->chosen_path);
            browser->chosen = marcus_browser_result_take_input(result);
            browser->chosen_path = path;
            browser->chosen_entry = media.entry;
            close_browser(browser);
        }
    }
    else if (media.row >= 0 && media.row < browser->row_count)
    {
        if (media.operation == MARCUS_BROWSER_PREVIEW) store_card(browser, renderer, &media);
        else if ((card = card_for(browser, &browser->rows[media.row])) != NULL)
        {
            if (media.error[0]) SDL_snprintf(card->duration, sizeof(card->duration), "Duration unavailable\n%s", media.error);
            else SDL_strlcpy(card->duration, media.detail, sizeof(card->duration));
        }
    }
    marcus_browser_result_destroy(result);
}

static int eligible(MarcusBrowser *browser, const BrowserRow *row)
{
    return row && !is_container(&row->file) && !card_for(browser, row);
}

static void tick(MarcusBrowser *browser, SDL_Renderer *renderer)
{
    MarcusBrowserResult *result;
    MarcusBrowserListing *listing;
    const BrowserRow *row;
    unsigned long now;
    int status, i, end;
    status = marcus_browser_worker_take_result(browser->worker, &result);
    if (status)
    {
        browser->preparing = 0;
        if (status > 0) collect_result(browser, renderer, result);
        else copy_error(browser);
    }
    if (!browser->open) return;
    status = marcus_browser_worker_take_listing(browser->worker, &listing);
    if (status)
    {
        browser->loading = 0;
        if (status > 0) collect_listing(browser, listing);
        else copy_error(browser);
    }
    now = (unsigned long)SDL_GetTicks();
    if (browser->loading || browser->preparing || now - browser->selected_at < SELECTION_DELAY_MS ||
        now - browser->preview_at < PREVIEW_DELAY_MS) return;
    row = selected_row(browser);
    if (!eligible(browser, row))
    {
        row = NULL;
        end = browser->end_visible < browser->count ? browser->end_visible : browser->count;
        for (i = browser->first_visible; i < end; ++i)
            if (eligible(browser, browser->filtered[i]) && browser->filtered[i]->file.bytes <= 32.0 * 1024 * 1024)
            {
                row = browser->filtered[i];
                break;
            }
    }
    if (!row) return;
    browser->preview_at = now;
    prepare(browser, &row->file, row->index, MARCUS_BROWSER_PREVIEW);
}

MarcusBrowser *browser_create(SDL_Window *window, SDL_Renderer *renderer)
{
    MarcusBrowser *browser;
    char *pref_path;
    (void)window;
    (void)renderer;
    browser = SDL_calloc(1, sizeof(*browser));
    if (!browser) return NULL;
    browser->prefs.grid = 1;
    browser->selected = -1;
    browser->end_visible = 12;
    pref_path = SDL_GetPrefPath("3DMMForever", "Marcus");
    if (pref_path)
    {
        SDL_asprintf(&browser->state_path, "%smovie-browser.cfg", pref_path);
        SDL_free(pref_path);
        marcus_browser_preferences_load(&browser->prefs, browser->state_path);
    }
    browser->movie_folder = marcus_browser_movie_folder();
    if (!browser->prefs.location) set_text(&browser->prefs.location, browser->movie_folder);
    browser->worker = marcus_browser_worker_create();
    if (!browser->movie_folder || !browser->worker)
    {
        browser_destroy(browser);
        return NULL;
    }
    return browser;
}

void browser_destroy(MarcusBrowser *browser)
{
    if (!browser) return;
    if (browser->open) close_browser(browser);
    marcus_browser_worker_destroy(browser->worker);
    marcus_input_destroy(browser->chosen);
    clear_rows(browser);
    clear_history(browser);
    marcus_browser_preferences_clear(&browser->prefs);
    SDL_free(browser->history);
    SDL_free(browser->address);
    SDL_free(browser->filename);
    SDL_free(browser->movie_folder);
    SDL_free(browser->state_path);
    SDL_free(browser->resources);
    SDL_free(browser->content);
    SDL_free(browser->chosen_path);
    SDL_free(browser);
}

void browser_show(MarcusBrowser *browser, const char *path, const char *resources, const char *content)
{
    char *candidate, *parent, *current;
    const char *location;
    if (!browser) return;
    browser->open = 1;
    if (!set_text(&browser->resources, resources) || !set_text(&browser->content, content)) { copy_error(browser); return; }
    candidate = current = NULL;
    location = text_or_empty(browser->prefs.location);
    if (path && *path)
    {
        candidate = marcus_browser_path_absolute(path, 0);
        if (!candidate) goto failed;
        if (!is_directory(candidate) && marcus_browser_format(candidate) != MARCUS_BROWSER_ZIP)
        {
            parent = marcus_browser_path_parent(candidate);
            SDL_free(candidate);
            candidate = parent;
            if (!candidate) goto failed;
        }
        location = candidate;
    }
    if (!*location || (!is_directory(location) && marcus_browser_format(location) != MARCUS_BROWSER_ZIP))
    {
        current = marcus_browser_current_directory();
        if (!current) goto failed;
        location = current;
    }
    clear_history(browser);
    navigate(browser, location, 0);
    SDL_free(current);
    SDL_free(candidate);
    return;
failed:
    copy_error(browser);
    SDL_free(candidate);
    SDL_free(current);
}

int browser_visible(const MarcusBrowser *browser) { return browser && browser->open; }

MarcusMovieInput *browser_take(MarcusBrowser *browser, int *entry, char *path, size_t capacity)
{
    MarcusMovieInput *chosen;
    if (!browser || !browser->chosen || !entry || !path || !capacity) return NULL;
    chosen = browser->chosen;
    browser->chosen = NULL;
    *entry = browser->chosen_entry;
    SDL_strlcpy(path, browser->chosen_path, capacity);
    SDL_free(browser->chosen_path);
    browser->chosen_path = NULL;
    return chosen;
}

void browser_remember(MarcusBrowser *browser, const char *path, MarcusMovieInput *input, int entry)
{
    MarcusMovieEntry member;
    MarcusBrowserRecent remembered, *recent;
    int i, *count;
    if (!browser || !path || !*path || !input || !marcus_input_entry(input, entry, &member)) return;
    remembered.path = marcus_browser_path_absolute(path, 0);
    remembered.member = SDL_strdup(member.zipped ? member.name : "");
    if (!remembered.path || !remembered.member)
    {
        SDL_free(remembered.path);
        SDL_free(remembered.member);
        return;
    }
    recent = browser->prefs.recent;
    count = &browser->prefs.recent_count;
    for (i = *count - 1; i >= 0; --i)
        if (!strcmp(recent[i].path, remembered.path) && !strcmp(recent[i].member, remembered.member))
        {
            SDL_free(recent[i].path);
            SDL_free(recent[i].member);
            --*count;
            memmove(recent + i, recent + i + 1, (size_t)(*count - i) * sizeof(*recent));
        }
    if (*count == MARCUS_BROWSER_RECENT_LIMIT)
    {
        --*count;
        SDL_free(recent[*count].path);
        SDL_free(recent[*count].member);
    }
    memmove(recent + 1, recent, (size_t)*count * sizeof(*recent));
    recent[0] = remembered;
    ++*count;
    marcus_browser_preferences_save(&browser->prefs, browser->state_path);
}

static int favourite_index(const MarcusBrowser *browser)
{
    int i;
    for (i = 0; i < browser->prefs.favourite_count; ++i)
        if (!strcmp(browser->prefs.favourites[i], text_or_empty(browser->prefs.location))) return i;
    return -1;
}

static void toggle_favourite(MarcusBrowser *browser)
{
    char **favourites, *path;
    int i, *count;
    favourites = browser->prefs.favourites;
    count = &browser->prefs.favourite_count;
    i = favourite_index(browser);
    if (i >= 0)
    {
        SDL_free(favourites[i]);
        --*count;
        memmove(favourites + i, favourites + i + 1, (size_t)(*count - i) * sizeof(*favourites));
    }
    else if (*count < MARCUS_BROWSER_FAVOURITE_LIMIT)
    {
        path = SDL_strdup(text_or_empty(browser->prefs.location));
        if (!path) { copy_error(browser); return; }
        favourites[(*count)++] = path;
    }
    marcus_browser_preferences_save(&browser->prefs, browser->state_path);
}

static void movie_state(void *model, MarcusDialogState *out)
{
    const MarcusBrowser *browser;
    browser = model;
    memset(out, 0, sizeof(*out));
    out->open = browser->open;
    out->kind = DIALOG_MOVIES;
    out->count = browser->count;
    out->selected = browser->selected;
    out->grid = browser->prefs.grid;
    out->filter = browser->prefs.filter;
    out->sort_date = browser->prefs.sort_date;
    out->recent = browser->recent;
    out->loading = browser->loading;
    out->opening = browser->preparing && browser->operation == MARCUS_BROWSER_OPEN;
    out->calculating = browser->preparing && browser->operation == MARCUS_BROWSER_DURATION;
    out->favourite_count = browser->prefs.favourite_count;
    out->is_favourite = favourite_index(browser) >= 0;
    out->can_back = browser->history_count != 0;
    SDL_strlcpy(out->title, "Open movie", sizeof(out->title));
    SDL_strlcpy(out->confirm, "Open", sizeof(out->confirm));
    SDL_strlcpy(out->address, text_or_empty(browser->address), sizeof(out->address));
    SDL_strlcpy(out->filename, text_or_empty(browser->filename), sizeof(out->filename));
    SDL_strlcpy(out->search, browser->search, sizeof(out->search));
    SDL_strlcpy(out->error, browser->error, sizeof(out->error));
    SDL_snprintf(out->status, sizeof(out->status), "%d items%s", browser->count,
                 out->loading ? " / Reading folder..." : out->opening ? " / Preparing movie..." :
                 out->calculating ? " / Calculating duration..." : "");
}

static void movie_prepare(void *model, SDL_Renderer *renderer, int first, int end)
{
    MarcusBrowser *browser;
    browser = model;
    if (first >= 0)
    {
        browser->first_visible = first;
        browser->end_visible = end;
    }
    else if (browser->open) tick(browser, renderer);
}

static const char *row_kind(const BrowserRow *row)
{
    if (row->file.directory) return "FOLDER";
    if (row->path_format == MARCUS_BROWSER_ZIP)
    {
        if (!row->file.member[0]) return "ZIP";
        return row->member_format == MARCUS_BROWSER_VMM ? "VMM / ZIP" : "3MM / ZIP";
    }
    return row->path_format == MARCUS_BROWSER_VMM ? "VMM" : "3MM";
}

static int movie_item(void *model, SDL_Renderer *renderer, int index, MarcusDialogItem *out)
{
    MarcusBrowser *browser;
    const BrowserRow *row;
    BrowserCard *card;
    const char *path;
    char *name;
    char size[64];
    browser = model;
    (void)renderer;
    memset(out, 0, sizeof(*out));
    if (index < 0)
    {
        index = -(index + 1);
        if (index >= browser->prefs.favourite_count) return 0;
        path = browser->prefs.favourites[index];
        name = marcus_browser_path_basename(path);
        SDL_strlcpy(out->label, name ? name : path, sizeof(out->label));
        SDL_strlcpy(out->detail, path, sizeof(out->detail));
        SDL_free(name);
        return 1;
    }
    if (index >= browser->count) return 0;
    row = browser->filtered[index];
    card = card_for(browser, row);
    out->directory = row->file.directory;
    out->texture = card ? card->texture : NULL;
    SDL_strlcpy(out->label, row->file.name, sizeof(out->label));
    SDL_snprintf(size, sizeof(size), row->file.bytes >= 1048576 ? "%.1f MiB" : "%.0f KiB",
                 row->file.bytes / (row->file.bytes >= 1048576 ? 1048576 : 1024));
    SDL_snprintf(out->detail, sizeof(out->detail), "%s / %s\n%s%s%s%s", row_kind(row), size, row->file.path,
                 card ? card->description : "", card && card->duration[0] ? "\n" : "", card ? card->duration : "");
    return 1;
}

static void movie_action(void *model, int command, int index, float value, const char *text)
{
    MarcusBrowser *browser;
    const BrowserRow *row;
    const char *home;
    browser = model;
    (void)value;
    if (!browser->open) return;
    switch (command)
    {
    case DIALOG_SELECT: select_row(browser, index, selected_row(browser)); break;
    case DIALOG_CANCEL: close_browser(browser); break;
    case DIALOG_ACCEPT: accept(browser); break;
    case DIALOG_ADDRESS: if (!set_text(&browser->address, text)) copy_error(browser); break;
    case DIALOG_FILENAME: if (!set_text(&browser->filename, text)) copy_error(browser); break;
    case DIALOG_SEARCH:
        SDL_strlcpy(browser->search, text_or_empty(text), sizeof(browser->search));
        filter_rows(browser);
        break;
    case DIALOG_GO: open_location(browser, browser->address, 1); break;
    case DIALOG_DROP: open_location(browser, text, 0); break;
    case DIALOG_REFRESH: navigate(browser, browser->recent ? "" : text_or_empty(browser->prefs.location), 0); break;
    case DIALOG_BACK:
        if (browser->history_count && navigate(browser, browser->history[browser->history_count - 1], 0))
            SDL_free(browser->history[--browser->history_count]);
        break;
    case DIALOG_UP: go_up(browser); break;
    case DIALOG_FILTER: browser->prefs.filter = index; filter_rows(browser); break;
    case DIALOG_SORT: browser->prefs.sort_date = !browser->prefs.sort_date; filter_rows(browser); break;
    case DIALOG_GRID: browser->prefs.grid = !browser->prefs.grid; break;
    case DIALOG_RECENT: navigate(browser, "", 1); break;
    case DIALOG_MOVIE_FOLDER: navigate(browser, browser->movie_folder, 1); break;
    case DIALOG_HOME: if ((home = SDL_GetUserFolder(SDL_FOLDER_HOME)) != NULL) navigate(browser, home, 1); break;
    case DIALOG_FAVOURITE: toggle_favourite(browser); break;
    case DIALOG_FAVOURITE_GO:
        if (index >= 0 && index < browser->prefs.favourite_count) navigate(browser, browser->prefs.favourites[index], 1);
        break;
    case DIALOG_CALCULATE:
        if (browser->preparing && browser->operation == MARCUS_BROWSER_DURATION) cancel_preparation(browser);
        else if ((row = selected_row(browser)) != NULL) prepare(browser, &row->file, row->index, MARCUS_BROWSER_DURATION);
        break;
    }
}

const MarcusDialogHost marcus_movie_dialog_host = {movie_state, movie_prepare, movie_item, movie_action};
