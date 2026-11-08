"""Compare incremental high-resolution counters with a complete redraw of the same frame."""
import sys
sys.dont_write_bytecode = True
import argparse
import base64
import io
import json
import pathlib
import tempfile
import uuid
from types import SimpleNamespace
from PIL import Image, ImageChops
from studio_movie_workflow import Studio
from studio_options_workflow import resolution


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'iso', 'fixture', 'artifacts'):
        parser.add_argument('--' + name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.artifacts.resolve()))
    launch = SimpleNamespace(exe=args.exe, content=output, artifacts=output,
        launch_arguments=['--iso', str(args.iso.resolve())], environment={
            'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_BATCH_TEST': '1', 'KAUAI_TEST_SETTINGS_RUN': str(uuid.uuid4()),
            'THREEDMM_PACK_DIR': str(output / 'packs'), 'SDL_VIDEODRIVER': 'dummy',
            'SDL_AUDIODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software'})
    studio = Studio(launch, args.fixture, 'counter')
    checks = []
    try:
        # A long scene keeps the scrollbar thumb stationary across nearby
        # frames, exposing small digit-only damage instead of a broad repaint.
        studio.call('edit_frame', frame=10000)
        studio.call('edit_frame', frame=1)
        studio.call('save_movie', path=str(output / 'long-scene.3mm'))
        bounds = []
        for hid in (0x20084, 0x2008D):
            gob = json.loads(studio.call('find_gob', hid=hid)['content'][0]['text'])
            assert gob['found'], gob
            bounds.append((gob['x'], gob['y'], gob['x']+gob['w'], gob['y']+gob['h']))
        def capture(label):
            data = base64.b64decode(studio.call('screenshot', presentation=True)['content'][0]['data'])
            (output / (label + '.png')).write_bytes(data)
            return Image.open(io.BytesIO(data)).convert('RGB')
        for scale in (2, 4):
            resolution(studio, scale)
            for frame in (2, 3, 1, 12, 11):
                studio.call('seek_movie', scene=1, frame=frame)
                partial = capture('partial-%d-%d' % (scale, frame))
                resolution(studio, 1)
                resolution(studio, scale)
                complete = capture('complete-%d-%d' % (scale, frame))
                assert partial.size == complete.size == (640*scale, 480*scale)
                for rectangle in bounds:
                    rectangle = tuple(value*scale for value in rectangle)
                    difference = ImageChops.difference(partial.crop(rectangle), complete.crop(rectangle))
                    assert difference.getbbox() is None, (scale, frame, rectangle, difference.getbbox(), output)
                checks.append({'scale': scale, 'frame': frame})
        studio.quit()
    finally:
        studio.close()
    (output / 'report.json').write_text(json.dumps({'status': 'passed', 'checks': checks}, indent=2))
    print(json.dumps({'artifacts': str(output), 'status': 'passed', 'comparisons': len(checks)}, indent=2))


if __name__ == '__main__': main()
