"""Compile every production C translation unit with GCC's static analyzer."""
import argparse
from pathlib import Path
import subprocess
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / 'tests/runs' / ('analysis-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    output.mkdir(parents=True)
    sources = sorted((root / 'src').glob('*.c')) + sorted((root / 'src/gui').glob('*.c'))
    for source in sources:
        print('Analyze', source.relative_to(root).as_posix(), flush=True)
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                        '-fanalyzer', '-I' + str(root / 'include'), '-c', str(source),
                        '-o', str(output / (source.stem + '.o'))], check=True, cwd=root)
    print(f'PASS GCC static analysis: {len(sources)} production C files; warnings treated as errors.')


if __name__ == '__main__':
    main()
