"""Replay scenes in the ISO retail EXE and SDL, comparing native results, actors and rendered pixels."""
import sys
sys.dont_write_bytecode = True
import argparse
import base64
from collections import Counter
import io
import json
import math
import pathlib
import tempfile
import time
import uuid
from types import SimpleNamespace
from PIL import Image, ImageChops, ImageStat
from session import RetailSession, prepare_iso, digest, OracleError

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from studio_movie_workflow import Studio


class SDLSession:
    def __init__(self, executable, iso, movie, output, cache):
        output.mkdir(parents=True, exist_ok=True)
        self.settings_run = uuid.uuid4().hex
        args = SimpleNamespace(exe=executable, content=output, artifacts=output,
            launch_arguments=['--iso', str(iso.resolve())], environment={
                'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_ISO': None, 'KAUAI_TEST_FAIL_FAST': None,
                'THREEDMM_BATCH_TEST': '1', 'THREEDMM_CACHE_DIR': str(cache.resolve()),
                'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software',
                'KAUAI_TEST_SETTINGS_RUN': self.settings_run, 'KAUAI_SOUNDFONT': '', 'KAUAI_FONT_PATH': ''})
        self.studio = Studio(args, movie, 'runtime')
        self.output = output

    def state(self):
        return json.loads(self.studio.call('get_state', movie_details=True)['content'][0]['text'])

    def seek(self, scene, frame):
        result = json.loads(self.studio.call('seek_movie', scene=scene, frame=frame)['content'][0]['text'])
        result['state'] = self.state()
        return result

    def capture(self, label):
        encoded = self.studio.call('screenshot')['content'][0]['data']
        data = base64.b64decode(encoded)
        (self.output / (label + '.png')).write_bytes(data)
        return Image.open(io.BytesIO(data)).convert('RGB')

    def close(self):
        self.studio.close()
        from session import delete_registry_tree
        delete_registry_tree(r'Software\3DMMForever\RetailOracle\SDL-' + self.settings_run)


def differences(retail, port):
    result = []
    for field in ('movie_scene_count', 'movie_scene', 'scene_frame', 'scene_first_frame', 'scene_last_frame',
                  'scene_camera', 'scene_actor_count', 'scene_textbox_count', 'error_stack_depth'):
        if retail[field] != port[field]:
            result.append({'field': field, 'retail': retail[field], 'sdl': port[field]})
    left = {actor['id']: actor for actor in retail['scene_actors']}
    right = {actor['id']: actor for actor in port['scene_actors']}
    if len(left) != len(retail['scene_actors']) or len(right) != len(port['scene_actors']):
        result.append({'field': 'actor_ids', 'error': 'duplicate_actor_id'})
    if left.keys() != right.keys():
        result.append({'field': 'actor_ids', 'retail_only': sorted(left.keys() - right.keys()),
                       'sdl_only': sorted(right.keys() - left.keys())})
    for actor_id in sorted(left.keys() & right.keys()):
        a, b = left[actor_id], right[actor_id]
        fields = ['on_stage', 'template_sid', 'template_cno']
        if a['on_stage'] and b['on_stage']:
            fields.extend(('action', 'cel', 'frame', 'next_event'))
        for field in fields:
            if a[field] != b[field]:
                result.append({'actor': actor_id, 'field': field, 'retail': a[field], 'sdl': b[field]})
        if a['on_stage'] and b['on_stage']:
            if not all(isinstance(x, (int, float)) and isinstance(y, (int, float)) and
                       math.isclose(x, y, rel_tol=1e-6, abs_tol=1e-4) for x, y in zip(a['position'], b['position'])):
                result.append({'actor': actor_id, 'field': 'position', 'retail': a['position'], 'sdl': b['position']})
    return result


def pixels(retail, port, rectangle):
    if retail.size != port.size:
        return {'error': 'different_client_size', 'retail': retail.size, 'sdl': port.size}
    if not (0 <= rectangle[0] < rectangle[2] <= retail.width and
            0 <= rectangle[1] < rectangle[3] <= retail.height):
        return {'error': 'viewport_outside_client'}
    left, right = retail.crop(rectangle), port.crop(rectangle)
    delta = ImageChops.difference(left, right)
    data = delta.tobytes()
    changed = sum(bool(data[i] or data[i + 1] or data[i + 2]) for i in range(0, len(data), 3))
    return {'different_pixels': changed, 'total_pixels': left.width * left.height,
            'mean_absolute_channel_error': sum(ImageStat.Stat(delta).mean) / 3,
            'max_channel_error': max(high for low, high in delta.getextrema()),
            'retail_sha256': __import__('hashlib').sha256(left.tobytes()).hexdigest(),
            'sdl_sha256': __import__('hashlib').sha256(right.tobytes()).hexdigest()}


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    product, manifest = prepare_iso(args.iso, args.output.resolve() / 'iso-package')
    source_hash = digest(args.movie)
    report = {'movie': str(args.movie.resolve()), 'movie_sha256': source_hash, 'iso': manifest,
              'sdl_exe_sha256': digest(args.sdl), 'bridge_dll_sha256': digest(args.bridge / 'retail-oracle.dll'),
              'coordinates': 'one-based scene/frame UI numbers', 'viewport': args.viewport,
              'max_different_pixels': args.max_different_pixels,
              'state_contract': 'scene facts, actor identity/visibility/template, visible action/cel/frame/event cursor/position',
              'position_tolerance': {'relative': 1e-6, 'absolute': 1e-4}, 'frames': []}
    retail, port = None, None
    started = time.monotonic()
    try:
        retail = RetailSession(args.bridge, product, args.movie, output / 'retail')
        report['retail_identity'] = retail.identity
        port = SDLSession(args.sdl, args.iso, retail.movie, output / 'sdl', args.output / 'sdl-cache')
        initial = retail.state()
        scenes = range(1, initial['movie_scene_count'] + 1) if args.scenes == 'all' else [int(x) for x in args.scenes.split(',')]
        for scene in scenes:
            first = retail.seek(scene, 1)
            count = first['state']['scene_last_frame'] - first['state']['scene_first_frame'] + 1
            if args.frames == 'all':
                frame_numbers = range(1, count + 1)
            else:
                lower, upper = map(int, args.frames.split(':'))
                if lower < 1 or upper < lower or upper > count:
                    raise OracleError('Requested frame range is outside scene ' + str(scene))
                frame_numbers = range(lower, upper + 1)
            for frame in frame_numbers:
                label = 'scene%04d-frame%06d' % (scene, frame)
                # Both sides execute the same absolute seek followed by a render
                # only when the actual engine operation reports success.
                a, b = retail.seek(scene, frame), port.seek(scene, frame)
                item = {'scene_number': scene, 'frame_number': frame,
                        'retail_success': a['success'], 'sdl_success': b['success'],
                        'retail': a['state'], 'sdl': b['state'],
                        'state_differences': differences(a['state'], b['state'])}
                item['pixels'] = pixels(retail.capture(label), port.capture(label), args.viewport)
                item['pixels']['comparable'] = a['success'] and b['success']
                # A failed seek leaves a partial scene and may repaint the two
                # clients at different times. Keep those screenshots as evidence,
                # but compare rendered pixels only after successful engine calls.
                if a['success'] != b['success'] or item['state_differences']:
                    item['status'] = 'different'
                elif not a['success']:
                    item['status'] = 'shared_failure'
                else:
                    item['status'] = 'different' if (item['pixels'].get('error') or
                        item['pixels']['different_pixels'] > args.max_different_pixels) else 'matched'
                report['frames'].append(item)
                report['counts'] = dict(Counter(frame['status'] for frame in report['frames']))
                (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
                print('%s %s (%s pixels, %d state differences)' % (label, item['status'],
                      item['pixels'].get('different_pixels', 'unknown') if item['pixels']['comparable'] else 'not comparable',
                      len(item['state_differences'])), flush=True)
        report['retail_provenance'] = retail.validate_assets()
        report['status'] = ('different' if report['counts'].get('different') else
                            'shared_failure' if report['counts'].get('shared_failure') else 'matched')
    except Exception as error:
        report['status'] = 'error'
        report['error'] = str(error)
    finally:
        for session in (port, retail):
            if session:
                try:
                    session.close()
                except Exception as error:
                    report['status'] = 'error'
                    report.setdefault('cleanup_errors', []).append(str(error))
        report['wall_seconds'] = round(time.monotonic() - started, 3)
        report['source_unchanged'] = digest(args.movie) == source_hash
        report['iso_resources_unchanged'] = all(digest(args.output.resolve() / 'iso-package' / path) == expected
            for path, expected in manifest['files'].items())
        if not report['source_unchanged'] or not report['iso_resources_unchanged']:
            report['status'] = 'error'
            report['error'] = 'Test source or ISO resources changed'
        (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        (args.output / 'latest.json').write_text(json.dumps({'report': str(output / 'report.json')}, indent=2))
        print('Report: ' + str(output / 'report.json'))
    return 0 if report['status'] == 'matched' else 2 if report['status'] == 'error' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdl', type=pathlib.Path, required=True)
    parser.add_argument('--iso', type=pathlib.Path, required=True)
    parser.add_argument('--bridge', type=pathlib.Path, required=True)
    parser.add_argument('--movie', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--scenes', default='1', help='Comma-separated UI scene numbers, or all')
    parser.add_argument('--frames', default='all', help='Inclusive UI frame range START:END, or all')
    parser.add_argument('--viewport', type=int, nargs=4, default=[45, 99, 594, 407])
    parser.add_argument('--max-different-pixels', type=int, default=0, help='Explicit pixel tolerance; state and native results must still match')
    args = parser.parse_args()
    if args.max_different_pixels < 0:
        parser.error('Pixel tolerance cannot be negative')
    if not (0 <= args.viewport[0] < args.viewport[2] and 0 <= args.viewport[1] < args.viewport[3]):
        parser.error('Viewport must be a nonempty rectangle with nonnegative coordinates')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
