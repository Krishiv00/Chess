# ♟ Chess++

A high-performance chess engine written from scratch in **C++**, featuring a bitboard-based move generator, advanced search techniques, and multiple interfaces:

* an interactive **SFML GUI**
* a fully compliant **UCI (Universal Chess Interface) implementation**

The project demonstrates systems programming, algorithm design, and performance-oriented architecture, with all core engine logic implemented without external libraries.

---

# Overview

This project implements a complete chess engine with both graphical and command-line interfaces. All core systems - move generation, search, evaluation, and hashing - are written from first principles.

The engine follows the full rules of chess and incorporates modern search optimizations used in competitive engines.

The repository is structured to emphasize **clarity, performance, and modular design**, serving both as a functional engine and a demonstration of engine architecture.

---

# Architecture

The project is organized into three primary components:

* **Engine**
  Core chess logic including move generation, search, evaluation, and hashing.

* **GUI**
  An SFML-based graphical interface for interactive play.

* **UCI Interface**
  A command-line interface implementing the Universal Chess Interface protocol, enabling integration with external chess GUIs such as Arena or Cute Chess.

Both GUI and UCI layers are built on top of the same engine, ensuring consistent behavior across all interfaces.

---

# Board Representation

The engine uses a **hybrid representation** combining:

* **Bitboards** for efficient piece set operations
* **Mailbox array** for fast piece lookup on individual squares

This design balances performance in move generation with simplicity in move execution and evaluation.

---

# Search

The engine uses a **negamax alpha-beta search** with iterative deepening.

Enhancements include:

* aspiration windows
* null-move pruning
* probcut
* reverse futility pruning
* late move reductions (LMR) with history-based reduction adjustments
* late move pruning
* singular extensions
* quiescence search with delta pruning
* move ordering using history heuristics, killer moves, and transposition table entries
* transposition table with depth-preferred replacement
* repetition detection
* pondering (background thinking)

These techniques significantly reduce the search space and improve playing strength.

---

# Evaluation

Positions are scored using a **tapered evaluation model** that smoothly interpolates between midgame and endgame.

Key factors include:

* material balance with piece-square tables
* king safety (pawn shield, king danger from attacking pieces)
* pawn structure (passed pawns, doubled pawns, isolated pawns, connected passed pawns)
* piece mobility (knights, bishops, rooks, queens)
* endgame-specific evaluation (king centralization, forcing opponent king to corners)
* rook placement (open and semi-open files)
* bishop pair bonus
* tempo bonus

Evaluation is symmetric and always calculated relative to the side to move.

---

# Move Generation

The engine implements **fully legal move generation** with proper handling of:

* pinned pieces
* discovered checks
* en passant captures
* castling legality
* check detection integrated during move generation

All special moves are correctly flagged for search and evaluation purposes.

---

# Graphical Interface

The GUI is built using **SFML** and provides:

* drag-and-drop and click-based piece movement
* legal move highlighting with capture indicators
* smooth piece animations with piece tilting during drag
* evaluation bar visualization with live updating
* game over detection (checkmate, stalemate, draws) with particle effects
* sound effects for moves, captures, checks, and special moves
* board flipping and reset controls
* **switchable themes** (Lichess, Chesscom) with distinct visual and behavioral differences
* **arrow drawing** (right-click drag between squares, toggleable by redrawing)
* **square markers** (right-click on square to toggle highlight)
* promotion menu with piece selection
* inspection mode for position analysis
* configurable engine parameters (think time, search depth, opening book, pondering, catch-all mode)
* FEN string copy and paste support (`Ctrl + C` / `Ctrl + V`)
* tooltip hints on button hover

This interface allows direct interaction with the engine without external tools.

## Theme System

The engine features a **runtime-switchable theme system** that replicates the look and feel of popular chess platforms like `Lichess` and `Chesscom`.

Themes control both visual appearance (colors, rendering styles) and behavioral differences (hover feedback, drag indicators), allowing faithful recreation of different platform experiences. The system is designed for easy extension - new themes can be added by inheriting from the `Theme` base class.

---

# UCI Interface

The project includes a standalone **UCI-compatible executable**.

Features:

* full UCI protocol support
* compatibility with external GUIs (Arena, Cute Chess, etc.)
* efficient command parsing and move handling
* configurable hash table size
* opening book support
* pondering support
* time control management with move overhead compensation
* shared search and evaluation logic with the GUI

This enables engine testing, benchmarking, and engine-vs-engine play.

### Supported Commands

The engine implements all **mandatory UCI commands** plus additional configuration options:

* `uci` - initialize UCI mode
* `isready` - synchronization check
* `ucinewgame` - reset internal state
* `position` - set up board state (FEN / move list)
* `go` - start search (supports time controls, depth, movetime, perft, infinite)
* `stop` - halt search
* `quit` - terminate engine
* `setoption` - configure engine parameters (Hash, Ponder, OwnBook, Move Overhead, Minimum Thinking Time, Contempt, CatchAll)

---

# Technical Highlights

* **Magic bitboards** for constant-time sliding piece attack generation
* **Incremental Zobrist hashing** for fast position hashing during search
* **Check detection integration** - moves are flagged with check status during generation
* **Separate pawn hash table** for cached pawn structure evaluation
* **Static exchange evaluation (SEE)** for accurate capture analysis
* **Layered pruning techniques** to reduce explored nodes
* **Opening book** with 3500+ positions from major opening lines
* **Runtime-switchable theme system** with behavioral polymorphism for theme-specific UX
* **Modular architecture** separating engine logic from interface layers

---

# Requirements

* **C++20 or later**
* **SFML 3.x** (for GUI only)

---

# Building

The project includes separate executables for GUI and UCI interfaces.

**GUI**: Requires SFML 3.x linked with the Chess engine source files.

**UCI**: Requires only standard C++ (no external dependencies).

---

# Testing

The engine includes a comprehensive test suite (`tests.cpp`) covering:

* Perft validation across many standard test positions
* Check detection accuracy
* Pin handling
* En passant edge cases
* Castling legality
* Draw detection (50-move rule, threefold repetition, insufficient material)

Run tests to verify correctness after modifications.

---

# Assets

The GUI requires graphical and audio assets in a `Resources/` directory next to the executable.

Expected structure:

```
Resources/
├── Textures/
│   ├── Pieces.png
│   └── Icons.png
├── Fonts/
│   └── Font.ttf
└── Sounds/
    ├── Capture.wav
    ├── Check.wav
    ├── Game End.wav
    ├── Move Opp.wav
    ├── Move Self.wav
    ├── Notify.wav
    ├── Promotion.wav
    ├── Illegal.wav
    └── Special Move.wav
```

## Piece Texture

Chess pieces as a **texture atlas**:

```
[ WP | WB | WN | WR | WQ | WK ]
[ BP | BB | BN | BR | BQ | BK ]
```

## Icons

Icons in a **horizontal sprite sheet**:

```
[Flip | Engine | Cancel | Reset | Up | Down | Own Book On | Own Book Off | Ponder On | Ponder Off | CatchAll On | CatchAll Off | Inspection On | Inspection Off]
```

## Fonts

Any TrueType font (`.ttf`).

## Sounds

Audio files for game events (in `.wav` format).