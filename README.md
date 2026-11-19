# Garum 1

A bot that plays **Roman chess**. It has its own rules engine, notation and an alpha-beta search.

**Play against it in your browser: [chess.rendemdingen.com/roman/](https://chess.rendemdingen.com/roman/)**

## Pieces

Each Roman piece takes the place of a normal chess piece.

| Letter | Roman piece | Normal chess counterpart | How it differs                                      |
|--------|-------------|--------------------------|-----------------------------------------------------|
| E      | Emperor     | King                     | Same as the King                                    |
| G      | General     | Queen                    | Can't be captured while next to its own Emperor     |
| C      | Chariot     | Rook                     | Passes over its own pieces and runs over Legionaries |
| B      | Ballista    | Bishop                   | Can also shoot straight ahead instead of moving     |
| A      | Cavalry     | Knight                   | Can also charge 2 squares in a straight line        |
| L      | Legionary   | Pawn                     | Can't be captured while in a phalanx                |

## Rules of Roman chess

Everything not listed here works as in normal chess.

### Setup

The board is set up like normal chess, with each piece swapped for its Roman counterpart:

- **Rank 1 (White):** Chariot a1, Cavalry b1, Ballista c1, General d1, Emperor e1, Ballista f1, Cavalry g1, Chariot h1.
- **Rank 2 (White):** 8 Legionaries.
- **Black** mirrors White on ranks 8 and 7 (General on d8, Emperor on e8).
- White moves first.

### Legionary (L)

- Moves like a pawn: one square forward, or two from its starting square, if the squares are empty. Captures one square diagonally forward.
- There is **no en passant**.
- **Promotion:** on the last rank it becomes a General, Chariot, Ballista or Cavalry (never an Emperor).
- **Phalanx:** a Legionary with a Legionary of its own colour right beside it on the same rank (left or right) can't be captured by anything, except a Ballista arrow.

### Chariot (C)

- Moves like a rook, any number of squares in a straight line along a rank or file.
- It **passes over its own pieces** but can't stop on one.
- It **runs over enemy Legionaries** that are not in a phalanx and captures all of them. It may stop on any empty square past them, or on one of those Legionaries.
- After running over a Legionary, it may not capture any other enemy piece in that move. An enemy piece that isn't a Legionary stops it as usual (it may capture that piece only if it hasn't run over anything).
- An enemy Legionary in a phalanx blocks it like a wall.

### Ballista (B)

Each turn the Ballista does one of these:

- **Move** like a bishop, any number of squares diagonally.
- **Shoot** straight ahead along its own file (towards the enemy side, never backwards). The first piece in the way is hit. If it's an enemy piece, it is captured and the Ballista stays where it is. If it's one of your own pieces, the shot is blocked.

Arrows **ignore the phalanx**, so they can capture Legionaries in a phalanx. They can't capture a General guarded by its Emperor.

### Cavalry (A)

- Jumps like a knight, over any pieces.
- Or **charges** exactly 2 squares in a straight line, in any of the 8 directions (straight or diagonal). The square in between must be empty.

### General (G)

- Moves like a queen.
- **Guarded:** while it stands on any of the 8 squares around its own Emperor, it can't be captured by anything, arrows included.

### Emperor (E)

- Moves like a king, one square in any direction.
- Check and checkmate work as in normal chess. Any capture counts as an attack on the Emperor: a normal move, a Cavalry charge or a Ballista arrow.
- **Castling** works as in normal chess, with the Chariot in place of the Rook. The Emperor and that Chariot must not have moved, the squares between them must be empty, and the Emperor may not be in check, pass through check or land in check.
  - `O-O`: Emperor e1 to g1, Chariot h1 to f1.
  - `O-O-O`: Emperor e1 to c1, Chariot a1 to d1.

### End of the game

- **Win:** checkmate the enemy Emperor.
- **Draw** by any of these:
  - Stalemate: the side to move has no legal move and isn't in check.
  - Threefold repetition.
  - The 50-move rule: 50 moves by each side with no capture and no Legionary move.
  - Only the two Emperors are left.
  - **Pawn wall:** all 16 Legionaries are still on the board, each exactly two squares in front of where it started (White's on rank 4, Black's on rank 5). The game ends in a draw at once.

### Notation

Moves are written like normal chess notation (SAN), using the Roman letters:

| Example   | Meaning                                                      |
|-----------|--------------------------------------------------------------|
| `e4`      | Legionary to e4                                              |
| `exd5`    | Legionary on the e-file captures on d5                       |
| `e8=G`    | Legionary promotes to a General                              |
| `Ac3`     | Cavalry to c3                                                |
| `Gxd7`    | General captures on d7                                       |
| `Bxe7(S)` | Ballista shoots and captures the piece on e7 (it doesn't move) |
| `Ce5(x)`  | Chariot to e5, running over and capturing Legionaries on the way |
| `O-O`     | Castling                                                     |

`+` means check and `#` means checkmate.

## Three versions

All three use the same rules, evaluation and search, and give the same moves.

| File         | Language   | Notes                                  |
|--------------|------------|----------------------------------------|
| `garum1.py`  | Python     | The original                           |
| `garum1.js`  | JavaScript | Node command line, or `window.Garum1` in a browser |
| `garum1.cpp` | C++        | About 6 to 10 times faster             |

## Running it in the terminal

The game file holds one move per line (SAN). If you give a move, it is checked and added, then the bot replies and adds its own move. With no game file, the JS and C++ versions start a game in the terminal.

```bash
python3 garum1.py game.txt e4 --think 2
```

```bash
node garum1.js game.txt e4 --think 2
```

```bash
clang++ -std=c++17 -O2 -o garum1 garum1.cpp
./garum1 game.txt e4 --think 2 --depth 6
```

`--think` is the time limit in seconds. `--depth` (C++ only) is the deepest search, 6 by default.

### Reading the board in the terminal

This is only about the board the bot prints in the terminal, not about how moves are written.

```
8 c . b g e b a c
7 l l l l l l l l
6 . . a . . . . .
5 . . . . . . . .
4 . . . . L . . .
3 . . . . . . . .
2 L L L L . L L L
1 C A B G E B A C
  a b c d e f g h
```

- **Upper case** letters are White's pieces, **lower case** letters are Black's. For example, `E` is White's Emperor and `e` is Black's.
- `.` is an empty square.
- Moves always use upper-case piece letters for both sides. Black's reply above was `Ac6`, not `ac6`.

The terminal versions end a game on checkmate, stalemate, the 50-move rule and the pawn wall. They don't check threefold repetition or Emperor against Emperor; the website does.
