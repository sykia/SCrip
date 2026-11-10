#!/usr/bin/env python3
"""Восстановить материалы частичного выпуска из проверенного базового архива."""
import argparse
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import urllib.request
from check_licenses import digest, local_file


def prepare(release_dir, output, bundle, assets=None):
    info = json.loads((release_dir / 'release.json').read_text())
    if output.exists() and any(output.iterdir()):
        raise ValueError('Каталог материалов должен быть пустым')
    base = info['base_materials']
    if not base['url'].startswith('https://github.com/sykia/SCrip/releases/download/'):
        raise ValueError('Неизвестный адрес базовых материалов')
    bundle.parent.mkdir(parents=True, exist_ok=True)
    if not bundle.exists():
        with urllib.request.urlopen(base['url'], timeout=120) as response, bundle.open('wb') as target:
            shutil.copyfileobj(response, target)
    if digest(bundle) != base['sha256']:
        raise ValueError('SHA-256 базовых материалов не совпадает')
    output.mkdir(parents=True, exist_ok=True)
    with tarfile.open(bundle) as archive:
        archive.extractall(output, filter='data')
    # Новые исходники и манифесты можно поставлять вложением релиза,
    # сохраняя бинарные архивы вне Git-репозитория.
    overlay = info.get('materials_overlay')
    temporary = None
    if overlay:
        if assets is None:
            raise ValueError('Для дополнения материалов нужен каталог --assets')
        archive_path = local_file(assets, overlay['name'])
        if digest(archive_path) != overlay['sha256']:
            raise ValueError('SHA-256 дополнения материалов не совпадает')
        temporary = tempfile.TemporaryDirectory()
        overlay_dir = Path(temporary.name)
        with tarfile.open(archive_path) as archive:
            archive.extractall(overlay_dir, filter='data')
        release_dir = overlay_dir
    platforms = info['platforms']
    if not platforms or not set(platforms) <= {'Android', 'Windows', 'Linux-DEB', 'Linux-Arch'}:
        raise ValueError('Неизвестный набор платформ')
    for platform in platforms:
        target = output / platform
        target.mkdir(exist_ok=True)
        shutil.copy2(local_file(release_dir, platform + '-compliance.json'), target / 'compliance.json')
        manifest = json.loads((target / 'compliance.json').read_text())
        for component in manifest['components'].values():
            source = Path(component['source'])
            if source.is_absolute() or '..' in source.parts:
                raise ValueError('Небезопасный путь исходников')
            replacement = release_dir / source
            if replacement.is_file():
                target_file = target / source
                target_file.parent.mkdir(parents=True, exist_ok=True)
                # Базовый tar может содержать hardlink: не менять соседние платформы.
                target_file.unlink(missing_ok=True)
                shutil.copy2(local_file(release_dir, str(source)), target_file)
        print(platform)
    if temporary is not None:
        temporary.cleanup()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--assets', type=Path)
    args = parser.parse_args()
    prepare(args.release_dir, args.output, args.bundle, args.assets)
