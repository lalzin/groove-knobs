#pragma once

namespace gk
{
/** Static description of a Groove Knobs plugin. */
struct OneKnobConfig
{
    juce::String productName;
    juce::String suiteName { "Groove Knobs" };
    juce::String companyName { "LALZIN" };   // shown as "made by ..." in the editor
    PresetManager presets;

    /** Bipolar knob: centred by default, left and right drive two sides of
        the engine. The engine then receives a signed macro in -1..1. */
    bool bipolar = false;
    float centreDeadZone = 0.01f;         // bipolar: |macro| below this is exactly the centre
    juce::String lowSideLabel { "LP" }, highSideLabel { "HP" };

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
class OneKnobProcessor : public juce::AudioProcessor
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
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    double getTailLengthSeconds() const override { return config.tailSeconds; }
    bool acceptsMidi() const override { return false; }
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

    /** Hook for future state versions: upgrade `state` in place from `fromVersion`. */
    virtual void migrateState (juce::ValueTree& state, int fromVersion);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const OneKnobConfig& config);
    void processInternal (juce::AudioBuffer<float>& buffer, bool forceBypass);
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

    // Audio-thread state (sized in prepareToPlay)
    HostSync hostSync;
    FeatureExtractor featureExtractor;
    AudioFeatures features;
    IntegerDelay dryDelay;
    juce::AudioBuffer<float> dryBuffer, wetBuffer;
    std::vector<float> amountBuffer, bypassBuffer, weightBuffer;
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
