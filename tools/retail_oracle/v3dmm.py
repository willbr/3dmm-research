"""Independent, private handoff to the original v3dmm 0.14 VFS/PhysicsFS runtime.

This does not use the port's container parser, materializer or resource resolver.
Only the shipped US patch is applied, to a private copy of the pinned ISO EXE.
"""
import hashlib
import json
import pathlib
import shutil
import struct
import uuid
import xml.etree.ElementTree as ET
import zipfile
from client import OracleError, EXE_SHA256, V3DMM_SHA256
from session import RetailSession, digest

PATCH_SHA256 = '3962916a4d9ecd9f74f8d5405070998c3d8cc0836732f96b5bd93daf5154284d'
RUNTIME = {
    'VFS.dll': 'd059a20fb4a50e15f734dfa75607c4d423f10003492d29fa262e683f55c38797',
    'physfs.dll': '6e2baf2eddd5a9938be50cf3be420ab26e849905d1c7849fd01a8fd3883c3417',
    'msvcr71.dll': '8094af5ee310714caebccaeee7769ffb08048503ba478b879edfef5f1a24fefe',
    'zlib1.dll': '8aaf9e851021d1afab9e2b93c9e796d37dd31c114a76476e6c00bd8931ee9446',
    'libpng13.dll': 'bcc18faee81a2362b2bebc58208ff23e8a9641dd5843dd6692c19f942e0259bc',
    'libcurl.dll': 'de7a71d1f50b80e11bb98b174459f787312e2c9d7cedbb8fc358472a6defc4b4',
}
LIBRARIES = {
    'nickelodeon': ('nickelodeon', 3, 'N3DMMExp', 'N3DMM EXPANSION/N3DMMExp',
        'NICKACTOR.3TH NICKBKGDS.3CN NICKBKGDS.3TH NICKMTRL.3TH NICKMTRLS.3CN NICKPROP.3TH NICKSNDS.3CN NICKSOUND.3TH NICKTDFS.3CN NICKTMPLS.3CN'),
    'doraemon': ('doraemon', 4, 'DORAEMON', 'Doraemon Character Kit/DORAEMON',
        'DORAACTR.3TH DORABKGD.3CN DORABKGD.3TH DORAPROP.3CN DORAPROP.3TH DORASNDS.3CN DORASNDS.3TH DORATMPL.3CN'),
    '3dmm expansion pack': ('frankie', 5, 'EXPANSIONS', 'Expansions/EXPANSIONS',
        '3DFONTPACK1.3CN 3DFONTPACK1.3TH MTRLPACK1-2.3CN MTRLPACK1.3CN MTRLPACK1.3TH MTRLPACK1old.3CN SCENEPACK1.3CN SCENEPACK1.3TH SOLIDMTRLPACK.3CN SOLIDMTRLPACK.3TH SOUNDPACK1.3CN SOUNDPACK1.3TH TMPLPACK1.3CN TMPLPACK1actors.3TH WACKYDINGS.3CN WACKYDINGS.3TH'),
}


def patch_executable(source, patch, destination):
    if digest(source) != EXE_SHA256 or digest(patch) != PATCH_SHA256:
        raise OracleError('Unsupported retail executable or v3dmm patch')
    data = bytearray(pathlib.Path(source).read_bytes())
    changes = next(node for node in ET.parse(patch).getroot() if node.get('name') == '3dmm-US')
    for node in changes:
        offset, length = int(node.get('offset')), int(node.get('length'))
        before, after = bytes.fromhex(node.get('original')), bytes.fromhex(node.get('changed'))
        if len(before) != length or len(after) != length or data[offset:offset + length] != before:
            raise OracleError('Original patch preimage mismatch at ' + str(offset))
        data[offset:offset + length] = after
    if hashlib.sha256(data).hexdigest() != V3DMM_SHA256:
        raise OracleError('Unexpected patched executable identity')
    with pathlib.Path(destination).open('xb') as stream:
        stream.write(data)


def unpack_bundle(source, directory):
    """Copy ZIP windows unchanged and the inner movie; never unpack asset names."""
    directory = pathlib.Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    total = pathlib.Path(source).stat().st_size
    records = []
    with pathlib.Path(source).open('rb') as stream:
        def read(count):
            if count < 0 or count > total - stream.tell():
                raise OracleError('Truncated VMM')
            value = stream.read(count)
            if len(value) != count:
                raise OracleError('Truncated VMM')
            return value
        if read(6) != b'v3dmm\x01':
            raise OracleError('Only original v1 VMM containers are supported')
        count, = struct.unpack('<I', read(4))
        if not 0 < count <= 10000:
            raise OracleError('Invalid VMM dependency count')
        for _ in range(count):
            size, length = struct.unpack('<IH', read(6))
            raw = read(length)
            if not raw or b'\0' in raw:
                raise OracleError('Invalid VMM dependency name')
            records.append({'name': raw.decode('cp1252'), 'name_hex': raw.hex(), 'size': size})
        for index, record in enumerate(records):
            if not record['size']:
                continue
            if read(2) != b'\xf0\x0b':
                raise OracleError('Invalid expansion start marker')
            path = directory / ('%04d.vxp' % index)
            with path.open('xb') as target:
                remaining = record['size']
                while remaining:
                    block = read(min(remaining, 1024 * 1024))
                    target.write(block)
                    remaining -= len(block)
            if read(2) != b'\xfe\xeb':
                raise OracleError('Invalid expansion end marker')
            # The reference mounts the ZIP itself using original PhysicsFS.
            # The Python read is limited to metadata, not decoded asset content.
            with zipfile.ZipFile(path, metadata_encoding='cp1252') as archive:
                configs = [item for item in archive.infolist() if item.filename.lower().endswith('.cfg')]
                if len(configs) != 1 or configs[0].file_size > 1024 * 1024:
                    raise OracleError('Unsupported VXP configuration')
                fields = {}
                for line in archive.read(configs[0]).decode('cp1252').splitlines():
                    if '=' in line:
                        key, value = line.split('=', 1)
                        fields[key.strip().lower()] = value.strip()
                if fields.get('type', '').lower() != 'portable' or fields.get('name') != record['name']:
                    raise OracleError('Unsupported or mismatched embedded expansion: ' + record['name'])
            record.update(archive=str(path), sha256=digest(path))
        offset = stream.tell()
        if read(4) != b'CHN2':
            raise OracleError('VMM has no chunky movie')
        movie = directory / 'movie.3mm'
        with movie.open('xb') as target:
            target.write(b'CHN2')
            shutil.copyfileobj(stream, target, 1024 * 1024)
    return records, movie, offset


class V3dmmSession(RetailSession):
    def __init__(self, build, product, bundle, artifacts, distribution, libraries=None):
        artifacts = pathlib.Path(artifacts).resolve()
        artifacts.mkdir(parents=True, exist_ok=False)
        self.frozen = {}
        self.bundle_source = pathlib.Path(bundle).resolve()
        self.bundle_hash = digest(bundle)
        self.bundle = artifacts / 'original.vmm'
        shutil.copyfile(bundle, self.bundle)
        self.frozen[str(self.bundle)] = self.bundle_hash
        system = artifacts / 'system'
        system.mkdir()
        distribution = pathlib.Path(distribution).resolve()
        for name, expected in RUNTIME.items():
            source = distribution / 'System' / name
            if digest(source) != expected:
                raise OracleError('Unsupported original v3dmm DLL: ' + name)
            target = system / name
            shutil.copyfile(source, target)
            self.frozen[str(target)] = expected
        self.executable = system / 'v3dmm.exe'
        patch_executable(product / '3DMOVIE.EXE', distribution / 'Patches/v3dmm.xpatch', self.executable)
        self.frozen[str(self.executable)] = V3DMM_SHA256
        records, inner, offset = unpack_bundle(self.bundle, artifacts / 'dependencies')
        users = artifacts / 'users'
        home = users / 'Oracle'
        home.mkdir(parents=True)
        for name in ('McZee', 'Melanie'):
            (users / name).mkdir()
        movie = home / 'movie.3mm'
        shutil.copyfile(inner, movie)
        self.frozen[str(movie)] = digest(movie)
        temporary = artifacts / 'temp'
        temporary.mkdir()
        self.pack_root = artifacts / 'packs'
        self.pack_settings = {entry[0]: {'generation': '', 'enabled': False, 'remove': False, 'source': entry[1]}
                              for entry in LIBRARIES.values()}
        root = ET.Element('environment')
        ET.SubElement(root, 'setup', userdir=str(users), tempdir=str(temporary), homedir=str(home),
            fullscreen='off', hires='on', cmdline='"' + str(self.executable) + '" -m "' + str(self.bundle) + '"')
        self.mounts = [users, pathlib.Path(product).resolve()]
        def mount(name, archive, folder, sid, title, kind='library'):
            element = ET.SubElement(root, 'expansion', type=kind)
            for key, value in {'name': name, 'archive': str(archive), 'mount': folder,
                               'regid': str(sid), 'regname': title}.items():
                ET.SubElement(element, key).text = value
        mount('3dmm', product, '3DMovie', 2, '3D Movie Maker/3DMovie')
        seen = {'3dmm'}
        for record in records:
            name = record['name']
            if name.lower() in seen:
                continue
            seen.add(name.lower())
            if record['size']:
                path = pathlib.Path(record['archive'])
                self.frozen[str(path)] = record['sha256']
                mount(name, path, 'v3dmm', 6, 'Virtual3dmm/v3dmm', 'portable')
            else:
                if name.lower() not in LIBRARIES or name.lower() not in (libraries or {}):
                    raise OracleError('Missing external library; supply --library NAME=FOLDER: ' + name)
                key, sid, folder, title, names = LIBRARIES[name.lower()]
                generation = 'g-' + uuid.uuid4().hex
                path = self.pack_root / key / generation
                path.mkdir(parents=True)
                source = pathlib.Path(libraries[name.lower()]).resolve()
                contents = {item.name.lower(): item for item in source.iterdir() if item.is_file()}
                files = []
                for filename in names.split():
                    original = contents.get(filename.lower())
                    if not original:
                        raise OracleError('External library file missing: ' + filename)
                    destination = path / filename
                    shutil.copyfile(original, destination)
                    value = digest(destination)
                    self.frozen[str(destination)] = value
                    files.append({'name': filename, 'size': destination.stat().st_size, 'sha256': value})
                manifest = path / 'pack.json'
                manifest.write_text(json.dumps({'schema': '3dmm-managed-pack-1', 'pack': key, 'files': files}))
                self.frozen[str(manifest)] = digest(manifest)
                self.pack_settings[key] = {'generation': generation, 'enabled': True, 'remove': False, 'source': sid}
                mount(name, path, folder, sid, title)
            self.mounts.append(path)
        self.environment = artifacts / 'environment.xml'
        ET.ElementTree(root).write(self.environment, encoding='us-ascii', xml_declaration=True)
        self.frozen[str(self.environment)] = digest(self.environment)
        self.manifest = {'profile': 'v3dmm-014-us', 'bundle_sha256': self.bundle_hash, 'movie_offset': offset,
            'dependencies': records, 'files': self.frozen, 'search_paths': [str(path) for path in self.mounts]}
        (artifacts / 'manifest.json').write_text(json.dumps(self.manifest, indent=2))
        super().__init__(build, product, movie, artifacts / 'bridge', executable=self.executable,
                         environment=self.environment, expected_hash=V3DMM_SHA256)

    def validate_assets(self):
        identity = self.oracle.hello()
        if identity.get('backend') != 'v3dmm-014-us':
            raise OracleError('The reference is not running the original v3dmm profile')
        actual = [pathlib.Path(item).resolve() for item in identity['search_paths']]
        if actual != self.mounts:
            raise OracleError('Original PhysicsFS mount order differs: ' + repr(actual))
        system = self.executable.parent
        for name in RUNTIME:
            loaded = identity['runtime_modules'][name]
            if not loaded and name in ('VFS.dll', 'physfs.dll'):
                raise OracleError('Required original runtime DLL is not loaded: ' + name)
            if loaded and pathlib.Path(loaded).resolve() != system / name:
                raise OracleError('Reference loaded a DLL outside its private runtime: ' + name)
        self.validate_frozen()
        (self.artifacts / 'provenance.json').write_text(json.dumps(identity, indent=2))
        return identity

    def validate_frozen(self):
        for path, expected in self.frozen.items():
            if digest(path) != expected:
                raise OracleError('Frozen original v3dmm input changed: ' + path)
        if digest(self.bundle_source) != self.bundle_hash:
            raise OracleError('Source VMM changed')

    def relaunch(self, artifacts):
        clone = object.__new__(V3dmmSession)
        for name in ('frozen', 'bundle_source', 'bundle_hash', 'bundle', 'executable', 'environment', 'mounts',
                     'pack_root', 'pack_settings', 'manifest'):
            setattr(clone, name, getattr(self, name))
        RetailSession.__init__(clone, self.build, self.product, self.source, artifacts,
            executable=self.executable, environment=self.environment, expected_hash=V3DMM_SHA256)
        return clone
