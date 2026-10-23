// SLAPPER — one voice: one string, its modes, its excitation, its sub.
// (prototype: prototypes/slapper, the "v2" validated by ear on 2026-10-22)
//
// The string is a sum of its modes: one damped resonator per harmonic, each
// with its own decay (the highs die in tenths of a second, the fundamental
// rings for seconds), slightly inharmonic (a stiff string), nothing above
// 6 kHz (nothing aliases). Excited by a mix of three attacks (ATTAQUE):
// DOIGT (a pluck, rounded by the fingertip), SLAP (a thumb strike, then its
// rebound) and POP (a pull and a snap). The string bounces off the frets
// under it: the slap's clank and the pop's snap are that collision. Read by
// a magnetic pickup (velocity, its window, the coil's resonance). A sine
// locked on the fundamental's phase rounds the low end (the sub); a second,
// sympathetic string 0.2 % off rings with it (RÉSONANCE).
#pragma once
#include <array>
#include <cstdint>

namespace slapper
{
/** What a note is played with (sampled at its start). */
struct NoteParams
{
    float attack = 0.5f;  // ATTAQUE: 0 DOIGT, 0.5 SLAP, 1 POP (blended in between)
    float pick = 0.15f;   // PINCEMENT: the finger's point, from the bridge (0.05..0.5)
    float string = 0.4f;  // CORDE: stiffness (inharmonicity, how long the highs last)
    float mute = 0.0f;    // ÉTOUFFER: the palm on the strings
    float body = 0.3f;    // RÉSONANCE: the sympathetic string
    // from FUNK (FunkMap.h)
    float hardness = 0.5f; // thumb / finger hardness: shorter contact, brighter
    float clank = 0.5f;    // lower action: the string reaches the frets sooner
    float rebound = 0.2f;  // the thumb's second strike on the slap
    float sub = 0.5f;      // the sub sine's level
};

class Voice
{
public:
    static constexpr int maxModes = 320;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** A plucked / struck note. Velocity < ghostVelocity: a ghost note (a
        dead, muted thump with no clear pitch). The ringing note, if any, is
        damped in ~15 ms by the fretting hand (no click). */
    void noteOn (double hz, float velocity, const NoteParams& p) noexcept;
    /** Legato: the finger slides to `hz` (no new pluck) in `seconds`. */
    void glideTo (double hz, double seconds) noexcept;
    /** The fretting finger lifts: the string is damped (~0.1 s). */
    void noteOff (double seconds = 0.10) noexcept; // RELEASE: the damping's time
    /** Pitch bend, as a frequency ratio (1 = none). */
    void setBend (double ratio) noexcept { bend = ratio; }
    /** FUNK moved while the note rings: its sub level and the action (how soon
        the string meets the frets) follow at once; the thumb's hardness and
        rebound belong to the attack already played. */
    void setFunkLive (float sub, float clank) noexcept
    {
        subLevel = sub < 0.0f ? 0.0f : sub > 2.0f ? 2.0f : sub; // (SUB can double it)
        fretGap = 2.2 - 1.2 * (clank < 0.0f ? 0.0f : clank > 1.0f ? 1.0f : clank);
    }

    float process() noexcept;

    bool isActive() const noexcept { return bank[0].alive || bank[1].alive; }
    bool isHeld() const noexcept { return held; }
    double currentHz() const noexcept { return targetHz; }

    static constexpr float ghostVelocity = 0.15f;

    // diagnosis
    int contactSamples = 0;

private:
    struct Bank
    {
        int K = 0;
        // the modes, as rotating phasors z = u + j q (q the modal displacement, u its velocity / omega)
        std::array<float, maxModes> re {}, im {}, pr {}, pi {}, out {}, fret {}, fretInvW {};
        std::array<float, maxModes> omega {}, radius {}; // (glides: the poles are rebuilt from these)
        std::array<float, maxModes> rebRe {}, rebIm {};  // the slap's rebound: a second strike, added later
        double hz = 55.0, ratio = 1.0;  // the note it was set up for, its current pitch ratio (glide, bend)
        double reach = 0.0;             // how far one sample of fret force moves the contact point
        float subEnv = 0.0f, subTarget = 0.0f, subDecay = 1.0f;
        int reboundIn = -1;
        bool alive = false, contact = false;
    };
    void setup (Bank& b, double hz, float vel, const NoteParams& p) noexcept;
    void damp (Bank& b, double t60) noexcept;
    void retune (Bank& b, double ratio) noexcept;
    float run (Bank& b) noexcept;
    float subOf (Bank& b) noexcept;

    double sr = 48000.0;
    Bank bank[2];
    int cur = 0;
    bool held = false;
    double targetHz = 55.0, fretGap = 1.5;
    // glide: log-ratio from -> to over its time; poles rebuilt every 16 samples
    double glideFrom = 0.0, glideTo_ = 0.0, glideStep = 0.0, glidePos = 1.0, bend = 1.0, appliedRatio = 1.0;
    int retuneCountdown = 0;
    float subAttack = 0.0f, subLevel = 0.5f;
    // ghost note: a short, dull burst (the muted string's thump) on top
    float ghostEnv = 0.0f, ghostDecay = 0.0f, ghostLp = 0.0f, ghostBp = 0.0f;
    std::uint32_t seed = 0x2545f491u;
    float noise() noexcept
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) / 8388608.0f - 1.0f;
    }
    // the sympathetic string: its first 8 modes, 0.2 % sharp, driven by this one's sound
    static constexpr int sympModes = 8;
    std::array<float, sympModes> sRe {}, sIm {}, sPr {}, sPi {}, sIn {};
    float sympGain = 0.0f;
    // the coil and its cable (resonant low-pass), the low cut under it all
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float run (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void clear() noexcept { x1 = x2 = y1 = y2 = 0; }
    } coil, lowCut;
};
} // namespace slapper
