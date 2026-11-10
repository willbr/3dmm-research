"""Versioned SQLite observations for playback regression runs.

Media stays beside the database. Observations are immutable inputs to separately
versioned comparisons; a matching seek pass never implies playback parity.
"""
import hashlib
import json
from pathlib import Path
import sqlite3

SCHEMA = """
CREATE TABLE IF NOT EXISTS suites (
 id TEXT PRIMARY KEY, started TEXT NOT NULL, manifest_json TEXT NOT NULL,
 environment_json TEXT NOT NULL, source_revision TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS movies (
 id INTEGER PRIMARY KEY, sha256 TEXT NOT NULL UNIQUE, path TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS cases (
 suite_id TEXT NOT NULL REFERENCES suites, name TEXT NOT NULL,
 movie_id INTEGER NOT NULL REFERENCES movies, reference TEXT NOT NULL,
 features_json TEXT NOT NULL, PRIMARY KEY(suite_id, name));
CREATE TABLE IF NOT EXISTS runs (
 id INTEGER PRIMARY KEY, suite_id TEXT NOT NULL, case_name TEXT NOT NULL,
 player TEXT NOT NULL, mode TEXT NOT NULL, preparation TEXT NOT NULL,
 repetition INTEGER NOT NULL, status TEXT NOT NULL DEFAULT 'running',
 artifact_dir TEXT NOT NULL, capabilities_json TEXT NOT NULL,
 provenance_json TEXT NOT NULL DEFAULT '{}', error TEXT,
 FOREIGN KEY(suite_id, case_name) REFERENCES cases(suite_id, name));
CREATE TABLE IF NOT EXISTS scenes (
 run_id INTEGER NOT NULL REFERENCES runs, scene INTEGER NOT NULL,
 first_frame INTEGER NOT NULL, last_frame INTEGER NOT NULL,
 PRIMARY KEY(run_id, scene));
CREATE TABLE IF NOT EXISTS frames (
 run_id INTEGER NOT NULL REFERENCES runs, sequence INTEGER NOT NULL,
 scene INTEGER NOT NULL, frame INTEGER NOT NULL, timestamp_ns INTEGER,
 state_json TEXT NOT NULL, PRIMARY KEY(run_id, sequence));
CREATE TABLE IF NOT EXISTS images (
 run_id INTEGER NOT NULL REFERENCES runs, sequence INTEGER NOT NULL,
 scene INTEGER NOT NULL, frame INTEGER NOT NULL, timestamp_ns INTEGER,
 render_ns INTEGER, capture_ns INTEGER, width INTEGER NOT NULL,
 height INTEGER NOT NULL, pixel_sha256 TEXT NOT NULL, artifact_sha256 TEXT NOT NULL,
 path TEXT NOT NULL, state_json TEXT NOT NULL, PRIMARY KEY(run_id, sequence));
CREATE TABLE IF NOT EXISTS sound_events (
 run_id INTEGER NOT NULL REFERENCES runs, sequence INTEGER NOT NULL,
 scene INTEGER NOT NULL, frame INTEGER NOT NULL, timestamp_ns INTEGER,
 event_json TEXT NOT NULL, PRIMARY KEY(run_id, sequence));
CREATE TABLE IF NOT EXISTS audio (
 run_id INTEGER PRIMARY KEY REFERENCES runs, path TEXT NOT NULL,
 sha256 TEXT NOT NULL, anchor_sample INTEGER, metadata_json TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS comparisons (
 id INTEGER PRIMARY KEY, reference_run INTEGER NOT NULL REFERENCES runs,
 candidate_run INTEGER NOT NULL REFERENCES runs, component TEXT NOT NULL,
 status TEXT NOT NULL, policy_json TEXT NOT NULL, metrics_json TEXT NOT NULL);
CREATE VIEW IF NOT EXISTS run_review AS
 SELECT r.id, r.suite_id, r.case_name, r.player, r.mode, r.preparation,
 r.repetition, r.status, (SELECT count(*) FROM frames f WHERE f.run_id=r.id) frames,
 (SELECT count(*) FROM images i WHERE i.run_id=r.id) images, r.error FROM runs r;
CREATE VIEW IF NOT EXISTS comparison_review AS
 SELECT c.id, a.suite_id, a.case_name, a.player reference_player,
 b.player candidate_player, a.mode, a.preparation, a.repetition,
 c.component, c.status, c.metrics_json, c.policy_json
 FROM comparisons c JOIN runs a ON a.id=c.reference_run JOIN runs b ON b.id=c.candidate_run;
CREATE VIEW IF NOT EXISTS latest_comparison_review AS
 SELECT * FROM comparison_review WHERE id IN
 (SELECT max(id) FROM comparisons GROUP BY reference_run,candidate_run,component);
CREATE VIEW IF NOT EXISTS frame_timing AS
 SELECT run_id, sequence, scene, frame, timestamp_ns,
 timestamp_ns-lag(timestamp_ns) OVER(PARTITION BY run_id ORDER BY sequence) duration_since_previous_ns
 FROM frames;
CREATE VIEW IF NOT EXISTS image_review AS
 SELECT ar.suite_id,ar.case_name,ar.preparation,ar.player reference_player,
 br.player candidate_player,ar.id reference_run,br.id candidate_run,
 a.sequence,a.scene,a.frame,a.path reference_path,b.path candidate_path,
 a.pixel_sha256 reference_hash,b.pixel_sha256 candidate_hash,
 CASE WHEN a.scene=b.scene AND a.frame=b.frame AND a.width=b.width AND a.height=b.height
 AND a.pixel_sha256=b.pixel_sha256 THEN 1 ELSE 0 END equal_pixels
 FROM runs ar JOIN cases c ON c.suite_id=ar.suite_id AND c.name=ar.case_name
 JOIN runs br ON br.suite_id=ar.suite_id AND br.case_name=ar.case_name
 AND br.mode=ar.mode AND br.preparation=ar.preparation AND br.repetition=ar.repetition
 JOIN images a ON a.run_id=ar.id LEFT JOIN images b ON b.run_id=br.id AND b.sequence=a.sequence
 WHERE ar.mode='seek' AND ar.player=c.reference AND br.id<>ar.id;
CREATE VIEW IF NOT EXISTS feature_coverage AS
 SELECT c.suite_id, c.name case_name, j.value feature, r.player, r.mode,
 r.status FROM cases c, json_each(c.features_json) j
 LEFT JOIN runs r ON r.suite_id=c.suite_id AND r.case_name=c.name;
"""


def encode(value):
    return json.dumps(value, sort_keys=True, allow_nan=False)


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while block := stream.read(1024 * 1024):
            value.update(block)
    return value.hexdigest()


class Store:
    def __init__(self, path):
        self.path = Path(path).resolve()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(self.path)
        self.db.row_factory = sqlite3.Row
        self.db.execute('PRAGMA foreign_keys=ON')
        version = self.db.execute('PRAGMA user_version').fetchone()[0]
        if version not in (0, 1):
            self.db.close()
            raise ValueError('Unsupported playback database schema: ' + str(version))
        self.db.executescript(SCHEMA)
        self.db.execute('PRAGMA user_version=1')
        self.db.commit()

    def close(self):
        self.db.close()

    def insert(self, table, **fields):
        # Table/column identifiers are internal constants, never manifest text.
        cursor = self.db.execute('INSERT INTO ' + table + ' (' + ','.join(fields) +
                                 ') VALUES (' + ','.join('?' for _ in fields) + ')', tuple(fields.values()))
        self.db.commit()
        return cursor.lastrowid

    def relative(self, path):
        import os
        return Path(os.path.relpath(Path(path).resolve(), self.path.parent)).as_posix()

    def resolve(self, path):
        return (self.path.parent / path).resolve()

    def add_case(self, suite, case):
        checksum = digest(case['path'])
        self.db.execute('INSERT OR IGNORE INTO movies(sha256,path) VALUES (?,?)',
                        (checksum, str(case['path'])))
        movie = self.db.execute('SELECT id FROM movies WHERE sha256=?', (checksum,)).fetchone()[0]
        self.insert('cases', suite_id=suite, name=case['name'], movie_id=movie,
                    reference=case['reference'], features_json=encode(case['features']))
        return checksum

    def finish(self, run, status, *, error=None, provenance=None):
        self.db.execute('UPDATE runs SET status=?,error=?,provenance_json=? WHERE id=?',
                        (status, error, encode(provenance or {}), run))
        self.db.commit()

    def image(self, run, sequence, scene, frame, path, **observation):
        from PIL import Image
        with Image.open(path) as source:
            rgb = source.convert('RGB')
            width, height = rgb.size
            pixels = hashlib.sha256(rgb.tobytes()).hexdigest()
        self.insert('images', run_id=run, sequence=sequence, scene=scene, frame=frame,
                    timestamp_ns=observation.pop('timestamp_ns', None),
                    render_ns=observation.pop('render_ns', None), capture_ns=observation.pop('capture_ns', None),
                    width=width, height=height, pixel_sha256=pixels, artifact_sha256=digest(path),
                    path=self.relative(path), state_json=encode(observation))

    def trace(self, run, trace):
        for index, frame in enumerate(trace['frames']):
            self.db.execute('INSERT OR IGNORE INTO scenes VALUES (?,?,?,?)',
                            (run, frame['scene'], frame['first'], frame['last']))
            self.insert('frames', run_id=run, sequence=index, scene=frame['scene'], frame=frame['frame'],
                        timestamp_ns=frame['monotonic_ns'], state_json=encode(frame))
        for index, event in enumerate(trace.get('sounds', [])):
            self.insert('sound_events', run_id=run, sequence=index, scene=event['scene'], frame=event['frame'],
                        timestamp_ns=event.get('monotonic_ns'), event_json=encode(event))


def frame_coverage(scenes, frames):
    """Check the complete ordered sequence, not just last-frame arrival."""
    expected = [(scene['scene'], frame) for scene in scenes
                for frame in range(scene['first_frame'], scene['last_frame'] + 1)]
    actual = [(frame['scene'], frame['frame']) for frame in frames]
    return bool(expected) and actual == expected


def timing_metrics(left, right, tolerance_ns):
    if not left or not right:
        return {'status': 'inconclusive', 'reason': 'missing frame trace'}
    if [(f['scene'], f['frame']) for f in left] != [(f['scene'], f['frame']) for f in right]:
        return {'status': 'different', 'reason': 'frame sequence differs'}
    a0, b0 = left[0]['timestamp_ns'], right[0]['timestamp_ns']
    if any(f['timestamp_ns'] is None for f in (*left, *right)):
        return {'status': 'inconclusive', 'reason': 'timestamps unavailable'}
    if any(b['timestamp_ns'] < a['timestamp_ns'] for frames in (left, right) for a, b in zip(frames, frames[1:])):
        return {'status': 'inconclusive', 'reason': 'nonmonotonic timestamps'}
    delta = [(b['timestamp_ns'] - b0) - (a['timestamp_ns'] - a0) for a, b in zip(left, right)]
    ordered = sorted(abs(value) for value in delta)
    mismatches = [i for i, value in enumerate(delta) if abs(value) > tolerance_ns]
    return {'status': 'different' if mismatches else 'matched',
            'max_difference_ns': ordered[-1], 'p95_difference_ns': ordered[int((len(ordered) - 1) * .95)],
            'end_drift_ns': delta[-1], 'different_frames': len(mismatches),
            'first_different_sequence': mismatches[0] if mismatches else None}
