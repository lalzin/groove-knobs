#include "FrenchFilterDsp.h"

namespace ff
{
using namespace gk::dspmath;

FrenchFilterPreset FrenchFilterPreset::fromJson (const juce::var& root)
{
    FrenchFilterPreset p;
    p.low = FrenchFilterSide::fromJson (root.getProperty ("low", {}), FrenchFilterSide::Mode::lowpass);
    p.high = FrenchFilterSide::fromJson (root.getProperty ("high", {}), FrenchFilterSide::Mode::highpass);
    return p;
}

FrenchFilterSide FrenchFilterSide::fromJson (const juce::var& root, Mode mode)
{
    FrenchFilterSide p;
    p.mode = mode;
    if (const auto* arr = root.getProperty ("tags", {}).getArray())
        for (const auto& t : *arr)
            p.tags.add (t.toString());
    auto num = [] (const juce::var& section, const char* key, float fallback)
    {
        const auto v = section.getProperty (key, fallback);
        return v.isDouble() || v.isInt() || v.isInt64() ? (float) (double) v : fallback;
    };

    const auto filter = root.getProperty ("filter", {});
    p.slopeDb = num (filter, "slope", 24.0f) < 18.0f ? 12.0f : 24.0f;
    // "endHz" (cutoff at 100 %); "cutoffMinHz" kept for the first preset format.
    p.endHz = juce::jlimit (20.0f, 16000.0f, num (filter, "endHz", mode == Mode::lowpass ? 180.0f : 900.0f));
    p.curve = juce::jlimit (0.2f, 4.0f, num (filter, "curve", p.curve));
    p.qMax = juce::jlimit (0.5f, 12.0f, num (filter, "qMax", p.qMax));
    p.resPeak = juce::jlimit (0.05f, 0.95f, num (filter, "resPeak", p.resPeak));
    p.growl = juce::jlimit (0.0f, 1.0f, num (filter, "growl", p.growl));

    const auto drive = root.getProperty ("drive", {});
    p.driveMaxDb = juce::jlimit (0.0f, 24.0f, num (drive, "maxDb", p.driveMaxDb));
    p.asymmetry = juce::jlimit (-0.5f, 0.5f, num (drive, "asymmetry", p.asymmetry));

    const auto env = root.getProperty ("envelope", {});
    p.envOctaves = juce::jlimit (0.0f, 5.0f, num (env, "octaves", 0.0f));

    const auto grit = root.getProperty ("grit", {});
    p.gritBits = juce::jlimit (4.0f, 24.0f, num (grit, "bits", 24.0f));
    p.gritRateHz = juce::jlimit (0.0f, 48000.0f, num (grit, "rateHz", 0.0f));
    p.gritStart = juce::jlimit (0.0f, 0.95f, num (grit, "start", 0.3f));

    const auto pump = root.getProperty ("pump", {});
    p.pumpDepthDb = juce::jlimit (0.0f, 18.0f, num (pump, "depthDb", 0.0f));
    p.pumpReleaseBeats = juce::jlimit (0.1f, 2.0f, num (pump, "releaseBeats", 0.5f));

    const auto room = root.getProperty ("room", {});
    p.roomMix = juce::jlimit (0.0f, 1.0f, num (room, "mix", 0.0f));
    p.roomSize = juce::jlimit (0.0f, 1.0f, num (room, "size", 0.5f));
    p.roomDamping = juce::jlimit (0.0f, 1.0f, num (room, "damping", 0.5f));
    p.roomStart = juce::jlimit (0.0f, 0.95f, num (room, "start", 0.3f));
    p.roomLevel = juce::jlimit (0.0f, 1.5f, num (room, "level", 0.45f));
    p.filterOnRoom = filter.getProperty ("target", "signal").toString().equalsIgnoreCase ("room");

    const auto phaser = root.getProperty ("phaser", {});
    p.phaserMaxMix = juce::jlimit (0.0f, 1.0f, num (phaser, "maxMix", p.phaserMaxMix));
    p.phaserBars = juce::jlimit (0.25f, 16.0f, num (phaser, "bars", p.phaserBars));
    p.phaserFeedback = juce::jlimit (-0.9f, 0.9f, num (phaser, "feedback", p.phaserFeedback));
    p.phaserMinHz = juce::jlimit (20.0f, 10000.0f, num (phaser, "minHz", p.phaserMinHz));
    p.phaserMaxHz = juce::jlimit (p.phaserMinHz, 16000.0f, num (phaser, "maxHz", p.phaserMaxHz));
    p.stereoPhaseDeg = juce::jlimit (-180.0f, 180.0f, num (phaser, "stereoPhaseDeg", p.stereoPhaseDeg));
    return p;
}

FrenchFilterTargets FrenchFilterDsp::computeTargets (const FrenchFilterPreset& preset, float macro) noexcept
{
    const auto& p = preset.sideFor (macro);
    const float amount = clamp01 (std::abs (macro));
    FrenchFilterTargets t;
    const bool lp = p.mode == FrenchFilterSide::Mode::lowpass;
    t.lowpassWeight = lp ? 1.0f : 0.0f;
    t.slope24 = p.slopeDb > 18.0f ? 1.0f : 0.0f;
    t.lowpassHz = lp ? gk::macro::logSweep (openLowpassHz, p.endHz, amount, p.curve) : openLowpassHz;
    t.highpassHz = lp ? openHighpassHz : gk::macro::logSweep (openHighpassHz, p.endHz, amount, p.curve);
    t.roomLowpassHz = openLowpassHz;
    t.roomHighpassHz = openHighpassHz;
    if (p.filterOnRoom)
    {
        // The sweep moves to the reverb send; the signal path stays open.
        t.roomLowpassHz = t.lowpassHz;
        t.roomHighpassHz = t.highpassHz;
        t.lowpassHz = openLowpassHz;
        t.highpassHz = openHighpassHz;
    }
    t.resonance = gk::macro::peakThenRelease (amount, p.resPeak, resonanceRelease);
    t.q2 = q2Base + t.resonance * (p.qMax - q2Base);
    t.driveDb = gk::macro::power (amount, p.driveMaxDb, driveExponent);
    t.phaserMix = p.phaserMaxMix * gk::macro::lateOnset (amount, phaserOnset);
    t.envOctaves = p.envOctaves * smoothstep (0.05f, 0.5f, amount);
    t.grit = p.hasGrit() ? smoothstep (p.gritStart, 1.0f, amount) : 0.0f;
    t.pumpDb = p.pumpDepthDb * smoothstep (0.1f, 0.9f, amount);
    t.room = p.roomMix * smoothstep (p.roomStart, 1.0f, amount);
    t.roomLevel = p.roomLevel;
    return t;
}

void FrenchFilterDsp::setPresets (const gk::PresetManager& pm)
{
    presets.clear();
    for (int i = 0; i < pm.size(); ++i)
        presets.push_back (FrenchFilterPreset::fromJson (pm[i].json));
    if (presets.empty())
        presets.emplace_back();
}

void FrenchFilterDsp::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = juce::jlimit (1, maxChannels, newNumChannels);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) juce::jmax (1, maxBlockSize));
    latency = juce::roundToInt (oversampler->getLatencyInSamples());
    osRate = sampleRate * (double) oversampler->getOversamplingFactor();

    const double tickRate = osRate / controlInterval;
    for (auto* s : { &lpLogHz, &hpLogHz, &q24, &q12, &envDepth })
        s->prepare (tickRate, 0.030);
    for (auto* s : { &driveGain, &asymmetry, &growl, &phaserMix, &phaserFeedback, &lowpassWeight, &slope24, &gritAmount, &gritBits,
                     &gritLogRatio, &pumpDb })
        s->prepare (osRate, 0.030);
    for (auto* s : { &roomMix, &roomLevel, &roomLpLogHz, &roomHpLogHz })
        s->prepare (sampleRate, 0.030);

    for (auto& ch : channels)
    {
        ch.dc.prepare (osRate, 10.0);
        ch.phaserLogHz.prepare (tickRate, 0.015);
    }
    squelchEnv.prepare (osRate, 0.003, 0.14);
    pump.prepare (osRate);

    room.setSampleRate (sampleRate);
    roomBuffer.setSize (numChannels, juce::jmax (1, maxBlockSize), false, true, false);
    roomMixBuffer.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    roomLevelBuffer.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    reset();
}

void FrenchFilterDsp::reset()
{
    if (oversampler != nullptr)
        oversampler->reset();
    for (auto& ch : channels)
        ch.reset();
    for (auto* s : { &lpLogHz, &hpLogHz, &q24, &q12, &envDepth, &driveGain, &asymmetry, &growl, &phaserMix, &phaserFeedback, &lowpassWeight,
                     &slope24, &gritAmount, &gritBits, &gritLogRatio, &pumpDb, &roomMix, &roomLevel, &roomLpLogHz, &roomHpLogHz })
        s->invalidate();
    for (auto& f : roomLp)
        f.reset();
    for (auto& f : roomHp)
        f.reset();
    for (auto* r : { &lpG, &hpG, &k24, &k12 })
        r->invalidate();
    squelchEnv.reset();
    pump.reset();
    room.reset();
    roomSide = nullptr;
    osCounter = 0;
    baseCounter = 0;
}

void FrenchFilterDsp::setPreset (int index)
{
    presetIndex = juce::jlimit (0, (int) presets.size() - 1, index);
}

void FrenchFilterDsp::applyRoomSettings (const FrenchFilterSide& p) noexcept
{
    juce::Reverb::Parameters rp;
    rp.roomSize = p.roomSize;
    rp.damping = p.roomDamping;
    rp.width = 0.8f;
    rp.wetLevel = 1.0f;
    rp.dryLevel = 0.0f;
    rp.freezeMode = 0.0f;
    room.setParameters (rp);
    roomSide = &p;
}

void FrenchFilterDsp::controlTick (float macro, double beats) noexcept
{
    const auto& p = presets[(size_t) presetIndex].sideFor (macro);
    const auto t = computeTargets (presets[(size_t) presetIndex], macro);

    lpLogHz.setTarget (std::log (t.lowpassHz));
    hpLogHz.setTarget (std::log (t.highpassHz));
    q24.setTarget (t.q2);
    q12.setTarget (q12Base + t.resonance * (0.6f * p.qMax - q12Base));
    envDepth.setTarget (t.envOctaves);

    driveGain.setTarget (driveHeadroom * dbToGain (t.driveDb));
    asymmetry.setTarget (p.asymmetry);
    growl.setTarget (p.growl);
    phaserMix.setTarget (t.phaserMix);
    phaserFeedback.setTarget (p.phaserFeedback);
    lowpassWeight.setTarget (t.lowpassWeight);
    slope24.setTarget (t.slope24);
    gritAmount.setTarget (t.grit);
    if (p.hasGrit())
    {
        gritBits.setTarget (p.gritBits);
        gritLogRatio.setTarget (std::log (juce::jlimit (1.0e-3f, 1.0f, (float) (p.gritRateHz / osRate))));
    }
    pumpDb.setTarget (t.pumpDb);

    // Squelch: the input envelope pushes the cutoff up (in octaves)
    const float e = clamp01 ((gainToDb (squelchEnv.get()) + 36.0f) / 30.0f);
    const double envShift = (double) envDepth.next() * e * 0.69314718;
    const double lpHz = std::exp ((double) lpLogHz.next() + envShift);
    const double hpHz = std::exp ((double) hpLogHz.next() + envShift);
    const double maxHz = maxCutoffRatio * osRate;
    lpG.retarget (gk::TptSvf::Coeffs::prewarp (juce::jlimit (20.0, maxHz, lpHz), osRate), controlInterval);
    hpG.retarget (gk::TptSvf::Coeffs::prewarp (juce::jlimit (10.0, maxHz, hpHz), osRate), controlInterval);
    k24.retarget (1.0 / q24.next(), controlInterval);
    k12.retarget (1.0 / q12.next(), controlInterval);

    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels[(size_t) c];
        const double offset = c == 1 ? p.stereoPhaseDeg / 360.0 : 0.0;
        const double phase = gk::lfo::phaseFromBeats (beats, p.phaserBars, offset);
        if (c == 0)
            modPhase.store ((float) phase, std::memory_order_relaxed);
        const float hz = logLerp (p.phaserMinHz, p.phaserMaxHz, gk::lfo::raisedCosine (phase));
        ch.phaserLogHz.setTarget (std::log (hz));
        ch.phaserGain.retarget (gk::AllpassPhaser<4>::gainForFrequency (std::exp (ch.phaserLogHz.next()), osRate), controlInterval);
    }
}

void FrenchFilterDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing)
{
    jassert (oversampler != nullptr);
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    const auto& preset = presets[(size_t) presetIndex];
    const auto& side = preset.sideFor (amount[numSamples / 2]);
    pump.setRelease (timing.bpm, side.pumpReleaseBeats);

    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) nch, (size_t) numSamples);
    auto os = oversampler->processSamplesUp (block);
    const int osN = (int) os.getNumSamples();
    const int factor = (int) oversampler->getOversamplingFactor();
    const float monoNorm = 1.0f / (float) nch;

    std::array<float*, maxChannels> ptr {};
    for (int c = 0; c < nch; ++c)
        ptr[(size_t) c] = os.getChannelPointer ((size_t) c);

    for (int j = 0; j < osN; ++j)
    {
        // Stereo-linked detectors, fed with the input
        float mono = 0.0f;
        for (int c = 0; c < nch; ++c)
            mono += ptr[(size_t) c][j];
        mono *= monoNorm;
        squelchEnv.process (mono);
        const float pumpGain = pump.process (mono, pumpDb.next());

        if ((osCounter++ % (std::uint32_t) controlInterval) == 0)
        {
            const int i = j / factor;
            const double beats = timing.beatsAtStart + ((double) j / factor) * timing.beatsPerSample;
            controlTick (amount[i], beats);
        }

        // Per-sample interpolated coefficients and smoothed gains
        const double g = lpG.next(), hg = hpG.next(), kk24 = k24.next(), kk12 = k12.next();
        const auto c1 = gk::TptSvf::Coeffs::fromGK (g, 1.0 / q1);
        const auto c2 = gk::TptSvf::Coeffs::fromGK (g, kk24);
        const auto c12 = gk::TptSvf::Coeffs::fromGK (g, kk12);
        const auto h1 = gk::TptSvf::Coeffs::fromGK (hg, 1.0 / q1);
        const auto h2 = gk::TptSvf::Coeffs::fromGK (hg, kk24);
        const auto h12 = gk::TptSvf::Coeffs::fromGK (hg, kk12);
        saturator.set (driveGain.next(), asymmetry.next());
        const float gr = growl.next();
        const double growlLimit = gr > 1.0e-3f ? 1.0 / gr : 0.0;
        const float mix = phaserMix.next(), fb = phaserFeedback.next();
        const float lw = lowpassWeight.next(), s24 = slope24.next();
        // Grit: a fixed sampler rate / bit depth per preset, blended in with
        // the knob (sweeping the hold rate itself crackles at low settings).
        const float gritAmt = gritAmount.next();
        const float bits = gritBits.next();
        const double holdRatio = std::exp ((double) gritLogRatio.next());

        if (passthrough)
            continue;

        for (int c = 0; c < nch; ++c)
        {
            auto& ch = channels[(size_t) c];
            ch.phaser.setGain (ch.phaserGain.next());
            ch.phaser.setFeedback (fb);

            float x = saturator.process (ptr[(size_t) c][j]);
            // Always run the sampler so its held value is never stale.
            const float crushed = ch.grit.process (x, holdRatio, bits);
            if (gritAmt > 0.0f && gritEnabled)
                x += gritAmt * (crushed - x);
            x = ch.dc.process (x);

            // Low-pass: 24 dB (2 stages) morphing with 12 dB (1 stage)
            const double l24 = ch.lp2.processLowpass (ch.lp1.processLowpass (x, c1), c2);
            ch.lp2.saturateResonance (growlLimit);
            const double l12 = ch.lp12.processLowpass (x, c12);
            ch.lp12.saturateResonance (growlLimit);
            const double lowpass = l12 + s24 * (l24 - l12);

            // High-pass: 24 dB morphing with 12 dB
            const double h24 = ch.hp2.processHighpass (ch.hp1.processHighpass (x, h1), h2);
            ch.hp2.saturateResonance (growlLimit);
            const double hh12 = ch.hp12.processHighpass (x, h12);
            ch.hp12.saturateResonance (growlLimit);
            const double highpass = hh12 + s24 * (h24 - hh12);

            const double y = highpass + lw * (lowpass - highpass);
            ptr[(size_t) c][j] = ch.phaser.process ((float) y, mix) * pumpGain;
        }
    }

    oversampler->processSamplesDown (block);

    // Room / wash, at the base rate. The reverb always runs (cheap at this
    // rate): starting it only when a room preset is selected would feed it an
    // abrupt onset, which it rings with.
    if (roomSide != &side && side.hasRoom())
        applyRoomSettings (side);
    // Send filter (12 dB LP + HP, open unless the side filters the reverb).
    // Targets every 8 samples (global counter: block-size independent).
    const double maxHz = maxCutoffRatio * sampleRate;
    for (int i = 0; i < numSamples; ++i)
    {
        if ((baseCounter++ % (std::uint32_t) controlInterval) == 0)
        {
            const auto t = computeTargets (preset, amount[i]);
            roomLpLogHz.setTarget (std::log (t.roomLowpassHz));
            roomHpLogHz.setTarget (std::log (t.roomHighpassHz));
            roomMix.setTarget (t.room);
            roomLevel.setTarget (t.roomLevel);
        }
        roomMixBuffer[(size_t) i] = roomMix.next();
        roomLevelBuffer[(size_t) i] = roomLevel.next();
        const auto lpC = gk::TptSvf::Coeffs::make (juce::jlimit (20.0, maxHz, (double) std::exp (roomLpLogHz.next())), sampleRate, 0.7071);
        const auto hpC = gk::TptSvf::Coeffs::make (juce::jlimit (10.0, maxHz, (double) std::exp (roomHpLogHz.next())), sampleRate, 0.7071);
        for (int c = 0; c < nch; ++c)
        {
            const double x = buffer.getReadPointer (c)[i];
            roomBuffer.getWritePointer (c)[i] = (float) roomHp[(size_t) c].processHighpass (roomLp[(size_t) c].processLowpass (x, lpC), hpC);
        }
    }
    if (nch == 2)
        room.processStereo (roomBuffer.getWritePointer (0), roomBuffer.getWritePointer (1), numSamples);
    else
        room.processMono (roomBuffer.getWritePointer (0), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = roomMixBuffer[(size_t) i], level = roomLevelBuffer[(size_t) i];
        if (m <= 0.0f)
            continue;
        for (int c = 0; c < nch; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            d[i] = d[i] * (1.0f - 0.35f * m) + level * m * roomBuffer.getReadPointer (c)[i];
        }
    }
}

} // namespace ff
