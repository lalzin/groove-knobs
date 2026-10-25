#include "BoogieTrioDsp.h"

namespace bt
{
using namespace gk::dspmath;

BoogieTrioPreset BoogieTrioPreset::fromJson (const juce::var& root)
{
    BoogieTrioPreset p;
    auto num = [] (const juce::var& section, const char* key, float fallback)
    {
        const auto v = section.getProperty (key, fallback);
        return v.isDouble() || v.isInt() || v.isInt64() ? (float) (double) v : fallback;
    };
    const auto voices = root.getProperty ("voices", {});
    p.baseDelayMs = juce::jlimit (3.0f, 20.0f, num (voices, "baseDelayMs", p.baseDelayMs));
    p.chorusHz = juce::jlimit (0.02f, 5.0f, num (voices, "chorusHz", p.chorusHz));
    p.chorusDepthMs = juce::jlimit (0.0f, 6.0f, num (voices, "chorusDepthMs", p.chorusDepthMs));
    p.vibratoHz = juce::jlimit (0.0f, 12.0f, num (voices, "vibratoHz", p.vibratoHz));
    p.vibratoDepthMs = juce::jlimit (0.0f, 1.0f, num (voices, "vibratoDepthMs", p.vibratoDepthMs));
    const auto mixSection = root.getProperty ("mix", {});
    p.maxMix = juce::jlimit (0.0f, 1.0f, num (mixSection, "maxMix", p.maxMix));
    p.wetHpfHz = juce::jlimit (20.0f, 1000.0f, num (mixSection, "wetHpfHz", p.wetHpfHz));

    const auto fx = root.getProperty ("fx", {});
    p.fxStart = juce::jlimit (0.0f, 0.9f, num (fx, "start", p.fxStart));
    p.feedback = juce::jlimit (-0.75f, 0.75f, num (fx, "feedback", 0.0f));
    p.driftMs = juce::jlimit (0.0f, 4.0f, num (fx, "driftMs", 0.0f));
    const auto trem = fx.getProperty ("tremolo", {});
    p.tremoloDepth = juce::jlimit (0.0f, 1.0f, num (trem, "depth", 0.0f));
    p.tremoloBeats = juce::jlimit (0.0625f, 8.0f, num (trem, "beats", p.tremoloBeats));
    p.tremoloStereoDeg = juce::jlimit (0.0f, 180.0f, num (trem, "stereoDeg", p.tremoloStereoDeg));
    const auto gate = fx.getProperty ("gate", {});
    p.gateDepth = juce::jlimit (0.0f, 1.0f, num (gate, "depth", 0.0f));
    p.gateStepsPerBeat = juce::jlimit (1.0f, 8.0f, num (gate, "stepsPerBeat", p.gateStepsPerBeat));
    p.gateDuty = juce::jlimit (0.2f, 0.9f, num (gate, "duty", p.gateDuty));
    const auto room = fx.getProperty ("room", {});
    p.roomMix = juce::jlimit (0.0f, 1.0f, num (room, "mix", 0.0f));
    p.roomSize = juce::jlimit (0.0f, 1.0f, num (room, "size", p.roomSize));
    p.roomDamping = juce::jlimit (0.0f, 1.0f, num (room, "damping", p.roomDamping));
    p.toneDb = juce::jlimit (-9.0f, 9.0f, num (fx, "toneDb", 0.0f));
    const auto ph = fx.getProperty ("phaser", {});
    p.phaserMix = juce::jlimit (0.0f, 1.0f, num (ph, "mix", 0.0f));
    p.phaserBars = juce::jlimit (0.25f, 16.0f, num (ph, "bars", p.phaserBars));
    p.phaserFeedback = juce::jlimit (0.0f, 0.85f, num (ph, "feedback", p.phaserFeedback));
    return p;
}

BoogieTrioTargets BoogieTrioDsp::computeTargets (const BoogieTrioPreset& p, float amount) noexcept
{
    amount = clamp01 (amount);
    BoogieTrioTargets t;
    t.chorusDepthMs = p.chorusDepthMs * std::pow (amount, 0.8f);
    t.mix = p.maxMix * smoothstep (0.0f, 0.6f, amount);
    t.width = smoothstep (0.2f, 0.8f, amount);
    t.vibratoDepthMs = p.vibratoDepthMs * smoothstep (0.5f, 1.0f, amount);
    t.fx = smoothstep (p.fxStart, 1.0f, amount);
    return t;
}

void BoogieTrioDsp::setPresets (const gk::PresetManager& pm)
{
    presets.clear();
    for (int i = 0; i < pm.size(); ++i)
        presets.push_back (BoogieTrioPreset::fromJson (pm[i].json));
    if (presets.empty())
        presets.emplace_back();
}

void BoogieTrioDsp::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = juce::jlimit (1, maxChannels, newNumChannels);
    line.prepare (sampleRate, 0.05);

    for (auto* s : { &baseDelayMs, &chorusDepthMs, &vibratoDepthMs, &chorusHz, &vibratoHz, &mix, &width, &hpfLogHz, &fxAmount, &feedback, &drift,
                     &tremolo, &gate, &roomMix, &toneDb, &phaserMix })
        s->prepare (sampleRate, 0.030);
    toneCoeffs = gk::TptSvf::Coeffs::make (1500.0, sampleRate, 0.7071);
    room.setSampleRate (sampleRate);
    wetBuffer.setSize (maxChannels, juce::jmax (1, maxBlockSize), false, true, false);
    highBuffer.setSize (maxChannels, juce::jmax (1, maxBlockSize), false, true, false);
    roomBuffer.setSize (maxChannels, juce::jmax (1, maxBlockSize), false, true, false);

    inputEnv.prepare (sampleRate, 0.005, 0.2);
    saturator.set (dbToGain (vintageDriveDb), 0.0f);
    lpfCoeffs = gk::TptSvf::Coeffs::make (juce::jmin ((double) vintageLowpassHz, 0.45 * sampleRate), sampleRate, 0.7071);
    hissGain = dbToGain (hissDbfs);
    reset();
}

void BoogieTrioDsp::reset()
{
    line.reset();
    for (auto& f : wetHpf)
        f.reset();
    for (auto& f : wetHpf2)
        f.reset();
    for (auto& f : vintageLpf)
        f.reset();
    hiss[0].reset (101u);
    hiss[1].reset (202u);
    inputEnv.reset();
    for (auto* s : { &baseDelayMs, &chorusDepthMs, &vibratoDepthMs, &chorusHz, &vibratoHz, &mix, &width, &hpfLogHz, &fxAmount, &feedback, &drift,
                     &tremolo, &gate, &roomMix, &toneDb, &phaserMix })
        s->invalidate();
    for (auto& f : toneSplit)
        f.reset();
    for (auto& ph : phaser)
        ph.reset();
    room.reset();
    roomPreset = -1;
    feedbackSample = 0.0f;
    driftState = {};
    driftTarget = {};
    driftCountdown = 0;
    driftRng = gk::testsignals::Rng (4242u);
    counter = 0;
    // LFO phases keep running (free-running LFOs, as on the hardware).
}

void BoogieTrioDsp::setToggleValues (const float* const* perSample, int numToggles)
{
    numToggleData = juce::jmin (2, numToggles);
    for (int t = 0; t < numToggleData; ++t)
        toggleData[t] = perSample[t];
}

void BoogieTrioDsp::setKnobs (const float* values, int n)
{
    for (int i = 0; i < juce::jmin (n, (int) numKnobs); ++i)
        knobs[(size_t) i] = clamp01 (values[i]);
}

void BoogieTrioDsp::controlTick (float amount) noexcept
{
    const auto& p = presets[(size_t) presetIndex];
    const auto t = computeTargets (p, amount);
    const auto k = [this] (Knob id) { return knobs[(size_t) id]; };
    // What the panel adds comes in with the chorus mix (knob 0: nothing)
    const float ramp = smoothstep (0.0f, 0.6f, clamp01 (amount));

    // VOICES: LFO rate x0.35 .. x2.8, chorus depth x0 .. x2, vibrato x0 .. x1
    // then up to +0.5 ms more (6 Hz if the preset has none), tape drift
    const float rate = std::exp2 (3.0f * (k (rateKnob) - 0.5f));
    const float vib = 2.0f * k (vibratoKnob);
    const float vibExtra = juce::jmax (0.0f, vib - 1.0f) * 0.5f * ramp;
    const float depth = juce::jmin (t.chorusDepthMs * 2.0f * k (depthKnob), p.baseDelayMs - 0.6f);
    baseDelayMs.setTarget (p.baseDelayMs);
    chorusDepthMs.setTarget (juce::jmax (0.0f, depth));
    vibratoDepthMs.setTarget (t.vibratoDepthMs * juce::jmin (1.0f, vib) + vibExtra);
    chorusHz.setTarget (p.chorusHz * rate);
    vibratoHz.setTarget ((p.vibratoHz > 0.1f ? p.vibratoHz : 6.0f) * rate);
    mix.setTarget (juce::jmin (1.0f, t.mix * 2.0f * k (mixKnob)));
    width.setTarget (t.width * 2.0f * k (widthKnob));
    // COLOUR: wet low cut x0.35 .. x2.8, tone +-6 dB, feedback, room
    hpfLogHz.setTarget (std::log (juce::jlimit (40.0f, 1500.0f, p.wetHpfHz * std::exp2 (3.0f * (k (lowCutKnob) - 0.5f)))));

    fxAmount.setTarget (t.fx);
    const float fbSign = p.feedback < 0.0f ? -1.0f : 1.0f;
    feedback.setTarget (juce::jlimit (-0.8f, 0.8f, p.feedback * t.fx + fbSign * 0.6f * k (feedbackKnob) * ramp));
    drift.setTarget (juce::jmin (4.0f, p.driftMs * t.fx + 2.0f * k (driftKnob) * ramp));
    // MOTION: tremolo / autopan and phaser at the preset's rates
    tremolo.setTarget (juce::jmin (1.0f, p.tremoloDepth * t.fx + 0.7f * k (tremoloKnob) * ramp));
    gate.setTarget (p.gateDepth * t.fx);
    roomMix.setTarget (juce::jmin (1.0f, p.roomMix * t.fx + 0.6f * k (spaceKnob) * ramp));
    toneDb.setTarget (juce::jlimit (-12.0f, 12.0f, p.toneDb * t.fx + 12.0f * (k (toneKnob) - 0.5f) * ramp));
    phaserMix.setTarget (juce::jmin (1.0f, p.phaserMix * t.fx + 0.7f * k (phaserKnob) * ramp));
}

void BoogieTrioDsp::applyRoomSettings() noexcept
{
    const auto& p = presets[(size_t) presetIndex];
    juce::Reverb::Parameters rp;
    rp.roomSize = p.roomSize;
    rp.damping = p.roomDamping;
    rp.width = 1.0f;
    rp.wetLevel = 1.0f;
    rp.dryLevel = 0.0f;
    room.setParameters (rp);
    roomPreset = presetIndex;
}

void BoogieTrioDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing)
{
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    const double msToSamples = sampleRate * 0.001;
    const double twoPi = 2.0 * pi;
    const auto& preset = presets[(size_t) presetIndex];
    if (roomPreset != presetIndex)
        applyRoomSettings();

    // Pass 1: voices, colour, tone and phaser into the wet buffer
    for (int i = 0; i < numSamples; ++i)
    {
        if ((counter++ % (std::uint32_t) controlInterval) == 0)
            controlTick (amount[i]);

        const float vintage = numToggleData > vintageToggle ? toggleData[vintageToggle][i] : 0.0f;
        const float wide = numToggleData > wideToggle ? toggleData[wideToggle][i] : 0.0f;

        // Smoothed parameters (2-pole, 30 ms)
        const float base = baseDelayMs.next(), cDepth = chorusDepthMs.next(), vDepth = vibratoDepthMs.next();
        const float w = width.next();
        const double hpfHz = std::exp ((double) hpfLogHz.next());
        // 4th-order Butterworth (24 dB/oct): the bass stays dry (DECISIONS.md)
        hpfCoeffs = gk::TptSvf::Coeffs::make (hpfHz, sampleRate, 0.5412);
        hpfCoeffs2 = gk::TptSvf::Coeffs::make (hpfHz, sampleRate, 1.3066);
        const float fb = feedback.next(), driftMs = drift.next(), tone = toneDb.next(), phMix = phaserMix.next();

        // Free-running LFOs
        chorusPhase = frac (chorusPhase + chorusHz.next() / sampleRate);
        vibratoPhase = frac (vibratoPhase + vibratoHz.next() / sampleRate);

        // Tape wow: each voice drifts towards a new random offset every ~0.35 s
        if (--driftCountdown <= 0)
        {
            driftCountdown = (int) (0.35 * sampleRate);
            for (auto& t : driftTarget)
                t = driftRng.bipolar();
        }
        const float driftCoeff = (float) (1.0 - std::exp (-1.0 / (0.25 * sampleRate)));
        for (int v = 0; v < numVoices; ++v)
            driftState[(size_t) v] += (driftTarget[(size_t) v] - driftState[(size_t) v]) * driftCoeff;

        // Wet send: high-passed, summed to mono (+ feedback for the jet colour)
        std::array<float, maxChannels> x {}, high {};
        float mono = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            x[(size_t) c] = buffer.getReadPointer (c)[i];
            high[(size_t) c] = (float) wetHpf2[(size_t) c].processHighpass (wetHpf[(size_t) c].processHighpass (x[(size_t) c], hpfCoeffs), hpfCoeffs2);
            mono += high[(size_t) c];
        }
        mono /= (float) nch;
        line.push (mono + fb * feedbackSample);

        float inMono = 0.0f;
        for (int c = 0; c < nch; ++c)
            inMono += x[(size_t) c];
        const float env = inputEnv.process (inMono / (float) nch);

        // Three voices: same line, three modulated taps (0°, 120°, 240°)
        std::array<float, numVoices> y {};
        for (int v = 0; v < numVoices; ++v)
        {
            const double offset = (double) v / numVoices;
            const double delayMs = base + voiceOffsetMs[v] + cDepth * std::sin (twoPi * (chorusPhase + offset))
                                   + vDepth * std::sin (twoPi * (vibratoPhase + offset)) + driftMs * driftState[(size_t) v];
            y[(size_t) v] = line.read (juce::jmax (1.0, delayMs * msToSamples));
        }
        feedbackSample = juce::jlimit (-2.0f, 2.0f, y[1]);

        // Stereo: "Wider" style — the centre stays the dry signal, the chorus
        // lives in the side (+ on the left, - on the right), so L + R gives
        // back the input exactly (mono-safe, centre phase untouched).
        // Mono output: classic chorus (average of the three voices).
        std::array<float, maxChannels> wet {};
        if (nch == 2)
            wet[0] = sideGain * w * lerp (narrowSide, 1.0f, wide) * (0.5f * (y[0] - y[2]) + 0.35f * (y[1] - mono));
        else
            wet[0] = (y[0] + y[1] + y[2]) / (float) numVoices;
        const int wetChannels = 1;

        const double beats = timing.beatsAt (i);
        for (int c = 0; c < wetChannels; ++c)
        {
            float wv = wet[(size_t) c];
            if (vintage > 0.0f)
            {
                // BBD colour: 7 kHz low-pass, soft symmetric tanh, hiss that
                // follows the input (silence stays silent)
                const float coloured = saturator.process ((float) vintageLpf[(size_t) c].processLowpass (wv, lpfCoeffs))
                                       + (hissEnabled ? hiss[(size_t) c].next() * hissGain * juce::jmin (1.0f, env * 10.0f) : 0.0f);
                wv += vintage * (coloured - wv);
            }
            else
                vintageLpf[(size_t) c].reset(); // no stale state when VINTAGE fades back in

            // Tone: high shelf above ~1.5 kHz (bright or dark wet)
            const float lowPart = (float) toneSplit[(size_t) c].processLowpass (wv, toneCoeffs);
            wv = lowPart + dbToGain (tone) * (wv - lowPart);

            // Phaser on the wet, locked to the bar
            auto& ph = phaser[(size_t) c];
            const double phPhase = gk::lfo::phaseFromBeats (beats, preset.phaserBars, c == 1 ? 0.25 : 0.0);
            ph.setFrequency (logLerp (300.0f, 3500.0f, gk::lfo::raisedCosine (phPhase)), sampleRate);
            ph.setFeedback (preset.phaserFeedback);
            wv = ph.process (wv, phMix);

            wetBuffer.getWritePointer (c)[i] = wv;
        }
        for (int c = 0; c < nch; ++c)
            highBuffer.getWritePointer (c)[i] = high[(size_t) c];
    }

    // Reverb on the wet (block, always running: no onset clicks). In stereo
    // it is fed with the side and only its L - R part is used (mono-safe).
    roomBuffer.copyFrom (0, 0, wetBuffer, 0, 0, numSamples);
    if (nch == 2)
    {
        roomBuffer.copyFrom (1, 0, wetBuffer, 0, 0, numSamples);
        roomBuffer.applyGain (1, 0, numSamples, -1.0f);
        room.processStereo (roomBuffer.getWritePointer (0), roomBuffer.getWritePointer (1), numSamples);
        for (int i = 0; i < numSamples; ++i)
            roomBuffer.getWritePointer (0)[i] = 0.5f * (roomBuffer.getReadPointer (0)[i] - roomBuffer.getReadPointer (1)[i]);
    }
    else
        room.processMono (roomBuffer.getWritePointer (0), numSamples);

    // Pass 2: mix, tremolo / gate on everything above the high-pass
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mix.next(), rm = roomMix.next(), tremDepth = tremolo.next(), gateDepth = gate.next();
        const double beats = timing.beatsAt (i);
        // Gate: on for `duty` of each step, soft edges (no clicks)
        const double stepPhase = frac (beats * preset.gateStepsPerBeat);
        const float edge = 0.08f;
        const float open = smoothstep (0.0f, edge, (float) stepPhase) * (1.0f - smoothstep (preset.gateDuty, preset.gateDuty + edge, (float) stepPhase));
        const float gateGain = 1.0f - gateDepth * (1.0f - open);

        const float wetSample = wetBuffer.getReadPointer (0)[i] + rm * 0.5f * roomBuffer.getReadPointer (0)[i];
        for (int c = 0; c < nch; ++c)
        {
            const float x = buffer.getReadPointer (c)[i];
            const float high = highBuffer.getReadPointer (c)[i];
            // Stereo: centre untouched, ± side. Mono: crossfade to the chorus.
            const float processedHigh = nch == 2 ? high + (c == 0 ? 1.0f : -1.0f) * m * wetSample : high + m * (wetSample - high);
            // Tremolo / autopan: raised-cosine, offset between channels
            const double tremPhase = frac (beats / preset.tremoloBeats + (c == 1 ? preset.tremoloStereoDeg / 360.0 : 0.0));
            const float tremGain = 1.0f - tremDepth * (float) (0.5 - 0.5 * std::cos (twoPi * tremPhase));
            buffer.getWritePointer (c)[i] = (x - high) + processedHigh * tremGain * gateGain;
        }
    }

    modPhase.store ((float) chorusPhase, std::memory_order_relaxed);
    vibPhaseUi.store ((float) vibratoPhase, std::memory_order_relaxed);
}
} // namespace bt
