#!/usr/bin/env python3
"""Generator instancji (osobny skrypt, aplikacja sama nie generuje danych).
Uzycie: python gen_instance.py n m seed plik.txt"""
import random, sys

def main():
    if len(sys.argv) != 5:
        print(__doc__); sys.exit(1)
    n, m, seed, path = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    rnd = random.Random(seed)
    with open(path, "w") as f:
        f.write(f"{n} {m}\n")
        for _ in range(m):
            f.write(" ".join(str(rnd.randint(1, 99)) for _ in range(n)) + "\n")
        f.write(" ".join(str(rnd.randint(0, 1)) for _ in range(n)) + "\n")
        f.write(" ".join(str(rnd.randint(1, 9)) for _ in range(m)) + "\n")

main()
