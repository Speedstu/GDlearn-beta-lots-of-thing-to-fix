#!/usr/bin/env python3
"""Replay one gdlearn macro in gdlearn and Pathfinder and report first drift."""
from __future__ import annotations
import argparse, pathlib, shutil, subprocess, sys, textwrap

ROOT=pathlib.Path(__file__).resolve().parents[1]

def run(cmd, **kw):
    print('+', ' '.join(map(str,cmd)), flush=True)
    return subprocess.run(list(map(str,cmd)), check=True, **kw)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--bin', default=str(ROOT/'build/gdlearn'))
    ap.add_argument('--gmd', default=str(ROOT/'levels/18.gmd'))
    ap.add_argument('--name', default='Theory of Everything 2')
    ap.add_argument('--work', default='/tmp/gdlearn-pathfinder-diff')
    ap.add_argument('--beam', type=int, default=6144)
    ap.add_argument('--frames', type=int, default=3000)
    args=ap.parse_args()
    w=pathlib.Path(args.work); shutil.rmtree(w,ignore_errors=True); w.mkdir(parents=True)
    gdl=w/'level.gdl'; macro=w/'policy.macro'; gdtrace=w/'gd.trace'
    run([sys.executable,ROOT/'tools/gmd_to_gdl.py',args.gmd,'-o',gdl,'--name',args.name])
    with open(w/'solve.log','w') as f:
        subprocess.run(list(map(str,[args.bin,'solve',gdl,'--beam',args.beam,'--widen','0','--max-frames',args.frames,'--stall','2000','--out',macro])),stdout=f)
    with open(gdtrace,'w') as f: run([args.bin,'trace',gdl,'--macro',macro,'--frames',args.frames],stdout=f)

    sys.path.insert(0,str(ROOT/'tools')); import gmd_to_gdl as conv
    dec=conv.try_decompress(pathlib.Path(args.gmd).read_bytes())
    if not dec: raise SystemExit('cannot decode gmd')
    (w/'level.txt').write_bytes(dec)
    acts=[]
    for line in macro.read_text().splitlines():
        p=line.split()
        if len(p)==3 and p[0]=='r': acts += [p[1]]*int(p[2])
    (w/'actions.txt').write_text(''.join(acts))

    pf=w/'pathfinder'
    run(['git','clone','--depth','1','https://github.com/camila314/pathfinder',pf])
    pfb=w/'pf-build'; run(['cmake','-S',pf/'gd-sim','-B',pfb,'-DCMAKE_BUILD_TYPE=Release'],stdout=subprocess.DEVNULL)
    run(['cmake','--build',pfb,'-j2'],stdout=subprocess.DEVNULL)
    cpp=w/'ref.cpp'; cpp.write_text(textwrap.dedent(r'''
        #include <Level.hpp>
        #include <fstream>
        #include <iostream>
        #include <iterator>
        int main(int argc,char**argv){
          std::ifstream f(argv[1]); std::string l((std::istreambuf_iterator<char>(f)),{});
          std::ifstream af(argv[2]); std::string a; af>>a; Level lv(l);
          for(size_t i=0;i<a.size();++i){ auto&p=lv.runFrame(a[i]=='1',1.0f/240.0f);
            double wvy=(p.upsideDown?-p.velocity:p.velocity)/240.0;
            std::cout<<i<<" "<<p.pos.x<<" "<<p.pos.y<<" "<<wvy<<" "<<(int)p.vehicle.type<<" "<<p.upsideDown<<" "<<p.grounded<<" "<<p.dead<<"\n";
            if(p.dead)break; }
        }
    '''))
    refbin=w/'ref'; run(['g++','-O2','-std=c++20',cpp,'-I'+str(pf/'gd-sim/include'),pfb/'libgd-sim.a','-o',refbin])
    reftrace=w/'ref.trace'
    with open(reftrace,'w') as f: run([refbin,w/'level.txt',w/'actions.txt'],stdout=f)

    gd={}
    for l in gdtrace.read_text().splitlines():
        p=l.split()
        if len(p)>=9 and p[0].isdigit(): gd[int(p[0])]=(float(p[2])*30,float(p[3])*30,float(p[4]),int(p[7]),int(p[8]))
    ref={}
    for l in reftrace.read_text().splitlines():
        p=l.split()
        if len(p)>=8: ref[int(p[0])]=(float(p[1]),float(p[2]),float(p[3]),int(p[4]),int(p[5]),int(p[7]))
    first=None
    for i in sorted(set(gd)&set(ref)):
        g=gd[i]; r=ref[i]; delta=(abs(g[0]-r[0]),abs(g[1]-r[1]),abs(g[2]-r[2]))
        if delta[0]>.15 or delta[1]>.15 or delta[2]>.03 or g[3]!=r[3] or g[4]!=r[4]:
            first=(i,g,r,delta); break
    print('FIRST_DIVERGENCE',first)
    if first:
        i=first[0]
        print('GD_WINDOW'); [print(j,gd[j]) for j in range(max(0,i-8),i+9) if j in gd]
        print('REF_WINDOW'); [print(j,ref[j]) for j in range(max(0,i-8),i+9) if j in ref]
    print('FRAMES',{'gd':len(gd),'pathfinder':len(ref),'actions':len(acts)})
    return 0

if __name__=='__main__': raise SystemExit(main())
