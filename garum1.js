#!/usr/bin/env node
/* Roman chess engine: rules, notation, and an alpha-beta search.
   JavaScript translation of garum1.py (same rules, evaluation and search).

   Usage: node garum1.js <game_file> [user_move] [--think SECONDS]
   The game file holds one SAN move per line. If user_move is given it is
   validated and appended, then the engine replies and appends its move.
   With no game file it starts an interactive game in the terminal.

   In a browser, load it with <script src="garum1.js"></script> and use
   window.Garum1 (initial, legal, make, san, parse, think, ...). */
'use strict';

const FILES = 'abcdefgh';
const VAL = { L: 100, C: 500, B: 540, A: 420, G: 950, E: 0 };
const MATE = 100000;
const INF = 1e9;

const ROOK = [[1, 0], [-1, 0], [0, 1], [0, -1]];
const BISH = [[1, 1], [1, -1], [-1, 1], [-1, -1]];
const KING = ROOK.concat(BISH);
const KN = [[1, 2], [2, 1], [2, -1], [1, -2], [-1, -2], [-2, -1], [-2, 1], [-1, 2]];

// castling rights as bits (Python used a frozenset of "KQkq")
const CR = { K: 1, Q: 2, k: 4, q: 8 };

function name(s) {
  return FILES[s % 8] + String(Math.floor(s / 8) + 1);
}

function isUpper(p) {
  return p >= 'A' && p <= 'Z';
}

function col(p) {
  return isUpper(p) ? 0 : 1;
}

function initial() {
  const b = new Array(64).fill('.');
  const back = 'CABGEBAC';
  for (let f = 0; f < 8; f++) {
    b[f] = back[f];
    b[8 + f] = 'L';
    b[48 + f] = 'l';
    b[56 + f] = back[f].toLowerCase();
  }
  return { b, side: 0, castle: CR.K | CR.Q | CR.k | CR.q, half: 0 };
}

function phalanx(b, s) {
  const p = b[s];
  const f = s % 8;
  return (f > 0 && b[s - 1] === p) || (f < 7 && b[s + 1] === p);
}

function genImmune(b, s) {
  const e = b[s] === 'G' ? 'E' : 'e';
  const f = s % 8, r = Math.floor(s / 8);
  for (const [df, dr] of KING) {
    const nf = f + df, nr = r + dr;
    if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8 && b[nr * 8 + nf] === e) return true;
  }
  return false;
}

// Pawn wall draw: all 16 Legionaries on their 2-squares-ahead squares (White rank 4, Black rank 5).
function pawnWall(b) {
  for (let f = 0; f < 8; f++) if (b[24 + f] !== 'L' || b[32 + f] !== 'l') return false;
  return true;
}

function capturable(b, s, arrow = false) {
  const p = b[s];
  if (p === 'L' || p === 'l') return arrow || !phalanx(b, s);
  if (p === 'G' || p === 'g') return !genImmune(b, s);
  return true;
}

// move = {frm, to, kind, caps, promo}
//   kind: 'n' normal, 's' ballista shot, 'K'/'Q' castling
//   caps: squares whose pieces are removed (landing capture included)
//   promo: promotion piece letter, or null
function mk(frm, to, kind, caps, promo) {
  return { frm, to, kind, caps, promo };
}

const NONE = [];

function pseudo(b, c) {
  const mv = [];
  for (let s = 0; s < 64; s++) {
    const p = b[s];
    if (p === '.' || col(p) !== c) continue;
    const f = s % 8, r = Math.floor(s / 8);
    const t = p.toUpperCase();
    if (t === 'L') {
      const dr = c === 0 ? 1 : -1;
      const last = c === 0 ? 7 : 0;
      const nr = r + dr;
      if (nr >= 0 && nr < 8) {
        const to = nr * 8 + f;
        if (b[to] === '.') {
          if (nr === last) {
            for (const pr of 'GCBA') mv.push(mk(s, to, 'n', NONE, pr));
          } else {
            mv.push(mk(s, to, 'n', NONE, null));
            if (r === (c === 0 ? 1 : 6)) {
              const to2 = (r + 2 * dr) * 8 + f;
              if (b[to2] === '.') mv.push(mk(s, to2, 'n', NONE, null));
            }
          }
        }
        for (const df of [-1, 1]) {
          const nf = f + df;
          if (nf >= 0 && nf < 8) {
            const tc = nr * 8 + nf;
            const q = b[tc];
            if (q !== '.' && col(q) !== c && capturable(b, tc)) {
              if (nr === last) {
                for (const pr of 'GCBA') mv.push(mk(s, tc, 'n', [tc], pr));
              } else {
                mv.push(mk(s, tc, 'n', [tc], null));
              }
            }
          }
        }
      }
    } else if (t === 'C') {
      for (const [df, dr] of ROOK) {
        let nf = f + df, nr = r + dr;
        const passed = [];
        while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
          const to = nr * 8 + nf;
          const q = b[to];
          if (q === '.') {
            mv.push(mk(s, to, 'n', passed.slice(), null));
          } else if (col(q) === c) {
            // flies over own pieces
          } else if (q === 'L' || q === 'l') {
            if (capturable(b, to)) {
              mv.push(mk(s, to, 'n', passed.concat([to]), null));
              passed.push(to);
            } else {
              break; // immune legionary blocks
            }
          } else {
            if (passed.length === 0 && capturable(b, to)) mv.push(mk(s, to, 'n', [to], null));
            break;
          }
          nf += df;
          nr += dr;
        }
      }
    } else if (t === 'B' || t === 'G') {
      const dirs = t === 'B' ? BISH : KING;
      for (const [df, dr] of dirs) {
        let nf = f + df, nr = r + dr;
        while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
          const to = nr * 8 + nf;
          const q = b[to];
          if (q === '.') {
            mv.push(mk(s, to, 'n', NONE, null));
          } else {
            if (col(q) !== c && capturable(b, to)) mv.push(mk(s, to, 'n', [to], null));
            break;
          }
          nf += df;
          nr += dr;
        }
      }
      if (t === 'B') {
        const dr = c === 0 ? 1 : -1;
        let nr = r + dr;
        while (nr >= 0 && nr < 8) {
          const to = nr * 8 + f;
          const q = b[to];
          if (q !== '.') {
            if (col(q) !== c && capturable(b, to, true)) mv.push(mk(s, to, 's', [to], null));
            break;
          }
          nr += dr;
        }
      }
    } else if (t === 'A') {
      const targets = [];
      for (const [df, dr] of KN) {
        const nf = f + df, nr = r + dr;
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) targets.push(nr * 8 + nf);
      }
      for (const [df, dr] of KING) {
        const mf = f + df, mr = r + dr;
        const nf = f + 2 * df, nr = r + 2 * dr;
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8 && b[mr * 8 + mf] === '.') targets.push(nr * 8 + nf);
      }
      for (const to of targets) {
        const q = b[to];
        if (q === '.') mv.push(mk(s, to, 'n', NONE, null));
        else if (col(q) !== c && capturable(b, to)) mv.push(mk(s, to, 'n', [to], null));
      }
    } else if (t === 'E') {
      for (const [df, dr] of KING) {
        const nf = f + df, nr = r + dr;
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
          const to = nr * 8 + nf;
          const q = b[to];
          if (q === '.') mv.push(mk(s, to, 'n', NONE, null));
          else if (col(q) !== c && capturable(b, to)) mv.push(mk(s, to, 'n', [to], null));
        }
      }
    }
  }
  return mv;
}

function emperorSq(b, c) {
  const e = c === 0 ? 'E' : 'e';
  for (let i = 0; i < 64; i++) if (b[i] === e) return i;
  return null;
}

/** Can side `by` capture the piece standing on s? */
function attacked(b, s, by) {
  for (const m of pseudo(b, by)) if (m.caps.includes(s)) return true;
  return false;
}

function inCheck(b, c) {
  const e = emperorSq(b, c);
  return e !== null && attacked(b, e, 1 - c);
}

function castles(st) {
  const b = st.b, c = st.side, cr = st.castle;
  const out = [];
  const home = c === 0 ? 4 : 60;
  const E = c === 0 ? 'E' : 'e';
  const C = c === 0 ? 'C' : 'c';
  if (b[home] !== E) return out;
  const ks = c === 0 ? CR.K : CR.k, qs = c === 0 ? CR.Q : CR.q;
  const opts = [];
  if ((cr & ks) && b[home + 3] === C && b[home + 1] === '.' && b[home + 2] === '.')
    opts.push(['K', [home, home + 1, home + 2], home + 2]);
  if ((cr & qs) && b[home - 4] === C && b[home - 1] === '.' && b[home - 2] === '.' && b[home - 3] === '.')
    opts.push(['Q', [home, home - 1, home - 2], home - 2]);
  for (const [kind, path, to] of opts) {
    let ok = true;
    for (const sq of path) {
      const tb = b.slice();
      tb[home] = '.';
      tb[sq] = E;
      if (attacked(tb, sq, 1 - c)) { ok = false; break; }
    }
    if (ok) out.push(mk(home, to, kind, NONE, null));
  }
  return out;
}

const CORNER = { 0: CR.Q, 7: CR.K, 56: CR.q, 63: CR.k };

function make(st, m) {
  const b = st.b.slice();
  const { frm, to, kind, caps, promo: pr } = m;
  const p = b[frm];
  let cr = st.castle;
  let half = st.half + 1;
  for (const x of caps) {
    b[x] = '.';
    if (x in CORNER) cr &= ~CORNER[x];
  }
  if (caps.length) half = 0;
  if (kind === 's') {
    // the ballista shoots and stays where it is
  } else if (kind === 'K' || kind === 'Q') {
    b[frm] = '.';
    b[to] = p;
    if (kind === 'K') {
      b[frm + 3] = '.';
      b[frm + 1] = p === 'E' ? 'C' : 'c';
    } else {
      b[frm - 4] = '.';
      b[frm - 1] = p === 'E' ? 'C' : 'c';
    }
  } else {
    b[frm] = '.';
    b[to] = pr ? (p === 'L' ? pr : pr.toLowerCase()) : p;
    if (p === 'L' || p === 'l') half = 0;
  }
  if (p === 'E') cr &= ~(CR.K | CR.Q);
  else if (p === 'e') cr &= ~(CR.k | CR.q);
  if (frm in CORNER) cr &= ~CORNER[frm];
  return { b, side: 1 - st.side, castle: cr, half };
}

function legal(st) {
  const c = st.side;
  const out = [];
  for (const m of pseudo(st.b, c).concat(castles(st))) {
    if (!inCheck(make(st, m).b, c)) out.push(m);
  }
  return out;
}

// ---------------- notation ----------------

function sanCore(st, m, allLegal) {
  const b = st.b;
  const { frm, to, kind, caps, promo: pr } = m;
  const p = b[frm].toUpperCase();
  if (kind === 'K') return 'O-O';
  if (kind === 'Q') return 'O-O-O';
  if (p === 'L') {
    let s = caps.length ? FILES[frm % 8] + 'x' + name(to) : name(to);
    if (pr) s += '=' + pr;
    return s;
  }
  const rivals = allLegal.filter(o => o.to === to && o.frm !== frm && o.kind === kind
    && b[o.frm].toUpperCase() === p);
  let dis = '';
  if (rivals.length) {
    if (rivals.every(o => o.frm % 8 !== frm % 8)) dis = FILES[frm % 8];
    else if (rivals.every(o => Math.floor(o.frm / 8) !== Math.floor(frm / 8))) dis = String(Math.floor(frm / 8) + 1);
    else dis = name(frm);
  }
  if (kind === 's') return p + dis + 'x' + name(to) + '(S)';
  const landing = b[to] !== '.';
  let s = p + dis + (landing ? 'x' : '') + name(to);
  if (p === 'C' && caps.some(x => x !== to)) s += '(x)';
  return s;
}

function san(st, m, allLegal = null) {
  if (allLegal === null) allLegal = legal(st);
  let s = sanCore(st, m, allLegal);
  const nst = make(st, m);
  if (inCheck(nst.b, nst.side)) s += legal(nst).length ? '+' : '#';
  return s;
}

function norm(s) {
  s = s.trim().replaceAll('+', '').replaceAll('#', '').replaceAll('!', '').replaceAll('?', '');
  s = s.replaceAll('0-0-0', 'O-O-O').replaceAll('0-0', 'O-O').replaceAll('o-o', 'O-O');
  if (s && !s.startsWith('O-O') && s.length > 1) {
    // forgive caps after the piece letter: "AC3" -> "Ac3", "BxC7(s)" -> "Bxc7(S)"
    let rest = s.slice(1).toLowerCase().replaceAll('(s)', '(S)');
    const i = rest.indexOf('=');
    if (i >= 0) rest = rest.slice(0, i + 1) + rest.slice(i + 1).toUpperCase();
    s = s[0] + rest;
  }
  return s;
}

function loose(s) {
  return norm(s).replaceAll('(x)', '').replaceAll('x', '').replaceAll('(S)', 'S').replaceAll('=', '');
}

function parse(st, text) {
  const lm = legal(st);
  const t = norm(text);
  const sans = lm.map(m => [norm(sanCore(st, m, lm)), m]);
  for (const [s, m] of sans) if (s === t) return m;
  const cands = sans.filter(([s]) => loose(s) === loose(t)).map(([, m]) => m);
  if (cands.length === 1) return cands[0];
  throw new Error('Illegal or ambiguous move: ' + text + ' (legal: ' + sans.map(([s]) => s).join(', ') + ')');
}

// ---------------- evaluation & search ----------------

const CENTER = new Array(64);
for (let i = 0; i < 64; i++) {
  const f = i % 8, r = Math.floor(i / 8);
  CENTER[i] = Math.trunc(14 - 4 * (Math.abs(f - 3.5) + Math.abs(r - 3.5)) / 1.0);
}
const LADV = [0, 0, 4, 10, 22, 40, 70, 0];

function evaluate(st) {
  const b = st.b;
  let score = 0;
  let material = 0;
  for (let i = 0; i < 64; i++) {
    const p = b[i];
    if (p === '.') continue;
    const t = p.toUpperCase();
    const c = isUpper(p) ? 0 : 1;
    const r = Math.floor(i / 8);
    const rr = c === 0 ? r : 7 - r;
    let v = VAL[t];
    if (t !== 'E' && t !== 'L') material += VAL[t];
    if (t === 'L') {
      v += LADV[rr];
      if (phalanx(b, i)) v += 12;
      const f = i % 8;
      if (f >= 2 && f <= 5) v += 4;
    } else if (t === 'A' || t === 'B') {
      v += CENTER[i];
      if (rr === 0) v -= 12;
    } else if (t === 'G') {
      if (genImmune(b, i)) v += 10;
      v += Math.floor(CENTER[i] / 3); // Python // rounds down, also for negatives
    } else if (t === 'C') {
      v += Math.floor(4 * Math.min(rr, 5) / 2);
    }
    score += c === 0 ? v : -v;
  }
  // emperor safety in the middlegame
  for (const c of [0, 1]) {
    const e = emperorSq(b, c);
    if (e === null) continue;
    const rr = c === 0 ? Math.floor(e / 8) : 7 - Math.floor(e / 8);
    let pen;
    if (material > 3000) {
      pen = 18 * rr;
      const f = e % 8;
      if (f === 6 || f === 2 || f === 1) pen -= 15;
    } else {
      pen = -CENTER[e];
    }
    score += c === 0 ? -pen : pen;
  }
  return st.side === 0 ? score : -score;
}

const now = () => Date.now() / 1000;

class Search {
  constructor(deadline) {
    this.deadline = deadline;
    this.nodes = 0;
    this.stop = false;
  }

  order(b, moves) {
    const key = m => {
      let k = 0;
      for (const x of m.caps) k += b[x] !== '.' ? VAL[b[x].toUpperCase()] : 0;
      if (m.caps.length) k += 1000 - Math.floor(VAL[b[m.frm].toUpperCase()] / 10);
      if (m.promo) k += 800;
      return k;
    };
    const keyed = moves.map(m => [key(m), m]);
    keyed.sort((x, y) => y[0] - x[0]); // stable, best-looking first
    for (let i = 0; i < moves.length; i++) moves[i] = keyed[i][1];
    return moves;
  }

  killsEmperor(b, c, moves) {
    const e = c === 0 ? 'e' : 'E';
    for (const m of moves) for (const x of m.caps) if (b[x] === e) return true;
    return false;
  }

  qs(st, alpha, beta, ply, qdepth) {
    this.nodes++;
    const b = st.b, c = st.side;
    const moves = pseudo(b, c);
    if (this.killsEmperor(b, c, moves)) return MATE - ply;
    const stand = evaluate(st);
    if (stand >= beta || qdepth <= 0) return stand;
    if (stand > alpha) alpha = stand;
    const caps = moves.filter(m => m.caps.length);
    for (const m of this.order(b, caps)) {
      const sc = -this.qs(make(st, m), -beta, -alpha, ply + 1, qdepth - 1);
      if (sc >= beta) return sc;
      if (sc > alpha) alpha = sc;
    }
    return alpha;
  }

  nm(st, depth, alpha, beta, ply) {
    if ((this.nodes & 255) === 0 && now() > this.deadline) this.stop = true;
    if (this.stop) return 0;
    this.nodes++;
    const b = st.b, c = st.side;
    if (pawnWall(b)) return 0; // the game ends here as a draw
    let moves = pseudo(b, c);
    if (this.killsEmperor(b, c, moves)) return MATE - ply;
    if (depth <= 0) return this.qs(st, alpha, beta, ply, 4);
    moves = this.order(b, moves.concat(castles(st)));
    let best = -INF;
    for (const m of moves) {
      const sc = -this.nm(make(st, m), depth - 1, -beta, -alpha, ply + 1);
      if (this.stop) return 0;
      if (sc > best) best = sc;
      if (sc > alpha) alpha = sc;
      if (alpha >= beta) break;
    }
    if (best <= -(MATE - 500)) {
      if (!inCheck(b, c)) return 0; // stalemate is a draw
    }
    return best;
  }
}

/** Returns [bestMove, score]; score is null if no depth finished. */
function think(st, seconds = 20.0, maxdepth = 6, log = defaultLog) {
  const lm = legal(st);
  if (!lm.length) return [null, null];
  const srch = new Search(now() + seconds);
  let best = lm[0];
  let bestScore = null;
  const order = srch.order(st.b, lm.slice());
  for (let depth = 1; depth <= maxdepth; depth++) {
    let curBest = null, curScore = -INF;
    let alpha = -INF;
    for (const m of order) {
      const sc = -srch.nm(make(st, m), depth - 1, -INF, -alpha, 1);
      if (srch.stop) break;
      if (sc > curScore) { curScore = sc; curBest = m; }
      if (sc > alpha) alpha = sc;
    }
    if (srch.stop || curBest === null) break;
    best = curBest;
    bestScore = curScore;
    order.splice(order.indexOf(best), 1);
    order.unshift(best);
    log(`depth ${depth}  score ${bestScore}  best ${sanCore(st, best, lm)}  nodes ${srch.nodes}`);
    if (Math.abs(bestScore) > MATE - 500) break;
  }
  return [best, bestScore];
}

function defaultLog(line) {
  if (typeof process !== 'undefined' && process.stderr) process.stderr.write(line + '\n');
}

function show(b) {
  const rows = [];
  for (let r = 7; r >= 0; r--) {
    const cells = [];
    for (let f = 0; f < 8; f++) cells.push(b[r * 8 + f]);
    rows.push(String(r + 1) + ' ' + cells.join(' '));
  }
  rows.push('  a b c d e f g h');
  return rows.join('\n');
}

function gameOver(st) {
  if (legal(st).length) return null;
  return inCheck(st.b, st.side) ? 'checkmate' : 'stalemate (draw)';
}

const Garum1 = {
  FILES, VAL, MATE, INF, name, initial, phalanx, genImmune, capturable, pseudo, emperorSq,
  attacked, inCheck, castles, make, legal, sanCore, san, norm, loose, parse, evaluate,
  Search, think, show, gameOver, pawnWall,
};

if (typeof module !== 'undefined' && module.exports) module.exports = Garum1;
if (typeof window !== 'undefined') window.Garum1 = Garum1;

// ---------------- command line (Node only) ----------------

if (typeof require !== 'undefined' && typeof module !== 'undefined' && require.main === module) {
  const fs = require('fs');

  class EOF extends Error {}

  // Blocking line read from stdin, so the game loop can stay synchronous like the Python one.
  function input(prompt) {
    process.stdout.write(prompt);
    const bytes = [];
    const buf = Buffer.alloc(1);
    for (;;) {
      let n;
      try {
        n = fs.readSync(0, buf, 0, 1, null);
      } catch (e) {
        if (e.code === 'EAGAIN') continue;
        if (e.code === 'EOF') n = 0;
        else throw e;
      }
      if (n === 0) {
        if (!bytes.length) throw new EOF();
        break;
      }
      if (buf[0] === 10) break;
      bytes.push(buf[0]);
    }
    return Buffer.from(bytes).toString('utf8').replace(/\r$/, '');
  }

  function load(path) {
    let st = initial();
    const sans = [];
    let text;
    try {
      text = fs.readFileSync(path, 'utf8');
    } catch (e) {
      if (e.code === 'ENOENT') return [st, sans];
      throw e;
    }
    for (let line of text.split('\n')) {
      line = line.trim();
      if (line) {
        const m = parse(st, line);
        sans.push(line);
        st = make(st, m);
      }
    }
    return [st, sans];
  }

  function interactive(secs) {
    console.log('Roman chess. Type a move (e4, Ac3, Cd5(x), Bxc7(S), O-O),');
    console.log('"moves" for legal moves, "board" to show the board, "quit" to stop.');
    const side = input('Play as White or Black? [w/b] ').trim().toLowerCase();
    const human = side.startsWith('b') ? 1 : 0;
    let st = initial();
    console.log(show(st.b));
    for (;;) {
      const over = gameOver(st);
      if (over) { console.log('GAME OVER: ' + over); break; }
      if (st.half >= 100) { console.log('GAME OVER: draw by 50-move rule'); break; }
      if (pawnWall(st.b)) { console.log('GAME OVER: draw by pawn wall'); break; }
      if (st.side === human) {
        const txt = input('\nYour move: ').trim();
        if (['quit', 'exit', 'resign'].includes(txt)) break;
        if (txt === 'moves') {
          const lm = legal(st);
          const names = lm.map(m => sanCore(st, m, lm));
          names.sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
          console.log(names.join(', '));
          continue;
        }
        if (txt === 'board') { console.log(show(st.b)); continue; }
        let m;
        try {
          m = parse(st, txt);
        } catch (e) {
          console.log('Illegal or ambiguous move. Type "moves" to see the legal ones.');
          continue;
        }
        const s = san(st, m);
        st = make(st, m);
        console.log('You: ' + s);
      } else {
        console.log('\nThinking...');
        const [m] = think(st, secs);
        const s = san(st, m);
        st = make(st, m);
        console.log('Engine: ' + s);
      }
      console.log(show(st.b));
    }
  }

  function main() {
    let secs = 20.0;
    let args = process.argv.slice(2);
    const ti = args.indexOf('--think');
    if (ti >= 0) {
      secs = parseFloat(args[ti + 1]);
      args = args.slice(0, ti).concat(args.slice(ti + 2));
    }
    if (!args.length) {
      try {
        interactive(secs);
      } catch (e) {
        if (e instanceof EOF) console.log();
        else throw e;
      }
      return;
    }
    const path = args[0];
    args = args.slice(1);
    let [st] = load(path);
    if (args.length) {
      const m = parse(st, args[0]);
      const s = san(st, m);
      st = make(st, m);
      fs.appendFileSync(path, s + '\n');
      console.log('YOU: ' + s);
    }
    if (!legal(st).length) {
      console.log('GAME OVER: ' + (inCheck(st.b, st.side) ? 'checkmate' : 'stalemate'));
      console.log(show(st.b));
      return;
    }
    if (pawnWall(st.b)) {
      console.log('GAME OVER: draw by pawn wall');
      console.log(show(st.b));
      return;
    }
    const [m, sc] = think(st, secs);
    const s = san(st, m);
    st = make(st, m);
    fs.appendFileSync(path, s + '\n');
    console.log('ME: ' + s + '  eval(for me): ' + (sc === null ? 'None' : sc));
    console.log(show(st.b));
    if (!legal(st).length) {
      console.log('GAME OVER: ' + (inCheck(st.b, st.side) ? 'checkmate' : 'stalemate'));
    } else if (pawnWall(st.b)) {
      console.log('GAME OVER: draw by pawn wall');
    }
  }

  try {
    main();
  } catch (e) {
    if (e instanceof Error && e.message.startsWith('Illegal or ambiguous move')) {
      console.error(e.message);
      process.exit(1);
    }
    throw e;
  }
}
