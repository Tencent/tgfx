#!/usr/bin/env python3
"""Extracts the corner-smoothing shader functions from Figma's minified shader dump.

Usage: python3 extract_figma_smooth_shader.py <shader_blocks.txt>
"""

import re
import sys


def extract(pattern, text):
    match = re.search(pattern, text)
    if not match:
        return None
    start = text.find('{', match.end() - 1)
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[match.start():i + 1]
    return None


def split_statements(body):
    out = []
    indent = 0
    line = ''
    for ch in body:
        if ch == '{':
            if line.strip():
                out.append('    ' * indent + line.strip())
            line = ''
            out.append('    ' * indent + '{')
            indent += 1
        elif ch == '}':
            if line.strip():
                out.append('    ' * indent + line.strip())
            line = ''
            indent = max(0, indent - 1)
            out.append('    ' * indent + '}')
        elif ch == ';':
            line += ';'
            out.append('    ' * indent + line.strip())
            line = ''
        else:
            line += ch
    if line.strip():
        out.append('    ' * indent + line.strip())
    return '\n'.join(out)


TARGETS = [
    ('box SDF', r'float d\(vec2 [a-z]{1,2},vec2 [a-z]{1,2}\)\{'),
    ('standard rounded-rect SDF', r'float h\(vec2 [a-z]{1,2},vec2 [a-z]{1,2},float [a-z]{1,2}\)\{'),
    ('rotation matrix', r'mat2 n\('),
    ('cubic equation solver', r'int u\('),
    ('winding sign test', r'float aj\('),
    ('newton iteration step', r'float ba\(float'),
    ('squared distance to cubic', r'float bm\('),
    ('distance to cubic', r'float bq\(vec2'),
    ('smooth corner SDF', r'float br\(vec2 ak,float'),
    ('per-corner dispatch with degradation', r'float ch\(vec2 ak,vec2 f,float'),
    ('four-corner union', r'float cn\(vec2'),
]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    text = open(sys.argv[1], errors='ignore').read()
    for label, pattern in TARGETS:
        body = extract(pattern, text)
        print('// ===== %s =====' % label)
        print(split_statements(body) if body else '// NOT FOUND')
        print()
    for name in ('r', 's', 't'):
        anchor = text.find('float br(vec2 ak,float')
        window = text[max(0, anchor - 4000):anchor]
        found = re.search(r'const\s+(?:int|float)\s+%s\s*=\s*([^;]+);' % name, window)
        if found:
            print('// const %s = %s' % (name, found.group(1)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
