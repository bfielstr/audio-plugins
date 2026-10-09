#!/usr/bin/env python3
"""scripts/als_extract.py --moistr on a small synthetic Live set made here (no real project): every lane type maps
to the moistr target it should, with its range in the target's units, on one timeline; unmapped lanes are listed.

usage: als_extract_test.py path/to/als_extract.py
"""
import gzip
import json
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print(f"    FAIL {what}")


def sub(parent, tag, **attrs):
    return ET.SubElement(parent, tag, {k: str(v) for k, v in attrs.items()})


def param(parent, tag, value, target=None):
    p = sub(parent, tag)
    sub(p, "Manual", Value=value)
    if target is not None:
        sub(p, "AutomationTarget", Id=target)
    return p


def on(dev):
    param(dev, "On", "true")


def eq8(parent, ident, bands):
    """bands: (index, mode, freq, freq target, q target)"""
    dev = sub(parent, "Eq8", Id=ident)
    on(dev)
    for b, mode, freq, ft, qt in bands:
        pa = sub(sub(dev, f"Bands.{b}"), "ParameterA")
        param(pa, "IsOn", "true")
        param(pa, "Mode", mode)
        param(pa, "Freq", freq, ft)
        param(pa, "Gain", 0)
        param(pa, "Q", 0.71, qt)
    return dev


def branch(branches, name, volume_target):
    br = sub(branches, "AudioEffectBranch")
    sub(sub(br, "Name"), "EffectiveName", Value=name)
    mix = sub(br, "MixerDevice")
    param(mix, "Volume", 1, volume_target)
    param(mix, "Speaker", "true")
    return sub(sub(sub(br, "DeviceChain"), "AudioToAudioDeviceChain"), "Devices")


def make_set(path):
    root = ET.Element("Ableton")
    live = sub(root, "LiveSet")
    tracks = sub(live, "Tracks")
    group = sub(tracks, "GroupTrack", Id=1)
    sub(sub(group, "Name"), "EffectiveName", Value="Synth Group")
    sub(group, "TrackGroupId", Value=-1)
    gdc = sub(group, "DeviceChain")
    sub(gdc, "Mixer")
    sub(sub(gdc, "DeviceChain"), "Devices")
    t = sub(tracks, "MidiTrack", Id=2)
    sub(sub(t, "Name"), "EffectiveName", Value="Lead")
    sub(t, "TrackGroupId", Value=1)
    dc = sub(t, "DeviceChain")
    param(sub(dc, "Mixer"), "Volume", 1, 100)
    devices = sub(sub(dc, "DeviceChain"), "Devices")
    # a rack of bands: Sub (a high cut at 120 Hz), Band A (a low cut at 600 Hz, a tremolo), Band B (a low cut at
    # 4 kHz), Grit (a saturator, no cut), and an effect the gesture has no target for
    rack = sub(devices, "AudioEffectGroupDevice", Id=10)
    on(rack)
    branches = sub(rack, "Branches")
    eq8(branch(branches, "Sub", 140), 11, [(7, 6, 120, 901, 902)])
    a = branch(branches, "Band A", 101)
    eq8(a, 12, [(0, 1, 600, 903, 904)])
    pan = sub(a, "AutoPan", Id=13)
    on(pan)
    lfo = sub(pan, "Lfo")
    param(lfo, "Frequency", 4, 120)
    param(lfo, "LfoAmount", 0.5, 121)
    eq8(branch(branches, "Band B", 102), 14, [(0, 1, 4000, 905, 906)])
    grit = branch(branches, "Grit", 103)
    sat = sub(grit, "Saturator", Id=15)
    on(sat)
    param(sat, "Drive", 5, 907)
    verb = sub(grit, "Reverb", Id=16)
    on(verb)
    param(verb, "DecayTime", 1, 130)
    # a rack that splits by frequency: Lower (its high cut moves: the crossover), Upper (a low cut at 300 Hz and a
    # high cut that closes, with its Q)
    split = sub(devices, "AudioEffectGroupDevice", Id=20)
    on(split)
    sb = sub(split, "Branches")
    eq8(branch(sb, "Lower", 908), 21, [(7, 6, 600, 110, 909)])
    eq8(branch(sb, "Upper", 910), 22, [(0, 1, 300, 911, 912), (7, 6, 18000, 111, 112)])
    # the automation
    envs = sub(sub(t, "AutomationEnvelopes"), "Envelopes")
    lanes = {
        100: [(0, 1), (4, 1), (6, 0.5)],              # the track's volume (no band: skipped)
        101: [(0, 1), (4, 1), (6, 0.1), (8, 1)],      # Band A: High Level, 0 .. -20 dB
        102: [(0, 1), (5, 0.5), (7, 1)],              # Band B: Air Level
        103: [(0, 0.25), (6, 1), (8, 0.25)],          # Grit: Dirt
        110: [(4, 600), (6, 1200), (8, 600)],         # Lower's high cut: Mid X
        111: [(4, 18000), (7, 400), (8, 18000)],      # Upper's high cut: Close
        112: [(4, 0.7), (7, 4)],                      # its Q: tied to Close
        120: [(4, 4), (6, 8)],                        # the tremolo's rate (Hz): Wobble Rate
        121: [(4, 0), (5, 1)],                        # its amount: Wobble Amount
        130: [(4, 1), (8, 3)],                        # reverb decay: no target
        140: [(4, 1), (6, 0.5)],                      # Sub: the low band, skipped
    }
    for i, (target, pts) in enumerate(lanes.items()):
        env = sub(envs, "AutomationEnvelope", Id=i)
        sub(sub(env, "EnvelopeTarget"), "PointeeId", Value=target)
        ev = sub(sub(env, "Automation"), "Events")
        sub(ev, "FloatEvent", Id=0, Time=-63072000, Value=pts[0][1])
        for k, (time, value) in enumerate(pts):
            sub(ev, "FloatEvent", Id=k + 1, Time=time, Value=value)
    master = sub(live, "MasterTrack")
    param(sub(sub(master, "DeviceChain"), "Mixer"), "Tempo", 120)
    with gzip.open(path, "wb") as f:
        f.write(ET.tostring(root))


def main():
    script = sys.argv[1]
    with tempfile.TemporaryDirectory() as d:
        als = os.path.join(d, "synthetic.als")
        out = os.path.join(d, "g", "gesture.json")
        make_set(als)
        r = subprocess.run([sys.executable, "-I", script, als, "--group", "Synth Group", "--beats", "4", "8", "--moistr", out],
                           capture_output=True, text=True)
        print(r.stdout[-3000:])
        check(r.returncode == 0, f"the extractor runs ({r.stderr[-500:]})")
        g = json.load(open(out))
        check(g["length_beats"] == 4 and g["name"] == "Synth Group 4-8", f"one gesture over the window ({g['name']}, {g['length_beats']})")
        by = {}
        for lane in g["lanes"]:
            if lane["target"] != "Off":
                by[lane["target"]] = lane
        want = {"High Level": "Band A", "Air Level": "Band B", "Dirt": "Grit", "Mid X": "Lower", "Close": "Upper",
                "Wobble Rate": "LFO Rate", "Wobble Amount": "LFO Amount"}
        check(set(by) == set(want), f"the targets: {sorted(by)}")
        for target, part in want.items():
            if target in by:
                check(part in by[target]["source"], f"{target} from {part} ({by[target]['source']})")
                pts = by[target]["points"]
                check(pts[0][0] == 0 and pts[-1][0] == 4 and all(0 <= v <= 1 for _, v in pts) and
                      all(pts[i][0] <= pts[i + 1][0] for i in range(len(pts) - 1)), f"{target}: points from 0 to 4 beats, 0 .. 1")
        if "High Level" in by:
            hl = by["High Level"]
            check(abs(hl["min"] + 20) < 1e-6 and hl["max"] == 0, f"High Level: -20 .. 0 dB ({hl['min']} .. {hl['max']})")
            check(hl["points"][:2] == [[0, 1], [2, 0]], f"High Level: at its top, then down by beat 2 ({hl['points'][:2]})")
        if "Close" in by:
            c = by["Close"]
            check(c["min"] == 400 and c["max"] == 18000 and c["points"][1] == [3, 0], f"Close: 400 .. 18000 Hz, shut on beat 3 ({c})")
        if "Wobble Rate" in by:
            w = by["Wobble Rate"]
            check(w["min"] == 2 and w["max"] == 4, f"Wobble Rate: 4 .. 8 Hz at 120 bpm is 2 .. 4 cycles per beat ({w['min']} .. {w['max']})")
        if "Mid X" in by:
            m = by["Mid X"]
            check(m["min"] == 600 and m["max"] == 1200 and abs(m["points"][1][1] - 1) < 1e-9, "Mid X: 600 .. 1200 Hz")
        if "Dirt" in by:
            check(by["Dirt"]["min"] == 0 and by["Dirt"]["max"] == 1, "Dirt: 0 .. 1")
        reasons = {s["source"].split(" > ")[-2] + " " + s["source"].split(" > ")[-1]: s["reason"] for s in g["skipped"]}
        check(any("Q" in k and "tied" in v for k, v in reasons.items()), f"the cut's Q: tied to Close ({reasons})")
        check(any("Reverb" in k for k in reasons), "the reverb: skipped")
        check(any("Sub" in s["source"] and "low band" in s["reason"] for s in g["skipped"]), "Sub: the low band, skipped")
        check(any("Track Volume" in s["source"] for s in g["skipped"]), "the track's volume (no band): skipped")
        check("moistr target" in r.stdout and "Close" in r.stdout, "a mapping table printed")
        # --list: the lanes with the target each maps to
        r = subprocess.run([sys.executable, "-I", script, als, "--list"], capture_output=True, text=True)
        check(r.returncode == 0 and "-> Close" in r.stdout and "-> Wobble Rate" in r.stdout and "==" not in r.stdout,
              "--list: the lanes and their targets, no device chains")
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
