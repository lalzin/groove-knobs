#include "Ballroom.h"
#include <algorithm>

namespace ballroom
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    // A longer decay piles up more energy: the late brought down by part of
    // it (the full 1 / sqrt (decay) made the long tails start too quiet)
    double decayComp (double decay) { return std::pow (2.0 / std::clamp (decay, 0.2, 30.0), 0.4); } // (beyond 30 s the tail never piles up in musical time)
    float softLimit (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.8f)
            return x;
        return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
    }
} // namespace

void Ballroom::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = std::max (1, maxBlock);
    int n = 1;
    while (n < (int) std::ceil (maxPredelayMs * 0.001 * sr) + 8)
        n <<= 1;
    preL.assign ((size_t) n, 0.0f);
    preR.assign ((size_t) n, 0.0f);
    preMask = n - 1;
    for (auto* v : { &pdL, &pdR, &eL, &eR, &lateInL, &lateInR, &lL, &lR, &wetL, &wetR, &wet2L, &wet2R })
        v->assign ((size_t) maxBlockSize, 0.0f);
    for (auto& nw : net)
    {
        nw.early.prepare (sr);
        nw.late.prepare (sr);
        nw.configure (target.mode, target.earlySizeMs, target.lateSize);
    }
    const double a = std::exp (-1.0 / (0.03 * sr));
    for (auto* s : { &sMix, &sDepth, &sSend, &sHighCut, &sDecay })
        s->a = a;
    sPredelay.a = std::exp (-1.0 / (0.12 * sr));
    fadeStep = 1.0 / (fadeSeconds * sr);
    reset();
}

void Ballroom::reset()
{
    std::fill (preL.begin(), preL.end(), 0.0f);
    std::fill (preR.begin(), preR.end(), 0.0f);
    preW = 0;
    for (auto& nw : net)
    {
        nw.early.reset();
        nw.late.reset();
    }
    fade = 1.0;
    settleCount = 0;
    hcIc1[0] = hcIc1[1] = hcIc2[0] = hcIc2[1] = 0.0;
    for (auto* s : { &sMix, &sPredelay, &sDepth, &sSend, &sHighCut, &sDecay })
        s->init = false;
}

bool Ballroom::wants (const Network& nw) const
{
    return nw.mode == target.mode && std::abs (nw.earlySizeMs - target.earlySizeMs) < 0.25 && std::abs (nw.lateSize - target.lateSize) < 0.004;
}

void Ballroom::runNetwork (Network& nw, int n)
{
    EarlyParams ep;
    ep.cross = target.earlyCross;
    ep.modRateHz = target.earlyModRate;
    ep.modDepth = target.earlyModDepth;
    ep.diffusion = target.diffusion;
    nw.early.setParams (ep);
    LateParams lp;
    lp.decaySeconds = target.decaySeconds;
    lp.cross = target.lateCross;
    lp.modRateHz = target.lateModRate;
    lp.modDepth = target.lateModDepth;
    lp.bassMult = target.bassMult;
    lp.bassXoverHz = target.bassXoverHz;
    lp.highMult = target.highMult;
    lp.highXoverHz = target.highXoverHz;
    lp.diffusion = target.diffusion;
    lp.freeze = target.freeze;
    nw.late.setParams (lp);

    nw.early.process (pdL.data(), pdR.data(), eL.data(), eR.data(), n);
    // EARLY SEND: the late fed by the predelayed input (0), by the early (1), or a mix
    for (int i = 0; i < n; ++i)
    {
        const float s = nw.early.hasTaps() ? (float) sSend.v : 0.0f; // (no early, e.g. Plate: the late fed by the input)
        lateInL[(size_t) i] = (1.0f - s) * pdL[(size_t) i] + s * eL[(size_t) i];
        lateInR[(size_t) i] = (1.0f - s) * pdR[(size_t) i] + s * eR[(size_t) i];
    }
    nw.late.process (lateInL.data(), lateInR.data(), lL.data(), lR.data(), n);
}

void Ballroom::process (const float* inL, const float* inR, float* outL, float* outR, int total)
{
    for (int start = 0; start < total; start += maxBlockSize)
    {
        const int n = std::min (maxBlockSize, total - start);
        const float* xl = inL + start;
        const float* xr = inR + start;

        // A new mode / size: crossfade to the other network once the value
        // rests (a mode change: at once)
        if (fade >= 1.0 && ! wants (net[(size_t) active]))
        {
            settleCount += n;
            if (target.mode != net[(size_t) active].mode || settleCount >= (int) (settleSeconds * sr))
            {
                net[(size_t) (1 - active)].configure (target.mode, target.earlySizeMs, target.lateSize);
                fade = 0.0;
                settleCount = 0;
            }
        }
        else if (fade >= 1.0)
            settleCount = 0;

        // Predelay (cut when frozen: nothing new comes in)
        const double freezeIn = target.freeze ? 0.0 : 1.0;
        for (int i = 0; i < n; ++i)
        {
            preW = (preW + 1) & preMask;
            preL[(size_t) preW] = (float) (freezeIn * xl[i]);
            preR[(size_t) preW] = (float) (freezeIn * xr[i]);
            const double d = sPredelay.next (std::clamp (target.predelayMs, 0.0, maxPredelayMs)) * 0.001 * sr;
            const int k = (int) d;
            const float f = (float) (d - k);
            auto rd = [this, k, f] (const std::vector<float>& b)
            {
                const float a = b[(size_t) ((preW - k) & preMask)], c = b[(size_t) ((preW - k - 1) & preMask)];
                return a + f * (c - a);
            };
            pdL[(size_t) i] = rd (preL);
            pdR[(size_t) i] = rd (preR);
        }
        sSend.next (std::clamp (target.earlySend, 0.0, 1.0));

        // DEPTH
        const double lateComp = decayComp (sDecay.next (std::max (0.1, target.decaySeconds)));
        auto runInto = [&] (Network& nw, std::vector<float>& wl, std::vector<float>& wr)
        {
            runNetwork (nw, n);
            // each part as loud as the dry (the mode's trims): DEPTH, then MIX 100 %, keep the level
            const auto& shape = modeShape (nw.mode);
            const double te = std::pow (10.0, shape.earlyTrimDb / 20.0), tl = std::pow (10.0, shape.lateTrimDb / 20.0);
            for (int i = 0; i < n; ++i)
            {
                const double th = 0.5 * pi * sDepth.v;
                const double ge = te * std::cos (th), gl = tl * lateComp * std::sin (th);
                wl[(size_t) i] = (float) (ge * eL[(size_t) i] + gl * lL[(size_t) i]);
                wr[(size_t) i] = (float) (ge * eR[(size_t) i] + gl * lR[(size_t) i]);
            }
        };
        sDepth.next (std::clamp (target.depth, 0.0, 1.0));
        runInto (net[(size_t) active], wetL, wetR);
        if (fade < 1.0)
        {
            runInto (net[(size_t) (1 - active)], wet2L, wet2R);
            for (int i = 0; i < n; ++i)
            {
                fade = std::min (1.0, fade + fadeStep);
                const double go = std::cos (0.5 * pi * fade), gn = std::sin (0.5 * pi * fade);
                wetL[(size_t) i] = (float) (go * wetL[(size_t) i] + gn * wet2L[(size_t) i]);
                wetR[(size_t) i] = (float) (go * wetR[(size_t) i] + gn * wet2R[(size_t) i]);
            }
            if (fade >= 1.0)
                active = 1 - active;
        }

        // HIGH CUT: 12 dB / octave (state-variable low-pass), then the soft limit
        const double fc = std::exp (sHighCut.next (std::log (std::clamp (target.highCutHz, 500.0, 20000.0))));
        const double g = std::tan (pi * std::min (fc, 0.45 * sr) / sr), k = std::sqrt (2.0);
        const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        bool finite = true;
        for (int c = 0; c < 2; ++c)
        {
            auto& w = c == 0 ? wetL : wetR;
            for (int i = 0; i < n; ++i)
            {
                const double v3 = w[(size_t) i] - hcIc2[c];
                const double v1 = a1 * hcIc1[c] + a2 * v3;
                const double v2 = hcIc2[c] + a2 * hcIc1[c] + a3 * v3;
                hcIc1[c] = 2.0 * v1 - hcIc1[c];
                hcIc2[c] = 2.0 * v2 - hcIc2[c];
                w[(size_t) i] = softLimit ((float) v2);
                finite = finite && std::isfinite (w[(size_t) i]);
            }
        }
        if (! finite) // NaN / Inf: start clean, this block dry
        {
            reset();
            std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
            std::fill (wetR.begin(), wetR.begin() + n, 0.0f);
        }

        // MIX: constant power (MIX 0 = the input, exactly)
        for (int i = 0; i < n; ++i)
        {
            const double m = sMix.next (std::clamp (target.mix, 0.0, 1.0));
            const double dg = std::cos (0.5 * pi * m), wg = std::sin (0.5 * pi * m);
            outL[start + i] = (float) (dg * xl[i] + wg * wetL[(size_t) i]);
            outR[start + i] = (float) (dg * xr[i] + wg * wetR[(size_t) i]);
        }
    }
}
} // namespace ballroom
