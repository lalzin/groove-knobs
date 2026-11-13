#pragma once

#include "DaftChunkProcessor.h"

namespace daftchunk
{
class WaveformView;
class SliceKeys;
class IdeaStrip;
class StepGrid;

/** DAFT CHUNK, 860 x 660 (spec 7, taller: the SON / TRANCHE / SEQUENCE pages and the idea strip). On top, the toolbar (CHARGER, the
    sample, the tempo with ÷2 / ×2 / TAP, the key, the slicing mode, the
    grid division, the sensitivity) and the waveform with its slices. In the
    middle: Chunky on the left, CHOP with DÉ, ENCORE and the style (step 3),
    the 8 idea slots on the right (step 3). At the bottom, the slice
    keyboard. Drop a file anywhere on it to load it. */
class DaftChunkEditor final : public gk::OneKnobEditor, public juce::FileDragAndDropTarget
{
public:
    explicit DaftChunkEditor (DaftChunkProcessor& p);
    ~DaftChunkEditor() override;

    static gk::Palette makePalette();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    /** 0: the SON page, 1: TRANCHE (the selected slice). */
    void showPage (int index);

    /** Screenshots: the waveform zoomed by `factor` around x (0..1 of its width). */
    void zoomWaveform (double factor, float at);

    /** Screenshots: everything up to date, no motion. */
    void snapView();

protected:
    void editorTick (double dt) override;

private:
    class Panels;
    class Pill;
    class MiniSlider;
    class InfoLabel;
    class SliceKnob;
    class FxMeter;

    void chooseFile();
    void showModeMenu();
    void showSyncMenu();
    void showStyleMenu();
    void showLengthMenu();
    void updateGenerator();
    void updateToolbar();
    void updateSlicePage();

    DaftChunkProcessor& proc;
    std::unique_ptr<Panels> panels;
    std::unique_ptr<WaveformView> wave;
    std::unique_ptr<SliceKeys> keys;
    std::unique_ptr<Pill> load, half, twice, tapButton, mode, dice, encore, style;
    std::array<std::unique_ptr<Pill>, 3> divisions;
    std::unique_ptr<MiniSlider> sensitivity;
    std::unique_ptr<InfoLabel> fileName, keyLabel;
    std::unique_ptr<juce::Label> bpm;
    std::unique_ptr<IdeaStrip> ideaStrip;
    std::unique_ptr<StepGrid> stepGrid;
    std::unique_ptr<MiniSlider> swing;
    std::unique_ptr<Pill> undoButton, redoButton, tabSequence, lockButton, capture, length, tabFx;
    std::vector<std::unique_ptr<gk::SmallKnob>> fxKnobs;
    std::unique_ptr<gk::ChoiceSelector> sweepSelector;
    std::unique_ptr<gk::ChoiceSelector> rateSelector;
    std::unique_ptr<FxMeter> fxMeter;
    std::unique_ptr<InfoLabel> ideaInfo;
    int shownIdeas = -1, shownGenKit = -1;
    juce::String shownStatus;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Pill> tabSound, tabSlice, reverse, resetSlice, applyAll;
    std::unique_ptr<InfoLabel> sliceTitle;
    std::vector<std::unique_ptr<gk::SmallKnob>> soundKnobs;
    std::unique_ptr<gk::ChoiceSelector> modeSelector, playSelector;
    std::array<std::unique_ptr<SliceKnob>, 5> sliceKnobs;
    int page = 0, shownSelected = -1, shownSliceVersion = -1;
    int shownVersion = -1;
    bool wasBusy = false, wasStretching = false;
    double shownHost = -1.0;
    int shownSync = -1;
    std::unique_ptr<Pill> sync;
};
} // namespace daftchunk
