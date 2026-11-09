"""Copy explicit base/expansion archives into a private legacy content folder.

Some older movies reference expansion assets as source 2 (3D Movie Maker).
Their original setup placed additional .3cn/.3th archives in the base folder.
This prepares that layout for Marcus's --content-dir without changing sources,
movie references, installed packs or resolver precedence. Original content must
already be available locally; nothing is downloaded or installed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

CONTENT_TYPES = {'.3cn', '.3th'}
BASE_FILES = {'tmpls.3cn', 'bkgds.3cn', 'mtrls.3cn', 'tdfs.3cn'}


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def prepare(base, overlays, output):
    base, output = base.resolve(), output.resolve()
    folders = [base, *(path.resolve() for path in overlays)]
    if output.exists():
        raise ValueError('Output must be a new folder: ' + str(output))
    planned = {}
    for index, folder in enumerate(folders):
        if not folder.is_dir():
            raise ValueError('Content folder does not exist: ' + str(folder))
        files = sorted(path for path in folder.iterdir()
                       if path.is_file() and path.suffix.lower() in CONTENT_TYPES)
        if index == 0 and not BASE_FILES <= {path.name.lower() for path in files}:
            raise ValueError('Base folder needs tmpls.3cn, bkgds.3cn, mtrls.3cn and tdfs.3cn')
        if not files:
            raise ValueError('No content archives in: ' + str(folder))
        for path in files:
            key = path.name.casefold()
            if key in planned:
                raise ValueError('Colliding archive names: ' + str(planned[key]) + ' and ' + str(path))
            planned[key] = path
    # All names are checked before creating anything. Never replace an existing
    # destination or remove files from a failed/partial copy.
    output.mkdir(parents=True)
    rows = []
    for source in planned.values():
        checksum = digest(source)
        destination = output / source.name
        with source.open('rb') as incoming, destination.open('xb') as outgoing:
            shutil.copyfileobj(incoming, outgoing)
        if digest(destination) != checksum or digest(source) != checksum:
            raise ValueError('Content changed while copying: ' + str(source))
        rows.append({'source': str(source), 'name': source.name, 'sha256': checksum})
    manifest = {'base': str(base), 'overlays': [str(path) for path in folders[1:]], 'files': rows,
                'purpose': 'Expansion archives exposed in the base-content namespace; original sources unchanged'}
    with (output / 'legacy-content.json').open('x', encoding='utf-8') as stream:
        json.dump(manifest, stream, indent=2)
        stream.write('\n')
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--overlay', type=Path, action='append', required=True,
                        help='Prepared expansion folder; repeat for additional explicitly selected libraries')
    args = parser.parse_args()
    try:
        count = prepare(args.base, args.overlay, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')
    print(f'Prepared {count} content archives in {args.output.resolve()}')
    print('Use this folder with Marcus --content-dir; keep your usual --resources file.')


if __name__ == '__main__':
    main()
