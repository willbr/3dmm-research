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
        current = select_tab(studio, options(studio), 1071)
        click(studio, current, 1095)
        manager = dialog(studio, 'V3DMM packages')
        controls = {item['id']: item for item in manager['controls']}
        assert controls[1097]['number']
        click(studio, manager, 1097)
        click(studio, dialog(studio, 'V3DMM packages'), 1)
        click(studio, dialog(studio, 'Options'), 2)
        report['package_manager_toggle'] = True
        studio.quit()
    finally:
        studio.close()
    studio = Studio(launch, plain, 'disabled')
    try:
        state = studio.state()
        assert not state['v3dmm_packages'][0]['enabled']
        assert not any(item['source_id'] == 6 for item in state['thumbnail_files'])
        report['disable_applies_after_restart'] = True
        studio.quit()
    finally:
        studio.close()
    # A standalone import uses the same package store through the real file
    # portfolio, with three nested modal windows (Options, manager, portfolio).
    launch.environment['THREEDMM_PACK_DIR'] = str(out / 'standalone-packs')
    studio = Studio(launch, plain, 'standalone-import')
    try:
        assert not studio.state()['v3dmm_packages']
        current = select_tab(studio, options(studio), 1071)
        click(studio, current, 1095)
        click(studio, dialog(studio, 'V3DMM packages'), 1098)
        portfolio = dialog(studio, 'Install a V3DMM expansion')
        studio.call('sdl_dialog_action', generation=portfolio['generation'], action='text', control=11,
                    text=str(args.prop_vxp.resolve()))
        # Let ImGui apply the posted text event before the accept click.
        deadline = time.monotonic() + 10
        while not any(item['id'] == 11 and item['text'] == str(args.prop_vxp.resolve())
                      for item in dialog(studio, 'Install a V3DMM expansion')['controls']):
            assert time.monotonic() < deadline
        click(studio, portfolio, 1)
        manager = dialog(studio, 'V3DMM packages')
        while not next(item for item in manager['controls'] if item['id'] == 1097)['number']:
            assert time.monotonic() < deadline, manager
            time.sleep(.02)
            manager = dialog(studio, 'V3DMM packages')
        click(studio, manager, 1)
        click(studio, dialog(studio, 'Options'), 2)
        studio.quit()
    finally:
        studio.close()
    studio = Studio(launch, plain, 'standalone-restart')
    try:
        state = studio.state()
        assert len(state['v3dmm_packages']) == 1 and state['v3dmm_packages'][0]['enabled']
        assert any(item['source_id'] == 6 for item in state['thumbnail_files'])
        report['standalone_import_through_portfolio_and_restart'] = True
        studio.quit()
    finally:
        studio.close()
    assert all(digest(path) == value for path, value in originals.items())
    report['reference_inputs_unchanged'] = True
    (out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'artifacts': str(out), 'checks': report}, indent=2))


if __name__ == '__main__':
    main()
