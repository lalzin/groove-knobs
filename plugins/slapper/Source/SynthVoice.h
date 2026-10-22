// SLAPPER — the synth voices: the other basses a funk / disco record has.
//
//   ANALOG  two saws (the second detuned, or a square), into a resonant 24 dB ladder low-pass with its envelope:
//           the mono-synth funk bass (the squelch is the resonance).
//   FM      two operators: a sine carrier, its modulator (ratio 1, 2 or 3,
//           with feedback), the index falling fast after the attack: the
//           80s FM bass, percussive, glassy, a slap without a string.
//   PULSE   a pulse wave (its width moving slowly), into the ladder with less resonance: the boogie /
//           disco polysynth bass.
//
// The same knobs play them, each its own way (SlapperDsp::synthParams):
//   ATTACK  how fast the bright attack closes (0 slow, 1 a short pluck)
//   FUNK    how much attack: envelope depth / FM index, resonance
//   MUTE    the note's length (sustain down, release short)
//   PICK    ANALOG saw -> square; FM the modulator's ratio; PULSE the width
//   STRING  ANALOG detune; FM feedback; PULSE the width's movement
//   BODY    a sine on the fundamental, in phase with it (as the string's sub:
//           weight at the note's pitch, never an octave lower)
// Oscillators band-limited (polyBLEP); a slide (MONO legato) glides the pitch.
#pragma once
#include <cstdint>

namespace slapper
{
enum class Engine { string, analog, fm, pulse };

struct SynthParams
{
    Engine engine = Engine::analog;
    float attack = 0.5f, pick = 0.15f, string = 0.4f, body = 0.3f, mute = 0.0f, funk = 0.5f;
};

class SynthVoice
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void noteOn (double hz, float velocity, const SynthParams& p) noexcept;
    void glideTo (double hz, double seconds) noexcept;
    void noteOff() noexcept;
    void setBend (double ratio) noexcept { bend = ratio; }
    float process() noexcept;

    bool isActive() const noexcept { return stage != Stage::idle; }
    bool isHeld() const noexcept { return held; }
    double currentHz() const noexcept { return targetHz; }

private:
    enum class Stage { idle, attack, decay, release };
    double sr = 48000.0;
    SynthParams p;
    bool held = false, ghost = false;
    // pitch (log, glided), bend
    double targetHz = 55.0, logHz = 0.0, logFrom = 0.0, logTo = 0.0, glidePos = 1.0, glideStep = 0.0, bend = 1.0;
    // oscillators (phases 0..1)
    double ph1 = 0.0, ph2 = 0.0, phMod = 0.0, phLfo = 0.0;
    double fmPrev = 0.0, fmIndex = 0.0, fmIndexCoef = 0.0; // the index follows its target in ~0.7 ms (a jump is a click)
    // envelopes: amp (attack / decay to sustain / release), filter (or FM index): a decay
    Stage stage = Stage::idle;
    double amp = 0.0, sustain = 0.8, attackStep = 0.0, decayCoef = 0.0, releaseCoef = 0.0;
    double fenv = 0.0, fenvCoef = 0.0;
    float velocity = 1.0f;
    // the ladder: four TPT one-poles, feedback
    double s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;
    double ladder (double x, double fc, double k) noexcept;
    double dcX = 0.0, dcY = 0.0;
    int pitchCountdown = 0;
    double f = 55.0; // the current frequency (glide, bend), every 16 samples
};
} // namespace slapper
