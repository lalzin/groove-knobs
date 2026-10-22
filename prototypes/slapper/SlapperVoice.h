// SLAPPER — step 1 prototype: one voice, no UI, no JUCE.
// A plucked / struck string as a bidirectional digital waveguide, excited by
// a nonlinear contact (SLAP, DOIGT) or a pull-and-release (POP).
#pragma once
#include <cstdint>
#include <vector>

namespace slapper
{
enum class Attack { slap, pop, finger };

/** One string, one voice. Sample rate fixed at prepare(). */
class Voice
{
public:
    void prepare (double sampleRate);
    /** Starts a note (the string keeps vibrating if it already does: a new
        excitation on top, as on a real string). */
    void noteOn (double hz, float velocity, Attack attack);
    float process() noexcept; // one sample of the pickup (velocity, like a magnetic pickup)

    // the string (step 1: fixed by hand; later FUNK / the advanced knobs)
    float pickPosition = 0.15f; // PINCEMENT: the excitation point, from the bridge (0.05..0.5)
    float stringStiffness = 0.4f; // CORDE: tension and stiffness (0..1)
    float mute = 0.0f; // ÉTOUFFER: damping in the loop (0..1)

    /** The loop's period at the note, for the tuning test. */
    double loopDelay() const noexcept { return loopTotal; }
    int contactSamples = 0; // (diagnosis: how long the thumb / finger touched the string)
    double peakForce = 0.0;

private:
    struct Line
    {
        std::vector<float> buf;
        int mask = 0, w = 0;
        void init (int size);
        void push (float x) noexcept { w = (w + 1) & mask; buf[(size_t) w] = x; }
        float at (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }
        void add (int d, float x) noexcept { buf[(size_t) ((w - d) & mask)] += x; }
        void set (int d, float x) noexcept { buf[(size_t) ((w - d) & mask)] = x; }
    };
    void tune (double hz);
    void displace (float A, int width);
    float displacementAt (int fromNut) const noexcept { return right.at (fromNut) + left.at (M - 1 - fromNut); }
    void inject (int fromNut, float dy) noexcept
    {
        right.add (fromNut, 0.5f * dy);
        left.add (M - 1 - fromNut, 0.5f * dy);
    }

    double sr = 48000.0, f0 = 55.0, loopTotal = 0.0;
    Line right, left; // right: nut -> bridge (index = distance from the nut); left: bridge -> nut
    int M = 2, excitePoint = 1, pickupPoint = 1;
    // the bridge's reflection: -g * lowpass (loss) * allpass (stiffness) * Thiran (tuning)
    float g = 0.99f, lpA = 0.2f, lpZ = 0.0f, dispC = 0.0f, dispX = 0.0f, dispY = 0.0f, thA = 0.0f, thX = 0.0f, thY = 0.0f;
    // the hammer (thumb, finger): a mass on a nonlinear spring, F = k d^p
    struct Hammer
    {
        bool active = false;
        double x = 0.0, v = 0.0, m = 1.0, k = 1.0, p = 2.0;
        int age = 0, life = 0;
    } hammer, rebound;
    int reboundDelay = -1;
    float attackGain = 1.0f; // (step 1: the three attacks near the same level; the amp comes in step 2)
    // the slap's "clac" (thumb on the frets) and the pop's snap: a short, filtered burst
    int clickLeft = 0;
    float clickGain = 0.0f, clickHp = 0.0f, clickPrev = 0.0f;
    std::uint32_t seed = 0x9e3779b9u;
    float lastPick = 0.0f, dcX = 0.0f, dcY = 0.0f;
    // the pickup (a passive coil and its cable): a resonant low-pass ~3 kHz
    float pb0 = 0, pb1 = 0, pb2 = 0, pa1 = 0, pa2 = 0, px1 = 0, px2 = 0, py1 = 0, py2 = 0;
    float noise() noexcept
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) / 8388608.0f - 1.0f;
    }
    void stepHammer (Hammer& h) noexcept;
};
} // namespace slapper
