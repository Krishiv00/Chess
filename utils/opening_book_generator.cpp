#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cstring>

#define CHESS_EXPOSE_INTERNALS
#include "../include/Engine/Chess.hpp"

struct BookEntry {
    uint64_t Hash;
    uint8_t StartSquare;
    uint8_t TargetSquare;
    uint16_t Flag;
};

std::vector<BookEntry> g_BookEntries;

static void AddLine(const std::string& moves) {
    Chess::Board board;

    Chess::PieceColor sideToMove = board.LoadFromFen(Chess::DefaultFEN);

    std::istringstream iss(moves);
    std::string uciMove;

    unsigned int numMovesAdded = 0u;

    while (iss >> uciMove) {
        // Parse UCI move
        Chess::Move move(
            Chess::To2DIndex('8' - uciMove[1], uciMove[0] - 'a'),
            Chess::To2DIndex('8' - uciMove[3], uciMove[2] - 'a')
        );

        const std::vector<Chess::Move> legalMoves = board.GetLegalMoves(move.StartingSquare);

        const auto it = std::find(legalMoves.begin(), legalMoves.end(), move);

        // Move is illegal
        if (it == legalMoves.end()) {
            // delete the entire line
            if (numMovesAdded > 0u) {
                g_BookEntries.erase(g_BookEntries.end() - numMovesAdded, g_BookEntries.end());
            }

            break;
        }

        move = *it;

        // Check for promotion
        if (uciMove.length() == 5) {
            switch (uciMove[4]) {
            case 'q': move.Flag = Chess::AddFlag(move.Flag, Chess::MoveFlag::PromoteToQueen); break;
            case 'r': move.Flag = Chess::AddFlag(move.Flag, Chess::MoveFlag::PromoteToRook); break;
            case 'n': move.Flag = Chess::AddFlag(move.Flag, Chess::MoveFlag::PromoteToKnight); break;
            case 'b': move.Flag = Chess::AddFlag(move.Flag, Chess::MoveFlag::PromoteToBishop); break;
            }
        }

        // Add to book
        g_BookEntries.emplace_back(
            board.m_Hash,
            move.StartingSquare, move.TargetSquare,
            static_cast<uint16_t>(move.Flag)
        );

        ++numMovesAdded;

        // Make move
        board.DoMove(move);
        sideToMove = InvertColor(sideToMove);
    }
}

static void AddAllOpenings() {
    // ------------- 1. E4 -------------

    // Sicilian Defense: Closed Variation
    AddLine("e2e4 c7c5 b1c3");

    // Sicilian Defense: Open Variation
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4");

    // Sicilian Defense: Najdorf Variation
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6");

    // Sicilian Defense: Alapin Variation
    AddLine("e2e4 c7c5 c2c3");

    // Sicilian Defense: Dragon Variation
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6");

    // Sicilian Defense: French Variation
    AddLine("e2e4 c7c5 g1f3 e7e6");

    // French Defense: Tarrasch Variation
    AddLine("e2e4 e7e6 d2d4 d7d5 b1d2");

    // French Defense: Winawer Variation
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 f8b4");

    // French Defense: Classical French
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5");

    // French Defense: Advance Variation
    AddLine("e2e4 e7e6 d2d4 d7d5 e4e5");

    // French Defense: Exchange Variation
    AddLine("e2e4 e7e6 d2d4 d7d5 e4d5 e6d5");

    // Ruy Lopez Opening: Main Line
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1");

    // Ruy Lopez Opening: Closed Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7");

    // Ruy Lopez Opening: Berlin Defense
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 g8f6");

    // Ruy Lopez Opening: Exchange Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6");

    // Ruy Lopez Opening: Open Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f6e4");

    // Ruy Lopez Opening: Schliemann-Jaenisch Gambit
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 f7f5");

    // Ruy Lopez Opening: Marshall Attack
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7");

    // Caro-Kann Defense: Classical Variation
    AddLine("e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5");

    // Caro-Kann Defense: Karpov Variation
    AddLine("e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 b8d7");

    // Caro-Kann Defense: 4...Nf6 Variations
    AddLine("e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 g8f6");

    // Caro-Kann Defense: Advance Variation
    AddLine("e2e4 c7c6 d2d4 d7d5 e4e5");

    // Caro-Kann Defense: Exchange Variation
    AddLine("e2e4 c7c6 d2d4 d7d5 e4d5 c6d5");

    // Caro-Kann Defense: Panov-Botvinnik Attack
    AddLine("e2e4 c7c6 d2d4 d7d5 e4d5 c6d5 c2c4");

    // Caro-Kann Defense: Fantasy Variation
    AddLine("e2e4 c7c6 d2d4 d7d5 f2f3");

    // Italian Game: Giuoco Piano
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5");

    // Italian Game: Giuoco Pianissimo
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3");

    // Italian Game: Evans Gambit
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 b2b4");

    // Scandinavian Defense: 2...Qxd5
    AddLine("e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6 g1f3 c8f5");

    // Scandinavian Defense: 2...Nf6
    AddLine("e2e4 d7d5 e4d5 g8f6 d2d4 f6d5 g1f3 b8c6");

    // Pirc Defense
    AddLine("e2e4 d7d6 d2d4 g8f6");

    // Alekhine's Defense: Modern Variation
    AddLine("e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 g1f3 c8g4");

    // Alekhine's Defense: Exchange Variation
    AddLine("e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 e5d6 c7d6");

    // Alekhine's Defense: Four Pawns Attack
    AddLine("e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 c2c4 d5b6 f2f4");

    // King's Gambit Accepted: King's Knight's Gambit
    AddLine("e2e4 e7e5 f2f4 e5f4 g1f3");

    // King's Gambit Accepted: Bishop's Gambit
    AddLine("e2e4 e7e5 f2f4 e5f4 f1c4");

    // King's Gambit Declined: Falkbeer Countergambit
    AddLine("e2e4 e7e5 f2f4 d7d5");

    // King's Gambit Declined: Classical Variation
    AddLine("e2e4 e7e5 f2f4 f8c5");

    // Scotch Game: Schmidt Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6");

    // Scotch Game: Classical Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 f8c5");

    // Scotch Game: Scotch Gambit
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f1c4");

    // Scotch Game: Goring Gambit
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 c2c3");

    // Scotch Game: Malaniuk Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 f8b4");

    // Scotch Game: Schmidt Variation With 5.Nxc6
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 d4c6");

    // Scotch Game: Classical Variation With 5.Nxc6
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 f8c5 d4c6");

    // Vienna Game: Falkbeer Variation
    AddLine("e2e4 e7e5 b1c3 g8f6");

    // Vienna Game: Falkbeer, Mieses Variation
    AddLine("e2e4 e7e5 b1c3 g8f6 g2g3");

    // Vienna Game: Vienna Gambit
    AddLine("e2e4 e7e5 b1c3 g8f6 f2f4");

    // Vienna Game: Max Lange Defense
    AddLine("e2e4 e7e5 b1c3 b8c6");

    // Vienna Game: Anderssen Defense
    AddLine("e2e4 e7e5 b1c3 f8c5");

    // ------------- 1. D4 -------------

    // Queen's Gambit Declined
    AddLine("d2d4 d7d5 c2c4 e7e6");

    // Queen's Gambit Declined: Three Knights Variation
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3");

    // Queen's Gambit Declined: Queen's Knight Variation
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3");

    // Queen's Gambit Accepted
    AddLine("d2d4 d7d5 c2c4 d5c4");

    // Queen's Gambit Declined: Exchange Variation
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c4d5");

    // Slav Defense: Main Line
    AddLine("d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5 e2e3 e7e6 f1c4");

    // Slav Defense: Modern Line, Quiet Variation
    AddLine("d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 e2e3");

    // Slav Defense: Semi-Slav
    AddLine("d2d4 d7d5 c2c4 c7c6 g1f3 e7e6");

    // Slav Defense: Chameleon (Chebanenko Slav)
    AddLine("d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 a7a6");

    // Slav Defense: Exchange Variation
    AddLine("d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 c4d5");

    // King's Indian Defense: Main Line
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 e1g1 b8c6");

    // King's Indian Defense: Samisch
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f3");

    // King's Indian Defense: Averbakh
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f1e2 e8g8 c1g5");

    // King's Indian Defense: Petrosian
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 d4d5");

    // King's Indian Defense: Four Pawns Attack
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f4");

    // Nimzo-Indian Defense: Rubinstein Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5 g1f3 c7c5 e1g1 b8c6");

    // Nimzo-Indian Defense: Hubner Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 c7c5 f1d3 b8c6 g1f3 b4c3 b2c3 d7d6");

    // Nimzo-Indian Defense: Classical Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 d1c2 e8g8 a2a3 b4c3 c2c3 b7b6 c1g5");

    // Nimzo-Indian Defense: Three Knights Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 g1f3 c7c5 e2e3 d7d5 f1d3 b8c6 e1g1");

    // Nimzo-Indian Defense: Kmoch Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 c7c5 g1e2 c5d4 e3d4 d7d5");

    // Nimzo-Indian Defense: Samisch Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 a2a3 b4c3 b2c3 c7c5 f2f3 d7d5 c4d5 e6d5");

    // Nimzo-Indian Defense: Other Fourth Moves
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 g2g3 d7d5 f1g2 e8g8 g1f3 c7c5");

    // Queen's Indian Defense: Fianchetto Nimzowitsch Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8a6");

    // Queen's Indian Defense: Fianchetto Traditional Line
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7");

    // Queen's Indian Defense: Petrosian Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 a2a3");

    // Queen's Indian Defense: Kasparov Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 b1c3");

    // Queen's Indian Defense: Spassky System
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 e2e3 c8b7");

    // Catalan Opening: Open Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g2g3 d7d5 f1g2 d5c4");

    // Catalan Opening: Closed Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g2g3 d7d5 f1g2 f8e7 g1f3 e8g8 e1g1 c7c6");

    // Catalan Opening: Anti-Catalan
    AddLine("d2d4 g8f6 c2c4 e7e6 g2g3 f8b4 c1d2");

    // Bogo-Indian Defense: Nimzowitsch Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 f8b4 c1d2 d8e7");

    // Bogo-Indian Defense: Wade-Smyslov Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 f8b4 c1d2 a7a5");

    // Bogo-Indian Defense: Vitolins Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 f8b4 c1d2 c7c5");

    // Bogo-Indian Defense: Exchange Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 f8b4 c1d2 b4d2");

    // Bogo-Indian Defense: Grunfeld Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 f8b4 b1d2 b7b6");

    // Grunfeld Defense: Exchange Variation
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5 e2e4 d5c3 b2c3");

    // Grunfeld Defense: Russian Variation
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 g1f3 f8g7 d1b3 d5c4 b3c4");

    // Grunfeld Defense: Petrosian System
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 g1f3 f8g7 c1g5 f6e4 g5h4 e4c3 b2c3 d5c4");

    // Grunfeld Defense: Brinckmann Attack
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c1f4 f8g7 e2e3 e8g8");

    // Grunfeld Defense: Stockholm Variation
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c1g5 f6e4 g5h4 e4c3 b2c3 d5c4");

    // Dutch Defense: Leningrad Variation
    AddLine("d2d4 f7f5 g2g3 g8f6 f1g2 g7g6 g1f3 f8g7 e1g1 e8g8 c2c4 d7d6");

    // Dutch Defense: Stonewall Formation
    AddLine("d2d4 f7f5 g2g3 g8f6 f1g2 e7e6 c2c4 d7d5 g1f3 c7c6");

    // Dutch Defense: Classical Variation
    AddLine("d2d4 f7f5 g2g3 g8f6 g1f3 e7e6 c2c4 f8e7");

    // Dutch Defense: Hopton Attack
    AddLine("d2d4 f7f5 c1g5");

    // Dutch Defense: Staunton Gambit
    AddLine("d2d4 f7f5 e2e4 f5e4 b1c3");

    // Trompowsky Attack: Ne4 Variation
    AddLine("d2d4 g8f6 c1g5 f6e4 g5f4 c7c5 f2f3 e4f6 d4d5");

    // Trompowsky Attack: e6 Variation
    AddLine("d2d4 g8f6 c1g5 e7e6 e2e4 h7h6 g5f6 d8f6");

    // Trompowsky Attack: g6 Variation
    AddLine("d2d4 g8f6 c1g5 g7g6 g5f6 e7f6");

    // Benko Gambit: Fully Accepted
    AddLine("d2d4 g8f6 c2c4 c7c5 d4d5 b7b5 c4b5 a7a6 b5a6 f8a6 b1c3 g7g6 g1f3 d7d6 g2g3 f8g7 f1g2");

    // Benko Gambit: Half-Accepted
    AddLine("d2d4 g8f6 c2c4 c7c5 d4d5 b7b5 c4b5 a7a6 b5a6 c8a6 g1f3");

    // Benko Gambit: Declined Main Line
    AddLine("d2d4 g8f6 c2c4 c7c5 d4d5 b7b5 g1f3");

    // London System: Main Line
    AddLine("d2d4 d7d5 c1f4 g8f6 e2e3 c7c5 c2c3 b8c6 b1d2 e7e6 g1f3 f8d6 f4g3 e8g8 d1e2 b7b6");

    // London System: vs. King's Indian Setup
    AddLine("d2d4 g8f6 c1f4 g7g6 e2e3 f8g7 g1f3 e8g8 f1e2 d7d6 h2h3");

    // London System: Jobava London
    AddLine("d2d4 d7d5 c1f4 g8f6 b1c3");

    // Benoni Defense: Modern Variation
    AddLine("d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6 e2e4 g7g6 g1f3 f8g7 f1e2 e8g8");

    // Benoni Defense: Modern, Four Pawns Attack
    AddLine("d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6 e2e4 g7g6 f2f4");

    // Benoni Defense: Modern, Classical Variation
    AddLine("d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6 e2e4 g7g6 g1f3 f8g7 f1e2 e8g8 e1g1 a7a6 a2a4");

    // Benoni Defense: Snake Benoni
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 c7c5 d4d5 e6d5 c4d5 f8d6");

    // Reti Opening: Reti Gambit
    AddLine("g1f3 d7d5 c2c4");

    // Reti Opening: Reti Gambit, 2...d4
    AddLine("g1f3 d7d5 c2c4 d5d4 g2g3");

    // Reti Opening: King's Indian Attack vs. d5
    AddLine("g1f3 d7d5 g2g3 g8f6 f1g2 g7g6 e1g1");

    // Reti Opening: vs. c5
    AddLine("g1f3 c7c5 c2c4");

    // English Opening: Symmetrical Variation
    AddLine("c2c4 c7c5 b1c3 b8c6 g1f3 g8f6 g2g3 g7g6 f1g2 f8g7 e1g1 e8g8");

    // English Opening: King's English Variation
    AddLine("c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 f8b4");

    // English Opening: Anglo-Indian, Queen's Knight Defense
    AddLine("c2c4 g8f6 b1c3 e7e6 e2e4");

    // English Opening: Four Knights Variation
    AddLine("c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 e2e4");

    // English Opening: Reversed Sicilian
    AddLine("c2c4 e7e5 g1f3 e5e4 f3g5");

    // Bird's Opening: Dutch Variation
    AddLine("f2f4 d7d5 g1f3");

    // Bird's Opening: From's Gambit
    AddLine("f2f4 e7e5 f4e5 d7d6 e5d6 f8d6");

    // Bird's Opening: Sicilian Bird
    AddLine("f2f4 c7c5 g1f3 b8c6 e2e3 g8f6 f1b5");

    // Bird's Opening: Standard Development
    AddLine("f2f4 g8f6 g1f3 g7g6 e2e3 f8g7 f1e2 e8g8");

    // King's Indian Attack: Yugoslav Variation (vs. d5+Nf6)
    AddLine("g1f3 g8f6 g2g3 d7d5 f1g2 g7g6 e1g1");

    // King's Indian Attack: vs. French setup
    AddLine("g1f3 d7d5 g2g3 g8f6 f1g2 e7e6 e1g1 f8e7 d2d3 e8g8 b1d2 c7c4 e2e4");

    // King's Indian Attack: Keres Variation
    AddLine("g1f3 d7d5 g2g3 c7c5 f1g2 b8c6 e1g1 e7e5");

    // King's Indian Attack: Reversed King's Indian
    AddLine("g1f3 c7c5 g2g3 b8c6 f1g2 g7g6 e1g1 f8g7 d2d3 e7e5");

    // King's Fianchetto Opening
    AddLine("g2g3 d7d5 f1g2 g8f6 g1f3 g7g6 e1g1 f8g7");

    // King's Fianchetto vs. e5
    AddLine("g2g3 e7e5 f1g2 g8f6 d2d3 d7d5 g1f3 f8e7");

    // Nimzowitsch-Larsen Attack: Classical Variation
    AddLine("b2b3 e7e5 c1b2 b8c6 e2e3 g8f6 f1b5");

    // Nimzowitsch-Larsen Attack: Modern Variation
    AddLine("b2b3 d7d5 c1b2 g8f6 g1f3 g7g6 g2g3 f8g7 f1g2 e8g8");

    // Nimzowitsch-Larsen Attack: Indian Variation
    AddLine("b2b3 g8f6 c1b2 g7g6 g1f3 f8g7 g2g3 e8g8 f1g2 d7d6");

    // Nimzowitsch-Larsen Attack: English Variation
    AddLine("b2b3 e7e5 c1b2 b8c6 c2c4");

    // Polish Opening: Main Line
    AddLine("b2b4 e7e5 c1b2 f8b4 e2e4");

    // Polish Opening: Outflank Variation
    AddLine("b2b4 c7c6 c1b2 a7a5 b4a5 d8a5 g1f3");

    // Polish Opening: Symmetrical Variation
    AddLine("b2b4 b7b5 e2e3 c1b7 c2c4");

    // Polish Opening: King's Indian Variation
    AddLine("b2b4 g8f6 c1b2 g7g6 g1f3 f8g7 e2e4");

    // Grob Opening: Main Line
    AddLine("g2g4 d7d5 c1g5 g8f6 g5f6 e7f6 h2h3 c7c5 d2d3");

    // Grob Opening: Grob Gambit
    AddLine("g2g4 d7d5 g4g5 c8e6 g1f3 g8f6");

    // Grob Opening: vs. e5
    AddLine("g2g4 e7e5 c1g5 g8f6 g5f6 d8f6 g1f3 b8c6");
}

int main() {
    Chess::Init();

    AddAllOpenings();

    // Sort by hash
    std::sort(g_BookEntries.begin(), g_BookEntries.end(),
        [](const BookEntry& a, const BookEntry& b) {
            if (a.Hash != b.Hash) return a.Hash < b.Hash;
            if (a.StartSquare != b.StartSquare) return a.StartSquare < b.StartSquare;
            else return a.TargetSquare < b.TargetSquare;
        }
    );

    // Remove duplicates
    auto last = std::unique(g_BookEntries.begin(), g_BookEntries.end(),
        [](const BookEntry& a, const BookEntry& b) {
            return (
                a.Hash == b.Hash &&
                a.StartSquare == b.StartSquare &&
                a.TargetSquare == b.TargetSquare
            );
        }
    );

    g_BookEntries.erase(last, g_BookEntries.end());

    // Calculate optimal column count based on sqrt of number of entries
    const std::size_t numEntries = g_BookEntries.size();
    const std::size_t colSize = static_cast<std::size_t>(std::sqrt(numEntries));

    std::cout << "{" << std::endl;

    for (std::size_t i = 0; i < numEntries; ++i) {
        const auto& e = g_BookEntries[i];

        // Print indentation at start of row
        if (i % colSize == 0) {
            std::cout << "    ";
        }

        // Print entry
        std::cout << "{0x" << std::hex << e.Hash << std::dec
            << ", " << +e.StartSquare
            << ", " << +e.TargetSquare
            << ", " << e.Flag << "}";

        // Add comma if not the last entry
        if (i < numEntries - 1) {
            std::cout << ", ";
        }

        // Newline at end of row or at the end
        if ((i + 1) % colSize == 0 || i == numEntries - 1) {
            std::cout << std::endl;
        }
    }

    std::cout << "};" << std::endl << std::endl;

    // Print statistics
    std::cerr << "Opening book generated successfully!" << std::endl;
    std::cerr << "Total entries: " << numEntries << std::endl;
    std::cerr << "Entries per row: " << colSize << std::endl;

    return 0;
}