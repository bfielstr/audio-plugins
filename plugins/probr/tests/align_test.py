#!/usr/bin/env python3
"""align_test.py <probr_align.py> <work dir> [<session the writer made>]

Tests scripts/probr_align.py on sessions generated here, in the files' format: a source probe (a
harmonic tone, a note per beat, with its MIDI) from ppq 0, a probe 6 dB louder armed a beat later whose
recording stops and starts again (two takes), and a noise probe. The analysis must line them up by song
position: the louder probe reads +6.02 dB on every bin, band and note it shares with the source, the
tone's notes are peaky and the noise's are not. Then, when given, the session probr's own writer made
(probr_tests --make-session): "2 half" reads -6.02 dB of mid against "1 source" and has no side.
Exits 77 (skipped) without numpy.
"""
import csv
import json
import math
import os
import shutil
import subprocess
import sys

try:
    import numpy as np
except ImportError:
    print("numpy is not installed: skipped")
    sys.exit(77)

SR = 48000
TEMPO = 120.0
NOTES = [45, 52, 57, 60, 64, 57, 52, 48]
fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        fails += 1


def write_wav(path, x):
    data = np.ascontiguousarray(x, dtype="<f4").tobytes()
    frames = len(x)
    head = (b"RIFF" + (50 + len(data)).to_bytes(4, "little") + b"WAVE" + b"fmt " + (18).to_bytes(4, "little") +
            (3).to_bytes(2, "little") + (2).to_bytes(2, "little") + SR.to_bytes(4, "little") + (SR * 8).to_bytes(4, "little") +
            (8).to_bytes(2, "little") + (32).to_bytes(2, "little") + (0).to_bytes(2, "little") +
            b"fact" + (4).to_bytes(4, "little") + frames.to_bytes(4, "little") + b"data" + len(data).to_bytes(4, "little"))
    with open(path, "wb") as f:
        f.write(head + data)


def song(n0, n1, kind):
    """the song from sample n0 to n1 (ppq = n / SR * 2): a tone per beat, or noise"""
    n = np.arange(n0, n1)
    t = n / SR
    beat = np.floor(t * 2.0).astype(int)
    env = np.exp(-3.0 * (t * 2.0 - beat))
    if kind == "noise":
        rng = np.random.default_rng(1)
        s = rng.standard_normal(len(n)) * 0.1 * env
        return np.stack([s, s * 0.9], axis=1)
    f0 = 440.0 * 2.0 ** ((np.array(NOTES)[beat % 8] - 69) / 12.0)
    s = sum(np.sin(2 * np.pi * f0 * h * t) / h for h in range(1, 9))
    return np.stack([0.3 * env * s, 0.25 * env * s], axis=1)


def write_take(folder, label, take, n0, n1, kind, gain=1.0, midi=False):
    name = f"{label}_{take:03d}"
    x = song(n0, n1, kind) * gain
    write_wav(os.path.join(folder, name + ".wav"), x)
    rows = [[s, (n0 + s) / SR * TEMPO / 60.0, math.floor((n0 + s) / SR * 2 / 4) * 4.0, TEMPO, 1, 4, 4, n0 + s]
            for s in range(0, n1 - n0, 512)]
    meta = {"probr": 1, "label": label, "take": take, "wav": name + ".wav", "midi": (name + ".midi.json") if midi else None,
            "sample_rate": SR, "channels": 2, "sample_format": "float32", "frames": n1 - n0, "tempo": TEMPO,
            "time_signature": [4, 4], "ended": "transport stopped", "time_map": rows}
    with open(os.path.join(folder, name + ".json"), "w") as f:
        json.dump(meta, f)
    if midi:
        ev = []
        for b in range(int(n0 / SR * 2), int(math.ceil(n1 / SR * 2))):
            ev.append({"sample": int(b * SR / 2 - n0), "ppq": float(b), "channel": 0, "type": "note_on", "note": NOTES[b % 8], "velocity": 0.8})
            ev.append({"sample": int((b + 0.9) * SR / 2 - n0), "ppq": b + 0.9, "channel": 0, "type": "note_off", "note": NOTES[b % 8],
                       "velocity": 0.0})
        with open(os.path.join(folder, name + ".midi.json"), "w") as f:
            json.dump({"probr_midi": 1, "events": ev}, f)


def read_csv(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def num(s):
    return float(s) if s not in ("", None) else float("nan")


def run(script, *args):
    r = subprocess.run([sys.executable, script, *args], capture_output=True, text=True)
    print(r.stdout + r.stderr)
    return r.returncode


def main():
    script, work = sys.argv[1], sys.argv[2]
    shutil.rmtree(work, ignore_errors=True)
    session = os.path.join(work, "2026-10-08_14-03-22")
    os.makedirs(session)
    beat = SR // 2
    write_take(session, "1 source", 1, 0, 8 * beat, "tone", midi=True)
    write_take(session, "2 louder", 1, 1 * beat, 4 * beat, "tone", gain=2.0)   # armed a beat in
    write_take(session, "2 louder", 2, 4 * beat, 8 * beat, "tone", gain=2.0)   # stopped and started again
    write_take(session, "3 noise", 1, 0, 8 * beat, "noise")
    out = os.path.join(work, "analysis")
    check(run(script, session, "--out", out) == 0, "probr_align.py runs")

    with open(os.path.join(out, "timeline.json")) as f:
        tl = json.load(f)
    check([p["name"] for p in tl["probes"]] == ["1 source", "2 louder", "3 noise"], "three probes, in natural order")
    louder = tl["probes"][1]
    check(len(louder["takes"]) == 2 and abs(louder["ppq_range"][0] - 1.0) < 1e-9, "the louder probe: two takes from ppq 1")
    check(tl["common_ppq_range"] and abs(tl["common_ppq_range"][0] - 1.0) < 1e-9, f"common range from ppq 1: {tl['common_ppq_range']}")
    check(tl["notes"] == 8, f"eight notes from the source's MIDI ({tl['notes']})")

    lv = read_csv(os.path.join(out, "diff_2 louder_minus_1 source.levels.csv"))
    d = [num(r["rms_db"]) for r in lv]
    check(len(lv) == 7 * 16, f"7 beats of 1/16 bins in common ({len(lv)})")
    check(all(abs(v - 6.0206) < 0.01 for v in d), f"+6.02 dB on every bin (from {min(d):.4f} to {max(d):.4f})")
    sp = read_csv(os.path.join(out, "diff_2 louder_minus_1 source.spectra.csv"))
    src = {r["beat"]: r for r in read_csv(os.path.join(out, "1 source.spectra.csv"))}
    loud_bands = []
    for r in sp:
        ref = src[r["beat"]]
        top = max(num(v) for k, v in ref.items() if k.startswith("mid_") and v)
        # (the bands with the sound in them: within 60 dB of the loudest)
        loud_bands += [num(v) for k, v in r.items() if k.startswith("mid_") and v and num(ref[k]) > top - 60]
    check(len(sp) == 7 and loud_bands and all(abs(b - 6.0206) < 0.05 for b in loud_bands),
          f"+6.02 dB in every third-octave band of the 7 beats in common ({len(sp)} beats)")
    with open(os.path.join(out, "compare.json")) as f:
        cmp = json.load(f)
    check(abs(cmp["pairs"][0]["mean_db"]["mid_db"] - 6.0206) < 0.01, "compare.json: mid +6.02 dB")
    notes_src = read_csv(os.path.join(out, "1 source.notes.csv"))
    notes_noise = read_csv(os.path.join(out, "3 noise.notes.csv"))
    peaky = [num(r["peakiness_db"]) for r in notes_src]
    flat = [num(r["peakiness_db"]) for r in notes_noise]
    check(len(notes_src) == 8 and min(peaky) > 20.0, f"the tone's notes are peaky ({min(peaky):.1f} dB at least)")
    check(len(notes_noise) == 8 and max(flat) < 8.0, f"the noise's are not ({max(flat):.1f} dB at most)")
    nd = read_csv(os.path.join(out, "diff_2 louder_minus_1 source.notes.csv"))
    check(len(nd) == 7 and all(abs(num(r["level_db"]) - 6.0206) < 0.05 and abs(num(r["peakiness_db"])) < 0.5 for r in nd),
          "per note: +6.02 dB, the same peakiness")
    check(all(abs(num(r["h1_db"]) - num(r["h2_db"]) - 6.02) < 1.0 for r in notes_src),
          "the first harmonic 6 dB over the second (1/h amplitudes)")
    lvs = read_csv(os.path.join(out, "1 source.levels.csv"))
    corr = [num(r["correlation"]) for r in lvs if r["correlation"]]
    check(corr and min(corr) > 0.999, "left and right correlated")

    if len(sys.argv) > 3:
        made = sys.argv[3]
        sessions = [os.path.join(made, d) for d in os.listdir(made) if os.path.isdir(os.path.join(made, d))]
        check(len(sessions) == 1, f"the writer's session folder ({sessions})")
        out2 = os.path.join(work, "analysis-writer")
        check(run(script, sessions[0], "--out", out2) == 0, "probr_align.py runs on the writer's session")
        with open(os.path.join(out2, "timeline.json")) as f:
            tl2 = json.load(f)
        check([p["name"] for p in tl2["probes"]] == ["1 source", "2 half"], "its two probes")
        check(abs(tl2["common_ppq_range"][0] - 1.0) < 0.01, f"the half probe from ppq 1 ({tl2['common_ppq_range']})")
        lv2 = read_csv(os.path.join(out2, "diff_2 half_minus_1 source.levels.csv"))
        mids = [num(r["mid_db"]) for r in lv2]
        check(len(lv2) >= 6 * 16 and all(abs(v + 6.0206) < 0.01 for v in mids),
              f"mid -6.02 dB on every bin in common ({len(lv2)} bins, {min(mids):.4f} .. {max(mids):.4f})")
        half = read_csv(os.path.join(out2, "2 half.levels.csv"))
        check(all(r["side_db"] == "-inf" for r in half), "no side")
        check(len(read_csv(os.path.join(out2, "2 half.notes.csv"))) >= 6, "the source's MIDI notes analysed on the half probe too")

    print(f"{'OK' if not fails else 'FAILED'}: {fails} failures")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
