#pragma once

namespace gk
{
/** Linear ramp smoother.

    Every call to setTarget() restarts a ramp of fixed duration towards the new
    target. The ramp lands exactly on the target (no floating point residue),
    which is what makes the "bit-exact at 0 %" guarantee possible.
    Deterministic and sample accurate: the output does not depend on how the
    host splits the stream into blocks. */
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = juce::jmax (1, (int) std::round (sampleRate * rampSeconds));
        snap (target);
    }

    void setTarget (float newTarget) noexcept
    {
        if (juce::exactlyEqual (newTarget, target))
            return;
        target = newTarget;
        remaining = rampLength;
        step = (target - current) / (float) rampLength;
    }

    void snap (float value) noexcept
    {
        current = target = value;
        remaining = 0;
        step = 0.0f;
    }

    float next() noexcept
    {
        if (remaining > 0)
        {
            if (--remaining == 0)
                current = target;
            else
                current += step;
        }
        return current;
    }

    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return remaining > 0; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0, rampLength = 1;
};

/** One-pole (exponential) smoother with snapping.

    Used for parameters derived from the macro: it follows a target that moves
    continuously (the macro itself is already ramped) and absorbs the sudden
    jumps caused by a preset change. "Settle time" is the time to reach ~95 %
    of a step (3 time constants). Snaps exactly onto the target once the
    remaining distance is negligible, so a settled smoother is bit-stable. */
class OnePoleSmoother
{
public:
    /** @param updateRate  how many times per second next() is called. */
    void prepare (double updateRate, double settleSeconds) noexcept
    {
        const double tau = settleSeconds / 3.0;
        coeff = (float) std::exp (-1.0 / (tau * updateRate));
    }

    void snap (float value) noexcept
    {
        current = target = value;
        initialised = true;
    }

    void setTarget (float newTarget) noexcept
    {
        target = newTarget;
        if (! initialised)
            snap (newTarget);
    }

    float next() noexcept
    {
        const float diff = target - current;
        if (std::abs (diff) <= snapEpsilon * (1.0f + std::abs (target)))
            current = target;
        else
            current = target - diff * coeff;
        return current;
    }

    /** Forget the current value: the next setTarget() snaps. */
    void invalidate() noexcept { initialised = false; }

    float getCurrent() const noexcept { return current; }
    bool isSettled() const noexcept { return juce::exactlyEqual (current, target); }

private:
    float current = 0.0f, target = 0.0f, coeff = 0.0f;
    bool initialised = false;
    static constexpr float snapEpsilon = 1.0e-6f;
};
/** Critically damped two-pole smoother (two cascaded one-poles).

    Same role as OnePoleSmoother, but its trajectory is continuous in slope
    too: a one-pole starts moving at full speed the instant its target jumps,
    which leaves a tiny kink (a broadband "tick" in the 2nd derivative) when it
    drives a gain or a filter coefficient. Settle time = ~95 % of a step. */
class SmoothParam
{
public:
    void prepare (double updateRate, double settleSeconds) noexcept
    {
        const double tau = settleSeconds / 4.74; // 2-pole cascade: 95 % at ~4.74 tau
        coeff = (float) std::exp (-1.0 / (tau * updateRate));
    }

    void snap (float value) noexcept
    {
        stage1 = current = target = value;
        initialised = true;
    }

    void setTarget (float newTarget) noexcept
    {
        target = newTarget;
        if (! initialised)
            snap (newTarget);
    }

    float next() noexcept
    {
        const float eps = snapEpsilon * (1.0f + std::abs (target));
        if (std::abs (target - stage1) <= eps && std::abs (target - current) <= eps)
        {
            stage1 = current = target;
            return current;
        }
        stage1 = target - (target - stage1) * coeff;
        current = stage1 - (stage1 - current) * coeff;
        return current;
    }

    void invalidate() noexcept { initialised = false; }
    float getCurrent() const noexcept { return current; }
    bool isSettled() const noexcept { return juce::exactlyEqual (current, target); }

private:
    float stage1 = 0.0f, current = 0.0f, target = 0.0f, coeff = 0.0f;
    bool initialised = false;
    static constexpr float snapEpsilon = 1.0e-6f;
};

/** Per-sample linear interpolation between control-rate values, so
    coefficients computed every N samples never change in steps (zipper). */
class ControlRamp
{
public:
    /** Start a new segment of `steps` samples towards `target`. */
    void retarget (double target, int steps) noexcept
    {
        if (! initialised)
        {
            value = target;
            inc = 0.0;
            initialised = true;
            return;
        }
        inc = (target - value) / (double) steps;
    }

    double next() noexcept { return value += inc; }
    double get() const noexcept { return value; }
    void invalidate() noexcept { initialised = false; }

private:
    double value = 0.0, inc = 0.0;
    bool initialised = false;
};
} // namespace gk
