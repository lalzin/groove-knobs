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
    p.osc = osc == "triangle" ? Osc::triangle : osc == "square" ? Osc::square : osc == "fm" ? Osc::fm : Osc::sine;
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
    return p;
}

//==============================================================================
double PewVoice::baseFrequency (const PewPreset& p, int note) noexcept
{
    // MIDI note 60 (C3 in Ableton) plays the preset's base frequency
    return (double) p.baseHz * std::pow (2.0, ((double) note - 60.0 + (double) p.tuneOffset) / 12.0);
}

void PewVoice::start (const Start& s) noexcept
{
    const auto& p = *s.preset;
    sr = s.sampleRate;
    osc = p.osc;
    order = s.order;
    const double fBase = baseFrequency (p, s.note);
    const double nyquistGuard = 0.45 * sr;
    // The glide scales with the knob at both ends: at 0 % the tom is flat
    // (fStart = fEnd = fBase), at 100 % it falls from +sweepOct to endOct.
    fStart = juce::jmin (nyquistGuard, fBase * std::pow (2.0, (double) p.sweepOct * s.amount));
    fEnd = juce::jmin (nyquistGuard, fBase * std::pow (2.0, (double) p.endOct * s.amount));
    glide = fStart - fEnd;
    glideCoeff = std::exp (-1.0 / (p.pitchTauMs * 0.001 * sr));

    env = 1.0;
    decayCoeff = std::exp (-1.0 / (p.decayMs * 0.001 * sr));
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

float PewVoice::pitchPosition() const noexcept
{
    const double range = fStart - fEnd;
    return range > 1.0e-9 ? (float) juce::jlimit (0.0, 1.0, glide / range) : 0.0f;
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

float PewVoice::render() noexcept
{
    if (! active)
        return 0.0f;

    // Pitch: fEnd + (fStart - fEnd) e^(-t / tau)
    const double f = fEnd + glide;
    glide *= glideCoeff;
    const double inc = f / sr;
    phase += inc; // advanced first: the very first sample is not a zero
    phase -= std::floor (phase);

    double v = 0.0;
    switch (osc)
    {
        case PewPreset::Osc::sine: v = std::sin (juce::MathConstants<double>::twoPi * phase); break;
        case PewPreset::Osc::triangle: v = 1.0 - 4.0 * std::abs (phase - 0.5); break;
        case PewPreset::Osc::square:
        {
            double sq = phase < 0.5 ? 1.0 : -1.0;
            sq += polyBlep (phase, inc) - polyBlep (std::fmod (phase + 0.5, 1.0), inc);
            v = lp.processLowpass (sq, lpCoeffs);
            break;
        }
        case PewPreset::Osc::fm:
        {
            modPhase += inc * fmRatio;
            modPhase -= std::floor (modPhase);
            v = std::sin (juce::MathConstants<double>::twoPi * phase + fmIndex * std::sin (juce::MathConstants<double>::twoPi * modPhase));
            fmIndex *= fmCoeff;
            break;
        }
    }

    // Amplitude: 1 ms attack, then e^(-t / decay)
    const double a = env * juce::jmin (1.0, (double) (n + 1) / attackLen);
    env *= decayCoeff;

    // Laser click: high-passed noise, 4 ms, only when the knob is up
    double click = 0.0;
    if (clickGain > 0.0f && n < clickLen)
    {
        const double e = 1.0 - (double) n / clickLen;
        click = clickHp.processHighpass (rng.bipolar(), clickCoeffs) * e * e * clickGain;
    }

    float y = gain * (float) (v * a + click);
    ++n;

    // -60 dB: 5 ms fade, then the voice is free
    if (releaseLeft < 0 && env < 0.001)
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
double PewPewDsp::divisionBeats (int index) noexcept
{
    switch (index)
    {
        case 0: return 0.25; // 1/16
        case 1: return 0.75; // dotted 1/8
        case 2: return 0.5;  // 1/8
        default: return 1.0; // 1/4
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
    for (auto* s : { &roomMix, &echoMix, &amountForVoices, &feedback })
        s->prepare (sampleRate, smoothSeconds);

    for (auto& v : voices)
        v = PewVoice();
    for (auto& v : tails)
        v = PewVoice();
    clearEffects();
    soundRecently = false;
    silentSamples = std::int64_t (1) << 40;
    panicLeft = -1;
    for (auto* s : { &roomMix, &echoMix, &amountForVoices, &feedback })
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
        clearEffects();
    }
}

void PewPewDsp::panic()
{
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
}

void PewPewDsp::noteOn (int note, float velocity, float amountNow)
{
    PewVoice* target = nullptr;
    for (auto& v : voices)
        if (! v.isActive())
        {
            target = &v;
            break;
        }
    if (target == nullptr)
    {
        // Steal the oldest; it keeps sounding in a tail slot, faded over 2 ms
        target = &voices[0];
        for (auto& v : voices)
            if (v.getOrder() < target->getOrder())
                target = &v;
        PewVoice* tail = &tails[0];
        for (auto& t : tails)
            if (! t.isActive() || t.getOrder() < tail->getOrder())
            {
                tail = &t;
                if (! t.isActive())
                    break;
            }
        *tail = *target;
        tail->fadeOut ((int) std::round (stealFadeSeconds * sampleRate));
    }
    PewVoice::Start s;
    s.preset = &presets[(size_t) presetIndex];
    s.note = note;
    s.velocity = velocity;
    s.amount = amountNow;
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
        float s = 0.0f;
        for (auto& v : voices)
            s += v.render();
        for (auto& v : tails)
            s += v.render();
        out[i] = s;
    }
}

void PewPewDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing)
{
    juce::ScopedNoDenormals noDenormals;
    const auto& p = presets[(size_t) presetIndex];

    if (appliedReverbPreset != presetIndex)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = p.roomSize;
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
    if (midi != nullptr)
    {
        for (const auto meta : *midi)
        {
            const int at = meta.samplePosition - midiStart;
            if (at < 0)
                continue;
            if (at >= midiCount || at >= numSamples)
                break;
            advanceTo (at);
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) // (velocity 0 counts as a note-off: ignored, the pew is a one-shot)
            {
                noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), amountForVoices.getCurrent());
                telemetry.lastTriggerSample.store (telemetry.sampleClock.load (std::memory_order_relaxed) + at, std::memory_order_relaxed);
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                for (auto& v : voices)
                    v.fadeOut (panicLen);
                for (auto& v : tails)
                    v.fadeOut (panicLen);
            }
        }
        midi = nullptr;
    }
    advanceTo (numSamples);

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
            feedback.setTarget (p.delayFeedback);
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
        lineR[(size_t) delayWrite] = 0.4f * x + fb * (float) echoLpR.processLowpass (dl, echoLp);
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
        const float outL = x + rm * rl[i] + em * dl;
        const float outR = x + rm * rr[i] + em * dr;
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
