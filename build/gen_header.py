#!/usr/bin/env python3
"""Generate prank_data.h — embeds the three prank files as C byte arrays."""
import os, sys

ROOT = os.path.join(os.path.dirname(__file__), '..')

FILES = [
    ('launch_prank_ps1',           os.path.join(ROOT, 'launch-prank.ps1')),
    ('windows_hacking_prank_html', os.path.join(ROOT, 'windows-hacking-prank.html')),
    ('bsod_prank_html',            os.path.join(ROOT, 'bsod-prank.html')),
]

out_path = os.path.join(os.path.dirname(__file__), 'prank_data.h')

with open(out_path, 'w') as out:
    out.write('#pragma once\n#include <stddef.h>\n\n')
    for varname, filepath in FILES:
        data = open(filepath, 'rb').read()
        out.write(f'static const unsigned char {varname}[] = {{\n')
        for i, b in enumerate(data):
            out.write(f'0x{b:02x},')
            if (i + 1) % 16 == 0:
                out.write('\n')
        out.write('\n};\n')
        out.write(f'static const size_t {varname}_len = {len(data)};\n\n')
        print(f'  embedded {varname}: {len(data):,} bytes')

print(f'Written: {out_path}')
