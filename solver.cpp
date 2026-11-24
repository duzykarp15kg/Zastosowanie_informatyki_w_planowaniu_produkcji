// ZIWPP - temat 3: permutacyjny problem przepływowy z typami przezbrojeń (Cmax)
// Użycie: solver <instancja> <wyjscie> <algorytm: neh|sa> [klucz=wartosc ...]
//   sa: iters=300000 p0=0.1 tfinal=0.001 stall=0 reheat=0.3 time=0 seed=12345 init=neh|random
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>
using namespace std;
typedef long long ll;

struct Instance {
    int n = 0, m = 0;
    vector<vector<int>> p;  // p[i][j]: maszyna i, zadanie j
    vector<int> type;       // typ zadania (0/1)
    vector<int> s;          // s[i]: parametr przezbrojenia maszyny i
};

// Format: "n m", m wierszy po n czasów p[i][*], wiersz typów (n liczb), wiersz s (m liczb)
static bool readInstance(const string& path, Instance& I, string& err) {
    ifstream f(path);
    if (!f) { err = "Nie mozna otworzyc pliku: " + path; return false; }
    vector<ll> tok; string w;
    while (f >> w) {
        char* e; ll v = strtoll(w.c_str(), &e, 10);
        if (*e) { err = "Niepoprawna wartosc w pliku: " + w; return false; }
        tok.push_back(v);
    }
    if (tok.size() < 2) { err = "Plik instancji jest pusty lub niekompletny"; return false; }
    if (tok[0] < 1 || tok[1] < 1 || tok[0] > 100000 || tok[1] > 10000) { err = "Niepoprawne n lub m"; return false; }
    I.n = (int)tok[0]; I.m = (int)tok[1];
    size_t need = 2 + (size_t)I.n * I.m + I.n + I.m;
    if (tok.size() != need) { err = "Zla liczba wartosci w pliku (jest " + to_string(tok.size()) + ", oczekiwano " + to_string(need) + ")"; return false; }
    size_t k = 2;
    I.p.assign(I.m, vector<int>(I.n));
    for (int i = 0; i < I.m; i++) for (int j = 0; j < I.n; j++) {
        if (tok[k] < 1) { err = "Czasy p musza byc dodatnie"; return false; }
        I.p[i][j] = (int)tok[k++];
    }
    I.type.resize(I.n);
    for (int j = 0; j < I.n; j++) {
        if (tok[k] != 0 && tok[k] != 1) { err = "Typ zadania musi byc 0 lub 1"; return false; }
        I.type[j] = (int)tok[k++];
    }
    I.s.resize(I.m);
    for (int i = 0; i < I.m; i++) {
        if (tok[k] < 1) { err = "Parametry s musza byc dodatnie"; return false; }
        I.s[i] = (int)tok[k++];
    }
    return true;
}

// czas przezbrojenia na maszynie i z zadania prev na cur (prev<0: pierwsze zadanie => j=k => s_i)
static inline int setupTime(const Instance& I, int i, int prev, int cur) {
    if (prev < 0 || I.type[prev] == I.type[cur]) return I.s[i];
    return 10 * I.s[i];
}

// Cmax permutacji. Przezbrojenie zalezy tylko od maszyny, wiec rusza zaraz po
// zakonczeniu poprzedniego zadania na maszynie (nie czeka na zadanie).
static ll evaluate(const Instance& I, const vector<int>& perm, vector<ll>& C) {
    fill(C.begin(), C.end(), 0);
    int prev = -1;
    for (int j : perm) {
        ll up = 0;
        for (int i = 0; i < I.m; i++) {
            ll ready = C[i] + setupTime(I, i, prev, j);
            ll st = max(ready, up);
            C[i] = st + I.p[i][j];
            up = C[i];
        }
        prev = j;
    }
    return C[I.m - 1];
}

// ---------- heurystyka: NEH (kolejnosc po sumie czasow malejaco, wstawianie na najlepsza pozycje) ----------
static vector<int> neh(const Instance& I) {
    vector<ll> sum(I.n, 0);
    for (int j = 0; j < I.n; j++) for (int i = 0; i < I.m; i++) sum[j] += I.p[i][j];
    vector<int> order(I.n);
    iota(order.begin(), order.end(), 0);
    stable_sort(order.begin(), order.end(), [&](int a, int b) { return sum[a] > sum[b]; });
    vector<int> cur; vector<ll> C(I.m);
    for (int j : order) {
        ll bestV = LLONG_MAX; int bestPos = 0;
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

// ---------- metaheurystyka: symulowane wyzarzanie z kalibracja T0, mieszanym sasiedztwem i regrzaniem ----------
struct SAParams {
    ll iters = 300000;
    double p0 = 0.1;       // prawdop. akceptacji przeciętnego pogorszenia na starcie
    double tfinal = 0.001; // Tkoncowa / T0
    ll stall = 0;          // po ilu iteracjach bez poprawy regrzewamy (0 = auto)
    double reheat = 0.3;   // temperatura po regrzaniu jako ulamek T0
    double timeLimit = 0;  // s, 0 = bez limitu
    unsigned seed = 12345;
    string init = "neh";
};

static void randomMove(vector<int>& v, mt19937& rng) {
    int n = v.size();
    int a = rng() % n, b = rng() % n;
    while (b == a) b = rng() % n;
    if (rng() % 10 < 6) {  // insert
        if (a < b) rotate(v.begin() + a, v.begin() + a + 1, v.begin() + b + 1);
        else rotate(v.begin() + b, v.begin() + a, v.begin() + a + 1);
    } else swap(v[a], v[b]);
}

static vector<int> simulatedAnnealing(const Instance& I, const SAParams& P, ll& iterDone, int& reheats) {
    mt19937 rng(P.seed);
    vector<ll> C(I.m);
    vector<int> cur;
    if (P.init == "random") { cur.resize(I.n); iota(cur.begin(), cur.end(), 0); shuffle(cur.begin(), cur.end(), rng); }
    else cur = neh(I);
    ll curC = evaluate(I, cur, C);
    vector<int> best = cur; ll bestC = curC;
    iterDone = 0; reheats = 0;
    if (I.n < 2) return best;

        vector<double> ds;
    vector<int> cand;
    {   // kalibracja na losowej permutacji (typowy krajobraz), niezaleznie od rozwiazania startowego
        vector<int> rp(I.n); iota(rp.begin(), rp.end(), 0); shuffle(rp.begin(), rp.end(), rng);
        ll rc = evaluate(I, rp, C);
        for (int t = 0; t < 300; t++) {
            cand = rp; randomMove(cand, rng);
            ll d = evaluate(I, cand, C) - rc;
            if (d > 0) ds.push_back((double)d);
        }
    }
    double avg = 1.0;
    if (!ds.empty()) { nth_element(ds.begin(), ds.begin() + ds.size() / 2, ds.end()); avg = ds[ds.size() / 2]; }
    double T0 = avg / (-log(P.p0));
    double T = T0;
    double alpha = pow(P.tfinal, 1.0 / (double)P.iters);
    ll stall = P.stall > 0 ? P.stall : max<ll>(1000, P.iters / 10);
    vector<int> cycSol = cur;
    ll lastImp = 0, cycBest = LLONG_MAX;  // cycBest: najlepsze w biezacym cyklu (stagnacja liczona wzgledem niego)
    uniform_real_distribution<double> U(0.0, 1.0);
    auto t0 = chrono::steady_clock::now();

    for (ll it = 1; it <= P.iters; it++) {
        iterDone = it;
        cand = cur; randomMove(cand, rng);
        ll c = evaluate(I, cand, C);
        ll d = c - curC;
        if (d <= 0 || U(rng) < exp(-(double)d / T)) {
            cur.swap(cand); curC = c;
            if (c < cycBest) { cycBest = c; cycSol = cur; lastImp = it; }
            if (c < bestC) { bestC = c; best = cur; }
        }
        T *= alpha;
        if (it - lastImp > stall && T < T0 * P.reheat) {  // regrzanie (tylko w fazie zimnej)
            if (cycBest != LLONG_MAX) { cur = cycSol; curC = cycBest; }  // regrzanie od najlepszego z cyklu
            T = max(T, T0 * P.reheat);
            lastImp = it; cycBest = LLONG_MAX; reheats++;
        }
        if (P.timeLimit > 0 && (it & 255) == 0) {
            double el = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
            if (el >= P.timeLimit) break;
        }
    }
    return best;
}

static void writeOutput(const string& path, const Instance& I, const vector<int>& perm, const string& algo,
                        double secs, ll iters, int reheats) {
    ofstream o(path);
    vector<ll> C(I.m);
    ll cmax = evaluate(I, perm, C);
    o << "CMAX " << cmax << "\nALGORITHM " << algo << "\nTIME " << secs << "\n";
    if (iters > 0) o << "ITERATIONS " << iters << "\nREHEATS " << reheats << "\n";
    o << "PERMUTATION";
    for (int j : perm) o << ' ' << j + 1;
    o << "\nSCHEDULE\n# maszyna zadanie typ start_przezbrojenia koniec_przezbrojenia start_operacji koniec_operacji\n";
    fill(C.begin(), C.end(), 0);
    int prev = -1;
    for (int j : perm) {
        ll up = 0;
        for (int i = 0; i < I.m; i++) {
            ll ss = C[i], se = ss + setupTime(I, i, prev, j);
            ll st = max(se, up), en = st + I.p[i][j];
            o << i + 1 << ' ' << j + 1 << ' ' << I.type[j] << ' ' << ss << ' ' << se << ' ' << st << ' ' << en << '\n';
            C[i] = en; up = en;
        }
        prev = j;
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        cerr << "Uzycie: solver <instancja> <wyjscie> <neh|sa> [klucz=wartosc ...]\n";
        return 1;
    }
    string inPath = argv[1], outPath = argv[2], algo = argv[3];
    map<string, string> kv;
    for (int a = 4; a < argc; a++) {
        string s = argv[a]; size_t e = s.find('=');
        if (e == string::npos) { cerr << "Niepoprawny parametr: " << s << "\n"; return 1; }
        kv[s.substr(0, e)] = s.substr(e + 1);
    }
    Instance I; string err;
    if (!readInstance(inPath, I, err)) { cerr << "Blad instancji: " << err << "\n"; return 2; }

    auto start = chrono::steady_clock::now();
    vector<int> perm; ll iters = 0; int reheats = 0;
    try {
        if (algo == "neh") perm = neh(I);
        else if (algo == "sa") {
            SAParams P;
            if (kv.count("iters")) P.iters = stoll(kv["iters"]);
            if (kv.count("p0")) P.p0 = stod(kv["p0"]);
            if (kv.count("tfinal")) P.tfinal = stod(kv["tfinal"]);
            if (kv.count("stall")) P.stall = stoll(kv["stall"]);
            if (kv.count("reheat")) P.reheat = stod(kv["reheat"]);
            if (kv.count("time")) P.timeLimit = stod(kv["time"]);
            if (kv.count("seed")) P.seed = (unsigned)stoul(kv["seed"]);
            if (kv.count("init")) P.init = kv["init"];
            if (P.iters < 1 || P.p0 <= 0 || P.p0 >= 1 || P.tfinal <= 0 || P.tfinal >= 1 || P.reheat <= 0 ||
                P.timeLimit < 0 || (P.init != "neh" && P.init != "random")) {
                cerr << "Niepoprawne parametry SA\n"; return 3;
            }
            perm = simulatedAnnealing(I, P, iters, reheats);
        } else { cerr << "Nieznany algorytm: " << algo << "\n"; return 1; }
    } catch (const exception& ex) { cerr << "Niepoprawna wartosc parametru: " << ex.what() << "\n"; return 3; }
    double secs = chrono::duration<double>(chrono::steady_clock::now() - start).count();
    writeOutput(outPath, I, perm, algo, secs, iters, reheats);
    vector<ll> C(I.m);
    cout << "Cmax = " << evaluate(I, perm, C) << ", czas = " << secs << " s\n";
    return 0;
}
