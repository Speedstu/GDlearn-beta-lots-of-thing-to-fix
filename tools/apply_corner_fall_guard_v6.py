#!/usr/bin/env python3
from pathlib import Path

p = Path('src/core/sim.cpp')
s = p.read_text()
old = '''      const float pen = (surfaceTop - (st_.y - hh * g)) * g;
      if (pen > 0.0f && pen <= hh) {
'''
new = '''      const float pen = (surfaceTop - (st_.y - hh * g)) * g;
      // Pathfinder only allows a top-face snap while moving toward the
      // support surface (velocity <= 0 in gravity-relative coordinates).
      // Without this guard a rising cube gets magnetically pulled onto the
      // next platform a couple of ticks before reaching its apex.
      if (falling && pen > 0.0f && pen <= hh) {
'''
if old not in s:
    raise SystemExit('corner forgiveness anchor not found')
s = s.replace(old, new, 1)
p.write_text(s)
print('corner fall guard v6 applied')
