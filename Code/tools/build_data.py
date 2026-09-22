#!/usr/bin/python3
"""Build the LittleFS content (used by gzip_littlefs.py and by tools/make_release.py).

  build_data(data_dir)          render webInterface + copy data_base into data_dir
  gzip_data(data_dir, out_dir)  copy data_dir to out_dir, gzipping what makes sense,
                                and write filelist.txt next to the copies
"""

import gzip
import os
import pathlib
import shutil
import sys

CODE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if CODE_DIR not in sys.path:
    sys.path.insert(0, CODE_DIR)

from webInterface.build import build_web_interface

FILELIST = "filelist.txt"
# these are worth compressing, everything else is copied as it is
GZIP_EXTENSIONS = ("js", "css", "html", "ico")


def build_data(data_dir, web_interface_base_dir=None, data_base_dir=None):
    """render the web interface and add the static files from data_base"""
    if web_interface_base_dir is None:
        web_interface_base_dir = os.path.join(CODE_DIR, "webInterface")
    if data_base_dir is None:
        data_base_dir = data_dir + "_base"

    if not os.path.exists(data_dir):
        os.makedirs(data_dir)

    print(f"Building web interface from {web_interface_base_dir} into {data_dir}")
    build_web_interface(data_dir, web_interface_base_dir)

    if os.path.exists(data_base_dir):
        print("Copy base data files")
        for item in os.listdir(data_base_dir):
            src = os.path.join(data_base_dir, item)
            dst = os.path.join(data_dir, item)
            if os.path.isdir(src):
                shutil.copytree(src, dst, dirs_exist_ok=True)
            else:
                shutil.copy2(src, dst)
    return data_dir


def _copy_data(src, dst):
    """copy one file, gzipping it when useful, and note the result in filelist.txt"""
    ext = pathlib.Path(src).suffix[1:]
    path, file = os.path.split(dst)

    with open(os.path.join(path, FILELIST), "a") as filelist:
        if ext in GZIP_EXTENSIONS:
            filelist.write(file + ".gz\n")
            with open(src, "rb") as fsrc, gzip.open(dst + ".gz", "wb") as fdst:
                for chunk in iter(lambda: fsrc.read(4096), b""):
                    fdst.write(chunk)
        else:
            filelist.write(file + "\n")
            shutil.copy(src, dst)


def gzip_data(data_dir, out_dir):
    """copy data_dir to out_dir, compressing html/js/css/ico on the way"""
    print(f"zipping data files into {out_dir}")
    shutil.rmtree(out_dir, True)
    shutil.copytree(data_dir, out_dir, copy_function=_copy_data)
    return out_dir


def del_data(data_dir):
    shutil.rmtree(data_dir, True)
