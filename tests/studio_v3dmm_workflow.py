"""Exercise Portable VXP activation, authoring, VMM save/reopen and package options.

Requires a local blank movie and a redistributable/user-supplied prop VXP.
Reference inputs are read only; all writes use a fresh private artifact folder.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import pathlib
import struct
import tempfile
import time
import uuid
import zipfile
from types import SimpleNamespace
from studio_movie_workflow import Studio
from studio_options_workflow import options, select_tab, dialog, click


def package_page(studio):
    current = select_tab(studio, options(studio), 1071)
    assert not any(item['id'] in (1096, 1100) for item in current['controls'])
    return select_tab(studio, current, 1095)


def package_list(studio, control, present):
    deadline = time.monotonic() + 10
    while True:
        current = dialog(studio, 'Options')
        value = next(item for item in current['controls'] if item['id'] == control)
        if bool(value['text']) == present:
            return current
        assert time.monotonic() < deadline, current
        time.sleep(.02)


def package_selection(studio, control, name):
    deadline = time.monotonic() + 10
    while True:
        current = dialog(studio, 'Options')
        if any(item['id'] == control and item['text'] == name for item in current['controls']):
            return current
        assert time.monotonic() < deadline, current
        time.sleep(.02)


def install_package(studio, current, path, name):
    click(studio, current, 1098)
    portfolio = dialog(studio, 'Install a V3DMM expansion')
    studio.call('sdl_dialog_action', generation=portfolio['generation'], action='text', control=11,
                text=str(path.resolve()))
    deadline = time.monotonic() + 10
    while not any(item['id'] == 11 and item['text'] == str(path.resolve())
                  for item in dialog(studio, 'Install a V3DMM expansion')['controls']):
        assert time.monotonic() < deadline
        time.sleep(.02)
    click(studio, portfolio, 1)
    return package_selection(studio, 1096, name)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def payload(path):
    data = path.read_bytes()
    if data[:4] == b'CHN2':
        return data
    assert data[:6] == b'v3dmm\x01'
    count, = struct.unpack_from('<I', data, 6)
    offset, lengths = 10, []
    for _ in range(count):
        length, name_length = struct.unpack_from('<IH', data, offset)
        lengths.append(length)
        offset += 6 + name_length
    for length in lengths:
        if length:
            assert data[offset:offset+2] == b'\xf0\x0b'
            offset += length + 4
            assert data[offset-2:offset] == b'\xfe\xeb'
    assert data[offset:offset+4] == b'CHN2'
    return data[offset:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'iso', 'blank', 'prop_vxp', 'artifacts'):
        parser.add_argument(name, type=pathlib.Path)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    out = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.artifacts.resolve()))
    originals = {path: digest(path) for path in (args.blank, args.prop_vxp)}
    movie = out / 'portable.vmm'
    with zipfile.ZipFile(args.prop_vxp) as archive:
        config = archive.read(next(name for name in archive.namelist() if name.lower().endswith('.cfg')))
    fields = dict(line.split(b'=', 1) for line in config.replace(b'\r', b'').splitlines() if b'=' in line)
    name = next(value for key, value in fields.items() if key.lower() == b'name')
    display_name = name.decode('cp1252')
    alias_name = name + b' (second)'
    alias = out / 'second.vxp'
    with zipfile.ZipFile(args.prop_vxp) as source, zipfile.ZipFile(alias, 'w') as destination:
        for member in source.infolist():
            content = source.read(member)
            if member.filename.lower().endswith('.cfg'):
                content = content.replace(b'Name=' + name, b'Name=' + alias_name)
                assert b'Name=' + alias_name in content
            destination.writestr(member, content)
    expansion = args.prop_vxp.read_bytes()
    # Independent fixture writer, using the documented v1 wire layout.
    movie.write_bytes(b'v3dmm\1' + struct.pack('<I', 2) + struct.pack('<IH', 0, 4) + b'3dmm' +
                     struct.pack('<IH', len(expansion), len(name)) + name + b'\xf0\x0b' + expansion +
                     b'\xfe\xeb' + payload(args.blank))
    plain = out / 'plain.3mm'
    plain.write_bytes(payload(args.blank))
    launch = SimpleNamespace(exe=args.exe, content=out, artifacts=out, response_timeout=120,
        launch_arguments=['--iso', str(args.iso.resolve())], environment={
            'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_BATCH_TEST': '1',
            'KAUAI_TEST_SETTINGS_RUN': str(uuid.uuid4()), 'THREEDMM_PACK_DIR': str(out / 'packs'),
            'SDL_AUDIODRIVER': 'dummy', 'SDL_VIDEODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software'})
    report = {}
    studio = Studio(launch, movie, 'author')
    try:
        state = studio.state()
        assert len(state['v3dmm_packages']) == 1
        assert any(item['source_id'] == 6 for item in state['thumbnail_files'])
        custom = []
        for kind in ('props', 'actors'):
            assets = json.loads(studio.call('list_assets', kind=kind)['content'][0]['text'])
            custom += [item for item in assets if item['sid'] == 6]
        assert custom, 'The supplied VXP must contain a prop or actor template'
        backgrounds = json.loads(studio.call('list_assets', kind='backgrounds')['content'][0]['text'])
        background = next(item for item in backgrounds if item['sid'] == 2)
        studio.call('create_scene', background_cno=background['cno'], sid=2)
        studio.call('create_actor', template_cno=custom[0]['cno'], sid=6, name='Portable actor')
        # Remove selection outlines before comparing the movie viewport.
        studio.call('key', vk=27)
        studio.call('wait_ms', ms=50)
        state = studio.state()
        assert any(actor['template_sid'] == 6 for actor in state['scene_actors'])
        scene = state['movie_scene']
        actors = state['scene_actors']
        before = studio.capture('before-save')
        saved = out / 'saved.vmm'
        studio.save(saved)
        report['sid_6_authoring_and_portfolio_save'] = True
        studio.quit()
    finally:
        studio.close()
    # ZIP bytes survive packaging unchanged, independently of native parsing.
    data = saved.read_bytes()
    assert expansion in data and data.startswith(b'v3dmm\1')
    # Installed packages must also work in an ordinary movie. Stock output
    # stays chunky; adding a source-6 actor switches the required save format.
    studio = Studio(launch, plain, 'plain-author')
    try:
        studio.call('create_scene', background_cno=background['cno'], sid=2)
        stock = out / 'stock.3mm'
        studio.save(stock)
        assert stock.read_bytes().startswith(b'CHN2')
        studio.call('create_actor', template_cno=custom[0]['cno'], sid=6, name='Installed portable actor')
        portable = out / 'new-portable.vmm'
        studio.save(portable)
        assert portable.read_bytes().startswith(b'v3dmm\1') and expansion in portable.read_bytes()
        report['ordinary_movie_save_and_installed_package_authoring'] = True
        studio.quit()
    finally:
        studio.close()
    studio = Studio(launch, saved, 'reopen')
    try:
        state = studio.state()
        if state['movie_scene'] != scene:
            studio.call('select_scene', scene=scene + 1)
            state = studio.state()
        assert state['scene_actors'] == actors
        assert studio.capture('after-reopen') == before
        report['fresh_process_state_and_pixels_match'] = True
        manager = package_page(studio)
        controls = {item['id']: item for item in manager['controls']}
        assert controls[1096]['text'] and controls[1100]['text']
        assert not controls[1097]['enabled'] and controls[1101]['enabled']
        click(studio, manager, 1101)
        manager = package_list(studio, 1100, False)
        assert next(item for item in manager['controls'] if item['id'] == 1096)['text']
        assert studio.state()['v3dmm_packages'][0]['enabled'], 'Removing from a movie must not disable its installation'
        click(studio, manager, 1097)
        manager = package_list(studio, 1100, True)
        studio.call('sdl_dialog_action', generation=manager['generation'], action='screenshot', text=str(out / 'v3dmm-tab.bmp'))
        click(studio, dialog(studio, 'Options'), 2)
        assert studio.state()['v3dmm_packages'][0]['applied']
        studio.save(out / 'reapplied.vmm')
        report['two_list_movie_package_management'] = True
        studio.quit()
    finally:
        studio.close()
    studio = Studio(launch, args.blank, 'separate-movie')
    try:
        state = studio.state()
        assert state['v3dmm_packages'][0]['enabled'] and not state['v3dmm_packages'][0]['applied']
        assert not any(item['source_id'] == 6 for item in state['thumbnail_files'])
        report['vmm_uses_its_own_applied_set'] = True
        studio.quit()
    finally:
        studio.close()
    # A standalone import uses the same package store through the real file
    # portfolio nested directly inside the V3DMM Options tab.
    launch.environment['THREEDMM_PACK_DIR'] = str(out / 'standalone-packs')
    studio = Studio(launch, plain, 'standalone-import')
    try:
        assert not studio.state()['v3dmm_packages']
        current = package_page(studio)
        manager = install_package(studio, current, args.prop_vxp, display_name)
        assert not next(item for item in manager['controls'] if item['id'] == 1100)['text']
        click(studio, manager, 1097)
        manager = package_list(studio, 1100, True)
        manager = install_package(studio, manager, alias, alias_name.decode('cp1252'))
        click(studio, manager, 1097)
        manager = package_selection(studio, 1100, alias_name.decode('cp1252'))
        # Actual mouse selection of the first visible right-hand row.
        click(studio, manager, 0x10000000 + (1100 << 16))
        manager = package_selection(studio, 1100, display_name)
        click(studio, manager, 1101)
        manager = package_selection(studio, 1100, alias_name.decode('cp1252'))
        assert [item['applied'] for item in studio.state()['v3dmm_packages']] == [False, True]
        studio.call('sdl_dialog_action', generation=manager['generation'], action='screenshot',
                    text=str(out / 'two-packages.bmp'))
        click(studio, manager, 1106)
        manager = package_list(studio, 1100, False)
        click(studio, manager, 0x10000000 + (1096 << 16))
        manager = package_selection(studio, 1096, display_name)
        click(studio, manager, 1097)
        manager = package_selection(studio, 1100, display_name)
        click(studio, dialog(studio, 'Options'), 2)
        assert studio.state()['v3dmm_packages'][0]['applied']
        studio.call('create_scene', background_cno=background['cno'], sid=2)
        studio.save(out / 'applied-unused.vmm')
        assert expansion in (out / 'applied-unused.vmm').read_bytes()
        assert alias.read_bytes() not in (out / 'applied-unused.vmm').read_bytes()
        manager = package_page(studio)
        click(studio, manager, 1106)
        manager = package_list(studio, 1100, False)
        click(studio, manager, 1105)
        manager = package_list(studio, 1100, True)
        assert all(item['applied'] for item in studio.state()['v3dmm_packages'])
        click(studio, manager, 1106)
        manager = package_list(studio, 1100, False)
        click(studio, manager, 1097)
        manager = package_list(studio, 1100, True)
        click(studio, manager, 1101)
        manager = package_list(studio, 1100, False)
        click(studio, manager, 2)
        studio.save(out / 'removed.vmm')
        assert expansion not in (out / 'removed.vmm').read_bytes()
        studio.quit()
    finally:
        studio.close()
    studio = Studio(launch, out / 'removed.vmm', 'standalone-restart')
    try:
        state = studio.state()
        assert len(state['v3dmm_packages']) == 2 and all(item['enabled'] for item in state['v3dmm_packages'])
        assert not any(item['applied'] for item in state['v3dmm_packages'])
        assert not any(item['source_id'] == 6 for item in state['thumbnail_files'])
        manager = package_page(studio)
        assert next(item for item in manager['controls'] if item['id'] == 1096)['text']
        assert not next(item for item in manager['controls'] if item['id'] == 1100)['text']
        click(studio, manager, 2)
        report['standalone_install_apply_remove_save_and_restart'] = True
        studio.quit()
    finally:
        studio.close()
    studio = Studio(launch, out / 'applied-unused.vmm', 'subset-restart')
    try:
        assert [item['applied'] for item in studio.state()['v3dmm_packages']] == [True, False]
        report['multiple_package_selection_and_saved_subset'] = True
        studio.quit()
    finally:
        studio.close()
    assert all(digest(path) == value for path, value in originals.items())
    report['reference_inputs_unchanged'] = True
    (out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'artifacts': str(out), 'checks': report}, indent=2))


if __name__ == '__main__':
    main()
