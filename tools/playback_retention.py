"""Bound raw playback capture storage while retaining observations and metadata."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil

SUITE = re.compile(r'\d{8}T\d{6}Z-[0-9a-f]{8}')
METADATA = {'.json', '.jsonl', '.log', '.csv'}


def shared_cache(output, explicit=None):
    return Path(explicit).resolve() if explicit else Path(output).resolve() / 'sdl-cache'


def require_space(output, minimum_mib=4096):
    if minimum_mib < 0:
        raise ValueError('Free-space limit cannot be negative')
    available = shutil.disk_usage(output).free
    if available < minimum_mib * 1024 * 1024:
        raise RuntimeError('Insufficient capture space: %d MiB free, %d MiB required; prune superseded captures first' %
                           (available // (1024 * 1024), minimum_mib))


def plan(store, keep=2, pins=(), cache=None):
    if keep < 1:
        raise ValueError('Keep at least one completed capture per observation key')
    root = store.path.parent.resolve()
    protected = set(pins)
    pin_file = root / 'retention-pins.json'
    if pin_file.is_file():
        protected.update(json.loads(pin_file.read_text(encoding='utf-8-sig')))
    if cache:
        try:
            first = Path(cache).resolve().relative_to(root).parts[0]
            if SUITE.fullmatch(first):
                protected.add(first)
        except (ValueError, IndexError):
            pass
    if any(not SUITE.fullmatch(name) for name in protected):
        raise ValueError('Invalid pinned suite identifier')
    partition = 'case_name,player,mode,preparation,repetition'
    protected.update(row[0] for row in store.db.execute(f'''SELECT suite_id FROM (
        SELECT r.suite_id,row_number() OVER(PARTITION BY {partition}
        ORDER BY s.started DESC,r.id DESC) rank FROM runs r JOIN suites s ON s.id=r.suite_id
        WHERE r.status='complete') WHERE rank<=?''', (keep,)))
    protected.update(row[0] for row in store.db.execute(f'''SELECT suite_id FROM (
        SELECT r.suite_id,row_number() OVER(PARTITION BY {partition}
        ORDER BY s.started DESC,r.id DESC) rank FROM runs r JOIN suites s ON s.id=r.suite_id
        WHERE r.status NOT IN ('complete','running','unsupported')) WHERE rank=1'''))
    protected.update(row[0] for row in store.db.execute("SELECT DISTINCT suite_id FROM runs WHERE status='running'"))
    protected.update(row[0] for row in store.db.execute('''SELECT id FROM suites s
        WHERE NOT EXISTS(SELECT 1 FROM runs r WHERE r.suite_id=s.id)'''))
    known = {row[0] for row in store.db.execute('SELECT id FROM suites')}
    latest = store.db.execute('SELECT id FROM suites ORDER BY started DESC LIMIT 1').fetchone()
    if latest:
        protected.add(latest[0])
    targets = [path.name for path in root.iterdir() if SUITE.fullmatch(path.name)
               and path.name in known - protected and path.is_dir()]
    return {'root': str(root), 'keep_completed': keep, 'protected': sorted(protected), 'targets': sorted(targets)}


def _directory(path, root):
    if (path.is_symlink() or getattr(path, 'is_junction', lambda: False)() or
            path.resolve().parent != root or not SUITE.fullmatch(path.name)):
        raise ValueError('Unsafe capture directory: ' + str(path))
    for current, directories, _ in os.walk(path, followlinks=False):
        for name in directories:
            child = Path(current) / name
            if (child.is_symlink() or getattr(child, 'is_junction', lambda: False)() or
                    not child.resolve().is_relative_to(path.resolve())):
                raise ValueError('Capture contains a directory link: ' + str(child))


def prune(store, keep=2, pins=(), cache=None, apply=False):
    result = plan(store, keep, pins, cache)
    if not apply:
        return result
    if store.db.execute("SELECT 1 FROM runs WHERE status='running' LIMIT 1").fetchone():
        raise RuntimeError('Cannot prune while captures are running')
    root = store.path.parent.resolve()
    for name in result['targets']:
        _directory(root / name, root)
    archive = root / 'retained-metadata'
    if archive.is_symlink() or getattr(archive, 'is_junction', lambda: False)():
        raise ValueError('Metadata archive must not be a directory link')
    archive.mkdir(exist_ok=True)
    journal_path = root / 'pruned-suites.json'
    journal = json.loads(journal_path.read_text(encoding='utf-8-sig')) if journal_path.is_file() else {}
    removed = set(journal.get('removed', []))
    for name in result['targets']:
        source = root / name
        for current, directories, files in os.walk(source, followlinks=False):
            directories[:] = [part for part in directories if part.lower() != 'cache']
            for filename in files:
                path = Path(current) / filename
                if path.suffix.lower() in METADATA and not path.is_symlink():
                    target = archive / name / path.relative_to(source)
                    # Existing archive paths are also untrusted: never copy
                    # metadata through a link outside this output directory.
                    if not target.resolve().is_relative_to(archive.resolve()):
                        raise ValueError('Metadata target escapes archive')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, target)
        shutil.rmtree(source)
        removed.add(name)
        journal.update(removed=sorted(removed), metadata='retained-metadata',
                       keep_completed=keep, protected=result['protected'],
                       timestamp=datetime.now(timezone.utc).isoformat())
        temporary = journal_path.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(journal, indent=2), encoding='utf-8')
        temporary.replace(journal_path)
    result['removed'] = result['targets']
    return result
