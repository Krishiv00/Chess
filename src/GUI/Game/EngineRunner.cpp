#include "GUI/Game/EngineRunner.hpp"

#include <limits>

EngineRunner::~EngineRunner() {
    Reset();
}

void EngineRunner::joinAll() {
    m_Board.CancelSearch();

    if (m_PonderThread.joinable()) m_PonderThread.join();
    if (m_SearchThread.joinable()) m_SearchThread.join();
}

void EngineRunner::Search(
    const Chess::Board& board, Chess::PieceColor side, const SearchLimit& limit, bool suggestion
) {
    if (m_Thinking) return;

    joinAll();

    m_Thinking = true;

    m_SearchThread = std::thread([this, searchBoard = board, side, limit, suggestion]() {
        const Chess::Move best = limit.ByDepth
            ? searchBoard.FindBestMoveByDepth(side, limit.Depth, limit.UseBook)
            : searchBoard.FindBestMoveByTime(side, limit.TimeMs, limit.UseBook);

        std::lock_guard lock(m_Mutex);
        m_Thinking = false;
        m_Result = Result{best, suggestion};
    });
}

void EngineRunner::Ponder(Chess::Board board, Chess::PieceColor opponent) {
    if (m_Thinking) return;

    joinAll();

    const Chess::Move reply = board.FindBestMoveByDepth(opponent, 1);

    if (!reply) return;

    board.DoMove(reply);

    const Chess::PieceColor engine = Chess::InvertColor(opponent);

    if (!board.HasAnyLegalMoves(engine)) return;

    m_PonderThread = std::thread([ponderBoard = board, engine]() {
        (void)ponderBoard.FindBestMoveByTime(engine, std::numeric_limits<int>::max(), false);
    });
}

void EngineRunner::StopPonder() {
    if (!m_PonderThread.joinable()) return;

    m_Board.CancelSearch();
    m_PonderThread.join();
}

void EngineRunner::Skip() {
    if (m_Thinking) m_Board.CancelSearch();
}

void EngineRunner::Reset() {
    joinAll();

    std::lock_guard lock(m_Mutex);
    m_Result.reset();
    m_Thinking = false;
}

std::optional<EngineRunner::Result> EngineRunner::Take() {
    std::lock_guard lock(m_Mutex);

    std::optional<Result> result;
    result.swap(m_Result);

    return result;
}
