#pragma once

#include <iostream>
#include <thread>
#include <cstdint>

#include "Engine/Chess.hpp"

class UciHandler {
private:
    struct Options {
        static inline bool Ponder;
        static inline bool OwnBook;
        static inline uint16_t MoveOverhead;
        static inline uint16_t MinimumThinkingTime;
    };

    struct GoParams {
        int Wtime{-1};
        int Btime{-1};
        int Winc{0};
        int Binc{0};
        int Movestogo{0};
        int Movetime{-1};
        int Depth{-1};
        bool Perft{false};
        bool Infinite{false};
    };

    GoParams parseGoParams(const std::string& command) const;
    int computeThinkTime(const GoParams& params) const;

    void stopCurrentSearch();

    Chess::Move uciToMove(const std::string& uci) const;
    void doUciMove(const std::string& uci);

    void command_stop();
    void command_uci() const;
    void command_isready() const;
    void command_ucinewgame();
    void command_position(const std::string& command);
    void command_go(const std::string& command);
    void command_setoption(const std::string& command);
    void command_flip();
    void command_debug();

    Chess::Board m_Board;
    Chess::PieceColor m_SideToMove;
    Chess::PieceColor m_LastEngineColor;

    std::thread m_SearchThread;
    std::thread m_PonderThread;

public:
    UciHandler();
    ~UciHandler();

    void HandleCommand(const std::string& command);
};
