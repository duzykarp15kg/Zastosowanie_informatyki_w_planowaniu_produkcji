#!/usr/bin/env python3
"""Strojenie parametrow SA: zmieniamy jeden parametr na raz, reszta zostaje domyslna.

Uzycie (z folderu projektu, gdzie leza solver.exe i gen_instance.py):
    python tune_sa.py
    python tune_sa.py --iters 100000 --seeds 5
    python tune_sa.py --instances inst.txt przyklad_50x10.txt

Wynik: tabela w konsoli oraz plik tune_wyniki.csv (kazde pojedyncze uruchomienie).
"""
import argparse
import csv
import math
import os
import re
import subprocess
import sys
import tempfile
from statistics import mean, stdev

# Wartosci domyslne SA (takie jak w solver.cpp). stall podajemy jako ulamek iteracji.
DEFAULTS = {"p0": 0.1, "tfinal": 0.001, "reheat": 0.3, "stall_frac": 0.1}

# Wartosci do sprawdzenia dla kazdego parametru
GRID = {
    "p0": [0.02, 0.05, 0.1, 0.2, 0.3],
    "tfinal": [0.01, 0.001, 0.0001],
    "reheat": [0.1, 0.3, 0.6],
    "stall_frac": [0.01, 0.05, 0.1, 0.3],
}

# Domyslne instancje testowe (generowane gen_instance.py): n, m, ziarno
GENERATED = [(100, 20, 101), (50, 10, 102), (80, 15, 103)]


def find_solver(path):
    if path:
        return path
    for cand in ("solver.exe", "./solver"):
        if os.path.exists(cand):
            return cand
    sys.exit("Nie znaleziono solver.exe - podaj sciezke przez --solver")


def make_instances(files):
    if files:
        return list(files)
    gen = "gen_instance.py"
    if not os.path.exists(gen):
        sys.exit("Brak gen_instance.py w tym folderze - podaj instancje przez --instances")
    tmp = tempfile.mkdtemp(prefix="tune_inst_")
    out = []
    for n, m, seed in GENERATED:
        path = os.path.join(tmp, f"g_{n}x{m}_s{seed}.txt")
        subprocess.run([sys.executable, gen, str(n), str(m), str(seed), path], check=True)
        out.append(path)
    return out


def run(solver, inst, args):
    r = subprocess.run([solver, inst, os.devnull] + args, capture_output=True, text=True)
    mt = re.search(r"Cmax = (\d+)", r.stdout)
    if r.returncode != 0 or not mt:
        sys.exit(f"Blad solvera dla {inst} {args}:\n{r.stdout}{r.stderr}")
    return int(mt.group(1))


def configs():
    """Konfiguracja domyslna + warianty zmieniajace po jednym parametrze."""
    out = [dict(DEFAULTS)]
    for name, values in GRID.items():
        for v in values:
            if v != DEFAULTS[name]:
                c = dict(DEFAULTS)
                c[name] = v
                out.append(c)
    return out


def label(c):
    diff = [f"{k}={c[k]}" for k in DEFAULTS if c[k] != DEFAULTS[k]]
    return ", ".join(diff) if diff else "DOMYSLNE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", default=None)
    ap.add_argument("--instances", nargs="*", default=None)
    ap.add_argument("--iters", type=int, default=100000, help="iteracje SA podczas strojenia")
    ap.add_argument("--seeds", type=int, default=5, help="ile ziaren na konfiguracje")
    a = ap.parse_args()

    solver = find_solver(a.solver)
    insts = make_instances(a.instances)
    cfgs = configs()
    seeds = list(range(1, a.seeds + 1))
    total = len(cfgs) * len(insts) * len(seeds)
    print(f"Konfiguracji: {len(cfgs)}, instancji: {len(insts)}, ziaren: {len(seeds)}, "
          f"razem uruchomien SA: {total} (iteracje SA: {a.iters})\n")

    # Odniesienie: NEH dla kazdej instancji
    neh = {i: run(solver, i, ["neh"]) for i in insts}

    rows = []  # (konfiguracja, instancja, ziarno, cmax)
    done = 0
    for ci, c in enumerate(cfgs):
        stall = max(1, int(a.iters * c["stall_frac"]))
        for inst in insts:
            for sd in seeds:
                args = ["sa", f"iters={a.iters}", f"seed={sd}", f"p0={c['p0']}",
                        f"tfinal={c['tfinal']}", f"reheat={c['reheat']}", f"stall={stall}"]
                rows.append((ci, inst, sd, run(solver, inst, args)))
                done += 1
        print(f"  [{done}/{total}] {label(c)}")

    # Najlepszy znany Cmax na kazdej instancji (minimum po wszystkich uruchomieniach i NEH)
    best = {i: min(neh[i], min(r[3] for r in rows if r[1] == i)) for i in insts}

    stats = []
    for ci, c in enumerate(cfgs):
        mine = [r for r in rows if r[0] == ci]
        dev = [100.0 * (r[3] - best[r[1]]) / best[r[1]] for r in mine]       # % ponad najlepszy znany
        imp = [100.0 * (neh[r[1]] - r[3]) / neh[r[1]] for r in mine]         # % poprawy wzgledem NEH
        se = stdev(dev) / math.sqrt(len(dev)) if len(dev) > 1 else 0.0
        stats.append((mean(dev), se, mean(imp), ci))

    stats.sort()
    print("\nWYNIKI (posortowane od najlepszej konfiguracji)")
    print("odch. = srednie odchylenie od najlepszego znanego Cmax [%] (mniej = lepiej)")
    print("+/-   = blad standardowy sredniej; roznice mniejsze od niego to szum\n")
    print(f"{'konfiguracja':34}{'odch. [%]':>10}{'+/-':>8}{'popr. vs NEH [%]':>20}")
    print("-" * 72)
    for d, se, imp, ci in stats:
        print(f"{label(cfgs[ci]):34}{d:10.3f}{se:8.3f}{imp:20.2f}")

    # Zapis surowych wynikow
    with open("tune_wyniki.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["konfiguracja", "instancja", "ziarno", "cmax", "neh_cmax"])
        for ci, inst, sd, cm in rows:
            w.writerow([label(cfgs[ci]), os.path.basename(inst), sd, cm, neh[inst]])
    print("\nSurowe wyniki zapisano w tune_wyniki.csv")

    best_cfg = cfgs[stats[0][3]]
    print("\nNajlepsza konfiguracja w tym tescie:", label(best_cfg))
    print("Jesli jej przewaga nad DOMYSLNE jest mniejsza niz +/-, to roznica jest w granicach szumu.")


if __name__ == "__main__":
    main()
