#include <vector>

#include "UCI/UCI.hpp"

// --------- Utils ---------

namespace Utils {
    [[nodiscard]]
    static std::vector<std::string> Tokenize(const std::string& string, char sep = ' ') {
        std::vector<std::string> splitted;

        std::string buffer;
        buffer.reserve(string.size());

        for (const char c : string) {
            if (c == sep) {
                if (!buffer.empty()) {
                    splitted.push_back(buffer);
                    buffer.clear();
                }
            } else {
                buffer.push_back(c);
            }
        }

        if (!buffer.empty()) splitted.push_back(buffer);

        return splitted;
    }

    [[nodiscard]]
    static Chess::MoveFlag CharToPromo(char c) noexcept {
        if (c == 'q') return Chess::MoveFlag::PromoteToQueen;
        if (c == 'r') return Chess::MoveFlag::PromoteToRook;
        if (c == 'b') return Chess::MoveFlag::PromoteToBishop;
        return Chess::MoveFlag::PromoteToKnight;
    }

    [[nodiscard]]
    static inline bool StartsWith(const std::string& string, const std::string& prefix) noexcept {
        return string.rfind(prefix, 0) == 0;
    }
}

// --------- Commands ---------

void UciHandler::command_stop() {
    stopCurrentSearch();
}

void UciHandler::command_uci() const {
    Options::Ponder = true;
    Options::OwnBook = true;
    Options::MoveOverhead = 10;
    Options::MinimumThinkingTime = 30;
    m_Board.SetHashSize(128);
    m_Board.SetThreads(1);

    std::cout
        // Bio
        << "id name Chess++" << std::endl
        << "id author Krishiv" << std::endl
        // Expose options
        << "option name Move Overhead type spin default " << Options::MoveOverhead << " min 0 max 500" << std::endl
        << "option name Threads type spin default " << m_Board.GetThreads() << " min 1 max 4" << std::endl
        << "option name Hash type spin default " << m_Board.GetHashSize() << " min 1 max 1024" << std::endl
        << "option name SyzygyPath type string default <empty>" << std::endl
        << "option name UCI_ShowWDL type check default false" << std::endl
        << "option name Clear Hash type button" << std::endl
        << "option name Minimum Thinking Time type spin default " << Options::MinimumThinkingTime << " min 0 max 5000" << std::endl
        << "option name Ponder type check default " << (Options::Ponder ? "true" : "false") << std::endl
        << "option name OwnBook type check default " << (Options::OwnBook ? "true" : "false") << std::endl
        << "option name CatchAll type check default " << (m_Board.GetCatchAll() ? "true" : "false") << std::endl
        // Complete Handshake
        << "uciok" << std::endl;
}

void UciHandler::command_isready() const {
    std::cout << "readyok" << std::endl;
}

void UciHandler::command_ucinewgame() {
    stopCurrentSearch();

    m_Board.NewGame();
    m_SideToMove = m_Board.LoadFromFen(Chess::DefaultFEN);
}

void UciHandler::command_position(const std::string& command) {
    stopCurrentSearch();

    const std::vector<std::string> tokens = Utils::Tokenize(command);

    // start after the "position" token
    unsigned int currentToken = 1u;

    const auto HasToken = [&]() -> bool { return currentToken < tokens.size(); };
    const auto ConsumeToken = [&]() -> const std::string& { return tokens[currentToken++]; };

    if (!HasToken()) {
        std::cerr << "Invalid usage of position command: \"" << command << "\"" << std::endl;
        return;
    }

    // load starting state

    if (ConsumeToken() == "fen") {
        std::string fen;

        for (int i = 0; i < 6; ++i) {
            if (!HasToken()) {
                std::cerr << "Invalid FEN\n"; return;
            }

            fen += ConsumeToken();
            if (i < 5) fen += " ";
        }

        m_SideToMove = m_Board.LoadFromFen(fen);
    } else {
        m_SideToMove = m_Board.LoadFromFen(Chess::DefaultFEN);
    }

    // do all moves (if any)
    if (HasToken() && ConsumeToken() == "moves") {
        while (HasToken()) doUciMove(ConsumeToken());
    }
}

void UciHandler::command_go(const std::string& command) {
    stopCurrentSearch();

    m_Board.SetEngineColor(m_SideToMove);

    // clear hash if engine color has changed this game
    // since hash values are misleading now
    if (m_Board.GetCatchAll()) {
        if (m_SideToMove != m_LastEngineColor) {
            m_Board.ClearHash();
        }

        m_LastEngineColor = m_SideToMove;
    }

    m_SearchThread = std::thread([this, command]() -> void {
        const GoParams params = parseGoParams(command);

        if (
            m_Board.GetCatchAll() &&
            (m_SideToMove == Chess::PieceColor::Black ? params.Btime : params.Wtime) <= 10000
        ) {
            m_Board.SetCatchAll(false);
            m_Board.ClearHash();
        }

        if (params.Perft) {
            m_Board.Perft(params.Depth, m_SideToMove); return;
        }

        Chess::Move bestMove;

        if (params.Infinite) {
            bestMove = m_Board.FindBestMoveByTime(m_SideToMove, std::numeric_limits<int>::max(), false);
        } else if (params.Depth > 0) {
            bestMove = m_Board.FindBestMoveByDepth(m_SideToMove, params.Depth, Options::OwnBook);
        } else {
            bestMove = m_Board.FindBestMoveByTime(m_SideToMove, computeThinkTime(params), Options::OwnBook);
        }

        std::cout << "bestmove " << Chess::MoveToUCI(bestMove) << std::endl;

        if (Options::Ponder) {
            m_PonderThread = std::thread([this, bestMove]() -> void {
                Chess::Board ponderBoard = m_Board;
                Chess::PieceColor ponderSideToMove = m_SideToMove;

                if (bestMove) {
                    ponderBoard.DoMove(bestMove);
                    ponderSideToMove = Chess::InvertColor(ponderSideToMove);
                }

                (void)ponderBoard.FindBestMoveByTime(ponderSideToMove, std::numeric_limits<int>::max(), false);
            });
        }
    });
}

void UciHandler::command_setoption(const std::string& command) {
    const std::vector<std::string> tokens = Utils::Tokenize(command);

    std::string name;
    std::string value;

    enum class ReadMode {
        Name, Value, None
    };

    ReadMode mode = ReadMode::None;

    for (size_t i = 1; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        if (token == "name") {
            mode = ReadMode::Name;
            continue;
        }

        if (token == "value") {
            mode = ReadMode::Value;
            continue;
        }

        if (mode == ReadMode::Name) {
            if (!name.empty()) name += " ";
            name += token;
        } else if (mode == ReadMode::Value) {
            if (!value.empty()) value += " ";
            value += token;
        }
    }

    // apply options
    if (name == "Hash") {
        m_Board.SetHashSize(std::stoi(value));
    }

    else if (name == "Threads") {
        m_Board.SetThreads(std::stoi(value));
    }

    else if (name == "Ponder") {
        Options::Ponder = value == "true";
        if (!Options::Ponder && m_PonderThread.joinable()) m_PonderThread.join();
    }

    else if (name == "OwnBook") {
        Options::OwnBook = value == "true";
    }

    else if (name == "CatchAll") {
        m_Board.SetCatchAll(value == "true");
        m_Board.ClearHash();
    }

    else if (name == "Clear Hash") {
        m_Board.ClearHash();
    }

    else if (name == "Move Overhead") {
        Options::MoveOverhead = std::stoi(value);
    }

    else if (name == "Contempt") {
        m_Board.SetContempt(std::stoi(value));
    }

    else if (name == "Minimum Thinking Time") {
        Options::MinimumThinkingTime = std::stoi(value);
    }
}

void UciHandler::command_flip() {
    m_SideToMove = Chess::InvertColor(m_SideToMove);
    m_Board.FlipSideToMove();
}

void UciHandler::command_debug() {
    m_Board.PrintBoard(m_SideToMove);
}

// --------- Methods ---------

UciHandler::UciHandler() {
    m_SideToMove = m_Board.LoadFromFen(Chess::DefaultFEN);
}

UciHandler::~UciHandler() {
    stopCurrentSearch();
}

void UciHandler::HandleCommand(const std::string& command) {
    if (command == "stop") {
        command_stop();
    }

    else if (command == "uci") {
        command_uci();
    }

    else if (command == "isready") {
        command_isready();
    }

    else if (command == "ucinewgame") {
        command_ucinewgame();
    }

    else if (Utils::StartsWith(command, "position ")) {
        command_position(command);
    }

    else if (Utils::StartsWith(command, "go ")) {
        command_go(command);
    }

    else if (Utils::StartsWith(command, "setoption ")) {
        command_setoption(command);
    }

    else if (command == "flip") {
        command_flip();
    }

    else if (command == "d") {
        command_debug();
    }

    else {
        std::cerr << "Invalid UCI: un-known command received: \"" << command << "\"" << std::endl;
    }
}

void UciHandler::stopCurrentSearch() {
    m_Board.CancelSearch();
    if (m_PonderThread.joinable()) m_PonderThread.join();
    if (m_SearchThread.joinable()) m_SearchThread.join();
}

Chess::Move UciHandler::uciToMove(const std::string& uci) const {
    if (uci.size() < 4) return Chess::Move();

    const int from = Chess::StringNotationToIndex(uci[0], uci[1]);
    const int to = Chess::StringNotationToIndex(uci[2], uci[3]);

    const bool isPromotion = uci.size() > 4;
    const Chess::MoveFlag promoFlag = isPromotion ? Utils::CharToPromo(uci[4]) : Chess::MoveFlag::None;

    for (Chess::Move move : m_Board.GetLegalMoves(from)) {
        if (move.TargetSquare != to) continue;

        if (Chess::isPromotion(move.Flag)) {
            if (isPromotion && Chess::HasFlag(move.Flag, promoFlag)) return move;
        } else {
            if (!isPromotion) return move;
        }
    }

    return Chess::Move();
}

void UciHandler::doUciMove(const std::string& uci) {
    if (const Chess::Move move = uciToMove(uci)) {
        m_Board.DoMove(move);
        m_SideToMove = Chess::InvertColor(m_SideToMove);
    }
}

UciHandler::GoParams UciHandler::parseGoParams(const std::string& command) const {
    const std::vector<std::string> tokens = Utils::Tokenize(command);

    // start after the "go" token
    unsigned int currentToken = 1u;

    const auto ConsumeToken = [&]() -> const std::string& { return tokens[currentToken++]; };

    GoParams params;

    while (currentToken < tokens.size()) {
        const std::string& token = ConsumeToken();

        if (token == "wtime") params.Wtime = std::stoi(ConsumeToken());
        else if (token == "btime") params.Btime = std::stoi(ConsumeToken());
        else if (token == "winc") params.Winc = std::stoi(ConsumeToken());
        else if (token == "binc") params.Binc = std::stoi(ConsumeToken());
        else if (token == "movestogo")params.Movestogo = std::stoi(ConsumeToken());
        else if (token == "movetime") params.Movetime = std::stoi(ConsumeToken());
        else if (token == "depth") params.Depth = std::stoi(ConsumeToken());
        else if (token == "infinite") params.Infinite = true;
        else if (token == "perft") {
            params.Depth = std::stoi(ConsumeToken()); params.Perft = true;
        }
    }

    return params;
}

int UciHandler::computeThinkTime(const GoParams& params) const {
    if (params.Movetime > 0) {
        return params.Movetime;
    }

    const bool isBlack = m_SideToMove == Chess::PieceColor::Black;

    const int remaining = isBlack ? params.Btime : params.Wtime;
    if (remaining <= 0) return 100;

    const int increment = isBlack ? params.Binc : params.Winc;

    return std::clamp<int>(
        remaining / 75 + static_cast<int>(increment * 0.6) - Options::MoveOverhead,
        Options::MinimumThinkingTime,
        remaining / 20
    );
}
