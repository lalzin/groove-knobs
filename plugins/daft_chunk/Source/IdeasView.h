#pragma once

#include "DaftChunkProcessor.h"

namespace daftchunk
{
/** The 8 idea slots, in a row: each with its steps in miniature, PLAY,
    the star (kept: the dice never replace it) and the cross. A click on a
    slot selects it (the SEQUENCE page edits it). */
class IdeaStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit IdeaStrip (DaftChunkProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onSelect;

private:
    enum Handle { wavHandle, midiHandle, kitHandle };
    juce::Rectangle<float> slot (int i) const;
    juce::Rectangle<float> handle (int i, int h) const;
    /** Writes the file(s) and hands them to the OS's drag and drop (into Ableton). */
    void dragOut (int i, int h);
    int pressedIdea = -1, pressedHandle = -1;
    bool dragging = false;
    juce::Rectangle<float> playButton (int i) const;
    juce::Rectangle<float> starButton (int i) const;
    juce::Rectangle<float> crossButton (int i) const;

    DaftChunkProcessor& proc;
    std::shared_ptr<const Kit> kit;
    int shownVersion = -1, shownStep = -2, shownKit = -1;
};

/** The selected idea's steps, 16 at a time (one bar; MESURE 1..4 above):
    click a step to put the selected slice there (again: empty it), the
    wheel changes its slice (shift: its pitch), right-click for the rest
    (reverse, repeat, pitch, cut). */
class StepGrid final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepGrid (DaftChunkProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> cell (int k) const; // k: 0..15 in the page
    juce::Rectangle<float> barButton (int b) const;
    int stepAt (juce::Point<float> p) const;   // the step index in the pattern, -1: none
    void edit (int k, const std::function<void (Step&)>& change);

    DaftChunkProcessor& proc;
    std::shared_ptr<const Kit> kit;
    int page = 0, shownVersion = -1, shownStep = -2, shownIdea = -2, shownKit = -1;
    float wheelAccum = 0.0f;
};
} // namespace daftchunk
