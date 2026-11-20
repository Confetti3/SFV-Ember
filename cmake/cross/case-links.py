#!/usr/bin/env python3
"""Windows finds import libraries case-insensitively; the xwin splat has them in
lower case only. For every word in the sources that names one of them in
another case (Bcrypt, AdvApi32, ...), a link of that case is added beside it in
the splat. The sources are only read. scripts/build.ps1 runs it on Linux.

Usage: case-links.py <splat> <source folder> [<source folder>...]
"""
import pathlib
import re
import sys

splat = pathlib.Path(sys.argv[1])
words = set()
word = re.compile(rb"[A-Za-z][A-Za-z0-9_]{2,}")
for src in sys.argv[2:]:
    for path in pathlib.Path(src).rglob("*"):
        if path.suffix in (".cmake", ".txt", ".cxx", ".cpp", ".hxx", ".h", ".rs", ".toml") and path.is_file() and ".git" not in path.parts:
            words.update(w.decode() for w in word.findall(path.read_bytes()))
made = 0
for libdir in list(splat.glob("sdk/lib/*/*")) + list(splat.glob("crt/lib/*")):
    present = {p.name for p in libdir.iterdir()}
    for w in words:
        lower = w.lower() + ".lib"
        variant = w + ".lib"
        if lower in present and variant not in present and variant != lower:
            (libdir / variant).symlink_to(lower)
            made += 1
print(f"case links made: {made}")
