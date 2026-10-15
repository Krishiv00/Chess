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

ENGINE_OPTIONS = {
    "Hash": "32",
    "Threads": "1",
    "OwnBook": "true",
}

MAX_MOVES = 200
SHOW_BOARD = True # render board in TUI
CONFIDENCE = 0.95

SEARCH_MIN_NODES = 1000
SEARCH_MIN_DEPTH = 4
BOOTSTRAP_ITERS = 2000

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
    los: float # likelihood engine A is stronger, 0..1


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
class MoveSearchSample:
    depth: int
    nodes: int
    nps: float
    ebf: float # effective branching factor: nodes ** (1/depth)


def sample_from_info(info: dict) -> Optional["MoveSearchSample"]:
    depth = info.get("depth")
    nodes = info.get("nodes")
    if not depth or not nodes or depth < SEARCH_MIN_DEPTH or nodes < SEARCH_MIN_NODES:
        return None

    time_s = info.get("time") or 0
    nps = info.get("nps") or (nodes / time_s if time_s > 0 else 0)
    ebf = nodes ** (1.0 / depth)
    return MoveSearchSample(depth=depth, nodes=nodes, nps=nps, ebf=ebf)


@dataclass
class SearchEfficiencyEstimate:
    n_pairs: int
    depth_a: float
    depth_b: float
    depth_diff: float
    depth_margin: float
    depth_conf: float
    ebf_a: float
    ebf_b: float
    ebf_pct_diff: float # negative = A prunes more (fewer nodes/ply) than B
    ebf_pct_margin: float
    ebf_conf: float
    nps_a: float
    nps_b: float
    nps_pct_diff: float


def _bootstrap_mean_ci(diffs: list, confidence: float) -> tuple:
    if not diffs:
        return 0.0, 0.0, 0.5
    mean = sum(diffs) / len(diffs)
    if len(diffs) < 2:
        return mean, 0.0, (1.0 if mean > 0 else 0.0)

    means = []
    n = len(diffs)
    rng = random.Random(1234)
    for _ in range(BOOTSTRAP_ITERS):
        resample = [diffs[rng.randrange(n)] for _ in range(n)]
        means.append(sum(resample) / n)
    means.sort()

    alpha = 1 - confidence
    lo = means[int((alpha / 2) * BOOTSTRAP_ITERS)]
    hi = means[int((1 - alpha / 2) * BOOTSTRAP_ITERS) - 1]
    margin = max(hi - mean, mean - lo)
    prob_positive = sum(1 for m in means if m > 0) / len(means)
    return mean, margin, prob_positive


def compute_search_efficiency(pair_diffs: dict, confidence: float) -> SearchEfficiencyEstimate:
    n_pairs = len(pair_diffs["depth"])

    depth_diff, depth_margin, depth_conf = _bootstrap_mean_ci(pair_diffs["depth"], confidence)
    ebf_log_diff, ebf_log_margin, ebf_conf = _bootstrap_mean_ci(pair_diffs["ebf_log_ratio"], confidence)

    depth_a = sum(pair_diffs["depth_a"]) / n_pairs if n_pairs else 0.0
    depth_b = sum(pair_diffs["depth_b"]) / n_pairs if n_pairs else 0.0
    ebf_a = sum(pair_diffs["ebf_a"]) / n_pairs if n_pairs else 0.0
    ebf_b = sum(pair_diffs["ebf_b"]) / n_pairs if n_pairs else 0.0
    nps_a = sum(pair_diffs["nps_a"]) / n_pairs if n_pairs else 0.0
    nps_b = sum(pair_diffs["nps_b"]) / n_pairs if n_pairs else 0.0

    # log-ratio -> percent difference
    ebf_pct_diff = (math.exp(ebf_log_diff) - 1) * 100
    ebf_pct_margin = (math.exp(ebf_log_diff + ebf_log_margin) - math.exp(ebf_log_diff)) * 100
    nps_pct_diff = ((nps_a - nps_b) / nps_b * 100) if nps_b else 0.0

    return SearchEfficiencyEstimate(
        n_pairs=n_pairs,
        depth_a=depth_a, depth_b=depth_b,
        depth_diff=depth_diff, depth_margin=depth_margin, depth_conf=depth_conf,
        ebf_a=ebf_a, ebf_b=ebf_b,
        ebf_pct_diff=ebf_pct_diff, ebf_pct_margin=ebf_pct_margin, ebf_conf=ebf_conf,
        nps_a=nps_a, nps_b=nps_b, nps_pct_diff=nps_pct_diff,
    )


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
    engine = chess.engine.SimpleEngine.popen_uci(cmd)

    for name, value in ENGINE_OPTIONS.items():
        if name in engine.options:
            engine.configure({name: value})

    return engine


def play_one_game(
    engine_white, engine_black, name_white, name_black,
    start_board, game_index, worker_id, limit, on_update,
    pair_index=None, search_stats=None,
) -> GameOutcome:
    board = start_board.copy()
    state = LiveState(worker_id, game_index, name_white, name_black, board.copy())
    state.status = "playing"
    on_update(state)

    samples_white: list = []
    samples_black: list = []

    def finish(result_str: str) -> GameOutcome:
        state.status = "finished"
        on_update(state)
        if search_stats is not None and pair_index is not None:
            search_stats.record(pair_index, name_white, samples_white)
            search_stats.record(pair_index, name_black, samples_black)
        return GameOutcome(result_str, name_white, name_black)

    while not board.is_game_over(claim_draw=True):
        if board.fullmove_number > MAX_MOVES:
            return finish("1/2-1/2")

        white_to_move = board.turn == chess.WHITE
        engine = engine_white if white_to_move else engine_black
        try:
            result = engine.play(board, limit, info=chess.engine.INFO_ALL)
        except chess.engine.EngineTerminatedError:
            return finish("1/2-1/2")

        move = result.move
        if move is None:
            break

        sample = sample_from_info(result.info)
        if sample is not None:
            (samples_white if white_to_move else samples_black).append(sample)

        san = board.san(move)
        board.push(move)

        state.board = board.copy()
        state.last_move_san = san
        state.ply = board.ply()
        on_update(state)

    outcome = board.outcome(claim_draw=True)
    result_str = outcome.result() if outcome else "1/2-1/2"
    return finish(result_str)


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


class SearchStats:
    def __init__(self, name_a: str, name_b: str) -> None:
        self.name_a = name_a
        self.name_b = name_b
        self.lock = threading.Lock()
        self.pending: dict = {} # pair_index -> {engine_name: [samples]}
        self.pair_diffs: dict = {
            "depth": [], "ebf_log_ratio": [],
            "depth_a": [], "depth_b": [], "ebf_a": [], "ebf_b": [],
            "nps_a": [], "nps_b": [],
        }

    def record(self, pair_index: int, engine_name: str, samples: list) -> None:
        with self.lock:
            slot = self.pending.setdefault(pair_index, {})
            slot[engine_name] = slot.get(engine_name, []) + samples

            if self.name_a in slot and self.name_b in slot:
                a_samples, b_samples = slot.pop(self.name_a), slot.pop(self.name_b)
                self.pending.pop(pair_index, None)
                self._add_pair(a_samples, b_samples)

    def _add_pair(self, a_samples: list, b_samples: list) -> None:
        if not a_samples or not b_samples:
            return # one side had nothing usable (all book/instant moves) - skip the pair

        depth_a = sum(s.depth for s in a_samples) / len(a_samples)
        depth_b = sum(s.depth for s in b_samples) / len(b_samples)
        ebf_a = math.exp(sum(math.log(s.ebf) for s in a_samples) / len(a_samples))
        ebf_b = math.exp(sum(math.log(s.ebf) for s in b_samples) / len(b_samples))
        nps_a = sum(s.nps for s in a_samples) / len(a_samples)
        nps_b = sum(s.nps for s in b_samples) / len(b_samples)

        self.pair_diffs["depth"].append(depth_a - depth_b)
        self.pair_diffs["ebf_log_ratio"].append(math.log(ebf_a) - math.log(ebf_b))
        self.pair_diffs["depth_a"].append(depth_a)
        self.pair_diffs["depth_b"].append(depth_b)
        self.pair_diffs["ebf_a"].append(ebf_a)
        self.pair_diffs["ebf_b"].append(ebf_b)
        self.pair_diffs["nps_a"].append(nps_a)
        self.pair_diffs["nps_b"].append(nps_b)

    def snapshot(self) -> SearchEfficiencyEstimate:
        with self.lock:
            diffs = {k: list(v) for k, v in self.pair_diffs.items()}
        return compute_search_efficiency(diffs, CONFIDENCE)


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


def search_panel(est: SearchEfficiencyEstimate, name_a: str, name_b: str) -> Panel:
    if est.n_pairs == 0:
        return Panel(
            Align.center(Text("waiting for first result...", style="dim")),
            title="Search Efficiency", border_style="yellow"
        )

    depth_color = "green" if est.depth_diff > 0 else ("red" if est.depth_diff < 0 else "white")
    # negative ebf_pct_diff = A needs fewer nodes/ply than B = A prunes harder
    ebf_color = "green" if est.ebf_pct_diff < 0 else ("red" if est.ebf_pct_diff > 0 else "white")
    nps_color = "green" if est.nps_pct_diff > 0 else ("red" if est.nps_pct_diff < 0 else "white")

    table = Table(expand=True, box=None, show_header=False)
    table.add_column("metric", style="bold", width=8)
    table.add_column("values")
    table.add_column("diff", justify="right")

    table.add_row(
        "Depth",
        f"{name_a} {est.depth_a:.1f}  vs  {name_b} {est.depth_b:.1f}",
        Text(f"{est.depth_diff:+.2f} ±{est.depth_margin:.2f} ({est.depth_conf*100:.0f}%)", style=depth_color),
    )
    table.add_row(
        "EBF",
        f"{name_a} {est.ebf_a:.2f}  vs  {name_b} {est.ebf_b:.2f}",
        Text(f"{est.ebf_pct_diff:+.1f}% ±{est.ebf_pct_margin:.1f}% ({est.ebf_conf*100:.0f}%)", style=ebf_color),
    )
    table.add_row(
        "NPS",
        f"{name_a} {est.nps_a:,.0f}  vs  {name_b} {est.nps_b:,.0f}",
        Text(f"{est.nps_pct_diff:+.1f}%", style=nps_color),
    )

    return Panel(table, title=f"Search Efficiency ({est.n_pairs} pairs)", border_style="cyan")


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
    def __init__(self, stats: MatchStats, search_stats: "SearchStats", total_games: int) -> None:
        self.stats = stats
        self.search_stats = search_stats
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

        search_est = self.search_stats.snapshot()

        parts = [
            header,
            elo_panel(est, ENGINE_A_NAME, ENGINE_B_NAME),
            search_panel(search_est, ENGINE_A_NAME, ENGINE_B_NAME),
            progress_panel(self.stats.completed, self.total, elapsed),
        ]

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


def run_worker(worker_id: int, work_q: "queue.Queue", stats: MatchStats, search_stats: SearchStats, dashboard: Dashboard):
    engine_a = start_engine(ENGINE_A_PATH)
    engine_b = start_engine(ENGINE_B_PATH)
    limit = make_limit()
    try:
        while True:
            try:
                pair_index, start_board = work_q.get_nowait()
            except queue.Empty:
                break

            outcome1 = play_one_game(
                engine_a, engine_b, ENGINE_A_NAME, ENGINE_B_NAME, start_board, pair_index * 2, worker_id, limit,
                dashboard.on_update, pair_index=pair_index, search_stats=search_stats,
            )
            stats.record(outcome1, ENGINE_A_NAME)

            outcome2 = play_one_game(
                engine_b, engine_a, ENGINE_B_NAME, ENGINE_A_NAME, start_board, pair_index * 2 + 1, worker_id, limit,
                dashboard.on_update, pair_index=pair_index, search_stats=search_stats,
            )
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
    concurrency = CONCURRENCY or max(1, (os.cpu_count() or 0) - 2)

    stats = MatchStats()
    search_stats = SearchStats(ENGINE_A_NAME, ENGINE_B_NAME)
    total_games = GAME_PAIRS * 2
    dashboard = Dashboard(stats, search_stats, total_games)

    work_q: "queue.Queue" = queue.Queue()
    for pair_index in range(GAME_PAIRS):
        work_q.put((pair_index, chess.Board()))

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
                futures = [pool.submit(run_worker, wid, work_q, stats, search_stats, dashboard) for wid in range(workers)]
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

    search_est = search_stats.snapshot()
    if search_est.n_pairs > 0:
        console.print(
            f"[bold]Depth:[/bold] {search_est.depth_diff:+.2f} ±{search_est.depth_margin:.2f} "
            f"({search_est.depth_conf*100:.0f}%)   "
            f"[bold]EBF:[/bold] {ENGINE_A_NAME} {search_est.ebf_a:.2f} vs {ENGINE_B_NAME} {search_est.ebf_b:.2f} "
            f"({search_est.ebf_pct_diff:+.1f}% ±{search_est.ebf_pct_margin:.1f}%, {search_est.ebf_conf*100:.0f}%)   "
            f"[bold]NPS:[/bold] {search_est.nps_pct_diff:+.1f}%  "
            f"({search_est.n_pairs} pairs)"
        )
    else:
        console.print("[yellow]No search-efficiency samples collected.[/yellow]")


if __name__ == "__main__":
    main()
