#pragma once

#include "DaftChunkEngine.h"
#include "Stretcher.h"

namespace daftchunk
{
/** DAFT CHUNK: the instrument. CHOP (the main knob, "chop") is how wild the
    generated loops get (step 3). Step 1: load a sample (drag and drop or
    CHARGER), analyse it in the background (tempo, beat 1, key, transients),
    slice it (TRANSITOIRES / GRILLE / MIXTE), move the cuts by hand; the
    slices play from C1 (MIDI 36) up.

    The slicing settings and the cuts are the session's (saved with it, not
    host parameters). Everything here runs on the message thread; the audio
    thread only sees an immutable Kit (see DaftChunkEngine::setKit), kept
    alive here until it can no longer be in use. */
class DaftChunkProcessor final : public gk::OneKnobProcessor, public juce::ChangeBroadcaster, private juce::Timer
{
public:
    DaftChunkProcessor();
    ~DaftChunkProcessor() override;

    juce::AudioProcessorEditor* createEditor() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using OneKnobProcessor::processBlock;
    static gk::PresetManager loadBuiltInPresets();

    DaftChunkEngine& getDsp() noexcept { return static_cast<DaftChunkEngine&> (getEngine()); }
    const DaftChunkEngine& getDsp() const noexcept { return static_cast<const DaftChunkEngine&> (const_cast<DaftChunkProcessor*> (this)->getEngine()); }

    //== loading ==================================================================
    void loadFile (const juce::File& file);
    void loadAudio (juce::AudioBuffer<float> audio, double sampleRate, const juce::String& name);
    bool isAnalysing() const noexcept { return loader.isBusy(); }
    float getAnalysisProgress() const noexcept { return loader.getProgress(); }
    /** Tests and offline renders: waits for the analysis and applies it now. */
    void waitForAnalysis();
    /** "" when fine; otherwise what went wrong (shown in the editor). */
    const juce::String& getStatus() const noexcept { return status; }

    /** The current kit (null: no sample). Message thread. */
    std::shared_ptr<const Kit> getKit() const { return kit; }
    const LoadedSample* getSample() const noexcept { return kit != nullptr ? kit->sample.get() : nullptr; }

    //== slicing ==================================================================
    const SliceSettings& getSettings() const noexcept { return settings; }
    void setMode (SliceMode m);
    void setDivision (int d);
    void setSensitivity (float s);
    /** The tempo the grid uses: the user's, else the analysis's. */
    double getBpm() const noexcept;
    void setBpm (double bpm);     // <= 0: back to the analysis's
    void doubleBpm() { setBpm (getBpm() * 2.0); }
    void halveBpm() { setBpm (getBpm() * 0.5); }
    /** TAP: a tap now (seconds, any clock): after 3 taps, the tempo. */
    void tap (double seconds);
    double getFirstBeat() const noexcept; // samples
    void setFirstBeat (double samples);   // < 0: back to the analysis's
    /** The cuts moved by hand: boundary `index` (1 .. slices) to `samples`. */
    void moveCut (int index, int64_t samples);
    void addCut (int64_t samples);
    void removeCut (int index);
    bool hasUserCuts() const noexcept { return userCuts; }
    /** Back to the cuts the settings give (drops the hand edits). */
    void resetCuts();

    //== a slice's own settings (the TRANCHE page) ===============================
    SliceParams getSliceParams (int index) const;
    void setSliceParams (int index, const SliceParams& p);
    void resetSliceParams (int index) { setSliceParams (index, {}); }
    /** Slice `index`'s settings to every slice. */
    void applyToAllSlices (int index);
    /** The slice the TRANCHE page edits (the last one clicked). */
    int getSelectedSlice() const noexcept { return selected; }
    void selectSlice (int index);

    //== the host's tempo (SYNC) =================================================
    /** The host's tempo (0: none known: the standalone app, or no playhead). */
    double getHostBpm() const noexcept { return getDsp().getTelemetry().hostBpm.load (std::memory_order_relaxed); }
    int getSync() const noexcept;
    void setSync (int sync);
    /** TEMPO: the slices are being stretched to the host's tempo. */
    bool isStretching() const noexcept { return stretcher.isBusy() || stretchWanted; }
    /** Tests: applies the host tempo now and waits for the stretch. */
    void stretchNow();

    /** +1 at every new kit (the editor redraws). */
    int getKitVersion() const noexcept { return kitVersion; }

protected:
    void saveExtraState (juce::ValueTree& state) override;
    void loadExtraState (const juce::ValueTree& state) override;

private:
    void timerCallback() override;
    void takeAnalysis();
    void reslice();
    void publish (std::vector<Slice> slices);
    void freeRetired (bool force);
    void republish(); // the same slices, new settings
    void updateStretch (bool now);

    SampleLoader loader;
    StretchWorker stretcher;
    std::shared_ptr<const juce::AudioBuffer<float>> stretched; // the sample at the host's tempo (null: none)
    double stretchedFactor = 1.0, requestedFactor = 0.0, lastHostBpm = 0.0, hostStableSince = 0.0;
    int stretchId = 0;
    bool stretchWanted = false;
    std::shared_ptr<const LoadedSample> sample;
    std::shared_ptr<const Kit> kit;
    struct Retired { std::shared_ptr<const Kit> kit; double since; };
    std::vector<Retired> retired;
    SliceSettings settings;
    std::vector<int64_t> boundaries;   // the current cuts (0 .. end)
    std::vector<SliceParams> sliceParams;
    int selected = 0;
    bool userCuts = false;
    std::vector<double> taps;
    juce::String status, samplePath;
    int kitVersion = 0;

    // a state restored before its sample is analysed: its cuts wait for it
    std::vector<int64_t> pendingBoundaries;
    int64_t pendingLength = -1;
};
} // namespace daftchunk
