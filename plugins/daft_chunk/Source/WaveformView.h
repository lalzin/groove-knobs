#pragma once

#include "DaftChunkProcessor.h"

namespace daftchunk
{
/** DAFT CHUNK's colours (spec 7: tomato coral, cream, black; no blue, green,
    lime, teal, amber, lilac, neon pink or silver) and the slice types'. */
namespace colours
{
    const juce::Colour coral (0xffff5a36), cream (0xfffff4e0), black (0xff141414), panel (0xff1e1c1b), surface (0xff2a2725);
    juce::Colour forType (SliceType t);
    const char* letterFor (SliceType t); // K, S, H, V, T, B
} // namespace colours

/** The sample's waveform with its slices (coloured by type), the beat grid
    and beat 1. Drag a cut to move it; drag the "1" flag to move beat 1;
    double-click to add a cut; right-click (or alt-click) a cut to remove it;
    click a slice to hear it. Drop a file anywhere on the plug-in to load it.
    Zoom: the wheel or a pinch (around the mouse); shift + wheel or a sideways
    swipe moves along; the bar under it shows where you are (drag it,
    double-click: all of it), with - / + / TOUT. */
class WaveformView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveformView (DaftChunkProcessor& p);

    /** Called by the editor every frame: redraws when something changed. */
    void refresh();
    void setDropHighlight (bool on);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    /** The zoom: `factor` > 1 closer, around the sample under x. */
    void zoomAround (float x, double factor);
    void resetZoom();
    bool isZoomed() const noexcept { return viewLength > 0.0; }

private:
    double total() const noexcept;
    double visible() const noexcept { return viewLength > 0.0 ? viewLength : total(); }
    void clampView();
    juce::Rectangle<float> overviewArea() const;
    juce::Rectangle<float> zoomButton (int i) const; // 0: -, 1: +, 2: TOUT
    double viewStart = 0.0, viewLength = -1.0;   // samples; length <= 0: the whole sample
    bool draggingOverview = false;
    float xFor (double sample) const;
    double sampleFor (float x) const;
    juce::Rectangle<float> waveArea() const;
    int cutNear (float x) const;     // boundary index (1 .. n-1), -1: none
    bool onBeatFlag (juce::Point<float> p) const;
    int sliceAt (float x) const;

    DaftChunkProcessor& proc;
    std::shared_ptr<const Kit> kit;
    int shownVersion = -1, shownSlice = -2, shownSelected = -1;
    float shownProgress = -1.0f, shownPosition = -1.0f;
    juce::String shownStatus;
    bool shownBusy = false, dropHighlight = false;
    int hoverCut = -1, dragCut = -1;
    bool hoverFlag = false, dragFlag = false;
};

/** The slice keyboard: 16 keys visible (C1 up), scroll with the wheel or the
    arrows; each key in its slice's colour; click to hear it. */
class SliceKeys final : public juce::Component
{
public:
    explicit SliceKeys (DaftChunkProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::String noteName (int midi); // 36 -> "C1" (Ableton's naming)

private:
    static constexpr int visible = 16;
    juce::Rectangle<float> keyArea (int k) const;
    juce::Rectangle<float> arrow (bool right) const;
    void scrollBy (int keys);

    DaftChunkProcessor& proc;
    std::shared_ptr<const Kit> kit;
    int first = 0, shownVersion = -1, shownSlice = -2, shownSelected = -1;
};
} // namespace daftchunk
