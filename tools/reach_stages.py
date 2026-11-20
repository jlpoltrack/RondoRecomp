"""Reach Rondo stages headlessly by poking the stage step ($81) and forcing an area change ($37).

usage: python3 tools/reach_stages.py CUE areas STEP...         a state per step, 60 frames into play
       python3 tools/reach_stages.py CUE build STATE:NOTE...   manifest states of each room reached
States are read from and written to work/states (s20000.state: frame 20000 of the capture run).
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CUE = None   # the game's .cue, the first argument
BIN = os.path.join(ROOT, "build/rondo_headless")
TMP = os.path.join(ROOT, "work/states/tmp")


def run(args, trace=True):
    """Run the headless binary; returns (stdout, list of per-frame WRAM snapshots, start frame)."""
    os.makedirs(TMP, exist_ok=True)
    tpath = os.path.join(TMP, "trace.bin")
    cmd = [BIN, CUE] + args + (["--ram-trace", tpath] if trace else [])
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    frames = []
    if trace:
        d = open(tpath, "rb").read()
        frames = [d[i:i + 0x2001] for i in range(0, len(d), 0x2001)]
    start = 0
    for line in out.splitlines():
        if line.startswith("state loaded: frame"):
            start = int(line.split()[-1])
    return out, frames, start


def first_play(frames, start, after=0):
    """First frame (absolute) at or after `after` with $30=05 and $31=01."""
    for i, f in enumerate(frames):
        if start + i + 1 >= after and f[0x30] == 5 and f[0x31] == 1:
            return start + i + 1
    return None


def cam(f):
    return f[0x1E96] * 256 + f[0x1E08]


def verify(state, hold="right", n=300):
    """Load a state, hold a direction; returns (in play all along, camera X moved, area, room)."""
    _, fr, st = run(["--load-state", state, "--frames", str(n), "--cheats", "health,hearts,lives",
                     "--every", f"0:99999999:1:{hold}"])
    ok = fr[0][0x30] == 5 and fr[0][0x31] == 1
    xs = [cam(f) for f in fr]
    return ok, max(xs) - min(xs), fr[0][0xA2], fr[0][0x83], [f[0x83] for f in fr][-1]


def make_area(step, settle=60):
    """From s20000, jump to the table entry `step` and save a state `settle` frames into play."""
    base = os.path.join(ROOT, "work/states/s20000.state")
    args = ["--load-state", base, "--cheats", "health,hearts,lives",
            "--poke", "37=1@20000:20000", "--poke", f"81={step:X}@20000:20012"]
    _, fr, st = run(args + ["--frames", "800"])
    p = first_play(fr, st, 20020)
    if p is None:
        return None
    at = p + settle
    tmp = os.path.join(TMP, f"area_{step:02X}.state")
    _, fr2, _ = run(args + ["--frames", str(at - st), "--save-state", f"{at}:{tmp}"])
    return tmp, at, fr2[-1][0xA2], fr2[-1][0x83]


PATTERNS = {
    "R": ["0:99999999:1:right"],
    "RJ": ["0:99999999:1:right", "0:99999999:60:i:20"],
    "L": ["0:99999999:1:left"],
    "LJ": ["0:99999999:1:left", "0:99999999:60:i:20"],
}


def camxy(f):
    return cam(f), f[0x1E20] * 256 + f[0x1E18]


def explore(state, pat, n=3000):
    """Play a pattern from a state; return [(area, room, first play frame, camX range, camY range, frames)]."""
    ev = []
    for e in PATTERNS[pat]:
        ev += ["--every", e]
    _, fr, st = run(["--load-state", state, "--frames", str(n), "--cheats", "health,hearts,lives"] + ev)
    rooms = {}
    for i, f in enumerate(fr):
        if f[0x30] != 5 or f[0x31] != 1:
            continue
        k = (f[0xA2], f[0x83])
        x, y = camxy(f)
        r = rooms.setdefault(k, [st + i + 1, x, x, y, y, 0])
        r[1], r[2], r[3], r[4], r[5] = min(r[1], x), max(r[2], x), min(r[3], y), max(r[4], y), r[5] + 1
    return st, rooms


STAGE = {0x10: "1", 0x0E: "2", 0x12: "2b", 0x02: "3", 0x0C: "3b", 0x06: "4", 0x16: "4b",
         0x1A: "5", 0x30: "5b", 0x22: "7", 0x1C: "7", 0x34: "8"}
PAT_TEXT = {"R": "hold right", "RJ": "hold right, I (jump) 20 frames every 60",
            "L": "hold left", "LJ": "hold left, I (jump) 20 frames every 60"}


def build(starts, n=1500, out_dir=os.path.join(ROOT, "work/states")):
    """starts: [(state path, note)]. Saves st<stage>_a<area>_r<room>_<frame>.state; returns manifest rows."""
    rows, seen = [], set()
    for path, note in starts:
        res = {p: explore(path, p, n) for p in ("R", "RJ", "L", "LJ")}
        st = res["R"][0]
        cand = {}
        for p, (_, rooms) in res.items():
            for k, v in rooms.items():
                if v[2] - v[1] >= 64 and (k not in cand or v[2] - v[1] > cand[k][1][2] - cand[k][1][1]):
                    cand[k] = (p, v)
        for (a, r), (p, v) in sorted(cand.items(), key=lambda kv: kv[1][1][0]):
            if (a, r) in seen:
                continue
            seen.add((a, r))
            name = f"st{STAGE.get(a, 'X')}_a{a:02X}_r{r:02X}_{{}}.state"
            if v[0] <= st + 1:
                frame = st
                dst = os.path.join(out_dir, name.format(frame))
                shutil.copy(path, dst)
                how = PAT_TEXT[p]
            else:  # reached later: replay the pattern and save 20 frames into the room
                frame = v[0] + 20
                dst = os.path.join(out_dir, name.format(frame))
                ev = sum((["--every", e] for e in PATTERNS[p]), [])
                run(["--load-state", path, "--frames", str(frame - st), "--cheats", "health,hearts,lives",
                     "--save-state", f"{frame}:{dst}"] + ev, trace=False)
                how = PAT_TEXT[p]
            ok, dx, *_ = verify(dst, "left" if p.startswith("L") else "right")
            rows.append((os.path.basename(dst), STAGE.get(a, "?"), f"{a:02X}", f"{r:02X}", note, how, ok, dx))
            print(rows[-1], flush=True)
    return rows


def main(argv):
    global CUE
    if len(argv) < 3:
        print(__doc__)
        return 2
    CUE, cmd, args = argv[1], argv[2], argv[3:]
    if cmd == "areas":
        for a in args:
            r = make_area(int(a, 16))
            if not r:
                print(a, "no play")
                continue
            v = verify(r[0])
            print(f"step {a}: frame {r[1]} area {r[2]:02X} room {r[3]:02X} play {v[0]} camdX {v[1]} "
                  f"room after {v[4]:02X}")
    elif cmd == "build" and args:
        for row in build([tuple(a.split(":", 1)) if ":" in a else (a, "") for a in args]):
            print(" | ".join(str(x) for x in row))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
