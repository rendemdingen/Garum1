# Garum 1

A bot that plays **Roman chess**. It has its own rules engine, notation and an alpha-beta search.

**Play against it in your browser: [chess.rendemdingen.com/roman/](https://chess.rendemdingen.com/roman/)**

## Pieces

| Letter | Piece     |
|--------|-----------|
| E      | Emperor   |
| G      | General   |
| C      | Chariot   |
| B      | Ballista  |
| A      | Cavalry   |
| L      | Legionary |

White is upper case, Black is lower case.

## Three versions

All three use the same rules, evaluation and search, and give the same moves.

| File         | Language   | Notes                                  |
|--------------|------------|----------------------------------------|
| `garum1.py`  | Python     | The original                           |
| `garum1.js`  | JavaScript | Node command line, or `window.Garum1` in a browser |
| `garum1.cpp` | C++        | About 6 to 10 times faster             |

## Running it

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
