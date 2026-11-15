#!/usr/bin/env python3
"""Embed editable QML into an installable SCrip JSON manifest (stdlib only)."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="Manifest template with one runtime contribution")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="Output path, normally under build/")
    args = parser.parse_args()
    document = json.loads(args.manifest.read_text(encoding="utf-8"))
    runtimes = [c for c in document["contributions"] if c["type"] == "runtime"]
    if document.get("kind") != "qml" or len(runtimes) != 1:
        parser.error("expected kind=qml and exactly one runtime contribution")
    runtimes[0]["source"] = args.source.read_text(encoding="utf-8")
    data = (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if len(data) > 128 * 1024:
        parser.error("package exceeds 128 KiB")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)


if __name__ == "__main__":
    main()
