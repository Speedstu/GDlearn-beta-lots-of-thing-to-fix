#!/usr/bin/env python3
from pathlib import Path
p=Path('src/app/main.cpp')
s=p.read_text()
old='''  std::printf("frame  hold      x(bl)   y(bl)     vy  ground dead mode flip pad orb contact deathuid\\n");
'''
new='''  std::printf("frame  hold      x(bl)   y(bl)     vy  ground dead mode flip pad orb contact deathuid mini tier\\n");
'''
if old not in s: raise SystemExit('trace header anchor not found')
s=s.replace(old,new,1)
old2='''      std::printf("%5d  %s   %8.3f %7.3f %6.2f     %d    %d    %d    %d %d %d %d %d\\n", f,
                  hold ? "HOLD" : "....", s.x / phys::BLOCK,
                  s.y / phys::BLOCK, s.vy, s.onGround ? 1 : 0,
                  s.dead ? 1 : 0, static_cast<int>(s.mode), s.flip ? 1 : 0,
                  s.lastPadUid, s.lastOrbUid, sim.lastContactUid(), sim.deathUid());
'''
new2='''      std::printf("%5d  %s   %8.3f %7.3f %6.2f     %d    %d    %d    %d %d %d %d %d %d %d\\n", f,
                  hold ? "HOLD" : "....", s.x / phys::BLOCK,
                  s.y / phys::BLOCK, s.vy, s.onGround ? 1 : 0,
                  s.dead ? 1 : 0, static_cast<int>(s.mode), s.flip ? 1 : 0,
                  s.lastPadUid, s.lastOrbUid, sim.lastContactUid(), sim.deathUid(),
                  s.mini ? 1 : 0, static_cast<int>(s.tier));
'''
if old2 not in s: raise SystemExit('trace printf anchor not found')
s=s.replace(old2,new2,1)
p.write_text(s)
print('trace mini/tier v7 applied')
