"""Prepare ISO-only retail resources and manage an isolated oracle child."""
import base64
import ctypes
from ctypes import wintypes as w
import hashlib
import io
import json
import os
import pathlib
import shutil
import subprocess
import time
import winreg
from PIL import Image
from client import RetailOracle, OracleError, EXE_SHA256


def digest(path):
    value = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        while block := stream.read(1024 * 1024):
            value.update(block)
    return value.hexdigest()


def prepare_iso(iso, destination):
    """Extract developer test resources, never modify the ISO or installed 3DMM."""
    iso, destination = pathlib.Path(iso).resolve(), pathlib.Path(destination).resolve()
    product = destination / 'Microsoft Kids/3D Movie Maker'
    manifest_path = destination / 'iso-manifest.json'
    source_hash = digest(iso)
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        if manifest['iso_sha256'] == source_hash and all(
                (destination / path).is_file() and digest(destination / path) == value
                for path, value in manifest['files'].items()):
            return product, manifest
    extractor = shutil.which('7z')
    if not extractor:
        raise OracleError('7-Zip is required to prepare the retail test package')
    product.mkdir(parents=True, exist_ok=True)
    with (destination / 'extract.log').open('w', encoding='utf-8') as log:
        subprocess.run([extractor, 'e', str(iso), '3DMOVIE/*', '-o' + str(product), '-y'],
                       stdout=log, stderr=log, check=True, timeout=120)
    executable = product / '3DMOVIE.EXE'
    if digest(executable) != EXE_SHA256:
        raise OracleError('This ISO does not contain the supported retail EXE')
    for user in ('McZee', 'Melanie', 'Oracle'):
        (destination / 'Microsoft Kids/Users' / user).mkdir(parents=True, exist_ok=True)
    manifest = {'iso': str(iso), 'iso_sha256': source_hash, 'exe_sha256': EXE_SHA256,
                'files': {str(file.relative_to(destination)): digest(file) for file in product.iterdir() if file.is_file()}}
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    return product, manifest


def delete_private_registry(pid):
    # The bridge writes only its own application preferences below this key.
    name = r'Software\3DMMForever\RetailOracle\Run-' + str(int(pid))
    delete_registry_tree(name)


def delete_registry_tree(name):
    if not name.startswith('Software\\3DMMForever\\RetailOracle\\') or name.endswith('\\'):
        raise OracleError('Invalid private registry path')
    advapi = ctypes.WinDLL('advapi32', use_last_error=True)
    advapi.RegDeleteTreeW.argtypes = [w.HKEY, w.LPCWSTR]
    result = advapi.RegDeleteTreeW(w.HKEY(int(winreg.HKEY_CURRENT_USER)), name)
    if result not in (0, 2, 3):
        raise OracleError('Cannot remove private preferences: Windows error ' + str(result))


class RetailSession:
    def __init__(self, build, product, source, artifacts, timeout=30):
        self.build, self.product = pathlib.Path(build).resolve(), pathlib.Path(product).resolve()
        self.source, self.artifacts = pathlib.Path(source).resolve(), pathlib.Path(artifacts).resolve()
        self.artifacts.mkdir(parents=True, exist_ok=True)
        self.source_hash = digest(self.source)
        self.movie = self.artifacts / self.source.name
        shutil.copy2(self.source, self.movie)
        self.oracle, self.process = None, None
        self.verified = False
        self.closed = False
        self.sequence = 0
        self.k = ctypes.WinDLL('kernel32', use_last_error=True)
        self.k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.k.OpenProcess.restype = w.HANDLE
        self.k.CloseHandle.argtypes = [w.HANDLE]
        self.k.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
        self.k.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
        self.k.GetProcessTimes.argtypes = [w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4
        try:
            launch = subprocess.run([str(self.build / 'retail-oracle-launch.exe'), str(self.product / '3DMOVIE.EXE'),
                str(self.build / 'retail-oracle.dll'), str(self.product.parent), str(self.movie)],
                capture_output=True, text=True, timeout=50, creationflags=subprocess.CREATE_NO_WINDOW)
            (self.artifacts / 'bootstrap.log').write_text(launch.stderr, encoding='utf-8')
            if launch.returncode:
                raise OracleError('Retail launcher failed: ' + launch.stderr)
            self.identity = json.loads(launch.stdout)
            (self.artifacts / 'process.json').write_text(json.dumps(self.identity, indent=2))
            self.process = self.k.OpenProcess(0x100000 | 0x1000 | 1, False, self.identity['pid'])
            if not self.process:
                raise OracleError('Cannot open owned retail test process')
            times = [w.FILETIME() for _ in range(4)]
            if not self.k.GetProcessTimes(self.process, *(ctypes.byref(item) for item in times)):
                raise OracleError('Cannot verify retail process identity')
            created = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            if created != self.identity['created']:
                raise OracleError('Retail process identity changed')
            self.verified = True
            self.oracle = RetailOracle(self.identity['pid'], timeout)
            deadline = time.monotonic() + timeout
            while True:
                try:
                    self.oracle.state()
                    break
                except OracleError as error:
                    if str(error) not in ('native_object_not_ready', 'scene_not_ready') or time.monotonic() > deadline:
                        raise
                    time.sleep(.1)
            self.validate_assets()
        except BaseException:
            self.close()
            raise

    def validate_assets(self):
        identity = self.oracle.hello()
        expected = self.product.parent
        if not identity['registry_redirects']:
            raise OracleError('Retail resource-path redirection was not exercised')
        for value in identity['opened_assets']:
            path = pathlib.Path(value).resolve()
            if not path.is_relative_to(expected):
                raise OracleError('Retail read assets outside the ISO test package: ' + str(path))
        (self.artifacts / 'provenance.json').write_text(json.dumps(identity, indent=2), encoding='utf-8')
        return identity

    def state(self):
        return self.oracle.state()

    def seek(self, scene, frame):
        return self.oracle.seek(scene, frame)

    def capture(self, label):
        bitmap = self.artifacts / (label + '.bmp')
        self.oracle.capture(bitmap)
        with Image.open(bitmap) as captured:
            image = captured.convert('RGB')
        image.save(bitmap.with_suffix('.png'))
        bitmap.unlink()
        return image

    def call(self, name, **arguments):
        if name == 'get_state':
            result = self.state()
        elif name == 'seek_movie':
            result = self.seek(arguments['scene'], arguments['frame'])
        elif name == 'send_command':
            if arguments.get('hid'):
                raise OracleError('Targeted HID dispatch is not implemented by this profile')
            result = self.oracle.command(arguments['cid'], *(arguments.get('lw' + str(i), 0) for i in range(4)))
        elif name == 'wait_ms':
            time.sleep(max(0, min(30000, arguments['ms'])) / 1000)
            result = {'waited_ms': arguments['ms']}
        elif name == 'screenshot':
            self.sequence += 1
            image = self.capture('capture-%04d' % self.sequence)
            stream = io.BytesIO()
            image.save(stream, format='PNG')
            return {'content': [{'type': 'image', 'mimeType': 'image/png',
                                 'data': base64.b64encode(stream.getvalue()).decode('ascii')}]}
        else:
            raise OracleError('Unsupported retail tool: ' + name)
        return {'content': [{'type': 'text', 'text': json.dumps(result)}]}

    def close(self):
        if self.closed:
            return
        self.closed = True
        if self.oracle:
            self.oracle.close()
        if self.process:
            # End only this identity-checked child. No normal quit/save dialogs
            # are claimed as tested by this teardown operation.
            if self.verified and self.k.WaitForSingleObject(self.process, 0) != 0:
                self.k.TerminateProcess(self.process, 0)
                self.k.WaitForSingleObject(self.process, 5000)
            self.k.CloseHandle(self.process)
            self.process = None
            if self.verified:
                delete_private_registry(self.identity['pid'])
        if digest(self.source) != self.source_hash:
            raise OracleError('The source movie changed during the test')
