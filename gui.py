#!/usr/bin/env python3
"""ZIWPP - temat 3: permutacyjny flowshop z typami przezbrojeń (Cmax).
GUI (tkinter) do solvera C++. Komunikacja: plik instancji -> solver -> plik wyjściowy."""
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

HERE = os.path.dirname(os.path.abspath(__file__))
SOLVER_NAME = "solver.exe" if os.name == "nt" else "solver"

# parametry algorytmów: (klucz, etykieta, domyślna wartość, typ, min, max)
ALGORITHMS = {
    "NEH (heurystyka)": ("neh", []),
    "Symulowane wyżarzanie": ("sa", [
        ("iters", "Liczba iteracji", "300000", int, 1, 10**9),
        ("p0", "p0 (akceptacja na starcie, 0-1)", "0.1", float, 1e-6, 0.999999),
        ("tfinal", "T końcowa / T0 (0-1)", "0.001", float, 1e-9, 0.999999),
        ("reheat", "Regrzanie (ułamek T0)", "0.3", float, 1e-6, 1e6),
        ("time", "Limit czasu [s] (0 = brak)", "0", float, 0, 10**6),
        ("seed", "Ziarno losowości", "12345", int, 0, 2**32 - 1),
    ]),
}


def parse_instance(path):
    """Wczytuje i waliduje instancję. Zwraca (n, m, P, types, s) lub rzuca ValueError."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            tok = f.read().split()
    except OSError as e:
        raise ValueError(f"Nie można odczytać pliku: {e}")
    try:
        v = [int(x) for x in tok]
    except ValueError:
        raise ValueError("Plik zawiera wartości niebędące liczbami całkowitymi")
    if len(v) < 2:
        raise ValueError("Plik jest pusty lub niekompletny")
    n, m = v[0], v[1]
    if n < 1 or m < 1:
        raise ValueError("n i m muszą być dodatnie")
    need = 2 + n * m + n + m
    if len(v) != need:
        raise ValueError(f"Zła liczba wartości: jest {len(v)}, oczekiwano {need} (n={n}, m={m})")
    k = 2
    P = [v[k + i * n:k + (i + 1) * n] for i in range(m)]
    k += n * m
    types, s = v[k:k + n], v[k + n:k + n + m]
    if any(x < 1 for row in P for x in row):
        raise ValueError("Czasy wykonywania muszą być dodatnie")
    if any(t not in (0, 1) for t in types):
        raise ValueError("Typy zadań muszą być równe 0 lub 1")
    if any(x < 1 for x in s):
        raise ValueError("Parametry przezbrojeń s_i muszą być dodatnie")
    return n, m, P, types, s


def parse_result(path):
    res = {"perm": [], "rows": []}
    in_sched = False
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if in_sched:
                if not line.startswith("#"):
                    res["rows"].append(tuple(int(x) for x in line.split()))
            elif line == "SCHEDULE":
                in_sched = True
            elif line.startswith("PERMUTATION"):
                res["perm"] = [int(x) for x in line.split()[1:]]
            else:
                key, _, val = line.partition(" ")
                res[key] = val
    return res


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("ZIWPP – Flowshop permutacyjny z typami przezbrojeń")
        self.geometry("900x680")
        self.instance_path = None
        self.instance = None
        self.proc = None
        self.result_file = None
        self.solver_path = self._find_solver()
        self.param_vars = {}
        self._build()
        self._on_algo_change()

    # ---------- UI ----------
    def _build(self):
        top = ttk.LabelFrame(self, text="1. Instancja")
        top.pack(fill="x", padx=8, pady=6)
        ttk.Button(top, text="Wczytaj instancję…", command=self.load_instance).pack(side="left", padx=6, pady=6)
        self.inst_label = ttk.Label(top, text="(nie wczytano)")
        self.inst_label.pack(side="left", padx=6)

        solv = ttk.LabelFrame(self, text="Solver (C++)")
        solv.pack(fill="x", padx=8, pady=2)
        ttk.Button(solv, text="Wskaż plik solvera…", command=self.pick_solver).pack(side="left", padx=6, pady=4)
        self.solver_label = ttk.Label(solv, text="")
        self.solver_label.pack(side="left", padx=6)
        self._refresh_solver_label()

        mid = ttk.LabelFrame(self, text="2. Algorytm i parametry")
        mid.pack(fill="x", padx=8, pady=6)
        ttk.Label(mid, text="Algorytm:").grid(row=0, column=0, padx=6, pady=4, sticky="w")
        self.algo_var = tk.StringVar(value=list(ALGORITHMS)[1])
        cb = ttk.Combobox(mid, textvariable=self.algo_var, values=list(ALGORITHMS), state="readonly", width=28)
        cb.grid(row=0, column=1, padx=6, pady=4, sticky="w")
        cb.bind("<<ComboboxSelected>>", lambda e: self._on_algo_change())
        self.params_frame = ttk.Frame(mid)
        self.params_frame.grid(row=1, column=0, columnspan=2, sticky="w", padx=6, pady=4)

        run = ttk.Frame(self)
        run.pack(fill="x", padx=8, pady=4)
        self.run_btn = ttk.Button(run, text="Uruchom", command=self.run)
        self.run_btn.pack(side="left", padx=4)
        self.cancel_btn = ttk.Button(run, text="Anuluj", command=self.cancel, state="disabled")
        self.cancel_btn.pack(side="left", padx=4)
        self.save_btn = ttk.Button(run, text="Zapisz wynik…", command=self.save_result, state="disabled")
        self.save_btn.pack(side="left", padx=4)
        self.progress = ttk.Progressbar(run, mode="indeterminate", length=160)
        self.progress.pack(side="left", padx=10)
        self.status = ttk.Label(run, text="Gotowe")
        self.status.pack(side="left", padx=6)

        res = ttk.LabelFrame(self, text="3. Wynik")
        res.pack(fill="both", expand=True, padx=8, pady=6)
        self.summary = ttk.Label(res, text="", font=("TkDefaultFont", 11, "bold"))
        self.summary.pack(anchor="w", padx=6, pady=2)
        self.perm_text = tk.Text(res, height=3, wrap="word")
        self.perm_text.pack(fill="x", padx=6)
        cols = ("maszyna", "zadanie", "typ", "start przezbr.", "koniec przezbr.", "start oper.", "koniec oper.")
        frame = ttk.Frame(res)
        frame.pack(fill="both", expand=True, padx=6, pady=4)
        self.tree = ttk.Treeview(frame, columns=cols, show="headings")
        for c in cols:
            self.tree.heading(c, text=c)
            self.tree.column(c, width=100, anchor="center")
        sb = ttk.Scrollbar(frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")

    def _on_algo_change(self):
        for w in self.params_frame.winfo_children():
            w.destroy()
        self.param_vars = {}
        _, params = ALGORITHMS[self.algo_var.get()]
        if not params:
            ttk.Label(self.params_frame, text="Algorytm nie ma parametrów.").grid(row=0, column=0, sticky="w")
        for r, (key, label, default, *_rest) in enumerate(params):
            ttk.Label(self.params_frame, text=label + ":").grid(row=r // 2, column=(r % 2) * 2, sticky="w", padx=4, pady=2)
            var = tk.StringVar(value=default)
            ttk.Entry(self.params_frame, textvariable=var, width=12).grid(row=r // 2, column=(r % 2) * 2 + 1, padx=4, pady=2)
            self.param_vars[key] = var

    # ---------- solver ----------
    def _find_solver(self):
        for cand in (os.path.join(HERE, SOLVER_NAME), os.path.join(os.getcwd(), SOLVER_NAME)):
            if os.path.isfile(cand):
                return cand
        return None

    def _refresh_solver_label(self):
        self.solver_label.config(text=self.solver_path or "NIE ZNALEZIONO – skompiluj solver.cpp lub wskaż plik")

    def pick_solver(self):
        p = filedialog.askopenfilename(title="Wskaż plik wykonywalny solvera")
        if p:
            self.solver_path = p
            self._refresh_solver_label()

    # ---------- instancja ----------
    def load_instance(self):
        p = filedialog.askopenfilename(title="Wczytaj instancję", filetypes=[("Pliki tekstowe", "*.txt"), ("Wszystkie", "*.*")])
        if not p:
            return
        try:
            self.instance = parse_instance(p)
        except ValueError as e:
            messagebox.showerror("Błędna instancja", str(e))
            return
        self.instance_path = p
        n, m, _P, types, _s = self.instance
        self.inst_label.config(text=f"{os.path.basename(p)}: n={n} zadań, m={m} maszyn, typ 0: {types.count(0)}, typ 1: {types.count(1)}")

    # ---------- uruchomienie ----------
    def _collect_args(self):
        key, params = ALGORITHMS[self.algo_var.get()]
        args = [key]
        for pkey, label, _d, typ, lo, hi in params:
            raw = self.param_vars[pkey].get().strip().replace(",", ".")
            try:
                val = typ(raw)
            except ValueError:
                raise ValueError(f"Parametr „{label}” musi być liczbą ({typ.__name__}).")
            if not (lo <= val <= hi):
                raise ValueError(f"Parametr „{label}” poza zakresem [{lo}, {hi}].")
            args.append(f"{pkey}={val}")
        return args

    def run(self):
        if not self.instance_path:
            messagebox.showwarning("Brak instancji", "Najpierw wczytaj plik z instancją.")
            return
        if not self.solver_path or not os.path.isfile(self.solver_path):
            messagebox.showerror("Brak solvera", "Nie znaleziono solvera. Skompiluj solver.cpp (g++ -O2 -std=c++17 -o solver solver.cpp) lub wskaż plik.")
            return
        try:
            args = self._collect_args()
        except ValueError as e:
            messagebox.showerror("Błędne parametry", str(e))
            return
        tmpdir = tempfile.mkdtemp(prefix="ziwpp_")
        self.result_file = os.path.join(tmpdir, "wynik.txt")
        cmd = [self.solver_path, self.instance_path, self.result_file, args[0]] + args[1:]
        self.run_btn.config(state="disabled")
        self.cancel_btn.config(state="normal")
        self.save_btn.config(state="disabled")
        self.progress.start(12)
        self.status.config(text="Liczenie…")
        self._cancelled = False
        threading.Thread(target=self._worker, args=(cmd,), daemon=True).start()

    def _worker(self, cmd):
        try:
            self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            out, err = self.proc.communicate()
            rc = self.proc.returncode
        except OSError as e:
            self.after(0, self._done, None, f"Nie udało się uruchomić solvera: {e}")
            return
        if self._cancelled:
            self.after(0, self._done, None, "Anulowano.")
        elif rc != 0:
            self.after(0, self._done, None, f"Solver zakończył się błędem (kod {rc}):\n{err.strip() or out.strip()}")
        else:
            self.after(0, self._done, self.result_file, None)

    def cancel(self):
        self._cancelled = True
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()

    def _done(self, path, error):
        self.progress.stop()
        self.run_btn.config(state="normal")
        self.cancel_btn.config(state="disabled")
        self.proc = None
        if error:
            self.status.config(text="Błąd" if "Anulowano" not in error else "Anulowano")
            if "Anulowano" not in error:
                messagebox.showerror("Błąd", error)
            return
        try:
            res = parse_result(path)
            self._show(res)
        except (OSError, ValueError, KeyError) as e:
            self.status.config(text="Błąd")
            messagebox.showerror("Błąd wyniku", f"Nie udało się odczytać wyniku solvera: {e}")
            return
        self.status.config(text="Gotowe")
        self.save_btn.config(state="normal")

    def _show(self, res):
        extra = ""
        if "ITERATIONS" in res:
            extra = f", iteracje: {res['ITERATIONS']}, regrzania: {res.get('REHEATS', '0')}"
        self.summary.config(text=f"Cmax = {res['CMAX']}   (czas: {float(res['TIME']):.3f} s{extra})")
        self.perm_text.delete("1.0", "end")
        self.perm_text.insert("1.0", "Permutacja: " + " ".join(map(str, res["perm"])))
        self.tree.delete(*self.tree.get_children())
        for row in res["rows"]:
            self.tree.insert("", "end", values=row)

    def save_result(self):
        if not (self.result_file and os.path.isfile(self.result_file)):
            return
        p = filedialog.asksaveasfilename(defaultextension=".txt", filetypes=[("Pliki tekstowe", "*.txt")])
        if p:
            try:
                shutil.copyfile(self.result_file, p)
            except OSError as e:
                messagebox.showerror("Błąd zapisu", str(e))


if __name__ == "__main__":
    if "--selftest" in sys.argv:  # test bez GUI: python gui.py --selftest instancja.txt
        print(parse_instance(sys.argv[2])[:2])
    else:
        App().mainloop()
