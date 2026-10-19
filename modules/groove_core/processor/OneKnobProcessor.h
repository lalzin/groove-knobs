#pragma once

namespace gk
{
/** An extra on/off parameter (shown as a chip under the knob). Each preset
    gives its default in its JSON: "toggles": { "<id>": true/false }. */
struct ToggleSpec
{
    juce::String id, name, chipLabel;
    bool defaultOn = false; // switches only (toggles take their preset's default)
};

/** A global multiple-choice parameter (not part of the presets), shown as a
    small selector with its caption (e.g. SWELL: 1/4). */
struct ChoiceSpec
{
    juce::String id, name, caption;
    juce::StringArray options;
    int defaultIndex = 0;
};

/** A small global knob (0..1, shown in percent), not part of the presets
    (e.g. an instrument's ROOM / ECHO sends). */
struct KnobSpec
{
    juce::String id, name, caption;
    float defaultValue = 0.0f;
    /** Range and unit: percent 0..1 by default; "dB": minimum = -inf. */
    float minimum = 0.0f, maximum = 1.0f, skew = 1.0f;
    juce::String unit { "%" };
};

/** Static description of a Groove Knobs plugin. */
struct OneKnobConfig
{
    juce::String productName;
    juce::String suiteName { "Groove Knobs" };
    juce::String companyName { "LALZIN" };   // shown as "made by ..." in the editor
    juce::String companyUrl { "https://www.lalzin.com/links" }; // opened by a click on the credit (empty: not clickable)
    PresetManager presets;

    /** Bipolar knob: centred by default, left and right drive two sides of
        the engine. The engine then receives a signed macro in -1..1. */
    bool bipolar = false;
    float centreDeadZone = 0.01f;         // bipolar: |macro| below this is exactly the centre
    juce::String lowSideLabel { "LP" }, highSideLabel { "HP" };
    /** Labels at the two ends of the knob arc. Empty: the side labels when
        bipolar, nothing otherwise. */
    juce::String knobLowLabel, knobHighLabel;

    /** Extra on/off parameters, between "preset" and "bypass" (max 2). */
    std::vector<ToggleSpec> toggles;
    double toggleFadeSeconds = 0.020;

    /** Global on/off parameters (not part of the presets, no makeup table),
        after the toggles; shown as chips after them. The engine may change
        its latency with them (see OneKnobEngine::latencyForSwitches): the
        output then fades out, the latency changes, and it fades back in. */
    std::vector<ToggleSpec> switches;
    double switchFadeSeconds = 0.010;

    /** Instrument (VSTi / AU "aumu"): no audio input, MIDI in, the engine
        makes the sound. There is no dry path: the knob's 0 % is the engine's
        own sound (no transparency zone, no makeup, no warm-up fade), and the
        engine receives the MIDI of each chunk (OneKnobEngine::setMidi). */
    bool isInstrument = false;
    /** Default of "amount" (< 0: 0, or the centre when bipolar). */
    float defaultAmount = -1.0f;

    /** Small global knobs, after the switches (before the choices); the
        engine gets their values every block (OneKnobEngine::setKnobs). */
    std::vector<KnobSpec> knobs;

    /** Global multiple-choice parameters, after the switches; the engine gets
        their indices every block (OneKnobEngine::setChoices). */
    std::vector<ChoiceSpec> choices;

    /** Also accept a mono input with a stereo output (mono is duplicated). */
    bool allowMonoToStereo = false;

    /** Keep the engine running at 0 % and when bypassed (engines with a long
        history, e.g. a lookahead convolution). The output stays bit-exact at
        0 % since the wet weight is 0. */
    bool engineAlwaysRuns = false;

    /** Take the UI telemetry (RMS, transients) from the output instead of the
        input (plugins with a long latency). */
    bool featuresFromOutput = false;

    float transparencyZone = 0.03f;       // amount range crossfaded dry -> wet
    double amountRampSeconds = 0.020;     // linear ramp on the macro
    double derivedSettleSeconds = 0.030;  // smoothing of derived values (makeup here, DSP in engines)
    double bypassRampSeconds = 0.020;
    double wetFadeInSeconds = 0.003;      // fade-in of the wet path after it was idle
    double crossfadeMinSeconds = 0.010;   // fastest allowed full dry <-> wet transition
    double tailSeconds = 0.2;
    int editorBaseWidth = 420, editorBaseHeight = 520;
};

/** Generic one-knob AudioProcessor.

    Signal flow (per channel):
        in ─┬────────────────── dry (delayed by the engine latency) ──────┐
            └─> engine (wet) ─> makeup gain ─> transparency crossfade ────┴─> safety clip ─> bypass crossfade ─> out

    Parameters (versioned IDs, v1): "amount" (0..1), "preset" (choice), "bypass".
*/
class OneKnobProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorParameter::Listener,
                         private juce::AsyncUpdater
{
public:
    static constexpr int stateVersion = 2;
    static inline const juce::ParameterID amountId { "amount", 2 };
    static inline const juce::ParameterID presetId { "preset", 1 };
    static inline const juce::ParameterID bypassId { "bypass", 1 };

    OneKnobProcessor (OneKnobConfig config, std::unique_ptr<OneKnobEngine> engine);
    ~OneKnobProcessor() override;

    //== AudioProcessor ==========================================================
    const juce::String getName() const override { return config.productName; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    void setNonRealtime (bool isNonRealtime) noexcept override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    double getTailLengthSeconds() const override { return config.tailSeconds; }
    bool acceptsMidi() const override { return config.isInstrument; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool hasEditor() const override { return true; }

    int getNumPrograms() override { return juce::jmax (1, config.presets.size()); }
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    //== Groove Knobs API ========================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    juce::RangedAudioParameter& getAmountParameter() noexcept { return *amountParam; }
    juce::AudioParameterChoice& getPresetParameter() noexcept { return *presetParam; }
    juce::AudioParameterBool& getBypassParameterObject() noexcept { return *bypassParam; }
    int getNumToggles() const noexcept { return (int) toggleParams.size(); }
    juce::AudioParameterBool& getToggleParameter (int index) noexcept { return *toggleParams[(size_t) index]; }
    int getNumSwitches() const noexcept { return (int) switchParams.size(); }
    juce::AudioParameterBool& getSwitchParameter (int index) noexcept { return *switchParams[(size_t) index]; }
    int getNumKnobs() const noexcept { return (int) knobParams.size(); }
    juce::RangedAudioParameter& getKnobParameter (int index) noexcept { return *knobParams[(size_t) index]; }
    int getNumChoices() const noexcept { return (int) choiceParams.size(); }
    juce::AudioParameterChoice& getChoiceParameter (int index) noexcept { return *choiceParams[(size_t) index]; }

    /** A preset's default value for toggle `index` (from its JSON). */
    bool presetToggleDefault (int presetIndex, int toggleIndex) const;
    const PresetManager& getPresets() const noexcept { return config.presets; }
    const OneKnobConfig& getConfig() const noexcept { return config; }
    OneKnobEngine& getEngine() noexcept { return *engine; }
    AudioFeatures& getFeatures() noexcept { return features; }

    /** The engine's macro for a raw "amount" value: 0..1 (unipolar), or
        -1..1 with the centre dead zone applied (bipolar). */
    float macroFor (float amount01) const noexcept;

    /** Normalised "amount" value giving a macro (inverse of macroFor). */
    float amountFor (float macro) const noexcept;

    /** Editor scale factor (0.75 .. 2), saved in the state. */
    std::atomic<float> editorScale { 1.0f };

    /** Skip the ramps on the next block (offline rendering, tests). */
    void snapToTargets() noexcept { snapRequested.store (true); }

    /** Calibration only: disables the makeup gain table. */
    void setMakeupEnabled (bool shouldBeEnabled) noexcept { makeupEnabled.store (shouldBeEnabled); }

    /** Plugin-specific data saved with the state (e.g. captured audio). */
    virtual void saveExtraState (juce::ValueTree& state) { juce::ignoreUnused (state); }
    virtual void loadExtraState (const juce::ValueTree& state) { juce::ignoreUnused (state); }

    /** Hook for future state versions: upgrade `state` in place from `fromVersion`. */
    virtual void migrateState (juce::ValueTree& state, int fromVersion);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const OneKnobConfig& config);
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;
    void applyPresetToggleDefaults();
    void processInternal (juce::AudioBuffer<float>& buffer, bool forceBypass, const juce::MidiBuffer* midi = nullptr);
    static BusesProperties busesFor (const OneKnobConfig& config);
    void updateSwitches (bool snap);
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples, const BlockTiming& timing, int numChannels);

    OneKnobConfig config;
    std::unique_ptr<OneKnobEngine> engine;
    juce::AudioProcessorValueTreeState apvts;

    juce::RangedAudioParameter* amountParam = nullptr;
    juce::AudioParameterChoice* presetParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<float>* amountValue = nullptr;
    std::atomic<float>* presetValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;
    std::vector<juce::AudioParameterBool*> toggleParams;
    std::vector<std::atomic<float>*> toggleValues;
    std::atomic<int> presetForToggles { -1 }; // last preset whose toggle defaults were applied
    std::vector<juce::AudioParameterBool*> switchParams;
    std::vector<std::atomic<float>*> switchValues;
    std::vector<juce::RangedAudioParameter*> knobParams;
    std::vector<std::atomic<float>*> knobValues;
    const juce::MidiBuffer* chunkMidi = nullptr; // instrument: the block's MIDI, during processInternal
    std::vector<juce::AudioParameterChoice*> choiceParams;
    std::vector<std::atomic<float>*> choiceValues;
    LinearSmoother switchGain;  // output fade around a latency change
    bool latencySwitchPending = false;
    std::atomic<int> pendingLatency { -1 }; // reported to the host on the message thread

    // Audio-thread state (sized in prepareToPlay)
    HostSync hostSync;
    FeatureExtractor featureExtractor;
    AudioFeatures features;
    IntegerDelay dryDelay;
    juce::AudioBuffer<float> dryBuffer, wetBuffer;
    std::vector<float> amountBuffer, bypassBuffer, weightBuffer;
    std::array<LinearSmoother, PresetData::maxToggles> toggleSmoothers;
    std::array<std::vector<float>, PresetData::maxToggles> toggleBuffers;
    std::array<const float*, PresetData::maxToggles> togglePointers {};
    LinearSmoother amountSmoother, bypassSmoother, wetGate, wetInputFade;
    SmoothParam makeupSmoother;
    float crossfadeWeight = 0.0f, crossfadeMaxStep = 1.0f;
    int maxBlock = 0, latency = 0, warmupRemaining = 0, currentPreset = -1;
    bool wetRunning = false;
    std::atomic<bool> snapRequested { true }, makeupEnabled { true };
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneKnobProcessor)
};
} // namespace gk
