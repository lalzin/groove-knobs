#pragma once
// BALLROOM — the whole reverb.
//
//   in ─> PREDELAY ─> Early ─┬───────────────────────────── early ─┐
//            │               └─ x EARLY SEND ─┐                    ├─ DEPTH (sin / cos) ─> HIGH CUT ─> soft limit ─> MIX (sin / cos) ─> out
//            └──────── x (1 - EARLY SEND) ────┴─> Late ──── late ─┘
//
// Two complete networks (early + late): MODE, EARLY SIZE or LATE SIZE moving
// crossfades to the other one, set for the new values, over 50 ms (no
// click, no pitch bend). DEPTH: the early and the late each made as loud
// as the dry by the mode's trims (and the late by part of its decay: a
// longer decay piles up more energy), so MIX 100 % keeps the level. FREEZE: the input cut, the late
// network holds. NaN / Inf: everything is cleared. MIX 0: the input exactly.

#include "EarlyNet.h"
#include "LateFdn.h"

namespace ballroom
{
struct Params
{
    // main
    double mix = 0.30;          // 0..1
    double predelayMs = 10.0;   // 0..500
    double decaySeconds = 2.0;  // 0.1..100
    double highCutHz = 8000.0;  // 500..20000
    double depth = 0.60;        // 0 = early only .. 1 = late only
    Mode mode = Mode::club;
    // early
    double earlySizeMs = 40.0;  // 2..200
    double earlyCross = 0.20;
    double earlyModRate = 0.4;
    double earlyModDepth = 0.15;
    double earlySend = 0.5;     // 0 = parallel .. 1 = the late fed by the early only
    double diffusion = 0.8;
    // late
    double lateSize = 0.6;
    double lateCross = 0.3;
    double lateModRate = 0.5;
    double lateModDepth = 0.25;
    double bassMult = 1.2;
    double bassXoverHz = 500.0;
    double highMult = 0.5;
    double highXoverHz = 5000.0;
    bool freeze = false;
};

class Ballroom
{
public:
    static constexpr double fadeSeconds = 0.050, settleSeconds = 0.060, maxPredelayMs = 500.0;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const Params& p) { target = p; }
    /** inL / inR (the same pointer for mono) -> outL / outR, any block size <= maxBlock. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    /** Tests: the active network's configuration. */
    Mode activeMode() const noexcept { return net[(size_t) active].mode; }
    bool isCrossfading() const noexcept { return fade < 1.0; }

private:
    struct Network
    {
        EarlyNet early;
        LateFdn late;
        Mode mode = Mode::club;
        double earlySizeMs = 40.0, lateSize = 0.6;
        void configure (Mode m, double eMs, double lSize)
        {
            mode = m;
            earlySizeMs = eMs;
            lateSize = lSize;
            early.configure (m, eMs);
            late.configure (m, lSize);
        }
    };
    struct Smooth
    {
        double a = 0.0, v = 0.0;
        bool init = false;
        double next (double t)
        {
            if (! init) { v = t; init = true; }
            v = t + (v - t) * a;
            return v;
        }
    };
    void runNetwork (Network& nw, int n);
    bool wants (const Network& nw) const;

    double sr = 48000.0;
    int maxBlockSize = 512;
    Params target;
    std::array<Network, 2> net;
    int active = 0;
    double fade = 1.0, fadeStep = 0.0;
    int settleCount = 0;
    // predelay
    std::vector<float> preL, preR;
    int preMask = 0, preW = 0;
    // scratch (sized in prepare: nothing allocated while processing)
    std::vector<float> pdL, pdR, eL, eR, lateInL, lateInR, lL, lR, wetL, wetR, wet2L, wet2R;
    double hcIc1[2] {}, hcIc2[2] {};
    Smooth sMix, sPredelay, sDepth, sSend, sHighCut, sDecay;
};
} // namespace ballroom
