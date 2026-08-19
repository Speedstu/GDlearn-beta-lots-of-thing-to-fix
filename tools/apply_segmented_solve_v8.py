#!/usr/bin/env python3
from pathlib import Path
p=Path('src/app/main.cpp')
s=p.read_text()
old='''  PolicyRunner guide;\n  if (a.has("policy")) {'''
new='''  PolicyRunner guide;\n  if (a.has("policy")) {'''
if old not in s: raise SystemExit('policy anchor missing')
# Insert prefix setup after policy block, anchored before t0.
anchor='''  const auto t0 = std::chrono::steady_clock::now();\n  SolveResult r = beamSolve(lv, o);'''
insert='''  std::vector<uint8_t> fixedPrefix;\n  if (a.has("prefix-macro")) {\n    Macro pm;\n    const std::string prefixPath = a.str("prefix-macro");\n    if (!Macro::load(prefixPath, &pm)) {\n      std::printf("cannot load prefix macro: %s\\n", prefixPath.c_str());\n      return 2;\n    }\n    const int back = std::max(0, a.num("prefix-back", phys::ticks(2.0f)));\n    const int cut = std::max(0, static_cast<int>(pm.holds.size()) - back);\n    Sim prefixSim(&lv);\n    for (int i = 0; i < cut; ++i) {\n      if (!prefixSim.step(pm.holds[static_cast<size_t>(i)] != 0)) {\n        std::printf("prefix macro dies before checkpoint at tick %d\\n", i);\n        return 2;\n      }\n    }\n    fixedPrefix.assign(pm.holds.begin(), pm.holds.begin() + cut);\n    o.hasStart = true;\n    o.start = prefixSim.state();\n    std::printf("segment checkpoint: tick %d/%zu  progress %.2f%%  back %d ticks\\n",\n                cut, pm.holds.size(), prefixSim.progress() * 100.0f, back);\n  }\n\n  const auto t0 = std::chrono::steady_clock::now();\n  SolveResult r = beamSolve(lv, o);'''
if anchor not in s: raise SystemExit('t0 anchor missing')
s=s.replace(anchor,insert,1)
old2='''  VerifyResult v = verifyMacro(lv, r.holds);\n  std::printf(\n      "%s  progress %.2f%%  frames %d  expanded %lldk  %.1fs  verify %.2f%%\\n",\n      r.solved ? "SOLVED" : "partial", r.progress * 100.0f, r.frames,\n      static_cast<long long>(r.expanded / 1000), sec, v.progress * 100.0f);\n\n  const std::string out = a.str("out", a.pos[0] + ".macro");\n  Macro m;\n  m.level = lv.name;\n  m.progress = v.progress;\n  m.holds = r.holds;\n  if (m.save(out)) std::printf("macro -> %s\\n  %s\\n", out.c_str(),\n                               m.summary().c_str());\n  return r.solved ? 0 : 1;'''
new2='''  std::vector<uint8_t> fullHolds = fixedPrefix;\n  fullHolds.insert(fullHolds.end(), r.holds.begin(), r.holds.end());\n  VerifyResult v = verifyMacro(lv, fullHolds);\n  std::printf(\n      "%s  progress %.2f%%  suffix_frames %d  total_frames %zu  expanded %lldk  %.1fs  verify %.2f%%\\n",\n      v.solved ? "SOLVED" : "partial", v.progress * 100.0f, r.frames,\n      fullHolds.size(), static_cast<long long>(r.expanded / 1000), sec,\n      v.progress * 100.0f);\n\n  const std::string out = a.str("out", a.pos[0] + ".macro");\n  Macro m;\n  m.level = lv.name;\n  m.progress = v.progress;\n  m.holds = std::move(fullHolds);\n  if (m.save(out)) std::printf("macro -> %s\\n  %s\\n", out.c_str(),\n                               m.summary().c_str());\n  return v.solved ? 0 : 1;'''
if old2 not in s: raise SystemExit('verify/output anchor missing')
s=s.replace(old2,new2,1)
# Usage string.
s=s.replace('''[--prior-weight X] [--out macro]\\n");''','''[--prior-weight X] [--prefix-macro file] [--prefix-back ticks] [--out macro]\\n");''',1)
p.write_text(s)
print('segmented solve CLI patch applied')
