#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

#include "Engine/Chess.hpp"

struct SearchLimit {
    bool ByDepth{false};
    int TimeMs{250};
    int Depth{12};
    bool UseBook{true};
};

class EngineRunner final {
public:
    struct Result {
        Chess::Move Move;
        bool Suggestion;
    };

private:
    void joinAll();

    Chess::Board& m_Board;

    std::thread m_SearchThread;
    std::thread m_PonderThread;

    std::atomic<bool> m_Thinking{false};

    std::mutex m_Mutex;
    std::optional<Result> m_Result;

public:
    explicit EngineRunner(Chess::Board& board) : m_Board(board) {}
    ~EngineRunner();

    EngineRunner(const EngineRunner&) = delete;
    EngineRunner& operator=(const EngineRunner&) = delete;

    void Search(const Chess::Board& board, Chess::PieceColor side, const SearchLimit& limit, bool suggestion);

    void Ponder(Chess::Board board, Chess::PieceColor opponent);
    void StopPonder();

    void Skip();
    void Reset();

    [[nodiscard]]
    inline bool Thinking() const noexcept {
        return m_Thinking;
    }

    [[nodiscard]]
    std::optional<Result> Take();
};
