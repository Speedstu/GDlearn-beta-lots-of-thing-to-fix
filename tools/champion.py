#!/usr/bin/env python3
"""Monotonic gdlearn champion orchestrator.

The important property is not that training always improves; it is that a bad
update can never replace the last verified champion.

Loop:
  exact replay cache -> policy-guided beam -> DAgger curriculum -> strict eval
  -> optional PPO finisher -> strict eval -> atomic promotion.

A macro is admitted as an oracle only when `gdlearn replay` returns COMPLETE.
A candidate is promoted only when it does not lose any level the champion
already clears and its lexicographic score improves (clears, mean progress,
minimum progress).  An interrupted candidate directory is disposable; the
champion directory is never trained in place.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable, Optional

PROGRESS_RE = re.compile(r":\s*([0-9]+(?:\.[0-9]+)?)%")


@dataclass
class LevelScore:
    path: str
    progress: float
    won: bool


@dataclass
class PoolScore:
    clears: int
    mean: float
    minimum: float
    levels: list[LevelScore]

    @property
    def key(self):
        return (self.clears, self.mean, self.minimum)


def run(cmd: list[str], *, check: bool = False, capture: bool = False,
        env: Optional[dict[str, str]] = None) -> subprocess.CompletedProcess:
    print("+", " ".join(map(str, cmd)), flush=True)
    return subprocess.run(
        cmd,
        check=check,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
        env=env,
    )


def collect_levels(dirs: Iterable[Path], merged: Path) -> list[Path]:
    merged.mkdir(parents=True, exist_ok=True)
    for old in merged.glob("*.gdl"):
        old.unlink()
    found: list[Path] = []
    stems: dict[str, Path] = {}
    for root in dirs:
        if not root.exists():
            continue
        files = [root] if root.is_file() else sorted(root.rglob("*.gdl"))
        for src in files:
            stem = src.stem
            if stem in stems and stems[stem].resolve() != src.resolve():
                raise RuntimeError(
                    f"duplicate .gdl stem {stem!r}: {stems[stem]} and {src}"
                )
            stems[stem] = src
            dst = merged / f"{stem}.gdl"
            try:
                os.symlink(src.resolve(), dst)
            except (OSError, NotImplementedError):
                shutil.copy2(src, dst)
            found.append(dst)
    if not found:
        raise RuntimeError("no .gdl levels found")
    return sorted(found)


def valid_checkpoint(path: Path) -> bool:
    return all((path / name).is_file() for name in
               ("policy.bin", "obs_norm.bin", "schema.txt"))


def macro_path(macros: Path, level: Path) -> Path:
    return macros / f"{level.stem}.macro"


def verify_macro(binary: str, level: Path, macro: Path) -> bool:
    if not macro.is_file():
        return False
    cp = run([binary, "replay", str(level), str(macro)], capture=True)
    if cp.stdout:
        print(cp.stdout, end="")
    return cp.returncode == 0 and "COMPLETE" in (cp.stdout or "")


def audit_macros(binary: str, levels: list[Path], macros: Path) -> set[str]:
    macros.mkdir(parents=True, exist_ok=True)
    valid: set[str] = set()
    for level in levels:
        m = macro_path(macros, level)
        if verify_macro(binary, level, m):
            valid.add(level.stem)
        elif m.exists():
            bad = m.with_suffix(m.suffix + ".invalid")
            bad.unlink(missing_ok=True)
            m.rename(bad)
            print(f"invalid oracle quarantined: {m} -> {bad}")
    print(f"verified oracle cache: {len(valid)}/{len(levels)}")
    return valid


def solve_missing(binary: str, levels: list[Path], macros: Path,
                  policy: Optional[Path], beams: list[int], max_ticks: int,
                  stall_ticks: int, prior_weight: float, work: Path) -> int:
    valid = audit_macros(binary, levels, macros)
    solved_now = 0
    partial_dir = work / "partials"
    partial_dir.mkdir(parents=True, exist_ok=True)
    for level in levels:
        if level.stem in valid:
            print(f"oracle {level.name}: cached + replay verified")
            continue
        for beam in beams:
            tmp = partial_dir / f"{level.stem}.b{beam}.macro"
            cmd = [binary, "solve", str(level), "--beam", str(beam),
                   "--widen", "0", "--max-frames", str(max_ticks),
                   "--stall", str(stall_ticks), "--out", str(tmp)]
            if policy is not None and valid_checkpoint(policy):
                cmd += ["--policy", str(policy),
                        "--prior-weight", str(prior_weight)]
            cp = run(cmd)
            if cp.returncode == 0 and verify_macro(binary, level, tmp):
                dst = macro_path(macros, level)
                shutil.copy2(tmp, dst)
                print(f"NEW EXACT ORACLE: {level.name} -> {dst}")
                solved_now += 1
                break
    return solved_now


def parse_eval_output(text: str) -> float:
    matches = PROGRESS_RE.findall(text)
    if not matches:
        raise RuntimeError(f"cannot parse eval progress from:\n{text}")
    return max(0.0, min(1.0, float(matches[-1]) / 100.0))


def evaluate(binary: str, policy: Path, levels: list[Path], work: Path,
             max_ticks: int) -> PoolScore:
    eval_dir = work / "eval" / policy.name
    eval_dir.mkdir(parents=True, exist_ok=True)
    rows: list[LevelScore] = []
    for level in levels:
        out = eval_dir / f"{level.stem}.macro"
        cp = run([binary, "eval", str(policy), str(level),
                  "--max-frames", str(max_ticks), "--out", str(out)],
                 capture=True)
        text = cp.stdout or ""
        print(text, end="")
        if cp.returncode not in (0, 3):
            raise RuntimeError(
                f"evaluation failed for {level} with exit {cp.returncode}"
            )
        progress = parse_eval_output(text)
        rows.append(LevelScore(str(level), progress, cp.returncode == 0))
    clears = sum(int(r.won) for r in rows)
    mean = sum(r.progress for r in rows) / len(rows)
    minimum = min((r.progress for r in rows), default=0.0)
    score = PoolScore(clears, mean, minimum, rows)
    print(f"POOL SCORE {policy}: clears={clears}/{len(rows)} "
          f"mean={mean*100:.2f}% min={minimum*100:.2f}%")
    return score


def loses_clear(candidate: PoolScore, champion: PoolScore) -> list[str]:
    champ = {Path(x.path).stem: x for x in champion.levels}
    cand = {Path(x.path).stem: x for x in candidate.levels}
    lost = []
    for stem, old in champ.items():
        if old.won and (stem not in cand or not cand[stem].won):
            lost.append(stem)
    return lost


def should_promote(candidate: PoolScore, champion: Optional[PoolScore]) -> bool:
    if champion is None:
        return True
    lost = loses_clear(candidate, champion)
    if lost:
        print("candidate rejected: regressed cleared levels:", ", ".join(lost))
        return False
    return candidate.key > champion.key


def write_metrics(path: Path, score: PoolScore, label: str) -> None:
    payload = {"label": label, "time": time.time(), **asdict(score)}
    path.mkdir(parents=True, exist_ok=True)
    tmp = path / "metrics.json.tmp"
    final = path / "metrics.json"
    tmp.write_text(json.dumps(payload, indent=2))
    os.replace(tmp, final)


def atomic_promote(candidate: Path, champion: Path, score: PoolScore) -> None:
    write_metrics(candidate, score, "champion")
    champion.parent.mkdir(parents=True, exist_ok=True)
    backup = champion.with_name(champion.name + ".previous")
    if backup.exists():
        shutil.rmtree(backup)
    if champion.exists():
        os.replace(champion, backup)
    try:
        os.replace(candidate, champion)
    except Exception:
        if champion.exists():
            shutil.rmtree(champion)
        if backup.exists():
            os.replace(backup, champion)
        raise
    if backup.exists():
        shutil.rmtree(backup)
    print(f"PROMOTED -> {champion}")


def copy_checkpoint(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    if src.exists():
        shutil.copytree(src, dst)
    else:
        dst.mkdir(parents=True, exist_ok=True)


def train_curriculum(binary: str, pool: Path, macros: Path, candidate: Path,
                     beams: list[int], args) -> bool:
    cmd = [binary, "curriculum", "--levels", str(pool),
           "--out", str(candidate), "--macros", str(macros),
           "--beams", ",".join(str(x) for x in beams),
           "--tiers", str(args.tiers), "--rounds", str(args.rounds),
           "--epochs", str(args.epochs), "--minibatch", str(args.minibatch),
           "--hidden", str(args.hidden), "--lr", str(args.lr),
           "--promote", str(args.curriculum_promote),
           "--max-samples", str(args.max_samples),
           "--max-frames", str(args.max_ticks),
           "--rescue-beam", str(args.rescue_beam),
           "--mem-budget-mb", str(args.mem_budget_mb), "--resume"]
    cp = run(cmd)
    return cp.returncode == 0 and valid_checkpoint(candidate)


def train_ppo(binary: str, pool: Path, candidate: Path, args) -> bool:
    if args.ppo_steps <= 0:
        return False
    cmd = [binary, "train", "--levels", str(pool), "--out", str(candidate),
           "--resume", "--total-steps", str(args.ppo_steps),
           "--envs", str(args.ppo_envs),
           "--steps-per-env", str(args.ppo_rollout),
           "--epochs", str(args.ppo_epochs),
           "--minibatch", str(args.ppo_minibatch),
           "--hidden", str(args.hidden), "--save-every", "25"]
    cp = run(cmd)
    return cp.returncode == 0 and valid_checkpoint(candidate)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="./build/gdlearn")
    ap.add_argument("--levels", action="append", required=True,
                    help="level directory/file; repeatable")
    ap.add_argument("--work", default="runs/champion_work")
    ap.add_argument("--champion", default="champion")
    ap.add_argument("--macros", default="macros_native")
    ap.add_argument("--cycles", type=int, default=2)
    ap.add_argument("--beams", default="96,384,1536")
    ap.add_argument("--prior-weight", type=float, default=8.0)
    ap.add_argument("--max-ticks", type=int, default=240 * 240)
    ap.add_argument("--stall-ticks", type=int, default=240 * 12)
    ap.add_argument("--tiers", type=int, default=5)
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--minibatch", type=int, default=256)
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--lr", type=float, default=8e-4)
    ap.add_argument("--curriculum-promote", type=float, default=0.70)
    ap.add_argument("--max-samples", type=int, default=400000)
    ap.add_argument("--rescue-beam", type=int, default=1200)
    ap.add_argument("--mem-budget-mb", type=int, default=3500)
    ap.add_argument("--ppo-steps", type=int, default=0)
    ap.add_argument("--ppo-envs", type=int, default=64)
    ap.add_argument("--ppo-rollout", type=int, default=128)
    ap.add_argument("--ppo-epochs", type=int, default=4)
    ap.add_argument("--ppo-minibatch", type=int, default=2048)
    args = ap.parse_args()

    binary = args.bin
    work = Path(args.work)
    champion = Path(args.champion)
    macros = Path(args.macros)
    work.mkdir(parents=True, exist_ok=True)
    pool = work / "pool"
    levels = collect_levels([Path(x) for x in args.levels], pool)
    beams = [int(x) for x in args.beams.split(",") if x.strip()]
    if not beams:
        raise RuntimeError("empty beam ladder")
    print(f"champion pool: {len(levels)} levels")

    champ_score: Optional[PoolScore] = None
    if valid_checkpoint(champion):
        champ_score = evaluate(binary, champion, levels, work, args.max_ticks)
        write_metrics(champion, champ_score, "champion-before")

    for cycle in range(args.cycles):
        print(f"\n===== CHAMPION CYCLE {cycle + 1}/{args.cycles} =====")
        solve_missing(binary, levels, macros,
                      champion if valid_checkpoint(champion) else None,
                      beams, args.max_ticks, args.stall_ticks,
                      args.prior_weight, work)

        candidate = work / f"candidate_c{cycle + 1}"
        if valid_checkpoint(champion):
            copy_checkpoint(champion, candidate)
        else:
            if candidate.exists():
                shutil.rmtree(candidate)
            candidate.mkdir(parents=True)

        if train_curriculum(binary, pool, macros, candidate, beams, args):
            score = evaluate(binary, candidate, levels, work, args.max_ticks)
            write_metrics(candidate, score, "candidate-dagger")
            if should_promote(score, champ_score):
                atomic_promote(candidate, champion, score)
                champ_score = score
            else:
                print("DAgger candidate did not beat champion")
        else:
            print("curriculum candidate unavailable; preserving champion")

        # PPO is a finisher, never the source of truth. It starts from the
        # current champion and must pass the same no-regression gate.
        if args.ppo_steps > 0 and valid_checkpoint(champion):
            ppo_candidate = work / f"candidate_ppo_c{cycle + 1}"
            copy_checkpoint(champion, ppo_candidate)
            if train_ppo(binary, pool, ppo_candidate, args):
                ppo_score = evaluate(binary, ppo_candidate, levels, work,
                                     args.max_ticks)
                write_metrics(ppo_candidate, ppo_score, "candidate-ppo")
                if should_promote(ppo_score, champ_score):
                    atomic_promote(ppo_candidate, champion, ppo_score)
                    champ_score = ppo_score
                else:
                    print("PPO candidate rejected; champion unchanged")

    if valid_checkpoint(champion):
        final = evaluate(binary, champion, levels, work, args.max_ticks)
        write_metrics(champion, final, "champion-final")
        print("FINAL", final.key)
        return 0
    print("no valid champion was produced")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("interrupted; champion directory was not modified in-place",
              file=sys.stderr)
        raise
