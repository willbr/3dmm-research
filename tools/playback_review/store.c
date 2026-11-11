#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failure(ReviewStore *s, const char *message)
{
    sqlite3_snprintf((int)sizeof(s->error), s->error, "%s", message);
    return 0;
}

void review_close(ReviewStore *s)
{
    sqlite3_close(s->results);
    sqlite3_close(s->notes);
    s->results = NULL;
    s->notes = NULL;
}

int review_open(ReviewStore *s, const char *path)
{
    sqlite3_stmt *q;
    char *sidecar;
    int version;
    memset(s, 0, sizeof(*s));
    if (sqlite3_open_v2(path, &s->results, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
        goto failed;
    if (sqlite3_prepare_v2(s->results, "PRAGMA user_version", -1, &q, NULL) != SQLITE_OK)
        goto failed;
    version = sqlite3_step(q) == SQLITE_ROW ? sqlite3_column_int(q, 0) : -1;
    sqlite3_finalize(q);
    if (version != 1)
    {
        failure(s, "Unsupported results schema (expected playback schema 1)");
        review_close(s);
        return 0;
    }
    if (sqlite3_prepare_v2(s->results, "SELECT suite_id,case_name,player,mode FROM runs LIMIT 1", -1, &q, NULL) !=
        SQLITE_OK)
        goto failed;
    sqlite3_finalize(q);
    sidecar = sqlite3_mprintf("%s.notes.sqlite", path);
    if (!sidecar)
    {
        review_close(s);
        return failure(s, "Out of memory");
    }
    version = sqlite3_open(sidecar, &s->notes);
    sqlite3_free(sidecar);
    if (version != SQLITE_OK)
        goto failed;
    sqlite3_busy_timeout(s->notes, 3000);
    if (sqlite3_exec(s->notes,
                     "CREATE TABLE IF NOT EXISTS notes("
                     "suite TEXT NOT NULL,run INTEGER NOT NULL,sequence INTEGER NOT NULL,"
                     "body TEXT NOT NULL,resolved INTEGER NOT NULL,updated TEXT NOT NULL,"
                     "PRIMARY KEY(suite,run,sequence));",
                     NULL, NULL, NULL) != SQLITE_OK)
        goto failed;
    return 1;
failed:
    failure(s, sqlite3_errmsg(s->notes ? s->notes : s->results));
    review_close(s);
    return 0;
}

static int note_query(ReviewStore *s, sqlite3_stmt **q, const char *sql, int run, int sequence)
{
    sqlite3_stmt *r;
    int rc;
    s->error[0] = 0;
    rc = sqlite3_prepare_v2(s->results, "SELECT suite_id FROM runs WHERE id=?", -1, &r, NULL);
    if (rc != SQLITE_OK)
        return failure(s, sqlite3_errmsg(s->results));
    sqlite3_bind_int(r, 1, run);
    if (sqlite3_step(r) != SQLITE_ROW)
    {
        sqlite3_finalize(r);
        return failure(s, "Run no longer exists");
    }
    rc = sqlite3_prepare_v2(s->notes, sql, -1, q, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_text(*q, 1, (const char *)sqlite3_column_text(r, 0), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(*q, 2, run);
        sqlite3_bind_int(*q, 3, sequence);
    }
    sqlite3_finalize(r);
    return rc == SQLITE_OK ? 1 : failure(s, sqlite3_errmsg(s->notes));
}

int review_note_load(ReviewStore *s, int run, int sequence, char *body, size_t capacity, int *resolved)
{
    sqlite3_stmt *q;
    int rc, ok;
    body[0] = 0;
    *resolved = 0;
    if (!note_query(s, &q, "SELECT body,resolved FROM notes WHERE suite=? AND run=? AND sequence=?", run, sequence))
        return 0;
    rc = sqlite3_step(q);
    ok = rc == SQLITE_ROW || rc == SQLITE_DONE;
    if (rc == SQLITE_ROW)
    {
        if ((size_t)sqlite3_column_bytes(q, 0) >= capacity)
        {
            ok = failure(s, "Note is larger than editor capacity; refusing to truncate");
        }
        else
        {
            strcpy(body, (const char *)sqlite3_column_text(q, 0));
            *resolved = sqlite3_column_int(q, 1);
        }
    }
    else if (!ok)
        failure(s, sqlite3_errmsg(s->notes));
    sqlite3_finalize(q);
    return ok;
}

int review_note_save(ReviewStore *s, int run, int sequence, const char *body, int resolved)
{
    sqlite3_stmt *q;
    int rc;
    if (!note_query(s, &q,
                    "INSERT INTO notes VALUES(?,?,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
                    "ON CONFLICT(suite,run,sequence) DO UPDATE SET "
                    "body=excluded.body,resolved=excluded.resolved,updated=excluded.updated",
                    run, sequence))
        return 0;
    sqlite3_bind_text(q, 4, body, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(q, 5, resolved);
    rc = sqlite3_step(q);
    if (rc != SQLITE_DONE)
        failure(s, sqlite3_errmsg(s->notes));
    sqlite3_finalize(q);
    return rc == SQLITE_DONE;
}
