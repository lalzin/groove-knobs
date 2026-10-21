#pragma once

namespace gk
{
/** The plugin-specific part of a Groove Knobs plugin: the "wet" path.

    OneKnobProcessor owns everything generic around it (parameters, macro
    smoothing, latency-aligned dry path, transparency crossfade, calibrated
    makeup gain, safety clipper, bypass, state, UI telemetry). An engine only
    turns an input block into the wet signal for a given per-sample macro. */
class OneKnobEngine
{
public:
    virtual ~OneKnobEngine() = default;

    /** Called once on the message thread, before prepare(). Build every
        preset-dependent table here (never on the audio thread). */
    virtual void setPresets (const PresetManager& presets) = 0;

    /** May be called several times with different settings. Allocates. */
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;

    /** Clears all filter states. Derived-parameter smoothers must snap to
        their targets on the next processed sample. Real-time safe. */
    virtual void reset() = 0;

    /** Latency of the wet path in samples (constant after prepare()). */
    virtual int getLatencySamples() const = 0;

    /** Selects a preset. Audio thread, wait-free; the change itself must be
        smoothed by the engine. */
    virtual void setPreset (int index) = 0;

    /** Processes the wet path in place. `amount` holds the already ramped
        macro value for every sample of the block. Real-time safe. */
    virtual void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const BlockTiming& timing) = 0;

    /** Plugins with toggles: per-sample values (0..1, already faded over
        20 ms by the processor) for the next processWet() call; one array per
        toggle, valid until that call returns. */
    virtual void setToggleValues (const float* const* perSample, int numToggles) { juce::ignoreUnused (perSample, numToggles); }

    /** Plugins with switches (OneKnobConfig::switches): the latency the
        engine would have with these switches on/off. Audio thread. */
    virtual int latencyForSwitches (const bool* on, int numSwitches) const
    {
        juce::ignoreUnused (on, numSwitches);
        return getLatencySamples();
    }

    /** Applies the switches (on/off, not faded). The processor only calls it
        when the latency does not change, or once the output has been faded
        out for a latency change; getLatencySamples() then gives the new
        latency. Audio thread (and prepare). */
    virtual void setSwitches (const bool* on, int numSwitches) { juce::ignoreUnused (on, numSwitches); }

    /** Instruments: the MIDI events of the chunk about to be processed are
        those of `midi` with a sample position in [start, start + numSamples)
        (their offset in the chunk is position - start). Audio thread. */
    virtual void setMidi (const juce::MidiBuffer& midi, int start, int numSamples) { juce::ignoreUnused (midi, start, numSamples); }

    /** OneKnobConfig::sidechain: the sidechain samples of the next processWet()
        chunk (numChannels 0 = no sidechain connected / bus disabled). Audio
        thread, before processWet; the pointers are valid during that call only. */
    virtual void setSidechain (const float* const* channels, int numChannels, int numSamples)
    {
        juce::ignoreUnused (channels, numChannels, numSamples);
    }

    /** Plugins with small knobs (OneKnobConfig::knobs): their values (0..1),
        before every block; smoothing is up to the engine. Audio thread. */
    virtual void setKnobs (const float* values, int numKnobs) { juce::ignoreUnused (values, numKnobs); }

    /** Plugins with choices (OneKnobConfig::choices): their current indices,
        before every block. Audio thread. */
    virtual void setChoices (const int* indices, int numChoices) { juce::ignoreUnused (indices, numChoices); }

    /** Largest latency the switches can give (sizes the dry path). */
    virtual int getMaxLatencySamples() const { return getLatencySamples(); }

    /** Offline rendering (export, freeze, tests): engines that rebuild data in
        the background should do it synchronously, for reproducible results. */
    virtual void setNonRealtime (bool) noexcept {}

    /** Phase (0..1) of the engine's main modulation (LFO), for the UI. */
    virtual float getModulationPhase() const noexcept { return 0.0f; }

    /** Diagnostic mode for tests: the wet path keeps its latency (and
        resampling) but skips all processing. */
    virtual void setDiagnosticPassthrough (bool) {}
};
} // namespace gk
