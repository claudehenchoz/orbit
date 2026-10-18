#!/usr/bin/env python3
"""Generate assets on Windows, then compile with the JieLi toolchain in WSL.

No system packages are installed. --toolchain is an existing Linux path
(default: ~/.jieli/toolchain in the distro); --sdk is a Windows directory
containing cpu/wl82/tools from the pinned SDK (default: ~/fw-AC79_AIoT_SDK
in the distro).
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--distro', required=True)
    ap.add_argument('--toolchain')
    ap.add_argument('--sdk', type=Path)
    ap.add_argument('--skip-generate', action='store_true')
    ap.add_argument('--slice', choices=['0', '1'], help='SLICE engine and BREAK sample generation')
    args = ap.parse_args()
    os.chdir(ROOT)
    if args.slice is not None:
        os.environ['FELUCCA_SLICE'] = args.slice
    import build
    if not args.skip_generate:
        build.generate()
    wsl = ['wsl', '-d', args.distro, '--exec']
    def linux_path(p):
        return subprocess.check_output(wsl + ['wslpath', '-a', p.resolve().as_posix()], text=True).strip()
    home = subprocess.check_output(wsl + ['printenv', 'HOME'], text=True).strip()
    toolchain = args.toolchain or home + '/.jieli/toolchain'
    src = linux_path(ROOT)
    sdk = linux_path(args.sdk) if args.sdk else home + '/fw-AC79_AIoT_SDK'
    code = 'import sys;sys.path.insert(0,"tools");import build;build.generate=lambda:None;sys.exit(build.main())'
    extra = ['FELUCCA_SLICE=' + args.slice] if args.slice is not None else []
    subprocess.run(['wsl', '-d', args.distro, '--cd', src, '--exec', 'env',
                    'JIELI_TOOLCHAIN=' + toolchain, 'AC79_SDK=' + sdk, *extra,
                    'python3', '-c', code], check=True)

if __name__ == '__main__':
    main()

