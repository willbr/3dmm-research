#include "store.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Line %d: %s\n", __LINE__, #x);                                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void)
{
    ReviewStore s;
    sqlite3 *db;
    char body[256];
    int resolved;
    const char *path = "review-store-test.sqlite";
    remove(path);
    remove("review-store-test.sqlite.notes.sqlite");
    CHECK(sqlite3_open(path, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(
              db,
              "PRAGMA user_version=1;CREATE TABLE runs(id INTEGER,suite_id TEXT,case_name TEXT,player TEXT,mode TEXT);"
              "INSERT INTO runs VALUES(1,'suite-a','text-acid','sdl','seek');",
              NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    CHECK(review_open(&s, path));
    CHECK(sqlite3_db_readonly(s.results, "main") == 1);
    CHECK(review_note_load(&s, 1, -1, body, sizeof(body), &resolved) && !body[0] && !resolved);
    CHECK(review_note_save(&s, 1, -1, "Whole run: apostrophe ' and UTF-8 \xc3\xa9", 0));
    CHECK(review_note_save(&s, 1, 0, "Frame zero\nsecond line", 1));
    CHECK(review_note_save(&s, 1, 10, "Different frame", 0));
    review_close(&s);
    CHECK(review_open(&s, path));
    CHECK(review_note_load(&s, 1, 0, body, sizeof(body), &resolved));
    CHECK(!strcmp(body, "Frame zero\nsecond line") && resolved);
    CHECK(review_note_load(&s, 1, -1, body, sizeof(body), &resolved));
    CHECK(strstr(body, "apostrophe '") != NULL && !resolved);
    CHECK(!review_note_load(&s, 1, -1, body, 3, &resolved));
    CHECK(strstr(s.error, "truncate") != NULL);
    CHECK(review_note_save(&s, 1, 0, "Updated", 0));
    CHECK(review_note_load(&s, 1, 0, body, sizeof(body), &resolved));
    CHECK(!strcmp(body, "Updated") && !resolved);
    CHECK(!review_note_save(&s, 99, 0, "Orphan", 0));
    review_close(&s);
    CHECK(sqlite3_open(path, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "UPDATE runs SET suite_id='suite-b'", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    CHECK(review_open(&s, path));
    CHECK(review_note_load(&s, 1, 0, body, sizeof(body), &resolved) && !body[0]);
    review_close(&s);
    CHECK(sqlite3_open(path, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "PRAGMA user_version=99", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    CHECK(!review_open(&s, path));
    CHECK(strstr(s.error, "schema") != NULL);
    remove(path);
    remove("review-store-test.sqlite.notes.sqlite");
    puts("Review store: persistence, scope, UTF-8, read-only evidence and schema checks passed.");
    return 0;
}
