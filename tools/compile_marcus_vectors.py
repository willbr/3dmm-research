"""Compile Marcus's flat SVG artwork to portable C89 triangle data.

The current icons use rectangles and convex polygons, with explicit RGB fills.
Other SVG features are rejected, so unsupported artwork cannot silently change.
The generated header is checked in; building Marcus needs no SVG loader/Python.
"""
import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ICONS = ROOT / 'src/marcus/icons'
OUTPUT = ROOT / 'src/marcus/ui_vectors.h'


def number(value):
    result = float(value)
    if not (-10000 < result < 10000):
        raise ValueError('SVG coordinates must be finite and within the icon budget')
    return result


def vertices(path, view_box='0 0 40 44'):
    root = ET.parse(path).getroot()
    if root.tag != '{http://www.w3.org/2000/svg}svg' or root.get('viewBox') != view_box:
        raise ValueError(f'{path.name}: expected SVG with viewBox="{view_box}"')
    if set(root.attrib) - {'viewBox', 'width', 'height', 'version'}:
        raise ValueError(f'{path.name}: unsupported SVG root attributes')
    result = []
    for shape in root:
        kind = shape.tag.rsplit('}', 1)[-1]
        if kind in ('title', 'desc'):
            continue
        permitted = {'fill', 'id'}
        if kind == 'rect':
            permitted |= {'x', 'y', 'width', 'height'}
            x, y = number(shape.get('x', '0')), number(shape.get('y', '0'))
            width, height = number(shape.get('width')), number(shape.get('height'))
            if width <= 0 or height <= 0:
                raise ValueError(f'{path.name}: rectangles must have positive dimensions')
            points = [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]
        elif kind == 'polygon':
            permitted.add('points')
            coordinates = [number(v) for v in re.split(r'[\s,]+', shape.get('points', '').strip())]
            if len(coordinates) < 6 or len(coordinates) % 2:
                raise ValueError(f'{path.name}: invalid polygon coordinates')
            points = list(zip(coordinates[::2], coordinates[1::2]))
        else:
            raise ValueError(f'{path.name}: unsupported SVG element {kind}')
        if set(shape.attrib) - permitted or len(shape):
            raise ValueError(f'{path.name}: unsupported SVG attributes or children')
        fill = shape.get('fill', '')
        if not re.fullmatch(r'#[0-9a-fA-F]{6}', fill):
            raise ValueError(f'{path.name}: use an explicit #RRGGBB fill')
        colour = int(fill[1:], 16)
        turns = []
        for i, a in enumerate(points):
            b, c = points[(i + 1) % len(points)], points[(i + 2) % len(points)]
            turns.append((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]))
        if not (all(t > 0 for t in turns) or all(t < 0 for t in turns)):
            raise ValueError(f'{path.name}: use strictly convex polygons')
        for i in range(1, len(points) - 1):
            result.extend((x, y, colour) for x, y in (points[0], points[i], points[i + 1]))
        if len(result) > 1536:
            raise ValueError(f'{path.name}: icon exceeds the triangle budget')
    if not result:
        raise ValueError(f'{path.name}: icon is empty')
    return result


def literal(value):
    text = format(value, '.7g')
    if '.' not in text and 'e' not in text:
        text += '.0'
    return text + 'f'


def compile_header():
    icons = {name: vertices(ICONS / (name + '.svg')) for name in ('frame', 'play', 'pause', 'continue')}
    icons.update({name: vertices(ICONS / (name + '.svg'), '0 0 28 20')
                  for name in ('first_frame', 'previous_frame', 'next_frame', 'last_frame')})
    icons.update({name: vertices(ICONS / (name + '.svg'), '0 0 10 20')
                  for name in ('frame_slider', 'scene_slider')})
    icons['transport_readout'] = vertices(ICONS / 'transport_readout.svg', '0 0 164 20')
    icons.update({name: vertices(ICONS / (name + '.svg'), '0 0 46 43')
                  for name in ('dialog_frame', 'dialog_cancel', 'dialog_ok')})
    lines = [
        '/* Generated from src/marcus/icons/*.svg by tools/compile_marcus_vectors.py. */',
        '#ifndef MARCUS_UI_VECTORS_H', '#define MARCUS_UI_VECTORS_H',
        'typedef struct MarcusVectorVertex { float x, y; unsigned long colour; } MarcusVectorVertex;',
        '#define MARCUS_VECTOR_WIDTH 40.0f', '#define MARCUS_VECTOR_HEIGHT 44.0f',
        f'#define MARCUS_VECTOR_MAX_VERTICES {max(map(len, icons.values()))}',
    ]
    for name, values in icons.items():
        lines += [f'#define MARCUS_VECTOR_{name.upper()}_COUNT {len(values)}',
                  f'static const MarcusVectorVertex marcus_vector_{name}[] = {{']
        lines += [f'    {{{literal(x)}, {literal(y)}, 0x{colour:06x}UL}},' for x, y, colour in values]
        lines.append('};')
    return '\n'.join([*lines, '#endif', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Check generated data without writing files')
    args = parser.parse_args()
    try:
        generated = compile_header()
    except (ValueError, TypeError, ET.ParseError) as error:
        parser.exit(1, f'Cannot compile Marcus SVG artwork: {error}\n')
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != generated:
            parser.exit(1, 'Vector data is stale. Run: python tools/compile_marcus_vectors.py\n')
        print('Marcus SVG vector data is up to date.')
    else:
        with OUTPUT.open('w', encoding='utf-8', newline='\n') as output:
            output.write(generated)
        print('Generated ' + str(OUTPUT.relative_to(ROOT)))


if __name__ == '__main__':
    main()
