#pragma once

namespace gk
{
/** Tempo-synced LFO helpers. The phase is always derived from a musical
    position (in quarter notes / beats), so it stays locked to the host grid
    while playing and keeps running from the same position when stopped. */
namespace lfo
{
    /** Phase in [0, 1) of an LFO whose period is `bars` bars of 4 beats. */
    inline double phaseFromBeats (double beats, double bars, double offsetCycles = 0.0) noexcept
    {
        return dspmath::frac (beats / (4.0 * juce::jmax (1.0e-3, bars)) + offsetCycles);
    }

    /** Raised-cosine shape: 0 at phase 0, 1 at phase 0.5. Smooth at the wrap. */
    inline float raisedCosine (double phase) noexcept
    {
        return (float) (0.5 - 0.5 * std::cos (2.0 * dspmath::pi * phase));
    }
} // namespace lfo
} // namespace gk
