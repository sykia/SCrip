#!/usr/bin/env python3
"""Reject developer-machine dependencies in a deployed macOS application."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

bundle = Path(sys.argv[1]).resolve()
architecture = sys.argv[2]
magic = {bytes.fromhex(value) for value in (
    'feedface', 'cefaedfe', 'feedfacf', 'cffaedfe', 'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}
records = []
for path in sorted(bundle.rglob('*')):
    if not path.is_file() or path.is_symlink():
        continue
    with path.open('rb') as stream:
        if stream.read(4) not in magic:
            continue
    subprocess.run(['lipo', str(path), '-verify_arch', architecture], check=True)
    output = subprocess.check_output(['otool', '-L', str(path)], text=True)
    dependencies = []
    for line in output.splitlines():
        if '(compatibility version' not in line:
            continue
        dependency = line.strip().split(' (compatibility version', 1)[0]
        if not dependency.startswith(('@rpath/', '@loader_path/', '@executable_path/', '/System/Library/', '/usr/lib/')):
            raise SystemExit(f'External dependency in {path}: {dependency}')
        dependencies.append(dependency)
    records.append({'path': str(path.relative_to(bundle)), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                    'dependencies': dependencies})
if not records:
    raise SystemExit('No Mach-O binaries found')
print(json.dumps({'architecture': architecture, 'binaries': records}, indent=2))
