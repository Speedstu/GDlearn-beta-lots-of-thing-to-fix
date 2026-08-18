#!/usr/bin/env python3
from pathlib import Path

p=Path('tools/gmd_to_gdl.py')
s=p.read_text()
old="""        else:
            pw, ph = 12.0, 45.0
        return GdlObject('portal', PORTALS[oid], x, y, pw * sx, ph * sy, oid)
"""
new="""        else:
            pw, ph = 12.0, 45.0
        # Pathfinder/GD rotates the portal collision rectangle with the object.
        # Keeping 90-degree portals vertical makes their trigger volume far too
        # tall (ToE2 ID11 at x=2295 fires ~8 ticks early).  The current .gdl
        # format is axis-aligned, so preserve exact cardinal rotations and use
        # the conservative rotated AABB for non-cardinal cases until OBB data is
        # represented explicitly in the core format.
        pw, ph = _rotated_half_extents(pw * sx, ph * sy, rot)
        return GdlObject('portal', PORTALS[oid], x, y, pw, ph, oid)
"""
if old not in s: raise SystemExit('portal conversion anchor not found')
s=s.replace(old,new,1)
p.write_text(s)
print('portal rotation v6 applied')
