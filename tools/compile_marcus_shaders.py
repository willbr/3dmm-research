"""Regenerate Marcus's bundled SDL GPU shaders with the official DXC compiler."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dxc', default='dxc')
args = parser.parse_args()
folder = Path(__file__).resolve().parents[1] / 'src/marcus/shaders'
for stage, define, profile in [('compute', 'COMPUTE', 'cs_6_0'),
                                ('vertex', 'VERTEX', 'vs_6_0'),
                                ('fragment', 'FRAGMENT', 'ps_6_0')]:
    for fmt in ['dxil', 'spv']:
        command = [args.dxc, str(folder / 'raster.hlsl'), '-E', 'main', '-T', profile,
                   '-D', define, '-O3', '-Fo', str(folder / f'{stage}.{fmt}')]
        if fmt == 'spv':
            command += ['-spirv', '-fspv-target-env=vulkan1.0']
        subprocess.run(command, check=True)
