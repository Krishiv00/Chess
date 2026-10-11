# requires python-chess and rich

import math
import os
import queue
import random
import shlex
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from typing import Optional, Literal

import chess
import chess.engine
from rich.align import Align
from rich.console import Console, Group
from rich.live import Live
from rich.panel import Panel
from rich.table import Table
from rich.text import Text


ENGINE_A_PATH = "new"
ENGINE_A_NAME = "Engine A"

ENGINE_B_PATH = "old"
ENGINE_B_NAME = "Engine B"

GAME_PAIRS = 100 # total games played = GAME_PAIRS * 2

CONCURRENCY = None

MOVETIME_MS = 50 # ms
DEPTH = None

RANDOM_OPENING_PLIES = 4
MAX_MOVES = 200
SHOW_BOARD = True # render board in TUI
CONFIDENCE = 0.95

UNICODE_PIECES = {
    "P": "♙", "N": "♘", "B": "♗", "R": "♖", "Q": "♕", "K": "♔",
    "p": "♟", "n": "♞", "b": "♝", "r": "♜", "q": "♛", "k": "♚",
}

Z_TABLE = {0.90: 1.6449, 0.95: 1.9600, 0.99: 2.5758}


@dataclass
class EloEstimate:
    n: int
    wins: int
    losses: int
    draws: int
    score_pct: float
    elo_diff: float
    elo_margin: float
    los: float  # likelihood engine A is stronger, 0..1


def _elo_from_pct(p: float) -> float:
    p = min(max(p, 1e-6), 1 - 1e-6)
    return 400.0 * math.log10(p / (1.0 - p))


def compute_elo(wins: int, losses: int, draws: int, confidence: float) -> EloEstimate:
    n = wins + losses + draws
    if n == 0:
        return EloEstimate(0, 0, 0, 0, 0.5, 0.0, 0.0, 0.5)

    p = (wins + 0.5 * draws) / n
    e_x2 = (wins * 1.0 + draws * 0.25) / n
    var = max(e_x2 - p * p, 0.0)
    se = math.sqrt(var / n)

    z = Z_TABLE.get(confidence, 1.96)
    p_lo = min(max(p - z * se, 1e-6), 1 - 1e-6)
    p_hi = min(max(p + z * se, 1e-6), 1 - 1e-6)

    elo = _elo_from_pct(p)
    margin = max(_elo_from_pct(p_hi) - elo, elo - _elo_from_pct(p_lo))

    los = 0.5 * (1.0 + math.erf((p - 0.5) / (se * math.sqrt(2)))) if se > 0 else (1.0 if p > 0.5 else 0.0)

    return EloEstimate(n, wins, losses, draws, p, elo, margin, los)


@dataclass
class LiveState:
    worker_id: int
    game_index: int
    white_name: str
    black_name: str
    board: chess.Board = field(default_factory=chess.Board)
    last_move_san: str = ""
    ply: int = 0
    status: Literal["starting", "playing", "finished"] = "starting"
    updated_at: float = field(default_factory=time.time)


@dataclass
class GameOutcome:
    result: Literal["1-0", "0-1", "1/2-1/"]
    white: str
    black: str


def make_limit() -> chess.engine.Limit:
    if MOVETIME_MS is not None:
        return chess.engine.Limit(time=MOVETIME_MS / 1000.0)
    return chess.engine.Limit(depth=DEPTH or 12)


def start_engine(path: str) -> chess.engine.SimpleEngine:
    cmd = shlex.split(path) if " " in path else path
    return chess.engine.SimpleEngine.popen_uci(cmd)


def random_opening(plies: int, rng: random.Random) -> chess.Board:
    board = chess.Board()
    for _ in range(plies):
        if board.is_game_over():
            break
        legal = list(board.legal_moves)
        board.push(rng.choice(legal))
    return board


def play_one_game(
    engine_white, engine_black, name_white, name_black,
    start_board, game_index, worker_id, limit, on_update
) -> GameOutcome:
    board = start_board.copy()
    state = LiveState(worker_id, game_index, name_white, name_black, board.copy())
    state.status = "playing"
    on_update(state)

    while not board.is_game_over(claim_draw=True):
        if board.fullmove_number > MAX_MOVES:
            state.status = "finished"
            on_update(state)
            return GameOutcome("1/2-1/2", name_white, name_black)

        engine = engine_white if board.turn == chess.WHITE else engine_black
        try:
            result = engine.play(board, limit)
        except chess.engine.EngineTerminatedError:
            state.status = "finished"
            on_update(state)
            return GameOutcome("1/2-1/2", name_white, name_black)

        move = result.move
        if move is None:
            break

        san = board.san(move)
        board.push(move)

        state.board = board.copy()
        state.last_move_san = san
        state.ply = board.ply()
        on_update(state)

    outcome = board.outcome(claim_draw=True)
    result_str = outcome.result() if outcome else "1/2-1/2"

    state.status = "finished"
    on_update(state)
    return GameOutcome(result_str, name_white, name_black)


class MatchStats:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.wins_a = 0
        self.losses_a = 0
        self.draws = 0
        self.completed = 0

    def record(self, outcome: GameOutcome, name_a: str) -> None:
        with self.lock:
            self.completed += 1
            if outcome.result == "1/2-1/2":
                self.draws += 1
            else:
                a_is_white = outcome.white == name_a
                a_won = (outcome.result == "1-0" and a_is_white) or (outcome.result == "0-1" and not a_is_white)
                if a_won:
                    self.wins_a += 1
                else:
                    self.losses_a += 1

    def snapshot(self) -> EloEstimate:
        with self.lock:
            return compute_elo(self.wins_a, self.losses_a, self.draws, CONFIDENCE)


def render_board(board: chess.Board, white_name: str, black_name: str) -> Text:
    t = Text()
    t.append(f"{black_name}\n", style="bold red")
    for rank in range(7, -1, -1):
        t.append(f"{rank + 1} ", style="dim")
        for file in range(8):
            piece = board.piece_at(chess.square(file, rank))
            light = (file + rank) % 2 == 1
            bg = "on grey35" if light else "on grey15"
            ch = UNICODE_PIECES.get(piece.symbol(), " ") if piece else " "
            style = (("bold white " if piece.color else "bold black ") + bg) if piece else bg
            t.append(f" {ch} ", style=style)
        t.append("\n")
    t.append("   a  b  c  d  e  f  g  h\n", style="dim")
    t.append(f"{white_name}", style="bold cyan")
    return t


def elo_panel(est: EloEstimate, name_a: str, name_b: str) -> Panel:
    if est.n == 0:
        return Panel(
            Align.center(Text("waiting for first result...", style="dim")),
            title="Live Elo Estimate", border_style="yellow"
        )

    color = "green" if est.elo_diff > 0 else ("red" if est.elo_diff < 0 else "yellow")
    leader = name_a if est.elo_diff >= 0 else name_b
    los = est.los if est.elo_diff >= 0 else (1 - est.los)

    ci_lo, ci_hi = est.elo_diff - est.elo_margin, est.elo_diff + est.elo_margin
    inconclusive = ci_lo < 0 < ci_hi

    big = Text(f"{est.elo_diff:+.1f}", style=f"bold {color}", justify="center")
    big.append(f"  ± {est.elo_margin:.1f}", style="dim")

    sub = Text(justify="center")
    sub.append(f"\n{name_a} vs {name_b}: ", style="white")
    sub.append(f"{est.elo_diff:+.1f} Elo\n", style=color)
    sub.append(f"{CONFIDENCE*100:.0f}% CI: [{ci_lo:+.1f}, {ci_hi:+.1f}]\n", style="dim")
    if inconclusive:
        sub.append("not yet statistically distinguishable (CI includes 0) - play more games\n", style="bold yellow")
    else:
        sub.append(f"{leader} is stronger - confidence ", style="white")
        sub.append(f"{los*100:.1f}%\n", style="bold " + ("green" if los > 0.95 else "yellow"))

    score = Text(justify="center")
    score.append(f"\n{name_a}: ", style="bold")
    score.append(f"{est.wins}W ", style="green")
    score.append(f"{est.draws}D ", style="yellow")
    score.append(f"{est.losses}L", style="red")
    score.append(f"  ({est.score_pct*100:.1f}% score, {est.n} games)", style="dim")

    return Panel(
        Group(Align.center(big), Align.center(sub), Align.center(score)),
        title="[bold]Live Elo Estimate[/bold]", border_style=color, padding=(1, 2)
    )


def progress_panel(completed: int, total: int, elapsed: float) -> Panel:
    pct = completed / total if total else 0
    width = 40
    filled = int(width * pct)
    bar = Text()
    bar.append("█" * filled, style="cyan")
    bar.append("░" * (width - filled), style="dim")
    rate = completed / elapsed if elapsed > 0 else 0
    eta = (total - completed) / rate if rate > 0 else 0

    txt = Text()
    txt.append(f"{completed}/{total} games  ")
    txt.append(bar)
    txt.append(f"  {pct*100:.0f}%\n", style="bold")
    txt.append(f"rate: {rate*60:.1f} games/min   elapsed: {elapsed:.0f}s   eta: {eta:.0f}s", style="dim")
    return Panel(txt, title="Progress", border_style="blue")


def workers_table(states: dict[int, LiveState]) -> Panel:
    table = Table(expand=True, box=None)
    table.add_column("Worker", style="dim", width=7)
    table.add_column("Game#", width=6)
    table.add_column("White", width=14)
    table.add_column("Black", width=14)
    table.add_column("Ply", width=5, justify="right")
    table.add_column("Last move", width=10)
    table.add_column("Status", width=9)

    for wid in sorted(states.keys()):
        st = states[wid]
        status_style = "green" if st.status == "playing" else "dim"
        table.add_row(
            f"#{wid}", str(st.game_index), st.white_name, st.black_name,
            str(st.ply), st.last_move_san or "-",
            Text(st.status, style=status_style)
        )

    return Panel(table, title="Active Games", border_style="magenta")


class Dashboard:
    def __init__(self, stats: MatchStats, total_games: int) -> None:
        self.stats = stats
        self.total = total_games
        self.states: dict[int, LiveState] = {}
        self.worker_done: set[int] = set()
        self.lock = threading.Lock()
        self.start = time.time()
        self.live: Optional[Live] = None
        self.console = Console()
        self.focus_worker_id: int = 0

    def on_update(self, state: LiveState) -> None:
        state.updated_at = time.time()
        with self.lock:
            self.states[state.worker_id] = state

    def on_worker_done(self, worker_id: int) -> None:
        with self.lock:
            self.worker_done.add(worker_id)

    def render(self) -> Group:
        with self.lock:
            states = dict(self.states)
            done = set(self.worker_done)

            if self.focus_worker_id in done:
                candidates = sorted(wid for wid in states.keys() if wid not in done)
                if candidates:
                    self.focus_worker_id = candidates[0]
            focus_id = self.focus_worker_id

        est = self.stats.snapshot()
        elapsed = time.time() - self.start

        header = Panel(
            Align.center(Text(f"{ENGINE_A_NAME}  vs  {ENGINE_B_NAME}", style="bold white")), border_style="white"
        )

        parts = [header, elo_panel(est, ENGINE_A_NAME, ENGINE_B_NAME), progress_panel(self.stats.completed, self.total, elapsed)]

        if SHOW_BOARD and states:
            focus = states.get(focus_id) or next(iter(states.values()))
            parts.append(Panel(
                Align.center(render_board(focus.board, focus.white_name, focus.black_name), width=30),
                title=f"Board (worker #{focus.worker_id}, game #{focus.game_index})",
                border_style="cyan",
            ))

        parts.append(workers_table(states))
        return Group(*parts)

    def __enter__(self):
        self.live = Live(self.render(), console=self.console, refresh_per_second=6)
        self.live.__enter__()
        return self

    def __exit__(self, *exc) -> None:
        self.refresh()
        return self.live.__exit__(*exc)

    def refresh(self) -> None:
        if self.live:
            self.live.update(self.render())


def run_worker(worker_id: int, work_q: "queue.Queue", stats: MatchStats, dashboard: Dashboard):
    engine_a = start_engine(ENGINE_A_PATH)
    engine_b = start_engine(ENGINE_B_PATH)
    limit = make_limit()
    try:
        while True:
            try:
                pair_index, start_board = work_q.get_nowait()
            except queue.Empty:
                break

            outcome1 = play_one_game(engine_a, engine_b, ENGINE_A_NAME, ENGINE_B_NAME, start_board, pair_index * 2, worker_id, limit, dashboard.on_update)
            stats.record(outcome1, ENGINE_A_NAME)

            outcome2 = play_one_game(engine_b, engine_a, ENGINE_B_NAME, ENGINE_A_NAME, start_board, pair_index * 2 + 1, worker_id, limit, dashboard.on_update)
            stats.record(outcome2, ENGINE_A_NAME)
    finally:
        dashboard.on_worker_done(worker_id)
        for eng in (engine_a, engine_b):
            try:
                eng.quit()
            except Exception:
                try:
                    eng.close()
                except Exception:
                    pass


def main() -> None:
    concurrency = CONCURRENCY or os.cpu_count() or 1

    stats = MatchStats()
    total_games = GAME_PAIRS * 2
    dashboard = Dashboard(stats, total_games)

    rng = random.Random()
    work_q: "queue.Queue" = queue.Queue()
    for pair_index in range(GAME_PAIRS):
        start_board = random_opening(RANDOM_OPENING_PLIES, rng)
        work_q.put((pair_index, start_board))

    workers = max(1, min(concurrency, GAME_PAIRS))

    stop_refresh = threading.Event()

    def refresh_loop() -> None:
        while not stop_refresh.is_set():
            dashboard.refresh()
            time.sleep(1 / 6)

    with dashboard:
        rt = threading.Thread(target=refresh_loop, daemon=True)
        rt.start()
        try:
            with ThreadPoolExecutor(max_workers=workers) as pool:
                futures = [pool.submit(run_worker, wid, work_q, stats, dashboard) for wid in range(workers)]
                for f in as_completed(futures):
                    f.result()
        except KeyboardInterrupt:
            dashboard.console.print("\n[yellow]Interrupted.[/yellow]")
        finally:
            stop_refresh.set()
            rt.join(timeout=1)
            dashboard.refresh()

    est = stats.snapshot()
    console = dashboard.console
    console.print()
    if est.n > 0:
        ci_lo, ci_hi = est.elo_diff - est.elo_margin, est.elo_diff + est.elo_margin
        console.print(f"[bold]Result:[/bold] {ENGINE_A_NAME} {est.elo_diff:+.1f} ± {est.elo_margin:.1f} Elo "
                       f"vs {ENGINE_B_NAME}  ({est.wins}W/{est.draws}D/{est.losses}L over {est.n} games)")
        if ci_lo < 0 < ci_hi:
            console.print(
                "[bold yellow]Not enough games yet to say which is stronger "
                f"(95% CI [{ci_lo:+.1f}, {ci_hi:+.1f}] includes 0). Run more games.[/bold yellow]"
            )
        else:
            winner = ENGINE_A_NAME if est.elo_diff > 0 else ENGINE_B_NAME
            los = est.los if est.elo_diff >= 0 else (1 - est.los)
            console.print(f"[bold]{winner}[/bold] appears stronger (confidence: {los*100:.1f}%)")
    else:
        console.print("[yellow]No games completed.[/yellow]")


if __name__ == "__main__":
    main()
