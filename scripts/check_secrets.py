#!/usr/bin/env python3
"""Проверка staged или отслеживаемых файлов без вывода значений секретов."""
import argparse
import os
import pathlib
import re
import subprocess
import sys


def git(*args):
    return subprocess.check_output(["git", *args], stderr=subprocess.PIPE)


ASSIGNMENT = re.compile(
    rb'''(?ix)(?:client[_-]?secret|soundcloud[_-]?secret|access[_-]?token|
    refresh[_-]?token|(?:api|auth|bearer)[_-]?token|api[_-]?key|private[_-]?key)
    ["']?\s*[:=]\s*["']?([^\s"'`,;}{]{20,})'''
)
PRIVATE_KEY = re.compile(rb"-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY-----")
KNOWN_TOKEN = re.compile(
    rb"(?i)(?:gh[pousr]_[a-z0-9]{20,}|github_pat_[a-z0-9_]{20,}|"
    rb"xox[baprs]-[a-z0-9-]{20,}|sk_live_[a-z0-9]{20,})"
)
AWS_KEY = re.compile(rb"AKIA[0-9A-Z]{16}")
PLACEHOLDER = re.compile(
    rb"(?i)(?:^|[^a-z0-9])(?:test|dummy|example|sample|placeholder|changeme|"
    rb"your[_-]?|fake|redacted|none|null)(?:[^a-z0-9]|$)|^(?:x+|0+|\*+)$"
)


def sensitive_name(name):
    path = pathlib.PurePosixPath(name)
    basename = path.name.lower()
    return (
        (basename == ".env" or basename.startswith(".env."))
        and basename not in {".env.example", ".env.template"}
    ) or path.suffix.lower() in {".pem", ".key", ".p12", ".pfx", ".jks"} or any(
        part.lower() in {"secrets", "credentials"} for part in path.parts
    ) or basename in {"credentials", "credentials.json", "secrets.json"}


def _is_real_value(value):
    value = value.rstrip(b".)]")
    lowered = value.lower()
    if PLACEHOLDER.search(value):
        return False
    if (b"${" in value or b"$env:" in lowered or b"os.getenv" in lowered
            or b"getenv(" in lowered or b"qenvironmentvariable(" in lowered
            or re.fullmatch(rb"(?:env\.)?[A-Z][A-Z0-9_]*", value)):
        return False
    # Test fixtures use deliberately short labels such as test-access; real tokens
    # and secrets are long enough to be useful findings.
    return len(value) >= 20


def content_has_secret(content):
    if PRIVATE_KEY.search(content) or KNOWN_TOKEN.search(content) or AWS_KEY.search(content):
        return True
    for match in ASSIGNMENT.finditer(content):
        if _is_real_value(match.group(1)):
            return True
    return False


def scan_files(files):
    """files maps repository-relative names to bytes; returns names only."""
    rejected = []
    for name, content in files.items():
        if sensitive_name(name) or content_has_secret(content):
            rejected.append(name)
    return rejected


def _file_map(mode):
    if mode == "staged":
        names = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z")
    else:
        names = git("ls-files", "-z")
    files = {}
    for raw_name in names.split(b"\0"):
        if not raw_name:
            continue
        name = os.fsdecode(raw_name)
        if mode == "staged":
            files[name] = git("show", ":" + name)
        else:
            path = pathlib.Path(name)
            if path.is_file() and not path.is_symlink():
                try:
                    files[name] = path.read_bytes()
                except OSError:
                    continue
    return files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("staged", "tracked"), default="staged")
    args = parser.parse_args(argv)
    try:
        root = git("rev-parse", "--show-toplevel").decode().strip()
        os.chdir(root)
        rejected = scan_files(_file_map(args.mode))
    except (subprocess.CalledProcessError, OSError):
        print("Проверка невозможна: Git-репозиторий недоступен.", file=sys.stderr)
        return 2
    if rejected:
        print("Проверка остановлена: возможные секреты в файлах:", file=sys.stderr)
        for name in rejected:
            print(f"  {name!r}", file=sys.stderr)
        print("Уберите секреты; значения обнаружений не выводятся.", file=sys.stderr)
        return 1
    print(f"Проверка {args.mode}: типовые признаки секретов не найдены.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
