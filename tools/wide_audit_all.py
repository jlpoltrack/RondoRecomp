"""Widescreen audit of every save state in a manifest (work/states/manifest.txt).

usage: python3 tools/wide_audit_all.py CUE [--manifest FILE] [--frames N] [--cols N] [--out DIR]
                                      [--route-every N]

--cols defaults to 46, the widest the game shows (wide.h, WIDE_MAX). Each state runs with its
manifest input and sides in every room (rondo_headless --wide N
--wide-any --wide-audit), all at once; the per-room tables are merged into OUT/audit.txt and
pictures every 150 frames go to OUT/<state>/. --route-every N: every N frames the game is
asked to leave the room ($38 = 3, as a room's edge or door asks), so a run goes on through
the stage's route, up to 7 rooms past the state's.
"""
import concurrent.futures
import os
import shlex
import subprocess
import sys


def route_pokes(state, every, frames):
    """--poke pairs leaving the room every `every` frames (the frame is the name's last number)"""
    if not every:
        return []
    f0 = int(os.path.splitext(state)[0].rsplit("_", 1)[1])
    pokes = []
    for k in range(1, min(8, (frames - 1) // every + 1)):   # every exit before the run's end
        f = f0 + k * every
        pokes += ["--poke", "8C=80@%d:%d" % (f, f), "--poke", "38=03@%d:%d" % (f, f)]
    return pokes


def run(cue, state, inputs, frames, cols, out):
    name = os.path.splitext(os.path.basename(state))[0]
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    audit = os.path.join(d, "audit.txt")
    cmd = ["build/rondo_headless", cue, "--load-state", state, "--frames", str(frames), "--shot-every", "150",
           "--out", d, "--wide", str(cols), "--wide-any", "--wide-audit", audit,
           "--cheats", "health,hearts,lives"] + inputs
    p = subprocess.run(cmd, capture_output=True, text=True)
    wide = [l for l in p.stdout.splitlines() if l.startswith("Widescreen:")]
    rows = open(audit).read().splitlines()[1:] if os.path.exists(audit) else []
    return name, rows, wide[0] if wide else "no run (exit %d)" % p.returncode


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cue, manifest, frames, cols, out, every = argv[1], "work/states/manifest.txt", 1500, 46, "work/wide_all", 0
    i = 2
    while i + 1 < len(argv):
        k, v = argv[i], argv[i + 1]
        if k == "--manifest": manifest = v
        elif k == "--frames": frames = int(v)
        elif k == "--cols": cols = int(v)
        elif k == "--out": out = v
        elif k == "--route-every": every = int(v)
        i += 2
    jobs = []
    for line in open(manifest):
        if line.startswith("#") or "|" not in line:
            continue
        f = [x.strip() for x in line.split("|")]
        inp = f[5].split(":", 1)[1] if ":" in f[5] else ""
        state = os.path.join(os.path.dirname(manifest), f[0])
        jobs.append((state, shlex.split(inp) + route_pokes(state, every, frames)))
    os.makedirs(out, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        res = list(ex.map(lambda j: run(cue, j[0], j[1], frames, cols, out), jobs))
    with open(os.path.join(out, "audit.txt"), "w") as f:
        f.write("%-24s area room  frames  wide  avg_l avg_r    cells mismatch unverified band_rows  popins vanishes\n"
                % "state")
        for name, rows, wide in res:
            for r in rows:
                f.write("%-24s%s\n" % (name, r))
            f.write("%-24s   %s\n" % ("", wide))
    print(open(os.path.join(out, "audit.txt")).read())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
