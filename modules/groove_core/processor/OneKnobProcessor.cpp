namespace gk
{
namespace
{
    constexpr const char* stateTag = "GrooveKnobsState";
}

juce::AudioProcessor::BusesProperties OneKnobProcessor::busesFor (const OneKnobConfig& cfg)
{
    if (cfg.isInstrument) // no audio input
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

OneKnobProcessor::OneKnobProcessor (OneKnobConfig cfg, std::unique_ptr<OneKnobEngine> eng)
    : AudioProcessor (busesFor (cfg)),
      config (std::move (cfg)),
      engine (std::move (eng)),
      apvts (*this, nullptr, "PARAMETERS", createLayout (config))
{
    jassert (engine != nullptr);
    jassert (config.presets.size() > 0);
    engine->setPresets (config.presets);

    amountParam = apvts.getParameter (amountId.getParamID());
    presetParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (presetId.getParamID()));
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (bypassId.getParamID()));
    amountValue = apvts.getRawParameterValue (amountId.getParamID());
    presetValue = apvts.getRawParameterValue (presetId.getParamID());
    bypassValue = apvts.getRawParameterValue (bypassId.getParamID());
    jassert (amountParam && presetParam && bypassParam);

    jassert ((int) config.toggles.size() <= PresetData::maxToggles);
    for (const auto& t : config.toggles)
    {
        toggleParams.push_back (dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (t.id)));
        toggleValues.push_back (apvts.getRawParameterValue (t.id));
        jassert (toggleParams.back() != nullptr);
    }
    for (const auto& t : config.switches)
    {
        switchParams.push_back (dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (t.id)));
        switchValues.push_back (apvts.getRawParameterValue (t.id));
        jassert (switchParams.back() != nullptr);
    }
    for (const auto& k : config.knobs)
    {
        knobParams.push_back (apvts.getParameter (k.id));
        knobValues.push_back (apvts.getRawParameterValue (k.id));
        jassert (knobParams.back() != nullptr);
    }
    for (const auto& c : config.choices)
    {
        choiceParams.push_back (dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (c.id)));
        choiceValues.push_back (apvts.getRawParameterValue (c.id));
        jassert (choiceParams.back() != nullptr);
    }
    if (! config.toggles.empty())
    {
        // Changing the preset applies its toggle defaults (not when a state
        // is restored: see setStateInformation).
        presetForToggles = 0;
        presetParam->addListener (this);
    }
}

OneKnobProcessor::~OneKnobProcessor()
{
    if (! config.toggles.empty())
        presetParam->removeListener (this);
    cancelPendingUpdate();
}

bool OneKnobProcessor::presetToggleDefault (int presetIndex, int toggleIndex) const
{
    const auto& json = config.presets[presetIndex].json;
    return (bool) json.getProperty ("toggles", {}).getProperty (config.toggles[(size_t) toggleIndex].id, false);
}

void OneKnobProcessor::parameterValueChanged (int, float)
{
    // May come from the audio thread (automation): defer to the message thread.
    if (juce::MessageManager::existsAndIsCurrentThread())
        applyPresetToggleDefaults();
    else
        triggerAsyncUpdate();
}

void OneKnobProcessor::handleAsyncUpdate()
{
    if (const int l = pendingLatency.load(); l >= 0 && l != getLatencySamples())
        setLatencySamples (l);
    if (! config.toggles.empty())
        applyPresetToggleDefaults();
}

void OneKnobProcessor::applyPresetToggleDefaults()
{
    const int preset = presetParam->getIndex();
    if (presetForToggles.exchange (preset) == preset)
        return;
    for (size_t i = 0; i < toggleParams.size(); ++i)
    {
        const bool on = presetToggleDefault (preset, (int) i);
        if (toggleParams[i]->get() != on)
        {
            toggleParams[i]->beginChangeGesture();
            toggleParams[i]->setValueNotifyingHost (on ? 1.0f : 0.0f);
            toggleParams[i]->endChangeGesture();
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout OneKnobProcessor::createLayout (const OneKnobConfig& cfg)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    std::function<juce::String (float, int)> toText;
    std::function<float (const juce::String&)> fromText;

    if (cfg.bipolar)
    {
        // "LP 42 %" / "—" / "HP 30 %" (percent of each side's travel)
        toText = [lo = cfg.lowSideLabel, hi = cfg.highSideLabel] (float v, int)
        {
            const int pct = juce::roundToInt (std::abs (v - 0.5f) * 200.0f);
            if (pct == 0)
                return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")); // em dash: centre
            return (v < 0.5f ? lo : hi) + " " + juce::String (pct) + " %";
        };
        // Accepts "LP 40", "HP 20", "-40", "+20", "40" (= right), "0"
        fromText = [lo = cfg.lowSideLabel] (const juce::String& text)
        {
            auto t = text.trim().toUpperCase();
            float sign = 1.0f;
            if (t.startsWith (lo.toUpperCase()) || t.startsWith ("-") || t.startsWith ("L"))
                sign = -1.0f;
            const auto digits = t.retainCharacters ("0123456789.");
            const float pct = juce::jlimit (0.0f, 100.0f, digits.getFloatValue());
            return juce::jlimit (0.0f, 1.0f, 0.5f + sign * pct / 200.0f);
        };
    }
    else
    {
        toText = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        // The knob is displayed in percent, so typed numbers are percent:
        // "50", "50 %", "50%" -> 0.5
        fromText = [] (const juce::String& text)
        {
            const auto t = text.trim().trimCharactersAtEnd ("% ").trim();
            return juce::jlimit (0.0f, 1.0f, t.getFloatValue() / 100.0f);
        };
    }

    // "amount" first: it becomes the first mappable parameter in Ableton.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        amountId, "Amount", juce::NormalisableRange<float> (0.0f, 1.0f), cfg.defaultAmount >= 0.0f ? cfg.defaultAmount : (cfg.bipolar ? 0.5f : 0.0f),
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText).withValueFromStringFunction (fromText)));

    // With toggles, a preset change also sets the toggles: hosts must know it
    // is a "meta" parameter (AU validation checks this).
    layout.add (std::make_unique<juce::AudioParameterChoice> (presetId, "Preset", cfg.presets.getNames(), 0,
                                                              juce::AudioParameterChoiceAttributes().withMeta (! cfg.toggles.empty())));
    for (const auto& t : cfg.toggles)
    {
        const bool def = cfg.presets.size() > 0 && (bool) cfg.presets[0].json.getProperty ("toggles", {}).getProperty (t.id, false);
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { t.id, 1 }, t.name, def));
    }
    for (const auto& t : cfg.switches)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { t.id, 1 }, t.name, t.defaultOn));
    for (const auto& k : cfg.knobs)
    {
        juce::NormalisableRange<float> range (k.minimum, k.maximum);
        range.skew = k.skew;
        const bool db = k.unit == "dB";
        const float minimum = k.minimum;
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { k.id, 1 }, k.name, range, k.defaultValue,
            juce::AudioParameterFloatAttributes()
                .withLabel (db ? "dB" : "%")
                .withStringFromValueFunction ([db, minimum] (float v, int)
                                              {
                                                  if (! db)
                                                      return juce::String (juce::roundToInt (v * 100.0f)) + " %";
                                                  return v <= minimum + 0.01f ? juce::String ("-inf") : juce::String (v, 1) + " dB";
                                              })
                .withValueFromStringFunction ([db, minimum] (const juce::String& t)
                                              {
                                                  const auto x = t.trim().trimCharactersAtEnd ("%dB ").trim();
                                                  if (db)
                                                      return x.containsIgnoreCase ("inf") ? minimum : x.getFloatValue();
                                                  return juce::jlimit (0.0f, 1.0f, x.getFloatValue() / 100.0f);
                                              })));
    }
    for (const auto& c : cfg.choices)
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { c.id, 1 }, c.name, c.options, c.defaultIndex));
    layout.add (std::make_unique<juce::AudioParameterBool> (bypassId, "Bypass", false));
    return layout;
}

float OneKnobProcessor::macroFor (float a) const noexcept
{
    if (! config.bipolar)
        return a;
    const float m = 2.0f * a - 1.0f;
    const float mag = std::abs (m) - config.centreDeadZone;
    if (mag <= 0.0f)
        return 0.0f;
    return std::copysign (juce::jmin (1.0f, mag / (1.0f - config.centreDeadZone)), m);
}

float OneKnobProcessor::amountFor (float macro) const noexcept
{
    if (! config.bipolar)
        return juce::jlimit (0.0f, 1.0f, macro);
    if (juce::exactlyEqual (macro, 0.0f))
        return 0.5f;
    const float mag = config.centreDeadZone + (1.0f - config.centreDeadZone) * juce::jlimit (0.0f, 1.0f, std::abs (macro));
    return 0.5f + 0.5f * std::copysign (mag, macro);
}

bool OneKnobProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (config.isInstrument)
        return in.isDisabled() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    if (config.allowMonoToStereo && in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo())
        return true;
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void OneKnobProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);
    const int numChannels = juce::jmax (1, getMainBusNumOutputChannels());

    engine->prepare (sampleRate, maxBlock, numChannels);
    if (! switchParams.empty())
    {
        std::array<bool, 8> on {};
        for (size_t t = 0; t < switchParams.size() && t < on.size(); ++t)
            on[t] = switchValues[t]->load() >= 0.5f;
        engine->setSwitches (on.data(), (int) switchParams.size());
        switchGain.prepare (sampleRate, config.switchFadeSeconds);
        switchGain.snap (1.0f);
        latencySwitchPending = false;
    }
    latency = engine->getLatencySamples();
    pendingLatency.store (latency);
    setLatencySamples (latency);

    if (switchParams.empty())
        dryDelay.prepare (numChannels, latency);
    else
        dryDelay.prepare (numChannels, latency, engine->getMaxLatencySamples());
    dryBuffer.setSize (numChannels, maxBlock, false, true, false);
    wetBuffer.setSize (numChannels, maxBlock, false, true, false);
    amountBuffer.assign ((size_t) maxBlock, 0.0f);
    bypassBuffer.assign ((size_t) maxBlock, 0.0f);
    for (size_t t = 0; t < toggleParams.size(); ++t)
    {
        toggleBuffers[t].assign ((size_t) maxBlock, 0.0f);
        togglePointers[t] = toggleBuffers[t].data();
        toggleSmoothers[t].prepare (sampleRate, config.toggleFadeSeconds);
    }
    weightBuffer.assign ((size_t) maxBlock, 0.0f);
    crossfadeMaxStep = (float) (1.0 / juce::jmax (1.0, config.crossfadeMinSeconds * sampleRate));

    amountSmoother.prepare (sampleRate, config.amountRampSeconds);
    bypassSmoother.prepare (sampleRate, config.bypassRampSeconds);
    wetGate.prepare (sampleRate, config.wetFadeInSeconds);
    wetInputFade.prepare (sampleRate, 0.002);
    makeupSmoother.prepare (sampleRate, config.derivedSettleSeconds);

    hostSync.prepare (sampleRate);
    featureExtractor.prepare (sampleRate);
    reset();
}

void OneKnobProcessor::reset()
{
    engine->reset();
    dryDelay.reset();
    featureExtractor.reset();
    wetRunning = false;
    warmupRemaining = 0;
    wetGate.snap (0.0f);
    makeupSmoother.invalidate();
    snapRequested.store (true);
}

void OneKnobProcessor::setNonRealtime (bool isNonRealtime) noexcept
{
    AudioProcessor::setNonRealtime (isNonRealtime);
    engine->setNonRealtime (isNonRealtime);
}

void OneKnobProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, false, &midi);
}

void OneKnobProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Host-side bypass: keep the latency-compensated dry path and ramp.
    processInternal (buffer, true, &midi);
}

void OneKnobProcessor::processInternal (juce::AudioBuffer<float>& buffer, bool forceBypass, const juce::MidiBuffer* midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels());
    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numSamples == 0 || numChannels == 0 || maxBlock == 0)
        return;
    if (config.isInstrument)
    {
        // No input: the host's buffer holds nothing meaningful
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.clear (ch, 0, numSamples);
        chunkMidi = midi;
        if (! knobParams.empty())
        {
            std::array<float, 8> v {}; // plain values (% knobs: 0..1, dB knobs: dB)
            const int n = juce::jmin ((int) knobParams.size(), (int) v.size());
            for (int k = 0; k < n; ++k)
                v[(size_t) k] = knobValues[(size_t) k]->load();
            engine->setKnobs (v.data(), n);
        }
    }

    // Mono in, stereo out: the mono input feeds both channels.
    if (numChannels == 2 && getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    const auto timing = hostSync.beginBlock (getPlayHead(), numSamples);

    const int presetIndex = juce::jlimit (0, config.presets.size() - 1, juce::roundToInt (presetValue->load()));
    if (presetIndex != currentPreset)
    {
        engine->setPreset (presetIndex);
        currentPreset = presetIndex;
    }

    const float amountTarget = juce::jlimit (0.0f, 1.0f, amountValue->load());
    const float bypassTarget = (forceBypass || bypassValue->load() >= 0.5f) ? 1.0f : 0.0f;

    const bool snapToggles = snapRequested.load();
    if (snapRequested.exchange (false))
    {
        amountSmoother.snap (amountTarget);
        bypassSmoother.snap (bypassTarget);
        crossfadeWeight = config.isInstrument ? 1.0f : macro::transparencyWeight (std::abs (macroFor (amountTarget)), config.transparencyZone);
        makeupSmoother.invalidate();
        engine->reset();
        wetRunning = false;
    }
    amountSmoother.setTarget (amountTarget);
    bypassSmoother.setTarget (bypassTarget);
    for (size_t t = 0; t < toggleParams.size(); ++t)
    {
        const float target = toggleValues[t]->load() >= 0.5f ? 1.0f : 0.0f;
        if (snapToggles)
            toggleSmoothers[t].snap (target);
        toggleSmoothers[t].setTarget (target);
    }

    if (! switchParams.empty())
        updateSwitches (snapToggles);
    if (! choiceParams.empty())
    {
        std::array<int, 8> idx {};
        const int n = juce::jmin ((int) choiceParams.size(), (int) idx.size());
        for (int c = 0; c < n; ++c)
            idx[(size_t) c] = juce::roundToInt (choiceValues[(size_t) c]->load());
        engine->setChoices (idx.data(), n);
    }

    if (! config.featuresFromOutput)
        featureExtractor.process (buffer, 0, numSamples, features);

    // Hosts may send more samples than announced in prepareToPlay: split.
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - start);
        processChunk (buffer, start, n, timing.offsetBy (start), numChannels);
    }

    if (! switchParams.empty() && (switchGain.isSmoothing() || switchGain.getCurrent() < 1.0f))
        for (int i = 0; i < numSamples; ++i)
        {
            const float f = dspmath::smoothstep (0.0f, 1.0f, switchGain.next());
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[i] *= f;
        }

    if (config.featuresFromOutput)
        featureExtractor.process (buffer, 0, numSamples, features);

    features.beats.store (timing.beatsAt (numSamples), std::memory_order_relaxed);
    features.bpm.store ((float) timing.bpm, std::memory_order_relaxed);
    features.playing.store (timing.isPlaying, std::memory_order_relaxed);
    features.modulationPhase.store (engine->getModulationPhase(), std::memory_order_relaxed);
    features.amount.store (macroFor (amountSmoother.getCurrent()), std::memory_order_relaxed);
    features.bypassMix.store (bypassSmoother.getCurrent(), std::memory_order_relaxed);
    chunkMidi = nullptr;
}

void OneKnobProcessor::updateSwitches (bool snap)
{
    std::array<bool, 8> on {};
    const int n = juce::jmin ((int) switchParams.size(), (int) on.size());
    for (int t = 0; t < n; ++t)
        on[(size_t) t] = switchValues[(size_t) t]->load() >= 0.5f;

    const int wanted = engine->latencyForSwitches (on.data(), n);
    if (wanted == latency)
    {
        engine->setSwitches (on.data(), n);
        if (latencySwitchPending)
        {
            // Switched back before the change happened
            latencySwitchPending = false;
            switchGain.setTarget (1.0f);
        }
        return;
    }

    // The latency changes: fade the output out, then switch (next block, or
    // at once when snapping: offline, state restored), then fade back in.
    if (! snap && ! latencySwitchPending)
    {
        latencySwitchPending = true;
        switchGain.setTarget (0.0f);
        return;
    }
    if (! snap && (switchGain.isSmoothing() || switchGain.getCurrent() > 0.0f))
        return;

    engine->setSwitches (on.data(), n);
    latency = engine->getLatencySamples();
    dryDelay.setDelay (latency);
    wetRunning = false; // restart the engine cleanly with the new latency
    latencySwitchPending = false;
    if (snap)
        switchGain.snap (1.0f);
    else
        switchGain.setTarget (1.0f);
    pendingLatency.store (latency);
    triggerAsyncUpdate();
}

void OneKnobProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int n, const BlockTiming& timing, int numChannels)
{
    bool anyActive = false;
    for (int i = 0; i < n; ++i)
    {
        const float a = macroFor (amountSmoother.next());
        // S-shaped bypass crossfade: no slope break at either end.
        const float b = dspmath::smoothstep (0.0f, 1.0f, bypassSmoother.next());

        // Dry/wet weight of the transparency zone, slew-limited: even a very
        // fast knob move crossfades over >= crossfadeMinSeconds (the dry and
        // wet paths differ slightly in phase, a 1 ms crossfade would click).
        const float target = config.isInstrument ? 1.0f : macro::transparencyWeight (std::abs (a), config.transparencyZone);
        if (std::abs (target - crossfadeWeight) <= crossfadeMaxStep)
            crossfadeWeight = target;
        else
            crossfadeWeight += target > crossfadeWeight ? crossfadeMaxStep : -crossfadeMaxStep;

        amountBuffer[(size_t) i] = a;
        bypassBuffer[(size_t) i] = b;
        weightBuffer[(size_t) i] = crossfadeWeight;
        for (size_t t = 0; t < toggleParams.size(); ++t)
            toggleBuffers[t][(size_t) i] = toggleSmoothers[t].next();
        anyActive = anyActive || ((! juce::exactlyEqual (a, 0.0f) || crossfadeWeight > 0.0f) && b < 1.0f);
    }
    anyActive = anyActive || config.engineAlwaysRuns;

    // Latency-aligned dry path
    for (int ch = 0; ch < numChannels; ++ch)
        dryDelay.process (ch, buffer.getReadPointer (ch, start), dryBuffer.getWritePointer (ch), n);
    dryDelay.advance (n);

    if (anyActive)
    {
        if (! wetRunning)
        {
            // The wet path was idle (amount == 0 or bypassed): restart it from
            // clean states. Its first `latency` output samples are only the
            // resampler filling up, so keep the output on the dry path until
            // the wet signal is valid, then fade it in.
            engine->reset();
            makeupSmoother.invalidate();
            if (config.isInstrument)
            {
                // The engine is the sound: no warm-up, no fade-in (a note in
                // the very first sample must start there)
                warmupRemaining = 0;
                wetGate.snap (1.0f);
            }
            else
            {
                warmupRemaining = latency + 16;
                wetGate.snap (0.0f);
                // The engine restarts from silence: fade its input in, so its
                // filters / reverb see a soft onset instead of a step (which they
                // would ring with after the gate opens).
                wetInputFade.snap (0.0f);
                wetInputFade.setTarget (1.0f);
            }
            wetRunning = true;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            wetBuffer.copyFrom (ch, 0, buffer, ch, start, n);
        if (wetInputFade.isSmoothing())
            for (int i = 0; i < n; ++i)
            {
                const float f = dspmath::smoothstep (0.0f, 1.0f, wetInputFade.next());
                for (int ch = 0; ch < numChannels; ++ch)
                    wetBuffer.getWritePointer (ch)[i] *= f;
            }

        if (! toggleParams.empty())
            engine->setToggleValues (togglePointers.data(), (int) toggleParams.size());
        if (config.isInstrument && chunkMidi != nullptr)
            engine->setMidi (*chunkMidi, start, n);
        engine->processWet (wetBuffer, n, amountBuffer.data(), timing);

        const bool useMakeup = makeupEnabled.load (std::memory_order_relaxed);
        const auto& preset = config.presets[currentPreset];

        for (int i = 0; i < n; ++i)
        {
            const float a = amountBuffer[(size_t) i];
            float makeupDb = 0.0f;
            if (useMakeup && ! config.isInstrument)
            {
                if (toggleParams.empty())
                    makeupDb = preset.makeupDbAt (a);
                else
                {
                    std::array<float, PresetData::maxToggles> tv {};
                    for (size_t t = 0; t < toggleParams.size(); ++t)
                        tv[t] = toggleBuffers[t][(size_t) i];
                    makeupDb = preset.makeupDbAt (a, tv.data(), (int) toggleParams.size());
                }
            }
            makeupSmoother.setTarget (makeupDb);
            const float makeup = dspmath::dbToGain (makeupSmoother.next());

            if (warmupRemaining > 0)
            {
                if (--warmupRemaining == 0)
                    wetGate.setTarget (1.0f);
            }
            const float w = dspmath::smoothstep (0.0f, 1.0f, wetGate.next()) * weightBuffer[(size_t) i];
            const float bp = bypassBuffer[(size_t) i];

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float dry = dryBuffer.getReadPointer (ch)[i];
                const float wet = wetBuffer.getReadPointer (ch)[i] * makeup;
                const float mixed = SafetyClipper::process ((1.0f - w) * dry + w * wet);
                buffer.getWritePointer (ch, start)[i] = (1.0f - bp) * mixed + bp * dry;
            }
        }
    }
    else
    {
        wetRunning = false;
        for (int i = 0; i < n; ++i)
        {
            const float bp = bypassBuffer[(size_t) i];
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float dry = dryBuffer.getReadPointer (ch)[i];
                buffer.getWritePointer (ch, start)[i] = bp >= 1.0f ? dry : (1.0f - bp) * SafetyClipper::process (dry) + bp * dry;
            }
        }
    }
}

//==============================================================================
int OneKnobProcessor::getCurrentProgram()
{
    return presetParam->getIndex();
}

void OneKnobProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= config.presets.size())
        return;
    presetParam->beginChangeGesture();
    presetParam->setValueNotifyingHost (presetParam->convertTo0to1 ((float) index));
    presetParam->endChangeGesture();
}

const juce::String OneKnobProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, config.presets.size()) ? config.presets[index].name : juce::String();
}

//==============================================================================
void OneKnobProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state (stateTag);
    state.setProperty ("version", stateVersion, nullptr);
    state.setProperty ("plugin", config.productName, nullptr);
    state.setProperty ("amount", amountParam->convertFrom0to1 (amountParam->getValue()), nullptr);
    const int presetIndex = presetParam->getIndex();
    state.setProperty ("preset", config.presets[presetIndex].name, nullptr);
    state.setProperty ("presetIndex", presetIndex, nullptr);
    state.setProperty ("bypass", bypassParam->get(), nullptr);
    for (size_t t = 0; t < toggleParams.size(); ++t)
        state.setProperty (config.toggles[t].id, toggleParams[t]->get(), nullptr);
    for (size_t t = 0; t < switchParams.size(); ++t)
        state.setProperty (config.switches[t].id, switchParams[t]->get(), nullptr);
    for (size_t k = 0; k < knobParams.size(); ++k)
        state.setProperty (config.knobs[k].id, knobParams[k]->convertFrom0to1 (knobParams[k]->getValue()), nullptr);
    for (size_t c = 0; c < choiceParams.size(); ++c)
        state.setProperty (config.choices[c].id, choiceParams[c]->getCurrentChoiceName(), nullptr);
    state.setProperty ("editorScale", editorScale.load(), nullptr);
    saveExtraState (state);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OneKnobProcessor::migrateState (juce::ValueTree& state, int fromVersion)
{
    // v1 -> v2: the knob became bipolar (centre = neutral). A v1 amount was
    // the low-pass travel 0..1: it maps onto the left half of the new knob.
    if (fromVersion < 2 && config.bipolar && state.hasProperty ("amount"))
    {
        const float v1 = juce::jlimit (0.0f, 1.0f, (float) state.getProperty ("amount"));
        state.setProperty ("amount", juce::exactlyEqual (v1, 0.0f) ? 0.5f : amountFor (-v1), nullptr);
    }
}

void OneKnobProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateTag))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const int version = state.getProperty ("version", 1);
    if (version > stateVersion)
        return; // state written by a newer build: keep current settings
    if (version < stateVersion)
        migrateState (state, version);

    auto setParam = [] (juce::RangedAudioParameter& p, float plainValue)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
        p.endChangeGesture();
    };

    if (state.hasProperty ("amount"))
        setParam (*amountParam, juce::jlimit (0.0f, 1.0f, (float) state.getProperty ("amount")));

    int presetIndex = config.presets.indexOf (state.getProperty ("preset").toString());
    if (presetIndex < 0)
        presetIndex = juce::jlimit (0, config.presets.size() - 1, (int) state.getProperty ("presetIndex", 0));
    // Restoring a state keeps the saved toggles: do not apply preset defaults.
    presetForToggles = presetIndex;
    setParam (*presetParam, (float) presetIndex);
    for (size_t t = 0; t < toggleParams.size(); ++t)
    {
        const bool on = state.hasProperty (config.toggles[t].id) ? (bool) state.getProperty (config.toggles[t].id)
                                                                 : presetToggleDefault (presetIndex, (int) t);
        setParam (*toggleParams[t], on ? 1.0f : 0.0f);
    }

    for (size_t t = 0; t < switchParams.size(); ++t)
        setParam (*switchParams[t], (bool) state.getProperty (config.switches[t].id, config.switches[t].defaultOn) ? 1.0f : 0.0f);
    for (size_t k = 0; k < knobParams.size(); ++k)
        setParam (*knobParams[k], juce::jlimit (config.knobs[k].minimum, config.knobs[k].maximum,
                                                (float) state.getProperty (config.knobs[k].id, config.knobs[k].defaultValue)));
    for (size_t c = 0; c < choiceParams.size(); ++c)
    {
        // Saved by name (options may be reordered later)
        const int idx = config.choices[c].options.indexOf (state.getProperty (config.choices[c].id).toString());
        setParam (*choiceParams[c], (float) (idx >= 0 ? idx : config.choices[c].defaultIndex));
    }
    loadExtraState (state);

    setParam (*bypassParam, (bool) state.getProperty ("bypass", false) ? 1.0f : 0.0f);
    editorScale.store (juce::jlimit (0.75f, 2.0f, (float) state.getProperty ("editorScale", 1.0f)));

    snapToTargets();
}
} // namespace gk
