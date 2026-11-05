"""Play an entire real movie through ordinary paced Shift+Play, including transitions.

Uses an isolated copy and private settings. Authored click pauses are acknowledged
through the actual view only when it reports waiting for a click. Timed/sound
pauses, scrolling captions, audio and transitions run at their ordinary speed.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import time
import uuid
from types import SimpleNamespace
from studio_movie_workflow import Studio
from studio_movie_only_workflow import play, picture, remove_settings, render_scale


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def quit_with_unused_sounds(studio, output, choice='keep'):
    """Answer only the identified English unused-sound balloon, then await Quit."""
    studio.call('quit')
    deadline = time.monotonic() + 90
    while studio.process.poll() is None:
        try:
            # HTOP's hidThis = kgobQueryPurgeSounds, not a generic modal window.
            balloon = json.loads(studio.call('find_gob', hid=0x2000016F)['content'][0]['text'])
        except AssertionError:
            # stdout can reach EOF just before the process handle signals exit.
            try:
                code = studio.process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                raise
            if code == 0:
                break
            raise
        if balloon['found'] and balloon['visible']:
            picture(studio, output / 'unused-sounds.png')
            # English resource's radio buttons, relative to the observed card.
            studio.call('click', x=balloon['x'] + 32,
                        y=balloon['y'] + (63 if choice == 'remove' else 88))
            assert studio.process.wait(timeout=90) == 0
            return True
        assert time.monotonic() < deadline, 'Quit did not finish or show the unused-sound prompt'
        time.sleep(.05)
    assert studio.process.returncode == 0
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'iso', 'movie', 'artifacts'):
        parser.add_argument(name, type=pathlib.Path)
    parser.add_argument('--timeout', type=float, default=1800)
    parser.add_argument('--desktop', action='store_true')
    parser.add_argument('--unused-sounds', choices=('keep', 'remove'), default='keep')
    parser.add_argument('--render-scale', type=int, choices=(1, 2, 4), default=1)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.artifacts.resolve()))
    movie = output / args.movie.name
    source_hash = digest(args.movie)
    shutil.copy2(args.movie, movie)
    run = str(uuid.uuid4())
    launch = SimpleNamespace(exe=args.exe, content=output, artifacts=output, response_timeout=60,
        launch_arguments=['--iso', str(args.iso.resolve())], environment={
            'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_BATCH_TEST': '1', 'KAUAI_TEST_SETTINGS_RUN': run,
            'XDG_CONFIG_HOME': str(output / 'settings'), 'SDL_AUDIODRIVER': 'dummy',
            'SDL_VIDEODRIVER': None if args.desktop else 'dummy',
            'SDL_RENDER_DRIVER': None if args.desktop else 'software'})
    studio = None
    report = {'movie': str(args.movie.resolve()), 'source_sha256': source_hash, 'paced': True,
              'movie_only': True, 'video_driver': 'desktop' if args.desktop else 'dummy',
              'audio_driver': 'dummy', 'status': 'running', 'playback_complete': False,
              'shutdown_complete': False, 'scenes': {}, 'click_pauses': 0}
    report['render_scale'] = args.render_scale
    started = time.monotonic()
    print('Starting complete paced playback: ' + str(output), flush=True)
    try:
        studio = Studio(launch, movie, 'playback')
        report['process_id'] = studio.process.pid
        studio.state(False)
        if args.render_scale != 1:
            render_scale(studio, args.render_scale)
        studio.call('seek_movie', scene=1, frame=1)
        original = studio.state(False)
        if args.desktop and os.name == 'nt':
            from studio_sdl_smoke import focus_studio
            focus_studio(studio.process.pid)
        play(studio, True)
        previous_scene = -1
        heartbeat = started
        while True:
            state = studio.state(False)
            scene = state['movie_scene'] + 1
            frame = state['scene_frame'] - state['scene_first_frame'] + 1
            report['last_state'] = state
            report['elapsed_seconds'] = round(time.monotonic() - started, 2)
            report['scenes'][str(scene)] = {'frame': frame, 'frames': state['scene_last_frame'] - state['scene_first_frame'] + 1}
            (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
            if scene != previous_scene or time.monotonic() - heartbeat >= 30:
                print('scene %d/%d, frame %d/%d, elapsed %.1fs' %
                      (scene, state['movie_scene_count'], frame, report['scenes'][str(scene)]['frames'],
                       report['elapsed_seconds']), flush=True)
                previous_scene = scene
                heartbeat = time.monotonic()
            if not state['movie_playing']:
                break
            assert state['movie_only'] and not state['movie_paused'], state
            if state['movie_waiting_for_click']:
                studio.call('click', x=320, y=240)
                report['click_pauses'] += 1
            assert time.monotonic() - started < args.timeout, 'Complete playback timed out'
            time.sleep(.1)
        assert scene == state['movie_scene_count'] and frame == report['scenes'][str(scene)]['frames'], state
        assert len(report['scenes']) == state['movie_scene_count'], 'Not all scenes were observed'
        assert not state['movie_only'] and not state['movie_paused'], state
        assert state['sdl_fullscreen'] == original['sdl_fullscreen'] and state['client_size'] == original['client_size'], state
        picture(studio, output / 'completed.png')
        report['playback_complete'] = True
        report['copy_unchanged_after_playback'] = digest(movie) == source_hash
        assert report['copy_unchanged_after_playback'], 'Playback changed the movie copy'
        (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        report['unused_sound_prompt'] = quit_with_unused_sounds(studio, output, args.unused_sounds)
        report['unused_sound_choice'] = args.unused_sounds
        report['shutdown_complete'] = True
        report['status'] = 'passed'
        print('Complete movie playback passed: ' + str(output), flush=True)
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        if studio:
            studio.close()
        remove_settings(run)
        report['source_unchanged'] = digest(args.movie) == source_hash
        report['copy_unchanged'] = digest(movie) == source_hash
        (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        # Retail can rewrite its copy during close, even when keeping sounds.
        assert report['source_unchanged'], 'The source movie changed'


if __name__ == '__main__':
    main()
