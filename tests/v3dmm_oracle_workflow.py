"""Opt-in original-v3dmm oracle workflow with a custom prop and two audible cues.

Pixel/PCM differences remain failures in the parity report. This workflow tests
the harness independently: reference identity, full coverage, real audio and
source/preference isolation. Assets remain local and are never committed.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import json
import math
import pathlib
import struct
import tempfile
import wave
import zipfile
from types import SimpleNamespace
ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/retail_oracle'))
from compare import SDLSession, run
from session import digest
from v3dmm import unpack_bundle
from retail_oracle_workflow import preferences


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdl', 'iso', 'bridge', 'v3dmm', 'blank', 'vxp', 'output'):
        parser.add_argument('--'+name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    before = preferences()
    originals = {str(path): digest(path) for path in (args.blank, args.vxp)}
    _, movie, _ = unpack_bundle(args.blank, output / 'blank')
    with zipfile.ZipFile(args.vxp, metadata_encoding='cp1252') as archive:
        config = archive.read(next(name for name in archive.namelist() if name.lower().endswith('.cfg')))
        fields = dict(line.split(b'=', 1) for line in config.replace(b'\r', b'').splitlines() if b'=' in line)
        name = next(value.strip() for key, value in fields.items() if key.strip().lower() == b'name')
    data = args.vxp.read_bytes()
    source = output / 'source.vmm'
    source.write_bytes(b'v3dmm\1' + struct.pack('<I', 2) + struct.pack('<IH', 0, 4) + b'3dmm' +
        struct.pack('<IH', len(data), len(name)) + name + b'\xf0\x0b' + data + b'\xfe\xeb' + movie.read_bytes())
    studio = SDLSession(args.sdl, args.iso, source, output / 'author', output / 'sdl-cache')
    saved = output / 'sound-and-prop.vmm'
    try:
        author = studio.studio
        assets = lambda kind: json.loads(author.call('list_assets', kind=kind)['content'][0]['text'])
        background = next(item for item in assets('backgrounds') if item['sid'] == 2)
        props = [item for kind in ('props', 'actors') for item in assets(kind) if item['sid'] == 6]
        assert props, 'Supply a Portable prop/actor VXP'
        author.call('create_scene', background_cno=background['cno'], sid=2)
        author.call('create_actor', template_cno=props[0]['cno'], sid=6, name='Oracle prop')
        author.call('key', vk=27)
        for frame, frequency in ((1, 440), (25, 880)):
            sound = output / ('tone-%d.wav' % frequency)
            with wave.open(str(sound), 'wb') as stream:
                stream.setnchannels(1); stream.setsampwidth(2); stream.setframerate(11025)
                # Deterministic, quiet PCM speech cues; sufficient silence
                # between them exposes cue order, omission and timing errors.
                samples = [round(2048*math.sin(2*math.pi*frequency*i/11025)) for i in range(5512)]
                stream.writeframes(struct.pack('<%dh' % len(samples), *samples))
            author.call('edit_frame', frame=frame)
            author.call('import_sound', path=str(sound))
            author.call('set_frame_pause', kind='none')
        author.call('edit_frame', frame=60)
        author.save(saved)
        author.quit()
    finally:
        studio.close()
    status = run(SimpleNamespace(sdl=args.sdl, iso=args.iso, bridge=args.bridge, movie=saved,
        v3dmm=args.v3dmm, libraries={}, output=output / 'compare', scenes='all', frames='all',
        viewport=[45, 99, 594, 407], max_different_pixels=0, audio='required',
        audio_max_seconds=25, audio_alignment_ms=0, audio_sample_tolerance=0, frame_time_tolerance_ms=25))
    latest = json.loads((output / 'compare/latest.json').read_text())
    report = json.loads(pathlib.Path(latest['report']).read_text())
    assert status in (0, 1), report.get('error', report.get('audio'))
    assert len(report['frames']) == 60 and all(item['retail_success'] and item['sdl_success'] for item in report['frames'])
    assert not any(item['state_differences'] for item in report['frames']), 'Inspect the parity report for native state differences'
    audio = report['audio']
    assert audio['sound_events_match'], 'Original and port disagree on sound IDs, frame, volume, looping or offset'
    for engine in ('reference', 'sdl'):
        playback = json.loads((pathlib.Path(latest['report']).parent / 'audio' / engine / 'playback.json').read_text())
        assert [(cue['frame'], cue['plays'], cue['category']) for cue in playback['trace']['sounds']] == [
            (1, 1, 0), (25, 1, 0)], 'Both speech clips must play once in the non-looping movie class'
    assert audio['frame_commits_match'] and audio['reference_complete_frame_commits'] and audio['sdl_complete_frame_commits'], audio
    assert audio['pcm']['reference_audible_frames'] > 1000 and audio['pcm']['sdl_audible_frames'] > 1000, audio
    assert preferences() == before, 'Installed retail preferences changed'
    assert all(digest(path) == value for path, value in originals.items()), 'A reference input changed'
    print(json.dumps({'artifacts': str(output), 'harness': 'passed', 'parity_status': report['status'],
        'frame_counts': report['counts'], 'pcm_status': audio['pcm']['status']}, indent=2))


if __name__ == '__main__': main()
