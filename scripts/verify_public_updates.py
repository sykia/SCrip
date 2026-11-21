import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import time
import urllib.parse
import urllib.request

parser = argparse.ArgumentParser(description='Verify complete public updater downloads for all six release packages.')
parser.add_argument('--tag', required=True)
parser.add_argument('--output', type=Path, default=Path('build/public-update-check'))
args = parser.parse_args()
assert re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+', args.tag)
root = args.output
root.mkdir(parents=True, exist_ok=True)
headers = {'Accept': 'application/vnd.github+json', 'Accept-Encoding': 'identity', 'User-Agent': 'Sange-release-verification'}
url = f'https://api.github.com/repos/sykia/sange/releases/latest?verification={int(time.time())}'
with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60) as response:
    metadata = json.load(response)
assert metadata['tag_name'] == args.tag and not metadata['draft'] and not metadata['prerelease']
(root / 'public-latest.json').write_text(json.dumps(metadata, indent=2) + '\n')
suffixes = {'Windows': '-Windows-x64.exe', 'Debian': '-amd64.deb', 'Arch': '-x86_64.pkg.tar.zst',
            'Android': '-Android-universal.apk', 'macOS-arm64': '-macOS-arm64.dmg', 'macOS-x86_64': '-macOS-x86_64.dmg'}
selected = []
for platform, suffix in suffixes.items():
    candidates = [a for a in metadata['assets'] if a['name'].endswith(suffix)]
    assert len(candidates) == 1, (platform, candidates)
    asset = candidates[0]
    assert args.tag[1:] in asset['name'] and Path(asset['name']).name == asset['name']
    assert 0 < asset['size'] <= 1024**3 and re.fullmatch(r'sha256:[0-9a-f]{64}', asset['digest'])
    address = urllib.parse.urlsplit(asset['browser_download_url'])
    assert address.scheme == 'https' and address.netloc == 'github.com'
    assert address.path.startswith('/sykia/sange/releases/download/' + args.tag + '/')
    selected.append((platform, asset))

def verify(entry):
    platform, asset = entry
    for attempt in range(3):
        try:
            request = urllib.request.Request(asset['browser_download_url'], headers=headers)
            with urllib.request.urlopen(request, timeout=60) as response:
                final = urllib.parse.urlsplit(response.url)
                assert response.status == 200 and final.scheme == 'https'
                assert final.hostname == 'github.com' or final.hostname.endswith('.githubusercontent.com')
                length = response.headers.get('Content-Length')
                assert length is None or int(length) == asset['size']
                checksum, size = hashlib.sha256(), 0
                while chunk := response.read(1024 * 1024):
                    checksum.update(chunk)
                    size += len(chunk)
                    assert size <= asset['size']
            assert size == asset['size'] and 'sha256:' + checksum.hexdigest() == asset['digest']
            print(platform, asset['name'], 'verified complete public download', flush=True)
            return {'platform': platform, 'name': asset['name'], 'size': size, 'digest': asset['digest'], 'status': 'verified public download'}
        except Exception:
            if attempt == 2:
                raise
            time.sleep(2)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    results = list(pool.map(verify, selected))
(root / 'public-update-verification.json').write_text(json.dumps(results, indent=2) + '\n')
for result in results:
    print(result['platform'], result['name'], result['size'], result['digest'], 'OK')
