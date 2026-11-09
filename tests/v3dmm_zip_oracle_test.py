"""Compare duplicate VXP resolution against v3dmm 0.14's original PhysicsFS DLL.

No UI or installed content is used. All archives and extracted bytes live in a
fresh directory below --output. Requires the x86 physfs-zip-probe target.
"""
import argparse
import hashlib
from pathlib import Path
import random
import subprocess
import tempfile
import warnings
import zipfile

PHYSFS_SHA256 = '6e2baf2eddd5a9938be50cf3be420ab26e849905d1c7849fd01a8fd3883c3417'


def original(args, archive, names):
    result = subprocess.run([str(args.probe.resolve()), str(args.dll.resolve()), str(archive), *names],
                            check=True, capture_output=True, text=True)
    assert result.stdout.splitlines()[0] == 'PhysicsFS 1.0.60', result.stdout
    return {name: None if value == ' missing' else bytes.fromhex(value)
            for name, value in (line.split(':', 1) for line in result.stdout.splitlines()[1:])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert hashlib.sha256(args.dll.read_bytes()).hexdigest() == PHYSFS_SHA256, 'Wrong reference DLL'
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='run-', dir=args.output)).resolve()
    rng = random.Random(314159)
    checks = 0
    for count in (1, 2, 3, 4, 5, 6, 8, 12, 31, 64):
        for variation in range(6):
            entries = []
            for i in range(count):
                cfg = f'Name=Choice {i}\nAuthor=Oracle\nType=Portable\n'.encode()
                entries.extend([('fixture.cfg', cfg), ('fixture.3cn', f'content-{i}'.encode()),
                                ('fixture.3th', f'thumbnail-{i}'.encode())])
            # Padding participates in sorting/searching, even when not materialized.
            entries += [(f'{i:02d}.txt', b'padding') for i in range(variation)]
            if variation:
                rng.shuffle(entries)
            archive = root / f'duplicates-{count}-{variation}.vxp'
            with warnings.catch_warnings():
                warnings.simplefilter('ignore', UserWarning)
                with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as target:
                    for name, data in entries:
                        target.writestr(name, data)
            expected = original(args, archive, ['fixture.cfg', 'fixture.3cn', 'fixture.3th'])
            result = subprocess.run([str(args.native.resolve()), '--materialize', str(archive),
                                     str(root / f'extract-{count}-{variation}')],
                                    check=True, capture_output=True, text=True)
            actual = dict(line.split('\t', 1) for line in result.stdout.splitlines())
            expected_name = expected['fixture.cfg'].decode().splitlines()[0].split('=', 1)[1]
            assert actual.pop('Name') == expected_name, (archive, actual, expected)
            assert set(actual) == {'fixture.3cn', 'fixture.3th'}, (archive, actual)
            for name, path in actual.items():
                assert Path(path).read_bytes() == expected[name], (archive, name)
                checks += 1
    # Record that case-distinct names are separate in the reference. Our shared
    # VFS still rejects case collisions explicitly; this is not duplicate folding.
    archive = root / 'case.zip'
    with zipfile.ZipFile(archive, 'w') as target:
        target.writestr('same.txt', b'lower')
        target.writestr('SAME.TXT', b'upper')
    assert original(args, archive, ['same.txt', 'SAME.TXT', 'Same.txt']) == {
        'same.txt': b'lower', 'SAME.TXT': b'upper', 'Same.txt': None}
    print(f'Original PhysicsFS: 60 packages, {checks} content reads and CFG choices matched; artifacts: {root}')


if __name__ == '__main__':
    main()
