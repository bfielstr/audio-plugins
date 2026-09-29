// Detonatr's Tone stage: makes the sound tonal. The input's band envelopes play
//   - a bank of tuned resonators ("household items": material presets with their modal ratios and
//     decays), tuned to a root note, and
//   - up to kCarrierSlots recordings (the user's household items), vocoded by the input,
// and a disperser (a chain of all-pass filters) smears the phase into the chirpy, disperser-type sound.
#pragma once

#include <memory>
#include <vector>

namespace detonatr {

constexpr int kCarrierSlots = 4;
// The resonators' materials. Ratios are of the modal frequencies to Root; every mode rings for Decay
// times its own factor (the lowest mode rings longest).
//   kGlass:    a wine glass's bending modes (1, 2.83, 5.42, 8.77, 12.9, 17.7), the low ones as close
//              pairs that beat slowly; bright and long.
//   kMetalPot: a pot's shell modes (1, 1.52, 2.03/2.11, 3.26/3.41, 5.17/5.34, 8.33, 11.9/12.4, 17.1):
//              close pairs that clang, a medium ring.
//   kPipe:     an air column: near-harmonic (1, 2.004, 3.012, ... 10.2), slightly stretched.
//   kWood:     a block: few modes (1, 1.58, 2.76, 3.9, 5.4, 7.1), short and dull.
//   kBell:     hum 1, prime 1.94, tierce 2.37 (the minor third), quint 3.03, nominal 4.0, then 5.24,
//              5.41, 6.72, 8.15, 10.9; the hum rings longest.
//   kBottle:   a strong, pure Helmholtz note (1) and the glass walls' clink high above (6.8/7.02, 11.3,
//              16.2/16.5, 21.5).
enum Material { kGlass = 0, kMetalPot, kPipe, kWood, kBell, kBottle, kNumMaterials };

// A recording used as a vocoder carrier. Immutable once made (shared with the audio thread).
struct Carrier
{
    std::vector<float> ch[2]; // left, right (right may equal left)
    int frames = 0;
    double sampleRate = 48000.0;
};

class Tone
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setRoot (double hz);              // the resonators' fundamental (30 .. 400 Hz)
    void setMaterial (int material);       // a Material
    void setDecay (double seconds);        // how long the resonators ring (0.05 .. 4 s)
    void setResonators (double level);     // 0 .. 1: how loud the resonators are
    void setCarriers (double level);       // 0 .. 1: how loud the vocoded recordings are
    void setCarrierLevel (int slot, double level); // 0 .. 1 per recording
    void setDry (double level);            // 0 .. 1: how much of the input passes as it is
    void setDisperse (double amount);      // 0 .. 1: how much all-pass dispersion (0: none)
    void setDisperseFreq (double hz);      // where the dispersion is centred (50 .. 5000 Hz)
    // Audio thread, at the start of a block: the recording in a slot (null: empty). The pointer stays
    // valid until it is replaced; a new pointer restarts that slot.
    void setCarrier (int slot, const Carrier* c);
    int latency () const { return 0; }
    void process (float* L, float* R, int n); // in place, stereo

private:
    static constexpr int kMaxModes = 12;  // resonator modes per channel
    static constexpr int kBands = 24;     // vocoder bands, log-spaced 40 Hz .. 12 kHz
    static constexpr int kMaxStages = 32; // disperser all-pass stages at Disperse 1
    static constexpr int kSub = 32;       // coefficients move once per this many samples

    // One band-pass bank's state (two cascaded biquads per band) and its band envelopes.
    struct Bank
    {
        float s1[kBands], s2[kBands], t1[kBands], t2[kBands], env[kBands];
        void clear ();
    };
    struct Slot
    {
        const Carrier* carrier = nullptr;
        double pos = 0.0;      // read position in the carrier's frames
        bool running = false;  // its bank is being processed
        float fade = 0.0f;     // 0 .. 1: ramps in when it starts, out when the carrier is removed
        float level = 1.0f, levelNow = 1.0f;
        float floorLevel[2] = {};  // per carrier channel: below this a band is not raised
        Bank bank[2];
    };

    void processSub (float* L, float* R, int n);
    void updateSlow (bool jump);
    void updateModes ();
    void runResonators (const float* L, const float* R, float* outL, float* outR, int n);
    bool runVocoder (const float* L, const float* R, float* outL, float* outR, int n); // false: silent
    void runDisperser (float* L, float* R, int n);

    double sr = 48000.0;
    int maxBlock = 512;
    bool snap = true; // the first block after prepare / reset jumps to the settings

    // settings
    double rootHz = 82.4, decaySec = 0.8, dispAmount = 0.3, dispHz = 400.0;
    int material = kMetalPot;
    float resLevel = 0.5f, carLevel = 0.5f, dryLevel = 0.7f;

    // smoothed settings
    double rootNow = 82.4, decayNow = 0.8, dispHzNow = 400.0, dispQNow = 1.0;
    float resNow = 0.5f, carNow = 0.5f, dryNow = 0.7f;
    double slowCoef = 0.1;   // per sub-block, for root, decay and the disperser's frequency and Q
    float levelCoef = 0.01f; // per sample, for the levels

    // resonators (complex one-pole modes, double: long decays at high rates need the precision)
    int modeCount = 0, modesMaterial = -1;
    double modesRoot = 0.0, modesDecay = 0.0;
    double poleRe[kMaxModes] = {}, poleIm[kMaxModes] = {}, modeGain[kMaxModes] = {};
    double zRe[2][kMaxModes] = {}, zIm[2][kMaxModes] = {};
    double prevX1[2] = {}, prevX2[2] = {};
    float peakIn = 0.0f, peakRes = 0.0f, ceilGain = 1.0f, peakFall = 0.999f, ceilRise = 0.001f;
    bool resRunning = false;

    // vocoder
    int bandCount = kBands;
    float bandB0[kBands] = {}, bandA1[kBands] = {}, bandA2[kBands] = {};
    float envAtk[kBands] = {}, envRel[kBands] = {}, carRel[kBands] = {};
    float fadeStep = 0.001f;
    Bank inBank[2];
    bool inRunning = false;
    Slot slots[kCarrierSlots];

    // disperser: identical all-pass stages (double: they sit low at high rates)
    double apA1 = 0.0, apA2 = 0.0;
    double apS1[2][kMaxStages] = {}, apS2[2][kMaxStages] = {};
    int apFrom = 0, apTo = 0, apFadePos = 0, apFadeLen = 480;
};

} // namespace detonatr
