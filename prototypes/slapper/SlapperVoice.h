// SLAPPER — one voice, no UI, no JUCE.
// The string as a sum of its modes (one damped resonator per harmonic, each
// with its own decay: the highs die in a fraction of a second, the
// fundamental rings for seconds — what a real bass does, and nothing above
// ~6 kHz exists, so nothing aliases). Excited by a thumb strike (SLAP), a
// pull-and-release (POP) or a finger pluck (DOIGT); the string bounces off
// the frets under it (the slap's clank, the pop's snap: a real collision, not
// noise). Read by a magnetic pickup (the string's velocity at the pickup, its
// coil's resonance). A sub oscillator locked on the fundamental rounds the
// low end, under a low cut.
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
    /** Starts a note. The string that was ringing is damped in a few ms (the
        fretting hand), not cut. */
    void noteOn (double hz, float velocity, Attack attack);
    float process() noexcept;

    float pickPosition = 0.15f;  // PINCEMENT: the finger's point, from the bridge (0.05..0.5)
    float stringStiffness = 0.4f; // CORDE: stiffness (inharmonicity) and brightness (0..1)
    float mute = 0.0f;            // ÉTOUFFER: the palm on the strings (0..1)
    float subLevel = 0.5f;        // SUB: the sine locked on the fundamental (0..1)
    float lowCutHz = 32.0f;       // the low cut under it all

    int contactSamples = 0; // (diagnosis: samples the string spent against the frets)
    double peakForce = 0.0;

private:
    // one mode bank: the string's modes, as rotating phasors z = u + j q
    // (q the modal displacement, u its velocity / omega)
    struct Bank
    {
        int K = 0;
        std::vector<float> re, im, pr, pi, out, fret, fretInvW;
        float fundW = 0.0f;
        // the sub: a sine on this string's fundamental (its phase), its envelope
        float subEnv = 0.0f, subTarget = 0.0f, subDecay = 1.0f;
        double reach = 0.0; // how far one sample of contact force moves the struck point (per unit force)
        bool alive = false;
        void resize (int n);
    };
    void setup (Bank& b, double hz, Attack attack, float vel);
    void damp (Bank& b, double t60);
    float run (Bank& b, bool contact) noexcept;

    double sr = 48000.0;
    Bank bank[2];
    int cur = 0;
    // the frets under the string where it is struck: the string can't go past them
    double fretGap = 1.0, fretK = 0.0;
    bool contactOn = false;
    float subAttack = 0.0f;
    float sub (Bank& b) noexcept;
    // the pickup's coil + cable (resonant low-pass), the low cut (2nd order high-pass)
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float run (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    } coil, lowCut;
    float designedLowCut = -1.0f;
};
} // namespace slapper
