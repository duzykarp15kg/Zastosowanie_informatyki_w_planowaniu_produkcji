// ZIWPP - temat 3: permutacyjny problem przeplywowy z typami przezbrojen (Cmax)
//
// Tryb interaktywny:  solver.exe
// Tryb z argumentami: solver <instancja> <wyjscie> <neh|sa> [klucz=wartosc ...]
// Klucze SA: iters, p0, tfinal, stall, reheat, time, seed, init (neh|random)
//
// Format pliku instancji (same liczby, dowolne odstepy):
//   n m
//   m wierszy po n liczb  - czasy operacji p[maszyna][zadanie]
//   n liczb (0 lub 1)     - typy zadan
//   m liczb               - parametry przezbrojen s_i

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
typedef long long ll;

struct Instance {
    int n = 0, m = 0;
    vector<vector<int>> p;  // p[maszyna][zadanie]
    vector<int> type;       // typ zadania (0/1)
    vector<int> s;          // parametr przezbrojenia maszyny
};

// ---------- wczytywanie instancji ----------

static bool readInstance(const string& path, Instance& I, string& err) {
    ifstream f(path);
    if (!f) { err = "Nie mozna otworzyc pliku: " + path; return false; }

    vector<ll> tok;
    string w;
    while (f >> w) {
        char* e;
        ll v = strtoll(w.c_str(), &e, 10);
        if (*e) { err = "Niepoprawna wartosc w pliku: " + w; return false; }
        tok.push_back(v);
    }
    if (tok.size() < 2) { err = "Plik instancji jest pusty lub niekompletny"; return false; }
    if (tok[0] < 1 || tok[1] < 1 || tok[0] > 100000 || tok[1] > 10000) {
        err = "Niepoprawne n lub m";
        return false;
    }

    I.n = (int)tok[0];
    I.m = (int)tok[1];
    size_t need = 2 + (size_t)I.n * I.m + I.n + I.m;
    if (tok.size() != need) {
        err = "Zla liczba wartosci w pliku (jest " + to_string(tok.size()) +
              ", oczekiwano " + to_string(need) + ")";
        return false;
    }

    size_t k = 2;
    I.p.assign(I.m, vector<int>(I.n));
    for (int i = 0; i < I.m; i++)
        for (int j = 0; j < I.n; j++) {
            if (tok[k] < 1 || tok[k] > INT_MAX) { err = "Niepoprawny czas operacji"; return false; }
            I.p[i][j] = (int)tok[k++];
        }

    I.type.resize(I.n);
    for (int j = 0; j < I.n; j++) {
        if (tok[k] != 0 && tok[k] != 1) { err = "Typ zadania musi byc 0 lub 1"; return false; }
        I.type[j] = (int)tok[k++];
    }

    I.s.resize(I.m);
    for (int i = 0; i < I.m; i++) {
        if (tok[k] < 1 || tok[k] > INT_MAX / 10) { err = "Niepoprawny parametr przezbrojenia"; return false; }
        I.s[i] = (int)tok[k++];
    }
    return true;
}

// ---------- przezbrojenie i funkcja celu ----------

// prev < 0 oznacza pierwsze zadanie na maszynie (traktowane jak ten sam typ)
static inline int setupTime(const Instance& I, int i, int prev, int cur) {
    return (prev < 0 || I.type[prev] == I.type[cur]) ? I.s[i] : 10 * I.s[i];
}

// Cmax dla permutacji; C[i] = chwila zakonczenia ostatniej operacji na maszynie i
static ll evaluate(const Instance& I, const vector<int>& perm, vector<ll>& C) {
    fill(C.begin(), C.end(), 0);
    int prev = -1;
    for (int j : perm) {
        ll up = 0;  // koniec tego zadania na poprzedniej maszynie
        for (int i = 0; i < I.m; i++) {
            ll ready = C[i] + setupTime(I, i, prev, j);  // maszyna po przezbrojeniu
            ll st = max(ready, up);
            C[i] = st + I.p[i][j];
            up = C[i];
        }
        prev = j;
    }
    return C[I.m - 1];
}

// ---------- heurystyka NEH ----------

static vector<int> neh(const Instance& I) {
    vector<ll> sum(I.n, 0);
    for (int j = 0; j < I.n; j++)
        for (int i = 0; i < I.m; i++) sum[j] += I.p[i][j];

    vector<int> order(I.n);
    iota(order.begin(), order.end(), 0);
    stable_sort(order.begin(), order.end(), [&](int a, int b) { return sum[a] > sum[b]; });

    vector<int> cur;
    vector<ll> C(I.m);
    for (int j : order) {
        ll bestV = LLONG_MAX;
        int bestPos = 0;
        for (int pos = 0; pos <= (int)cur.size(); pos++) {
            cur.insert(cur.begin() + pos, j);
            ll v = evaluate(I, cur, C);
            cur.erase(cur.begin() + pos);
            if (v < bestV) { bestV = v; bestPos = pos; }
        }
        cur.insert(cur.begin() + bestPos, j);
    }
    return cur;
}

// ---------- symulowane wyzarzanie ----------

struct SAParams {
    ll iters = 300000;
    double p0 = 0.1;        // prawdopodobienstwo przyjecia typowego pogorszenia na starcie
    double tfinal = 0.001;  // T koncowa jako ulamek T poczatkowej
    ll stall = 0;           // 0 = automatycznie (iters/10, min. 1000)
    double reheat = 0.3;    // do jakiego ulamku T0 podgrzewamy
    double timeLimit = 0;   // 0 = bez limitu [s]
    unsigned seed = 12345;
    string init = "neh";    // neh | random
};

// ruch sasiedzki: INSERT (60%) lub SWAP (40%); permutacja zawsze pozostaje permutacja
static void randomMove(vector<int>& v, mt19937& rng) {
    int n = v.size();
    int a = rng() % n;
    int b = rng() % n;
    while (b == a) b = rng() % n;

    if (rng() % 10 < 6) {
        if (a < b) rotate(v.begin() + a, v.begin() + a + 1, v.begin() + b + 1);
        else       rotate(v.begin() + b, v.begin() + a, v.begin() + a + 1);
    } else {
        swap(v[a], v[b]);
    }
}

static vector<int> simulatedAnnealing(const Instance& I, const SAParams& P, ll& iterDone, int& reheats) {
    mt19937 rng(P.seed);
    vector<ll> C(I.m);
    vector<int> cur;

    if (P.init == "random") {
        cur.resize(I.n);
        iota(cur.begin(), cur.end(), 0);
        shuffle(cur.begin(), cur.end(), rng);
    } else {
        cur = neh(I);
    }

    ll curC = evaluate(I, cur, C);
    vector<int> best = cur;
    ll bestC = curC;
    iterDone = 0;
    reheats = 0;
    if (I.n < 2) return best;

    // kalibracja T0: mediana pogorszen dla ruchow z losowej permutacji
    vector<double> ds;
    vector<int> cand;
    {
        vector<int> rp(I.n);
        iota(rp.begin(), rp.end(), 0);
        shuffle(rp.begin(), rp.end(), rng);
        ll rc = evaluate(I, rp, C);
        for (int t = 0; t < 300; t++) {
            cand = rp;
            randomMove(cand, rng);
            ll d = evaluate(I, cand, C) - rc;
            if (d > 0) ds.push_back((double)d);
        }
    }
    double avg = 1.0;
    if (!ds.empty()) {
        nth_element(ds.begin(), ds.begin() + ds.size() / 2, ds.end());
        avg = ds[ds.size() / 2];
    }

    double T0 = avg / (-log(P.p0));
    double T = T0;
    double alpha = pow(P.tfinal, 1.0 / (double)P.iters);  // chlodzenie geometryczne
    ll stall = P.stall > 0 ? P.stall : max<ll>(1000, P.iters / 10);

    vector<int> cycSol = cur;  // najlepsze rozwiazanie biezacego cyklu
    ll cycBest = LLONG_MAX;
    ll lastImp = 0;
    uniform_real_distribution<double> U(0.0, 1.0);
    auto t0 = chrono::steady_clock::now();

    for (ll it = 1; it <= P.iters; it++) {
        iterDone = it;
        cand = cur;
        randomMove(cand, rng);
        ll c = evaluate(I, cand, C);
        ll d = c - curC;

        if (d <= 0 || U(rng) < exp(-(double)d / T)) {
            cur.swap(cand);
            curC = c;
            if (c < cycBest) { cycBest = c; cycSol = cur; lastImp = it; }
            if (c < bestC)   { bestC = c; best = cur; }
        }

        T *= alpha;

        // regrzanie: brak poprawy od "stall" iteracji i niska temperatura
        if (it - lastImp > stall && T < T0 * P.reheat) {
            if (cycBest != LLONG_MAX) { cur = cycSol; curC = cycBest; }
            T = max(T, T0 * P.reheat);
            lastImp = it;
            cycBest = LLONG_MAX;
            reheats++;
        }

        if (P.timeLimit > 0 && (it & 255) == 0) {
            double el = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
            if (el >= P.timeLimit) break;
        }
    }
    return best;
}

// ---------- zapis wyniku (permutacja + harmonogram) ----------

static void writeOutput(const string& path, const Instance& I, const vector<int>& perm,
                        const string& algo, double secs, ll iters, int reheats) {
    ofstream o(path);
    if (!o) throw runtime_error("Nie mozna zapisac pliku: " + path);

    vector<ll> C(I.m);
    o << "CMAX " << evaluate(I, perm, C) << "\n";
    o << "ALGORITHM " << algo << "\n";
    o << "TIME " << secs << "\n";
    if (iters > 0) o << "ITERATIONS " << iters << "\nREHEATS " << reheats << "\n";

    o << "PERMUTATION";
    for (int j : perm) o << ' ' << j + 1;
    o << "\nSCHEDULE\n";
    o << "# maszyna zadanie typ start_przezbrojenia koniec_przezbrojenia start_operacji koniec_operacji\n";

    fill(C.begin(), C.end(), 0);
    int prev = -1;
    for (int j : perm) {
        ll up = 0;
        for (int i = 0; i < I.m; i++) {
            ll ss = C[i];
            ll se = ss + setupTime(I, i, prev, j);
            ll st = max(se, up);
            ll en = st + I.p[i][j];
            o << i + 1 << ' ' << j + 1 << ' ' << I.type[j] << ' '
              << ss << ' ' << se << ' ' << st << ' ' << en << '\n';
            C[i] = en;
            up = en;
        }
        prev = j;
    }

    o.close();
    if (!o) throw runtime_error("Blad podczas zapisywania pliku: " + path);
}

// ---------- wspolne funkcje dla menu i trybu z argumentami ----------

struct Result {
    vector<int> perm;
    ll cmax = 0;
    double secs = 0;
    ll iters = 0;
    int reheats = 0;
    string algo;
};

static Result runAlgorithm(const Instance& I, const string& algo, const SAParams& P) {
    Result R;
    R.algo = algo;
    auto start = chrono::steady_clock::now();
    if (algo == "neh") R.perm = neh(I);
    else if (algo == "sa") R.perm = simulatedAnnealing(I, P, R.iters, R.reheats);
    else throw runtime_error("Nieznany algorytm: " + algo);
    R.secs = chrono::duration<double>(chrono::steady_clock::now() - start).count();
    vector<ll> C(I.m);
    R.cmax = evaluate(I, R.perm, C);
    return R;
}

// zamiana tekstu na liczbe z czytelnym komunikatem bledu
static ll toLL(const string& name, const string& v) {
    size_t pos = 0;
    ll x = 0;
    try { x = stoll(v, &pos); } catch (...) { pos = 0; }
    if (pos == 0 || pos != v.size())
        throw runtime_error("parametr '" + name + "' wymaga liczby calkowitej, a podano: " + v);
    return x;
}

static double toD(const string& name, const string& v) {
    size_t pos = 0;
    double x = 0;
    try { x = stod(v, &pos); } catch (...) { pos = 0; }
    if (pos == 0 || pos != v.size())
        throw runtime_error("parametr '" + name + "' wymaga liczby, a podano: " + v);
    return x;
}

// ---------- wyswietlanie ----------

static void printLine() {
    cout << "==================================================\n";
}

static void printPermutation(const vector<int>& perm) {
    cout << "Kolejnosc zadan:\n";
    for (size_t i = 0; i < perm.size(); i++)
        cout << perm[i] + 1 << (i + 1 < perm.size() ? " " : "");
    cout << "\n";
}

static void printResult(const Result& R) {
    cout << "\n";
    printLine();
    cout << (R.algo == "neh" ? " WYNIK HEURYSTYKI NEH\n" : " WYNIK SYMULOWANEGO WYZARZANIA\n");
    printLine();
    cout << "Makespan (Cmax): " << R.cmax << "\n";
    cout << fixed << setprecision(6) << "Czas obliczen:   " << R.secs << " s\n";
    if (R.algo == "sa") {
        cout << "Liczba iteracji: " << R.iters << "\n";
        cout << "Liczba regrzan:  " << R.reheats << "\n";
    }
    cout << "\n";
    printPermutation(R.perm);
    cout << defaultfloat << setprecision(6);
    printLine();
}

static void printInstance(const Instance& I, const string& path) {
    cout << "\n";
    printLine();
    cout << " DANE INSTANCJI\n";
    printLine();
    cout << "Plik:          " << path << "\n";
    cout << "Liczba zadan:  " << I.n << "\n";
    cout << "Liczba maszyn: " << I.m << "\n";
    int type0 = count(I.type.begin(), I.type.end(), 0);
    cout << "Zadania typu 0: " << type0 << "\n";
    cout << "Zadania typu 1: " << I.n - type0 << "\n";

    cout << "\nCzasy przezbrojen:\n\n";
    cout << left << setw(12) << "Maszyna" << setw(18) << "Ten sam typ" << setw(18) << "Inny typ" << "\n";
    for (int i = 0; i < I.m; i++)
        cout << left << setw(12) << i + 1 << setw(18) << I.s[i] << setw(18) << 10LL * I.s[i] << "\n";

    if (I.n <= 15 && I.m <= 10) {
        cout << "\nMacierz czasow operacji:\n\n" << setw(10) << "Maszyna";
        for (int j = 0; j < I.n; j++) cout << setw(6) << ("Z" + to_string(j + 1));
        cout << "\n";
        for (int i = 0; i < I.m; i++) {
            cout << setw(10) << ("M" + to_string(i + 1));
            for (int j = 0; j < I.n; j++) cout << setw(6) << I.p[i][j];
            cout << "\n";
        }
        cout << setw(10) << "Typ";
        for (int j = 0; j < I.n; j++) cout << setw(6) << I.type[j];
        cout << "\n";
    } else {
        cout << "\nMacierz operacji jest zbyt duza do czytelnego wyswietlenia.\n";
    }
    cout << right;
    printLine();
}

static void saveResult(const Instance& I, const Result& R, const string& path) {
    writeOutput(path, I, R.perm, R.algo, R.secs, R.iters, R.reheats);
    cout << "Wynik zapisano do pliku: " << path << "\n";
}

static void compareAlgorithms(const Instance& I, const SAParams& P) {
    cout << "\nUruchamianie algorytmu NEH...\n";
    Result Rneh = runAlgorithm(I, "neh", P);
    cout << "Uruchamianie algorytmu SA...\n";
    Result Rsa = runAlgorithm(I, "sa", P);

    double improvement = Rneh.cmax > 0 ? 100.0 * (Rneh.cmax - Rsa.cmax) / Rneh.cmax : 0;

    cout << "\n";
    printLine();
    cout << " POROWNANIE ALGORYTMOW\n";
    printLine();
    cout << "Liczba zadan:   " << I.n << "\n";
    cout << "Liczba maszyn:  " << I.m << "\n";
    cout << "Iteracje SA:    " << Rsa.iters << "\n";
    cout << "Regrzania SA:   " << Rsa.reheats << "\n\n";

    cout << left << setw(15) << "Algorytm" << setw(15) << "Cmax" << setw(15) << "Czas [s]" << "\n";
    cout << "--------------------------------------------------\n";
    cout << fixed << setprecision(6);
    cout << left << setw(15) << "NEH" << setw(15) << Rneh.cmax << setw(15) << Rneh.secs << "\n";
    cout << left << setw(15) << "SA" << setw(15) << Rsa.cmax << setw(15) << Rsa.secs << "\n";
    cout << "--------------------------------------------------\n";
    cout << setprecision(2);
    cout << "Poprawa SA wzgledem NEH: " << improvement << "%\n";
    cout << "Roznica Cmax (NEH - SA): " << Rneh.cmax - Rsa.cmax << "\n";

    if (Rsa.cmax < Rneh.cmax) cout << "Lepsze rozwiazanie: SA\n";
    else if (Rsa.cmax == Rneh.cmax) cout << "Oba algorytmy uzyskaly ten sam Cmax.\n";
    else cout << "Lepsze rozwiazanie: NEH\n";

    cout << right << defaultfloat << setprecision(6);
    cout << "\nNEH - ";
    printPermutation(Rneh.perm);
    cout << "\nSA - ";
    printPermutation(Rsa.perm);
    cout << "\n";

    saveResult(I, Rneh, "wynik_neh.txt");
    saveResult(I, Rsa, "wynik_sa.txt");
    printLine();
}

// pytanie o liczbe iteracji SA; zwraca false przy koncu wejscia
static bool readIterations(SAParams& P) {
    while (true) {
        cout << "Liczba iteracji SA [300000]: ";
        string input;
        if (!getline(cin, input)) return false;
        if (input.empty()) { P.iters = 300000; return true; }
        try {
            size_t pos = 0;
            ll value = stoll(input, &pos);
            if (pos != input.size() || value < 1 || value > 1000000000LL) {
                cout << "Niepoprawna liczba iteracji.\n";
                continue;
            }
            P.iters = value;
            return true;
        } catch (...) {
            cout << "Podaj poprawna liczbe calkowita.\n";
        }
    }
}

// ---------- menu interaktywne ----------

static int interactiveMenu() {
    Instance I;
    string currentPath;
    bool loaded = false;
    SAParams P;

    while (true) {
        cout << "\n";
        printLine();
        cout << " ZIWPP - PROJEKT - TEMAT 3\n Permutacyjny problem przeplywowy\n z typami przezbrojen\n";
        printLine();
        if (loaded) {
            cout << "Aktualna instancja: " << currentPath << "\n";
            cout << "Zadania: " << I.n << " | Maszyny: " << I.m << "\n";
        } else {
            cout << "Aktualna instancja: brak\n";
        }
        cout << "\n1. Wczytaj instancje z pliku\n2. Uruchom algorytm NEH\n3. Uruchom algorytm SA\n"
             << "4. Porownaj NEH i SA\n5. Wyswietl dane instancji\n0. Zakoncz program\n";
        printLine();
        cout << "Wybor: ";

        string choice;
        if (!getline(cin, choice)) return 0;

        if (choice == "0") { cout << "\nKoniec programu.\n"; return 0; }

        if (choice == "1") {
            cout << "\nDostepne przykladowe pliki:\n - test.txt\n - przyklad_50x10.txt\n - inst.txt\n";
            cout << "\nPodaj nazwe pliku: ";
            string path;
            if (!getline(cin, path)) return 0;
            if (path.empty()) { cout << "Nie podano nazwy pliku.\n"; continue; }

            Instance newInstance;
            string err;
            if (readInstance(path, newInstance, err)) {
                I = newInstance;
                currentPath = path;
                loaded = true;
                cout << "\nPoprawnie wczytano instancje!\n";
                cout << "Liczba zadan: " << I.n << "\n";
                cout << "Liczba maszyn: " << I.m << "\n";
            } else {
                cout << "Blad: " << err << "\n";
            }
            continue;
        }

        if (choice != "2" && choice != "3" && choice != "4" && choice != "5") {
            cout << "\nNiepoprawny wybor.\n";
            continue;
        }
        if (!loaded) {
            cout << "\nNajpierw wczytaj instancje za pomoca opcji 1.\n";
            continue;
        }

        try {
            if (choice == "2") {
                Result R = runAlgorithm(I, "neh", P);
                printResult(R);
                saveResult(I, R, "wynik_neh.txt");
            } else if (choice == "3") {
                if (!readIterations(P)) return 0;
                cout << "\nUruchamianie SA...\n";
                Result R = runAlgorithm(I, "sa", P);
                printResult(R);
                saveResult(I, R, "wynik_sa.txt");
            } else if (choice == "4") {
                if (!readIterations(P)) return 0;
                compareAlgorithms(I, P);
            } else {
                printInstance(I, currentPath);
            }
        } catch (const exception& ex) {
            cout << "Blad: " << ex.what() << "\n";
        }
    }
}

// ---------- main ----------

int main(int argc, char** argv) {
    if (argc == 1) return interactiveMenu();

    if (argc < 4) {
        cerr << "Uzycie: solver <instancja> <wyjscie> <neh|sa> [klucz=wartosc ...]\n";
        return 1;
    }

    string inPath = argv[1], outPath = argv[2], algo = argv[3];

    map<string, string> kv;
    for (int a = 4; a < argc; a++) {
        string s = argv[a];
        size_t e = s.find('=');
        if (e == string::npos) { cerr << "Niepoprawny parametr: " << s << "\n"; return 1; }
        kv[s.substr(0, e)] = s.substr(e + 1);
    }

    Instance I;
    string err;
    if (!readInstance(inPath, I, err)) { cerr << "Blad instancji: " << err << "\n"; return 2; }

    SAParams P;
    try {
        if (algo != "neh" && algo != "sa") { cerr << "Nieznany algorytm: " << algo << "\n"; return 1; }

        if (algo == "sa") {
            if (kv.count("iters"))  P.iters = toLL("iters", kv["iters"]);
            if (kv.count("p0"))     P.p0 = toD("p0", kv["p0"]);
            if (kv.count("tfinal")) P.tfinal = toD("tfinal", kv["tfinal"]);
            if (kv.count("stall"))  P.stall = toLL("stall", kv["stall"]);
            if (kv.count("reheat")) P.reheat = toD("reheat", kv["reheat"]);
            if (kv.count("time"))   P.timeLimit = toD("time", kv["time"]);
            if (kv.count("init"))   P.init = kv["init"];
            if (kv.count("seed")) {
                ll sd = toLL("seed", kv["seed"]);
                if (sd < 0 || sd > (ll)UINT_MAX) throw runtime_error("parametr 'seed' poza zakresem");
                P.seed = (unsigned)sd;
            }

            if (P.iters < 1 || !isfinite(P.p0) || P.p0 <= 0 || P.p0 >= 1 ||
                !isfinite(P.tfinal) || P.tfinal <= 0 || P.tfinal >= 1 ||
                !isfinite(P.reheat) || P.reheat <= 0 ||
                !isfinite(P.timeLimit) || P.timeLimit < 0 ||
                (P.init != "neh" && P.init != "random")) {
                cerr << "Niepoprawne parametry SA\n";
                return 3;
            }
        }
    } catch (const exception& ex) {
        cerr << "Niepoprawna wartosc parametru: " << ex.what() << "\n";
        return 3;
    }

    Result R;
    try {
        R = runAlgorithm(I, algo, P);
    } catch (const exception& ex) {
        cerr << "Blad: " << ex.what() << "\n";
        return 1;
    }

    try {
        writeOutput(outPath, I, R.perm, algo, R.secs, R.iters, R.reheats);
    } catch (const exception& ex) {
        cerr << "Blad zapisu: " << ex.what() << "\n";
        return 4;
    }

    cout << "Cmax = " << R.cmax << ", czas = " << R.secs << " s\n";
    return 0;
}