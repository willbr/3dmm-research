"""Exercise authored movie features through the actual Studio runtime.

The fixture contains two populated scenes, camera cuts, skeletal animation,
two props, 3D text, costume changes, and two embedded checkerboard textures.
Checks live BRender data and workspace pixels, forward/backward seeks,
continuous playback across scenes, and save/reopen in a fresh Studio process.
Only generated artifact documents are opened or written.
"""
import argparse
import base64
import hashlib
import json
import os
import pathlib
import queue
import subprocess
import threading
import time
from mcp_png_test import decode_png


def texture_hash(variant):
    value = 14695981039346656037
    for y in range(16):
        for x in range(16):
            value = ((value ^ (32 + 32 * ((x // 4 + y // 4 + variant) % 4))) * 1099511628211) & ((1 << 64) - 1)
    return str(value)


class Studio:
    def __init__(self, args, movie, label):
        self.args = args
        self.log = (args.artifacts / (label + '.log')).open('w', encoding='utf-8')
        command = [str(args.exe.resolve()), '--mcp-server']
        if movie is not None:
            command.append(str(movie.resolve()))
        command[2:2] = getattr(args, 'launch_arguments', [])
        environment = {**os.environ, 'KAUAI_TEST_FAIL_FAST': '1',
                       'THREEDMM_CONTENT_ROOT': str(args.content.resolve().parent)}
        for name, value in getattr(args, 'environment', {}).items():
            if value is None:
                environment.pop(name, None)
            else:
                environment[name] = value
        self.process = subprocess.Popen(command,
            cwd=args.content.resolve(), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log,
            text=True, encoding='utf-8', env=environment)
        self.replies = queue.Queue()
        self.sequence = 0
        def read():
            for line in self.process.stdout:
                self.replies.put(json.loads(line))
            self.replies.put(None)
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()

    def call(self, name, **arguments):
        assert self.process.poll() is None, 'Studio exited; inspect ' + self.log.name
        self.sequence += 1
        self.process.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': self.sequence, 'method': 'tools/call',
            'params': {'name': name, 'arguments': arguments}}) + '\n')
        self.process.stdin.flush()
        reply = self.replies.get(timeout=getattr(self.args, 'response_timeout', 45))
        assert reply is not None, 'Studio exited; inspect the runtime log'
        assert reply['id'] == self.sequence, reply
        assert 'error' not in reply, reply
        result = reply['result']
        assert not result.get('isError'), result
        return result

    def state(self, details=True):
        result = json.loads(self.call('get_state', movie_details=details)['content'][0]['text'])
        assert result['error_stack_depth'] == 0, result
        return result

    def scroll(self, scene=False, tool=22):
        self.call('send_command', cid=50022 if scene else 50021, lw0=tool)
        self.call('wait_ms', ms=30)

    def first(self):
        self.control(0x20088)

    def next_scene(self):
        self.control(0x2008e)

    def control(self, hid):
        item = json.loads(self.call('find_gob', hid=hid)['content'][0]['text'])
        assert item['found'], item
        self.call('click', x=item['center_x'], y=item['center_y'])
        self.call('wait_ms', ms=40)

    def capture(self, label):
        image = self.call('screenshot')['content'][0]
        png = base64.b64decode(image['data'])
        (width, height), raw = decode_png(png)
        assert (width, height) == (640, 480)
        (self.args.artifacts / (label + '.png')).write_bytes(png)
        workspace = json.loads(self.call('find_gob', hid=0x2005d)['content'][0]['text'])
        assert workspace['found'] and workspace['w'] > 0 and workspace['h'] > 0, workspace
        left, top = max(0, workspace['x']), max(0, workspace['y'])
        right, bottom = min(width, left + workspace['w']), min(height, top + workspace['h'])
        # Exclude timeline labels/toolbars: a changing frame number alone is
        # not evidence that actor geometry or a camera view actually rendered.
        pixels = b''.join(raw[y * (width * 3 + 1) + 1 + left * 3:
                              y * (width * 3 + 1) + 1 + right * 3] for y in range(top, bottom))
        return hashlib.sha256(pixels).hexdigest()

    def save(self, destination):
        self.call('send_command', cid=104)
        deadline = time.monotonic() + 15
        while True:
            dialog = json.loads(self.call('get_sdl_dialog')['content'][0]['text'])
            if dialog['active'] and dialog['ready']:
                break
            assert time.monotonic() < deadline, 'Save portfolio did not open'
            time.sleep(.03)
        self.call('sdl_dialog_action', generation=dialog['generation'], action='text', control=11,
                  text=str(destination.resolve()))
        while True:
            current = json.loads(self.call('get_sdl_dialog')['content'][0]['text'])
            if any(c['id'] == 11 and c['text'] == str(destination.resolve()) for c in current['controls']):
                break
            assert time.monotonic() < deadline
            time.sleep(.03)
        self.call('sdl_dialog_action', generation=dialog['generation'], action='click', control=1)
        while json.loads(self.call('get_sdl_dialog')['content'][0]['text'])['active']:
            assert time.monotonic() < deadline
            time.sleep(.03)
        while not destination.exists() or destination.stat().st_size <= 1000:
            assert self.process.poll() is None, 'Studio exited during Save; inspect ' + self.log.name
            assert time.monotonic() < deadline, 'Save did not complete; inspect ' + self.log.name
            time.sleep(.03)

    def quit(self):
        self.call('quit')
        assert self.process.wait(timeout=15) == 0

    def close(self):
        if self.process.poll() is None:
            self.process.kill() # Only this test's process; never an existing Studio instance.
        self.process.wait(timeout=5)
        self.reader.join(timeout=2)
        self.log.close()


def actors(state):
    return {item['id']: item for item in state['scene_actors']}


def signature(state):
    return {key: state[key] for key in ('movie_scene', 'movie_scene_count', 'scene_frame', 'scene_camera', 'scene_actors')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=pathlib.Path)
    parser.add_argument('content', type=pathlib.Path)
    parser.add_argument('artifacts', type=pathlib.Path)
    parser.add_argument('--fixture-exe', type=pathlib.Path, required=True)
    parser.add_argument('--content-dir', type=pathlib.Path)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    content = args.content_dir or args.content
    def asset(folder, name):
        return next(file.resolve() for file in folder.iterdir() if file.name.lower() == name.lower())
    source = args.artifacts / 'features.3mm'
    saved = args.artifacts / 'roundtrip.3mm'
    saved.unlink(missing_ok=True)
    subprocess.run([str(args.fixture_exe.resolve()), str(source.resolve()), str(asset(content, 'bkgds.3cn')),
        str(asset(args.content, '3dmovie.chk')), str(asset(content, 'tmpls.3cn'))], check=True)
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    report = {'features': {}, 'snapshots': {}}
    studio = Studio(args, source, 'runtime')
    try:
        first = studio.state()
        assert first['movie_scene_count'] == 2 and first['scene_actor_count'] == 4, first
        assert first['scene_camera_count'] >= 2 and first['scene_camera'] == 0, first
        cast = actors(first)
        assert cast[0]['kind'] == 'actor' and cast[0]['on_stage'] and cast[0]['action'] > 0, cast
        assert cast[1]['kind'] == cast[2]['kind'] == 'prop', cast
        assert cast[3]['kind'] == '3d_text' and cast[3]['on_stage'] and cast[3]['parts'] >= 2, cast
        assert texture_hash(0) in cast[2]['texture_hashes'], cast[2]
        first_pixels = studio.capture('scene1-frame1')
        samples = {1: first}
        for frame in range(2, 31):
            studio.scroll()
            state = studio.state()
            assert state['scene_frame'] == frame, state
            if frame in (2, 3, 4, 5, 6, 9, 11, 12, 14, 15, 21, 30):
                samples[frame] = state
            if frame in (6, 9, 12, 15, 21):
                report['snapshots'][str(frame)] = studio.capture('scene1-frame' + str(frame))
        assert len({actors(samples[f])[0]['pose_hash'] for f in range(1, 7)}) > 1, 'No skeletal animation'
        assert actors(samples[6])[0]['position_hash'] != cast[0]['position_hash'], 'Actor did not move'
        assert actors(samples[6])[1]['position_hash'] != cast[1]['position_hash'], 'Prop did not move'
        assert actors(samples[9])[0]['appearance_hash'] != cast[0]['appearance_hash'], 'Costume change missing'
        assert actors(samples[21])[0]['appearance_hash'] == cast[0]['appearance_hash'], 'Costume did not restore'
        assert texture_hash(0) in actors(samples[11])[2]['texture_hashes']
        assert texture_hash(1) in actors(samples[12])[2]['texture_hashes'], 'Second embedded texture missing'
        assert samples[14]['scene_camera'] == 0 and samples[15]['scene_camera'] == 1, 'Camera cut missing'
        assert report['snapshots']['6'] != first_pixels, 'Workspace did not animate'
        studio.first()
        assert signature(studio.state()) == signature(first), 'Backward seek did not restore original actor state'
        assert studio.capture('scene1-rewind') == first_pixels, 'Backward seek did not restore workspace pixels'
        for _ in range(5):
            studio.scroll()
        assert signature(studio.state()) == signature(samples[6]), 'Repeated seek changed actor state'
        assert studio.capture('scene1-repeat6') == report['snapshots']['6'], 'Repeated seek changed workspace pixels'
        studio.next_scene()
        second = studio.state()
        assert second['movie_scene'] == 1 and second['scene_actor_count'] == 4 and second['scene_camera'] == 1, second
        second_pixels = studio.capture('scene2-frame1')
        assert second_pixels != first_pixels, 'Second scene did not render its distinct camera'
        # Rewind to scene 1 through the original scene scrollbar, then play
        # through both scenes without forcing a frame/scene from automation.
        studio.control(0x20090)
        studio.first()
        studio.control(0x20039)
        observed = []
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            state = studio.state(details=False)
            observed.append({key: state[key] for key in ('movie_scene', 'scene_frame', 'scene_camera', 'movie_playing')})
            if not state['movie_playing']:
                break
            time.sleep(.035)
        else:
            raise AssertionError('Multi-scene playback did not finish')
        assert any(s['movie_scene'] == 0 and s['scene_frame'] > 1 for s in observed), observed
        assert any(s['movie_scene'] == 1 and s['scene_frame'] > 1 for s in observed), observed
        assert observed[-1]['movie_scene'] == 1 and observed[-1]['scene_frame'] == 30, observed[-1]
        report['playback'] = observed
        studio.save(saved)
        studio.quit()
    finally:
        studio.close()
    studio = Studio(args, saved, 'reopened')
    try:
        assert signature(studio.state()) == signature(first), 'Saved scene/camera/actor/texture data changed'
        assert studio.capture('reopened-frame1') == first_pixels
        for _ in range(11):
            studio.scroll()
        assert signature(studio.state()) == signature(samples[12]), 'Saved texture swap/animation/costume changed'
        assert studio.capture('reopened-frame12') == report['snapshots']['12']
        studio.next_scene()
        assert signature(studio.state()) == signature(second), 'Saved second-scene state changed'
        assert studio.capture('reopened-scene2-frame1') == second_pixels
        studio.quit()
    finally:
        studio.close()
    assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash, 'Test edited its source fixture'
    report['features'] = dict.fromkeys(('cameras', 'multiple_scenes', 'skeletal_animation', 'actor_movement',
        'props', '3d_text', 'costume_changes', 'embedded_textures', 'rewind', 'repeated_seek',
        'workspace_pixels', 'natural_playback', 'save_reopen'), 'passed')
    report['samples'] = samples
    (args.artifacts / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Studio movie workflow: ' + ', '.join(report['features']) + ' passed')


if __name__ == '__main__':
    main()
