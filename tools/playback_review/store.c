#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int review_stable_pair(ReviewStore *s, int reference, int candidate, int scene, int frame)
{
    sqlite3_stmt *stmt;
    int pair = -1;
    const char *sql = "SELECT min(sequence) FROM images WHERE run_id=?1 AND scene=?3 AND frame=?4 "
                      "HAVING count(DISTINCT pixel_sha256)=1 AND count(pixel_sha256)=count(*) "
                      "AND (SELECT count(DISTINCT pixel_sha256)=1 AND count(pixel_sha256)=count(*) "
                      "FROM images WHERE run_id=?2 AND scene=?3 AND frame=?4)";
    if (sqlite3_prepare_v2(s->results, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, reference);
    sqlite3_bind_int(stmt, 2, candidate);
    sqlite3_bind_int(stmt, 3, scene);
    sqlite3_bind_int(stmt, 4, frame);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        pair = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return pair;
}
int review_presentation_pair(ReviewStore *s, int reference, int candidate, int sequence, double *delta_ms, int *timed)
{
    sqlite3_stmt *q;
    int found;
    const char *sql =
        "SELECT r.sequence,((r.timestamp_ns-rf.timestamp_ns)-(c.timestamp_ns-cf.timestamp_ns))/1000000.0 delta "
        "FROM images c LEFT JOIN frames cf ON cf.run_id=c.run_id AND cf.scene=c.scene AND cf.frame=c.frame "
        "JOIN images r ON r.run_id=? AND r.scene=c.scene AND r.frame=c.frame "
        "LEFT JOIN frames rf ON rf.run_id=r.run_id AND rf.scene=r.scene AND rf.frame=r.frame "
        "WHERE c.run_id=? AND c.sequence=? "
        "ORDER BY delta IS NULL,abs(delta),r.sequence LIMIT 1";
    found = -1;
    *delta_ms = 0;
    *timed = 0;
    if (sqlite3_prepare_v2(s->results, sql, -1, &q, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(q, 1, reference);
    sqlite3_bind_int(q, 2, candidate);
    sqlite3_bind_int(q, 3, sequence);
    if (sqlite3_step(q) == SQLITE_ROW)
    {
        *delta_ms = sqlite3_column_double(q, 1);
        *timed = sqlite3_column_type(q, 1) != SQLITE_NULL;
        found = sqlite3_column_int(q, 0);
    }
    sqlite3_finalize(q);
    return found;
}

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
