#pragma once

#include "Params.h"

namespace orbitr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kOrbs: return "How many moving sources play the sound.";
        case kPattern: return "Orbit: the orbs circle round the centre. Swarm: each wanders on its own smooth path.";
        case kSpeed: return "How fast the orbs move (m/s): the faster, the bigger the Doppler pitch shifts.";
        case kDistance: return "How far from you the swarm's centre is (m). Drag the ball in the display to move it.";
        case kAngle:
            return "Where the swarm is round you: 0 straight ahead, negative to the left, positive to the right, 180 behind. "
                   "Drag the ball (or yourself) in the display to move it.";
        case kRadius: return "How far from the centre the orbs move (m).";
        case kSpread: return "How wide the orbs spread across the stereo field.";
        case kRandom: return "How much the orbs differ: their speeds, sizes and tilts (orbit), or their paths' rates (swarm).";
        case kFloor: return "Adds each orb's reflection off the floor.";
        case kMix: return "The swarm against the input (50 % in the Liquid Debris-like default).";
        case kDryWet: return "Blend of the whole effect and the untouched (time-aligned) signal.";
        case kOutput: return "Overall output level.";
        case kGrains:
            return "Each orb plays a stream of short grains cut from the last second of the input instead of the input itself, "
                   "still moving (Doppler, pan and distance as before).";
        case kGrainSize: return "How long each grain is (ms).";
        case kGrainDensity:
            return "How many of an orb's grains overlap: 2 gives a smooth cloud, below 1 the grains leave gaps, above 2 they "
                   "pile up.";
        case kGrainScatter:
            return "How far each grain jumps about in the last half second of the input (and a little in time). 0: each orb "
                   "plays its own slice.";
        case kGrainPitch: return "Transposes the grains (semitones), before the orbs' own Doppler shifts.";
        default: return nullptr;
    }
}

constexpr const char* kOrbView =
    "The orbs seen from above: you facing up, the swarm's ball at its Distance and Angle, rings every metre (every 5 m "
    "when far). Drag the ball, or yourself, to move the swarm round you (Shift: fine); double-click to bring it back "
    "ahead at 3 m; the mouse wheel over it sets the Distance. With Grains on, each orb swells with its grains.";

} // namespace orbitr::help
