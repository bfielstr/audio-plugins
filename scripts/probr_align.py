#!/usr/bin/env python3
"""probr_align.py SESSION [--out DIR] [--order A,B,...] [--grid N] [--range START END]

Lines up every probe of a probr session by song position and writes an analysis of each, so the
stages of a chain can be compared (stage B minus stage A).

SESSION is a session folder probr wrote (<folder>/<session id>/): per take <label>_<take>.wav (32-bit
float stereo), <label>_<take>.json (sample rate, tempo, time signature and the time map) and, when MIDI
came in, <label>_<take>.midi.json. Every take of one label is one probe; takes are placed on the song's
timeline by their time map (ppq: quarter notes from the song's start), so probes armed at different
times, takes split by stopping and starting, and loops all land on the same beats. Only the samples
recorded while the song played count.

Written to DIR (default SESSION/analysis):
  timeline.json               the probes, their takes and ppq ranges, the range all probes cover
  <probe>.levels.csv          per 1/N beat (--grid, 16 by default): RMS, peak, mid, side, side minus
                              mid and the left/right correlation
  <probe>.spectra.csv         per beat: third-octave band levels (25 Hz .. 20 kHz) of the mid and of
                              the side signal
  <probe>.notes.csv           per MIDI note (from any probe of the session: they share the timeline):
                              its level, how peaky its harmonics are (mean peak over the floor between
                              harmonics, dB), the share of its power at the harmonics and the first
                              harmonics' levels
  <probe>.analysis.json       all of the above in one file
  diff_<B>_minus_<A>.levels.csv, .spectra.csv, .notes.csv
                              each probe minus the one before it in the order (--order, else the
                              labels sorted naturally: number them, "1 dry", "2 after EQ", ...), on
                              the bins, beats and notes both have
  compare.json                per pair: the mean level, mid, side and band differences

Levels are dBFS (a full-scale sine reads -3 dB RMS); "-inf" where a bin is silent. Needs Python 3 and
numpy. Everything stays on this computer.
"""
import argparse
import csv
import json
import math
import os
import re
import sys

try:
    import numpy as np
except ImportError:  # (the test skips without numpy)
    np = None

BANDS = [1000.0 * 2.0 ** (k / 3.0) for k in range(-16, 14)]  # 25 Hz .. 20 kHz, third-octave centres
HARMONICS_SHOWN = 8


# ---- reading a session ---------------------------------------------------------------------------

def read_wav(path):
    """(sample rate, frames x channels float32) of a WAV: 32-bit float or 16/24/32-bit PCM."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    at, fmt, rate, channels, bits, body = 12, None, 0, 0, 0, None
    while at + 8 <= len(data):
        cid, size = data[at:at + 4], int.from_bytes(data[at + 4:at + 8], "little")
        b = at + 8
        if cid == b"fmt ":
            fmt = int.from_bytes(data[b:b + 2], "little")
            channels = int.from_bytes(data[b + 2:b + 4], "little")
            rate = int.from_bytes(data[b + 4:b + 8], "little")
            bits = int.from_bytes(data[b + 14:b + 16], "little")
            if fmt == 0xFFFE and size >= 26:  # WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes
                fmt = int.from_bytes(data[b + 24:b + 26], "little")
        elif cid == b"data":
            body = data[b:b + min(size, len(data) - b)]
            break
        at = b + size + (size & 1)
    if body is None or not channels:
        raise ValueError(f"{path}: no audio")
    frame = channels * bits // 8
    body = body[:len(body) // frame * frame]
    if fmt == 3 and bits == 32:
        x = np.frombuffer(body, dtype="<f4")
    elif fmt == 1 and bits == 16:
        x = np.frombuffer(body, dtype="<i2").astype(np.float32) / 32768.0
    elif fmt == 1 and bits == 32:
        x = np.frombuffer(body, dtype="<i4").astype(np.float32) / 2147483648.0
    elif fmt == 1 and bits == 24:
        b = np.frombuffer(body, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = (np.where(v >= 1 << 23, v - (1 << 24), v)).astype(np.float32) / 8388608.0
    else:
        raise ValueError(f"{path}: format {fmt}, {bits} bits is not read")
    x = x.reshape(-1, channels).astype(np.float64)
    if channels == 1:
        x = np.repeat(x, 2, axis=1)
    return rate, x[:, :2]


def sample_ppq(take, frames):
    """(ppq per sample, playing per sample) from the take's time map: an entry holds until the next;
    while playing ppq advances at its tempo, stopped it stays."""
    sr = float(take["sample_rate"])
    rows = take.get("time_map") or []
    n = np.arange(frames, dtype=np.float64)
    if not rows or all(r[1] is None for r in rows):
        # no song position from the host: seconds from the take's start at its tempo (or 120)
        tempo = take.get("tempo") or 120.0
        return n / sr * tempo / 60.0, np.ones(frames, dtype=bool), False
    s = np.array([r[0] for r in rows], dtype=np.float64)
    p = np.array([r[1] if r[1] is not None else np.nan for r in rows], dtype=np.float64)
    t = np.array([r[3] if r[3] is not None else 120.0 for r in rows], dtype=np.float64)
    pl = np.array([bool(r[4]) for r in rows])
    # entries without a ppq take the one before them, moved on
    for i in range(1, len(p)):
        if np.isnan(p[i]):
            p[i] = p[i - 1] + ((s[i] - s[i - 1]) / sr * t[i - 1] / 60.0 if pl[i - 1] else 0.0)
    if np.isnan(p[0]):
        p[0] = 0.0
    idx = np.clip(np.searchsorted(s, n, side="right") - 1, 0, len(s) - 1)
    ppq = p[idx] + np.where(pl[idx], (n - s[idx]) / sr * t[idx] / 60.0, 0.0)
    return ppq, pl[idx], True


def natural_key(s):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]


def load_session(folder):
    probes = {}
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".json") or name.endswith(".midi.json"):
            continue
        path = os.path.join(folder, name)
        try:
            with open(path, encoding="utf-8") as f:
                take = json.load(f)
        except (OSError, ValueError) as e:
            print(f"  {name}: not read ({e})", file=sys.stderr)
            continue
        if not isinstance(take, dict) or take.get("probr") != 1:
            continue
        wav = os.path.join(folder, take["wav"])
        if not os.path.exists(wav):
            print(f"  {name}: its WAV {take['wav']} is missing", file=sys.stderr)
            continue
        rate, x = read_wav(wav)
        take["sample_rate"] = float(take.get("sample_rate") or rate)
        ppq, playing, aligned = sample_ppq(take, len(x))
        midi = []
        if take.get("midi"):
            mpath = os.path.join(folder, take["midi"])
            if os.path.exists(mpath):
                with open(mpath, encoding="utf-8") as f:
                    midi = json.load(f).get("events", [])
        base = re.sub(r"_\d+$", "", os.path.splitext(take["wav"])[0])
        probe = probes.setdefault(base, {"name": base, "label": take.get("label", base), "takes": []})
        probe["takes"].append({"take": take.get("take"), "json": name, "wav": take["wav"], "sr": take["sample_rate"],
                               "x": x, "ppq": ppq, "playing": playing, "aligned": aligned, "midi": midi,
                               "ended": take.get("ended"), "tempo": take.get("tempo"),
                               "time_signature": take.get("time_signature")})
    return probes


def notes_of(probes):
    """Every note of the session (from any probe's MIDI), by song position: (ppq on, ppq off, pitch,
    channel, velocity), duplicates (the same MIDI on two probes) once."""
    seen, out = set(), []
    for probe in probes.values():
        for take in probe["takes"]:
            held = {}
            for e in take["midi"]:
                if e.get("ppq") is None:
                    continue
                key = (e.get("channel", 0), e.get("note"))
                if e["type"] == "note_on":
                    held[key] = e
                elif e["type"] == "note_off" and key in held:
                    on = held.pop(key)
                    if e["ppq"] > on["ppq"]:
                        k = (round(on["ppq"], 4), on["note"], on.get("channel", 0))
                        if k not in seen:
                            seen.add(k)
                            out.append((on["ppq"], e["ppq"], on["note"], on.get("channel", 0), on.get("velocity", 0.0)))
    return sorted(out)


# ---- the analysis --------------------------------------------------------------------------------

def db(power):
    with np.errstate(divide="ignore"):
        return 10.0 * np.log10(np.asarray(power, dtype=np.float64))


def fmt(v):
    if v is None or (isinstance(v, float) and math.isnan(v)):
        return ""
    if isinstance(v, float) and math.isinf(v):
        return "-inf" if v < 0 else "inf"
    return f"{v:.3f}" if isinstance(v, float) else str(v)


def jnum(v):
    return None if v is None or not math.isfinite(v) else round(float(v), 4)


def segments(mask):
    """(start, end) of the runs of True."""
    if not mask.any():
        return []
    d = np.diff(np.concatenate(([0], mask.astype(np.int8), [0])))
    return list(zip(np.flatnonzero(d == 1), np.flatnonzero(d == -1)))


def frame_size(sr, seconds):
    n = 256
    while n < sr * seconds:
        n *= 2
    return n


def power_spectrum(signal, runs, n):
    """The mean power per rfft bin over Hann frames of n (hop n/2) in the runs of `signal`, scaled so
    the bins add up to the signal's mean square; (spectrum, frames)."""
    w = np.hanning(n)
    norm = n * np.sum(w * w)
    acc, count = np.zeros(n // 2 + 1), 0
    for a, b in runs:
        seg = signal[a:b]
        if len(seg) < n:
            if len(seg) < n // 4:
                continue
            seg = np.concatenate((seg, np.zeros(n - len(seg))))
            starts = [0]
        else:
            starts = range(0, len(seg) - n + 1, n // 2)
        for s0 in starts:
            spec = np.abs(np.fft.rfft(seg[s0:s0 + n] * w)) ** 2 / norm
            spec[1:-1] *= 2.0
            acc += spec
            count += 1
    return (acc / count if count else None), count


def band_levels(spec, sr, n):
    f = np.arange(len(spec)) * sr / n
    out = []
    for c in BANDS:
        lo, hi = c * 2 ** (-1 / 6), c * 2 ** (1 / 6)
        sel = (f >= lo) & (f < hi)
        out.append(float(db(spec[sel].sum())) if sel.any() and hi < sr / 2 else float("nan"))
    return out


def note_analysis(mid, runs, sr, pitch):
    """A note's harmonics in the runs of `mid`: level, peakiness (mean over the harmonics of the peak
    against the median floor between harmonics, dB), the share of the power at the harmonics (dB) and
    the first harmonics' levels."""
    n = frame_size(sr, 0.1)
    spec, frames = power_spectrum(mid, runs, n)
    if spec is None:
        return None
    f0 = 440.0 * 2 ** ((pitch - 69) / 12.0)
    bin_hz = sr / n
    top = min(sr * 0.45, 16000.0)
    peaks, floors, levels, harmonic_power = [], [], [], 0.0
    h = 1
    while h * f0 < top and h <= 32:
        fc = h * f0
        lo, hi = int(math.floor(fc * 2 ** (-1 / 24) / bin_hz)), int(math.ceil(fc * 2 ** (1 / 24) / bin_hz))
        lo, hi = max(lo, 1), max(hi, lo + 1)
        if hi >= len(spec):
            break
        peak = spec[lo:hi + 1].max()
        window = spec[lo:hi + 1].sum()  # (the harmonic's power: the window's sum does not depend on where it falls between bins)
        a, b = int((h - 0.5) * f0 / bin_hz), int((h + 0.5) * f0 / bin_hz)
        between = np.concatenate((spec[max(a, 1):lo], spec[hi + 1:min(b, len(spec))]))
        floor = np.median(between) if len(between) else np.nan
        peaks.append(peak)
        floors.append(floor)
        levels.append(float(db(window)))
        harmonic_power += window
        h += 1
    if not peaks:
        return None
    a, b = max(int(0.5 * f0 / bin_hz), 1), min(int((h - 0.5) * f0 / bin_hz), len(spec) - 1)
    total = spec[a:b + 1].sum()
    ratios = [10 * math.log10(p / fl) for p, fl in zip(peaks, floors) if fl and fl > 0 and p > 0]
    strongest = max(peaks)
    # (harmonics more than 60 dB under the strongest are noise: they do not count)
    weighted = [r for r, p in zip(ratios, peaks) if p > strongest * 1e-6]
    return {"f0": f0, "frames": frames, "level_db": float(db(spec.sum())),
            "peakiness_db": float(np.mean(weighted)) if weighted else float("nan"),
            "harmonic_share_db": float(db(harmonic_power / total)) if total > 0 else float("nan"),
            "harmonics_db": levels[:HARMONICS_SHOWN]}


def analyse(probe, grid, notes, ppq_range):
    """Levels per 1/grid beat, spectra per beat, notes; all takes of the probe together."""
    sr = probe["takes"][0]["sr"]
    L, R, P = [], [], []
    for t in probe["takes"]:
        if t["sr"] != sr:
            print(f"  {probe['name']}: take {t['take']} is at {t['sr']} Hz, not {sr}: left out", file=sys.stderr)
            continue
        m = t["playing"] & np.all(np.isfinite(t["x"]), axis=1)
        if ppq_range:
            m &= (t["ppq"] >= ppq_range[0]) & (t["ppq"] < ppq_range[1])
        L.append(np.where(m, t["x"][:, 0], 0.0))
        R.append(np.where(m, t["x"][:, 1], 0.0))
        P.append(np.where(m, t["ppq"], np.nan))
    # (takes one after the other, a NaN gap between them so no frame spans two)
    gap = np.full(1, np.nan)
    ppq = np.concatenate([np.concatenate((p, gap)) for p in P]) if P else np.zeros(0)
    left = np.concatenate([np.concatenate((x, [0.0])) for x in L]) if L else np.zeros(0)
    right = np.concatenate([np.concatenate((x, [0.0])) for x in R]) if R else np.zeros(0)
    valid = np.isfinite(ppq)
    mid, side = 0.5 * (left + right), 0.5 * (left - right)
    out = {"sample_rate": sr, "levels": [], "spectra": [], "notes": []}
    if not valid.any():
        return out

    # levels per 1/grid beat
    # (a hair of slack: the same song position computed from two time maps may differ in the last bits)
    bins = np.floor(ppq[valid] * grid + 1e-6).astype(np.int64)
    first = bins.min()
    k = bins - first
    cnt = np.bincount(k)
    def mean_of(v):
        return np.bincount(k, weights=v[valid]) / np.maximum(cnt, 1)
    ll, rr, lr = mean_of(left * left), mean_of(right * right), mean_of(left * right)
    mm, ss = mean_of(mid * mid), mean_of(side * side)
    peak = np.zeros(len(cnt))
    np.maximum.at(peak, k, np.maximum(np.abs(left[valid]), np.abs(right[valid])))
    for i in np.flatnonzero(cnt):
        corr = lr[i] / math.sqrt(ll[i] * rr[i]) if ll[i] > 0 and rr[i] > 0 else float("nan")
        out["levels"].append({"ppq": (first + i) / grid, "samples": int(cnt[i]),
                              "rms_db": float(db(0.5 * (ll[i] + rr[i]))), "peak_db": float(db(peak[i] ** 2)),
                              "mid_db": float(db(mm[i])), "side_db": float(db(ss[i])),
                              "side_minus_mid_db": float(db(ss[i]) - db(mm[i])) if mm[i] > 0 and ss[i] > 0 else float("nan"),
                              "correlation": corr})

    # third-octave spectra per beat
    n = frame_size(sr, 0.085)
    beats = np.floor(ppq + 1e-7).astype(np.float64)
    for b in np.unique(beats[valid]):
        runs = segments(valid & (beats == b))
        sm, frames = power_spectrum(mid, runs, n)
        if sm is None:
            continue
        ssp, _ = power_spectrum(side, runs, n)
        out["spectra"].append({"beat": float(b), "frames": frames, "mid_db": band_levels(sm, sr, n),
                               "side_db": band_levels(ssp, sr, n)})

    # notes
    for on, off, pitch, ch, vel in notes:
        runs = segments(valid & (ppq >= on) & (ppq < off))
        if not runs:
            continue
        a = note_analysis(mid, runs, sr, pitch)
        if a:
            a.update({"ppq_on": on, "ppq_off": off, "note": pitch, "channel": ch, "velocity": vel})
            out["notes"].append(a)
    return out


# ---- writing -------------------------------------------------------------------------------------

def write_csv(path, header, rows):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        for r in rows:
            w.writerow([fmt(v) for v in r])


def band_names(prefix):
    return [f"{prefix}_{c:.0f}Hz" if c < 1000 else f"{prefix}_{c / 1000:.1f}kHz" for c in BANDS]


LEVEL_KEYS = ["rms_db", "peak_db", "mid_db", "side_db", "side_minus_mid_db", "correlation"]
NOTE_KEYS = ["level_db", "peakiness_db", "harmonic_share_db"]


def write_probe(out, name, a):
    write_csv(os.path.join(out, f"{name}.levels.csv"), ["ppq", "samples"] + LEVEL_KEYS,
              [[l["ppq"], l["samples"]] + [l[k] for k in LEVEL_KEYS] for l in a["levels"]])
    write_csv(os.path.join(out, f"{name}.spectra.csv"), ["beat", "frames"] + band_names("mid") + band_names("side"),
              [[s["beat"], s["frames"]] + s["mid_db"] + s["side_db"] for s in a["spectra"]])
    if a["notes"]:
        write_csv(os.path.join(out, f"{name}.notes.csv"),
                  ["ppq_on", "ppq_off", "note", "channel", "f0"] + NOTE_KEYS + [f"h{i + 1}_db" for i in range(HARMONICS_SHOWN)],
                  [[n["ppq_on"], n["ppq_off"], n["note"], n["channel"], n["f0"]] + [n[k] for k in NOTE_KEYS] +
                   (n["harmonics_db"] + [None] * HARMONICS_SHOWN)[:HARMONICS_SHOWN] for n in a["notes"]])

    def clean(v):
        if isinstance(v, float):
            return jnum(v)
        if isinstance(v, list):
            return [clean(x) for x in v]
        if isinstance(v, dict):
            return {k: clean(x) for k, x in v.items()}
        return v
    with open(os.path.join(out, f"{name}.analysis.json"), "w", encoding="utf-8") as f:
        json.dump(clean({"probe": name, "bands_hz": BANDS, **a}), f, indent=1)


def diff(a, b):
    return b - a if math.isfinite(a) and math.isfinite(b) else float("nan")


def compare(out, na, a, nb, b):
    """b minus a on what both have; a summary for compare.json."""
    la = {round(l["ppq"], 6): l for l in a["levels"]}
    rows, sums = [], {k: [] for k in LEVEL_KEYS}
    for l in b["levels"]:
        o = la.get(round(l["ppq"], 6))
        if not o:
            continue
        d = [diff(o[k], l[k]) for k in LEVEL_KEYS]
        rows.append([l["ppq"]] + d)
        for k, v in zip(LEVEL_KEYS, d):
            if math.isfinite(v):
                sums[k].append(v)
    stem = f"diff_{nb}_minus_{na}"
    write_csv(os.path.join(out, f"{stem}.levels.csv"), ["ppq"] + LEVEL_KEYS, rows)
    sa = {s["beat"]: s for s in a["spectra"]}
    srows, bands = [], [[] for _ in BANDS]
    for s in b["spectra"]:
        o = sa.get(s["beat"])
        if not o:
            continue
        d = [diff(x, y) for x, y in zip(o["mid_db"], s["mid_db"])]
        ds = [diff(x, y) for x, y in zip(o["side_db"], s["side_db"])]
        srows.append([s["beat"]] + d + ds)
        for i, v in enumerate(d):
            if math.isfinite(v):
                bands[i].append(v)
    write_csv(os.path.join(out, f"{stem}.spectra.csv"), ["beat"] + band_names("mid") + band_names("side"), srows)
    na_ = {(round(n["ppq_on"], 4), n["note"]): n for n in a["notes"]}
    nrows, peaky = [], []
    for n in b["notes"]:
        o = na_.get((round(n["ppq_on"], 4), n["note"]))
        if not o:
            continue
        d = [diff(o[k], n[k]) for k in NOTE_KEYS]
        nrows.append([n["ppq_on"], n["note"]] + d)
        if math.isfinite(d[1]):
            peaky.append(d[1])
    if nrows:
        write_csv(os.path.join(out, f"{stem}.notes.csv"), ["ppq_on", "note"] + NOTE_KEYS, nrows)

    def mean(v):
        return jnum(float(np.mean(v))) if v else None
    return {"a": na, "b": nb, "files": stem, "bins": len(rows), "beats": len(srows), "notes": len(nrows),
            "mean_db": {k: mean(v) for k, v in sums.items()},
            "mean_band_db": {f"{c:g}": mean(v) for c, v in zip(BANDS, bands)},
            "mean_peakiness_db": mean(peaky)}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__.split("\n\n", 2)[2])
    ap.add_argument("session", help="a probr session folder (<folder>/<session id>)")
    ap.add_argument("--out", help="where to write (default: SESSION/analysis)")
    ap.add_argument("--order", help="the probes in chain order, by label or file name, comma-separated")
    ap.add_argument("--grid", type=int, default=16, help="level bins per beat (default 16)")
    ap.add_argument("--range", type=float, nargs=2, metavar=("START", "END"), help="only this ppq range")
    a = ap.parse_args(argv)
    if np is None:
        sys.exit("probr_align.py needs numpy (pip install numpy)")
    probes = load_session(a.session)
    if not probes:
        sys.exit(f"no probr takes in {a.session}")
    out = a.out or os.path.join(a.session, "analysis")
    os.makedirs(out, exist_ok=True)
    names = sorted(probes, key=natural_key)
    if a.order:
        want = [s.strip() for s in a.order.split(",") if s.strip()]
        by = {**{p["label"]: n for n, p in probes.items()}, **{n: n for n in probes}}
        missing = [w for w in want if w not in by]
        if missing:
            sys.exit(f"--order: no probe called {', '.join(missing)} (there are: {', '.join(names)})")
        names = [by[w] for w in want] + [n for n in names if n not in [by[w] for w in want]]
    notes = notes_of(probes)

    timeline = {"session": os.path.basename(os.path.normpath(a.session)), "grid": a.grid, "bands_hz": BANDS,
                "order": names, "notes": len(notes), "probes": []}
    starts, ends = [], []
    for n in names:
        p = probes[n]
        takes = []
        for t in p["takes"]:
            played = t["ppq"][t["playing"]]
            r = [float(played.min()), float(played.max())] if len(played) else None
            takes.append({"take": t["take"], "wav": t["wav"], "seconds": len(t["x"]) / t["sr"], "ppq_range": r,
                          "aligned": t["aligned"], "ended": t["ended"], "tempo": t["tempo"], "time_signature": t["time_signature"]})
        rs = [t["ppq_range"] for t in takes if t["ppq_range"]]
        rng = [min(r[0] for r in rs), max(r[1] for r in rs)] if rs else None
        if rng:
            starts.append(rng[0])
            ends.append(rng[1])
        timeline["probes"].append({"name": n, "label": p["label"], "takes": takes, "ppq_range": rng})
    timeline["common_ppq_range"] = [max(starts), min(ends)] if starts and max(starts) < min(ends) else None
    with open(os.path.join(out, "timeline.json"), "w", encoding="utf-8") as f:
        json.dump(timeline, f, indent=1)

    results = {}
    for n in names:
        results[n] = analyse(probes[n], a.grid, notes, a.range)
        write_probe(out, n, results[n])
        print(f"{n}: {len(probes[n]['takes'])} takes, {len(results[n]['levels'])} level bins, "
              f"{len(results[n]['spectra'])} beats, {len(results[n]['notes'])} notes")
    pairs = [compare(out, names[i - 1], results[names[i - 1]], names[i], results[names[i]]) for i in range(1, len(names))]
    with open(os.path.join(out, "compare.json"), "w", encoding="utf-8") as f:
        json.dump({"order": names, "pairs": pairs}, f, indent=1)
    for p in pairs:
        md = p["mean_db"]
        print(f"{p['b']} minus {p['a']}: RMS {md['rms_db']} dB, mid {md['mid_db']} dB, side {md['side_db']} dB "
              f"({p['bins']} bins, {p['beats']} beats, {p['notes']} notes)")
    print(f"written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
