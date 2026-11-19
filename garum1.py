"""Roman chess engine: rules, notation, and an alpha-beta search.

Usage: python3 roman.py <game_file> [user_move] [--think SECONDS]
The game file holds one SAN move per line. If user_move is given it is
validated and appended, then the engine replies and appends its move.
"""
import sys, time

FILES = 'abcdefgh'
VAL = {'L': 100, 'C': 500, 'B': 540, 'A': 420, 'G': 950, 'E': 0}
MATE = 100000
INF = 10 ** 9

ROOK = [(1, 0), (-1, 0), (0, 1), (0, -1)]
BISH = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
KING = ROOK + BISH
KN = [(1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1), (-2, 1), (-1, 2)]


def name(s):
    return FILES[s % 8] + str(s // 8 + 1)


def col(p):
    return 0 if p.isupper() else 1


def initial():
    b = ['.'] * 64
    back = 'CABGEBAC'
    for f in range(8):
        b[f] = back[f]
        b[8 + f] = 'L'
        b[48 + f] = 'l'
        b[56 + f] = back[f].lower()
    return {'b': b, 'side': 0, 'castle': frozenset('KQkq'), 'half': 0}


def phalanx(b, s):
    p = b[s]
    f = s % 8
    return (f > 0 and b[s - 1] == p) or (f < 7 and b[s + 1] == p)


def gen_immune(b, s):
    e = 'E' if b[s] == 'G' else 'e'
    f, r = s % 8, s // 8
    for df, dr in KING:
        nf, nr = f + df, r + dr
        if 0 <= nf < 8 and 0 <= nr < 8 and b[nr * 8 + nf] == e:
            return True
    return False



# Pawn wall draw: all 16 Legionaries on their 2-squares-ahead squares (White rank 4, Black rank 5).
def pawn_wall(b):
    for f in range(8):
        if b[24 + f] != 'L' or b[32 + f] != 'l':
            return False
    return True


def capturable(b, s, arrow=False):
    p = b[s]
    if p in 'Ll':
        return arrow or not phalanx(b, s)
    if p in 'Gg':
        return not gen_immune(b, s)
    return True


# move = (frm, to, kind, caps, promo)
#   kind: 'n' normal, 's' ballista shot, 'K'/'Q' castling
#   caps: squares whose pieces are removed (landing capture included)

def pseudo(b, c):
    mv = []
    app = mv.append
    for s in range(64):
        p = b[s]
        if p == '.' or col(p) != c:
            continue
        f, r = s % 8, s // 8
        t = p.upper()
        if t == 'L':
            dr = 1 if c == 0 else -1
            last = 7 if c == 0 else 0
            nr = r + dr
            if 0 <= nr < 8:
                to = nr * 8 + f
                if b[to] == '.':
                    if nr == last:
                        for pr in 'GCBA':
                            app((s, to, 'n', (), pr))
                    else:
                        app((s, to, 'n', (), None))
                        if r == (1 if c == 0 else 6):
                            to2 = (r + 2 * dr) * 8 + f
                            if b[to2] == '.':
                                app((s, to2, 'n', (), None))
                for df in (-1, 1):
                    nf = f + df
                    if 0 <= nf < 8:
                        to = nr * 8 + nf
                        q = b[to]
                        if q != '.' and col(q) != c and capturable(b, to):
                            if nr == last:
                                for pr in 'GCBA':
                                    app((s, to, 'n', (to,), pr))
                            else:
                                app((s, to, 'n', (to,), None))
        elif t == 'C':
            for df, dr in ROOK:
                nf, nr = f + df, r + dr
                passed = []
                while 0 <= nf < 8 and 0 <= nr < 8:
                    to = nr * 8 + nf
                    q = b[to]
                    if q == '.':
                        app((s, to, 'n', tuple(passed), None))
                    elif col(q) == c:
                        pass  # flies over own pieces
                    elif q in 'Ll':
                        if capturable(b, to):
                            app((s, to, 'n', tuple(passed) + (to,), None))
                            passed.append(to)
                        else:
                            break  # immune legionary blocks
                    else:
                        if not passed and capturable(b, to):
                            app((s, to, 'n', (to,), None))
                        break
                    nf += df
                    nr += dr
        elif t in 'BG':
            dirs = BISH if t == 'B' else KING
            for df, dr in dirs:
                nf, nr = f + df, r + dr
                while 0 <= nf < 8 and 0 <= nr < 8:
                    to = nr * 8 + nf
                    q = b[to]
                    if q == '.':
                        app((s, to, 'n', (), None))
                    else:
                        if col(q) != c and capturable(b, to):
                            app((s, to, 'n', (to,), None))
                        break
                    nf += df
                    nr += dr
            if t == 'B':
                dr = 1 if c == 0 else -1
                nr = r + dr
                while 0 <= nr < 8:
                    to = nr * 8 + f
                    q = b[to]
                    if q != '.':
                        if col(q) != c and capturable(b, to, True):
                            app((s, to, 's', (to,), None))
                        break
                    nr += dr
        elif t == 'A':
            targets = []
            for df, dr in KN:
                nf, nr = f + df, r + dr
                if 0 <= nf < 8 and 0 <= nr < 8:
                    targets.append(nr * 8 + nf)
            for df, dr in KING:
                mf, mr = f + df, r + dr
                nf, nr = f + 2 * df, r + 2 * dr
                if 0 <= nf < 8 and 0 <= nr < 8 and b[mr * 8 + mf] == '.':
                    targets.append(nr * 8 + nf)
            for to in targets:
                q = b[to]
                if q == '.':
                    app((s, to, 'n', (), None))
                elif col(q) != c and capturable(b, to):
                    app((s, to, 'n', (to,), None))
        elif t == 'E':
            for df, dr in KING:
                nf, nr = f + df, r + dr
                if 0 <= nf < 8 and 0 <= nr < 8:
                    to = nr * 8 + nf
                    q = b[to]
                    if q == '.':
                        app((s, to, 'n', (), None))
                    elif col(q) != c and capturable(b, to):
                        app((s, to, 'n', (to,), None))
    return mv


def emperor_sq(b, c):
    e = 'E' if c == 0 else 'e'
    for i in range(64):
        if b[i] == e:
            return i
    return None


def attacked(b, s, by):
    """Can side `by` capture the piece standing on s?"""
    for m in pseudo(b, by):
        if s in m[3]:
            return True
    return False


def in_check(b, c):
    e = emperor_sq(b, c)
    return e is not None and attacked(b, e, 1 - c)


def castles(st):
    b, c, cr = st['b'], st['side'], st['castle']
    out = []
    home = 4 if c == 0 else 60
    E = 'E' if c == 0 else 'e'
    C = 'C' if c == 0 else 'c'
    if b[home] != E:
        return out
    ks, qs = ('K', 'Q') if c == 0 else ('k', 'q')
    opts = []
    if ks in cr and b[home + 3] == C and b[home + 1] == '.' and b[home + 2] == '.':
        opts.append(('K', [home, home + 1, home + 2], home + 2))
    if qs in cr and b[home - 4] == C and b[home - 1] == '.' and b[home - 2] == '.' and b[home - 3] == '.':
        opts.append(('Q', [home, home - 1, home - 2], home - 2))
    for kind, path, to in opts:
        ok = True
        for sq_ in path:
            tb = b[:]
            tb[home] = '.'
            tb[sq_] = E
            if attacked(tb, sq_, 1 - c):
                ok = False
                break
        if ok:
            out.append((home, to, kind, (), None))
    return out


CORNER = {0: 'Q', 7: 'K', 56: 'q', 63: 'k'}


def make(st, m):
    b = st['b'][:]
    frm, to, kind, caps, pr = m
    p = b[frm]
    cr = set(st['castle'])
    half = st['half'] + 1
    for x in caps:
        b[x] = '.'
        if x in CORNER:
            cr.discard(CORNER[x])
    if caps:
        half = 0
    if kind == 's':
        pass
    elif kind in 'KQ':
        b[frm] = '.'
        b[to] = p
        if kind == 'K':
            b[frm + 3] = '.'
            b[frm + 1] = 'C' if p == 'E' else 'c'
        else:
            b[frm - 4] = '.'
            b[frm - 1] = 'C' if p == 'E' else 'c'
    else:
        b[frm] = '.'
        if pr:
            b[to] = pr if p == 'L' else pr.lower()
        else:
            b[to] = p
        if p in 'Ll':
            half = 0
    if p == 'E':
        cr.discard('K'); cr.discard('Q')
    elif p == 'e':
        cr.discard('k'); cr.discard('q')
    if frm in CORNER:
        cr.discard(CORNER[frm])
    return {'b': b, 'side': 1 - st['side'], 'castle': frozenset(cr), 'half': half}


def legal(st):
    c = st['side']
    out = []
    for m in pseudo(st['b'], c) + castles(st):
        nb = make(st, m)['b']
        if not in_check(nb, c):
            out.append(m)
    return out


# ---------------- notation ----------------

def san_core(st, m, all_legal):
    b = st['b']
    frm, to, kind, caps, pr = m
    p = b[frm].upper()
    if kind == 'K':
        return 'O-O'
    if kind == 'Q':
        return 'O-O-O'
    if p == 'L':
        s = (FILES[frm % 8] + 'x' + name(to)) if caps else name(to)
        if pr:
            s += '=' + pr
        return s
    rivals = [o for o in all_legal if o[1] == to and o[0] != frm and o[2] == kind
              and b[o[0]].upper() == p]
    dis = ''
    if rivals:
        if all(o[0] % 8 != frm % 8 for o in rivals):
            dis = FILES[frm % 8]
        elif all(o[0] // 8 != frm // 8 for o in rivals):
            dis = str(frm // 8 + 1)
        else:
            dis = name(frm)
    if kind == 's':
        return p + dis + 'x' + name(to) + '(S)'
    landing = b[to] != '.'
    s = p + dis + ('x' if landing else '') + name(to)
    if p == 'C':
        passed = [x for x in caps if x != to]
        if passed:
            s += '(x)'
    return s


def san(st, m, all_legal=None):
    if all_legal is None:
        all_legal = legal(st)
    s = san_core(st, m, all_legal)
    nst = make(st, m)
    if in_check(nst['b'], nst['side']):
        s += '#' if not legal(nst) else '+'
    return s


def norm(s):
    s = s.strip().replace('+', '').replace('#', '').replace('!', '').replace('?', '')
    s = s.replace('0-0-0', 'O-O-O').replace('0-0', 'O-O').replace('o-o', 'O-O')
    if s and not s.startswith('O-O') and len(s) > 1:
        # forgive caps after the piece letter: "AC3" -> "Ac3", "BxC7(s)" -> "Bxc7(S)"
        rest = s[1:].lower().replace('(s)', '(S)')
        if '=' in rest:
            i = rest.index('=')
            rest = rest[:i + 1] + rest[i + 1:].upper()
        s = s[0] + rest
    return s


def loose(s):
    return norm(s).replace('(x)', '').replace('x', '').replace('(S)', 'S').replace('=', '')


def parse(st, text):
    lm = legal(st)
    t = norm(text)
    sans = [(norm(san_core(st, m, lm)), m) for m in lm]
    for s, m in sans:
        if s == t:
            return m
    cands = [m for s, m in sans if loose(s) == loose(t)]
    if len(cands) == 1:
        return cands[0]
    raise ValueError('Illegal or ambiguous move: %s (legal: %s)' % (text, ', '.join(s for s, _ in sans)))


# ---------------- evaluation & search ----------------

CENTER = [0] * 64
for _i in range(64):
    _f, _r = _i % 8, _i // 8
    CENTER[_i] = int(14 - 4 * (abs(_f - 3.5) + abs(_r - 3.5)) / 1.0)
LADV = [0, 0, 4, 10, 22, 40, 70, 0]


def evaluate(st):
    b = st['b']
    score = 0
    material = 0
    for i in range(64):
        p = b[i]
        if p == '.':
            continue
        t = p.upper()
        c = 0 if p.isupper() else 1
        r = i // 8
        rr = r if c == 0 else 7 - r
        v = VAL[t]
        if t != 'E' and t != 'L':
            material += VAL[t]
        if t == 'L':
            v += LADV[rr]
            if phalanx(b, i):
                v += 12
            f = i % 8
            if 2 <= f <= 5:
                v += 4
        elif t == 'A' or t == 'B':
            v += CENTER[i]
            if rr == 0:
                v -= 12
        elif t == 'G':
            if gen_immune(b, i):
                v += 10
            v += CENTER[i] // 3
        elif t == 'C':
            v += 4 * min(rr, 5) // 2
        score += v if c == 0 else -v
    # emperor safety in the middlegame
    for c in (0, 1):
        e = emperor_sq(b, c)
        if e is None:
            continue
        rr = e // 8 if c == 0 else 7 - e // 8
        if material > 3000:
            pen = 18 * rr
            f = e % 8
            if f in (6, 2, 1):
                pen -= 15
        else:
            pen = -CENTER[e]
        score += -pen if c == 0 else pen
    return score if st['side'] == 0 else -score


class Search:
    def __init__(self, deadline):
        self.deadline = deadline
        self.nodes = 0
        self.stop = False

    def order(self, b, moves):
        def key(m):
            k = 0
            for x in m[3]:
                k += VAL[b[x].upper()] if b[x] != '.' else 0
            if m[3]:
                k += 1000 - VAL[b[m[0]].upper()] // 10
            if m[4]:
                k += 800
            return -k
        moves.sort(key=key)
        return moves

    def kills_emperor(self, b, c, moves):
        e = 'e' if c == 0 else 'E'
        for m in moves:
            for x in m[3]:
                if b[x] == e:
                    return True
        return False

    def qs(self, st, alpha, beta, ply, qdepth):
        self.nodes += 1
        b, c = st['b'], st['side']
        moves = pseudo(b, c)
        if self.kills_emperor(b, c, moves):
            return MATE - ply
        stand = evaluate(st)
        if stand >= beta or qdepth <= 0:
            return stand
        if stand > alpha:
            alpha = stand
        caps = [m for m in moves if m[3]]
        for m in self.order(b, caps):
            sc = -self.qs(make(st, m), -beta, -alpha, ply + 1, qdepth - 1)
            if sc >= beta:
                return sc
            if sc > alpha:
                alpha = sc
        return alpha

    def nm(self, st, depth, alpha, beta, ply):
        if self.nodes & 255 == 0 and time.time() > self.deadline:
            self.stop = True
        if self.stop:
            return 0
        self.nodes += 1
        b, c = st['b'], st['side']
        if pawn_wall(b):
            return 0  # the game ends here as a draw
        moves = pseudo(b, c)
        if self.kills_emperor(b, c, moves):
            return MATE - ply
        if depth <= 0:
            return self.qs(st, alpha, beta, ply, 4)
        moves = self.order(b, moves + castles(st))
        best = -INF
        for m in moves:
            sc = -self.nm(make(st, m), depth - 1, -beta, -alpha, ply + 1)
            if self.stop:
                return 0
            if sc > best:
                best = sc
            if sc > alpha:
                alpha = sc
            if alpha >= beta:
                break
        if best <= -(MATE - 500):
            if not in_check(b, c):
                return 0  # stalemate is a draw
        return best


def think(st, seconds=20.0, maxdepth=6):
    lm = legal(st)
    if not lm:
        return None, None
    deadline = time.time() + seconds
    srch = Search(deadline)
    best = lm[0]
    best_score = None
    order = srch.order(st['b'], lm[:])
    for depth in range(1, maxdepth + 1):
        cur_best, cur_score = None, -INF
        alpha = -INF
        for m in order:
            sc = -srch.nm(make(st, m), depth - 1, -INF, -alpha, 1)
            if srch.stop:
                break
            if sc > cur_score:
                cur_score, cur_best = sc, m
            if sc > alpha:
                alpha = sc
        if srch.stop:
            break
        best, best_score = cur_best, cur_score
        order.remove(best)
        order.insert(0, best)
        print('depth %d  score %d  best %s  nodes %d' % (depth, best_score, san_core(st, best, lm), srch.nodes),
              file=sys.stderr)
        if abs(best_score) > MATE - 500:
            break
    return best, best_score


def show(b):
    rows = []
    for r in range(7, -1, -1):
        rows.append(str(r + 1) + ' ' + ' '.join(b[r * 8 + f] for f in range(8)))
    rows.append('  a b c d e f g h')
    return '\n'.join(rows)


def load(path):
    st = initial()
    sans = []
    try:
        with open(path) as fh:
            for line in fh:
                line = line.strip()
                if line:
                    m = parse(st, line)
                    sans.append(line)
                    st = make(st, m)
    except FileNotFoundError:
        pass
    return st, sans


def game_over(st):
    if legal(st):
        return None
    return 'checkmate' if in_check(st['b'], st['side']) else 'stalemate (draw)'


def interactive(secs):
    print('Roman chess. Type a move (e4, Ac3, Cd5(x), Bxc7(S), O-O),')
    print('"moves" for legal moves, "board" to show the board, "quit" to stop.')
    side = input('Play as White or Black? [w/b] ').strip().lower()
    human = 1 if side.startswith('b') else 0
    st = initial()
    print(show(st['b']))
    while True:
        over = game_over(st)
        if over:
            print('GAME OVER:', over)
            break
        if st['half'] >= 100:
            print('GAME OVER: draw by 50-move rule')
            break
        if pawn_wall(st['b']):
            print('GAME OVER: draw by pawn wall')
            break
        if st['side'] == human:
            txt = input('\nYour move: ').strip()
            if txt in ('quit', 'exit', 'resign'):
                break
            if txt == 'moves':
                lm = legal(st)
                print(', '.join(sorted(san_core(st, m, lm) for m in lm)))
                continue
            if txt == 'board':
                print(show(st['b']))
                continue
            try:
                m = parse(st, txt)
            except ValueError:
                print('Illegal or ambiguous move. Type "moves" to see the legal ones.')
                continue
            s = san(st, m)
            st = make(st, m)
            print('You:', s)
        else:
            print('\nThinking...')
            m, sc = think(st, secs)
            s = san(st, m)
            st = make(st, m)
            print('Engine:', s)
        print(show(st['b']))


if __name__ == '__main__':
    secs = 20.0
    args = sys.argv[1:]
    if '--think' in args:
        i = args.index('--think')
        secs = float(args[i + 1])
        args = args[:i] + args[i + 2:]
    if not args:
        try:
            interactive(secs)
        except (KeyboardInterrupt, EOFError):
            print()
        sys.exit()
    path = args[0]
    args = args[1:]
    st, sans = load(path)
    if args:
        m = parse(st, args[0])
        s = san(st, m)
        st = make(st, m)
        with open(path, 'a') as fh:
            fh.write(s + '\n')
        print('YOU:', s)
    if not legal(st):
        print('GAME OVER:', 'checkmate' if in_check(st['b'], st['side']) else 'stalemate')
        print(show(st['b']))
        sys.exit()
    if pawn_wall(st['b']):
        print('GAME OVER: draw by pawn wall')
        print(show(st['b']))
        sys.exit()
    m, sc = think(st, secs)
    s = san(st, m)
    st = make(st, m)
    with open(path, 'a') as fh:
        fh.write(s + '\n')
    print('ME:', s, ' eval(for me):', sc)
    print(show(st['b']))
    if not legal(st):
        print('GAME OVER:', 'checkmate' if in_check(st['b'], st['side']) else 'stalemate')
    elif pawn_wall(st['b']):
        print('GAME OVER: draw by pawn wall')
