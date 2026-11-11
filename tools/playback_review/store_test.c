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
    int resolved, timed;
    double delta;
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
    CHECK(sqlite3_open(path, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(
              db,
              "CREATE TABLE frames(run_id INTEGER,scene INTEGER,frame INTEGER,timestamp_ns INTEGER);"
              "CREATE TABLE images(run_id INTEGER,sequence INTEGER,scene INTEGER,frame INTEGER,timestamp_ns INTEGER);"
              "INSERT INTO frames VALUES(1,0,3,1000000000),(2,0,3,9000000000),(1,1,3,9000000000);"
              "INSERT INTO images VALUES(1,0,0,3,1000000000),(1,19,0,3,1110000000),(2,5,0,3,9100000000),"
              "(1,99,1,3,9100000000),(2,6,0,3,9900000000),(2,7,0,3,NULL);",
              NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    CHECK(review_open(&s, path));
    CHECK(review_presentation_pair(&s, 1, 2, 5, &delta, &timed) == 19 && delta == 10.0 && timed);
    CHECK(review_presentation_pair(&s, 1, 2, 6, &delta, &timed) == 19 && delta == -790.0 && timed);
    CHECK(review_presentation_pair(&s, 1, 2, 7, &delta, &timed) == 0 && !timed);
    CHECK(review_presentation_pair(&s, 3, 2, 5, &delta, &timed) == -1);
    CHECK(review_presentation_pair(&s, 2, 1, 99, &delta, &timed) == -1); /* Never cross scenes. */
    CHECK(sqlite3_open(path, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db,
                       "ALTER TABLE images ADD COLUMN pixel_sha256 TEXT;"
                       "UPDATE images SET pixel_sha256=CASE run_id WHEN 1 THEN 'reference' ELSE 'candidate' END;",
                       NULL, NULL, NULL) == SQLITE_OK);
    CHECK(review_stable_pair(&s, 1, 2, 0, 3) == 0);  /* Different pixels across runs still pair. */
    CHECK(review_stable_pair(&s, 1, 2, 1, 3) == -1); /* Missing candidate. */
    CHECK(sqlite3_exec(db, "UPDATE images SET pixel_sha256='changed' WHERE run_id=2 AND sequence=6", NULL, NULL,
                       NULL) == SQLITE_OK);
    CHECK(review_stable_pair(&s, 1, 2, 0, 3) == -1);
    CHECK(sqlite3_exec(db, "UPDATE images SET pixel_sha256=NULL WHERE run_id=2", NULL, NULL, NULL) == SQLITE_OK);
    CHECK(review_stable_pair(&s, 1, 2, 0, 3) == -1);
    sqlite3_close(db);
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
