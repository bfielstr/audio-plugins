#!/usr/bin/env python3
"""List and export automation lanes from an Ableton Live .als project.

usage: als_extract.py PROJECT.als [--group "Bass"] [--out DIR] [--no-tree]
                      [--moistr DIR --beats START END]

Prints the device chain of every track in the group (racks, chains, devices, signal flow) and every
automated parameter with a full path

    track > rack > chain > device > parameter

(rack and device: user name, else preset name or device type; chain: chain name; EQ Eight bands as
"Band N <Freq|Gain|Q> [<filter type>]"; plug-in parameters as "<plug-in>: <parameter name>"; macros as
"Macro N '<name>'" plus the parameters they are mapped to). Arrangement lanes and clip envelopes
(automation stored inside clips) are both exported. With --out, writes one JSON gesture per lane:

    {name, source:{als, track, path, parameter, ...}, time_unit:"beats",
     points:[[beat, value], ...], min, max}

Arrangement lanes and arrangement clip envelopes use arrangement beats (clip envelopes are unrolled
over the clip's placement and loop); session clip envelopes use clip-local beats.

With --moistr DIR --beats START END, cuts every lane to the beats START .. END (the value at START, the
points in between, the value just before END) and writes it into DIR as a moistr gesture:

    {name, length_beats, points:[[beat, value], ...], source_min, source_max}

beats from START, values normalised to 0 .. 1 over the lane's range in that window. Lanes that do not move
in the window are left out. Copy the files into moistr's Gestures folder (<presets>/bfielstr/Moistr/Gestures)
and pick them with File in moistr's GESTURES row.

The file is parsed as data only (gzip + ElementTree); nothing in it is executed.
"""
import argparse, gzip, json, os, re, sys
import xml.etree.ElementTree as ET

DEVICE_NAMES = {
    "Eq8": "EQ Eight", "Eq3": "EQ Three", "ChannelEq": "Channel EQ", "Compressor2": "Compressor",
    "GlueCompressor": "Glue Compressor", "MultibandDynamics": "Multiband Dynamics",
    "StereoGain": "Utility", "AutoFilter": "Auto Filter", "AutoPan": "Auto Pan",
    "InstrumentGroupDevice": "Instrument Rack", "AudioEffectGroupDevice": "Audio Effect Rack",
    "MidiEffectGroupDevice": "MIDI Effect Rack", "DrumGroupDevice": "Drum Rack",
    "OriginalSimpler": "Simpler", "MultiSampler": "Sampler", "UltraAnalog": "Analog",
    "InstrumentVector": "Wavetable", "MidiPitcher": "Pitch", "MidiArpeggiator": "Arpeggiator",
    "MidiChord": "Chord", "MidiScale": "Scale", "MidiVelocity": "Velocity", "MidiRandom": "Random",
    "Saturator": "Saturator", "Overdrive": "Overdrive", "Redux2": "Redux", "Redux": "Redux",
    "Erosion": "Erosion", "DrumBuss": "Drum Buss", "Pedal": "Pedal", "Tube": "Dynamic Tube",
    "FrequencyShifter": "Frequency Shifter", "Limiter": "Limiter", "Gate": "Gate",
    "Reverb": "Reverb", "Delay": "Delay", "PingPongDelay": "Ping Pong Delay",
    "FilterDelay": "Filter Delay", "Chorus2": "Chorus-Ensemble", "Chorus": "Chorus",
    "PhaserNew": "Phaser-Flanger", "Phaser": "Phaser", "Flanger": "Flanger", "Vocoder": "Vocoder",
    "Corpus": "Corpus", "Resonator": "Resonators", "BeatRepeat": "Beat Repeat", "Vinyl": "Vinyl Distortion",
    "Amp": "Amp", "Cabinet": "Cabinet", "Tuner": "Tuner", "SpectrumAnalyzer": "Spectrum",
    "Operator": "Operator", "MixerDevice": "Chain Mixer", "Mixer": "Mixer",
}
RACKS = {"InstrumentGroupDevice", "AudioEffectGroupDevice", "MidiEffectGroupDevice", "DrumGroupDevice"}
BRANCHES = {"InstrumentBranch", "AudioEffectBranch", "MidiEffectBranch", "DrumBranch"}
EQ8_MODES = ["Low Cut 48", "Low Cut 12", "Low Shelf", "Bell", "Notch", "High Shelf", "High Cut 12",
             "High Cut 48"]
OPERATOR_PARTS = {"Operator.0": "Osc A", "Operator.1": "Osc B", "Operator.2": "Osc C",
                  "Operator.3": "Osc D", "Lfo": "LFO", "PitchEnv": "Pitch Env"}
AUTOPAN_PARAMS = {"Frequency": "Rate (Hz)", "LfoAmount": "Amount", "LfoShape": "Shape",
                  "BeatRate": "Rate (sync)", "RateType": "Rate Mode", "Phase": "Phase", "Offset": "Offset"}
FILLER_TAGS = {"Player", "LoopModulators", "Slot", "Value", "Globals", "Pitch Envelope", "SimplerPitchEnvelope"}
SIMPLE_PARAMS = {"SampleStart": "Sample Start", "SampleLength": "Sample Length", "LoopLength": "Loop Length",
                 "LoopFade": "Loop Fade", "PortamentoTime": "Glide Time", "DecayTime": "Decay Time",
                 "EnvelopeAmount": "Amount", "DelayLine_TimeL": "Delay Time L", "Gain": "Gain"}
DEFAULT_TIME = -63072000.0  # Ableton's "value before the first point" event


def val(el, path, default=None):
    if el is None:
        return default
    n = el.find(path) if path else el
    return n.get("Value") if n is not None and n.get("Value") is not None else default


def track_name(t):
    return val(t, "Name/EffectiveName") or val(t, "Name/UserName") or f"{t.tag} {t.get('Id')}"


def plugin_name(dev):
    d = dev.find("PluginDesc")
    if d is None:
        return None
    for info in d:
        for p in ("PlugName", "Name"):
            v = val(info, p)
            if v:
                return v
    return None


def preset_name(dev):
    v = val(dev, "LastPresetRef/Value/FilePresetRef/FileRef/Name")
    return re.sub(r"\.(adg|adv)$", "", v) if v else None


def device_label(dev):
    user = val(dev, "UserName", "")
    if dev.tag == "PluginDevice":
        pn = plugin_name(dev) or "Plug-in"
        return f"{user} ({pn})" if user else pn
    if user:
        return user
    kind = DEVICE_NAMES.get(dev.tag, dev.tag)
    if dev.tag in RACKS:
        pr = preset_name(dev)
        if pr:
            return f"{kind} '{pr}'"
    return kind


def branch_label(br, index):
    n = val(br, "Name/UserName") or val(br, "Name/EffectiveName") or "Chain"
    return f"chain {index + 1} '{n}'"


def child_devices(el):
    """Devices of a branch / track device chain (any *DeviceChain/Devices below `el`)."""
    for c in el:
        if c.tag == "Devices":
            return list(c)
    for c in el:
        if c.tag.endswith("DeviceChain"):
            r = child_devices(c)
            if r is not None:
                return r
    return None


class Index:
    """Maps automation / modulation target ids to a full parameter path."""

    def __init__(self, track, tname=None):
        self.track = track
        self.targets = {}   # id -> dict(path=[...], parameter=str, kind)
        self.macro_maps = []  # (rack path, macro index, target path, param, min, max)
        self.flow = []      # printable device chain lines
        tname = tname or track_name(track)
        dc = track.find("DeviceChain")
        mixer = dc.find("Mixer") if dc is not None else None
        if mixer is not None:
            self._params(mixer, [tname], "Track Mixer", mixer, rack_stack=[])
        devs = child_devices(dc.find("DeviceChain")) if dc is not None and dc.find("DeviceChain") is not None else []
        self._chain(devs or [], [tname], depth=0, rack_stack=[])
        self._annotate_macros()
        # clip-level targets (clip volume / transpose / sample offset ... modulation, MIDI controllers)
        parent = {c: p for p in track.iter() for c in p}
        for el in track.iter():
            i = el.get("Id")
            if i and i not in self.targets and (el.tag.endswith("ModulationTarget") or el.tag == "AutomationTarget"):
                owner = el.tag[:-len("ModulationTarget")] if el.tag.endswith("ModulationTarget") and el.tag != "ModulationTarget" else parent[el].tag
                owner = re.sub(r"([a-z])([A-Z])", r"\1 \2", owner)
                if el.tag.endswith("ModulationTarget") and el.tag != "ModulationTarget":
                    self.targets[i] = dict(path=[tname, "Clip"], parameter=f"Clip {owner} (mod)", kind="mod")
                else:
                    self.targets[i] = dict(path=[tname, parent.get(parent[el], el).tag], parameter=owner, kind="auto")

    def _annotate_macros(self):
        """Append what each automated macro ultimately controls: Macro 1 (-> Simpler Sample Start)."""
        by_macro = {}
        for m in self.macro_maps:
            by_macro.setdefault((m["rack"], m["macro"]), []).append(m)

        def leaves(key, seen):
            out = []
            for m in by_macro.get(key, []):
                inner = (m["target"], m["parameter"])
                if inner in by_macro and inner not in seen:
                    out += leaves(inner, seen | {inner})
                else:
                    dev = m["target"].split(" > ")[-1]
                    out.append(f"{dev} {m['parameter']} {m['min']}..{m['max']}")
            return out
        for info in self.targets.values():
            if info["parameter"].startswith("Macro ") and "(mod)" not in info["parameter"]:
                key = (" > ".join(info["path"]), info["parameter"])
                lv = leaves(key, {key})
                if lv:
                    info["parameter"] += " -> " + "; ".join(dict.fromkeys(lv))

    # parameter naming -----------------------------------------------------------------------------
    def _param_name(self, dev, rel):
        """rel: list of tags from the device down to the parameter element."""
        tag = dev.tag
        if tag == "Eq8" and rel[0].startswith("Bands."):
            band = int(rel[0].split(".")[1]) + 1
            side = rel[1] if len(rel) > 2 else ""
            par = rel[-1]
            bandel = dev.find(f"{rel[0]}/{side}") if side else None
            mode = val(bandel, "Mode/Manual")
            mtxt = f" [{EQ8_MODES[int(mode)]}]" if mode is not None and mode.isdigit() and int(mode) < 8 else ""
            stxt = " (B: R/Side)" if side == "ParameterB" else ""
            return f"Band {band} {par}{stxt}{mtxt}"
        if tag in ("InstrumentGroupDevice", "AudioEffectGroupDevice", "MidiEffectGroupDevice",
                   "DrumGroupDevice") and rel[0].startswith("MacroControls."):
            i = int(rel[0].split(".")[1])
            nm = val(dev, f"MacroDisplayNames.{i}", f"Macro {i + 1}")
            return f"Macro {i + 1} '{nm}'"
        if tag == "MixerDevice":
            return {"Volume": "Chain Volume", "Panorama": "Chain Pan", "Speaker": "Chain Active",
                    "On": "Chain Mixer On"}.get(rel[-1], "Chain " + rel[-1])
        if tag == "Mixer":
            if rel[0] == "Sends" and len(rel) > 1:
                n = rel[1].split(".")[-1]
                return "Send " + (chr(ord("A") + int(n)) if n.isdigit() else rel[1])
            return {"Volume": "Track Volume", "Pan": "Track Pan", "Speaker": "Track Activator",
                    "CrossFadeState": "Crossfade"}.get(rel[-1], "Track " + rel[-1])
        if rel == ["On"]:
            return "Device On"
        parts = [OPERATOR_PARTS.get(r, r) if tag == "Operator" else r for r in rel]
        if tag == "AutoPan" and rel[0] == "Lfo":
            parts = ["LFO", AUTOPAN_PARAMS.get(rel[-1], rel[-1])]
        parts = [x for x in parts if x not in FILLER_TAGS]
        if len(parts) > 1 and parts[0] == parts[1]:
            parts = parts[1:]
        return " ".join(SIMPLE_PARAMS.get(x, x) for x in parts)

    def _register(self, at, path, pname, kind):
        if at is not None and at.get("Id"):
            self.targets[at.get("Id")] = dict(path=list(path), parameter=pname, kind=kind)

    def _params(self, dev, path, label, root, rack_stack):
        """Register every AutomationTarget/ModulationTarget under `root` (not entering Branches)."""
        dpath = path + [label]
        if dev.tag == "PluginDevice":
            pn = plugin_name(dev) or "Plug-in"
            on = dev.find("On")
            if on is not None:
                self._register(on.find("AutomationTarget"), dpath, "Device On", "auto")
            for p in dev.findall("ParameterList/*"):
                pv = p.find("ParameterValue")
                nm = val(p, "ParameterName") or f"param {val(p, 'ParameterId')}"
                pname = f"{pn}: {nm}"
                if pv is not None:
                    self._register(pv.find("AutomationTarget"), dpath, pname, "auto")
                    self._register(pv.find("ModulationTarget"), dpath, pname + " (mod)", "mod")
                    self._macro(pv, dpath, pname, rack_stack)
            return

        def walk(el, rel):
            for c in el:
                if c.tag in ("Branches", "ReturnBranches", "SpectrumAnalyzer"):
                    continue
                at, mt = c.find("AutomationTarget"), c.find("ModulationTarget")
                if at is not None or mt is not None:
                    pname = self._param_name(dev, rel + [c.tag])
                    self._register(at, dpath, pname, "auto")
                    self._register(mt, dpath, pname + " (mod)", "mod")
                    self._macro(c, dpath, pname, rack_stack, is_own_macro=(dev.tag in RACKS and c.tag.startswith("MacroControls.")))
                elif len(c):
                    walk(c, rel + [c.tag])
        walk(root, [])

    def _macro(self, par_el, dpath, pname, rack_stack, is_own_macro=False):
        km = par_el.find("KeyMidi")
        if km is None or val(km, "Channel") != "16" or val(km, "IsNote") == "true":
            return
        idx = int(val(km, "NoteOrController", "-1"))
        stack = rack_stack[:-1] if is_own_macro else rack_stack
        if not stack or idx < 0:
            return
        rack_path, rack = stack[-1]
        rng = par_el.find("MidiControllerRange")
        lo, hi = val(rng, "Min"), val(rng, "Max")
        mname = val(rack, f"MacroDisplayNames.{idx}", f"Macro {idx + 1}")
        self.macro_maps.append(dict(rack=" > ".join(rack_path), macro=f"Macro {idx + 1} '{mname}'",
                                    target=" > ".join(dpath), parameter=pname, min=lo, max=hi))

    # chain walk -----------------------------------------------------------------------------------
    def _chain(self, devs, path, depth, rack_stack):
        ind = "  " * depth
        labels = [device_label(d) for d in devs]
        for i, dev in enumerate(devs):
            label = labels[i]
            if labels.count(label) > 1:
                label = f"{label} #{labels[:i + 1].count(label)}"
            on = val(dev, "On/Manual", "true")
            off = "" if on == "true" else "  [OFF]"
            arrow = "->" if i else "  "
            if dev.tag in RACKS:
                branches = [b for b in (dev.find("Branches") or []) if b.tag in BRANCHES]
                self.flow.append(f"{ind}{arrow} {label}{off}  (splits into {len(branches)} parallel chains, summed)")
                rstack = rack_stack + [(path + [label], dev)]
                self._params(dev, path, label, dev, rstack)
                for bi, br in enumerate(branches):
                    bl = branch_label(br, bi)
                    bpath = path + [label, bl]
                    mix = br.find("MixerDevice")
                    vol = val(mix, "Volume/Manual")
                    spk = val(mix, "Speaker/Manual", "true")
                    voltxt = f"vol {lin_to_db(vol)}" if vol is not None else ""
                    self.flow.append(f"{ind}     || {bl}  [{voltxt}{'' if spk == 'true' else ', muted'}]")
                    if mix is not None:
                        self._params(mix, bpath, "Chain Mixer", mix, rstack)
                    self._chain(child_devices(br.find("DeviceChain")) or [], bpath, depth + 3, rstack)
            else:
                extra = device_summary(dev)
                self.flow.append(f"{ind}{arrow} {label}{off}{'  ' + extra if extra else ''}")
                self._params(dev, path, label, dev, rack_stack)


def lin_to_db(v):
    import math
    try:
        f = float(v)
    except (TypeError, ValueError):
        return "?"
    return "-inf dB" if f <= 0 else f"{20 * math.log10(f):+.1f} dB"


def device_summary(dev):
    """Short static description of the main settings (for the signal-flow print)."""
    t = dev.tag
    try:
        if t == "Eq8":
            out = []
            for b in range(8):
                pa = dev.find(f"Bands.{b}/ParameterA")
                if pa is None or val(pa, "IsOn/Manual") != "true":
                    continue
                m = int(val(pa, "Mode/Manual", "3"))
                g = float(val(pa, "Gain/Manual", "0"))
                if m in (2, 3, 4, 5) and abs(g) < 0.05 and m != 4:
                    continue  # flat bell / shelf: inactive in practice
                out.append(f"b{b + 1} {EQ8_MODES[m]} {float(val(pa, 'Freq/Manual')):.0f}Hz"
                           + (f" {g:+.1f}dB" if m in (2, 3, 5) else "")
                           + f" Q{float(val(pa, 'Q/Manual')):.2f}")
            mode = ["stereo", "L/R", "M/S"][int(val(dev, "Mode/Manual", "0"))]
            return f"[{mode}; " + ("; ".join(out) if out else "flat") + "]"
        if t == "Saturator":
            return (f"[type {val(dev, 'Type/Manual')}, drive {float(val(dev, 'PreDrive/Manual')):.1f}dB, "
                    f"out {float(val(dev, 'PostDrive/Manual')):.1f}dB, dry/wet {float(val(dev, 'DryWet/Manual')):.2f}]")
        if t == "AutoPan":
            rt = val(dev, "Lfo/RateType/Manual")
            rate = (f"sync idx {val(dev, 'Lfo/BeatRate/Manual')}" if rt == "1"
                    else f"{float(val(dev, 'Lfo/Frequency/Manual')):.2f} Hz")
            return (f"[LFO amount {float(val(dev, 'Lfo/LfoAmount/Manual')):.2f}, rate {rate}, "
                    f"shape {float(val(dev, 'Lfo/LfoShape/Manual')):.2f}, phase {float(val(dev, 'Lfo/Phase/Manual')):.0f}, "
                    f"type {val(dev, 'Lfo/Type/Manual')}]")
        if t == "StereoGain":
            return (f"[width {float(val(dev, 'StereoWidth/Manual', '1')):.2f}, mono {val(dev, 'Mono/Manual', val(dev, 'ChannelMode/Manual'))}, "
                    f"gain {lin_to_db(val(dev, 'Gain/Manual', '1'))}]")
        if t == "MultibandDynamics":
            return (f"[xover {float(val(dev, 'SplitLowMid/Manual', '0')):.0f}/{float(val(dev, 'SplitMidHigh/Manual', '0')):.0f}Hz, "
                    f"amount {float(val(dev, 'GlobalAmount/Manual', '0')):.2f}]")
        if t == "Compressor2":
            ratio = float(val(dev, 'Ratio/Manual'))
            return (f"[thr {lin_to_db(val(dev, 'Threshold/Manual'))}, ratio {'inf' if ratio > 1e6 else f'{ratio:.1f}'}, "
                    f"sidechain {val(dev, 'SideChain/OnOff/Manual', '?')}]")
        if t in ("OriginalSimpler", "MultiSampler"):
            names = [val(r, "FileRef/Name") for r in dev.iter("SampleRef")]
            names = [n for n in names if n]
            return f"[sample {names[0]!r}]" if names else ""
        if t == "Operator":
            lfo = dev.find("Lfo")
            return (f"[LFO on {val(lfo, 'LfoOn/Manual')}, rate {float(val(lfo, 'LfoRate/Manual')):.2f}, "
                    f"amount {float(val(lfo, 'LfoAmount/Manual')):.2f}; filter on {val(dev, 'Filter/OnOff/Manual')}]")
    except (TypeError, ValueError, IndexError):
        return ""
    return ""


def events(env):
    ev = env.find("Automation/Events")
    pts = []
    for e in (ev if ev is not None else []):
        t = e.get("Time")
        v = e.get("Value")
        if t is None or v is None:
            continue
        if e.tag == "BoolEvent":
            v = 1.0 if v == "true" else 0.0
        try:
            pts.append((float(t), float(v)))
        except ValueError:
            pass
    return pts


def unroll_clip(clip, pts):
    """Clip envelope points (clip time) -> arrangement beats over the clip's placement."""
    cs, ce = float(val(clip, "CurrentStart")), float(val(clip, "CurrentEnd"))
    ls, le = float(val(clip, "Loop/LoopStart")), float(val(clip, "Loop/LoopEnd"))
    loop = val(clip, "Loop/LoopOn") == "true"
    start = ls + float(val(clip, "Loop/StartRelative", "0")) if loop else ls
    init = [p for p in pts if p[0] <= DEFAULT_TIME + 1]
    body = sorted(p for p in pts if p[0] > DEFAULT_TIME + 1)
    if not body:
        return []
    def value_at(c):
        prev = init[-1][1] if init else body[0][1]
        for i, (t, v) in enumerate(body):
            if t > c:
                if i == 0:
                    return prev
                t0, v0 = body[i - 1]
                return v0 + (v - v0) * (c - t0) / (t - t0) if t > t0 else v
            prev = v
        return prev
    out = []
    # segments of the playback: [start, le) then repeated [ls, le) while looping
    pos, c0 = cs, start
    seg_end = le if loop else start + (ce - cs)
    while pos < ce - 1e-9:
        length = min(seg_end - c0, ce - pos)
        if length <= 0:
            break
        out.append((pos, value_at(c0)))
        out += [(pos + (t - c0), v) for t, v in body if c0 < t < c0 + length]
        out.append((pos + length, value_at(c0 + length)))
        pos += length
        if not loop:
            break
        c0, seg_end = ls, le
    return out


def lane_dict(als, track, info, pts, extra=None):
    v = [p[1] for p in pts]
    src = dict(als=als, track=track, path=" > ".join(info["path"]), parameter=info["parameter"])
    if extra:
        src.update(extra)
    full = " > ".join(info["path"] + [info["parameter"]])
    return dict(name=full, source=src, time_unit="beats",
                points=[[round(t, 6), round(x, 6)] for t, x in pts], min=min(v), max=max(v))


MOISTR_MAX_POINTS = 512  # (moistr's kMaxGesturePoints)


def value_at(pts, t, before=False):
    """A lane's value at beat t (pts sorted by time): straight between points; at a jump (points at the same
    time) the later value from t on, or with before=True the value just before t."""
    prev = None
    for i, (pt, pv) in enumerate(pts):
        if pt > t or (before and pt >= t):
            if prev is None:
                return pv
            t0, v0 = prev
            return v0 + (pv - v0) * (t - t0) / (pt - t0) if pt > t0 else pv
        prev = (pt, pv)
    return prev[1] if prev else 0.0


def moistr_gesture(lane, start, end):
    """The lane cut to start .. end as a moistr gesture (None when it does not move there)."""
    pts = sorted(((float(t), float(v)) for t, v in lane["points"]), key=lambda p: p[0])
    if not pts or end <= start:
        return None
    window = [(0.0, value_at(pts, start))]
    window += [(t - start, v) for t, v in pts if start < t < end]
    window.append((end - start, value_at(pts, end, before=True)))
    lo, hi = min(v for _, v in window), max(v for _, v in window)
    if hi - lo <= 1e-9 * max(1.0, abs(hi)):
        return None
    if len(window) > MOISTR_MAX_POINTS:
        print(f"  {lane['name']}: {len(window)} points in the window (moistr takes {MOISTR_MAX_POINTS}): left out",
              file=sys.stderr)
        return None
    return dict(name=lane["source"].get("parameter", lane["name"]), length_beats=round(end - start, 6),
                points=[[round(t, 6), round((v - lo) / (hi - lo), 6)] for t, v in window],
                source_min=lo, source_max=hi)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("als")
    ap.add_argument("--group", help="only tracks inside this group track (by name), group included")
    ap.add_argument("--out", help="directory for one JSON file per lane")
    ap.add_argument("--no-tree", action="store_true", help="do not print the device chains")
    ap.add_argument("--moistr", metavar="DIR", help="write each lane's window (--beats) as a moistr gesture into DIR")
    ap.add_argument("--beats", nargs=2, type=float, metavar=("START", "END"),
                    help="the window --moistr cuts, in beats (arrangement beats; clip-local for session clips)")
    a = ap.parse_args()
    if a.moistr and not a.beats:
        ap.error("--moistr needs --beats START END")
    with gzip.open(a.als) as f:
        root = ET.fromstring(f.read())
    tracks = root.find("LiveSet/Tracks")
    ids = {t.get("Id"): t for t in tracks}

    def parent_group(t):
        g = val(t, "TrackGroupId")
        return ids.get(g) if g and g != "-1" else None

    def group_path(t):
        p = []
        g = parent_group(t)
        while g is not None:
            p.insert(0, track_name(g))
            g = parent_group(g)
        return p

    def in_group(t):
        if a.group is None:
            return True
        return track_name(t) == a.group or a.group in group_path(t)

    als = os.path.basename(a.als)
    tempo = val(root, "LiveSet/MasterTrack/DeviceChain/Mixer/Tempo/Manual", "?")
    sel = [t for t in tracks if in_group(t)]
    if a.group and not sel:
        sys.exit(f"no group track named {a.group!r}")
    lanes, maps = [], []
    names = [track_name(t) for t in sel]
    for t in sel:
        tname = track_name(t)
        if names.count(tname) > 1:
            tname = f"{tname} [track id {t.get('Id')}]"
        idx = Index(t, tname)
        gp = group_path(t)
        if not a.no_tree:
            print(f"\n== {t.tag} '{tname}' (id {t.get('Id')}; group: {' > '.join(gp) or '-'})")
            print("\n".join(idx.flow) or "   (no devices)")
        maps += [dict(track=tname, **m) for m in idx.macro_maps]
        for env in t.findall("AutomationEnvelopes/Envelopes/AutomationEnvelope"):
            pid = val(env, "EnvelopeTarget/PointeeId")
            pts = [p for p in events(env) if p[0] > DEFAULT_TIME + 1]
            if not pts:
                continue
            info = idx.targets.get(pid, dict(path=[tname, "?"], parameter=f"target {pid}", kind="?"))
            lanes.append(lane_dict(als, tname, info, pts, dict(kind="arrangement")))
        # clip envelopes (automation and modulation stored inside clips)
        parent = {c: p for p in t.iter() for c in p}
        for clip in t.iter():
            if clip.tag not in ("MidiClip", "AudioClip"):
                continue
            ces = clip.find("Envelopes/Envelopes")
            if ces is None or not len(ces):
                continue
            arr = parent.get(clip) is not None and parent[clip].tag == "Events"
            cname = val(clip, "Name") or "(unnamed)"
            for ce in ces:
                pid = val(ce, "EnvelopeTarget/PointeeId")
                raw = events(ce)
                info = idx.targets.get(pid, dict(path=[tname, "?"], parameter=f"target {pid}", kind="?"))
                extra = dict(kind="clip envelope" + (" (arrangement)" if arr else " (session, clip-local beats)"),
                             clip=cname, clip_start=float(val(clip, "CurrentStart")),
                             clip_end=float(val(clip, "CurrentEnd")))
                pts = unroll_clip(clip, raw) if arr else [p for p in raw if p[0] > DEFAULT_TIME + 1]
                if pts:
                    lanes.append(lane_dict(als, tname, info, pts, extra))

    print(f"\ntempo {tempo} bpm; {len(sel)} tracks; {len(lanes)} lanes "
          f"({sum(1 for l in lanes if l['source']['kind'] == 'arrangement')} arrangement, "
          f"{sum(1 for l in lanes if l['source']['kind'] != 'arrangement')} clip envelopes)")
    for l in lanes:
        b = [p[0] for p in l["points"]]
        k = "" if l["source"]["kind"] == "arrangement" else " [clip]"
        print(f"{len(b):5d} pts | beats {min(b):7.2f}-{max(b):7.2f} | {l['min']:9.3f}..{l['max']:9.3f} | {l['name']}{k}")
    if maps:
        print("\nmacro mappings:")
        for m in maps:
            print(f"  {m['rack']} : {m['macro']} -> {m['target']} > {m['parameter']} ({m['min']}..{m['max']})")
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        for fn in os.listdir(a.out):
            if fn.endswith(".json") and re.match(r"^\d+_", fn):
                os.remove(os.path.join(a.out, fn))
        for i, l in enumerate(lanes):
            slug = re.sub(r"[^A-Za-z0-9]+", "_", l["name"]).strip("_")[:150]
            with open(os.path.join(a.out, f"{i:03d}_{slug}.json"), "w") as f:
                json.dump(l, f)
        with open(os.path.join(a.out, "macro_mappings.json"), "w") as f:
            json.dump(maps, f, indent=1)
    if a.moistr:
        start, end = a.beats
        os.makedirs(a.moistr, exist_ok=True)
        written = 0
        for i, l in enumerate(lanes):
            g = moistr_gesture(l, start, end)
            if g is None:
                continue
            slug = re.sub(r"[^A-Za-z0-9]+", "_", l["name"]).strip("_")[-80:]
            with open(os.path.join(a.moistr, f"{i:03d}_{slug}.json"), "w") as f:
                json.dump(g, f)
            written += 1
        print(f"\n{written} moistr gestures (beats {start:g} .. {end:g}) written to {a.moistr}")


if __name__ == "__main__":
    main()
