#pragma once

#include "BackwashBounce.h"

namespace bw
{
/** The WAV chip (left of the knob, under LIVE / LOCK). Once Backwash has
    heard a whole bar (its LIVE memory), the chip turns into a handle: drag it
    into the DAW and you get the swells of that bar as a WAV loop, rendered
    at the moment of the drag with the current settings. Put it on a track
    from a bar line, loop it, and Backwash can go: no latency at all.

        WAV (waiting for a bar) ──a bar heard──> DRAG (drag out; click: show the last file) */
class BounceChip final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    BounceChip (BackwashProcessor& live, const gk::Palette& palette);

    /** Where the WAVs go (tests, screenshots). */
    juce::File folder = bounce::defaultFolder();
    /** Is there a bar to drag out? */
    bool isReady() const noexcept { return ready; }
    /** Screenshots: forces the state. */
    void setReadyForSnapshot (bool r) { ready = r; forced = true; updateTooltip(); repaint(); }

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    static constexpr float captionHeight = 13.0f, chipHeight = 21.0f;

private:
    void timerCallback() override;
    void updateTooltip();

    BackwashProcessor& live;
    const gk::Palette palette;
    juce::File lastFile;
    bool ready = false, forced = false, over = false, dragging = false;
};
} // namespace bw
