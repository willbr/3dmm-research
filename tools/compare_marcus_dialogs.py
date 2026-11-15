"""Compare actual offscreen Marcus controls with explicit retail screenshot crops.

RGB errors use unchanged native-size pixels: no alignment, scaling or tolerance.
The review sheet enlarges those pixels with nearest-neighbour filtering and also
shows the separate, freshly rendered 4x output. This is artwork review, not movie
playback or a claim that a high-resolution recreation matches a retail bitmap.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFont


def checksum(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def coloured(pixel, name):
    r, g, b = pixel
    return (r > 70 and r > g * 1.3 and r > b * 1.3) if name == 'cancel' else (
        g > 70 and g > r * 1.15 and g > b * 1.2)


def metrics(reference, candidate, name):
    def pixels(image):
        data = image.tobytes()
        return list(zip(data[0::3], data[1::3], data[2::3]))
    a, b = pixels(reference), pixels(candidate)
    def error(pairs):
        differences = [tuple(abs(x - y) for x, y in zip(left, right)) for left, right in pairs]
        if not differences:
            return None
        mean = [sum(d[c] for d in differences) / len(differences) for c in range(3)]
        return {'pixels': len(differences), 'mean_absolute_rgb': mean,
                'mean_absolute_channel_error': sum(mean) / 3,
                'rms_channel_error': (sum(v*v for d in differences for v in d) / (3*len(differences)))**.5,
                'max_channel_error': max(v for d in differences for v in d),
                'identical_pixels': sum(d == (0, 0, 0) for d in differences)}
    glyphs = [coloured(p, name) or coloured(q, name) for p, q in zip(a, b)]
    return {'whole_button': error(zip(a, b)),
            'coloured_glyph_union': error((p, q) for p, q, mask in zip(a, b, glyphs) if mask),
            'reference_coloured_pixels': sum(coloured(p, name) for p in a),
            'candidate_coloured_pixels': sum(coloured(p, name) for p in b)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--cancel-box', type=int, nargs=4, required=True, metavar=('X', 'Y', 'W', 'H'))
    parser.add_argument('--ok-box', type=int, nargs=4, required=True, metavar=('X', 'Y', 'W', 'H'))
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = Image.open(args.reference).convert('RGB')
    report = {'reference': str(args.reference), 'reference_sha256': checksum(args.reference),
              'renderer': 'Actual ui_dialog_button, SDL software renderer, normal enabled state',
              'comparison': 'Absolute RGB per channel in 0..255; native pixels; explicit crops; no resampling/alignment',
              'buttons': {}}
    font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 13) if Path('C:/Windows/Fonts/segoeui.ttf').is_file() else ImageFont.load_default()
    columns = ['Retail crop (1x enlarged)', 'Before (1x enlarged)', 'After (1x enlarged)',
               'Before RGB abs diff', 'After RGB abs diff', 'After (rendered at 4x)']
    sheet = Image.new('RGB', (6*202+16, 2*220+50), '#242a33')
    draw = ImageDraw.Draw(sheet)
    for c, label in enumerate(columns):
        draw.text((12+c*202, 9), label, font=font, fill='#eef0f5')
    for row, name in enumerate(('cancel', 'ok')):
        x, y, w, h = getattr(args, name + '_box')
        if (w, h) != (46, 43) or x < 0 or y < 0 or x+w > source.width or y+h > source.height:
            parser.error('Reference crops must be in-bounds 46x43 native button rectangles')
        reference = source.crop((x, y, x+w, y+h))
        before_path, after_path = args.before / (name + '-1x.bmp'), args.after / (name + '-1x.bmp')
        before, after = [Image.open(path).convert('RGB') for path in (before_path, after_path)]
        large = Image.open(args.after / (name + '-4x.bmp')).convert('RGB')
        if before.size != reference.size or after.size != reference.size or large.size != (184, 172):
            parser.error('Expected native 46x43 and separately rendered 184x172 controls')
        diffs = [ImageChops.difference(reference, candidate) for candidate in (before, after)]
        images = [reference, before, after, *diffs]
        for c, image in enumerate(images):
            sheet.paste(image.resize((184, 172), Image.Resampling.NEAREST), (12+c*202, 38+row*220))
        sheet.paste(large, (12+5*202, 38+row*220))
        reference.save(args.output / (name + '-retail.png'))
        for label, difference in zip(('before', 'after'), diffs):
            difference.save(args.output / (name + '-' + label + '-rgb-diff.png'))
        large.save(args.output / (name + '-high-resolution.png'))
        result = {'reference_crop_xywh': [x, y, w, h], 'before_sha256': checksum(before_path),
                  'after_sha256': checksum(after_path), 'before': metrics(reference, before, name),
                  'after': metrics(reference, after, name)}
        report['buttons'][name] = result
        draw.text((12, 215+row*220), name.upper(), font=font, fill='#eef0f5')
        print(name, json.dumps(result, indent=2))
    sheet.save(args.output / 'comparison.png')
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
