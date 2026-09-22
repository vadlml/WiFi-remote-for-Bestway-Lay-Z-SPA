#!/usr/bin/python3
"""Assemble the folder that the ESP (or the browser) downloads updates from.

    python3 tools/make_release.py [--firmware .pio/build/nodemcuv2/firmware.bin] [--out fw]

Result:

    <out>/manifest.json   {"version": ..., "size": ..., "md5": ...}
    <out>/firmware.bin
    <out>/filelist.txt    one web file per line
    <out>/data/<file>     the web files, gzipped where useful

Publish that folder on a branch so it is reachable through
raw.githubusercontent.com/<owner>/<repo>/<branch>/<out>/ - that is the url the
firmware update page asks for.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
CODE_DIR = os.path.dirname(TOOLS_DIR)
for path in (TOOLS_DIR, CODE_DIR):
    if path not in sys.path:
        sys.path.insert(0, path)

from build_data import build_data, gzip_data, FILELIST

DEFAULT_FIRMWARE = os.path.join(CODE_DIR, ".pio", "build", "nodemcuv2", "firmware.bin")
VERSION_HEADER = os.path.join(CODE_DIR, "lib", "BWC_unified", "FW_VERSION.h")


def read_version(header=VERSION_HEADER):
    """the version the firmware reports, so the ESP can compare it with its own"""
    with open(header) as fh:
        match = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', fh.read())
    if not match:
        raise SystemExit(f"no FW_VERSION in {header}")
    return match.group(1)


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", default=DEFAULT_FIRMWARE, help="the built firmware.bin")
    parser.add_argument("--out", default=os.path.join(CODE_DIR, "fw"), help="output folder")
    parser.add_argument("--version", default=None, help="override the version string")
    parser.add_argument("--no-data", action="store_true", help="firmware only, no web files")
    args = parser.parse_args()

    if not os.path.isfile(args.firmware):
        raise SystemExit(f"{args.firmware} not found - build the firmware first")

    version = args.version or read_version()
    out_dir = args.out
    shutil.rmtree(out_dir, True)
    os.makedirs(out_dir)

    shutil.copy2(args.firmware, os.path.join(out_dir, "firmware.bin"))
    size = os.path.getsize(args.firmware)
    manifest = {
        "version": version,
        "size": size,
        "md5": md5_of(args.firmware),
    }

    if not args.no_data:
        with tempfile.TemporaryDirectory() as tmp:
            data_dir = os.path.join(tmp, "data")
            build_data(data_dir, data_base_dir=os.path.join(CODE_DIR, "data_base"))
            gzip_data(data_dir, os.path.join(out_dir, "data"))
        # the firmware reads the list from <out>/filelist.txt, next to the manifest
        shutil.move(os.path.join(out_dir, "data", FILELIST), os.path.join(out_dir, FILELIST))
        with open(os.path.join(out_dir, FILELIST)) as fh:
            manifest["files"] = len([line for line in fh if line.strip()])

    with open(os.path.join(out_dir, "manifest.json"), "w") as fh:
        json.dump(manifest, fh)
        fh.write("\n")

    print(f"{out_dir}: version {version}, firmware {size} bytes, md5 {manifest['md5']}")
    if "files" in manifest:
        print(f"{out_dir}: {manifest['files']} web files")


if __name__ == "__main__":
    main()
