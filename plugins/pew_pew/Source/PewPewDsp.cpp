#include "PewPewDsp.h"

namespace pp
{
using namespace gk::dspmath;

PewPreset PewPreset::fromJson (const juce::var& root)
{
    PewPreset p;
    auto num = [&root] (const char* key, float fallback)
    {
        const auto v = root.getProperty (key, fallback);
        return v.isDouble() || v.isInt() || v.isInt64() ? (float) (double) v : fallback;
    };
    const auto osc = root.getProperty ("osc", "sine").toString();
    p.osc = osc == "triangle" ? Osc::triangle : osc == "square" ? Osc::square : osc == "fm" ? Osc::fm : osc == "metal" ? Osc::metal : Osc::sine;
    p.baseHz = juce::jlimit (20.0f, 2000.0f, num ("baseHz", p.baseHz));
    p.tuneOffset = juce::jlimit (-48.0f, 48.0f, num ("tuneOffset", p.tuneOffset));
    p.sweepOct = juce::jlimit (0.0f, 6.0f, num ("sweepOct", p.sweepOct));
    p.endOct = juce::jlimit (-4.0f, 2.0f, num ("endOct", p.endOct));
    p.pitchTauMs = juce::jlimit (1.0f, 2000.0f, num ("pitchTauMs", p.pitchTauMs));
    p.decayMs = juce::jlimit (10.0f, 5000.0f, num ("decayMs", p.decayMs));
    p.clickDb = juce::jlimit (-80.0f, 0.0f, num ("clickDb", p.clickDb));
    p.velCurve = juce::jlimit (0.5f, 3.0f, num ("velCurve", p.velCurve));
    p.level = juce::jlimit (0.0f, 4.0f, num ("level", p.level));
    p.lpHz = juce::jlimit (100.0f, 20000.0f, num ("lpHz", p.lpHz));
    p.fmRatio = juce::jlimit (0.25f, 8.0f, num ("fmRatio", p.fmRatio));
    p.fmIndex = juce::jlimit (0.0f, 20.0f, num ("fmIndex", p.fmIndex));
    p.fmTauMs = juce::jlimit (1.0f, 5000.0f, num ("fmTauMs", p.fmTauMs));
    p.roomSize = juce::jlimit (0.0f, 1.0f, num ("roomSize", p.roomSize));
    p.damping = juce::jlimit (0.0f, 1.0f, num ("damping", p.damping));
    p.reverbMax = juce::jlimit (0.0f, 1.0f, num ("reverbMax", p.reverbMax));
    p.delayFeedback = juce::jlimit (0.0f, 0.9f, num ("delayFeedback", p.delayFeedback));
    p.linearGlide = root.getProperty ("glide", "exp").toString() == "linear";
    p.glideMs = juce::jlimit (5.0f, 2000.0f, num ("glideMs", p.glideMs));
    p.holdMs = juce::jlimit (0.0f, 1000.0f, num ("holdMs", p.holdMs));
    p.detuneCents = juce::jlimit (-1200.0f, 1200.0f, num ("detuneCents", p.detuneCents));
    p.detuneMix = juce::jlimit (0.0f, 1.0f, num ("detuneMix", p.detuneMix));
    p.drive = juce::jlimit (0.0f, 1.0f, num ("drive", p.drive));
    p.noise = juce::jlimit (0.0f, 1.0f, num ("noise", p.noise));
    p.wobble = juce::jlimit (0.0f, 1.0f, num ("wobble", p.wobble));
    return p;
}

//==============================================================================
double PewVoice::baseFrequency (const PewPreset& p, int note) noexcept
{
    // MIDI note 60 (C3 in Ableton) plays the preset's base frequency
    // (KEY TRACK below 100 %: the notes move the pitch less, 0 = all at the base)
    return (double) p.baseHz * std::pow (2.0, (((double) note - 60.0) * (double) p.keyTrack + (double) p.tuneOffset) / 12.0);
}

void PewVoice::start (const Start& s) noexcept
{
    const auto& p = *s.preset;
    const double lengthScale = s.longer ? 2.0 : 1.0; // LONG: hold and decay doubled
    sr = s.sampleRate;
    osc = p.osc;
    order = s.order;
    fBase = baseFrequency (p, s.note);
    const double nyquistGuard = 0.45 * sr;
    // The glide scales with the knob at both ends: at 0 % the tom is flat
    // (fStart = fEnd = fBase), at 100 % it falls from +sweepOct to endOct.
    // Velocity also sets the depth: harder hits glide further (60 % at the
    // softest, all of it at full velocity); UP mirrors the glide upwards
    const double depth = s.amount * (0.6 + 0.4 * juce::jlimit (0.0f, 1.0f, s.velocity)) * (s.up ? -1.0 : 1.0);
    fStart = juce::jmin (nyquistGuard, fBase * std::pow (2.0, (double) p.sweepOct * depth));
    fEnd = juce::jmin (nyquistGuard, fBase * std::pow (2.0, (double) p.endOct * depth));
    glide = fStart - fEnd;
    glideCoeff = std::exp (-1.0 / (p.pitchTauMs * 0.001 * sr));
    // Linear glide (the disco lazer): from +sweepOct to endOct (both scaled
    // by the knob) at a steady rate, reached at glideMs, and on
    linear = p.linearGlide;
    octNow = std::log2 (fStart / fBase);
    octEnd = std::log2 (fEnd / fBase);
    octStep = (octEnd - octNow) / juce::jmax (1.0, p.glideMs * 0.001 * sr);
    holdLen = (int) std::round (p.holdMs * lengthScale * 0.001 * sr);
    ratio2 = std::pow (2.0, p.detuneCents / 1200.0);
    mix2 = p.detuneMix;
    phase2 = 0.0;
    lp2.reset();
    driveK = 1.0 + 3.0 * p.drive;
    driveBias = 0.2 * p.drive;
    driveNorm = 1.0 / (driveK * (1.0 - std::pow (std::tanh (driveK * driveBias), 2.0)));

    env = 1.0;
    decayCoeff = std::exp (-1.0 / (p.decayMs * lengthScale * 0.001 * sr));
    attackLen = juce::jmax (1, (int) std::round (0.001 * sr));
    releaseLen = juce::jmax (1, (int) std::round (0.005 * sr));
    releaseLeft = -1;
    fadeLeft = -1;
    clickLen = juce::jmax (1, (int) std::round (0.004 * sr));
    clickGain = s.amount * dbToGain (p.clickDb);
    clickCoeffs = gk::TptSvf::Coeffs::make (2000.0, sr, 0.7071);
    lpCoeffs = gk::TptSvf::Coeffs::make (juce::jmin ((double) p.lpHz, nyquistGuard), sr, 0.7071);
    fmIndex = p.fmIndex;
    fmCoeff = std::exp (-1.0 / (p.fmTauMs * 0.001 * sr));
    fmRatio = p.fmRatio;
    gain = p.level * std::pow (juce::jlimit (0.0f, 1.0f, s.velocity), p.velCurve) * 0.25f;

    // NOISE: a band-passed noise burst under the tone (Simmons / syndrum snare)
    noiseGain = 0.9f * p.noise;
    noiseEnv = 1.0;
    noiseCoeff = std::exp (-1.0 / (juce::jmin (250.0, 0.5 * (double) p.decayMs) * lengthScale * 0.001 * sr));
    noiseCoeffs = gk::TptSvf::Coeffs::make (juce::jlimit (300.0, juce::jmin (8000.0, nyquistGuard), 3.0 * fBase), sr, 0.9);
    noiseBp.reset();
    noiseRng = gk::testsignals::Rng (0x9e3779b9u + 7919u * (std::uint32_t) s.note); // the same burst on every hit of a note (a drum, not a hiss)
    // WOBBLE: a vibrato that grows in over 80 ms, deeper and faster with it
    wobbleSt = 1.5 * (double) p.wobble;
    wobbleInc = (5.0 + 4.0 * (double) p.wobble) / sr;
    wobblePhase = 0.0;
    wobbleEnv = 0.0;
    wobbleEnvStep = 1.0 / (0.08 * sr);
    metalPhase = 0.0;

    phase = modPhase = 0.0; // phase reset on every hit
    n = 0;
    lp.reset();
    clickHp.reset();
    rng = gk::testsignals::Rng (s.seed);
    active = true;
}

void PewVoice::fadeOut (int samples) noexcept
{
    if (! active)
        return;
    fadeLen = juce::jmax (1, samples);
    fadeLeft = fadeLeft >= 0 ? juce::jmin (fadeLeft, fadeLen) : fadeLen;
}

namespace
{
    inline double polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }
} // namespace

double PewVoice::oscillator (double ph, double inc, gk::TptSvf& filter) noexcept
{
    switch (osc)
    {
        case PewPreset::Osc::sine: return std::sin (juce::MathConstants<double>::twoPi * ph);
        case PewPreset::Osc::triangle: return 1.0 - 4.0 * std::abs (ph - 0.5);
        case PewPreset::Osc::square:
        {
            double sq = ph < 0.5 ? 1.0 : -1.0;
            sq += polyBlep (ph, inc) - polyBlep (std::fmod (ph + 0.5, 1.0), inc);
            return filter.processLowpass (sq, lpCoeffs);
        }
        case PewPreset::Osc::fm:
        {
            if (&filter == &lp) // the modulator follows the main oscillator
            {
                modPhase += inc * fmRatio;
                modPhase -= std::floor (modPhase);
                fmIndex *= fmCoeff;
            }
            return std::sin (juce::MathConstants<double>::twoPi * ph + fmIndex * std::sin (juce::MathConstants<double>::twoPi * modPhase));
        }
        case PewPreset::Osc::metal:
        {
            // ring modulation by an inharmonic partner (x 2.76): bell / cowbell partials
            if (&filter == &lp)
            {
                metalPhase += inc * 2.76;
                metalPhase -= std::floor (metalPhase);
            }
            const double a = std::sin (juce::MathConstants<double>::twoPi * ph);
            return 0.55 * a + 0.8 * a * std::sin (juce::MathConstants<double>::twoPi * metalPhase);
        }
    }
    return 0.0;
}

float PewVoice::pitchPosition() const noexcept
{
    if (linear)
    {
        const double range = std::log2 (fStart / fBase) - octEnd;
        return std::abs (range) > 1.0e-9 ? (float) juce::jlimit (0.0, 1.0, (octNow - octEnd) / range) : 0.0f;
    }
    const double range = fStart - fEnd;
    return range > 1.0e-9 ? (float) juce::jlimit (0.0, 1.0, glide / range) : 0.0f;
}


float PewVoice::render (double pitchMul) noexcept
{
    if (! active)
        return 0.0f;

    // Pitch: exponential "pew", fEnd + (fStart - fEnd) e^(-t / tau), or the
    // disco lazer's steady slide
    double f;
    if (linear)
    {
        // keeps sliding at the same rate past glideMs, as the reference does
        // while it fades (glideMs sets the rate: the depth is reached there)
        f = juce::jlimit (5.0, 0.45 * sr, fBase * std::exp2 (octNow));
        octNow += octStep;
    }
    else
    {
        f = fEnd + glide;
        glide *= glideCoeff;
    }
    if (wobbleSt > 0.0)
    {
        wobblePhase += wobbleInc;
        wobblePhase -= std::floor (wobblePhase);
        wobbleEnv = juce::jmin (1.0, wobbleEnv + wobbleEnvStep);
        f *= std::exp2 (wobbleSt * wobbleEnv * std::sin (juce::MathConstants<double>::twoPi * wobblePhase) / 12.0);
    }
    if (pitchMul != 1.0)
        f = juce::jmin (f * pitchMul, 0.45 * sr);
    const double inc = f / sr;
    phase += inc; // advanced first: the very first sample is not a zero
    phase -= std::floor (phase);
    double v = oscillator (phase, inc, lp);
    if (mix2 > 0.0)
    {
        // a second oscillator a few cents away: the slow beating of the reference
        phase2 += inc * ratio2;
        phase2 -= std::floor (phase2);
        v = (1.0 - 0.5 * mix2) * v + 0.5 * mix2 * oscillator (phase2, inc * ratio2, lp2);
    }
    if (driveK > 1.0)
        v = (std::tanh (driveK * (v + driveBias)) - std::tanh (driveK * driveBias)) * driveNorm;

    // Amplitude: 1 ms attack, held for holdMs, then e^(-t / decay)
    const double a = env * juce::jmin (1.0, (double) (n + 1) / attackLen);
    if (n >= holdLen)
        env *= decayCoeff;

    // Laser click: high-passed noise, 4 ms, only when the knob is up
    double click = 0.0;
    if (clickGain > 0.0f && n < clickLen)
    {
        const double e = 1.0 - (double) n / clickLen;
        click = clickHp.processHighpass (rng.bipolar(), clickCoeffs) * e * e * clickGain;
    }

    double noise = 0.0;
    if (noiseGain > 0.0f)
    {
        noise = noiseBp.processBandpass (noiseRng.bipolar(), noiseCoeffs) * noiseEnv * noiseGain * juce::jmin (1.0, (double) (n + 1) / attackLen);
        noiseEnv *= noiseCoeff;
    }

    float y = gain * (float) (v * a + click + noise);
    ++n;

    // -60 dB: 5 ms fade, then the voice is free
    if (releaseLeft < 0 && env < 0.001 && noiseEnv * noiseGain < 0.001)
        releaseLeft = releaseLen;
    if (releaseLeft >= 0)
    {
        y *= (float) releaseLeft / (float) releaseLen;
        if (--releaseLeft < 0)
            active = false;
    }
    if (fadeLeft >= 0)
    {
        y *= (float) fadeLeft / (float) fadeLen;
        if (--fadeLeft < 0)
            active = false;
    }
    return y;
}

//==============================================================================
PewPreset PewPewDsp::applyPanel (const PewPreset& p, const float* k, int waveIndex) noexcept
{
    // Each knob at its default leaves its field untouched (the same sound,
    // bit for bit, and the calibrated levels)
    PewPreset e = p;
    auto scale4 = [] (float knob) { return std::exp2 (4.0f * (knob - 0.5f)); }; // x0.25 .. x4, x1 at 50 %
    // TOM
    e.tuneOffset += k[tuneKnob];
    if (k[decayKnob] != 0.5f)
    {
        e.decayMs *= scale4 (k[decayKnob]);
        e.holdMs *= scale4 (k[decayKnob]);
    }
    if (k[glideKnob] != 0.5f)
    {
        e.pitchTauMs *= scale4 (k[glideKnob]);
        e.glideMs *= scale4 (k[glideKnob]);
    }
    if (const float d = k[detuneKnob]; d < 0.5f)
        e.detuneMix *= 2.0f * d;
    else if (d > 0.5f)
    {
        const float x = 2.0f * d - 1.0f;
        e.detuneMix += (1.0f - e.detuneMix) * x;
        e.detuneCents += (e.detuneCents < 0.0f ? -25.0f : 25.0f) * x;
    }
    // COLOUR
    if (const float d = k[driveKnob]; d < 0.5f)
        e.drive *= 2.0f * d;
    else if (d > 0.5f)
        e.drive += (1.0f - e.drive) * (2.0f * d - 1.0f);
    if (const float c = k[clickKnob]; c < 0.5f)
        e.clickDb = c <= 0.0f ? -200.0f : e.clickDb + gainToDb (2.0f * c);
    else if (c > 0.5f)
        e.clickDb = juce::jmin (0.0f, e.clickDb + gainToDb (1.0f + 15.0f * (2.0f * c - 1.0f)));
    e.noise = juce::jmin (1.0f, e.noise + k[noiseKnob]);
    // FX (voice part)
    e.wobble = juce::jmin (1.0f, e.wobble + k[wobbleKnob]);
    // PLAY
    if (const float v = k[velKnob]; v < 0.5f)
        e.velCurve *= 2.0f * v;
    else if (v > 0.5f)
        e.velCurve *= 1.0f + 3.0f * (v - 0.5f);
    e.keyTrack = k[keyTrackKnob];
    // WAVE: 0 keeps the preset's oscillator
    using Osc = PewPreset::Osc;
    const Osc waves[] { Osc::sine, Osc::triangle, Osc::square, Osc::fm, Osc::metal };
    if (waveIndex >= 1 && waveIndex <= 5)
        e.osc = waves[waveIndex - 1];
    return e;
}

float PewPewDsp::amountForHit() const noexcept
{
    // the mod wheel pushes PEW towards 100 % for the next hits
    const float a = amountForVoices.getCurrent();
    return modWheel > 0.0f ? a + modWheel * (1.0f - a) : a;
}

//==============================================================================
double PewPewDsp::divisionBeats (int index) noexcept
{
    switch (index)
    {
        case 0: return 1.0 / 6.0; // 1/16 triplet
        case 1: return 0.25;      // 1/16
        case 2: return 1.0 / 3.0; // 1/8 triplet
        case 3: return 0.5;       // 1/8
        case 4: return 0.75;      // dotted 1/8
        default: return 1.0;      // 1/4
    }
}

void PewPewDsp::setPresets (const gk::PresetManager& pm)
{
    presets.clear();
    for (int i = 0; i < pm.size(); ++i)
        presets.push_back (PewPreset::fromJson (pm[i].json));
    if (presets.empty())
        presets.emplace_back();
}

void PewPewDsp::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = juce::jlimit (1, 2, newNumChannels);
    const int block = juce::jmax (1, maxBlockSize);

    reverb.setSampleRate (sampleRate);
    appliedReverbPreset = -1;
    preDelay.prepare (2, (int) std::round (preDelaySeconds * sampleRate));
    dry.setSize (1, block, false, true, false);
    revBuf.setSize (2, block, false, true, false);
    delayLen = (int) std::ceil (maxDelaySeconds * sampleRate) + 4;
    for (auto& l : delayLine)
        l.assign ((size_t) delayLen, 0.0f);
    echoLp = gk::TptSvf::Coeffs::make (3000.0, sampleRate, 0.7071);
    delayFadeStep = (float) (1.0 / (delayCrossfadeSeconds * sampleRate));
    panicLen = juce::jmax (1, (int) std::round (panicFadeSeconds * sampleRate));
    for (auto* s : { &roomMix, &echoMix, &amountForVoices, &feedback, &volume, &toneGain, &echoRight })
        s->prepare (sampleRate, smoothSeconds);
    toneCoeffs = gk::TptSvf::Coeffs::make (1500.0, sampleRate, 0.7071);
    bendCoeff = 1.0 - std::exp (-1.0 / (0.005 * sampleRate));
    bendTarget = bendNow = 0.0;
    modWheel = 0.0f;
    appliedRoomSize = -1.0f;

    for (auto& v : voices)
        v = PewVoice();
    for (auto& v : tails)
        v = PewVoice();
    clearEffects();
    soundRecently = false;
    silentSamples = std::int64_t (1) << 40;
    panicLeft = -1;
    for (auto* s : { &roomMix, &echoMix, &amountForVoices, &feedback, &volume, &toneGain, &echoRight })
        s->invalidate();
    amountReady = false;
    delayTime = delayTimeOld = 0.0;
    delayFade = 1.0f;
}

void PewPewDsp::clearEffects()
{
    reverb.reset();
    preDelay.reset();
    for (auto& l : delayLine)
        std::fill (l.begin(), l.end(), 0.0f);
    delayWrite = 0;
    echoLpL.reset();
    echoLpR.reset();
    toneSplit.reset();
}

void PewPewDsp::reset()
{
    // With sound still ringing: a clean 5 ms cut; otherwise clear at once
    // (a note right after the reset must not be faded)
    if (soundRecently)
        panic();
    else
    {
        for (auto& v : voices)
            v = PewVoice();
        for (auto& v : tails)
            v = PewVoice();
        for (auto& r : rolls)
            r.left = 0;
        clearEffects();
    }
}

void PewPewDsp::panic()
{
    for (auto& r : rolls)
        r.left = 0;
    for (auto& v : voices)
        v.fadeOut (panicLen);
    for (auto& v : tails)
        v.fadeOut (panicLen);
    panicLeft = panicLen;
}

void PewPewDsp::setMidi (const juce::MidiBuffer& m, int start, int count)
{
    midi = &m;
    midiStart = start;
    midiCount = count;
}

void PewPewDsp::setKnobs (const float* values, int n)
{
    room = n > roomKnob ? clamp01 (values[roomKnob]) : 0.0f;
    echo = n > echoKnob ? clamp01 (values[echoKnob]) : 0.0f;
    volumeDb = n > volumeKnob ? values[volumeKnob] : 0.0f;
    for (int i = 0; i < juce::jmin (n, (int) numKnobs); ++i)
        knobs[(size_t) i] = values[i];
}

void PewPewDsp::moveToTail (PewVoice& v)
{
    // the voice keeps sounding in a tail slot (a free one, else the oldest),
    // faded out over 2 ms
    PewVoice* tail = &tails[0];
    for (auto& t : tails)
        if (! t.isActive() || t.getOrder() < tail->getOrder())
        {
            tail = &t;
            if (! t.isActive())
                break;
        }
    *tail = v;
    tail->fadeOut ((int) std::round (stealFadeSeconds * sampleRate));
    v = PewVoice();
}

void PewPewDsp::noteOn (int note, float velocity, float amountNow, bool up, bool longer)
{
    // SOLO: the sounding pew is cut (2 ms) by the new one
    if (solo)
        for (auto& v : voices)
            if (v.isActive())
                moveToTail (v);
    PewVoice* target = nullptr;
    for (auto& v : voices)
        if (! v.isActive())
        {
            target = &v;
            break;
        }
    if (target == nullptr)
    {
        // Steal the oldest
        target = &voices[0];
        for (auto& v : voices)
            if (v.getOrder() < target->getOrder())
                target = &v;
        moveToTail (*target);
    }
    PewVoice::Start s;
    voicePreset = applyPanel (presets[(size_t) presetIndex], knobs.data(), wave);
    s.preset = &voicePreset;
    s.note = note;
    s.velocity = velocity;
    s.amount = amountNow;
    s.up = up;
    s.longer = longer;
    s.sampleRate = sampleRate;
    s.seed = (seed = seed * 1664525u + 1013904223u) | 1u;
    s.order = ++noteOrder;
    target->start (s);

    telemetry.triggerCounter.fetch_add (1, std::memory_order_relaxed);
    telemetry.lastTriggerVelocity.store (velocity, std::memory_order_relaxed);
}

void PewPewDsp::renderVoices (float* out, int from, int to)
{
    for (int i = from; i < to; ++i)
    {
        double bend = 1.0;
        if (bendNow != bendTarget)
        {
            bendNow += (bendTarget - bendNow) * bendCoeff;
            if (std::abs (bendTarget - bendNow) < 1.0e-6)
                bendNow = bendTarget;
        }
        if (bendNow != 0.0)
            bend = std::exp2 (bendNow / 12.0);
        float s = 0.0f;
        for (auto& v : voices)
            s += v.render (bend);
        for (auto& v : tails)
            s += v.render (bend);
        out[i] = s;
    }
}

void PewPewDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing)
{
    juce::ScopedNoDenormals noDenormals;
    const auto& p = presets[(size_t) presetIndex];

    // SPACE: the room size, from small (0 %) through the preset's (50 %) to huge
    const float sp = knobs[spaceKnob];
    const float roomSize = sp == 0.5f ? p.roomSize : sp < 0.5f ? 0.2f + (p.roomSize - 0.2f) * 2.0f * sp : p.roomSize + (0.98f - p.roomSize) * (2.0f * sp - 1.0f);
    if (appliedReverbPreset != presetIndex || std::abs (roomSize - appliedRoomSize) > 1.0e-4f)
    {
        appliedRoomSize = roomSize;
        juce::Reverb::Parameters rp;
        rp.roomSize = roomSize;
        rp.damping = p.damping;
        rp.wetLevel = 1.0f;
        rp.dryLevel = 0.0f;
        rp.width = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        appliedReverbPreset = presetIndex;
    }

    // Delay time from the host tempo; a new time crossfades over 30 ms
    {
        const double t = juce::jlimit (1.0, (double) delayLen - 4.0, divisionBeats (division) * 60.0 / juce::jmax (20.0, timing.bpm) * sampleRate);
        if (delayTime <= 0.0)
            delayTime = t;
        else if (std::abs (t - delayTime) > 0.5 && delayFade >= 1.0f)
        {
            delayTimeOld = delayTime;
            delayTime = t;
            delayFade = 0.0f;
        }
    }

    // --- Voices, split at each MIDI event (sample accurate) ---------------
    float* d = dry.getWritePointer (0);
    // amount for new hits, smoothed (derived values follow over 30 ms); it
    // starts at the knob (a hit on the very first sample gets the knob value)
    if (! amountReady)
    {
        amountForVoices.snap (clamp01 (amount[0]));
        amountReady = true;
    }
    int pos = 0;
    auto advanceTo = [&] (int end)
    {
        for (int i = pos; i < end; ++i)
        {
            amountForVoices.setTarget (clamp01 (amount[i]));
            amountForVoices.next();
        }
        renderVoices (d, pos, end);
        pos = end;
    };
    // Events of this block, sample accurate: the MIDI notes and the ROLL
    // repeats, in time order
    const std::int64_t clock0 = telemetry.sampleClock.load (std::memory_order_relaxed);
    auto nextRoll = [&] (int) -> Roll*
    {
        Roll* best = nullptr;
        for (auto& r : rolls)
            if (r.left > 0 && r.next - clock0 < numSamples && (best == nullptr || r.next < best->next))
                best = &r;
        return best;
    };
    auto fireRolls = [&] (int upTo) // every repeat before `upTo` (exclusive)
    {
        while (auto* r = nextRoll (pos))
        {
            const int at = juce::jmax (pos, (int) (r->next - clock0)); // (a late one plays now)
            if (at >= upTo)
                break;
            advanceTo (at);
            r->velocity *= rollDecay;
            noteOn (r->note, r->velocity, amountForHit(), r->up, r->longer);
            telemetry.lastTriggerSample.store (clock0 + at, std::memory_order_relaxed);
            r->nextExact += r->step;
            r->step *= r->stepFactor; // RUSH: the roll speeds up (or slows down)
            r->next = (std::int64_t) std::llround (r->nextExact);
            --r->left;
        }
    };
    if (midi != nullptr)
    {
        for (const auto meta : *midi)
        {
            const int at = meta.samplePosition - midiStart;
            if (at < 0)
                continue;
            if (at >= midiCount || at >= numSamples)
                break;
            fireRolls (at);
            advanceTo (at);
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) // (velocity 0 counts as a note-off: ignored, the pew is a one-shot)
            {
                const bool up = toggleAt (upToggle, at), longer = toggleAt (longToggle, at);
                noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), amountForHit(), up, longer);
                telemetry.lastTriggerSample.store (clock0 + at, std::memory_order_relaxed);
                if (toggleAt (rollToggle, at))
                {
                    // ROLL: sixteenths at the host tempo, each softer
                    Roll* slot = &rolls[0];
                    for (auto& r : rolls)
                        if (r.left <= 0)
                        {
                            slot = &r;
                            break;
                        }
                    slot->step = 0.25 / juce::jmax (1.0e-9, timing.beatsPerSample);
                    slot->nextExact = (double) (clock0 + at) + slot->step;
                    slot->next = (std::int64_t) std::llround (slot->nextExact);
                    slot->note = msg.getNoteNumber();
                    slot->velocity = msg.getFloatVelocity();
                    // HITS and RUSH (panel): 5 sixteenths at an even pace by default
                    slot->left = juce::jlimit (2, 16, juce::roundToInt (knobs[hitsKnob])) - 1;
                    slot->stepFactor = 1.0 - 0.12 * (double) juce::jlimit (-1.0f, 1.0f, knobs[rushKnob]);
                    slot->up = up;
                    slot->longer = longer;
                }
            }
            else if (msg.isPitchWheel())
                bendTarget = bendRangeSemitones * juce::jlimit (-1.0, 1.0, (msg.getPitchWheelValue() - 8192) / 8192.0);
            else if (msg.isControllerOfType (1))
                modWheel = (float) msg.getControllerValue() / 127.0f;
            else if (msg.isResetAllControllers())
            {
                bendTarget = 0.0;
                modWheel = 0.0f;
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                for (auto& v : voices)
                    v.fadeOut (panicLen);
                for (auto& v : tails)
                    v.fadeOut (panicLen);
                for (auto& r : rolls)
                    r.left = 0;
            }
        }
        midi = nullptr;
    }
    fireRolls (numSamples);
    advanceTo (numSamples);

    // --- TONE: a high shelf on the voices (+-12 dB above 1.5 kHz) -------------
    toneGain.setTarget (dbToGain (24.0f * (knobs[toneKnob] - 0.5f)));
    for (int i = 0; i < numSamples; ++i)
    {
        const float low = (float) toneSplit.processLowpass (d[i], toneCoeffs); // (always running: no jump when it moves)
        const float g = toneGain.next();
        if (g != 1.0f)
            d[i] = low + g * (d[i] - low);
    }

    // --- Sends -------------------------------------------------------------
    for (int c = 0; c < 2; ++c)
        preDelay.process (c, d, revBuf.getWritePointer (c), numSamples);
    preDelay.advance (numSamples);
    reverb.processStereo (revBuf.getWritePointer (0), revBuf.getWritePointer (1), numSamples);

    const float* rl = revBuf.getReadPointer (0);
    const float* rr = revBuf.getReadPointer (1);
    auto& lineL = delayLine[0];
    auto& lineR = delayLine[1];
    auto readAt = [this] (const std::vector<float>& line, double delay)
    {
        double r = (double) delayWrite - delay;
        while (r < 0.0)
            r += delayLen;
        const int i0 = (int) r;
        const float f = (float) (r - i0);
        const int i1 = i0 + 1 >= delayLen ? 0 : i0 + 1;
        return line[(size_t) i0] + f * (line[(size_t) i1] - line[(size_t) i0]);
    };

    bool anySound = false, effectsCleared = false;
    for (int i = 0; i < numSamples; ++i)
    {
        if ((counter++ % 8u) == 0)
        {
            roomMix.setTarget (room * p.reverbMax);
            echoMix.setTarget (echo * 0.5f);
            // FEEDBACK: none (0 %), the preset's (50 %), up to 0.9;
            // WIDTH: the echo's right input, mono (0 %) .. the preset's light ping-pong (50 %) .. full ping-pong
            const float fbk = knobs[feedbackKnob], w = knobs[widthKnob];
            feedback.setTarget (fbk == 0.5f ? p.delayFeedback : fbk < 0.5f ? p.delayFeedback * 2.0f * fbk : p.delayFeedback + (0.9f - p.delayFeedback) * (2.0f * fbk - 1.0f));
            echoRight.setTarget (w == 0.5f ? 0.4f : w < 0.5f ? 1.0f - 0.6f * 2.0f * w : 0.4f * (2.0f - 2.0f * w));
            volume.setTarget (volumeDb <= -59.9f ? 0.0f : dbToGain (volumeDb)); // VOL: -inf at the bottom
        }
        const float x = d[i];
        anySound = anySound || x != 0.0f;

        // Stereo delay, light ping-pong: the input goes mostly left, the
        // repeats cross sides, darker each time (3 kHz low-pass in the loop)
        float dl = readAt (lineL, delayTime), dr = readAt (lineR, delayTime);
        if (delayFade < 1.0f)
        {
            const float ol = readAt (lineL, delayTimeOld), orr = readAt (lineR, delayTimeOld);
            const float f = smoothstep (0.0f, 1.0f, delayFade);
            dl = ol + f * (dl - ol);
            dr = orr + f * (dr - orr);
            delayFade = juce::jmin (1.0f, delayFade + delayFadeStep);
        }
        const float fb = feedback.next();
        lineL[(size_t) delayWrite] = x + fb * (float) echoLpL.processLowpass (dr, echoLp);
        lineR[(size_t) delayWrite] = echoRight.next() * x + fb * (float) echoLpR.processLowpass (dl, echoLp);
        if (++delayWrite >= delayLen)
            delayWrite = 0;

        float fx = effectsCleared ? 0.0f : 1.0f; // (the reverb block was rendered before the clear)
        if (panicLeft >= 0)
        {
            fx = (float) panicLeft / (float) panicLen;
            if (--panicLeft < 0)
            {
                clearEffects();
                effectsCleared = true;
                fx = 0.0f;
            }
        }
        const float rm = roomMix.next() * fx, em = echoMix.next() * fx;
        const float vol = volume.next();
        const float outL = vol * (x + rm * rl[i] + em * dl);
        const float outR = vol * (x + rm * rr[i] + em * dr);
        if (numChannels == 2 && buffer.getNumChannels() > 1)
        {
            buffer.getWritePointer (0)[i] = outL;
            buffer.getWritePointer (1)[i] = outR;
        }
        else
            buffer.getWritePointer (0)[i] = 0.5f * (outL + outR);
    }

    // --- Telemetry -----------------------------------------------------------
    int active = 0;
    const PewVoice* newest = nullptr;
    for (const auto& v : voices)
        if (v.isActive())
        {
            ++active;
            if (newest == nullptr || v.getOrder() > newest->getOrder())
                newest = &v;
        }
    telemetry.activeVoices.store (active, std::memory_order_relaxed);
    telemetry.pitchPosition.store (newest != nullptr ? newest->pitchPosition() : 0.0f, std::memory_order_relaxed);
    telemetry.sampleClock.fetch_add (numSamples, std::memory_order_relaxed);
    // Effects may ring for a while after the last voice: a reset within 5 s
    // of sound fades out instead of cutting
    if (anySound || active > 0)
        silentSamples = 0;
    else
        silentSamples += numSamples;
    soundRecently = silentSamples < (std::int64_t) (5.0 * sampleRate);
}
} // namespace pp
