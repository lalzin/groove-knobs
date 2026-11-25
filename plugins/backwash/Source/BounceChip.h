#pragma once

#include "BackwashBounce.h"

namespace bw
{
/** The WAV chip (left of the knob, under LIVE / LOCK): a sound dropped on
    Backwash (or picked with a click) is rendered offline with its swells;
    the chip then becomes a handle that drags the new WAV into the DAW.

        DROP ──drop / click──> 42 % ──done──> DRAG (drag out; click: show the file)
                                       └──error──> ! (tooltip: why) */
class BounceChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    BounceChip (BackwashProcessor& live, const gk::Palette& palette);
    ~BounceChip() override;

    /** Starts a render of `file` (ignored while one runs). */
    void start (const juce::File& file);
    bool isBusy() const noexcept { return job != nullptr; }
    /** A file is being dragged over the plugin. */
    void setDropHighlight (bool on);

    /** Where the WAVs go (tests, screenshots). */
    juce::File folder = bounce::defaultFolder();
    /** Shows a result as if a render had ended (screenshots). */
    void setResult (bounce::Result r)
    {
        result = std::move (r);
        updateTooltip();
        repaint();
    }
    /** Last result (tests). */
    const bounce::Result& getResult() const noexcept { return result; }
    /** Called on the message thread when a render ends. */
    std::function<void()> onFinished;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    static constexpr float captionHeight = 13.0f, chipHeight = 21.0f;

private:
    class Job;
    void finished (bounce::Result r);
    void updateTooltip();
    void choose();

    BackwashProcessor& live;
    const gk::Palette palette;
    std::unique_ptr<BackwashProcessor> renderer;
    std::unique_ptr<Job> job;
    std::atomic<float> progress { 0.0f };
    bounce::Result result;
    bool over = false, highlight = false, dragging = false;
    std::unique_ptr<juce::FileChooser> chooser;

    struct Repainter final : juce::Timer
    {
        BounceChip& c;
        explicit Repainter (BounceChip& chip) : c (chip) {}
        void timerCallback() override { c.repaint(); }
    } repainter { *this };
};

/** Drawn over the whole plugin while a file is dragged over it. */
class DropOverlay final : public juce::Component
{
public:
    explicit DropOverlay (const gk::Palette& p) : palette (p) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override;

private:
    const gk::Palette palette;
};
} // namespace bw
