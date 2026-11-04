#pragma once
// HEAD SPIN — where on the path the sound is: the phase, in turns.
//
// A beat clock B drives everything:
//   - SYNC with the transport playing: B is the host's position (PPQ), the
//     turn lasts the division's length in beats, the phase is locked to the
//     bar (it starts on the start point at PPQ 0, and keeps it for 64 bars);
//   - otherwise B runs freely at the tempo (SYNC, transport stopped) or, in
//     Hz mode, at 4 "beats" per turn.
// phase = offset + direction * groove(B) / beatsPerTurn
// GROOVE bends B into a softened staircase: the sound stops on every beat,
// then moves on. A direction change rewrites the offset so the position does
// not move (only the way it goes flips); a mode change too. A RETRIGGER (a
// MIDI note, or a transient in the input) puts the sound back on its start
// point; that, and the transport starting, are jumps: Motion reports them,
// the renderer glides there in a few milliseconds (no click).

#include <algorithm>
#include <cmath>

namespace headspin
{
struct Transport
{
    double bpm = 120.0;
    bool playing = false;
    double ppq = 0.0; // at the first sample of the block
};

/** RATE SYNC divisions: 8 bars .. 1/32, each straight, dotted, triplet (4/4). */
namespace division
{
    constexpr int bases = 9, count = bases * 3;
    inline double beats (int index)
    {
        static const double base[bases] { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 }; // 8 bars .. 1/32
        index = std::clamp (index, 0, count - 1);
        const double b = base[index / 3];
        const int kind = index % 3;
        return kind == 0 ? b : kind == 1 ? b * 1.5 : b * 2.0 / 3.0;
    }
    inline const char* name (int index)
    {
        static const char* names[count] { "8 bars", "8 bars D", "8 bars T", "4 bars", "4 bars D", "4 bars T", "2 bars", "2 bars D", "2 bars T",
                                          "1 bar",  "1 bar D",  "1 bar T",  "1/2",    "1/2 D",    "1/2 T",    "1/4",    "1/4 D",    "1/4 T",
                                          "1/8",    "1/8 D",    "1/8 T",    "1/16",   "1/16 D",   "1/16 T",   "1/32",   "1/32 D",   "1/32 T" };
        return names[std::clamp (index, 0, count - 1)];
    }
    constexpr int oneBar = 9;
} // namespace division

class Motion
{
public:
    enum class Retrigger { off, midi, transient };

    struct Settings
    {
        bool sync = true;
        int division = division::oneBar;
        double rateHz = 0.5;
        bool leftToRight = true;
        double startTurns = 0.0; // DÉPART / 360
        double groove = 0.0;
        Retrigger retrigger = Retrigger::off;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        fastAttack = 1.0 - std::exp (-1.0 / (0.001 * sr));
        fastRelease = 1.0 - std::exp (-1.0 / (0.030 * sr));
        slowCoeff = 1.0 - std::exp (-1.0 / (0.200 * sr));
        reset();
    }

    void reset()
    {
        beats = 0.0;
        offset = 0.0;
        initialised = false;
        wasLocked = false;
        envFast = envSlow = 0.0;
        holdoff = 0;
    }

    /** Call once per block, before the samples. */
    void beginBlock (const Settings& s, const Transport& t)
    {
        const bool locked = s.sync && t.playing;
        const double perTurn = s.sync ? division::beats (s.division) : 4.0;
        if (! initialised)
        {
            set = s;
            beatsPerTurn = perTurn;
            beats = locked ? t.ppq : 0.0;
            offset = s.startTurns;
            initialised = true;
            wasLocked = locked;
            jumped = false;
            return;
        }
        jumped = false;
        // the transport starts (or SYNC comes on while playing): lock to the bar
        if (locked && ! wasLocked)
        {
            beats = t.ppq;
            beatsPerTurn = perTurn;
            offset = s.startTurns;
            set = s;
            jumped = true;
        }
        else if (locked && std::abs (t.ppq - expectedPpq) > 1.0e-3)
        {
            // the host jumped (loop, locate): the sound follows the bar
            beats = t.ppq;
            set = s;
            beatsPerTurn = perTurn;
            jumped = true;
        }
        else
        {
            // keep the position while anything else changes: direction, mode,
            // division, rate, start point (the offset absorbs it); locked, the
            // clock is the host's position (the offset also absorbs rounding)
            const double before = phaseNow();
            if (locked)
                beats = t.ppq;
            set = s;
            beatsPerTurn = perTurn;
            offset += before - phaseNow();
        }
        wasLocked = locked;
        lockedNow = locked;
        bpm = t.bpm;
        expectedPpq = t.ppq;
    }

    /** One sample: advances, returns the phase (turns). `input` feeds the
        transient detector; `midiTrigger` = a note-on at this sample. */
    double next (float input, bool midiTrigger)
    {
        const double beatInc = set.sync ? bpm / 60.0 / sr : 4.0 * set.rateHz / sr;
        beats += beatInc;
        expectedPpq += bpm / 60.0 / sr;

        bool trigger = set.retrigger == Retrigger::midi && midiTrigger;
        if (set.retrigger == Retrigger::transient)
        {
            const double a = std::abs ((double) input);
            envFast += (a > envFast ? fastAttack : fastRelease) * (a - envFast);
            envSlow += slowCoeff * (a - envSlow);
            if (holdoff > 0)
                --holdoff;
            else if (envFast > 0.01 && envFast > 2.5 * envSlow + 1.0e-4)
            {
                trigger = true;
                holdoff = (int) (0.08 * sr);
            }
        }
        if (trigger)
        {
            // back to the start point, from here on
            offset += set.startTurns - phaseNow();
            jumped = true;
        }
        return phaseNow();
    }

    /** A jump happened since the last call (transport start, host jump, retrigger). */
    bool takeJump() noexcept
    {
        const bool j = jumped;
        jumped = false;
        return j;
    }

    bool isLocked() const noexcept { return lockedNow; }
    /** The speed in turns per second (for the Doppler limit). */
    double turnsPerSecond() const noexcept { return set.sync ? bpm / 60.0 / beatsPerTurn : set.rateHz; }
    double getBeatsPerTurn() const noexcept { return beatsPerTurn; }

    /** GROOVE: a softened staircase over the beats. g = 0: straight; up to
        1: the move takes the first 30 % of each beat, then the sound rests. */
    static double grooveBeats (double b, double g)
    {
        if (g <= 0.0)
            return b;
        const double k = std::floor (b), s = b - k;
        const double moveLength = 1.0 - 0.7 * g;
        const double x = std::min (1.0, s / moveLength);
        const double eased = x * x * x * (x * (x * 6.0 - 15.0) + 10.0); // smootherstep: starts and stops gently
        return k + (1.0 - g) * s + g * eased;
    }

private:
    double phaseNow() const { return offset + (set.leftToRight ? 1.0 : -1.0) * grooveBeats (beats, set.groove) / beatsPerTurn; }

    double sr = 48000.0, bpm = 120.0;
    Settings set;
    double beats = 0.0, offset = 0.0, beatsPerTurn = 4.0, expectedPpq = 0.0;
    bool initialised = false, wasLocked = false, lockedNow = false, jumped = false;
    double envFast = 0.0, envSlow = 0.0, fastAttack = 0.0, fastRelease = 0.0, slowCoeff = 0.0;
    int holdoff = 0;
};
} // namespace headspin
