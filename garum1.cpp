/* Roman chess engine: rules, notation, and an alpha-beta search.
   C++ translation of garum1.py / garum1.js (same rules, evaluation and search).

   Build:  clang++ -std=c++17 -O2 -o garum1 garum1.cpp
   Usage:  ./garum1 <game_file> [user_move] [--think SECONDS] [--depth N]
   The game file holds one SAN move per line. If user_move is given it is
   validated and appended, then the engine replies and appends its move.
   With no game file it starts an interactive game in the terminal.
   --depth sets the deepest search (default 6, like the Python and JS bots). */

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Board = std::array<char, 64>;

static const char *FILES = "abcdefgh";
static const int MATE = 100000;
static const int INF = 1000000000;

static int VAL(char t) {
  switch (t) {
    case 'L': return 100;
    case 'C': return 500;
    case 'B': return 540;
    case 'A': return 420;
    case 'G': return 950;
    default: return 0;  // 'E'
  }
}

static const int ROOK[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static const int BISH[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
static const int KING[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
static const int KN[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};

// castling rights as bits (Python used a frozenset of "KQkq")
static const int CR_K = 1, CR_Q = 2, CR_k = 4, CR_q = 8;

// move: kind 'n' normal, 's' ballista shot, 'K'/'Q' castling
//       caps: squares whose pieces are removed (landing capture included)
//       promo: promotion piece letter, or 0
struct Move {
  int frm = 0, to = 0;
  char kind = 'n';
  char promo = 0;
  int ncaps = 0;
  int caps[8] = {0};
  bool has(int s) const {
    for (int i = 0; i < ncaps; i++)
      if (caps[i] == s) return true;
    return false;
  }
};

static bool operator==(const Move &a, const Move &b) {
  if (a.frm != b.frm || a.to != b.to || a.kind != b.kind || a.promo != b.promo || a.ncaps != b.ncaps) return false;
  for (int i = 0; i < a.ncaps; i++)
    if (a.caps[i] != b.caps[i]) return false;
  return true;
}

struct St {
  Board b;
  int side;
  int castle;
  int half;
};

static std::string name(int s) { return std::string(1, FILES[s % 8]) + std::to_string(s / 8 + 1); }
static bool isUpper(char p) { return p >= 'A' && p <= 'Z'; }
static int col(char p) { return isUpper(p) ? 0 : 1; }
static char up(char p) { return (p >= 'a' && p <= 'z') ? char(p - 32) : p; }
static char low(char p) { return (p >= 'A' && p <= 'Z') ? char(p + 32) : p; }
static bool on(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

// Python-style floor division (rounds down, also for negatives)
static int floordiv(int a, int b) {
  int q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

static St initial() {
  St st;
  st.b.fill('.');
  const char *back = "CABGEBAC";
  for (int f = 0; f < 8; f++) {
    st.b[f] = back[f];
    st.b[8 + f] = 'L';
    st.b[48 + f] = 'l';
    st.b[56 + f] = low(back[f]);
  }
  st.side = 0;
  st.castle = CR_K | CR_Q | CR_k | CR_q;
  st.half = 0;
  return st;
}

static bool phalanx(const Board &b, int s) {
  char p = b[s];
  int f = s % 8;
  return (f > 0 && b[s - 1] == p) || (f < 7 && b[s + 1] == p);
}

static bool genImmune(const Board &b, int s) {
  char e = b[s] == 'G' ? 'E' : 'e';
  int f = s % 8, r = s / 8;
  for (auto &d : KING) {
    int nf = f + d[0], nr = r + d[1];
    if (on(nf, nr) && b[nr * 8 + nf] == e) return true;
  }
  return false;
}

// Pawn wall draw: all 16 Legionaries on their 2-squares-ahead squares (White rank 4, Black rank 5).
static bool pawnWall(const Board &b) {
  for (int f = 0; f < 8; f++)
    if (b[24 + f] != 'L' || b[32 + f] != 'l') return false;
  return true;
}

static bool capturable(const Board &b, int s, bool arrow = false) {
  char p = b[s];
  if (p == 'L' || p == 'l') return arrow || !phalanx(b, s);
  if (p == 'G' || p == 'g') return !genImmune(b, s);
  return true;
}

static void add(std::vector<Move> &mv, int frm, int to, char kind, char promo, const int *caps, int n) {
  Move m;
  m.frm = frm;
  m.to = to;
  m.kind = kind;
  m.promo = promo;
  m.ncaps = n;
  for (int i = 0; i < n; i++) m.caps[i] = caps[i];
  mv.push_back(m);
}

static void add1(std::vector<Move> &mv, int frm, int to, char kind, char promo, int cap) {
  add(mv, frm, to, kind, promo, &cap, 1);
}

static std::vector<Move> pseudo(const Board &b, int c) {
  std::vector<Move> mv;
  mv.reserve(96);
  for (int s = 0; s < 64; s++) {
    char p = b[s];
    if (p == '.' || col(p) != c) continue;
    int f = s % 8, r = s / 8;
    char t = up(p);
    if (t == 'L') {
      int dr = c == 0 ? 1 : -1;
      int last = c == 0 ? 7 : 0;
      int nr = r + dr;
      if (nr >= 0 && nr < 8) {
        int to = nr * 8 + f;
        if (b[to] == '.') {
          if (nr == last) {
            for (char pr : {'G', 'C', 'B', 'A'}) add(mv, s, to, 'n', pr, nullptr, 0);
          } else {
            add(mv, s, to, 'n', 0, nullptr, 0);
            if (r == (c == 0 ? 1 : 6)) {
              int to2 = (r + 2 * dr) * 8 + f;
              if (b[to2] == '.') add(mv, s, to2, 'n', 0, nullptr, 0);
            }
          }
        }
        for (int df : {-1, 1}) {
          int nf = f + df;
          if (nf >= 0 && nf < 8) {
            int tc = nr * 8 + nf;
            char q = b[tc];
            if (q != '.' && col(q) != c && capturable(b, tc)) {
              if (nr == last) {
                for (char pr : {'G', 'C', 'B', 'A'}) add1(mv, s, tc, 'n', pr, tc);
              } else {
                add1(mv, s, tc, 'n', 0, tc);
              }
            }
          }
        }
      }
    } else if (t == 'C') {
      for (auto &d : ROOK) {
        int nf = f + d[0], nr = r + d[1];
        int passed[8];
        int np = 0;
        while (on(nf, nr)) {
          int to = nr * 8 + nf;
          char q = b[to];
          if (q == '.') {
            add(mv, s, to, 'n', 0, passed, np);
          } else if (col(q) == c) {
            // flies over own pieces
          } else if (q == 'L' || q == 'l') {
            if (capturable(b, to)) {
              passed[np] = to;
              add(mv, s, to, 'n', 0, passed, np + 1);
              np++;
            } else {
              break;  // immune legionary blocks
            }
          } else {
            if (np == 0 && capturable(b, to)) add1(mv, s, to, 'n', 0, to);
            break;
          }
          nf += d[0];
          nr += d[1];
        }
      }
    } else if (t == 'B' || t == 'G') {
      int ndirs = t == 'B' ? 4 : 8;
      for (int i = 0; i < ndirs; i++) {
        int df = t == 'B' ? BISH[i][0] : KING[i][0];
        int dr = t == 'B' ? BISH[i][1] : KING[i][1];
        int nf = f + df, nr = r + dr;
        while (on(nf, nr)) {
          int to = nr * 8 + nf;
          char q = b[to];
          if (q == '.') {
            add(mv, s, to, 'n', 0, nullptr, 0);
          } else {
            if (col(q) != c && capturable(b, to)) add1(mv, s, to, 'n', 0, to);
            break;
          }
          nf += df;
          nr += dr;
        }
      }
      if (t == 'B') {
        int dr = c == 0 ? 1 : -1;
        int nr = r + dr;
        while (nr >= 0 && nr < 8) {
          int to = nr * 8 + f;
          char q = b[to];
          if (q != '.') {
            if (col(q) != c && capturable(b, to, true)) add1(mv, s, to, 's', 0, to);
            break;
          }
          nr += dr;
        }
      }
    } else if (t == 'A') {
      int targets[16];
      int nt = 0;
      for (auto &d : KN) {
        int nf = f + d[0], nr = r + d[1];
        if (on(nf, nr)) targets[nt++] = nr * 8 + nf;
      }
      for (auto &d : KING) {
        int mf = f + d[0], mr = r + d[1];
        int nf = f + 2 * d[0], nr = r + 2 * d[1];
        if (on(nf, nr) && b[mr * 8 + mf] == '.') targets[nt++] = nr * 8 + nf;
      }
      for (int i = 0; i < nt; i++) {
        int to = targets[i];
        char q = b[to];
        if (q == '.') add(mv, s, to, 'n', 0, nullptr, 0);
        else if (col(q) != c && capturable(b, to)) add1(mv, s, to, 'n', 0, to);
      }
    } else if (t == 'E') {
      for (auto &d : KING) {
        int nf = f + d[0], nr = r + d[1];
        if (on(nf, nr)) {
          int to = nr * 8 + nf;
          char q = b[to];
          if (q == '.') add(mv, s, to, 'n', 0, nullptr, 0);
          else if (col(q) != c && capturable(b, to)) add1(mv, s, to, 'n', 0, to);
        }
      }
    }
  }
  return mv;
}

static int emperorSq(const Board &b, int c) {
  char e = c == 0 ? 'E' : 'e';
  for (int i = 0; i < 64; i++)
    if (b[i] == e) return i;
  return -1;
}

// Can side `by` capture the piece standing on s?
static bool attacked(const Board &b, int s, int by) {
  for (const Move &m : pseudo(b, by))
    if (m.has(s)) return true;
  return false;
}

static bool inCheck(const Board &b, int c) {
  int e = emperorSq(b, c);
  return e >= 0 && attacked(b, e, 1 - c);
}

static std::vector<Move> castles(const St &st) {
  const Board &b = st.b;
  int c = st.side, cr = st.castle;
  std::vector<Move> out;
  int home = c == 0 ? 4 : 60;
  char E = c == 0 ? 'E' : 'e';
  char C = c == 0 ? 'C' : 'c';
  if (b[home] != E) return out;
  int ks = c == 0 ? CR_K : CR_k, qs = c == 0 ? CR_Q : CR_q;
  struct Opt { char kind; int path[3]; int to; };
  std::vector<Opt> opts;
  if ((cr & ks) && b[home + 3] == C && b[home + 1] == '.' && b[home + 2] == '.')
    opts.push_back({'K', {home, home + 1, home + 2}, home + 2});
  if ((cr & qs) && b[home - 4] == C && b[home - 1] == '.' && b[home - 2] == '.' && b[home - 3] == '.')
    opts.push_back({'Q', {home, home - 1, home - 2}, home - 2});
  for (const Opt &o : opts) {
    bool ok = true;
    for (int sq : o.path) {
      Board tb = b;
      tb[home] = '.';
      tb[sq] = E;
      if (attacked(tb, sq, 1 - c)) { ok = false; break; }
    }
    if (ok) add(out, home, o.to, o.kind, 0, nullptr, 0);
  }
  return out;
}

static int cornerBit(int sq) {
  switch (sq) {
    case 0: return CR_Q;
    case 7: return CR_K;
    case 56: return CR_q;
    case 63: return CR_k;
    default: return 0;
  }
}

static St make(const St &st, const Move &m) {
  St n;
  n.b = st.b;
  Board &b = n.b;
  char p = b[m.frm];
  int cr = st.castle;
  int half = st.half + 1;
  for (int i = 0; i < m.ncaps; i++) {
    int x = m.caps[i];
    b[x] = '.';
    cr &= ~cornerBit(x);
  }
  if (m.ncaps) half = 0;
  if (m.kind == 's') {
    // the ballista shoots and stays where it is
  } else if (m.kind == 'K' || m.kind == 'Q') {
    b[m.frm] = '.';
    b[m.to] = p;
    if (m.kind == 'K') {
      b[m.frm + 3] = '.';
      b[m.frm + 1] = p == 'E' ? 'C' : 'c';
    } else {
      b[m.frm - 4] = '.';
      b[m.frm - 1] = p == 'E' ? 'C' : 'c';
    }
  } else {
    b[m.frm] = '.';
    b[m.to] = m.promo ? (p == 'L' ? m.promo : low(m.promo)) : p;
    if (p == 'L' || p == 'l') half = 0;
  }
  if (p == 'E') cr &= ~(CR_K | CR_Q);
  else if (p == 'e') cr &= ~(CR_k | CR_q);
  cr &= ~cornerBit(m.frm);
  n.side = 1 - st.side;
  n.castle = cr;
  n.half = half;
  return n;
}

static std::vector<Move> legal(const St &st) {
  int c = st.side;
  std::vector<Move> all = pseudo(st.b, c);
  std::vector<Move> cs = castles(st);
  all.insert(all.end(), cs.begin(), cs.end());
  std::vector<Move> out;
  for (const Move &m : all)
    if (!inCheck(make(st, m).b, c)) out.push_back(m);
  return out;
}

// ---------------- notation ----------------

static std::string sanCore(const St &st, const Move &m, const std::vector<Move> &allLegal) {
  const Board &b = st.b;
  char p = up(b[m.frm]);
  if (m.kind == 'K') return "O-O";
  if (m.kind == 'Q') return "O-O-O";
  if (p == 'L') {
    std::string s = m.ncaps ? std::string(1, FILES[m.frm % 8]) + "x" + name(m.to) : name(m.to);
    if (m.promo) s += std::string("=") + m.promo;
    return s;
  }
  std::vector<int> rivals;
  for (const Move &o : allLegal)
    if (o.to == m.to && o.frm != m.frm && o.kind == m.kind && up(b[o.frm]) == p) rivals.push_back(o.frm);
  std::string dis;
  if (!rivals.empty()) {
    bool allFile = true, allRank = true;
    for (int f : rivals) {
      if (f % 8 == m.frm % 8) allFile = false;
      if (f / 8 == m.frm / 8) allRank = false;
    }
    if (allFile) dis = std::string(1, FILES[m.frm % 8]);
    else if (allRank) dis = std::to_string(m.frm / 8 + 1);
    else dis = name(m.frm);
  }
  if (m.kind == 's') return std::string(1, p) + dis + "x" + name(m.to) + "(S)";
  bool landing = b[m.to] != '.';
  std::string s = std::string(1, p) + dis + (landing ? "x" : "") + name(m.to);
  if (p == 'C') {
    for (int i = 0; i < m.ncaps; i++)
      if (m.caps[i] != m.to) { s += "(x)"; break; }
  }
  return s;
}

static std::string san(const St &st, const Move &m) {
  std::vector<Move> lm = legal(st);
  std::string s = sanCore(st, m, lm);
  St nst = make(st, m);
  if (inCheck(nst.b, nst.side)) s += legal(nst).empty() ? "#" : "+";
  return s;
}

static std::string replaceAll(std::string s, const std::string &from, const std::string &to) {
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n\f\v");
  if (a == std::string::npos) return "";
  size_t z = s.find_last_not_of(" \t\r\n\f\v");
  return s.substr(a, z - a + 1);
}

static std::string norm(std::string s) {
  s = trim(s);
  for (const char *x : {"+", "#", "!", "?"}) s = replaceAll(s, x, "");
  s = replaceAll(s, "0-0-0", "O-O-O");
  s = replaceAll(s, "0-0", "O-O");
  s = replaceAll(s, "o-o", "O-O");
  if (!s.empty() && s.rfind("O-O", 0) != 0 && s.size() > 1) {
    // forgive caps after the piece letter: "AC3" -> "Ac3", "BxC7(s)" -> "Bxc7(S)"
    std::string rest = s.substr(1);
    for (char &ch : rest) ch = low(ch);
    rest = replaceAll(rest, "(s)", "(S)");
    size_t i = rest.find('=');
    if (i != std::string::npos)
      for (size_t j = i + 1; j < rest.size(); j++) rest[j] = up(rest[j]);
    s = s.substr(0, 1) + rest;
  }
  return s;
}

static std::string loose(const std::string &s) {
  std::string t = norm(s);
  t = replaceAll(t, "(x)", "");
  t = replaceAll(t, "x", "");
  t = replaceAll(t, "(S)", "S");
  t = replaceAll(t, "=", "");
  return t;
}

struct IllegalMove : std::runtime_error {
  using std::runtime_error::runtime_error;
};

static Move parse(const St &st, const std::string &text) {
  std::vector<Move> lm = legal(st);
  std::string t = norm(text);
  std::vector<std::pair<std::string, Move>> sans;
  for (const Move &m : lm) sans.push_back({norm(sanCore(st, m, lm)), m});
  for (auto &sm : sans)
    if (sm.first == t) return sm.second;
  std::vector<Move> cands;
  std::string lt = loose(t);
  for (auto &sm : sans)
    if (loose(sm.first) == lt) cands.push_back(sm.second);
  if (cands.size() == 1) return cands[0];
  std::string all;
  for (size_t i = 0; i < sans.size(); i++) all += (i ? ", " : "") + sans[i].first;
  throw IllegalMove("Illegal or ambiguous move: " + text + " (legal: " + all + ")");
}

// ---------------- evaluation & search ----------------

static int CENTER[64];
static const int LADV[8] = {0, 0, 4, 10, 22, 40, 70, 0};

static void initTables() {
  for (int i = 0; i < 64; i++) {
    int f = i % 8, r = i / 8;
    CENTER[i] = static_cast<int>(14 - 4 * (std::fabs(f - 3.5) + std::fabs(r - 3.5)) / 1.0);
  }
}

static int evaluate(const St &st) {
  const Board &b = st.b;
  int score = 0;
  int material = 0;
  for (int i = 0; i < 64; i++) {
    char p = b[i];
    if (p == '.') continue;
    char t = up(p);
    int c = isUpper(p) ? 0 : 1;
    int r = i / 8;
    int rr = c == 0 ? r : 7 - r;
    int v = VAL(t);
    if (t != 'E' && t != 'L') material += VAL(t);
    if (t == 'L') {
      v += LADV[rr];
      if (phalanx(b, i)) v += 12;
      int f = i % 8;
      if (f >= 2 && f <= 5) v += 4;
    } else if (t == 'A' || t == 'B') {
      v += CENTER[i];
      if (rr == 0) v -= 12;
    } else if (t == 'G') {
      if (genImmune(b, i)) v += 10;
      v += floordiv(CENTER[i], 3);
    } else if (t == 'C') {
      v += 4 * std::min(rr, 5) / 2;
    }
    score += c == 0 ? v : -v;
  }
  // emperor safety in the middlegame
  for (int c = 0; c < 2; c++) {
    int e = emperorSq(b, c);
    if (e < 0) continue;
    int rr = c == 0 ? e / 8 : 7 - e / 8;
    int pen;
    if (material > 3000) {
      pen = 18 * rr;
      int f = e % 8;
      if (f == 6 || f == 2 || f == 1) pen -= 15;
    } else {
      pen = -CENTER[e];
    }
    score += c == 0 ? -pen : pen;
  }
  return st.side == 0 ? score : -score;
}

using Clock = std::chrono::steady_clock;

class Search {
 public:
  Clock::time_point deadline;
  long long nodes = 0;
  bool stop = false;

  explicit Search(Clock::time_point d) : deadline(d) {}

  std::vector<Move> &order(const Board &b, std::vector<Move> &moves) {
    std::vector<std::pair<int, Move>> keyed;
    keyed.reserve(moves.size());
    for (const Move &m : moves) {
      int k = 0;
      for (int i = 0; i < m.ncaps; i++) {
        char x = b[m.caps[i]];
        k += x != '.' ? VAL(up(x)) : 0;
      }
      if (m.ncaps) k += 1000 - VAL(up(b[m.frm])) / 10;
      if (m.promo) k += 800;
      keyed.push_back({k, m});
    }
    // stable, best-looking first
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const std::pair<int, Move> &x, const std::pair<int, Move> &y) { return x.first > y.first; });
    for (size_t i = 0; i < moves.size(); i++) moves[i] = keyed[i].second;
    return moves;
  }

  bool killsEmperor(const Board &b, int c, const std::vector<Move> &moves) {
    char e = c == 0 ? 'e' : 'E';
    for (const Move &m : moves)
      for (int i = 0; i < m.ncaps; i++)
        if (b[m.caps[i]] == e) return true;
    return false;
  }

  int qs(const St &st, int alpha, int beta, int ply, int qdepth) {
    nodes++;
    const Board &b = st.b;
    int c = st.side;
    std::vector<Move> moves = pseudo(b, c);
    if (killsEmperor(b, c, moves)) return MATE - ply;
    int stand = evaluate(st);
    if (stand >= beta || qdepth <= 0) return stand;
    if (stand > alpha) alpha = stand;
    std::vector<Move> caps;
    for (const Move &m : moves)
      if (m.ncaps) caps.push_back(m);
    for (const Move &m : order(b, caps)) {
      int sc = -qs(make(st, m), -beta, -alpha, ply + 1, qdepth - 1);
      if (sc >= beta) return sc;
      if (sc > alpha) alpha = sc;
    }
    return alpha;
  }

  int nm(const St &st, int depth, int alpha, int beta, int ply) {
    if ((nodes & 255) == 0 && Clock::now() > deadline) stop = true;
    if (stop) return 0;
    nodes++;
    const Board &b = st.b;
    int c = st.side;
    if (pawnWall(b)) return 0;  // the game ends here as a draw
    std::vector<Move> moves = pseudo(b, c);
    if (killsEmperor(b, c, moves)) return MATE - ply;
    if (depth <= 0) return qs(st, alpha, beta, ply, 4);
    std::vector<Move> cs = castles(st);
    moves.insert(moves.end(), cs.begin(), cs.end());
    order(b, moves);
    int best = -INF;
    for (const Move &m : moves) {
      int sc = -nm(make(st, m), depth - 1, -beta, -alpha, ply + 1);
      if (stop) return 0;
      if (sc > best) best = sc;
      if (sc > alpha) alpha = sc;
      if (alpha >= beta) break;
    }
    if (best <= -(MATE - 500)) {
      if (!inCheck(b, c)) return 0;  // stalemate is a draw
    }
    return best;
  }
};

struct ThinkResult {
  std::optional<Move> move;
  std::optional<int> score;  // empty if no depth finished
};

static ThinkResult think(const St &st, double seconds = 20.0, int maxdepth = 6) {
  std::vector<Move> lm = legal(st);
  if (lm.empty()) return {};
  auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
  Search srch(deadline);
  Move best = lm[0];
  std::optional<int> bestScore;
  std::vector<Move> order = lm;
  srch.order(st.b, order);
  for (int depth = 1; depth <= maxdepth; depth++) {
    std::optional<Move> curBest;
    int curScore = -INF;
    int alpha = -INF;
    for (const Move &m : order) {
      int sc = -srch.nm(make(st, m), depth - 1, -INF, -alpha, 1);
      if (srch.stop) break;
      if (sc > curScore) { curScore = sc; curBest = m; }
      if (sc > alpha) alpha = sc;
    }
    if (srch.stop || !curBest) break;
    best = *curBest;
    bestScore = curScore;
    order.erase(std::find(order.begin(), order.end(), best));
    order.insert(order.begin(), best);
    std::fprintf(stderr, "depth %d  score %d  best %s  nodes %lld\n", depth, curScore,
                 sanCore(st, best, lm).c_str(), srch.nodes);
    if (std::abs(curScore) > MATE - 500) break;
  }
  return {best, bestScore};
}

static std::string show(const Board &b) {
  std::string out;
  for (int r = 7; r >= 0; r--) {
    out += std::to_string(r + 1);
    for (int f = 0; f < 8; f++) {
      out += ' ';
      out += b[r * 8 + f];
    }
    out += '\n';
  }
  out += "  a b c d e f g h";
  return out;
}

static const char *gameOver(const St &st) {
  if (!legal(st).empty()) return nullptr;
  return inCheck(st.b, st.side) ? "checkmate" : "stalemate (draw)";
}

// ---------------- command line ----------------

static St load(const std::string &path) {
  St st = initial();
  std::ifstream fh(path);
  if (!fh) return st;  // no file yet: new game
  std::string line;
  while (std::getline(fh, line)) {
    line = trim(line);
    if (!line.empty()) st = make(st, parse(st, line));
  }
  return st;
}

static void append(const std::string &path, const std::string &s) {
  std::ofstream fh(path, std::ios::app);
  fh << s << '\n';
}

struct Eof {};

static std::string input(const std::string &prompt) {
  std::cout << prompt << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) throw Eof();
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return line;
}

static void interactive(double secs, int maxdepth) {
  std::cout << "Roman chess. Type a move (e4, Ac3, Cd5(x), Bxc7(S), O-O),\n";
  std::cout << "\"moves\" for legal moves, \"board\" to show the board, \"quit\" to stop.\n";
  std::string side = trim(input("Play as White or Black? [w/b] "));
  for (char &ch : side) ch = low(ch);
  int human = (!side.empty() && side[0] == 'b') ? 1 : 0;
  St st = initial();
  std::cout << show(st.b) << '\n';
  for (;;) {
    if (const char *over = gameOver(st)) { std::cout << "GAME OVER: " << over << '\n'; break; }
    if (st.half >= 100) { std::cout << "GAME OVER: draw by 50-move rule\n"; break; }
    if (pawnWall(st.b)) { std::cout << "GAME OVER: draw by pawn wall\n"; break; }
    if (st.side == human) {
      std::string txt = trim(input("\nYour move: "));
      if (txt == "quit" || txt == "exit" || txt == "resign") break;
      if (txt == "moves") {
        std::vector<Move> lm = legal(st);
        std::vector<std::string> names;
        for (const Move &m : lm) names.push_back(sanCore(st, m, lm));
        std::sort(names.begin(), names.end());
        for (size_t i = 0; i < names.size(); i++) std::cout << (i ? ", " : "") << names[i];
        std::cout << '\n';
        continue;
      }
      if (txt == "board") { std::cout << show(st.b) << '\n'; continue; }
      Move m;
      try {
        m = parse(st, txt);
      } catch (const IllegalMove &) {
        std::cout << "Illegal or ambiguous move. Type \"moves\" to see the legal ones.\n";
        continue;
      }
      std::string s = san(st, m);
      st = make(st, m);
      std::cout << "You: " << s << '\n';
    } else {
      std::cout << "\nThinking...\n" << std::flush;
      Move m = *think(st, secs, maxdepth).move;
      std::string s = san(st, m);
      st = make(st, m);
      std::cout << "Engine: " << s << '\n';
    }
    std::cout << show(st.b) << '\n';
  }
}

static void printOver(const St &st) {
  std::cout << "GAME OVER: " << (inCheck(st.b, st.side) ? "checkmate" : "stalemate") << '\n';
}

int main(int argc, char **argv) {
  initTables();
  double secs = 20.0;
  int maxdepth = 6;
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--think" && i + 1 < argc) secs = std::atof(argv[++i]);
    else if (a == "--depth" && i + 1 < argc) maxdepth = std::max(1, std::atoi(argv[++i]));
    else args.push_back(a);
  }
  try {
    if (args.empty()) {
      try {
        interactive(secs, maxdepth);
      } catch (const Eof &) {
        std::cout << '\n';
      }
      return 0;
    }
    const std::string path = args[0];
    St st = load(path);
    if (args.size() > 1) {
      Move m = parse(st, args[1]);
      std::string s = san(st, m);
      st = make(st, m);
      append(path, s);
      std::cout << "YOU: " << s << '\n';
    }
    if (legal(st).empty()) {
      printOver(st);
      std::cout << show(st.b) << '\n';
      return 0;
    }
    if (pawnWall(st.b)) {
      std::cout << "GAME OVER: draw by pawn wall\n" << show(st.b) << '\n';
      return 0;
    }
    ThinkResult res = think(st, secs, maxdepth);
    std::string s = san(st, *res.move);
    st = make(st, *res.move);
    append(path, s);
    std::cout << "ME: " << s << "  eval(for me): " << (res.score ? std::to_string(*res.score) : "None") << '\n';
    std::cout << show(st.b) << '\n';
    if (legal(st).empty()) printOver(st);
    else if (pawnWall(st.b)) std::cout << "GAME OVER: draw by pawn wall\n";
  } catch (const IllegalMove &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  return 0;
}
