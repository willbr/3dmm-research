"""Verify room palettes through the original Studio map and building scripts.

The startup palette contains bright green placeholders in slots 156..245.
Room backgrounds use those slots for their own colors. Screenshots must show
the room palette, including while its introductory AVI uses truecolor drawing.
Only fresh, test-owned Studio processes are used; no document is saved.
"""
import argparse
import base64
import json
import pathlib
import time

from mcp_png_test import decode_png
from studio_movie_workflow import Studio


def inspect(studio, hid):
    return json.loads(studio.call('find_gob', hid=hid)['content'][0]['text'])


def wait_for(studio, predicate, message, timeout=20):
    deadline = time.monotonic() + timeout
    while not predicate():
        assert time.monotonic() < deadline, message
        studio.call('wait_ms', ms=100)


def capture(studio, destination):
    png = base64.b64decode(studio.call('screenshot')['content'][0]['data'])
    (width, height), raw = decode_png(png)
    assert (width, height) == (640, 480)
    pixels = b''.join(raw[y * (width * 3 + 1) + 1:(y + 1) * (width * 3 + 1)] for y in range(height))
    colors = {pixels[i:i + 3] for i in range(0, len(pixels), 3)}
    green = sum(pixels[i] < 5 and pixels[i + 1] > 240 and pixels[i + 2] < 5
                for i in range(0, len(pixels), 3))
    destination.write_bytes(png)
    assert green < width * height // 200, ('Startup palette leaked into room', destination, green)
    assert len(colors) >= 128, ('Room did not render its artwork', destination, len(colors))
    return {'green_pixels': green, 'distinct_colors': len(colors)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=pathlib.Path)
    parser.add_argument('content', type=pathlib.Path)
    parser.add_argument('artifacts', type=pathlib.Path)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    report_path = args.artifacts / 'report.json'
    report_path.unlink(missing_ok=True)
    report = {}
    # Separate processes exercise every palette directly against the startup
    # palette. This also avoids carrying an earlier room's colors into the next.
    for name, mask, room in [('ticket', 0x12054, 0x10580), ('lobby', 0x12052, 0x10600),
                             ('studio', 0x12057, 0x11800)]:
        studio = Studio(args, None, name)
        try:
            initial = studio.state()
            studio.call('send_command', cid=40042) # Original Map command.
            wait_for(studio, lambda: inspect(studio, mask).get('visible', False), 'Map did not open')
            studio.control(mask)
            wait_for(studio, lambda: inspect(studio, room).get('visible', False), 'Building room did not open')
            studio.call('wait_ms', ms=750)
            studio.state()
            report[name] = capture(studio, args.artifacts / (name + '.png'))
            if name == 'ticket':
                wait_for(studio, lambda: studio.state()['sdl_video_presented'] > initial['sdl_video_presented'],
                         'Ticket intro video did not draw')
                video = studio.state()
                assert video['sdl_video_started'] > video['sdl_video_completed'], 'Ticket intro is already over'
                report['ticket_during_video'] = capture(studio, args.artifacts / 'ticket-during-video.png')
                # The cans appear after the intro AVI is destroyed. Check the
                # indexed drawing path too, after truecolor drawing is released.
                wait_for(studio, lambda: inspect(studio, 0x10584)['found'], 'Ticket intro did not finish', timeout=30)
                studio.call('wait_ms', ms=200)
                report['ticket_after_video'] = capture(studio, args.artifacts / 'ticket-after-video.png')
            studio.quit()
        finally:
            studio.close()
    report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Studio building workflow: ticket booth, lobby and studio palettes passed')


if __name__ == '__main__':
    main()
