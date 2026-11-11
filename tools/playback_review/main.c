/* Playback evidence browser. All application logic intentionally uses C89. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "backend.h"
#include "store.h"
#include <cimgui.h>

typedef struct Table
{
    char **cells;
    int rows, columns;
} Table;
typedef struct Picture
{
    SDL_Surface *pixels;
    SDL_Texture *texture;
    char error[512];
} Picture;
static ReviewStore store;
static Table runs, frames;
static char database[4096], filter[256], note[32768];
static int selected = -1, reference = -1, cursor, loaded_cursor = -2;
static int note_sequence = -1, note_ok, dirty;
static int resolved, frame_note;
static Picture pictures[3];
static SDL_Renderer *renderer;
static float zoom = 1.0f;
static int differences = -1;

static ImVec2 vec(float x, float y)
{
    ImVec2 v;
    v.x = x;
    v.y = y;
    return v;
}
static const char *cell(Table *t, int row, int column)
{
    const char *p;
    p = t->cells[(row + 1) * t->columns + column];
    return p ? p : "";
}
static void clear_table(Table *t)
{
    sqlite3_free_table(t->cells);
    memset(t, 0, sizeof(*t));
}
static int query(Table *t, const char *sql)
{
    char *error;
    int rc;
    clear_table(t);
    error = NULL;
    rc = sqlite3_get_table(store.results, sql, &t->cells, &t->rows, &t->columns, &error);
    if (rc != SQLITE_OK)
    {
        sqlite3_snprintf((int)sizeof(store.error), store.error, "%s", error ? error : "Query failed");
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}
static void clear_picture(Picture *p)
{
    SDL_DestroyTexture(p->texture);
    SDL_DestroySurface(p->pixels);
    memset(p, 0, sizeof(*p));
}
static void make_texture(Picture *p)
{
    p->texture = SDL_CreateTextureFromSurface(renderer, p->pixels);
    if (p->texture)
        SDL_SetTextureScaleMode(p->texture, SDL_SCALEMODE_NEAREST);
    else
        SDL_snprintf(p->error, sizeof(p->error), "%s", SDL_GetError());
}
static void load_picture(Picture *p, const char *relative)
{
    char path[8192];
    char *slash, *back;
    SDL_Surface *source;
    clear_picture(p);
    if (!relative[0])
    {
        strcpy(p->error, "No captured image");
        return;
    }
    SDL_strlcpy(path, database, sizeof(path));
    slash = strrchr(path, '/');
    back = strrchr(path, '\\');
    if (back && (!slash || back > slash))
        slash = back;
    if (slash)
        slash[1] = 0;
    else
        path[0] = 0;
    if (relative[0] == '/' || relative[0] == '\\' || (strlen(relative) > 1 && relative[1] == ':'))
        path[0] = 0;
    SDL_strlcat(path, relative, sizeof(path));
    source = SDL_LoadPNG(path);
    if (!source)
    {
        SDL_snprintf(p->error, sizeof(p->error), "%s", SDL_GetError());
        return;
    }
    p->pixels = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(source);
    if (p->pixels)
        make_texture(p);
    else
        SDL_snprintf(p->error, sizeof(p->error), "%s", SDL_GetError());
}
static void load_note(void)
{
    int state;
    if (selected < 0)
        return;
    note_sequence = frame_note && frames.rows ? atoi(cell(&frames, cursor, 0)) : -1;
    note_ok = review_note_load(&store, selected, note_sequence, note, sizeof(note), &state);
    resolved = state != 0;
    dirty = 0;
}
static void choose_run(int row)
{
    Table t;
    char *sql;
    memset(&t, 0, sizeof(t));
    selected = atoi(cell(&runs, row, 0));
    sql = sqlite3_mprintf("SELECT b.id FROM runs a JOIN cases c ON c.suite_id=a.suite_id AND c.name=a.case_name "
                          "JOIN runs b ON b.suite_id=a.suite_id AND b.case_name=a.case_name AND b.mode=a.mode "
                          "AND b.preparation=a.preparation AND b.repetition=a.repetition AND b.player=c.reference "
                          "WHERE a.id=%d ORDER BY b.id DESC LIMIT 1",
                          selected);
    query(&t, sql);
    sqlite3_free(sql);
    reference = t.rows ? atoi(cell(&t, 0, 0)) : -1;
    clear_table(&t);
    sql = sqlite3_mprintf("SELECT sequence,scene,frame FROM frames WHERE run_id=%d "
                          "UNION SELECT sequence,scene,frame FROM images WHERE run_id=%d ORDER BY sequence",
                          selected, selected);
    query(&frames, sql);
    sqlite3_free(sql);
    cursor = 0;
    loaded_cursor = -2;
    load_note();
}
static void load_images(void)
{
    Table t;
    char *sql;
    int i, x, y, seq, scene, frame;
    unsigned char *a, *b, *d;
    Picture *p;
    memset(&t, 0, sizeof(t));
    loaded_cursor = cursor;
    for (i = 0; i < 3; ++i)
        clear_picture(&pictures[i]);
    differences = -1;
    if (!frames.rows)
        return;
    seq = atoi(cell(&frames, cursor, 0));
    scene = atoi(cell(&frames, cursor, 1));
    frame = atoi(cell(&frames, cursor, 2));
    for (i = 0; i < 2; ++i)
    {
        sql = sqlite3_mprintf("SELECT path FROM images WHERE run_id=%d AND sequence=%d AND scene=%d AND frame=%d",
                              i ? selected : reference, seq, scene, frame);
        query(&t, sql);
        sqlite3_free(sql);
        load_picture(&pictures[i], t.rows ? cell(&t, 0, 0) : "");
        clear_table(&t);
    }
    p = &pictures[2];
    if (!pictures[0].pixels || !pictures[1].pixels)
    {
        strcpy(p->error, "Difference unavailable: missing paired evidence");
        return;
    }
    if (pictures[0].pixels->w != pictures[1].pixels->w || pictures[0].pixels->h != pictures[1].pixels->h)
    {
        strcpy(p->error, "Difference unavailable: image dimensions differ");
        return;
    }
    p->pixels = SDL_CreateSurface(pictures[0].pixels->w, pictures[0].pixels->h, SDL_PIXELFORMAT_RGBA32);
    if (!p->pixels)
    {
        SDL_strlcpy(p->error, SDL_GetError(), sizeof(p->error));
        return;
    }
    differences = 0;
    for (y = 0; y < p->pixels->h; ++y)
    {
        a = (unsigned char *)pictures[0].pixels->pixels + y * pictures[0].pixels->pitch;
        b = (unsigned char *)pictures[1].pixels->pixels + y * pictures[1].pixels->pitch;
        d = (unsigned char *)p->pixels->pixels + y * p->pixels->pitch;
        for (x = 0; x < p->pixels->w; ++x)
        {
            i = x * 4;
            d[i] = d[i + 2] = (unsigned char)((a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ? 255 : 0);
            d[i + 1] = 0;
            d[i + 3] = 255;
            if (d[i])
                ++differences;
        }
    }
    make_texture(p);
}
static void show_query(const char *sql)
{
    Table t;
    int r, c;
    memset(&t, 0, sizeof(t));
    query(&t, sql);
    if (!t.rows)
        igTextUnformatted("No recorded evidence for this selection.", NULL);
    for (r = 0; r < t.rows; ++r)
    {
        for (c = 0; c < t.columns; ++c)
            igTextWrapped("%s: %s", t.cells[c], cell(&t, r, c));
        igSeparator();
    }
    clear_table(&t);
}
static void next_difference(void)
{
    Table t;
    char *sql;
    int i, seq;
    memset(&t, 0, sizeof(t));
    seq = frames.rows ? atoi(cell(&frames, cursor, 0)) : -1;
    sql = sqlite3_mprintf("SELECT a.sequence FROM images a LEFT JOIN images b ON b.run_id=%d AND b.sequence=a.sequence "
                          "WHERE a.run_id=%d AND (b.sequence IS NULL OR a.scene<>b.scene OR a.frame<>b.frame OR "
                          "a.width<>b.width OR a.height<>b.height OR a.pixel_sha256<>b.pixel_sha256) "
                          "ORDER BY a.sequence<=%d,a.sequence LIMIT 1",
                          reference, selected, seq);
    query(&t, sql);
    sqlite3_free(sql);
    if (t.rows)
    {
        seq = atoi(cell(&t, 0, 0));
        for (i = 0; i < frames.rows; ++i)
            if (atoi(cell(&frames, i, 0)) == seq)
            {
                cursor = i;
                break;
            }
    }
    else
        SDL_strlcpy(store.error, "No differing candidate images recorded; this does not establish playback parity.",
                    sizeof(store.error));
    clear_table(&t);
}
static void details(void)
{
    int i, old, sequence;
    char *sql;
    ImTextureRef tex;
    const char *titles[3] = {"Reference", "Candidate", "Changed RGB pixels (magenta)"};
    if (selected < 0)
        return;
    igText("Candidate run %d | Reference run %d", selected, reference);
    igTextWrapped("Seek images are not proof of continuous playback parity. Inconclusive results remain inconclusive.");
    igBeginDisabled(dirty != 0);
    old = cursor;
    if (frames.rows)
    {
        if (igButton("Previous", vec(0, 0)) && cursor > 0)
            --cursor;
        igSameLine(0, -1);
        if (igButton("Next", vec(0, 0)) && cursor + 1 < frames.rows)
            ++cursor;
        igSameLine(0, -1);
        if (igButton("Next image difference", vec(0, 0)))
            next_difference();
        igSameLine(0, -1);
        igSliderInt("Observation", &cursor, 0, frames.rows - 1, "%d", 0);
        igText("Sequence %s | Scene %s (zero-based) | Authored frame %s", cell(&frames, cursor, 0),
               cell(&frames, cursor, 1), cell(&frames, cursor, 2));
    }
    else
        igTextUnformatted("No frame observations captured.", NULL);
    if (igCheckboxFlags_IntPtr("Note on this frame (otherwise entire run)", &frame_note, 1) || old != cursor)
        load_note();
    igEndDisabled();
    if (loaded_cursor != cursor)
        load_images();
    igBeginDisabled(!note_ok);
    if (igInputTextMultiline("Review note", note, sizeof(note), vec(-1, 85), 0, NULL, NULL))
        dirty = 1;
    if (igCheckboxFlags_IntPtr("Resolved", &resolved, 1))
        dirty = 1;
    igSameLine(0, -1);
    if (igButton("Save note", vec(0, 0)))
    {
        if (review_note_save(&store, selected, note_sequence, note, resolved))
            dirty = 0;
    }
    igSameLine(0, -1);
    if (igButton("Discard edits / reload note", vec(0, 0)))
        load_note();
    igSameLine(0, -1);
    igTextUnformatted(dirty ? "Unsaved: save or discard before navigating/closing" : "Saved / no pending edits", NULL);
    igEndDisabled();
    if (igCollapsingHeader_TreeNodeFlags("Recorded comparisons and policies", 0))
    {
        sql = sqlite3_mprintf("SELECT reference_run,component,status,metrics_json,policy_json FROM comparisons "
                              "WHERE candidate_run=%d AND id IN (SELECT max(id) FROM comparisons GROUP BY "
                              "reference_run,candidate_run,component)",
                              selected);
        show_query(sql);
        sqlite3_free(sql);
    }
    if (igCollapsingHeader_TreeNodeFlags("Run provenance, capabilities and errors", 0))
    {
        sql = sqlite3_mprintf("SELECT "
                              "suite_id,case_name,player,mode,preparation,repetition,status,error,capabilities_json,"
                              "provenance_json FROM runs WHERE id=%d",
                              selected);
        show_query(sql);
        sqlite3_free(sql);
    }
    if (igCollapsingHeader_TreeNodeFlags("Frame timing and state", 0) && frames.rows)
    {
        sequence = atoi(cell(&frames, cursor, 0));
        sql = sqlite3_mprintf("SELECT run_id,sequence,scene,frame,timestamp_ns,duration_since_previous_ns FROM "
                              "frame_timing WHERE run_id IN (%d,%d) AND sequence=%d",
                              selected, reference, sequence);
        show_query(sql);
        sqlite3_free(sql);
        sql = sqlite3_mprintf("SELECT state_json FROM frames WHERE run_id=%d AND sequence=%d", selected, sequence);
        show_query(sql);
        sqlite3_free(sql);
    }
    if (igCollapsingHeader_TreeNodeFlags("Audio recording and sound events", 0))
    {
        sql = sqlite3_mprintf("SELECT run_id,path,anchor_sample,metadata_json FROM audio WHERE run_id IN (%d,%d)",
                              selected, reference);
        show_query(sql);
        sqlite3_free(sql);
        if (frames.rows)
        {
            sql = sqlite3_mprintf("SELECT run_id,timestamp_ns,event_json FROM sound_events WHERE run_id IN (%d,%d) AND "
                                  "scene=%d AND frame=%d",
                                  selected, reference, atoi(cell(&frames, cursor, 1)), atoi(cell(&frames, cursor, 2)));
            show_query(sql);
            sqlite3_free(sql);
        }
    }
    if (igCollapsingHeader_TreeNodeFlags("Saved notes for this run", 0))
    {
        sqlite3_stmt *q;
        if (sqlite3_prepare_v2(store.notes,
                               "SELECT sequence,resolved,body,updated FROM notes WHERE run=? AND suite=(SELECT ?)", -1,
                               &q, NULL) == SQLITE_OK)
        {
            Table t;
            memset(&t, 0, sizeof(t));
            sql = sqlite3_mprintf("SELECT suite_id FROM runs WHERE id=%d", selected);
            query(&t, sql);
            sqlite3_free(sql);
            sqlite3_bind_int(q, 1, selected);
            sqlite3_bind_text(q, 2, t.rows ? cell(&t, 0, 0) : "", -1, SQLITE_TRANSIENT);
            while (sqlite3_step(q) == SQLITE_ROW)
            {
                igText("Sequence %d (-1 = run) | %s | %s", sqlite3_column_int(q, 0),
                       sqlite3_column_int(q, 1) ? "resolved" : "open", sqlite3_column_text(q, 3));
                igTextWrapped("%s", sqlite3_column_text(q, 2));
            }
            sqlite3_finalize(q);
            clear_table(&t);
        }
    }
    igSliderFloat("Image zoom (nearest neighbour)", &zoom, 0.5f, 4.0f, "%.1fx", 0);
    if (differences >= 0)
        igText("%d changed RGB pixels in this image pair", differences);
    if (igBeginTable("Images", 3, ImGuiTableFlags_SizingFixedFit, vec(0, 0), 0))
    {
        for (i = 0; i < 3; ++i)
            igTableSetupColumn(titles[i], ImGuiTableColumnFlags_WidthFixed,
                               pictures[i].pixels ? pictures[i].pixels->w * zoom : 320.0f, 0);
        for (i = 0; i < 3; ++i)
        {
            igTableNextColumn();
            igTextUnformatted(titles[i], NULL);
            if (pictures[i].texture)
            {
                tex._TexData = NULL;
                tex._TexID = (ImTextureID)(size_t)pictures[i].texture;
                igImage(tex, vec(pictures[i].pixels->w * zoom, pictures[i].pixels->h * zoom), vec(0, 0), vec(1, 1));
            }
            else
                igTextWrapped("%s", pictures[i].error[0] ? pictures[i].error : "No image");
        }
        igEndTable();
    }
}

static void run_table(void)
{
    static const char *names[] = {"Player", "Mode", "Status", "Repeat", "Case", "Run ID", "Preparation"};
    static const char *columns[] = {"player", "mode", "status", "repetition", "case_name", "id", "preparation"};
    static const int fields[] = {3, 4, 7, 6, 2, 0, 5};
    ImGuiTableSortSpecs *sort;
    ImVec4 hover;
    char label[1024];
    char *sql;
    int i, column, active;
    igBeginDisabled(dirty != 0);
    if (igBeginTable("Runs", 7,
                     ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                         ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                     vec(0, 185), 0))
    {
        for (column = 0; column < 7; ++column)
            igTableSetupColumn(
                names[column],
                column == 5 ? ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending : 0, 0,
                (ImGuiID)column);
        igTableSetupScrollFreeze(0, 1);
        igTableHeadersRow();
        sort = igTableGetSortSpecs();
        if (sort && sort->SpecsDirty && sort->SpecsCount)
        {
            column = (int)sort->Specs[0].ColumnUserID;
            if (column >= 0 && column < 7)
            {
                sql = sqlite3_mprintf("SELECT id,suite_id,case_name,player,mode,preparation,repetition,status "
                                      "FROM runs ORDER BY %s %s,id DESC",
                                      columns[column],
                                      sort->Specs[0].SortDirection == ImGuiSortDirection_Descending ? "DESC" : "ASC");
                query(&runs, sql);
                sqlite3_free(sql);
            }
            sort->SpecsDirty = 0;
        }
        for (i = 0; i < runs.rows; ++i)
        {
            SDL_snprintf(label, sizeof(label), "%s | %s | %s | %s | %s | %s | %s | %s", cell(&runs, i, 0),
                         cell(&runs, i, 2), cell(&runs, i, 3), cell(&runs, i, 4), cell(&runs, i, 5), cell(&runs, i, 6),
                         cell(&runs, i, 7), cell(&runs, i, 1));
            if (filter[0] && !strstr(label, filter))
                continue;
            active = selected == atoi(cell(&runs, i, 0));
            igTableNextRow(0, 0);
            igTableSetColumnIndex(0);
            SDL_snprintf(label, sizeof(label), "%s##run-%s", cell(&runs, i, 3), cell(&runs, i, 0));
            hover.x = active ? 0.26f : 0.20f;
            hover.y = active ? 0.59f : 0.20f;
            hover.z = active ? 0.98f : 0.20f;
            hover.w = 0.8f;
            igPushStyleColor_Vec4(ImGuiCol_HeaderHovered, hover);
            if (igSelectable_Bool(label, active, ImGuiSelectableFlags_SpanAllColumns, vec(0, 0)))
                choose_run(i);
            igPopStyleColor(1);
            for (column = 1; column < 7; ++column)
            {
                igTableSetColumnIndex(column);
                igTextUnformatted(cell(&runs, i, fields[column]), NULL);
            }
        }
        igEndTable();
    }
    igEndDisabled();
}

int main(int argc, char **argv)
{
    SDL_Window *window;
    SDL_Event event;
    int running, i, width, height, smoke;
    if (argc < 2)
    {
        if (!review_pick_database(database, (int)sizeof(database)))
        {
            fprintf(stderr, "Usage: playback-review <playback.sqlite> [--smoke [screenshot.png]]\n");
            return 2;
        }
    }
    else
    {
        if (strlen(argv[1]) >= sizeof(database))
            return 2;
        strcpy(database, argv[1]);
    }
    if (!review_open(&store, database))
    {
        fprintf(stderr, "%s\n", store.error);
        return 1;
    }
    if (!query(&runs,
               "SELECT id,suite_id,case_name,player,mode,preparation,repetition,status FROM runs ORDER BY id DESC"))
    {
        fprintf(stderr, "%s\n", store.error);
        review_close(&store);
        return 1;
    }
    smoke = argc > 2 && strcmp(argv[2], "--smoke") == 0 ? 3 : 0;
    if (!SDL_Init(SDL_INIT_VIDEO) ||
        !SDL_CreateWindowAndRenderer("3DMM Playback Review", 1720, 960, review_window_flags(smoke), &window, &renderer))
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        review_close(&store);
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    igCreateContext(NULL);
    igGetIO_Nil()->IniFilename = NULL;
    igStyleColorsDark(NULL);
    if (!review_backend_init(window, renderer))
    {
        fprintf(stderr, "ImGui backend initialization failed\n");
        return 1;
    }
    /* Prefer the newest SDL seek run for the primary parity workflow. */
    for (i = 0; i < runs.rows; ++i)
        if (!strcmp(cell(&runs, i, 3), "sdl") && !strcmp(cell(&runs, i, 4), "seek"))
            break;
    if (runs.rows)
        choose_run(i < runs.rows ? i : 0);
    running = 1;
    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            review_backend_event(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                if (!dirty)
                    running = 0;
                else
                    SDL_strlcpy(store.error, "Unsaved note: save or discard before closing.", sizeof(store.error));
            }
        }
        review_backend_frame();
        igNewFrame();
        SDL_GetWindowSize(window, &width, &height);
        igSetNextWindowPos(vec(0, 0), 0, vec(0, 0));
        igSetNextWindowSize(vec((float)width, (float)height), 0);
        igBegin("Playback review", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        igTextUnformatted(database, NULL);
        igInputTextWithHint("Filter runs", "suite / movie / player / mode / status", filter, sizeof(filter), 0, NULL,
                            NULL);
        run_table();
        if (store.error[0])
            igTextWrapped("%s", store.error);
        igBeginChild_Str("Evidence", vec(0, 0), 0, ImGuiWindowFlags_HorizontalScrollbar);
        details();
        igEndChild();
        igEnd();
        igRender();
        SDL_SetRenderDrawColor(renderer, 20, 20, 24, 255);
        SDL_RenderClear(renderer);
        review_backend_render(renderer);
        if (smoke == 1 && argc > 3)
        {
            SDL_Surface *screenshot;
            screenshot = SDL_RenderReadPixels(renderer, NULL);
            if (!screenshot || !SDL_SavePNG(screenshot, argv[3]))
            {
                fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
                SDL_DestroySurface(screenshot);
                return 1;
            }
            SDL_DestroySurface(screenshot);
        }
        SDL_RenderPresent(renderer);
        if (smoke && --smoke == 0)
            running = 0;
        SDL_Delay(10);
    }
    for (i = 0; i < 3; ++i)
        clear_picture(&pictures[i]);
    clear_table(&runs);
    clear_table(&frames);
    review_close(&store);
    review_backend_quit();
    igDestroyContext(NULL);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
