import argparse
import os
import pathlib
import re
import sqlite3
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from xiangqi_core import START_FEN, apply_uci, board_to_fen, fen_to_board, normalize_uci, opponent, wxf_to_uci


HEADER_RE = re.compile(r'^\[(\w+)\s+"(.*)"\]\s*$')


def split_games(path):
    headers = []
    moves = []
    in_game = False
    with open(path, "r", encoding="utf-8", errors="ignore") as file:
        for raw in file:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("[Game ") and in_game:
                yield headers, " ".join(moves)
                headers = []
                moves = []
            in_game = True
            if line.startswith("["):
                headers.append(line)
            else:
                moves.append(line)
    if in_game:
        yield headers, " ".join(moves)


def parse_headers(lines):
    out = {}
    for line in lines:
        match = HEADER_RE.match(line)
        if match:
            out[match.group(1)] = match.group(2)
    return out


def move_tokens(text):
    text = re.sub(r"\{[^}]*\}", " ", text, flags=re.S)
    text = re.sub(r";[^\n\r]*", " ", text)
    tokens = []
    for raw in re.split(r"[\s,]+", text):
        token = raw.strip()
        if not token:
            continue
        token = re.sub(r"^\d+\.(?:\.\.)?", "", token)
        token = token.strip()
        if not token or token in {"*", "1-0", "0-1", "1/2-1/2"}:
            continue
        token = token.rstrip("!?+#")
        if token:
            tokens.append(token)
    return tokens


def fen_to_key(fen):
    placement, side = fen.strip().split()[:2]
    rows = placement.count("/") + 1
    first_row = placement.split("/", 1)[0]
    cols = sum(int(ch) if ch.isdigit() else 1 for ch in first_row)
    size = rows * cols
    turn_is_red = side != "b"
    piece_codes = {
        "X": 0, "x": 0,
        "R": 1, "N": 2, "B": 3, "A": 4, "K": 5, "C": 6, "P": 7,
        "r": 9, "n": 10, "b": 11, "a": 12, "k": 13, "c": 14, "p": 15,
    }
    cells = [-1] * size
    index = 0
    for ch in placement:
        if ch == "/":
            continue
        if ch.isdigit():
            index += int(ch)
            continue
        lookup = ch if turn_is_red else ch.swapcase()
        cells[index] = piece_codes.get(lookup, -1)
        index += 1

    mirror_ud = False
    if not turn_is_red:
        for row in range(rows // 2):
            for col in range(cols):
                a = row * cols + col
                b = (rows - 1 - row) * cols + (cols - 1 - col)
                cells[a], cells[b] = cells[b], cells[a]
        mirror_ud = True

    mirror_lr = False
    done = False
    for row in range(rows):
        if done:
            break
        for col in range(cols // 2):
            a = row * cols + col
            b = row * cols + (cols - 1 - col)
            if cells[a] != cells[b]:
                mirror_lr = cells[b] > cells[a]
                done = True
                break
    if mirror_lr:
        for row in range(rows):
            for col in range(cols // 2):
                a = row * cols + col
                b = row * cols + (cols - 1 - col)
                cells[a], cells[b] = cells[b], cells[a]

    out = bytearray()
    buffer = 0
    bits = 0
    for idx, value in enumerate(cells):
        if value == -1:
            bits += 1
        else:
            buffer |= 1 << (32 - bits - 1)
            buffer |= value << (32 - bits - 1 - 4)
            bits += 5
        next_value = cells[idx + 1] if idx + 1 < len(cells) else None
        need_bits = 1 if next_value == -1 else 5
        if idx == len(cells) - 1 or 32 - bits < need_bits:
            threshold = 1 if idx == len(cells) - 1 else 8
            while bits >= threshold:
                out.append((buffer >> 24) & 0xFF)
                buffer = (buffer << 8) & 0xFFFFFFFF
                bits -= 8
    return bytes(out), mirror_ud, mirror_lr, rows, cols


def uci_to_xqbook_move(move):
    move = normalize_uci(move)
    src_col = ord(move[0]) - ord("a")
    src_row = 9 - int(move[1])
    dst_col = ord(move[2]) - ord("a")
    dst_row = 9 - int(move[3])
    return (src_row << 12) | (src_col << 8) | (dst_row << 4) | dst_col


def mirror_move(move, mirror_ud, mirror_lr, rows, cols):
    from_row = move >> 12
    from_col = (move >> 8) & 0xF
    to_row = (move >> 4) & 0xF
    to_col = move & 0xF
    if mirror_ud:
        from_row = rows - 1 - from_row
        to_row = rows - 1 - to_row
        from_col = cols - 1 - from_col
        to_col = cols - 1 - to_col
    if mirror_lr:
        from_col = cols - 1 - from_col
        to_col = cols - 1 - to_col
    return (from_row << 12) | (from_col << 8) | (to_row << 4) | to_col


def result_delta(result, side):
    if result == "1/2-1/2":
        return 0, 1, 0
    if result == "1-0":
        return (1, 0, 0) if side == "red" else (0, 0, 1)
    if result == "0-1":
        return (1, 0, 0) if side == "black" else (0, 0, 1)
    return 0, 0, 0


def import_game(conn, headers, moves_text, max_ply):
    headers = parse_headers(headers)
    result = headers.get("Result", "*")
    if result not in {"1-0", "0-1", "1/2-1/2"}:
        return 0, 0

    fen = headers.get("FEN", START_FEN)
    try:
        board, side = fen_to_board(fen)
    except Exception:
        board, side = fen_to_board(START_FEN)
        fen = board_to_fen(board, side)

    added = 0
    skipped = 0
    for ply, token in enumerate(move_tokens(moves_text), 1):
        if max_ply and ply > max_ply:
            break
        try:
            try:
                move = normalize_uci(token)
            except ValueError:
                move = wxf_to_uci(token, board, side)
            key, mirror_ud, mirror_lr, rows, cols = fen_to_key(fen)
            stored_move = mirror_move(uci_to_xqbook_move(move), mirror_ud, mirror_lr, rows, cols)
            win, draw, lost = result_delta(result, side)
            existing = conn.execute(
                "select id, win, draw, lost from book where key=? and move=?",
                (sqlite3.Binary(key), stored_move),
            ).fetchone()
            if existing:
                row_id, old_win, old_draw, old_lost = existing
                new_win = int(old_win or 0) + win
                new_draw = int(old_draw or 0) + draw
                new_lost = int(old_lost or 0) + lost
                conn.execute(
                    "update book set win=?, draw=?, lost=?, score=?, valid=1 where id=?",
                    (new_win, new_draw, new_lost, new_win - new_lost, row_id),
                )
            else:
                conn.execute(
                    "insert into book(key, move, score, win, draw, lost, valid, memo) values(?,?,?,?,?,?,1,NULL)",
                    (sqlite3.Binary(key), stored_move, win - lost, win, draw, lost),
                )
            added += 1
            board = apply_uci(board, move)
            side = opponent(side)
            fen = board_to_fen(board, side)
        except Exception:
            skipped += 1
            break
    return added, skipped


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--book", required=True)
    parser.add_argument("--max-ply", type=int, default=40)
    parser.add_argument("pgns", nargs="+")
    args = parser.parse_args()

    conn = sqlite3.connect(args.book)
    total_games = 0
    total_added = 0
    total_skipped = 0
    try:
        conn.execute("create index if not exists idx_book_key_move on book(key, move)")
        for path in args.pgns:
            for headers, moves_text in split_games(path):
                added, skipped = import_game(conn, headers, moves_text, args.max_ply)
                total_games += 1
                total_added += added
                total_skipped += skipped
                if total_games % 1000 == 0:
                    conn.commit()
                    print(f"games={total_games} positions={total_added} skipped={total_skipped}", flush=True)
        conn.commit()
    finally:
        conn.close()
    print(f"done games={total_games} positions={total_added} skipped={total_skipped}")


if __name__ == "__main__":
    main()
