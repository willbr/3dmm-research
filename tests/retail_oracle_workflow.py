"""Opt-in Windows integration test of the original ISO EXE, native bridge and SDL comparison.

Requires an interactive Windows desktop, x86 bridge build, 7-Zip and Pillow.
Pixel differences remain recorded failures in the comparison report; this test
checks the bridge/state contract independently of renderer compatibility.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import json
import pathlib
import queue
import subprocess
import threading
import time
import winreg
from types import SimpleNamespace

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/retail_oracle'))
from compare import run
from session import prepare_iso, digest


def preferences():
    """Capture the real app's preferences to prove test writes stayed private."""
    result = {}
    def visit(path):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_READ | winreg.KEY_WOW64_32KEY) as key:
                children, values, _ = winreg.QueryInfoKey(key)
                for i in range(values):
                    name, value, kind = winreg.EnumValue(key, i)
                    result[path + '\\' + name] = [kind, repr(value)]
                for i in range(children):
                    visit(path + '\\' + winreg.EnumKey(key, i))
        except FileNotFoundError:
            pass
    visit(r'Software\Microsoft\Microsoft Kids\3D Movie Maker')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdl', 'iso', 'bridge', 'fixture', 'output'):
        parser.add_argument('--' + name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    before = preferences()
    product, _ = prepare_iso(args.iso, args.output / 'iso-package')
    movie = args.output / 'features.3mm'
    with (args.output / 'fixture.log').open('w') as log:
        subprocess.run([str(args.fixture.resolve()), str(movie), str(product / 'BKGDS.3CN'),
                        str(product / '3DMOVIE.CHK'), str(product / 'TMPLS.3CN')],
                       stdout=log, stderr=log, check=True, timeout=30)
    original = digest(movie)
    status = run(SimpleNamespace(sdl=args.sdl, iso=args.iso, bridge=args.bridge, movie=movie,
                 output=args.output, scenes='all', frames='all', viewport=[45, 99, 594, 407], max_different_pixels=0))
    assert status in (0, 1), 'Comparison infrastructure failed'
    report = json.loads(pathlib.Path(json.loads((args.output / 'latest.json').read_text())['report']).read_text())
    assert len(report['frames']) == 60, 'Did not cover both complete scenes'
    assert all(f['retail_success'] and f['sdl_success'] and not f['state_differences'] for f in report['frames']), \
        'Native result/state differs; inspect comparison report'
    assert all('different_pixels' in f['pixels'] for f in report['frames']), 'Capture sizes do not match'
    assert any(f['pixels']['different_pixels'] == 0 for f in report['frames']), 'No exact frame; check capture alignment'
    assert {f['retail']['scene_camera'] for f in report['frames']} == {0, 1}
    assert {a['kind'] for a in report['frames'][0]['sdl']['scene_actors']} == {'actor', 'prop', '3d_text'}

    # Exercise the actual stdio frontend, not a substitute Python dispatcher.
    with (args.output / 'server.log').open('w') as log:
        server = subprocess.Popen([sys.executable, str(ROOT / 'tools/retail_oracle/server.py'),
            '--iso', str(args.iso.resolve()), '--bridge', str(args.bridge.resolve()),
            '--movie', str(movie), '--output', str(args.output)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True, encoding='utf-8')
        replies = queue.Queue()
        def read():
            for line in server.stdout:
                replies.put(json.loads(line))
            replies.put(None)
        threading.Thread(target=read, daemon=True).start()
        sequence = 0
        def rpc(method, **params):
            nonlocal sequence
            sequence += 1
            server.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': sequence, 'method': method, 'params': params}) + '\n')
            server.stdin.flush()
            reply = replies.get(timeout=45)
            assert reply and reply['id'] == sequence, 'Missing server reply; inspect server.log'
            assert 'error' not in reply, reply
            return reply['result']
        def call(name, **arguments):
            result = rpc('tools/call', name=name, arguments=arguments)
            assert not result.get('isError'), result
            return json.loads(result['content'][0]['text'])
        try:
            assert rpc('initialize')['serverInfo']['name'] == '3dmm-retail-oracle'
            assert {t['name'] for t in rpc('tools/list')['tools']} == {
                'get_state', 'seek_movie', 'send_command', 'screenshot', 'wait_ms'}
            invalid = rpc('tools/call', name='seek_movie', arguments={'scene': 999999, 'frame': 1})
            assert invalid['isError'] and 'scene_out_of_range' in invalid['content'][0]['text']
            baseline = call('seek_movie', scene=1, frame=12)
            assert baseline['success']
            call('seek_movie', scene=2, frame=30)
            restored = call('seek_movie', scene=1, frame=12)
            assert baseline['state'] == restored['state'], 'Cross-scene backward seek changed native state'
            call('seek_movie', scene=1, frame=1)
            assert call('send_command', cid=50005)['queued']  # Original play command.
            deadline = time.monotonic() + 30
            advanced = False
            observed = []
            while time.monotonic() < deadline:
                state = call('get_state')
                observed.append({key: state[key] for key in ('scene_number', 'frame_number', 'movie_playing', 'error_stack_depth')})
                advanced |= state['frame_number'] > 1 or state['scene_number'] > 1
                if advanced and not state['movie_playing']:
                    break
                # Retail's frame alarm waits for an idle dispatch. Frequent
                # bridge messages can starve it while a render is expensive.
                time.sleep(.25)
            (args.output / 'native-playback.json').write_text(json.dumps(observed, indent=2))
            assert advanced and not state['movie_playing'], 'Native command playback did not run to completion; inspect native-playback.json'
            assert state['scene_number'] == 2 and state['frame_number'] == 30 and not state['error_stack_depth'], state
            capture = rpc('tools/call', name='screenshot')['content'][0]
            assert capture['type'] == 'image' and capture['mimeType'] == 'image/png'
        finally:
            server.stdin.close()  # EOF tears down its own child and private registry.
            try:
                assert server.wait(timeout=15) == 0, 'Server failed; inspect server.log'
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
                raise
    assert digest(movie) == original, 'Source fixture changed'
    assert preferences() == before, 'Test modified installed app preferences'
    (args.output / 'workflow.json').write_text(json.dumps({'status': 'passed', 'frames': 60,
        'native_states': 'matched', 'pixels': report['counts'], 'mcp': 'passed',
        'backward_seek': 'passed', 'native_command_playback': 'passed', 'source_and_preferences': 'unchanged'}, indent=2))
    print('Retail oracle workflow passed: 60 native states, MCP, rewind, command playback, isolated preferences.')


if __name__ == '__main__':
    main()
