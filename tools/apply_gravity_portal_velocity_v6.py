#!/usr/bin/env python3
from pathlib import Path

p=Path('src/core/sim.cpp')
s=p.read_text()
old="""    case PortalKind::GravityNormal: st_.flip = false; break;
    case PortalKind::GravityFlip: st_.flip = true; break;
"""
new="""    case PortalKind::GravityNormal:
      if (st_.flip) {
        // Pathfinder GravityPortal: relative velocity becomes -v/2 while the
        // gravity basis flips. In gdlearn's world-space vy this preserves the
        // direction of travel and halves the magnitude.
        st_.vy *= 0.5f;
        st_.flip = false;
      }
      break;
    case PortalKind::GravityFlip:
      if (!st_.flip) {
        st_.vy *= 0.5f;
        st_.flip = true;
      }
      break;
"""
if old not in s: raise SystemExit('gravity portal anchor not found')
s=s.replace(old,new,1)
p.write_text(s)
print('gravity portal velocity v6 applied')
