// Detonatr's Transient stage: keeps an extremely short spike at the start of each hit at full level and
// turns everything after it down a lot (Drop), so the Saturator after it can raise the level back up
// dense and loud while the spike still cuts through. Onsets are found with a short look-ahead so the
// spike starts exactly at the hit.
#pragma once

#include <cstdint>
#include <vector>

namespace detonatr {

class Transient
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setSpike (double ms);       // how long the spike stays at full level after an onset (0.1 .. 20 ms)
    void setDrop (double db);        // how far the rest of the hit is turned down (0 .. 48 dB)
    void setFall (double ms);        // how long the level takes to go from the spike down to the drop (0.1 .. 50 ms)
    void setSensitivity (double db); // how far the level must jump for an onset (3 .. 24 dB)
    int latency () const;            // samples (the look-ahead); constant for a sample rate
    void process (float* L, float* R, int n); // in place, stereo

private:
    int64_t findOnset (int64_t t) const;
    float gainAt (int64_t t);

    double sr = 48000.0;
    int look = 240;     // look-ahead = latency (samples)
    int rampUp = 24;    // samples the gain takes to come back up to 0 dB before an onset
    int holdOff = 1920; // samples after an onset before another can trigger
    int preRoll = 10;   // samples the spike starts ahead of the onset found
    int size = 0;       // ring buffers' length (a power of two, more than the look-ahead)
    std::vector<float> bufL, bufR, hist, slowHist; // delayed audio, and the fast and slow levels' recent history
    int64_t now = 0;    // samples processed since reset
    bool fresh = true;  // nothing processed since reset: settings apply at once, not smoothed

    // settings
    double spikeMs = 3.0, fallMs = 5.0, dropDb = 24.0, sensDb = 9.0;
    double dropNow = 24.0; // Drop smoothed (dB), so turning the knob doesn't step the gain
    double dropGainDb = 0.0; float dropGain = 1.0f; // the gain dropNow was last turned into
    int spikeN = 144, fallN = 240; // Spike and Fall in samples (set at the start of each block)
    int hitSpikeN = 144, hitFallN = 240; // the ones the current hit started with

    // onset detector
    double fast = 0.0, slow = 0.0;
    int holdN = 720, holdLeft = 0; // the fast level's peak hold (samples)
    double fastRel = 0.0, slowAtk = 0.0, slowRel = 0.0, dropSmooth = 0.0;
    bool armed = true;
    int64_t lastTrigger = -1000000000;

    // gain curve (for the delayed output)
    int64_t pendingAt = -1; // output sample where the next spike starts (-1: none pending)
    int64_t spikeAt = -1000000000; // output sample where the last spike started
    float rampFrom = 1.0f;  // gain when the ramp up began
    float lastGain = 1.0f;
};

} // namespace detonatr
